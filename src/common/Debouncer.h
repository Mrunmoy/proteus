#pragma once

// ── Button debounce (portable, pure logic) ─────────────────────────────────
// Counter-integrator: update() is called once per input-sample tick. The
// debounced state only flips after `stableCount` consecutive opposite samples;
// any bounce back to the current state resets the counter. No hardware deps, so
// it is unit-tested on the host and ships UNCHANGED to the RP2350 input path.

#include <cstdint>

namespace common
{
    class Debouncer
    {
    public:
        Debouncer() = default;
        explicit Debouncer(uint8_t stableCount) : m_stableCount(stableCount) {}

        // Feed one raw sample; returns the current debounced state.
        bool update(bool raw);

        [[nodiscard]] bool state() const { return m_state; }

    private:
        uint8_t m_stableCount = 2;   // ticks of agreement required to flip
        uint8_t m_counter     = 0;
        bool    m_state       = false;
    };
}
