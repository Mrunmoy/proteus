// Build-only contract check. Instantiates each generic driver against its concrete
// RP2350 PHY and exercises every method, so the compiler proves the PHYs satisfy
// the driver templates and build against the SDK. Nothing calls phyContractCheck();
// it exists purely so the link of the proteus_phy_check library type-checks the
// whole driver+PHY layer ahead of the app rewire. Removed once app/gb + app/mfg
// instantiate these for real.

#include "drivers/audio/PwmSpeaker.h"
#include "drivers/display/St7789.h"
#include "drivers/input/Buttons.h"

#include "port/rp2350/phy/GpioButtonPort.h"
#include "port/rp2350/phy/PwmSpeakerOut.h"
#include "port/rp2350/phy/SpiDisplayBus.h"

namespace rp2350
{
    void phyContractCheck(const uint16_t* frame)
    {
        SpiDisplayBus lcdBus;
        lcdBus.init();
        drivers::St7789<SpiDisplayBus> lcd(lcdBus);
        lcd.init();
        lcd.setBacklight(255);
        lcd.present(frame, 160, 144);

        GpioButtonPort buttonPort;
        buttonPort.init();
        drivers::Buttons<GpioButtonPort> buttons(buttonPort);
        buttons.scan();
        (void)buttons.poll();

        PwmSpeakerOut pwm;
        pwm.init();
        drivers::PwmSpeaker<PwmSpeakerOut> speaker(pwm, pwm.wrap());
        const int16_t sample = 0;
        speaker.push(&sample, 1);
        speaker.feedSample();
    }
}
