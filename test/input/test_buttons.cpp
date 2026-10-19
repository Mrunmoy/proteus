#include "input/MockGpioPort.h"
#include "drivers/input/Buttons.h"

#include <gtest/gtest.h>

namespace
{
    // Scan n times so a held button clears the debounce threshold.
    void scanTimes(drivers::Buttons<MockGpioPort>& btns, int n)
    {
        for (int i = 0; i < n; ++i) { btns.scan(); }
    }
}

// A held button shows up in the debounced state and raises one Down event.
TEST(Buttons, PressDebouncesAndRaisesDown)
{
    MockGpioPort port;
    drivers::Buttons<MockGpioPort> btns(port, /*stableCount=*/2);

    port.press(hal::Button::A);
    scanTimes(btns, 3);                       // > stableCount

    const hal::InputFrame f = btns.poll();
    EXPECT_TRUE(f.isDown(hal::Button::A));
    ASSERT_EQ(f.eventCount, 1);
    EXPECT_EQ(f.events[0].button, hal::Button::A);
    EXPECT_EQ(f.events[0].type, hal::InputEvent::Type::Down);
}

// Releasing a held button clears the state and raises one Up event.
TEST(Buttons, ReleaseRaisesUp)
{
    MockGpioPort port;
    drivers::Buttons<MockGpioPort> btns(port, 2);

    port.press(hal::Button::Start);
    scanTimes(btns, 3);
    (void)btns.poll();                        // drain the Down

    port.release(hal::Button::Start);
    scanTimes(btns, 3);

    const hal::InputFrame f = btns.poll();
    EXPECT_FALSE(f.isDown(hal::Button::Start));
    ASSERT_EQ(f.eventCount, 1);
    EXPECT_EQ(f.events[0].type, hal::InputEvent::Type::Up);
}

// Bounce shorter than the debounce threshold must not flip the state.
TEST(Buttons, BounceBelowThresholdIsIgnored)
{
    MockGpioPort port;
    drivers::Buttons<MockGpioPort> btns(port, /*stableCount=*/3);

    port.press(hal::Button::B);
    btns.scan();                              // 1 agreeing sample (need 3)
    port.release(hal::Button::B);
    btns.scan();                              // bounce back resets the counter

    const hal::InputFrame f = btns.poll();
    EXPECT_FALSE(f.isDown(hal::Button::B));
    EXPECT_EQ(f.eventCount, 0);
}

// Two buttons held at once both register.
TEST(Buttons, MultiplePressesIndependent)
{
    MockGpioPort port;
    drivers::Buttons<MockGpioPort> btns(port, 2);

    port.press(hal::Button::Left);
    port.press(hal::Button::A);
    scanTimes(btns, 3);

    const hal::InputFrame f = btns.poll();
    EXPECT_TRUE(f.isDown(hal::Button::Left));
    EXPECT_TRUE(f.isDown(hal::Button::A));
    EXPECT_FALSE(f.isDown(hal::Button::Right));
}
