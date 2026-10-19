#pragma once

// ── Audio capability ───────────────────────────────────────────────────────
// The app produces PCM and pushes it down; the port owns the DAC/PWM/host-audio
// clock and drains the samples. A/V sync is the app/core's concern, not the
// port's.

#include <cstddef>
#include <cstdint>

namespace hal
{
    // Samples pushed through IAudioSink are mono, signed 16-bit PCM at this
    // fixed rate. The contract carries no per-call rate, so app and port agree
    // on it here. (Smoke-test grade for now; emulator cores may negotiate a
    // higher rate in a later slice.)
    constexpr uint32_t kAudioSampleRateHz = 32768;

    class IAudioSink
    {
    public:
        virtual ~IAudioSink() = default;
        virtual void push(const int16_t* samples, size_t count) = 0;
    };
}
