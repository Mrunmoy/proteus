#pragma once

// ── Flash-backed storage adapter for the diagnostics capsule ────────────────
// Persists the diag library's capsule (lifecycle/reset counters) in one
// dedicated QSPI-flash sector, read back through XIP. The sector sits clear of
// the firmware image, the FlashRom ROM region + its metadata, and the mfg
// device-info block (see FLASH LAYOUT in FlashRom.h). Erase/program route
// through flash_safe_execute exactly like FlashRom, so this is only safe to call
// at boot before core1 is launched.
//
// Returns a ready-to-attach `diag_storage` (ops + capabilities + capsule
// buffer); pass it straight to diag_storage_attach().

extern "C" {
#include "diag/storage.h"
}

namespace rp2350
{
    [[nodiscard]] const struct diag_storage* diagFlashStorage();
}
