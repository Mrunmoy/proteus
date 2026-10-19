#include "app/TestAgent.h"

extern "C"
{
#include "aether_lite_dispatch.h"
#include "aether_lite_types.h"
#include "TestStation.h"   // generated: types + dispatch table
}

// ── Portable TestStation OP handlers ────────────────────────────────────────
// Each handler runs the REAL driver through a hook and returns the RAW
// observation — no judging, no suite. GetIdentity / GetDeviceInfo live in the
// port (they need the silicon chip id and the flash XIP read). The device keeps
// no test state between calls; every op is self-contained.

namespace
{
    app::TestAgentHooks g_hooks{};
}

namespace app
{
    void testAgentInit(const TestAgentHooks& hooks) { g_hooks = hooks; }
}

extern "C" int TestStation_handleSetLed(uint8_t on, uint8_t* applied)
{
    if (!g_hooks.setLed) { return AL_ERR_INVALID_ARGUMENT; }
    *applied = g_hooks.setLed(on != 0) ? 1u : 0u;
    return AL_SUCCESS;
}

extern "C" int TestStation_handleLcdShowPattern(uint8_t pattern, uint8_t* drawn)
{
    if (!g_hooks.lcdShowPattern) { return AL_ERR_INVALID_ARGUMENT; }
    *drawn = g_hooks.lcdShowPattern(pattern) ? 1u : 0u;
    return AL_SUCCESS;
}

extern "C" int TestStation_handleScanButtons(uint32_t* held)
{
    if (!g_hooks.scanButtons) { return AL_ERR_INVALID_ARGUMENT; }
    *held = g_hooks.scanButtons();
    return AL_SUCCESS;
}

extern "C" int TestStation_handlePlayTone(uint32_t freqHz, uint32_t ms, uint8_t* started)
{
    if (!g_hooks.playTone) { return AL_ERR_INVALID_ARGUMENT; }
    *started = g_hooks.playTone(freqHz, ms) ? 1u : 0u;
    return AL_SUCCESS;
}

extern "C" int TestStation_handleSdProbe(uint8_t* present, uint32_t* files)
{
    if (!g_hooks.sdProbe) { return AL_ERR_INVALID_ARGUMENT; }
    uint32_t n = 0;
    *present = g_hooks.sdProbe(&n) ? 1u : 0u;
    *files = n;
    return AL_SUCCESS;
}

extern "C" int TestStation_handleHdmiShowPattern(uint8_t pattern, uint8_t* shown)
{
    if (!g_hooks.hdmiShowPattern) { return AL_ERR_INVALID_ARGUMENT; }
    *shown = g_hooks.hdmiShowPattern(pattern) ? 1u : 0u;
    return AL_SUCCESS;
}
