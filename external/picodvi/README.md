# PicoDVI (libdvi) — vendored

DVI/HDMI output for the RP2040/RP2350 via **PIO + DMA + software TMDS encode**
(no HSTX). Upstream: <https://github.com/Wren6991/PicoDVI>, BSD-3-Clause
(see `LICENSE`, © 2021 Luke Wren).

Only the `libdvi/` core is vendored — the parts needed to scan a framebuffer out
over three TMDS lanes. The upstream `libsprite`/`libgui`/example apps and image
assets are **not** vendored; this project provides its own C++ wrapper
(`src/port/rp2350/DviDisplay.*`) and frame bridge.

## Upstream provenance

- Project: <https://github.com/Wren6991/PicoDVI> (BSD-3-Clause, © 2021 Luke Wren).
- Exact commit SHA: **not pinned.** PicoDVI is unversioned (no tags/version
  markers) and `libdvi/` changes rarely; this copy corresponds to the `libdvi`
  core as of vendoring. Diff against upstream `main` to recover the precise base
  if needed. Vendored (subset only) rather than submoduled for the same reason as
  the SD driver.

## Why software TMDS (not HSTX)

The Waveshare **RP2350-PiZero** routes its DVI pins to GPIOs that the HSTX block
cannot reach. libdvi serialises TMDS from any GPIO using a PIO state machine per
lane plus a PWM-driven pixel clock, so it works on this board where HSTX does
not. The encode runs on **core 1** (`dvi_scanbuf_main_16bpp`); core 0 produces
RGB565 scanlines.

## Board pin configuration (RP2350-PiZero)

Taken from the vendor demo (`01-DVI`, `pico_sock_cfg` default + GPIO base 16):

| Signal       | GPIO (D+/D−) | Notes                              |
|--------------|--------------|------------------------------------|
| TMDS lane 0  | 32 / 33      | blue / sync lane                   |
| TMDS lane 1  | 34 / 35      | green                              |
| TMDS lane 2  | 36 / 37      | red                                |
| Pixel clock  | 38 / 39      | PWM slice, complementary pair      |
| `invert_diffpairs` | false  |                                    |

GPIOs ≥ 32 require `pio_set_gpio_base(pio0, 16)` (RP2350B has 48 GPIOs; a PIO
instance can only see a 32-pin window, so the window is shifted to 16…47).

The wrapper hard-codes this layout in `DviDisplay.cpp`; the constants are not in
the platform contract because they are a board detail of the rp2350 port.

## Updating

Re-copy `libdvi/*.{c,h,S,pio}` from upstream and keep the local
`libdvi/CMakeLists.txt` (an INTERFACE target). No source edits are applied — the
wrapper drives the library entirely through its public API.
