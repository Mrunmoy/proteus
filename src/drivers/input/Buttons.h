#pragma once

#include "common/Debouncer.h"
#include "hal/Buttons.h"
#include "hal/IInput.h"

#include <cstdint>

// ── Button input driver ─────────────────────────────────────────────────────
// Generic over a GPIO port (see hal/GpioPort.h). The port reports a raw pressed
// bitmask; this driver debounces each button and turns edges into events. It
// produces the same hal::InputFrame the apps already consume, so nothing above it
// changes. Pure logic + a port read, so it runs and is tested on the host with a
// mock port.
//
// Usage: call scan() at the sample rate (e.g. a 1 ms timer), poll() once per frame.

namespace drivers
{
    template <class Port>
    class Buttons
    {
    public:
        static constexpr uint8_t kCount         = static_cast<uint8_t>(hal::Button::Count);
        static constexpr uint8_t kEventCapacity = 32;   // press+release for every button

        explicit Buttons(Port& port, uint8_t stableCount = 2) : m_port(port)
        {
            for (common::Debouncer& d : m_deb) { d = common::Debouncer{stableCount}; }
        }

        // One sample tick: read the port, debounce every button, record edges.
        void scan()
        {
            const uint16_t raw = m_port.readRaw();
            uint16_t held = 0;
            for (uint8_t i = 0; i < kCount; ++i)
            {
                const bool prev = m_deb[i].state();
                const bool now  = m_deb[i].update(((raw >> i) & 1u) != 0);
                if (now) { held |= hal::buttonBit(static_cast<hal::Button>(i)); }
                if (now != prev)
                {
                    pushEvent(static_cast<hal::Button>(i),
                              now ? hal::InputEvent::Type::Down
                                  : hal::InputEvent::Type::Up);
                }
            }
            m_held = held;
        }

        // Drain the debounced state + the edges seen since the last poll().
        [[nodiscard]] hal::InputFrame poll()
        {
            for (uint8_t i = 0; i < m_count; ++i) { m_out[i] = m_ring[i]; }
            const hal::InputFrame frame{m_held, m_out, m_count};
            m_count = 0;
            return frame;
        }

    private:
        void pushEvent(hal::Button button, hal::InputEvent::Type type)
        {
            if (m_count < kEventCapacity)
            {
                m_ring[m_count++] = hal::InputEvent{button, type};
            }
        }

        Port&             m_port;
        common::Debouncer m_deb[kCount];
        hal::InputEvent   m_ring[kEventCapacity];
        hal::InputEvent   m_out[kEventCapacity];
        uint16_t          m_held  = 0;
        uint8_t           m_count = 0;
    };
}
