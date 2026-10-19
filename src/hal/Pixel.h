#pragma once

// ── Canvas pixel format ────────────────────────────────────────────────────
// The platform contract fixes one logical format: RGB565 (native to the LCD,
// cheap to expand elsewhere). Helpers are constexpr so colours fold at compile
// time.

#include <cstdint>

namespace hal
{
    [[nodiscard]] constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
    {
        return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }

    namespace color
    {
        constexpr uint16_t kBlack   = rgb565(0, 0, 0);
        constexpr uint16_t kWhite   = rgb565(255, 255, 255);
        constexpr uint16_t kRed     = rgb565(255, 0, 0);
        constexpr uint16_t kGreen   = rgb565(0, 255, 0);
        constexpr uint16_t kBlue    = rgb565(0, 0, 255);
        constexpr uint16_t kYellow  = rgb565(255, 255, 0);
        constexpr uint16_t kCyan    = rgb565(0, 255, 255);
        constexpr uint16_t kMagenta = rgb565(255, 0, 255);
    }
}
