#pragma once

// ── Input capability ───────────────────────────────────────────────────────
// Produced asynchronously by the port (ISR / event pump), delivered
// synchronously once per frame as an InputFrame. Carries BOTH a debounced state
// bitmask (emulator reads this) and an event list since last frame (UI drains
// this).

#include "hal/Buttons.h"

#include <cstdint>

namespace hal
{
    struct InputFrame
    {
        uint16_t          held       = 0;        // bit per Button, 1 = down (debounced)
        const InputEvent* events     = nullptr;  // edges since last frame
        uint8_t           eventCount = 0;

        [[nodiscard]] bool isDown(Button b) const { return (held & buttonBit(b)) != 0; }
    };

    class IInput
    {
    public:
        virtual ~IInput() = default;
        [[nodiscard]] virtual InputFrame poll() = 0;
    };
}
