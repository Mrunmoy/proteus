#include "port/rp2350/flash/FlashRom.h"

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/flash.h"

#include <cstring>

// ── Flash-XIP ROM loader implementation ─────────────────────────────────────
// See FlashRom.h for the layout + safety contract. The hard part on RP2350 is
// FLASH SAFETY: the QSPI flash cannot service XIP (instruction/literal fetches)
// while an erase or program is in flight, and on a dual-core part the OTHER core
// must be prevented from touching flash for the duration. We sidestep both:
//
//   1. flashRomFromSd() runs ONCE at boot, BEFORE core1 is launched and before
//      the render loop starts — so there is genuinely no concurrent XIP user.
//   2. We still route every erase/program through the SDK's flash_safe_execute()
//      so the SDK disables interrupts and parks the executing core in RAM for the
//      duration (the erase/program routines themselves run from SRAM, not XIP).
//      Because core1 has not been started, we build with PICO_FLASH_ASSUME_CORE1_
//      SAFE=1 (set on the proteus_emu target) so flash_safe_execute() does not
//      need core1 to have called flash_safe_execute_core_init() — which it can't,
//      since it isn't running yet. This is correct precisely because main() does
//      ALL flashing before it ever calls multicore_launch_core1().

namespace rp2350
{

namespace
{

// XIP window base — flash byte N is memory-mapped at XIP_BASE + N.
constexpr uintptr_t kXipBase = 0x10000000u;

// SD reads are issued one 512-byte sector at a time. The carlk3 SDIO MULTI-block
// read path fails on this board/SDK combo (f_read of >1 sector returns
// FR_DISK_ERR), while single-block reads are rock-solid — so we cap each f_read
// at one sector. A 512 KiB ROM is a one-shot boot load, so the extra commands
// cost only a fraction of a second.
constexpr uint32_t kSdReadSector = 512u;

[[nodiscard]] uint32_t roundUpTo(uint32_t v, uint32_t align)
{
    return (v + (align - 1)) & ~(align - 1);
}

// ── flash_safe_execute trampolines ──────────────────────────────────────────
// flash_safe_execute() takes a void(*)(void*); we pass small POD param structs.

struct EraseArgs
{
    uint32_t offset;  // flash offset (sector-aligned)
    uint32_t count;   // bytes (sector-multiple)
};

void doErase(void* p)
{
    const EraseArgs* a = static_cast<const EraseArgs*>(p);
    // Disable interrupts across the erase: while XIP is off (inside flash_range_
    // erase) any ISR that lives in flash would fault. Harmless on copy_to_ram
    // builds (ISRs are in RAM); REQUIRED on XIP builds (e.g. proteus_gb).
    const uint32_t irq = save_and_disable_interrupts();
    flash_range_erase(a->offset, a->count);
    restore_interrupts(irq);
}

struct ProgramArgs
{
    uint32_t       offset;  // flash offset (page-aligned)
    const uint8_t* data;    // RAM source
    uint32_t       count;   // bytes (page-multiple)
};

void doProgram(void* p)
{
    const ProgramArgs* a = static_cast<const ProgramArgs*>(p);
    const uint32_t irq = save_and_disable_interrupts();   // see doErase()
    flash_range_program(a->offset, a->data, a->count);
    restore_interrupts(irq);
}

// Park-and-run wrappers. flash_safe_execute returns PICO_OK (0) on success.
[[nodiscard]] bool safeErase(uint32_t offset, uint32_t count)
{
    EraseArgs args{ offset, count };
    return flash_safe_execute(doErase, &args, UINT32_MAX) == PICO_OK;
}

[[nodiscard]] bool safeProgram(uint32_t offset, const uint8_t* data,
                               uint32_t count)
{
    ProgramArgs args{ offset, data, count };
    return flash_safe_execute(doProgram, &args, UINT32_MAX) == PICO_OK;
}

// Listing callback: capture the first ".nes" file found, then stop.
struct FirstRom
{
    char name[sd::kMaxNameLen] = {};
    bool found                 = false;
};

[[nodiscard]] bool endsWithNes(const char* name)
{
    size_t n = 0;
    while (name[n] != '\0' && n < sd::kMaxNameLen) { ++n; }
    if (n < 4) { return false; }
    auto lower = [](char c) -> char
    { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; };
    return name[n - 4] == '.' && lower(name[n - 3]) == 'n'
           && lower(name[n - 2]) == 'e' && lower(name[n - 1]) == 's';
}

// ── Catalog record for the flash-ROM cache ──────────────────────────────────
// Stored at XIP_BASE + kRomMetaOffset; lets a re-select of the same game skip the
// (slow, flash-wearing) re-program. Match on name + size; a CRC is deliberately
// avoided — verifying it would mean re-reading the whole SD file, the exact cost
// we are trying to skip. Two distinct ROMs with identical name AND size are not a
// realistic case on a personal card.
constexpr uint32_t kRomMetaMagic = 0x4D524247u;  // 'GBRM'

struct RomMeta
{
    uint32_t magic;
    uint32_t size;
    char     name[64];
};

[[nodiscard]] bool strEqN(const char* a, const char* b, size_t n)
{
    for (size_t i = 0; i < n; ++i)
    {
        if (a[i] != b[i]) { return false; }
        if (a[i] == '\0') { return true; }
    }
    return true;  // ran out of room with all-equal prefix
}

bool captureFirstRom(const sd::RomEntry& entry, void* ctx)
{
    if (!endsWithNes(entry.name)) { return true; }  // keep scanning
    FirstRom* out = static_cast<FirstRom*>(ctx);
    std::memcpy(out->name, entry.name, sd::kMaxNameLen);
    out->found = true;
    return false;  // first .nes match only
}

}  // namespace

FlashRom::Status FlashRom::flashRomFromSd(sd::SdCard& card, uint8_t* scratch,
                                          size_t scratchLen)
{
    m_data  = nullptr;
    m_size  = 0;
    m_valid = false;

    // Need at least one flash page of staging; a sector-sized buffer is ideal.
    if (scratch == nullptr || scratchLen < FLASH_PAGE_SIZE)
    {
        return Status::OpenFailed;
    }
    // Work in whole sectors so erase/program stay aligned; clamp the staging
    // window to a sector multiple.
    const uint32_t chunk = (scratchLen >= FLASH_SECTOR_SIZE)
                               ? (static_cast<uint32_t>(scratchLen)
                                  & ~(FLASH_SECTOR_SIZE - 1))
                               : FLASH_PAGE_SIZE;

    if (!card.cardPresent())
    {
        return Status::NoCard;
    }
    if (!sd::ok(card.mount()))
    {
        return Status::MountFailed;
    }

    FirstRom first;
    if (!sd::ok(card.listRoms("/", captureFirstRom, &first)) || !first.found)
    {
        return Status::NoRom;
    }

    if (!sd::ok(card.open(first.name)))
    {
        return Status::OpenFailed;
    }

    const uint32_t fileSize = card.openFileSize();
    if (fileSize == 0)
    {
        card.close();
        return Status::ReadFailed;
    }
    const uint32_t romBytes = fileSize;  // .nes streamed verbatim (incl. iNES header)
    if (romBytes > kRomFlashCapacity)
    {
        card.close();
        return Status::TooLarge;
    }

    // ── Erase the destination region (4 KiB sector granularity) ─────────────
    // Erase up front, in one safe op, the whole span we will write so a partial
    // earlier image can never bleed through.
    const uint32_t eraseBytes = roundUpTo(romBytes, FLASH_SECTOR_SIZE);
    if (!safeErase(kRomFlashOffset, eraseBytes))
    {
        card.close();
        return Status::ReadFailed;  // flash op refused / failed
    }

    // ── Stream SD → RAM scratch → flash, one staging window at a time ────────
    // flash_range_program requires page-aligned (256 B) lengths; the LAST window
    // of a non-page-multiple ROM is zero-padded up to a page (and, since we work
    // in sector-sized windows, naturally within the erased region). Each f_read
    // is one 512-B sector (see kSdReadSector).
    uint32_t written = 0;
    while (written < romBytes)
    {
        const uint32_t remain = romBytes - written;
        const uint32_t want   = (remain < chunk) ? remain : chunk;

        uint32_t filled = 0;
        while (filled < want)
        {
            const uint32_t toRead = (want - filled > kSdReadSector)
                                        ? kSdReadSector : (want - filled);
            size_t got = 0;
            if (!sd::ok(card.read(scratch + filled, toRead, &got)))
            {
                card.close();
                return Status::ReadFailed;
            }
            if (got == 0)
            {
                card.close();
                return Status::ReadFailed;  // unexpected EOF
            }
            filled += static_cast<uint32_t>(got);
        }

        // Pad the final partial page with 0xFF (erased-flash value) so the
        // program length is page-aligned without altering ROM content.
        const uint32_t progLen = roundUpTo(filled, FLASH_PAGE_SIZE);
        for (uint32_t i = filled; i < progLen; ++i)
        {
            scratch[i] = 0xFFu;
        }

        if (!safeProgram(kRomFlashOffset + written, scratch, progLen))
        {
            card.close();
            return Status::ReadFailed;  // flash op refused / failed
        }
        written += filled;
    }
    card.close();

    // ── Point at the XIP copy ───────────────────────────────────────────────
    m_data = reinterpret_cast<const uint8_t*>(kXipBase + kRomFlashOffset);
    m_size = romBytes;

    // Sanity read-back: confirm XIP sees the bytes we programmed. A .nes starts
    // with the iNES magic "NES\x1A"; if byte 0 still reads erased (0xFF) the
    // program path silently failed — fall back rather than run garbage.
    if (m_data[0] != 'N' || m_data[1] != 'E' || m_data[2] != 'S' || m_data[3] != 0x1A)
    {
        m_data = nullptr;
        m_size = 0;
        return Status::ReadFailed;
    }

    m_valid = true;
    return Status::Ok;
}

bool FlashRom::cachedMatches(const char* name, uint32_t size) const
{
    if (name == nullptr || size == 0) { return false; }
    const RomMeta* m =
        reinterpret_cast<const RomMeta*>(kXipBase + kRomMetaOffset);
    if (m->magic != kRomMetaMagic || m->size != size) { return false; }
    if (!strEqN(m->name, name, sizeof(m->name))) { return false; }
    // Confirm the ROM region itself is still a valid GB image (flash not wiped).
    const uint8_t* rom = reinterpret_cast<const uint8_t*>(kXipBase + kRomFlashOffset);
    return size >= 0x150 && rom[0x104] == 0xCE && rom[0x105] == 0xED;
}

void FlashRom::adoptCached(uint32_t size)
{
    m_data  = reinterpret_cast<const uint8_t*>(kXipBase + kRomFlashOffset);
    m_size  = size;
    m_valid = true;
}

bool FlashRom::cachedInfo(char* nameOut, size_t cap, uint32_t* sizeOut) const
{
    const RomMeta* m =
        reinterpret_cast<const RomMeta*>(kXipBase + kRomMetaOffset);
    if (m->magic != kRomMetaMagic || m->size == 0) { return false; }
    // The catalog record can outlive the ROM region (e.g. a wiped flash); confirm
    // the image is still a valid GB cartridge before offering to play it.
    const uint8_t* rom = reinterpret_cast<const uint8_t*>(kXipBase + kRomFlashOffset);
    if (m->size < 0x150 || rom[0x104] != 0xCE || rom[0x105] != 0xED) { return false; }

    if (nameOut != nullptr && cap > 0)
    {
        size_t i = 0;
        for (; i + 1 < cap && i < sizeof(m->name) && m->name[i] != '\0'; ++i)
        {
            nameOut[i] = m->name[i];
        }
        nameOut[i] = '\0';
    }
    if (sizeOut != nullptr) { *sizeOut = m->size; }
    return true;
}

FlashRom::Status FlashRom::flashOpenFileToXip(sd::SdCard& card, const char* name,
                                              uint8_t* scratch, size_t scratchLen,
                                              void (*progress)(uint32_t, uint32_t))
{
    m_data  = nullptr;
    m_size  = 0;
    m_valid = false;

    if (scratch == nullptr || scratchLen < FLASH_PAGE_SIZE)
    {
        return Status::OpenFailed;
    }
    const uint32_t chunk = (scratchLen >= FLASH_SECTOR_SIZE)
                               ? (static_cast<uint32_t>(scratchLen)
                                  & ~(FLASH_SECTOR_SIZE - 1))
                               : FLASH_PAGE_SIZE;

    // Caller already did card.mount() + card.open(); just measure + stream.
    const uint32_t fileSize = card.openFileSize();
    if (fileSize == 0)
    {
        return Status::ReadFailed;
    }
    const uint32_t romBytes = fileSize;
    if (romBytes > kRomFlashCapacity)
    {
        return Status::TooLarge;
    }

    // Per-window erase+program (NOT one big up-front erase): keeps each
    // interrupts-off window short (~one sector group) so USB-CDC and the LCD
    // progress callback stay alive between windows on XIP (non-copy_to_ram) builds.
    uint32_t written = 0;
    while (written < romBytes)
    {
        const uint32_t remain = romBytes - written;
        const uint32_t want   = (remain < chunk) ? remain : chunk;

        uint32_t filled = 0;
        while (filled < want)
        {
            const uint32_t toRead = (want - filled > kSdReadSector)
                                        ? kSdReadSector : (want - filled);
            size_t got = 0;
            if (!sd::ok(card.read(scratch + filled, toRead, &got)) || got == 0)
            {
                return Status::ReadFailed;
            }
            filled += static_cast<uint32_t>(got);
        }

        // Erase this window's sectors, then program. Pad the tail page with 0xFF.
        const uint32_t eraseLen = roundUpTo(filled, FLASH_SECTOR_SIZE);
        if (!safeErase(kRomFlashOffset + written, eraseLen))
        {
            return Status::ReadFailed;
        }
        const uint32_t progLen = roundUpTo(filled, FLASH_PAGE_SIZE);
        for (uint32_t i = filled; i < progLen; ++i)
        {
            scratch[i] = 0xFFu;
        }
        if (!safeProgram(kRomFlashOffset + written, scratch, progLen))
        {
            return Status::ReadFailed;
        }
        written += filled;
        if (progress != nullptr) { progress(written, romBytes); }
    }

    // Write the catalog record so a future re-select of this ROM can skip the
    // re-flash (cachedMatches/adoptCached). Build it in the now-free scratch page.
    {
        std::memset(scratch, 0xFF, FLASH_PAGE_SIZE);
        RomMeta* meta = reinterpret_cast<RomMeta*>(scratch);
        meta->magic = kRomMetaMagic;
        meta->size  = romBytes;
        std::memset(meta->name, 0, sizeof(meta->name));
        size_t i = 0;
        if (name != nullptr)
        {
            for (; i < sizeof(meta->name) - 1 && name[i] != '\0'; ++i)
            {
                meta->name[i] = name[i];
            }
        }
        // Best-effort: a failed catalog write just forces a re-flash next time.
        if (safeErase(kRomMetaOffset, FLASH_SECTOR_SIZE))
        {
            (void)safeProgram(kRomMetaOffset, scratch, FLASH_PAGE_SIZE);
        }
    }

    m_data  = reinterpret_cast<const uint8_t*>(kXipBase + kRomFlashOffset);
    m_size  = romBytes;
    m_valid = true;
    return Status::Ok;
}

}  // namespace rp2350
