// ── proteus_gb — Game Boy (DMG) unified firmware (SD menu + LCD/HDMI output) ──
//
// ONE firmware image that delivers:
//   1. Browse to choose a game (SD .gb menu on LCD).
//   2. Play on the local LCD (default).
//   3. Play on HDMI/DVI (hold SELECT at power-on).
//
// OUTPUT SELECTION
// ────────────────
// Hold SELECT during power-on (keep held until the 3 s USB-CDC settle passes)
//   → HDMI/DVI output for the game.
// Release SELECT at boot (or hold any other button)
//   → LCD output (default).
//
// SELECT is sampled from the raw GPIO before the button scan timer is started.
// The HPD/DDC pins on the Waveshare RP2350-PiZero schematic are NOT routed to
// any RP2350 GPIO (they terminate on pull-ups to VBUS), so true HPD auto-detect
// is hardware-impossible without board modifications.
//
// BOOT SEQUENCE (critical ordering)
// ──────────────────────────────────
// A. Stock clock → LCD init → splash on LCD.
// B. SD mount + enumerate .gb → SD menu (D-pad/A/Start) on LCD.
//    DVI is OFF during this whole phase. Reason: carlk3 SDIO uses PIO1+DMA;
//    DVI uses PIO0+DMA (DMA_IRQ_1). They can coexist IF on separate PIO
//    instances, but DVI::init() claims core1 for the TMDS encoder — we need
//    core1 free for the SD phase because the SD driver's PIO program runs on
//    core0 but its DMA completion handler fires on whichever core is alive.
//    Simpler and safer: keep DVI completely OFF while the SD is active.
// C. Stream the chosen .gb SD→QSPI-flash and run it from the XIP window.
//    All SD access happens here at stock clock; the ROM then lives in flash, so
//    the size limit is the flash region (~12 MiB) rather than RAM.
// D. After unmount: overclock 250 MHz + reparent clk_peri.
//    THEN bring up the game's output:
//      DVI:  DviDisplay::init() → claims PIO0/DMA_IRQ_1, launches core1 as
//            the TMDS encoder. Core0 drives present directly (no FIFO handoff).
//      LCD:  re-init the SPI display bus at the fast clock, launch core1 as the
//            St7789 present worker (FIFO handoff as in the original build).
// E. Game loop: gb_run_frame → minigb_apu → audio → present to chosen output.
//
// SRAM BUDGET (520 KB, NOT copy_to_ram)
// ──────────────────────────────────────
//   DviDisplay::m_fb[2][320×240×2]  ≈ 300 KB  (only populated in DVI mode)
//   sFrameBuf[160×144×2]            =  46 KB
//   struct gb_s                     ≈  18 KB
//   audio ring (PwmSpeaker)         ≈  32 KB
//   misc / stack / code-data        ≈  10 KB
//   ─────────────────────────────────────────
//   TOTAL (DVI mode, worst case)    ≈ 410 KB
//
// Audio: minigb_apu 32768 Hz → PwmSpeaker (DC-blocked, mono, ring-buffered).
// Core: Peanut-GB (single-header, MIT) — deltabeard/Peanut-GB.

// ── Peanut-GB configuration ───────────────────────────────────────────────────
#define ENABLE_SOUND                1
#define PEANUT_GB_HIGH_LCD_ACCURACY 0

extern "C" {
#include "minigb_apu.h"
}

// Bridge the global audio_read/write callbacks to the ctx-based minigb_apu API.
static struct minigb_apu_ctx g_apu;
extern "C" uint8_t audio_read(const uint16_t addr)
{
    return minigb_apu_audio_read(&g_apu, addr);
}
extern "C" void audio_write(const uint16_t addr, const uint8_t val)
{
    minigb_apu_audio_write(&g_apu, addr, val);
}

// Peanut-GB is header-only: one include pulls in both declarations + impl.
extern "C" {
#include "peanut_gb.h"
}

// ── Drivers ───────────────────────────────────────────────────────────────────
#include "drivers/audio/PwmSpeaker.h"
#include "drivers/display/St7789.h"
#include "drivers/input/Buttons.h"
#include "port/rp2350/flash/DiagStore.h"
#include "port/rp2350/flash/FlashRom.h"
#include "port/rp2350/flash/Personalization.h"
#include "port/rp2350/DviDisplay.h"
#include "port/rp2350/boards/pins.h"
#include "port/rp2350/phy/GpioButtonPort.h"
#include "port/rp2350/phy/PwmSpeakerOut.h"
#include "port/rp2350/phy/SpiDisplayBus.h"
#include "port/rp2350/sd/SdCard.h"

#include "hal/IAudioSink.h"
#include "hal/IDisplay.h"
#include "hal/Pixel.h"
#include "ui/ListMenu.h"
#include "ui/Painter.h"

extern "C" {
#include "diag/diag.h"
}

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/watchdog.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include <cstdio>
#include <cstring>

namespace
{
    // ── Frame geometry ────────────────────────────────────────────────────────
    constexpr unsigned kGbWidth  = 160;
    constexpr unsigned kGbHeight = 144;

    // Classic DMG greenscale in RGB565: white → light → dark → black.
    // Peanut-GB pixel byte: bits[1:0] = colour index (0=white, 3=black).
    constexpr uint16_t kPalette[4] = {
        0x7FEF,  // white  (index 0) — ~RGB(248,252,248)
        0x5DA5,  // light  (index 1) — ~RGB(96,208,48)
        0x24A3,  // dark   (index 2) — ~RGB(48,128,24)
        0x0000,  // black  (index 3)
    };

    // The 160×144 RGB565 frame, shared core0 (emu draws) → core1/DVI (present).
    alignas(4) uint16_t sFrameBuf[kGbWidth * kGbHeight];

    // ── Driver bindings (generic driver + concrete RP2350 PHY) ────────────────
    using GbLcd     = drivers::St7789<rp2350::SpiDisplayBus>;
    using GbButtons = drivers::Buttons<rp2350::GpioButtonPort>;
    using GbAudio   = drivers::PwmSpeaker<rp2350::PwmSpeakerOut>;
    using GameMenu  = ui::ListMenu<128, 64, 4>;   // ≤128 entries, names ≤63 chars

    // ── Peripheral singletons ─────────────────────────────────────────────────
    // One of each device; constructed at startup (no heap, off the stack). The
    // splash/menu always render on the LCD, so `display` is the LCD even in DVI
    // mode; the DVI surface is brought up later only for the game.
    hal::Surface             surf{sFrameBuf, kGbWidth, kGbHeight, kGbWidth};
    ui::Painter              painter(surf);

    rp2350::SpiDisplayBus    lcdBus;
    GbLcd                    display(lcdBus);
    rp2350::GpioButtonPort   buttonPort;
    GbButtons                input(buttonPort, 5);   // ~5 ms debounce at 1 ms scan
    rp2350::PwmSpeakerOut    pwmOut;
    GbAudio                  audio(pwmOut, pwmOut.wrap());

    sd::SdCard               card;
    GameMenu                 menu(6, 22, 148, 116);
    rp2350::FlashRom         flashRom;
    rp2350::DviDisplay       dviDriver;
    struct gb_s              gb;

    repeating_timer          sButtonScanTimer;
    repeating_timer          sAudioFeedTimer;

    // ── ROM in flash (XIP), read like a cartridge ─────────────────────────────
    const uint8_t*           sRomData = nullptr;   // XIP pointer (FlashRom::data())
    uint32_t                 sRomSize = 0;
    alignas(4) uint8_t       sFlashScratch[4096];   // sector-sized SD→flash staging

    // ── core1 LCD present worker ──────────────────────────────────────────────
    volatile uint32_t        sPresentCycles = 0;
    constexpr uint32_t       kMsgGo   = 1;
    constexpr uint32_t       kMsgDone = 2;

    // ── Timer ticks (background scan/drain) ───────────────────────────────────
    bool buttonScanTick(repeating_timer* timer)
    {
        static_cast<GbButtons*>(timer->user_data)->scan();
        return true;
    }

    bool audioFeedTick(repeating_timer* timer)
    {
        static_cast<GbAudio*>(timer->user_data)->feedSample();
        return true;
    }

    inline uint32_t dwtCyc()
    {
        return *reinterpret_cast<volatile uint32_t*>(0xE0001004u);
    }

    void enableDwtCounter()
    {
        *reinterpret_cast<volatile uint32_t*>(0xE000EDFCu) |= (1u << 24);  // TRCENA
        *reinterpret_cast<volatile uint32_t*>(0xE0001000u) |= (1u << 0);   // CYCCNTENA
    }

    // core1 entry, launched in LCD mode only (DVI mode keeps core1 for the TMDS
    // encoder and presents from core0). Per-frame handshake: pop GO → present →
    // push DONE; the loop's first pop receives the initial DONE pushed here.
    void core1Present()
    {
        enableDwtCounter();
        multicore_fifo_push_blocking(kMsgDone);  // initial: buffer is free
        for (;;)
        {
            (void)multicore_fifo_pop_blocking();   // wait GO from core0
            const uint32_t t0 = dwtCyc();
            display.present(sFrameBuf, kGbWidth, kGbHeight);
            sPresentCycles = dwtCyc() - t0;
            multicore_fifo_push_blocking(kMsgDone); // buffer released
        }
    }

    // ── Peanut-GB callbacks ───────────────────────────────────────────────────
    uint8_t gbRomRead(struct gb_s* /*gb*/, const uint_fast32_t addr)
    {
        return (sRomData != nullptr && addr < sRomSize) ? sRomData[addr] : 0xFF;
    }

    uint8_t gbCartRamRead(struct gb_s* /*gb*/, const uint_fast32_t /*addr*/)
    {
        return 0xFF;  // ROM-only cart
    }

    void gbCartRamWrite(struct gb_s* /*gb*/, const uint_fast32_t /*addr*/,
                        const uint8_t /*val*/)
    {
        // no cart RAM
    }

    void gbError(struct gb_s* /*gb*/, const enum gb_error_e /*err*/,
                 const uint16_t /*addr*/)
    {
        // ignore — keep running
    }

    // Called by Peanut-GB per scanline (lines 0..143); pixels[] holds 160 colour
    // indices in bits[1:0]. Writes straight into the shared framebuffer (core0
    // only during emulation, so no synchronisation needed).
    void gbLcdDrawLine(struct gb_s* /*gb*/, const uint8_t* pixels,
                       const uint_fast8_t line)
    {
        uint16_t* dst = sFrameBuf + static_cast<unsigned>(line) * kGbWidth;
        for (unsigned x = 0; x < kGbWidth; ++x)
        {
            dst[x] = kPalette[pixels[x] & 0x03u];
        }
    }

    // Map GamePi13 buttons to the GB joypad byte (active-LOW, 0=pressed).
    [[nodiscard]] uint8_t gbPad(const hal::InputFrame& in)
    {
        uint8_t p = 0xFF;  // all released
        if (in.isDown(hal::Button::A))      { p &= ~0x01u; }
        if (in.isDown(hal::Button::B))      { p &= ~0x02u; }
        if (in.isDown(hal::Button::Select)) { p &= ~0x04u; }
        if (in.isDown(hal::Button::Start))  { p &= ~0x08u; }
        if (in.isDown(hal::Button::Right))  { p &= ~0x10u; }
        if (in.isDown(hal::Button::Left))   { p &= ~0x20u; }
        if (in.isDown(hal::Button::Up))     { p &= ~0x40u; }
        if (in.isDown(hal::Button::Down))   { p &= ~0x80u; }
        return p;
    }

    // listRoms() filter: keep ".gb" (DMG) files, skip hidden/AppleDouble + ".gbc".
    bool collectGb(const sd::RomEntry& e, void* ctx)
    {
        const char* n = e.name;
        if (n[0] == '.') { return true; }
        const size_t len = std::strlen(n);
        if (len >= 3 && n[len - 3] == '.' &&
            (n[len - 2] | 0x20) == 'g' && (n[len - 1] | 0x20) == 'b')
        {
            (void)static_cast<GameMenu*>(ctx)->addItem(n);
        }
        return true;
    }

    // ── Small drawing helpers ─────────────────────────────────────────────────
    // Font8x8 glyphs are 8 px wide → centre text in the 160-px frame.
    void drawCentered(int y, const char* s, uint16_t color)
    {
        const int w = static_cast<int>(std::strlen(s)) * 8;
        painter.drawText((kGbWidth - w) / 2, y, s, color);
    }

    void clearScreen()
    {
        painter.fillRect(0, 0, kGbWidth, kGbHeight, hal::color::kBlack);
    }

    void showOnLcd()
    {
        display.present(sFrameBuf, kGbWidth, kGbHeight);
    }

    // Boot can't continue (no SD, bad ROM, output/emulator init failed). Show the
    // reason full-screen and keep blinking the status LED so the error is visible
    // both on the panel and on the board. The blink rate distinguishes the stage.
    [[noreturn]] void indicateError(const char* line1, const char* line2,
                                    uint32_t blinkMs)
    {
        clearScreen();
        drawCentered(52, line1, hal::color::kRed);
        if (line2 != nullptr) { drawCentered(70, line2, hal::color::kWhite); }
        showOnLcd();
        for (;;)
        {
            gpio_put(bsp::kLedStatus, 1); sleep_ms(blinkMs);
            gpio_put(bsp::kLedStatus, 0); sleep_ms(blinkMs);
        }
    }

    // FlashRom progress callback: a simple LCD bar during the one-time SD→flash
    // stream (large ROMs over slow single-block SD can take a few seconds).
    void flashProgress(uint32_t done, uint32_t total)
    {
        if (total == 0) { return; }
        const int barW   = 140;
        const int filled = static_cast<int>(static_cast<uint64_t>(done) * barW / total);
        painter.fillRect(10, 80, barW, 10, hal::color::kBlack);
        painter.fillRect(10, 80, filled, 10, hal::color::kGreen);
        showOnLcd();
    }

    // ── A. Boot-time output detection (held-button override) ──────────────────
    // HPD/DDC are NOT wired, so HDMI cannot self-detect. Latch SELECT across the
    // whole 3 s USB-CDC settle so a momentary press is never missed.
    [[nodiscard]] bool dviRequestedAtBoot()
    {
        gpio_init(bsp::kKeySelect);
        gpio_set_dir(bsp::kKeySelect, GPIO_IN);
        gpio_pull_up(bsp::kKeySelect);
        bool useDvi = false;
        for (int i = 0; i < 300; ++i)          // 300 × 10 ms = 3 s settle + sample
        {
            if (gpio_get(bsp::kKeySelect) == 0) { useDvi = true; }
            sleep_ms(10);
        }
        return useDvi;
    }

    // Snapshot which buttons are held right now, as a comma list ("none" if idle).
    // Shown on the splash so the boot output choice is self-diagnosing on the LCD.
    void readHeldButtons(char* out, size_t cap)
    {
        struct Key { const char* name; uint32_t pin; };
        const Key keys[] = {
            {"Up",bsp::kKeyUp},   {"Down",bsp::kKeyDown}, {"Left",bsp::kKeyLeft},
            {"Right",bsp::kKeyRight}, {"A",bsp::kKeyA},    {"B",bsp::kKeyB},
            {"X",bsp::kKeyX},     {"Y",bsp::kKeyY},        {"L",bsp::kKeyL},
            {"R",bsp::kKeyR},     {"Start",bsp::kKeyStart},{"Select",bsp::kKeySelect},
        };
        for (const Key& k : keys)
        {
            gpio_init(k.pin); gpio_set_dir(k.pin, GPIO_IN); gpio_pull_up(k.pin);
        }
        sleep_ms(2);

        std::snprintf(out, cap, "none");
        size_t off = 0;
        for (const Key& k : keys)
        {
            if (gpio_get(k.pin) != 0) { continue; }
            const int n = std::snprintf(out + off, cap - off, "%s%s",
                                        off ? "," : "", k.name);
            if (n <= 0) { break; }
            off += static_cast<size_t>(n);
            if (off >= cap - 1) { break; }
        }
    }

    // ── A. Diagnostics: persistent boot/reset tracking ────────────────────────
    // Records every boot in a flash-backed capsule via the diagnostics library
    // (github Mrunmoy/diagnostics): counts boots and flags abnormal resets
    // (watchdog/brownout) that should never happen on a healthy unit. Resets
    // become observable state, never a silent event — the same principle as
    // indicateError (report, don't loop). See [[no-reboot-loop-on-error]].
    const char* resetReasonName(enum diag_reset_reason r)
    {
        switch (r)
        {
            case DIAG_RESET_REASON_POWER_ON: return "POWER_ON";
            case DIAG_RESET_REASON_SOFTWARE: return "SOFTWARE";
            case DIAG_RESET_REASON_WATCHDOG: return "WATCHDOG";
            case DIAG_RESET_REASON_BROWNOUT: return "BROWNOUT";
            case DIAG_RESET_REASON_EXTERNAL: return "EXTERNAL";
            case DIAG_RESET_REASON_UPDATE:   return "UPDATE";
            case DIAG_RESET_REASON_FAULT:    return "FAULT";
            default:                         return "UNKNOWN";
        }
    }

    // Records this boot and logs the running totals to the UART/USB log. The data
    // is NOT shown on the LCD; it lives in the flash capsule for a PC diagnostics
    // tool to extract, and is echoed here for live monitoring.
    void initDiagnostics()
    {
        static struct diag_context_storage storage;
        static struct diag_context*        ctx = nullptr;
        const struct diag_config           cfg = {};

        if (diag_init(&storage, &cfg, &ctx) != DIAG_OK)
        {
            printf("diag: init failed\n");
            return;
        }

        struct diag_lifecycle_config lc = {};
        lc.reset_counter_policy = DIAG_RESET_COUNTER_POLICY_EVERY_N;
        lc.reset_count_interval = 1;   // persist on every boot
        diag_lifecycle_attach(ctx, &lc);
        diag_storage_attach(ctx, rp2350::diagFlashStorage());

        (void)diag_load(ctx);          // restore the running count (ignored on first boot)
        const enum diag_reset_reason reason =
            watchdog_caused_reboot() ? DIAG_RESET_REASON_WATCHDOG : DIAG_RESET_REASON_POWER_ON;
        diag_lifecycle_observe_reset(ctx, reason);
        (void)diag_save(ctx);          // persist this boot into the flash capsule

        struct diag_lifecycle_snapshot snap = {};
        diag_lifecycle_get(ctx, &snap);
        printf("diag: boot #%lu last_reset=%s abnormal_total=%lu\n",
               static_cast<unsigned long>(snap.reset_count),
               resetReasonName(snap.last_reset_reason),
               static_cast<unsigned long>(snap.abnormal_reset_count));
    }

    // ── B. Bring up the LCD + input + the splash screen ───────────────────────
    void initPeripherals()
    {
        gpio_init(bsp::kLedStatus);
        gpio_set_dir(bsp::kLedStatus, GPIO_OUT);

        lcdBus.init();
        display.init();
        display.setBacklight(255);
        buttonPort.init();
        if (!add_repeating_timer_ms(-1, &buttonScanTick, &input, &sButtonScanTimer))
        {
            panic("failed to start button scan timer");
        }
    }

    void showSplash(bool useDvi, const char* bootKeys, const char* recipient)
    {
        clearScreen();
        drawCentered(20, "RP2350 RETRO", hal::color::kGreen);
        drawCentered(34, "HANDHELD",     hal::color::kGreen);

        char line[56];
        if (recipient[0] != '\0')   // personalized per board (tools/personalize.py)
        {
            std::snprintf(line, sizeof line, "For %s", recipient);
            drawCentered(58, line, hal::color::kYellow);
        }
        drawCentered(76, "Built by Sam", hal::color::kWhite);
        drawCentered(90, "June 2026",    hal::color::kCyan);

        drawCentered(108, useDvi ? "OUT: HDMI" : "OUT: LCD",
                     useDvi ? hal::color::kYellow : hal::color::kWhite);
        std::snprintf(line, sizeof line, "held: %s", bootKeys);
        drawCentered(122, line, hal::color::kCyan);
        showOnLcd();
        sleep_ms(3000);
    }

    // ── C. Mount the SD card and list the .gb files into the menu ─────────────
    // Returns true if the card mounted and holds at least one .gb game; false
    // otherwise (caller then offers the flash-cached game, see playCachedGame).
    [[nodiscard]] bool enumerateRoms()
    {
        const sd::Status ms = card.mount();
        printf("sd mount=%d\n", static_cast<int>(ms));
        if (sd::ok(ms))
        {
            (void)card.listRoms("/", collectGb, &menu);
        }
        printf("%u .gb files found\n", static_cast<unsigned>(menu.count()));
        return sd::ok(ms) && menu.count() > 0;
    }

    // No usable SD card: if a game was cached to flash on a previous play (the same
    // card is moved board-to-board for first play), offer to run it from flash.
    // Returns true and arms the ROM if the player accepts; false if nothing is
    // cached or the player declines.
    [[nodiscard]] bool playCachedGame()
    {
        card.unmount();   // release the SD stack; this path runs entirely from flash

        char     name[40];
        uint32_t size = 0;
        if (!flashRom.cachedInfo(name, sizeof name, &size)) { return false; }

        clearScreen();
        drawCentered(28, "NO SD CARD", hal::color::kYellow);
        drawCentered(52, "Play from flash?", hal::color::kWhite);
        drawCentered(70, name, hal::color::kGreen);
        drawCentered(100, "A = yes   B = no", hal::color::kCyan);
        showOnLcd();

        for (;;)
        {
            const hal::InputFrame in = input.poll();
            if (in.isDown(hal::Button::A) || in.isDown(hal::Button::Start))
            {
                flashRom.adoptCached(size);
                sRomData = flashRom.data();
                sRomSize = flashRom.size();
                printf("playing cached '%s' (%lu B) from flash\n",
                       name, static_cast<unsigned long>(size));
                return true;
            }
            if (in.isDown(hal::Button::B)) { return false; }
            sleep_ms(40);
        }
    }

    // Forgiving menu navigation: EITHER axis moves (Up/Left = prev, Down/Right =
    // next), the list wraps (one working direction reaches every game), ANY face
    // button selects, holding a direction auto-repeats. Returns the chosen index.
    [[nodiscard]] int runMenu()
    {
        constexpr uint32_t kRepeatDelay = 8;   // ~0.5 s held before auto-repeat
        constexpr uint32_t kRepeatEvery = 3;   // then a step ~every 180 ms
        const int n = static_cast<int>(menu.count());

        int      chosen = -1;
        bool     pPrev = false, pNext = false, pSel = false;
        uint32_t holdTicks = 0;

        while (chosen < 0)
        {
            clearScreen();
            drawCentered(4, "SELECT A GAME", hal::color::kYellow);
            menu.paint(painter);
            drawCentered(132, "Dpad=move A=play", hal::color::kCyan);
            showOnLcd();

            const hal::InputFrame in = input.poll();
            const bool prev = in.isDown(hal::Button::Up)   || in.isDown(hal::Button::Left);
            const bool next = in.isDown(hal::Button::Down) || in.isDown(hal::Button::Right);
            const bool sel  = in.isDown(hal::Button::A) || in.isDown(hal::Button::B) ||
                              in.isDown(hal::Button::X) || in.isDown(hal::Button::Y) ||
                              in.isDown(hal::Button::Start);

            if (prev || next)
            {
                const int d = prev ? -1 : 1;
                const bool pressEdge = (prev && !pPrev) || (next && !pNext);
                if (pressEdge)
                {
                    if (n > 0) { menu.setSelectedIndex(((menu.selectedIndex() + d) % n + n) % n); }
                    holdTicks = 0;
                }
                else if (++holdTicks >= kRepeatDelay &&
                         (holdTicks - kRepeatDelay) % kRepeatEvery == 0)
                {
                    if (n > 0) { menu.setSelectedIndex(((menu.selectedIndex() + d) % n + n) % n); }
                }
            }
            else
            {
                holdTicks = 0;
            }

            if (sel && !pSel) { chosen = menu.selectedIndex(); }
            pPrev = prev; pNext = next; pSel = sel;
            sleep_ms(60);
        }
        return chosen;
    }

    // Stream the chosen .gb SD→QSPI-flash, then point the GB core at the XIP
    // window. This is the ONLY flash write and it happens here — after the menu,
    // BEFORE any core1 launch and the overclock — so flash_safe_execute has no
    // concurrent XIP user (PICO_FLASH_ASSUME_CORE1_SAFE=1). The card is already
    // mounted from the menu phase. Shows an on-screen error on any failure.
    void loadRom(int chosen)
    {
        char path[80];
        std::snprintf(path, sizeof path, "/%s",
                      menu.itemText(static_cast<size_t>(chosen)));

        clearScreen();
        drawCentered(56, "LOADING...", hal::color::kWhite);
        showOnLcd();

        rp2350::FlashRom::Status fs = rp2350::FlashRom::Status::OpenFailed;
        if (sd::ok(card.open(path)))
        {
            const uint32_t fsz = card.openFileSize();
            if (flashRom.cachedMatches(path, fsz))
            {
                flashRom.adoptCached(fsz);       // same ROM already in flash
                fs = rp2350::FlashRom::Status::Ok;
                printf("'%s' already in flash (%lu B) — skip reflash\n",
                       path, (unsigned long)fsz);
                clearScreen();
                drawCentered(64, "CACHED", hal::color::kGreen);
                showOnLcd();
            }
            else
            {
                printf("streaming %lu bytes SD->flash (XIP)\n", (unsigned long)fsz);
                fs = flashRom.flashOpenFileToXip(card, path, sFlashScratch,
                                                 sizeof sFlashScratch, flashProgress);
            }
            card.close();
        }
        card.unmount();   // ROM lives in flash now; free the card before DVI/overclock

        // Cartridge validity: the Nintendo logo at 0x104 always starts CE ED 66 66
        // (also confirms the XIP program took — an unprogrammed region reads 0xFF).
        const bool romOk = rp2350::FlashRom::ok(fs) && flashRom.valid() &&
                           flashRom.size() >= 0x150 &&
                           flashRom.data()[0x104] == 0xCE &&
                           flashRom.data()[0x105] == 0xED;
        printf("flashed '%s' status=%ld size=%lu ok=%d\n", path, (long)fs,
               (unsigned long)flashRom.size(), romOk ? 1 : 0);

        if (!romOk)
        {
            indicateError("LOAD FAILED",
                            fs == rp2350::FlashRom::Status::TooLarge ? "ROM > 12 MB"
                                                                    : "bad ROM/SD",
                            120);
        }
        sRomData = flashRom.data();
        sRomSize = flashRom.size();
    }

    // ── D. Overclock + start audio + bring up the chosen output ───────────────
    // Overclock to 250 MHz (fall back to 200 MHz), then reparent clk_peri → clk_sys
    // so the LCD SPI divisor is recomputed. The GB core is light, so 250 is ample;
    // 300 MHz is avoided because it is marginal on some RP2350 silicon (faults
    // mid-frame → a partial frame then a freeze). DVI::init() overrides clocks again
    // to its own bit-clock rate.
    void raiseSystemClock()
    {
        if (!set_sys_clock_khz(250000, false))
        {
            set_sys_clock_khz(200000, false);
        }
        clock_configure(clk_peri, 0,
                        CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                        clock_get_hz(clk_sys), clock_get_hz(clk_sys));
        printf("clk_sys=%lu Hz\n", static_cast<unsigned long>(clock_get_hz(clk_sys)));
    }

    // The PWM carrier scales with clk_sys, so audio must come up after the
    // overclock and before the game loop starts producing samples.
    void startAudio()
    {
        minigb_apu_audio_init(&g_apu);
        pwmOut.init();
        if (!add_repeating_timer_us(-1000000 / static_cast<int64_t>(hal::kAudioSampleRateHz),
                                    &audioFeedTick, &audio, &sAudioFeedTimer))
        {
            panic("failed to start audio feed timer");
        }
    }

    // Bring up the game output. DVI: init libdvi + launch the core1 TMDS encoder
    // (core0 then presents directly). LCD: re-init at the fast clock and launch
    // the core1 present worker. Shows an on-screen error if DVI init fails.
    void bringUpOutput(bool useDvi)
    {
        if (!useDvi)
        {
            // LCD: re-init at the new (fast) peripheral clock so SPI recomputes its
            // divider, then launch the core1 present worker.
            lcdBus.init();
            display.init();
            display.setBacklight(255);
            multicore_launch_core1(core1Present);
            // Do NOT consume core1's initial DONE here: the loop's first pop must
            // receive it (pop DONE → push GO), otherwise the handshake deadlocks.
            return;
        }

        // DVI: SD is fully unmounted by now, so carlk3 (PIO1/DMA_IRQ_0) cannot
        // collide with libdvi (PIO0/DMA_IRQ_1). The LCD keeps working on its own
        // bus, so we show progress there while serial is unreliable at 252 MHz.
        clearScreen();
        drawCentered(56, "HDMI: init...", hal::color::kYellow);
        showOnLcd();

        if (dviDriver.init() != 0)
        {
            indicateError("HDMI INIT FAIL", nullptr, 120);
        }
        printf("DVI up (640x480p60, clk_sys=%lu Hz)\n",
               static_cast<unsigned long>(clock_get_hz(clk_sys)));
        clearScreen();
        drawCentered(50, "HDMI up", hal::color::kGreen);
        drawCentered(64, "starting game", hal::color::kWhite);
        showOnLcd();
    }

    // ── Peanut-GB init ────────────────────────────────────────────────────────
    void initEmulator()
    {
        const enum gb_init_error_e err = gb_init(
            &gb, gbRomRead, gbCartRamRead, gbCartRamWrite, gbError, nullptr);
        printf("gb_init=%d\n", static_cast<int>(err));
        if (err != GB_INIT_NO_ERROR)
        {
            indicateError("GB INIT FAIL", nullptr, 100);
        }
        gb_init_lcd(&gb, gbLcdDrawLine);
    }

    // One-pole DC blocker (high-pass ~13 Hz) + stereo→mono downmix. minigb's
    // square channels carry a duty-dependent DC offset that STEPS at every note
    // on/off → a click per note on the unfiltered speaker; removing DC turns each
    // step into inaudible sub-bass decay, so notes start cleanly.
    void mixAudioFrame(const audio_sample_t* stereo, int16_t* mono, unsigned samples)
    {
        static float dcInPrev = 0.0f, dcOutPrev = 0.0f;
        constexpr float kDcR = 0.9975f;
        for (unsigned i = 0; i < samples; ++i)
        {
            const int32_t mix = (static_cast<int32_t>(stereo[2u * i]) +
                                 static_cast<int32_t>(stereo[2u * i + 1u])) >> 1;
            const float x = static_cast<float>(mix);
            const float y = x - dcInPrev + kDcR * dcOutPrev;
            dcInPrev = x; dcOutPrev = y;
            int32_t o = static_cast<int32_t>(y);
            if (o > 32767)       { o = 32767;  }
            else if (o < -32768) { o = -32768; }
            mono[i] = static_cast<int16_t>(o);
        }
    }

    // Push the current framebuffer to the active output. DVI: CPU centred blit +
    // present from core0 (the DMA blit path conflicts with the still-claimed SD/
    // libdvi DMA channels). LCD: hand the frame to the core1 worker and sync.
    void presentFrame(bool useDvi, uint64_t& accPresentCyc)
    {
        const uint32_t tp0 = dwtCyc();
        if (useDvi)
        {
            dviDriver.presentFrame(sFrameBuf, kGbWidth, kGbHeight, 0);
            accPresentCyc += static_cast<uint32_t>(dwtCyc() - tp0);
        }
        else
        {
            (void)multicore_fifo_pop_blocking();   // wait until core1 released buf
            multicore_fifo_push_blocking(kMsgGo);
            accPresentCyc += sPresentCycles;       // sampled from the previous frame
        }
    }

    // Print the per-second frame + audio telemetry, then reset the accumulators.
    void reportTelemetry(bool useDvi, uint64_t accWorkCyc, uint64_t accPresentCyc,
                         uint64_t wallStart)
    {
        const uint64_t clkHz  = clock_get_hz(clk_sys);
        const uint64_t wallNs = (time_us_64() - wallStart) * 1000ull / 60u;
        const uint64_t fps    = wallNs ? (1000000000ull / wallNs) : 0u;
        const uint64_t workNs = accWorkCyc    * 1000000000ull / (clkHz * 60u);
        const uint64_t presNs = accPresentCyc * 1000000000ull / (clkHz * 60u);
        const GbAudio::Stats as = audio.stats();
        printf("gb/fps: paced=%llu | work=%lluus present=%lluus out=%s\n",
               static_cast<unsigned long long>(fps),
               static_cast<unsigned long long>(workNs / 1000u),
               static_cast<unsigned long long>(presNs / 1000u),
               useDvi ? "HDMI" : "LCD");
        printf("gb/audio: under=%lu drop=%lu pushed=%lu consumed=%lu fill=%lu\n",
               static_cast<unsigned long>(as.underruns),
               static_cast<unsigned long>(as.dropped),
               static_cast<unsigned long>(as.pushed),
               static_cast<unsigned long>(as.consumed),
               static_cast<unsigned long>(as.fill));
    }

    // ── E. Game loop ──────────────────────────────────────────────────────────
    [[noreturn]] void runGameLoop(bool useDvi)
    {
        printf("running ROM=%lu bytes out=%s\n",
               static_cast<unsigned long>(sRomSize), useDvi ? "HDMI" : "LCD");
        enableDwtCounter();   // core0 DWT measures emulation + audio work per frame

        // GB realtime ≈ 59.727 Hz; pace the loop so audio production ≈ consumption.
        constexpr uint64_t kFrameUs = 16743;   // 1e6 / 59.727
        static audio_sample_t apuStereo[AUDIO_SAMPLES_TOTAL];   // L+R interleaved
        static int16_t        apuMono[AUDIO_SAMPLES];

        uint64_t accWorkCyc    = 0;
        uint64_t accPresentCyc = 0;
        uint32_t frameCount    = 0;
        uint32_t tick          = 0;
        uint64_t nextDeadline  = time_us_64() + kFrameUs;
        uint64_t wallStart     = time_us_64();

        for (;;)
        {
            const uint32_t t0 = dwtCyc();

            gb.direct.joypad = gbPad(input.poll());
            gb_run_frame(&gb);

            minigb_apu_audio_callback(&g_apu, apuStereo);
            mixAudioFrame(apuStereo, apuMono, AUDIO_SAMPLES);
            audio.push(apuMono, AUDIO_SAMPLES);

            presentFrame(useDvi, accPresentCyc);

            accWorkCyc += static_cast<uint32_t>(dwtCyc() - t0);
            ++frameCount;
            gpio_put(bsp::kLedStatus, tick & 1u);
            ++tick;

            const uint64_t now = time_us_64();
            if (now < nextDeadline) { sleep_us(nextDeadline - now); }
            nextDeadline += kFrameUs;

            if (frameCount >= 60)
            {
                reportTelemetry(useDvi, accWorkCyc, accPresentCyc, wallStart);
                accWorkCyc = accPresentCyc = 0;
                frameCount = 0;
                wallStart  = time_us_64();
            }
        }
    }

}  // namespace

// ── main ──────────────────────────────────────────────────────────────────────
int main()
{
    stdio_init_all();

    const bool useDvi = dviRequestedAtBoot();
    char bootKeys[48];
    readHeldButtons(bootKeys, sizeof bootKeys);
    printf("output=%s  held=%s\n", useDvi ? "HDMI/DVI" : "LCD", bootKeys);

    initDiagnostics();        // A. persistent boot/reset tracking (flash + UART log)

    char recipient[rp2350::kRecipientMax + 1];
    (void)rp2350::readRecipient(recipient, sizeof recipient);   // empty string if unprogrammed
    printf("recipient=%s\n", recipient[0] ? recipient : "(none)");

    initPeripherals();        // A. LCD + input + scan timer
    showSplash(useDvi, bootKeys, recipient);

    // C. Pick + load a game. With an SD card, browse and stream the choice to flash;
    // without one, offer the game cached in flash from a previous play.
    if (enumerateRoms())
    {
        loadRom(runMenu());   // stream SD→flash→XIP (shows error on failure)
    }
    else if (!playCachedGame())
    {
        indicateError("NO SD CARD", "no game in flash", 200);
    }

    raiseSystemClock();       // D. overclock, then audio + the chosen output
    startAudio();
    bringUpOutput(useDvi);
    initEmulator();

    runGameLoop(useDvi);      // E. never returns
}
