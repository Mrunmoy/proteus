#pragma once

#include <cstddef>
#include <cstdint>

// ── PWM speaker driver ──────────────────────────────────────────────────────
// Mono int16 PCM out to a PWM pin, generic over a PWM output (see hal/PwmOut.h).
// push() (producer, the game loop) enqueues into a single-producer/single-
// consumer ring; feedSample() (consumer, a sample-rate timer) pops one sample and
// sets the duty. When the ring runs dry it holds the last sample — on an
// unfiltered speaker a held level is silent, so a gap is inaudible rather than a
// click. The ring uses head/tail only (no shared counter), so producer and
// consumer never write the same field: safe across a main-loop/ISR split with no
// lock. Pure logic + one PHY call, so it runs and is tested on the host.

namespace drivers
{
    template <class PwmOut, unsigned Capacity = 16384>
    class PwmSpeaker
    {
        static_assert((Capacity & (Capacity - 1)) == 0,
                      "ring capacity must be a power of two");

    public:
        // `wrap` is the PWM period (max duty); silence sits at half scale.
        PwmSpeaker(PwmOut& pwm, uint16_t wrap)
            : m_pwm(pwm), m_idle(static_cast<uint16_t>(wrap / 2)) {}

        // Producer side: enqueue samples, dropping any that overflow the ring.
        void push(const int16_t* samples, size_t count)
        {
            for (size_t i = 0; i < count; ++i)
            {
                const uint32_t next = (m_head + 1) & kMask;
                if (next == m_tail) { ++m_dropped; return; }   // ring full
                m_ring[m_head] = samples[i];
                m_head = next;
                ++m_pushed;
            }
        }

        // Consumer side (sample-rate tick): output one sample, or hold the last.
        void feedSample()
        {
            if (m_head == m_tail)                              // empty
            {
                ++m_underruns;
                m_pwm.setLevel(toDuty(m_last));
                return;
            }
            m_last = m_ring[m_tail];
            m_tail = (m_tail + 1) & kMask;
            ++m_consumed;
            m_pwm.setLevel(toDuty(m_last));
        }

        struct Stats
        {
            uint32_t pushed;
            uint32_t consumed;
            uint32_t underruns;
            uint32_t dropped;
            uint32_t fill;
        };

        [[nodiscard]] Stats stats() const
        {
            return {m_pushed, m_consumed, m_underruns, m_dropped,
                    (m_head - m_tail) & kMask};
        }

    private:
        static constexpr uint32_t kMask = Capacity - 1;

        // Map int16 PCM to a duty centred on idle (half scale), clamped to range.
        [[nodiscard]] uint16_t toDuty(int16_t sample) const
        {
            int32_t duty = m_idle + (static_cast<int32_t>(sample) * m_idle) / 32768;
            if (duty < 0) { duty = 0; }
            if (duty > 2 * m_idle) { duty = 2 * m_idle; }
            return static_cast<uint16_t>(duty);
        }

        PwmOut&  m_pwm;
        uint16_t m_idle;
        int16_t  m_ring[Capacity] = {};
        int16_t  m_last = 0;

        volatile uint32_t m_head = 0;   // producer writes
        volatile uint32_t m_tail = 0;   // consumer writes

        uint32_t m_pushed    = 0;
        uint32_t m_consumed  = 0;
        uint32_t m_underruns = 0;
        uint32_t m_dropped   = 0;
    };
}
