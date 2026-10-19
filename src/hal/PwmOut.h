#pragma once

#include <cstdint>

// ── PWM output (the PHY contract for the speaker driver) ────────────────────
// The speaker driver turns PCM samples into a PWM duty cycle; how that duty is
// applied to a pin (which slice, what carrier frequency) is platform business
// behind this contract. Host tests supply a mock that records the duties.
//
// Duck-typed (a template parameter, not a base class). Required surface:
//
//   void     setLevel(uint16_t duty);   // 0..wrap, the PWM compare value
//
// One call per sample tick — the driver owns the ring + sample→duty mapping.
