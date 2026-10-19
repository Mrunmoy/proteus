// ── Standalone DVI bring-up firmware (proteus_dvi.uf2) ─────────────────────
// Milestone for the HDMI/DVI output: drives an external monitor at 640×480p
// 60 Hz with a colour-bar test pattern, no emulator or LCD involved. Verifies
// the vendored PicoDVI path (PIO + DMA + TMDS on core 1) and the board pin map.
//
// Flash, connect the board's DVI/HDMI socket to a monitor: you should see SMPTE
// colour bars over a greyscale ramp. Requires a monitor to verify.

#include "port/rp2350/DviDisplay.h"

#include "pico/stdlib.h"

#include <cstdio>

int main()
{
    stdio_init_all();

    // Static so the two 320×240 RGB565 framebuffers (~300 KB) live in .bss.
    static rp2350::DviDisplay dvi;

    if (dvi.init() != 0)
    {
        while (true)
        {
            tight_loop_contents();
        }
    }

    dvi.drawTestPattern();

    uint32_t frame = 0;
    while (true)
    {
        // Re-draw + present every loop; double-buffered so the pattern is stable.
        dvi.drawTestPattern();
        dvi.present();
        if ((frame % 60) == 0)
        {
            printf("dvi alive frame=%lu\n", static_cast<unsigned long>(frame));
        }
        ++frame;
    }
}
