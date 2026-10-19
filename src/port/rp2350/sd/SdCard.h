#pragma once

#include <cstddef>
#include <cstdint>

// ── microSD card access (RP2350 port) ───────────────────────────────────────
// A thin, heap-free C++ facade over the vendored carlk3 no-OS-FatFS-SD library
// (external/no-OS-FatFS-SD, Apache-2.0) wired to SPI1 — see sd/hw_config.c and
// boards/pins.h. This lives BELOW the platform contract (it pulls in FatFs/SDK
// headers in the .cpp), so nothing above src/port/ ever sees it.
//
// Scope is what the emulator needs: mount the FAT volume, enumerate ROM files
// (.smc/.sfc), and stream a chosen file into a caller-owned buffer in chunks.
// No dynamic allocation: file names live in fixed buffers and listing is a
// callback so the caller decides where (and whether) to store entries.

namespace sd
{

// Error codes: 0 == success, negative == failure.
enum class Status : int32_t
{
    Ok          = 0,
    NoCard      = -1,   // card-detect says the socket is empty
    MountFailed = -2,   // f_mount / driver init failed
    NotMounted  = -3,   // operation needs a mounted volume
    OpenFailed  = -4,   // f_open failed (missing file, bad path)
    ReadFailed  = -5,   // f_read returned an error
    BadArg      = -6,   // null pointer / zero-length buffer
    EndOfDir    = -7,   // directory enumeration exhausted
};

[[nodiscard]] constexpr bool ok(Status s)
{
    return s == Status::Ok;
}

// Maximum file-name length we surface (LFN). FatFs LFN buffers are larger; we
// cap at a ROM-friendly length to keep entries on the stack.
constexpr size_t kMaxNameLen = 64;

// One directory entry handed to the listing callback. Borrowed for the duration
// of the call only; copy out anything you need to keep.
struct RomEntry
{
    char     name[kMaxNameLen] = {};  // NUL-terminated file name (no path)
    uint32_t sizeBytes         = 0;   // file size as reported by FatFs
};

// Listing callback: return true to continue enumeration, false to stop early.
using RomEntryFn = bool (*)(const RomEntry& entry, void* ctx);

// ── Card facade ─────────────────────────────────────────────────────────────
class SdCard
{
public:
    SdCard()                          = default;
    SdCard(const SdCard&)             = delete;
    SdCard& operator=(const SdCard&)  = delete;
    SdCard(SdCard&&)                  = delete;
    SdCard& operator=(SdCard&&)       = delete;

    // ── Mounting ────────────────────────────────────────────────────────────
    // Initialise the SD driver (idempotent) and mount the FAT volume. Returns
    // NoCard if the socket is empty, MountFailed on driver/FS error.
    [[nodiscard]] Status mount();

    // Unmount the volume. Safe to call when not mounted.
    void unmount();

    [[nodiscard]] bool mounted() const { return m_mounted; }

    // True if the card-detect line reports a card is seated.
    [[nodiscard]] bool cardPresent() const;

    // ── Listing ───────────────────────────────────────────────────────────
    // Enumerate ROM files (case-insensitive .smc/.sfc) in `dir` ("" or "/" =
    // volume root). Invokes `fn(entry, ctx)` per match; stops early if `fn`
    // returns false. Returns Ok on full/early completion, NotMounted, or
    // OpenFailed if the directory cannot be opened.
    [[nodiscard]] Status listRoms(const char* dir, RomEntryFn fn, void* ctx);

    // ── Reading ─────────────────────────────────────────────────────────────
    // Open `path` for reading. Only one file is open at a time per SdCard.
    // Returns OpenFailed if the file is missing or already open.
    [[nodiscard]] Status open(const char* path);

    // Read up to `cap` bytes into `dst`, advancing the file cursor. Sets
    // `*outRead` to the count actually read (0 at EOF). Chunked reads of a
    // large ROM are the intended pattern.
    [[nodiscard]] Status read(uint8_t* dst, size_t cap, size_t* outRead);

    // Size in bytes of the currently open file (0 if none open).
    [[nodiscard]] uint32_t openFileSize() const { return m_openSize; }

    // Rewind the open file cursor to the start.
    [[nodiscard]] Status seekStart();

    // Close the open file. Safe to call when nothing is open.
    void close();

private:
    bool     m_mounted  = false;
    bool     m_fileOpen = false;
    uint32_t m_openSize = 0;
};

}  // namespace sd
