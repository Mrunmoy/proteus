#pragma once

#include <cstdint>

// ── GPIO port (the PHY contract for button input) ───────────────────────────
// The button driver is generic over a "GPIO port" that reports which logical
// buttons are physically pressed right now, as a bitmask (bit per hal::Button,
// 1 = pressed). Which GPIOs map to which button, and active-low handling, live
// behind this contract in the platform port; host tests supply a settable mock.
//
// Duck-typed (a port is a template parameter, not a base class), so the read
// inlines with no vtable. The required surface:
//
//   uint16_t readRaw();   // bit per hal::Button, 1 = pressed (active handled in port)
//
// One read per scan tick — the driver does debouncing and edge detection on top.
