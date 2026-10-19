#include "audio/MockPwmOut.h"
#include "drivers/audio/PwmSpeaker.h"

#include <gtest/gtest.h>

namespace
{
    constexpr uint16_t kWrap = 1023;       // 10-bit PWM; idle = 511
    constexpr uint16_t kIdle = kWrap / 2;
    using Speaker = drivers::PwmSpeaker<MockPwmOut, 8>;   // tiny ring for overflow test
}

// Silence (sample 0) sits at half scale; full-scale swings to the rails.
TEST(PwmSpeaker, SampleMapsToDutyAroundIdle)
{
    MockPwmOut pwm;
    Speaker spk(pwm, kWrap);

    const int16_t samples[] = {0, 32767, -32768};
    spk.push(samples, 3);
    spk.feedSample();
    spk.feedSample();
    spk.feedSample();

    ASSERT_EQ(pwm.levels.size(), 3u);
    EXPECT_EQ(pwm.levels[0], kIdle);             // silence → idle
    EXPECT_GT(pwm.levels[1], kIdle);             // +full → above idle
    EXPECT_LT(pwm.levels[2], kIdle);             // -full → below idle
}

// Popping an empty ring counts an underrun and holds the previous duty (no click).
TEST(PwmSpeaker, UnderrunHoldsLastSample)
{
    MockPwmOut pwm;
    Speaker spk(pwm, kWrap);

    const int16_t s = 20000;
    spk.push(&s, 1);
    spk.feedSample();                            // consumes the one sample
    spk.feedSample();                            // ring empty → hold

    ASSERT_EQ(pwm.levels.size(), 2u);
    EXPECT_EQ(pwm.levels[0], pwm.levels[1]);     // held, not jumped to idle
    EXPECT_EQ(spk.stats().underruns, 1u);
}

// push() beyond ring capacity drops the overflow instead of corrupting the ring.
TEST(PwmSpeaker, OverflowDropsNotCorrupts)
{
    MockPwmOut pwm;
    Speaker spk(pwm, kWrap);                     // usable capacity = 8 - 1 = 7

    int16_t many[20];
    for (int i = 0; i < 20; ++i) { many[i] = static_cast<int16_t>(i); }
    spk.push(many, 20);

    EXPECT_EQ(spk.stats().pushed, 7u);
    EXPECT_GT(spk.stats().dropped, 0u);
}

// Producer/consumer accounting stays consistent across interleaved push/feed.
TEST(PwmSpeaker, FillTracksPushMinusConsume)
{
    MockPwmOut pwm;
    Speaker spk(pwm, kWrap);

    const int16_t s[] = {1, 2, 3, 4};
    spk.push(s, 4);
    spk.feedSample();
    spk.feedSample();

    EXPECT_EQ(spk.stats().fill, 2u);             // 4 pushed - 2 consumed
    EXPECT_EQ(spk.stats().consumed, 2u);
}
