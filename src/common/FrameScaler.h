#pragma once

// ── Emulator-agnostic frame → display blit ──────────────────────────────────
// Portable, hardware-free, integer-scaled (nearest, whole multiples) and centred
// copy of ANY RGB565 source frame into an RGB565 destination of arbitrary size,
// with the surrounding border cleared (pillarbox/letterbox).
//
// The SOURCE dimensions are parameters — the platform is NOT tied to any one
// emulator's resolution (NES is 256×240, SNES 256×224, …). A core hands its
// frame + size in via the hal contract; this blit knows nothing about which
// emulator produced it. Both sides are RGB565 (libdvi's native 16bpp and the
// common PPU output), so it is a pure scale/centre copy — no format conversion.
//
// computeScale is split out so it is unit-testable without a framebuffer and so
// a port can fold the scale at init time.

#include <cstdint>

namespace common
{
    // Largest whole-number scale at which srcW×srcH still fits in dstW×dstH.
    // Returns at least 1 (the blit crops if the target is smaller than one frame).
    [[nodiscard]] constexpr uint32_t computeScale(uint32_t srcW, uint32_t srcH,
                                                  uint32_t dstW, uint32_t dstH)
    {
        const uint32_t sx = (srcW != 0) ? (dstW / srcW) : 0;
        const uint32_t sy = (srcH != 0) ? (dstH / srcH) : 0;
        const uint32_t s  = sx < sy ? sx : sy;
        return s == 0 ? 1u : s;
    }

    // Blit srcW×srcH RGB565 `src` into an RGB565 destination framebuffer,
    // integer-scaled and centred, clearing the border to `border`.
    //
    //   src    : srcW*srcH RGB565 pixels (e.g. a core's framebuffer())
    //   dst    : dstStride*dstH RGB565 pixels, row-major
    //   stride : pixels per destination row (>= dstW)
    //
    // If the scaled source overflows the destination it is clipped to the
    // destination bounds, so the call is always safe.
    void blitScaledCentered(const uint16_t* src,
                            uint32_t        srcW,
                            uint32_t        srcH,
                            uint16_t*       dst,
                            uint32_t        dstW,
                            uint32_t        dstH,
                            uint32_t        dstStride,
                            uint16_t        border = 0);
}
