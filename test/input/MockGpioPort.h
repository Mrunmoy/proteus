#pragma once

#include "hal/Buttons.h"

#include <cstdint>

// Settable GPIO port for host tests: satisfies the hal/GpioPort contract and lets
// a test drive button state directly. Test-only.
struct MockGpioPort
{
    uint16_t raw = 0;   // bit per hal::Button, 1 = pressed

    [[nodiscard]] uint16_t readRaw() const { return raw; }

    void press(hal::Button b) { raw |= hal::buttonBit(b); }
    void release(hal::Button b) { raw &= static_cast<uint16_t>(~hal::buttonBit(b)); }
};
