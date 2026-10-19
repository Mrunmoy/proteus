#include "port/rp2350/phy/GpioButtonPort.h"

#include "boards/pins.h"
#include "hal/Buttons.h"

#include "hardware/gpio.h"

namespace rp2350
{
    namespace
    {
        // Order MUST match hal::Button (Up, Down, Left, Right, A, B, X, Y, L, R,
        // Start, Select) so bit i corresponds to button i.
        constexpr uint32_t kPins[] = {
            bsp::kKeyUp,   bsp::kKeyDown, bsp::kKeyLeft,  bsp::kKeyRight,
            bsp::kKeyA,    bsp::kKeyB,    bsp::kKeyX,     bsp::kKeyY,
            bsp::kKeyL,    bsp::kKeyR,    bsp::kKeyStart, bsp::kKeySelect,
        };
        constexpr unsigned kCount = sizeof(kPins) / sizeof(kPins[0]);
        static_assert(kCount == static_cast<unsigned>(hal::Button::Count),
                      "pin table must match hal::Button");
    }

    void GpioButtonPort::init()
    {
        for (uint32_t pin : kPins)
        {
            gpio_init(pin);
            gpio_set_dir(pin, GPIO_IN);
            gpio_pull_up(pin);
        }
    }

    uint16_t GpioButtonPort::readRaw() const
    {
        uint16_t raw = 0;
        for (unsigned i = 0; i < kCount; ++i)
        {
            if (gpio_get(kPins[i]) == 0) { raw |= static_cast<uint16_t>(1u << i); }
        }
        return raw;
    }
}
