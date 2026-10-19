#include "port/rp2350/phy/PwmSpeakerOut.h"

#include "boards/pins.h"

#include "hardware/gpio.h"
#include "hardware/pwm.h"

namespace rp2350
{
    void PwmSpeakerOut::init()
    {
        gpio_set_function(bsp::kAudio, GPIO_FUNC_PWM);
        m_slice = pwm_gpio_to_slice_num(bsp::kAudio);

        pwm_config cfg = pwm_get_default_config();
        pwm_config_set_wrap(&cfg, kWrap);
        pwm_init(m_slice, &cfg, true);
        setLevel(kWrap / 2);                 // start at silence (centre)
    }

    void PwmSpeakerOut::setLevel(uint16_t duty)
    {
        pwm_set_gpio_level(bsp::kAudio, duty);
    }
}
