#pragma once

#include <cstdint>

// ── GPIO button port (RP2350 PHY for the GamePi13 buttons) ──────────────────
// Concrete GPIO port (see hal/GpioPort.h): reads the 12 GamePi13 buttons (each an
// active-low GPIO with a pull-up) into a bitmask in hal::Button order. The Buttons
// driver is templated on this type and does the debouncing on top.

namespace rp2350
{
    class GpioButtonPort
    {
    public:
        // Configure every button GPIO as input + pull-up.
        void init();

        // bit per hal::Button, 1 = pressed (active-low pin reads 0).
        [[nodiscard]] uint16_t readRaw() const;
    };
}
