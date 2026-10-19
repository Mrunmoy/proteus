// ── Proteus TestAgent — rp2350 tester firmware ──────────────────────────────
// Event-driven (reactor) manufacturing-test agent. TinyUSB CDC carries the
// aether RPC link; aether-lite dispatches requests into the portable OP handlers
// (src/app/TestAgent.cpp). Each op runs a REAL product driver — the SAME generic
// St7789 / Buttons / PwmSpeaker the gift app ships (driver-identity) —
// and returns the RAW observation. The PC station judges. The device never declares
// the suite and never returns a verdict.
//
// Identity handlers live here (they need hardware): GetIdentity reads the silicon
// chip id; GetDeviceInfo reads the device-info block from flash via XIP. picotool
// writes that block in BOOTSEL — the firmware only reads it.

#include "app/TestAgent.h"
#include "boards/pins.h"
#include "common/device_info.h"
#include "drivers/audio/PwmSpeaker.h"
#include "drivers/display/St7789.h"
#include "drivers/input/Buttons.h"
#include "hal/Buttons.h"
#include "hal/IAudioSink.h"
#include "hal/Pixel.h"
#include "port/rp2350/DviDisplay.h"
#include "port/rp2350/phy/GpioButtonPort.h"
#include "port/rp2350/phy/PwmSpeakerOut.h"
#include "port/rp2350/phy/SpiDisplayBus.h"
#include "port/rp2350/sd/SdCard.h"

#include "hardware/gpio.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "pico/unique_id.h"

#include "tusb.h"

extern "C"
{
#include "aether_lite_dispatch.h"
#include "aether_lite_types.h"
#include "TestStation.h"   // generated aether-lite dispatch
}

#include <cstdio>
#include <cstring>

namespace
{
    constexpr uint32_t kFwVersion = 0x00010000u;             // 1.0.0
    constexpr uint32_t kProtoHash = TEST_STATION_SERVICE_ID;  // schema id

    constexpr uint8_t kDbgItf = 0;   // CDC0 — debug log + reset
    constexpr uint8_t kRpcItf = 1;   // CDC1 — binary aether RPC

    // ── Real product drivers — the ones the gift app ships (driver-identity) ──
    // The exact generic drivers + RP2350 PHYs that proteus_gb runs, so the test
    // exercises the shipping code paths, not a parallel copy.
    constexpr unsigned    kAudioRingCap = 16384;   // power of two; matches the gift app

    rp2350::SpiDisplayBus  g_lcdBus;
    drivers::St7789<rp2350::SpiDisplayBus> g_lcd(g_lcdBus);

    rp2350::GpioButtonPort g_buttonPort;
    drivers::Buttons<rp2350::GpioButtonPort> g_buttons(g_buttonPort, 5);  // ~5 ms debounce

    rp2350::PwmSpeakerOut  g_pwmOut;
    drivers::PwmSpeaker<rp2350::PwmSpeakerOut, kAudioRingCap> g_audio(g_pwmOut, g_pwmOut.wrap());

    sd::SdCard         g_sd;          // SD test mounts/reads this on demand
    rp2350::DviDisplay g_dvi;         // HDMI test brings this up on demand
    bool               g_dviUp = false;

    // Last debounced held mask (for the ScanButtons raw-read op). Button PRESS/RELEASE
    // edges are PUSHED to the PC as al_send_notify(kButtonEventMsg) so the manufacturing
    // button test is fully event-driven — no polling, no press-and-click race.
    uint16_t              g_lastHeld      = 0;
    uint32_t              g_lastScanMs    = 0;        // 1 ms gate for the button scan
    constexpr uint32_t    kButtonEventMsg = 0x10u;   // notify payload: [button u8][type u8]

    // Audio gating: the speaker stays silent until a tone test. The feed timer only
    // runs while a tone is playing — started in hookPlayTone, cancelled once the tone
    // has drained — so there is no idle PWM carrier buzzing on the speaker.
    repeating_timer       g_audioFeedTimer{};
    bool                  g_audioFeeding  = false;
    uint32_t              g_toneOffMs     = 0;        // when to silence audio post-tone (0=off)

    bool audioFeedTick(repeating_timer* /*timer*/)
    {
        g_audio.feedSample();
        return true;
    }

    void audioStart()
    {
        if (g_audioFeeding) { return; }
        g_audioFeeding = add_repeating_timer_us(
            -1000000 / static_cast<int64_t>(hal::kAudioSampleRateHz),
            &audioFeedTick, nullptr, &g_audioFeedTimer);
    }

    void audioStop()
    {
        if (!g_audioFeeding) { return; }
        cancel_repeating_timer(&g_audioFeedTimer);
        g_audioFeeding = false;
        g_pwmOut.setLevel(g_pwmOut.wrap() / 2);   // mid-rail = silent at rest
    }

    // ── aether-lite HAL: three functions over CDC1 (RPC) ────────────────────
    size_t halRxRead(uint8_t* buf, size_t maxBytes, void* /*ctx*/)
    {
        uint32_t avail = tud_cdc_n_available(kRpcItf);
        if (avail == 0) { return 0; }
        if (avail > maxBytes) { avail = (uint32_t)maxBytes; }
        return tud_cdc_n_read(kRpcItf, buf, avail);
    }

    int halTxWrite(const uint8_t* buf, size_t len, void* /*ctx*/)
    {
        size_t sent = 0;
        while (sent < len)
        {
            uint32_t n = tud_cdc_n_write(kRpcItf, buf + sent, (uint32_t)(len - sent));
            sent += n;
            if (n == 0) { tud_task(); }   // FIFO full — let USB drain, then retry
        }
        tud_cdc_n_write_flush(kRpcItf);
        return 0;
    }

    uint32_t halGetTickMs() { return to_ms_since_boot(get_absolute_time()); }

    // ── Op hooks — run the real driver, return the raw observation ──────────

    bool hookSetLed(bool on)
    {
        // GP28 is the status LED; the continuity op re-purposes it (sequential).
        gpio_set_function(bsp::kLedStatus, GPIO_FUNC_SIO);
        gpio_set_dir(bsp::kLedStatus, GPIO_OUT);
        gpio_put(bsp::kLedStatus, on);
        return on;
    }

    // Pattern 5 = fine vertical colour ruler for the tearing test. A striped 60x60
    // source scales 4x to fill the panel (~4px bars), so a horizontal SPI-desync —
    // a marginal LCD joint letting a button edge couple into DC/SCK — slides the
    // bars visibly. The solid fills (0..4) cannot reveal a shift: a shifted solid
    // colour looks identical, which is exactly why bad boards passed the RGB check.
    const uint16_t* lcdRuler(uint16_t& sideOut)
    {
        constexpr uint16_t kSide = 60;
        static uint16_t buf[kSide * kSide];
        [[maybe_unused]] static const bool built = []
        {
            const uint16_t palette[8] = {
                hal::color::kWhite,  hal::color::kRed,   hal::color::kGreen,
                hal::color::kBlue,   hal::color::kYellow, hal::color::kCyan,
                hal::color::kMagenta, hal::color::kBlack,
            };
            for (uint16_t y = 0; y < kSide; ++y)
            {
                for (uint16_t x = 0; x < kSide; ++x)
                {
                    buf[y * kSide + x] = palette[x & 7u];
                }
            }
            return true;
        }();
        sideOut = kSide;
        return buf;
    }

    bool hookLcdShowPattern(uint8_t pattern)
    {
        if (pattern == 5)
        {
            uint16_t side = 0;
            const uint16_t* ruler = lcdRuler(side);
            g_lcd.present(ruler, side, side);
            return true;
        }

        uint16_t color = hal::color::kBlack;
        switch (pattern)
        {
            case 1:  color = hal::color::kRed;   break;
            case 2:  color = hal::color::kGreen; break;
            case 3:  color = hal::color::kBlue;  break;
            case 4:  color = hal::color::kWhite; break;
            default: color = hal::color::kBlack; break;
        }
        // A single source pixel scales to fill the whole panel: the driver maps the
        // 1x1 source across all 240x240 output pixels, so a solid colour fill needs
        // no framebuffer in this app.
        const uint16_t px = color;
        g_lcd.present(&px, 1, 1);
        return true;
    }

    uint32_t hookScanButtons()
    {
        // Raw-read op: the last debounced held mask. The button TEST uses the pushed
        // edge notifications (see the main loop), not this poll.
        return g_lastHeld;
    }

    bool hookPlayTone(uint32_t freqHz, uint32_t ms)
    {
        if (freqHz == 0) { return false; }
        audioStart();                       // run the feed timer just for this tone
        const uint32_t rate = hal::kAudioSampleRateHz;
        // Cap to what the audio ring holds so the push never blocks the USB loop.
        uint32_t total = (uint64_t)rate * ms / 1000u;
        if (total > kAudioRingCap) { total = kAudioRingCap; }

        const uint32_t halfPeriod = rate / (2u * freqHz);   // samples per half-cycle
        static int16_t buf[kAudioRingCap];
        const int16_t amp = 12000;
        for (uint32_t i = 0; i < total; ++i)
        {
            const bool high = halfPeriod ? ((i / halfPeriod) & 1u) == 0u : true;
            buf[i] = high ? amp : (int16_t)-amp;
        }
        g_audio.push(buf, total);
        g_toneOffMs = halGetTickMs() + ms + 150u;   // silence shortly after it finishes
        return true;
    }

    bool hookSdProbe(uint32_t* outFiles)
    {
        if (!sd::ok(g_sd.mount())) { return false; }
        uint32_t count = 0;
        (void)g_sd.listRoms("/", [](const sd::RomEntry&, void* ctx) -> bool {
            ++*static_cast<uint32_t*>(ctx);
            return true;
        }, &count);
        g_sd.unmount();
        if (outFiles) { *outFiles = count; }
        return true;
    }

    bool hookHdmiShowPattern(uint8_t /*pattern*/)
    {
        // Bring DVI up once (it raises clk_sys + launches the core1 TMDS encoder);
        // thereafter just redraw. The USB CDC keeps working — the USB PLL is
        // independent of the system clock — so the RPC reply still gets back.
        if (!g_dviUp)
        {
            if (g_dvi.init() != 0) { return false; }
            g_dviUp = true;
        }
        g_dvi.drawTestPattern();   // SMPTE-style colour bars
        g_dvi.present();
        return true;
    }
}

// ── Identity handlers (hardware-bound, so they live in the port) ────────────
extern "C" int TestStation_handleGetIdentity(Identity* id)
{
    pico_unique_board_id_t uid;
    pico_get_unique_board_id(&uid);

    std::memcpy(id->chipUid, uid.id, 8);
    id->fwVersion = kFwVersion;
    id->protoHash = kProtoHash;
    return AL_SUCCESS;
}

extern "C" int TestStation_handleGetDeviceInfo(DevInfo* info)
{
    ::DeviceInfo flash;   // the device_info.h contract struct (read-only via XIP)
    const bool ok = device_info_read(&flash);

    std::memset(info, 0, sizeof(*info));
    info->present = ok ? 1u : 0u;
    if (ok)
    {
        info->serialized = device_info_is_serialized(&flash) ? 1u : 0u;
        info->hwRev      = flash.hw_rev;
        info->vid        = flash.vid;
        info->pid        = flash.pid;
        std::memcpy(info->model,   flash.model,   sizeof(info->model)   - 1);
        std::memcpy(info->variant, flash.variant, sizeof(info->variant) - 1);
        std::memcpy(info->serial,  flash.serial,  sizeof(info->serial)  - 1);
    }
    return AL_SUCCESS;
}

// 1200-baud touch → reboot to BOOTSEL, so the tester firmware stays re-flashable
// over USB without the physical button.
extern "C" void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const* coding)
{
    (void)itf;
    if (coding->bit_rate == 1200) { reset_usb_boot(0, 0); }
}

int main()
{
    // Status LED starts off (a test resource, lit only by SetLed).
    gpio_init(bsp::kLedStatus);
    gpio_set_dir(bsp::kLedStatus, GPIO_OUT);
    gpio_put(bsp::kLedStatus, 0);

    // Bring up the real drivers — the ops below drive these directly.
    g_lcdBus.init();
    g_lcd.init();
    g_buttonPort.init();
    g_pwmOut.init();            // output configured; the feed timer stays off (silent)

    tusb_init();

    static al_hal_t hal;
    std::memset(&hal, 0, sizeof(hal));
    hal.rx_read     = halRxRead;
    hal.tx_write    = halTxWrite;
    hal.get_tick_ms = halGetTickMs;

    al_init(&hal);
    TestStation_register();

    app::TestAgentHooks hooks{};
    hooks.setLed         = hookSetLed;
    hooks.lcdShowPattern = hookLcdShowPattern;
    hooks.scanButtons    = hookScanButtons;
    hooks.playTone       = hookPlayTone;
    hooks.sdProbe        = hookSdProbe;
    hooks.hdmiShowPattern = hookHdmiShowPattern;
    app::testAgentInit(hooks);

    uint32_t lastLogMs = 0;
    uint32_t tick      = 0;

    while (true)
    {
        tud_task();   // service USB
        al_poll();    // parse + dispatch any complete RPC frames on CDC1

        // Sample the buttons at ~1 ms (debounce cadence) on the main loop, then drain
        // the debounced state + edges. Scan and poll run on the same context, so the
        // event ring needs no lock.
        const uint32_t scanNow = halGetTickMs();
        if (scanNow != g_lastScanMs) { g_lastScanMs = scanNow; g_buttons.scan(); }

        // Push each button edge to the PC as a notification (button + Down/Up type),
        // so the manufacturing test reacts to presses instead of polling/clicking.
        const hal::InputFrame btnFrame = g_buttons.poll();
        g_lastHeld = btnFrame.held;
        for (uint8_t i = 0; i < btnFrame.eventCount; ++i)
        {
            const uint8_t ev[2] = {
                static_cast<uint8_t>(btnFrame.events[i].button),
                static_cast<uint8_t>(btnFrame.events[i].type),
            };
            al_send_notify(TEST_STATION_SERVICE_ID, kButtonEventMsg, ev, sizeof(ev));
        }

        const uint32_t now = halGetTickMs();
        if (g_toneOffMs && now >= g_toneOffMs)   // tone finished → silence the output
        {
            audioStop();
            g_toneOffMs = 0;
        }
        if (now - lastLogMs >= 1000u)
        {
            lastLogMs = now;
            char line[48];
            const int n = std::snprintf(line, sizeof(line),
                                        "proteus testagent alive tick=%lu\r\n",
                                        static_cast<unsigned long>(tick++));
            if (n > 0)
            {
                tud_cdc_n_write(kDbgItf, line, static_cast<uint32_t>(n));
                tud_cdc_n_write_flush(kDbgItf);
            }
        }
    }
}
