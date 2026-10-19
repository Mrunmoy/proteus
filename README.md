# Proteus

Bare-metal firmware and factory tooling for a **Waveshare RP2350-PiZero**
(RP2350B MCU) plugged into a **GamePi13** 1.3″ 240×240 game HAT.

The repo builds two things:

- **`proteus_gb`**: a Game Boy (DMG) handheld. It browses `.gb` games on the
  microSD card, plays them on the built-in LCD (or on HDMI), with sound. This is
  the image a finished board ships with.
- **`proteus_mfg`**, a manufacturing **test firmware**, plus a PC **test station**
  (PySide6 GUI). Together they bring up, test, serialize and record each board,
  then flash it with `proteus_gb`.

C++17, no heap in firmware paths, no RTOS: bare-metal dual-core plus ISRs.
Everything builds in Docker; the only host requirements are Docker and Python 3.

## Architecture

```
src/hal/       platform contract: small, typed, hardware-free interfaces
src/drivers/   generic, header-only chip drivers templated on a PHY
               (St7789 display, Buttons, PwmSpeaker)
src/common/    portable logic (debouncer, frame scaler, device-info block)
src/ui/        tiny widget toolkit used by the menus
src/app/       applications: gb, mfg (TestAgent), lcdtest, pio_usb
src/port/rp2350/
  boards/      board header + GamePi13 pin map
  phy/         RP2350 PHYs the generic drivers bind to (SPI, GPIO, PWM)
  sd/          microSD over SDIO (FatFS)
  flash/       ROM-in-flash loader, diagnostics store, personalization block
  DviDisplay*  HDMI/DVI output (PicoDVI)
proto/         TestStation RPC IDL + generated C99 (device) / C++ stubs
test/          host unit tests (GoogleTest), per-peripheral mocks
tools/mfg/     PC test station (GUI, device-info/serial tooling)
tools/         reflash / personalize helpers
external/      third-party code (submodules + vendored)
docs/          vendor datasheets and schematics
```

Everything above `src/port/` is hardware-free. Drivers know the *chip*, not
the SoC: `drivers::St7789<Phy>` is the same code on the board
(`rp2350::SpiDisplayBus`) and in host tests (a mock bus). The host build
enforces this, because a hardware header leaking above the contract fails to
compile there.

## Getting started

```bash
git clone --recurse-submodules <this-repo>
cd proteus
./build.py --images        # once: toolchain, station and picotool images
./build.py --firmware      # → build/*.uf2
```

Already cloned without submodules? Run `git submodule update --init --recursive`.

### All build targets

| Command | What it does |
|---|---|
| `./build.py --firmware` | All RP2350 images into `build/` |
| `./build.py --host-tests` | C++ unit tests on the host (`PORT=host`) |
| `./build.py --asan` | The same suite under AddressSanitizer + UBSan |
| `./build.py --mfg-tests` | Test-station GUI/controller tests (offscreen Qt) |
| `./build.py --run-mfg` | Launch the test station on your desktop (X11) |
| `./build.py --format` | clang-format (C/C++) + black (Python) |
| `./build.py --all` | images + firmware + host tests + station tests |

Native host tests without Docker (needs CMake, Ninja and a C++17 compiler):

```bash
cmake -S . -B build-host -DPORT=host -G Ninja [-DPROTEUS_SANITIZE=ON]
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

### Firmware images

| Image | Purpose |
|---|---|
| `proteus_gb.uf2` | The Game Boy handheld (shipping image) |
| `proteus_mfg.uf2` | Manufacturing test firmware (RPC over USB CDC) |
| `proteus_dvi.uf2` | HDMI/DVI bring-up: 640×480 colour bars |
| `lcd_buttontest.uf2` | Isolates LCD ↔ button-line electrical coupling |
| `proteus_pio.uf2` | Bring-up for the second USB-C port (PIO-USB, J4) |

## Flashing

Hold **BOOTSEL**, plug in USB, and copy a `.uf2` onto the drive that appears.

Or use picotool (from the Docker image). `tools/reflash-gb.sh` reboots a
running board into BOOTSEL and loads `build/proteus_gb.uf2`:

```bash
./tools/reflash-gb.sh
```

## Playing (`proteus_gb`)

1. Put `.gb` files in the root of a FAT-formatted microSD card.
2. Power on: splash screen, then the game menu. Choose with the D-pad and
   **A**/**Start**.
3. The chosen game streams from SD into QSPI flash and runs from the XIP
   window, so ROM size is limited by flash (~12 MiB), not by RAM.

- **HDMI output:** hold **SELECT** while powering on, and keep holding it
  through the ~3 s USB settle. The board's HPD/DDC lines aren't wired to the MCU,
  so it can't detect a monitor by itself.
- **No SD card:** it plays the last game it cached in flash.
- **Errors:** the screen shows the failure and the board halts. It never
  reboot-loops.

The emulator core is [Peanut-GB](https://github.com/deltabeard/Peanut-GB) with
minigb_apu audio (32768 Hz). It runs at 250 MHz; 300 MHz is marginal on some
RP2350 silicon.

## Manufacturing (`proteus_mfg` + test station)

The **PC owns the test.** The device firmware exposes verb-named operations
(`LcdShowPattern`, `ScanButtons`, `PlayTone`, `SdProbe`, …) that run the *real*
product drivers and return raw observations. The station applies the pass/fail
criteria and guides the operator. The protocol is defined once in
`proto/TestStation.idl` and generated for both sides by
[aether](https://github.com/Mrunmoy/aether)'s `ipcgen`. It runs over stock USB
CDC (VID:PID `1209:7201`).

```bash
./build.py --run-mfg
```

The station runs each board through **PROGRAM → TEST → SERIALIZE → SHIP**:

1. **PROGRAM:** write the device-info block (device type, no serial).
2. **TEST:** the operator-selectable suite: LCD colour patterns, LCD
   tearing/shift stress, all 12 buttons, speaker tone, microSD mount + read,
   and HDMI colour bars.
3. **SERIALIZE:** only after every test that ran has passed. Mints a serial,
   writes it, reads it back through the firmware, and binds it to the chip's
   unique ID in a local SQLite DB.
4. **SHIP:** optionally writes a recipient name for the splash screen, then
   flashes `proteus_gb`.

The station also handles re-manufacture (keep or replace the serial) and RMA.
`tools/personalize.py` writes the splash name on its own.

## Flash layout (16 MiB)

| Offset | Contents |
|---|---|
| `0x000000` | Firmware image |
| `0x3FD000` | Personalization (splash name), survives reflashes |
| `0x3FE000` | Diagnostics capsule (boot/reset tracking) |
| `0x3FF000` | Cached-ROM metadata |
| `0x400000` | Cached game ROM (run from XIP `0x10400000`) |
| `0xFFC000` | Device-info block (written by the station via picotool) |

## Board notes

- RP2350B (QFN-80) · 520 KB SRAM · 16 MB flash · PSRAM pad unpopulated.
- Pin numbers are **RP2350 GPIOs**, not the Waveshare wiki's Raspberry Pi BCM
  column (they differ on several keys and the SPI lines). See
  `src/port/rp2350/boards/pins.h`.
- The microSD slot is wired for **SDIO** (CLK GP30, CMD GP31, D0–D3 GP40–43),
  not SPI.

## Third-party code

| Component | Use | License |
|---|---|---|
| [Peanut-GB](https://github.com/deltabeard/Peanut-GB) + minigb_apu | Game Boy core + APU | MIT |
| [no-OS-FatFS-SD-SDIO-SPI-RPi-Pico](https://github.com/carlk3/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico) | microSD + FatFS | Apache-2.0 |
| [PicoDVI](https://github.com/Wren6991/PicoDVI) (libdvi) | HDMI/DVI output | BSD-3-Clause |
| [Pico-PIO-USB](https://github.com/sekigon-gonnoc/Pico-PIO-USB) | Second USB port | MIT |
| [aether](https://github.com/Mrunmoy/aether) | RPC framing + IDL codegen | see repo |
| [diagnostics](https://github.com/Mrunmoy/diagnostics) | Boot/reset diagnostics | see repo |
