#ifndef PROTEUS_DEVICE_INFO_H
#define PROTEUS_DEVICE_INFO_H

/* ── Device-info block — the shared identity contract ───────────────────────
 * The single seam between the bootloader, the manufacturing test, and any app.
 * Lives in a reserved, rewritable region at the TOP of flash (survives app
 * reflash and the bootrom->custom-bootloader transition).
 *
 * WRITER: picotool / the PC (static fields at programming, serial at serialize,
 *         in BOOTSEL). The device firmware ONLY READS this block (XIP).
 *
 * Plain C, no hardware/SDK headers — usable by firmware (C/C++), the bootloader,
 * and host tools. The host (tools/mfg/device_info.py) builds the identical
 * byte layout; keep the two in sync.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Layout constants ───────────────────────────────────────────────────── */

#define DEVICE_INFO_MAGIC          0x544F5250u  /* "PROT" little-endian (P,R,O,T) */
#define DEVICE_INFO_STRUCT_VERSION 1u

/* 16 KB reserved at the top of the 16 MB flash; two 4 KB A/B copies + spare. */
#define DEVICE_INFO_FLASH_OFFSET   0x00FFC000u  /* offset within the flash chip   */
#define DEVICE_INFO_XIP_ADDR       0x10FFC000u  /* CPU read address (XIP)          */
#define DEVICE_INFO_REGION_SIZE    0x00004000u  /* 16 KB reserved                  */
#define DEVICE_INFO_SECTOR_SIZE    0x00001000u  /* 4 KB flash erase granularity    */
#define DEVICE_INFO_COPY_A_OFFSET  0x0u
#define DEVICE_INFO_COPY_B_OFFSET  DEVICE_INFO_SECTOR_SIZE  /* +4 KB: A/B redundancy */

/* All char fields are NUL-terminated C strings: usable length is LEN-1. */
#define DEVICE_INFO_MODEL_LEN   16
#define DEVICE_INFO_VARIANT_LEN 16
#define DEVICE_INFO_SERIAL_LEN  24

/* ── The block (fixed wire layout, little-endian, packed = 80 bytes) ──────── */

typedef struct __attribute__((packed))
{
    uint32_t magic;                          /* DEVICE_INFO_MAGIC               */
    uint16_t struct_ver;                     /* DEVICE_INFO_STRUCT_VERSION      */
    uint16_t hw_rev;                         /* board hardware revision         */
    uint16_t vid;                            /* USB vendor id                   */
    uint16_t pid;                            /* USB product id                  */
    char     model[DEVICE_INFO_MODEL_LEN];   /* e.g. "RP2350-PiZero"  ── TYPE: written at PROGRAMMING */
    char     variant[DEVICE_INFO_VARIANT_LEN];
    char     serial[DEVICE_INFO_SERIAL_LEN]; /* NUL-padded ── UNIT: written at SERIALIZE (empty until) */
    uint64_t serial_utc;                     /* serialize timestamp (UTC seconds)*/
    uint32_t crc32;                          /* CRC-32 over the 76 preceding bytes */
} DeviceInfo;

#define DEVICE_INFO_CRC_REGION (sizeof(DeviceInfo) - sizeof(uint32_t))  /* 76 */

/* ── API ────────────────────────────────────────────────────────────────── */

/* Standard IEEE CRC-32 (matches zlib.crc32 on the host side). */
uint32_t device_info_crc32(const void* data, uint32_t len);

/* Validate + copy a DeviceInfo from a region base pointer (magic + version +
 * CRC). Portable + host-testable. Returns true if valid. */
bool device_info_parse(const void* region_base, DeviceInfo* out);

/* Firmware convenience: read+validate from the flash region via XIP. Tries the
 * A copy, then the B copy. Returns true if a valid block was found. */
bool device_info_read(DeviceInfo* out);

/* Is the unit serialized yet? (serial[0] != 0) */
static inline bool device_info_is_serialized(const DeviceInfo* di)
{
    return di->serial[0] != '\0';
}

#ifdef __cplusplus
}
#endif

#endif /* PROTEUS_DEVICE_INFO_H */
