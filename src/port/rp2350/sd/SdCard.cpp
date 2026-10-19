#include "sd/SdCard.h"

#include "f_util.h"
#include "ff.h"
#include "hw_config.h"
#include "sd_card.h"

#include <cstring>

namespace sd
{

namespace
{

// One card, one open file — keep the heavy FatFs objects out of the header so
// the platform contract above src/port/ never sees an SDK type. Single-threaded
// game-load path; not re-entrant.
FIL g_file;

// Resolve the (only) configured card and its FatFs drive prefix ("0:" by
// default). sd_init_driver() must have run first.
[[nodiscard]] sd_card_t* card0()
{
    return sd_get_num() > 0 ? sd_get_by_num(0) : nullptr;
}

}  // namespace

bool SdCard::cardPresent() const
{
    sd_card_t* card = card0();
    if (card == nullptr)
    {
        return false;
    }
    return sd_card_detect(card);
}

Status SdCard::mount()
{
    if (m_mounted)
    {
        return Status::Ok;
    }

    // sd_init_driver() is idempotent inside the library.
    if (!sd_init_driver())
    {
        return Status::MountFailed;
    }

    sd_card_t* card = card0();
    if (card == nullptr)
    {
        return Status::MountFailed;
    }
    if (!sd_card_detect(card))
    {
        return Status::NoCard;
    }

    const char*   prefix = sd_get_drive_prefix(card);
    const FRESULT fr     = f_mount(&card->state.fatfs, prefix, 1 /* mount now */);
    if (fr != FR_OK)
    {
        return Status::MountFailed;
    }

    m_mounted = true;
    return Status::Ok;
}

void SdCard::unmount()
{
    if (!m_mounted)
    {
        return;
    }
    close();

    sd_card_t* card = card0();
    if (card != nullptr)
    {
        f_unmount(sd_get_drive_prefix(card));
    }
    m_mounted = false;
}

Status SdCard::listRoms(const char* dir, RomEntryFn fn, void* ctx)
{
    if (!m_mounted)
    {
        return Status::NotMounted;
    }
    if (fn == nullptr)
    {
        return Status::BadArg;
    }

    DIR     dp;
    FILINFO info;
    const char* path = (dir == nullptr || dir[0] == '\0') ? "/" : dir;

    FRESULT fr = f_opendir(&dp, path);
    if (fr != FR_OK)
    {
        return Status::OpenFailed;
    }

    Status result = Status::Ok;
    for (;;)
    {
        fr = f_readdir(&dp, &info);
        if (fr != FR_OK || info.fname[0] == '\0')
        {
            break;  // error or end of directory
        }
        if (info.fattrib & AM_DIR)
        {
            continue;  // files only, no recursion
        }
        // Emulator-agnostic: pass every regular file to the callback and let IT
        // decide what's a ROM (e.g. ".nes"). The SD layer hardcodes no console
        // extension — that would couple the platform to one emulator.

        RomEntry entry;
        std::strncpy(entry.name, info.fname, kMaxNameLen - 1);
        entry.name[kMaxNameLen - 1] = '\0';
        entry.sizeBytes             = static_cast<uint32_t>(info.fsize);

        if (!fn(entry, ctx))
        {
            break;  // caller asked to stop
        }
    }

    f_closedir(&dp);
    return result;
}

Status SdCard::open(const char* path)
{
    if (!m_mounted)
    {
        return Status::NotMounted;
    }
    if (path == nullptr || path[0] == '\0')
    {
        return Status::BadArg;
    }
    if (m_fileOpen)
    {
        return Status::OpenFailed;  // one file at a time
    }

    const FRESULT fr = f_open(&g_file, path, FA_READ);
    if (fr != FR_OK)
    {
        return Status::OpenFailed;
    }

    m_fileOpen = true;
    m_openSize = static_cast<uint32_t>(f_size(&g_file));
    return Status::Ok;
}

Status SdCard::read(uint8_t* dst, size_t cap, size_t* outRead)
{
    if (outRead != nullptr)
    {
        *outRead = 0;
    }
    if (!m_fileOpen)
    {
        return Status::OpenFailed;
    }
    if (dst == nullptr || cap == 0)
    {
        return Status::BadArg;
    }

    UINT          br = 0;
    const FRESULT fr = f_read(&g_file, dst, static_cast<UINT>(cap), &br);
    if (fr != FR_OK)
    {
        return Status::ReadFailed;
    }
    if (outRead != nullptr)
    {
        *outRead = static_cast<size_t>(br);
    }
    return Status::Ok;
}

Status SdCard::seekStart()
{
    if (!m_fileOpen)
    {
        return Status::OpenFailed;
    }
    return (f_lseek(&g_file, 0) == FR_OK) ? Status::Ok : Status::ReadFailed;
}

void SdCard::close()
{
    if (!m_fileOpen)
    {
        return;
    }
    f_close(&g_file);
    m_fileOpen = false;
    m_openSize = 0;
}

}  // namespace sd
