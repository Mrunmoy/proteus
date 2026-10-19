#pragma once

#include <cstdint>
#include <vector>

// Recording PWM output for host tests: satisfies the hal/PwmOut contract and logs
// every duty the speaker driver sets. Test-only.
struct MockPwmOut
{
    std::vector<uint16_t> levels;
    void setLevel(uint16_t duty) { levels.push_back(duty); }
};
