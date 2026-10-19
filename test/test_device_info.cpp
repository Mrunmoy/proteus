// ── device-info block (host) ────────────────────────────────────────────────
// The shared identity contract (src/common/device_info.h). The cross-language
// test pins the byte layout: it parses a block built by tools/mfg/device_info.py
// and asserts the C struct sees the same fields — so firmware and the host writer
// can never silently disagree.

extern "C"
{
#include "common/device_info.h"
}

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

namespace
{
    // 80 bytes, emitted by device_info.py: model "RP2350-PiZero", vid 0x2E8A,
    // pid 0x4001, hw_rev 1, variant "GamePi13", serial "GE-2026-000123".
    const uint8_t kGolden[] = {
        0x50, 0x52, 0x4f, 0x54, 0x01, 0x00, 0x01, 0x00, 0x8a, 0x2e, 0x01, 0x40,
        0x52, 0x50, 0x32, 0x33, 0x35, 0x30, 0x2d, 0x50, 0x69, 0x5a, 0x65, 0x72,
        0x6f, 0x00, 0x00, 0x00, 0x47, 0x61, 0x6d, 0x65, 0x50, 0x69, 0x31, 0x33,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x47, 0x45, 0x2d, 0x32,
        0x30, 0x32, 0x36, 0x2d, 0x30, 0x30, 0x30, 0x31, 0x32, 0x33, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xe1, 0x4e, 0x68,
        0x00, 0x00, 0x00, 0x00, 0xc8, 0xee, 0xaa, 0x74,
    };
}

TEST(DeviceInfo, StructIs80BytesPacked)
{
    EXPECT_EQ(sizeof(DeviceInfo), 80u);
    EXPECT_EQ(DEVICE_INFO_CRC_REGION, 76u);
}

TEST(DeviceInfo, KnownCrc32Vector)
{
    // zlib.crc32("123456789") == 0xCBF43926 — proves the polynomial matches host.
    EXPECT_EQ(device_info_crc32("123456789", 9), 0xCBF43926u);
}

TEST(DeviceInfo, ParsesHostBuiltBlock)
{
    ASSERT_EQ(sizeof(kGolden), 80u);
    DeviceInfo di;
    ASSERT_TRUE(device_info_parse(kGolden, &di));   // C reads what Python wrote
    EXPECT_EQ(di.vid, 0x2E8Au);
    EXPECT_EQ(di.pid, 0x4001u);
    EXPECT_EQ(di.hw_rev, 1u);
    EXPECT_STREQ(di.model, "RP2350-PiZero");
    EXPECT_STREQ(di.variant, "GamePi13");
    EXPECT_STREQ(di.serial, "GE-2026-000123");
    EXPECT_TRUE(device_info_is_serialized(&di));
}

TEST(DeviceInfo, RejectsBadMagicAndCrc)
{
    uint8_t buf[80];
    std::memcpy(buf, kGolden, sizeof(buf));

    buf[0] ^= 0xFF;                                  // corrupt magic
    EXPECT_FALSE(device_info_parse(buf, nullptr));

    std::memcpy(buf, kGolden, sizeof(buf));
    buf[44] ^= 0xFF;                                 // corrupt a serial byte → CRC fails
    EXPECT_FALSE(device_info_parse(buf, nullptr));
}

TEST(DeviceInfo, UnserializedBlockDetected)
{
    // Build a static-only block in C (empty serial) and confirm it's "not serialized".
    DeviceInfo di;
    std::memset(&di, 0, sizeof(di));
    di.magic = DEVICE_INFO_MAGIC;
    di.struct_ver = DEVICE_INFO_STRUCT_VERSION;
    di.vid = 0x2E8A;
    di.pid = 0x4001;
    std::strncpy(di.model, "RP2350-PiZero", sizeof(di.model) - 1);
    di.crc32 = device_info_crc32(&di, DEVICE_INFO_CRC_REGION);

    DeviceInfo out;
    ASSERT_TRUE(device_info_parse(&di, &out));
    EXPECT_FALSE(device_info_is_serialized(&out));   // serial empty until serialize
}
