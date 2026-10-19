#pragma once

// ── Display capability ─────────────────────────────────────────────────────
// Provider model: the platform OWNS the framebuffer(s). The app asks for the
// back buffer, draws RGB565 pixels into it, and the platform present()s it the
// fast way for that hardware (DMA blit / GPU upload / window). A/B buffering, if
// any, is internal to the port.

#include <cstdint>

namespace hal
{
    struct Surface
    {
        uint16_t* pixels = nullptr;  // RGB565, row-major
        uint16_t  width  = 0;
        uint16_t  height = 0;
        uint16_t  stride = 0;        // pixels per row (>= width)
    };

    class IDisplay
    {
    public:
        static constexpr uint16_t kWidth  = 240;
        static constexpr uint16_t kHeight = 240;

        virtual ~IDisplay() = default;

        [[nodiscard]] virtual Surface backBuffer() = 0;   // buffer to draw into
        virtual void                  present()    = 0;   // push it to the panel
        virtual void                  setBacklight(uint8_t level) = 0;
    };
}
