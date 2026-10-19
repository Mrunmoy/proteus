#pragma once

#include <cstdint>

// ── PWM speaker output (RP2350 PHY for the GamePi13 buzzer) ──────────────────
// Concrete PWM output (see hal/PwmOut.h) on the speaker pin. The PwmSpeaker driver
// is templated on this type; it owns the ring + sample→duty mapping and only calls
// setLevel() here. The carrier (~586 kHz) sits well above hearing.

namespace rp2350
{
    class PwmSpeakerOut
    {
    public:
        static constexpr uint16_t kWrap = 1023;   // 10-bit duty; silence = kWrap/2

        // Configure the PWM slice on the speaker pin.
        void init();

        // ── hal/PwmOut contract ─────────────────────────────────────────────
        void setLevel(uint16_t duty);

        [[nodiscard]] uint16_t wrap() const { return kWrap; }

    private:
        unsigned m_slice = 0;
    };
}
