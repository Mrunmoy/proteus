#pragma once

// ── The application the platform drives ────────────────────────────────────
// Single-threaded, synchronous, hardware-free. The platform owns the run-loop
// and calls step() once per frame; the app draws into `surface`, reads `in`,
// and pushes audio. It never owns the loop, touches a GPIO, or allocates. This
// is the whole portability seam.

#include "hal/IAudioSink.h"
#include "hal/IDisplay.h"
#include "hal/IInput.h"

namespace hal
{
    class IApp
    {
    public:
        virtual ~IApp() = default;

        virtual void init() = 0;
        virtual void step(const InputFrame& in, Surface& surface, IAudioSink& audio) = 0;
    };
}
