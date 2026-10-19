#pragma once

// ── Per-unit personalization (recipient name) ───────────────────────────────
// A one-record flash block written once per board at manufacture by the host
// tool tools/personalize.py (picotool, BOOTSEL) and read here at boot to show a
// personalized splash. It has its OWN flash sector: the diagnostics capsule one
// sector above (0x3FE000) is rewritten on every boot, so the name must not share
// it. Firmware only reads this block; nothing in firmware ever writes it.
//
// FLASH LAYOUT (below the FlashRom ROM region — see FlashRom.h):
//   0x3FD000  personalization (this block)
//   0x3FE000  diagnostics capsule (DiagStore)
//   0x3FF000  ROM metadata (FlashRom)
//   0x400000  ROM region (FlashRom)

#include <cstddef>
#include <cstdint>

namespace rp2350
{
    inline constexpr uint32_t kPersonalizationOffset = 0x3FD000u;
    inline constexpr uint32_t kPersonalizationMagic  = 0x4E5A5250u;  // 'PRZN' (LE)
    inline constexpr size_t   kRecipientMax          = 31u;          // name chars, + NUL

    struct Personalization
    {
        uint32_t magic;
        char     recipient[kRecipientMax + 1];
    };

    // Copy the programmed recipient name into `out` (always NUL-terminated).
    // Returns true when a valid, non-empty name is present; otherwise `out` is
    // emptied and the caller falls back to a generic splash.
    [[nodiscard]] inline bool readRecipient(char* out, size_t cap)
    {
        constexpr uintptr_t kXipBase = 0x10000000u;
        const Personalization* p =
            reinterpret_cast<const Personalization*>(kXipBase + kPersonalizationOffset);

        if (out == nullptr || cap == 0) { return false; }
        out[0] = '\0';
        if (p->magic != kPersonalizationMagic) { return false; }

        size_t n = 0;
        while (n < kRecipientMax && n + 1 < cap && p->recipient[n] != '\0')
        {
            out[n] = p->recipient[n];
            ++n;
        }
        out[n] = '\0';
        return n > 0;
    }
}
