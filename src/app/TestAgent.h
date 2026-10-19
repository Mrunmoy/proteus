#pragma once

// ── TestAgent — portable manufacturing-test OPS ─────────────────────────────
// The device-side handlers for the TestStation RPC service. The device is a pure
// EXECUTOR: each op runs a REAL product driver (via a tiny hardware hook) and
// returns the RAW observation — it never judges and never declares the suite.
// The PC station (tools/mfg/gui) owns the test steps, the pass/fail criteria,
// and the operator text. Identity (GetIdentity / GetDeviceInfo) lives in the port:
// it needs the silicon chip id and the flash XIP read, which are hardware.
//
// The same handlers run on the rp2350 tester firmware and in host tests — only
// the hooks differ (real drivers vs. fakes), so the protocol/marshalling is
// proven on the host before it touches metal.

#include <cstdint>

namespace app
{
    // ── Hardware actions the port supplies for the ops ──────────────────────
    // Each runs the real driver and returns the raw observation; the PC judges.
    // To add an op later (e.g. SD card, HDMI) add a hook here, a handler in the
    // .cpp, and a test step + criteria in the station controller — nothing else moves.
    struct TestAgentHooks
    {
        // SetLed: drive the status LED; return the level actually applied.
        bool (*setLed)(bool on) = nullptr;

        // LcdShowPattern: draw a pattern via the real SPI display driver; return true
        // when the frame was presented. pattern: 0=blk 1=R 2=G 3=B 4=W, and 5=fine
        // vertical colour ruler (shift-revealing pattern for the tearing test).
        bool (*lcdShowPattern)(uint8_t pattern) = nullptr;

        // ScanButtons: snapshot the debounced held state; return the raw bitmask
        // (bit i = hal::Button i held).
        uint32_t (*scanButtons)() = nullptr;

        // PlayTone: emit a tone on the PWM buzzer; return true if the driver
        // accepted the request (the operator judges that they heard it).
        bool (*playTone)(uint32_t freqHz, uint32_t ms) = nullptr;

        // SdProbe: mount the microSD over the real SDIO driver, read the root
        // directory; return true if mounted+read OK and set *outFiles to the entry
        // count. The PC asks the operator to insert a card and passes on present.
        bool (*sdProbe)(uint32_t* outFiles) = nullptr;

        // HdmiShowPattern: bring up the real DVI/HDMI driver and show a colour-bar
        // test pattern; return true when the driver came up. The operator confirms
        // the bars on an attached monitor.
        bool (*hdmiShowPattern)(uint8_t pattern) = nullptr;
    };

    void testAgentInit(const TestAgentHooks& hooks);
}
