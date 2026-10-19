#include "common/Debouncer.h"

#include <gtest/gtest.h>

using common::Debouncer;

TEST(Debouncer, StartsLow)
{
    Debouncer d{3};
    EXPECT_FALSE(d.state());
}

TEST(Debouncer, FlipsOnlyAfterStableCount)
{
    Debouncer d{3};
    EXPECT_FALSE(d.update(true));   // 1
    EXPECT_FALSE(d.update(true));   // 2
    EXPECT_TRUE(d.update(true));    // 3 → flips
    EXPECT_TRUE(d.state());
}

TEST(Debouncer, GlitchShorterThanWindowIsIgnored)
{
    Debouncer d{3};
    d.update(true);                 // 1 high
    d.update(true);                 // 2 high (not yet stable)
    EXPECT_FALSE(d.update(false));  // glitch low resets the counter
    EXPECT_FALSE(d.update(true));   // 1
    EXPECT_FALSE(d.update(true));   // 2
    EXPECT_TRUE(d.update(true));    // 3 clean highs → flips
}

TEST(Debouncer, ReleaseIsDebouncedToo)
{
    Debouncer d{2};
    d.update(true);
    ASSERT_TRUE(d.update(true));    // pressed
    EXPECT_TRUE(d.update(false));   // 1 low — not yet
    EXPECT_FALSE(d.update(false));  // 2 lows → released
}
