#pragma once

#include <cstdint>

// ── SPI display bus (RP2350 PHY for the GamePi13 ST7789) ────────────────────
// Concrete display bus (see hal/DisplayBus.h) carrying the panel's command/data
// protocol over SPI1, with DC/CS/RST and the backlight PWM. The St7789 driver is
// templated on this type and drives the panel through this bus.

namespace rp2350
{
    class SpiDisplayBus
    {
    public:
        // Bring up SPI1 + the control GPIOs + the backlight PWM. Call once before
        // handing the bus to the St7789 driver's init().
        void init();

        // ── hal/DisplayBus contract ─────────────────────────────────────────
        void reset();
        void delayMs(uint32_t ms);
        void writeCommand(uint8_t cmd);
        void writeData(uint8_t byte);
        void writeData(const uint8_t* buf, uint32_t n);
        void beginPixels();
        void writePixels(const uint16_t* px, uint32_t n);
        void endPixels();
        void setBacklight(uint8_t level);

        [[nodiscard]] uint32_t spiHz() const { return m_spiHz; }

    private:
        uint32_t m_spiHz = 0;   // actual rate spi_init() resolved to
    };
}
