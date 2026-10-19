#pragma once

// ── DVI/HDMI output for the RP2350-PiZero ──────────────────────────────────
// Drives an external monitor over the board's DVI socket using vendored PicoDVI
// (libdvi): a PIO state machine per TMDS lane + a PWM pixel clock + software
// TMDS encode on core 1. HSTX is unusable here because the board's DVI pins are
// outside the HSTX-reachable GPIO set — see external/picodvi/README.md.
//
// Mode: 320×240 RGB565 framebuffer, line- and pixel-doubled by libdvi
// (DVI_VERTICAL_REPEAT=2) to a 640×480p 60 Hz DVI signal. The app draws into a
// 320×240 RGB565 back buffer; present() scans it out.
//
// The board owns the framebuffer (provider model, like hal::IDisplay). This is a
// standalone driver rather than an hal::IDisplay because the contract fixes the
// LCD's 240×240 geometry; the DVI surface is 320×240. Any emulator's RGB565
// frame is mapped in via presentFrame() with its own dimensions (integer-scaled
// + centred, common::FrameScaler) — the display is emulator-agnostic.

#include "hal/IDisplay.h"

#include <cstdint>

// Forward declaration: the libdvi instance is an implementation detail kept out
// of this header so app/port code does not pull in libdvi/Pico SDK headers.
struct dvi_inst;

namespace rp2350
{
    class DviDisplay
    {
    public:
        // ── Mode ──────────────────────────────────────────────────────────
        // Logical framebuffer; libdvi doubles it to 640×480 on the wire.
        static constexpr uint16_t kWidth  = 320;
        static constexpr uint16_t kHeight = 240;

        DviDisplay()                             = default;
        DviDisplay(const DviDisplay&)            = delete;
        DviDisplay& operator=(const DviDisplay&) = delete;

        // ── Lifecycle ─────────────────────────────────────────────────────
        // Bumps the core voltage + system clock to the DVI bit clock, sets up
        // libdvi, and launches the TMDS encode worker on core 1. Must run before
        // any other core-1 work is started. Returns 0 on success.
        [[nodiscard]] int init();

        // ── Presentation (provider model) ─────────────────────────────────
        [[nodiscard]] hal::Surface backBuffer();  // 320×240 RGB565 to draw into
        void                       present();      // scan the back buffer out

        // ── Emulator bridge (emulator-agnostic) ───────────────────────────
        // Blit any srcW×srcH RGB565 frame into the back buffer (integer scale +
        // centre; both buffers are RGB565, so no conversion) and present it.
        void presentFrame(const uint16_t* frame, uint32_t srcW, uint32_t srcH,
                          uint16_t border = 0);

        // DMA blit: copy a srcW×srcH RGB565 frame (srcW≤kWidth, srcH≤kHeight)
        // 1:1, centred, into the back buffer using a DMA channel (the byte copy
        // runs on the DMA engine, not the CPU), then present. Borders stay black
        // (the buffers start zeroed and only the centred region is ever written).
        // The DVI scan-out itself is already DMA-driven inside libdvi.
        void presentFrameDma(const uint16_t* frame, uint32_t srcW, uint32_t srcH);

        // Unified output interface, matching drivers::St7789::present(): integer-
        // scale + centre a source frame and show it. This is what lets the boot
        // LCD/HDMI choice be a std::variant<St7789<...>, DviDisplay> whose visitor
        // just calls present(frame, w, h) on whichever output was selected.
        void present(const uint16_t* frame, uint32_t srcW, uint32_t srcH)
        {
            presentFrame(frame, srcW, srcH, 0);
        }

        // ── Test pattern ──────────────────────────────────────────────────
        // Fill the back buffer with SMPTE-style colour bars (no monitor-free way
        // to verify DVI output; this is the standalone bring-up milestone).
        void drawTestPattern();

    private:
        void scanOut(const uint16_t* fb);  // enqueue one full frame of scanlines

        // Two framebuffers: app draws into the back one while the previous frame
        // is still being scanned out. Aligned/static-friendly (lives in .bss via
        // the owning instance).
        uint16_t m_fb[2][kWidth * kHeight] = {};
        uint8_t  m_back                    = 0;
        bool     m_started                 = false;
    };
}
