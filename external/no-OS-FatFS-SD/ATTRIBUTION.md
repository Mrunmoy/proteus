# Vendored: no-OS-FatFS-SD (SPI + SDIO) for Raspberry Pi Pico

This directory is a trimmed vendor drop of Carl Kugler's
**no-OS-FatFS-SD-SDIO-SPI-RPi-Pico** library, as shipped in the Waveshare
RP2350-PiZero demo `C/03-MicroSD`. It bundles ChaN's **FatFs R0.15** (in
`src/ff15/`) with an RP2040/RP2350 SPI + SDIO block driver.

Proteus uses the **SPI** path only (microSD on SPI1). The SDIO sources are kept
so the upstream `sd_card.c` links unchanged, but no SDIO card is configured.

## Upstream provenance

- Project: <https://github.com/carlk3/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico>
- Version: **v3.3.1** (per the version header in `UPSTREAM_README.md`).
- Delivery: obtained from the **Waveshare RP2350-PiZero vendor demo
  `C/03-MicroSD`**, not a direct clone of the upstream repo.
- FatFs: ChaN **R0.15** (`src/ff15/`).
- Exact commit SHA: **not pinnable.** `v3.3.1` is no longer a tag upstream (as of
  2026-06 the tags are v1.1.0, v1.1.1, v3.4.0, v3.5.1, v3.6.2 — the 3.3.x line was
  retagged/removed), and the version-bump commit isn't findable by message
  search. The version string above is the most precise pin available. This is
  also why the dep is vendored (a trimmed subset + Proteus glue) rather than a
  git submodule, which could only point at a whole upstream commit.

## What was removed from the upstream drop

- The Arduino/C++ convenience layer (`include/FsLib/`, `include/iostream/`,
  `include/FatFsSd.h`, `src/src/FatFsSd.cpp`) — not used; Proteus drives the
  plain C FatFs API.
- The vendor `src/CMakeLists.txt` (replaced by `proteus_sd.cmake`) and the
  FatFs HTML docs.
- `src/src/rtc.c` (board RTC demo); `my_rtc.c` provides `get_fattime()`.

## What was added by Proteus (NOT upstream)

- `proteus_sd.cmake` — source/include lists for the firmware build.
- The board wiring (`src/port/rp2350/sd/hw_config.c`) and FatFs config
  (`src/port/rp2350/sd/ffconf.h`) live in the Proteus port tree, not here.

## Licenses (preserved)

- **FatFs** (`src/ff15/`): 1-clause BSD-style — see `src/ff15/LICENSE.txt`.
- **SD driver / glue**: Apache-2.0 — per-file headers retained
  (Copyright 2021 Carl John Kugler III).

Upstream README kept verbatim as `UPSTREAM_README.md`.
