#include "port/rp2350/flash/DiagStore.h"

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/flash.h"

#include <cstdint>
#include <cstring>

namespace rp2350
{
    namespace
    {
        // One sector below the FlashRom ROM metadata (kRomMetaOffset = 0x3FF000),
        // so it never overlaps the firmware (low offsets), the ROM region (>=4 MiB),
        // or the device-info block (0xFFC000).
        constexpr uint32_t  kDiagFlashOffset = 0x3FE000u;
        constexpr uint32_t  kSectorSize      = 4096u;
        constexpr uint32_t  kPageSize        = 256u;   // flash_range_program granularity
        constexpr uintptr_t kXipBase         = 0x10000000u;

        // The capsule is tiny (lifecycle payload is 16 B + header); one page holds
        // it with room to spare. Caller-owned staging buffer for diag save/load.
        uint8_t g_capsuleBuffer[kPageSize];

        struct EraseArgs   { uint32_t offset; uint32_t count; };
        struct ProgramArgs { uint32_t offset; const uint8_t* data; uint32_t count; };

        void doErase(void* p)
        {
            const EraseArgs* a = static_cast<const EraseArgs*>(p);
            const uint32_t irq = save_and_disable_interrupts();
            flash_range_erase(a->offset, a->count);
            restore_interrupts(irq);
        }

        void doProgram(void* p)
        {
            const ProgramArgs* a = static_cast<const ProgramArgs*>(p);
            const uint32_t irq = save_and_disable_interrupts();
            flash_range_program(a->offset, a->data, a->count);
            restore_interrupts(irq);
        }

        enum diag_result storeLoad(void* /*user*/, uint8_t* buffer, size_t bufferSize,
                                   size_t* bytesRead)
        {
            const uint8_t* xip = reinterpret_cast<const uint8_t*>(kXipBase + kDiagFlashOffset);
            const size_t   n   = bufferSize < kPageSize ? bufferSize : kPageSize;
            std::memcpy(buffer, xip, n);
            *bytesRead = n;   // capsule header carries the real length; trailing 0xFF is ignored
            return DIAG_OK;
        }

        enum diag_result storeSave(void* /*user*/, const uint8_t* buffer, size_t size)
        {
            if (size > kPageSize) { return DIAG_ERROR_CAPACITY; }

            // flash_range_program needs a whole page; pad the capsule with the erase value.
            uint8_t page[kPageSize];
            std::memset(page, 0xFF, sizeof page);
            std::memcpy(page, buffer, size);

            EraseArgs ea{kDiagFlashOffset, kSectorSize};
            if (flash_safe_execute(doErase, &ea, UINT32_MAX) != PICO_OK)
            {
                return DIAG_ERROR_STORAGE;
            }
            ProgramArgs pa{kDiagFlashOffset, page, kPageSize};
            if (flash_safe_execute(doProgram, &pa, UINT32_MAX) != PICO_OK)
            {
                return DIAG_ERROR_STORAGE;
            }
            return DIAG_OK;
        }

        enum diag_result storeClear(void* /*user*/)
        {
            EraseArgs ea{kDiagFlashOffset, kSectorSize};
            return flash_safe_execute(doErase, &ea, UINT32_MAX) == PICO_OK ? DIAG_OK
                                                                           : DIAG_ERROR_STORAGE;
        }

        const struct diag_storage_ops kOps = {storeLoad, storeSave, storeClear};

        const struct diag_storage kStorage = {
            &kOps,
            nullptr,
            {0xFFu, 1u, DIAG_STORAGE_ATOMIC_COMMIT_NONE, DIAG_STORAGE_WEAR_LEVELING_NONE},
            g_capsuleBuffer,
            sizeof g_capsuleBuffer,
        };
    }

    const struct diag_storage* diagFlashStorage()
    {
        return &kStorage;
    }
}
