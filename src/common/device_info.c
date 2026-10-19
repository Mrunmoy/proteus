#include "common/device_info.h"

#include <string.h>

/* ── IEEE CRC-32 (reflected, init 0xFFFFFFFF, final XOR) = zlib.crc32 ──────── */

uint32_t device_info_crc32(const void* data, uint32_t len)
{
    const uint8_t* p = (const uint8_t*)data;
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; ++i)
    {
        crc ^= p[i];
        for (int b = 0; b < 8; ++b)
        {
            uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

bool device_info_parse(const void* region_base, DeviceInfo* out)
{
    DeviceInfo di;
    memcpy(&di, region_base, sizeof(di));

    if (di.magic != DEVICE_INFO_MAGIC) { return false; }
    if (di.struct_ver != DEVICE_INFO_STRUCT_VERSION) { return false; }
    if (device_info_crc32(&di, DEVICE_INFO_CRC_REGION) != di.crc32) { return false; }

    if (out) { *out = di; }
    return true;
}

bool device_info_read(DeviceInfo* out)
{
    const uint8_t* base = (const uint8_t*)(uintptr_t)DEVICE_INFO_XIP_ADDR;
    if (device_info_parse(base + DEVICE_INFO_COPY_A_OFFSET, out)) { return true; }
    if (device_info_parse(base + DEVICE_INFO_COPY_B_OFFSET, out)) { return true; }
    return false;
}
