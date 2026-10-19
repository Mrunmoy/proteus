# ── RP2350-PiZero build environment ────────────────────────────────────────
# Pico SDK 2.x (RP2350-capable) + arm-none-eabi toolchain + CMake/Ninja.
# Produces the firmware .uf2; you flash it on the host (drag-and-drop in BOOTSEL).

FROM ubuntu:24.04

ARG PICO_SDK_REF=2.1.1
ENV DEBIAN_FRONTEND=noninteractive
ENV PICO_SDK_PATH=/opt/pico-sdk

RUN apt-get update && apt-get install -y --no-install-recommends \
        cmake \
        ninja-build \
        gcc-arm-none-eabi \
        libnewlib-arm-none-eabi \
        libstdc++-arm-none-eabi-newlib \
        build-essential \
        python3 \
        git \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Pin the SDK to a known RP2350-capable tag and pull only the submodules we need.
RUN git clone --branch ${PICO_SDK_REF} --depth 1 \
        https://github.com/raspberrypi/pico-sdk.git ${PICO_SDK_PATH} \
    && cd ${PICO_SDK_PATH} \
    && git submodule update --init --depth 1 \
        lib/tinyusb lib/mbedtls

# SDL2 for the desktop emulator frontend (src/port/sdl, PORT=host emu_sdl) — a
# host dev tool to run ROMs at full speed in a window. Separate layer so the
# (slow) SDK clone above stays cached.
RUN apt-get update && apt-get install -y --no-install-recommends \
        libsdl2-dev \
        pkg-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work

# Default: configure + build into build/ and leave the .uf2 there.
CMD ["bash", "-lc", "cmake -S . -B build -G Ninja && cmake --build build"]
