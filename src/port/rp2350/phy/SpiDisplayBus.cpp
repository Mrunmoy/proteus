#include "port/rp2350/phy/SpiDisplayBus.h"

#include "boards/pins.h"

#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

namespace rp2350
{
    namespace
    {
        spi_inst_t* const kSpi = spi1;          // GamePi13 LCD is on SPI1

        // clk_peri follows clk_sys; at 300 MHz the achievable rates are 300/(even
        // divisor) → 75 MHz = clk_sys/4. Drop to 50 MHz if a panel can't take 75.
        constexpr uint32_t kSpiHz = 75u * 1000u * 1000u;

        inline void csLow()  { gpio_put(bsp::kLcdCs, 0); }
        inline void csHigh() { gpio_put(bsp::kLcdCs, 1); }
        inline void dcCmd()  { gpio_put(bsp::kLcdDc, 0); }
        inline void dcData() { gpio_put(bsp::kLcdDc, 1); }
    }

    void SpiDisplayBus::init()
    {
        m_spiHz = spi_init(kSpi, kSpiHz);
        gpio_set_function(bsp::kLcdSck, GPIO_FUNC_SPI);
        gpio_set_function(bsp::kLcdMosi, GPIO_FUNC_SPI);

        const uint32_t ctrlPins[] = {bsp::kLcdCs, bsp::kLcdDc, bsp::kLcdRst};
        for (uint32_t pin : ctrlPins)
        {
            gpio_init(pin);
            gpio_set_dir(pin, GPIO_OUT);
            gpio_put(pin, 1);
        }

        // Harden every LCD signal: max drive + fast slew so a neighbouring button's
        // switching edge can't couple enough to pull DC/CS/SCK across the panel's
        // logic threshold mid-transfer (which desyncs the pixel stream → the image
        // shifts). DC is GPIO25, right next to Start on GPIO26, so it is the most
        // exposed line.
        const uint32_t lcdPins[] = {bsp::kLcdSck, bsp::kLcdMosi,
                                    bsp::kLcdCs, bsp::kLcdDc, bsp::kLcdRst};
        for (uint32_t pin : lcdPins)
        {
            gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_12MA);
            gpio_set_slew_rate(pin, GPIO_SLEW_RATE_FAST);
        }

        gpio_set_function(bsp::kLcdBacklight, GPIO_FUNC_PWM);
        const uint slice = pwm_gpio_to_slice_num(bsp::kLcdBacklight);
        pwm_config cfg = pwm_get_default_config();
        pwm_config_set_wrap(&cfg, 255);
        pwm_init(slice, &cfg, true);
        setBacklight(0);
    }

    void SpiDisplayBus::reset()
    {
        gpio_put(bsp::kLcdRst, 1); sleep_ms(50);
        gpio_put(bsp::kLcdRst, 0); sleep_ms(50);
        gpio_put(bsp::kLcdRst, 1); sleep_ms(120);
    }

    void SpiDisplayBus::delayMs(uint32_t ms) { sleep_ms(ms); }

    void SpiDisplayBus::writeCommand(uint8_t cmd)
    {
        dcCmd(); csLow();
        spi_write_blocking(kSpi, &cmd, 1);
        csHigh();
    }

    void SpiDisplayBus::writeData(uint8_t byte)
    {
        dcData(); csLow();
        spi_write_blocking(kSpi, &byte, 1);
        csHigh();
    }

    void SpiDisplayBus::writeData(const uint8_t* buf, uint32_t n)
    {
        dcData(); csLow();
        spi_write_blocking(kSpi, buf, n);
        csHigh();
    }

    void SpiDisplayBus::beginPixels()
    {
        // Switch to 16-bit frames for the bulk pixel stream and hold CS down for
        // the whole RAM write (one transaction).
        spi_set_format(kSpi, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
        dcData(); csLow();
    }

    void SpiDisplayBus::writePixels(const uint16_t* px, uint32_t n)
    {
        spi_write16_blocking(kSpi, px, n);
    }

    void SpiDisplayBus::endPixels()
    {
        csHigh();
        spi_set_format(kSpi, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    }

    void SpiDisplayBus::setBacklight(uint8_t level)
    {
        pwm_set_gpio_level(bsp::kLcdBacklight,
                           static_cast<uint16_t>(level) * level / 255);
    }
}
