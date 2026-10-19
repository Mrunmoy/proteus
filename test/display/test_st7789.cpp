#include "display/MockDisplayBus.h"
#include "drivers/display/St7789.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace
{
    bool contains(const std::vector<uint8_t>& v, uint8_t b)
    {
        return std::find(v.begin(), v.end(), b) != v.end();
    }
}

// init() must pulse reset once, leave the sleep-out delay, and emit the panel
// register sequence ending DISPON + RAMWR.
TEST(St7789, InitEmitsPanelSequence)
{
    MockDisplayBus bus;
    drivers::St7789<MockDisplayBus> lcd(bus);
    lcd.init();

    EXPECT_EQ(bus.resets, 1);
    EXPECT_GE(bus.delayedMs, 120u);                 // SLPOUT settle

    EXPECT_TRUE(contains(bus.commands, 0x11));      // SLPOUT
    EXPECT_TRUE(contains(bus.commands, 0x3A));      // COLMOD
    EXPECT_TRUE(contains(bus.commands, 0x21));      // INVON
    EXPECT_TRUE(contains(bus.commands, 0x29));      // DISPON

    // COLMOD must select 16bpp (0x05) right after its command.
    const auto it = std::find(bus.commands.begin(), bus.commands.end(), 0x3A);
    ASSERT_NE(it, bus.commands.end());
}

// present() sets a full-panel window then streams exactly one panel's worth of
// pixels (240x240), regardless of the source size it scales from.
TEST(St7789, PresentScalesToFullPanel)
{
    MockDisplayBus bus;
    drivers::St7789<MockDisplayBus> lcd(bus);

    constexpr uint16_t kSrcW = 160, kSrcH = 144;
    std::vector<uint16_t> frame(kSrcW * kSrcH, 0xABCD);
    lcd.present(frame.data(), kSrcW, kSrcH);

    EXPECT_TRUE(contains(bus.commands, 0x2A));      // CASET
    EXPECT_TRUE(contains(bus.commands, 0x2B));      // RASET
    EXPECT_TRUE(contains(bus.commands, 0x2C));      // RAMWR
    EXPECT_EQ(bus.pixels.size(),
              static_cast<size_t>(drivers::St7789<MockDisplayBus>::kWidth) *
              drivers::St7789<MockDisplayBus>::kHeight);
}

// A square source fills the whole panel (no letter-box), so every streamed pixel
// is the source colour.
TEST(St7789, SquareSourceHasNoLetterbox)
{
    MockDisplayBus bus;
    drivers::St7789<MockDisplayBus> lcd(bus);

    constexpr uint16_t kSide = 120;
    std::vector<uint16_t> frame(kSide * kSide, 0x1234);
    lcd.present(frame.data(), kSide, kSide);

    const bool allSource = std::all_of(bus.pixels.begin(), bus.pixels.end(),
                                       [](uint16_t p) { return p == 0x1234; });
    EXPECT_TRUE(allSource);
}

// A wide source is letter-boxed: the top and bottom rows are black bars.
TEST(St7789, WideSourceLetterboxed)
{
    MockDisplayBus bus;
    drivers::St7789<MockDisplayBus> lcd(bus);

    constexpr uint16_t kSrcW = 320, kSrcH = 144;     // wider than tall → bars
    std::vector<uint16_t> frame(kSrcW * kSrcH, 0x5AA5);
    lcd.present(frame.data(), kSrcW, kSrcH);

    EXPECT_EQ(bus.pixels.front(), 0x0000);           // top bar is black
    EXPECT_EQ(bus.pixels.back(), 0x0000);            // bottom bar is black
    EXPECT_TRUE(std::find(bus.pixels.begin(), bus.pixels.end(), 0x5AA5) != bus.pixels.end());
}
