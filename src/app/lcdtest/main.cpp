// ── lcd_buttontest — display/button coupling isolation test ─────────────────
// Shows STATIC vertical colour bars on the LCD and re-presents them forever. The
// button GPIOs are configured (input + pull-up, the same electrical condition as
// the game) but NEVER read — no scan, no emulator, no audio, no second core. So
// the only thing touching the panel is the continuous present.
//
// If pressing a button shifts/corrupts these static bars, the fault is purely
// ELECTRICAL coupling on the LCD wiring (a button's high→low edge nudging DC/SCK/
// CS on an adjacent trace) — it cannot be a software/timing bug, because nothing
// in software reacts to the button. Runs at the same 250 MHz / fast clk_peri as
// the game so the SPI signal conditions match.

#include "drivers/display/St7789.h"
#include "port/rp2350/boards/pins.h"
#include "port/rp2350/phy/SpiDisplayBus.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include <cstdint>

namespace
{
    constexpr unsigned kW = 160;
    constexpr unsigned kH = 144;
    uint16_t sBars[kW * kH];

    // 8 vertical bars (RGB565): white, yellow, cyan, green, magenta, red, blue, black.
    constexpr uint16_t kBar[8] = {
        0xFFFF, 0xFFE0, 0x07FF, 0x07E0, 0xF81F, 0xF800, 0x001F, 0x0000,
    };
}

int main()
{
    stdio_init_all();

    for (unsigned y = 0; y < kH; ++y)
    {
        for (unsigned x = 0; x < kW; ++x)
        {
            sBars[y * kW + x] = kBar[(x * 8u) / kW];
        }
    }

    // Buttons configured exactly like the app (input + pull-up) so a press is a real
    // high→low transition — but we NEVER read them. Any shift is electrical only.
    const uint32_t keys[] = {
        bsp::kKeyUp,   bsp::kKeyDown, bsp::kKeyLeft,  bsp::kKeyRight,
        bsp::kKeyA,    bsp::kKeyB,    bsp::kKeyX,     bsp::kKeyY,
        bsp::kKeyL,    bsp::kKeyR,    bsp::kKeyStart, bsp::kKeySelect,
    };
    for (uint32_t pin : keys)
    {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
        gpio_pull_up(pin);
    }

    static rp2350::SpiDisplayBus bus;
    static drivers::St7789<rp2350::SpiDisplayBus> lcd(bus);

    // 1) Cold bring-up at the STOCK clock — the proven path (same as the game's
    //    splash). The panel comes out of reset reliably here.
    bus.init();
    lcd.init();
    lcd.setBacklight(255);
    lcd.present(sBars, kW, kH);

    // 2) Overclock + reparent clk_peri, then RE-init at the fast clock so SPI runs
    //    at the game's ~62.5 MHz rate (matches the conditions the shift appears in).
    if (!set_sys_clock_khz(300000, false)) { set_sys_clock_khz(250000, false); }
    clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                    clock_get_hz(clk_sys), clock_get_hz(clk_sys));
    bus.init();
    lcd.init();
    lcd.setBacklight(255);

    for (;;)
    {
        lcd.present(sBars, kW, kH);   // static image, streamed continuously
    }
}
