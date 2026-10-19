#pragma once

// ── Logical buttons ────────────────────────────────────────────────────────
// Physical wiring (GPIO / keyboard / gamepad) is translated to these logical
// buttons by the platform port. The app only ever sees logical buttons.

#include <cstdint>

namespace hal
{
    enum class Button : uint8_t
    {
        Up, Down, Left, Right,
        A, B, X, Y,
        L, R,
        Start, Select,
        Count
    };

    [[nodiscard]] constexpr uint16_t buttonBit(Button b)
    {
        return static_cast<uint16_t>(1u << static_cast<uint8_t>(b));
    }

    struct InputEvent
    {
        enum class Type : uint8_t { Down, Up, Hold, Repeat };
        Button button;
        Type   type;
    };
}
