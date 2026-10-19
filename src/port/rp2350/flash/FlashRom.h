#pragma once

#include "sd/SdCard.h"

#include <cstddef>
#include <cstdint>

// ── Flash-XIP ROM loader (rp2350 port) ──────────────────────────────────────
// Streams a .nes cartridge off the microSD card into a RESERVED region of the
// RP2350's 16 MiB QSPI flash, then exposes a pointer to the memory-mapped (XIP)
// copy. The NES core reads ROM/CHR bytes directly from flash through XIP — like a
// cartridge — so even a large ROM costs ZERO RAM (which the PPU framebuffer +
// system RAM already claim). The whole file is flashed verbatim, INCLUDING the
// 16-byte iNES header; NesApp::attachRom() parses it. Emulator-agnostic: this
// loader knows nothing about the ROM format beyond "stream bytes to flash".
//
// FLASH LAYOUT (16 MiB QSPI, XIP_BASE = 0x10000000):
//   [0x000000 .. firmware)              firmware image
//   [0x400000 .. 0x400000 + romBytes)   ROM region (this loader)  → XIP 0x10400000
// The 4 MiB offset is far past any plausible firmware growth, so erasing the ROM
// region can never clobber the running image.
//
// SAFETY: flash_range_erase/program must NOT run while code executes from / reads
// XIP, and core1 must be quiesced. flashRomFromSd() runs ONCE at boot, before
// core1 / the render loop, via the SDK's flash_safe_execute() (see the .cpp).

namespace rp2350
{

inline constexpr uint32_t kRomFlashOffset   = 0x400000u;            // 4 MiB
inline constexpr uint32_t kRomFlashCapacity = 12u * 1024 * 1024;   // ~12 MiB usable
// One sector BELOW the ROM region holds a tiny catalog record (name + size) of the
// ROM currently programmed, so a re-select of the same game can skip re-flashing.
inline constexpr uint32_t kRomMetaOffset    = kRomFlashOffset - 0x1000u;  // 4 MiB-4 KiB

class FlashRom
{
public:
    enum class Status : int32_t
    {
        Ok          = 0,
        NoCard      = -1,   // socket empty
        MountFailed = -2,   // mount / driver init failed
        NoRom       = -3,   // mounted but no .nes present
        OpenFailed  = -4,   // file open failed
        ReadFailed  = -5,   // short read / I/O error mid-stream
        TooLarge    = -6,   // ROM exceeds the flash region
    };

    [[nodiscard]] static constexpr bool ok(Status s) { return s == Status::Ok; }

    FlashRom() = default;
    FlashRom(const FlashRom&)            = delete;
    FlashRom& operator=(const FlashRom&) = delete;

    // Mount the card, find the first .nes, stream it SD→flash verbatim, and record
    // the XIP pointer + size. MUST be called at boot before core1 is launched and
    // before anything depends on uninterrupted XIP. `scratch` is a caller-owned
    // RAM staging buffer (>= one flash sector, 4 KiB, recommended).
    [[nodiscard]] Status flashRomFromSd(sd::SdCard& card, uint8_t* scratch,
                                        size_t scratchLen);

    // Variant for callers that already mounted the card AND opened the target file
    // (e.g. a browse-menu that let the user pick). Streams the OPEN file verbatim
    // SD→flash, writes the catalog record (`name`,size), and records the XIP pointer
    // + size; the caller closes/unmounts and validates the format. Format-agnostic —
    // no header magic check (unlike the .nes auto-finder). Boot-time only (no core1).
    // `progress`, if non-null, is invoked as (bytesWritten, totalBytes) per window.
    [[nodiscard]] Status flashOpenFileToXip(sd::SdCard& card, const char* name,
                                            uint8_t* scratch, size_t scratchLen,
                                            void (*progress)(uint32_t, uint32_t) = nullptr);

    // True if the ROM already programmed in flash matches (name,size) AND its GB
    // header logo is intact — i.e. re-flashing would be redundant. Pure XIP reads.
    [[nodiscard]] bool cachedMatches(const char* name, uint32_t size) const;

    // Adopt the already-programmed flash ROM without re-flashing (use after a
    // cachedMatches() hit): sets data()/size()/valid() to the cached region.
    void adoptCached(uint32_t size);

    // Report the ROM currently cached in flash, if any: fills `nameOut` (NUL-
    // terminated, up to `cap`) and `*sizeOut` from the catalog record and confirms
    // the ROM region still holds a valid GB image. Lets a no-SD boot offer to play
    // the last-flashed game. Pure XIP reads.
    [[nodiscard]] bool cachedInfo(char* nameOut, size_t cap, uint32_t* sizeOut) const;

    [[nodiscard]] bool           valid() const { return m_valid; }
    [[nodiscard]] const uint8_t* data() const  { return m_data; }   // XIP pointer
    [[nodiscard]] uint32_t       size() const  { return m_size; }

private:
    const uint8_t* m_data  = nullptr;  // XIP_BASE + kRomFlashOffset on success
    uint32_t       m_size  = 0;        // ROM size in bytes (whole .nes file)
    bool           m_valid = false;
};

}  // namespace rp2350
