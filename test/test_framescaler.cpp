#include "common/FrameScaler.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

namespace
{
using common::blitScaledCentered;
using common::computeScale;

// Emulator-agnostic: the scaler takes source dimensions as parameters. These
// tests pick a representative source size; nothing is hardcoded in the scaler.
constexpr uint32_t kSrcW = 256;
constexpr uint32_t kSrcH = 224;

// RGB565 sentinels (the PPU's output format, which the scaler passes through
// unchanged): {r[15:11], g[10:5], b[4:0]}.
constexpr uint16_t kBlack = 0x0000;
constexpr uint16_t kWhite = 0xFFFF;
constexpr uint16_t kRed   = 0xF800;  // R=31
constexpr uint16_t kGreen = 0x07E0;  // G=63

TEST(FrameScaler, ComputeScaleChoosesLargestWholeFit)
{
    EXPECT_EQ(computeScale(kSrcW, kSrcH, 320, 240), 1u);   // 256×224 fits once
    EXPECT_EQ(computeScale(kSrcW, kSrcH, 640, 480), 2u);   // fits twice (512×448)
    EXPECT_EQ(computeScale(kSrcW, kSrcH, 256, 224), 1u);   // exact
    EXPECT_EQ(computeScale(kSrcW, kSrcH, 100, 100), 1u);   // smaller than source → 1
    EXPECT_EQ(computeScale(kSrcW, kSrcH, 1024, 896), 4u);
    // A different source size (e.g. NES 256×240) scales independently.
    EXPECT_EQ(computeScale(256, 240, 512, 480), 2u);
}

TEST(FrameScaler, BlitCentersWithBorderAt1x)
{
    constexpr uint32_t kW = 320;
    constexpr uint32_t kH = 240;
    std::vector<uint16_t> src(kSrcW * kSrcH, kWhite);  // all white
    std::vector<uint16_t> dst(kW * kH, 0xDEAD);

    blitScaledCentered(src.data(), kSrcW, kSrcH, dst.data(), kW, kH, kW, /*border=*/kBlack);

    const uint32_t offX = (kW - kSrcW) / 2;   // 32
    const uint32_t offY = (kH - kSrcH) / 2;   // 8

    // Corners are border (black).
    EXPECT_EQ(dst[0], kBlack);
    EXPECT_EQ(dst[(kH - 1) * kW + (kW - 1)], kBlack);
    // Just outside the active region on the left edge of the middle row.
    EXPECT_EQ(dst[(offY + 10) * kW + (offX - 1)], kBlack);
    // Top-left of the active region is white.
    EXPECT_EQ(dst[offY * kW + offX], kWhite);
    // Bottom-right of the active region is white.
    EXPECT_EQ(dst[(offY + kSrcH - 1) * kW + (offX + kSrcW - 1)], kWhite);
}

TEST(FrameScaler, Blit2xReplicatesEachSourcePixelIntoA2x2Block)
{
    constexpr uint32_t kW = 640;
    constexpr uint32_t kH = 480;
    std::vector<uint16_t> src(kSrcW * kSrcH, kBlack);
    src[0] = kRed;  // a single red pixel at source (0,0)
    std::vector<uint16_t> dst(kW * kH, 0x1234);

    blitScaledCentered(src.data(), kSrcW, kSrcH, dst.data(), kW, kH, kW, /*border=*/kBlack);

    const uint32_t offX = (kW - kSrcW * 2) / 2;   // 64
    const uint32_t offY = (kH - kSrcH * 2) / 2;   // 16

    // The red source pixel becomes a 2×2 block of (unchanged) RGB565 red.
    for (uint32_t dy = 0; dy < 2; ++dy)
    {
        for (uint32_t dx = 0; dx < 2; ++dx)
        {
            EXPECT_EQ(dst[(offY + dy) * kW + (offX + dx)], kRed)
                << "dx=" << dx << " dy=" << dy;
        }
    }
    // The neighbouring block (source pixel (1,0)) is black, not red.
    EXPECT_EQ(dst[offY * kW + (offX + 2)], kBlack);
}

TEST(FrameScaler, BlitClipsWhenTargetSmallerThanSource)
{
    // 128×112 target: scale floors to 1, source is wider/taller than dst, so the
    // blit must clip without writing out of bounds (run under ASan to confirm).
    constexpr uint32_t kW = 128;
    constexpr uint32_t kH = 112;
    std::vector<uint16_t> src(kSrcW * kSrcH, kGreen);
    std::vector<uint16_t> dst(kW * kH, kBlack);

    blitScaledCentered(src.data(), kSrcW, kSrcH, dst.data(), kW, kH, kW, /*border=*/kBlack);

    // Origin maps 1:1 (offsets are 0 because the frame overflows the target).
    EXPECT_EQ(dst[0], kGreen);
    EXPECT_EQ(dst[kW * kH - 1], kGreen);
}
}  // namespace
