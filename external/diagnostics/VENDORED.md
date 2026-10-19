# Vendored: diagnostics (C99)

Source: https://github.com/Mrunmoy/diagnostics — branch `c`
Pinned commit: `93e0ccd2ff5a593950e989f98de5ccaecb3bfa34`

Embedded-first diagnostics: DTC management, lifecycle/reset tracking, numeric
identity, and capsule persistence. The library owns bounded state in
caller-provided RAM; the application supplies storage and transport adapters.

Only `include/diag/` and `src/diag_*.c` are vendored here. The RP2350 storage
adapter (a dedicated flash sector) lives in
`src/port/rp2350/flash/DiagStore.{h,cpp}`, and the gift app wires lifecycle/reset
tracking in `src/app/gb/main.cpp` (see `initDiagnostics`).

To refresh: re-copy `include/diag` and `src/diag_*.c` from the upstream branch and
update the pinned commit above.
