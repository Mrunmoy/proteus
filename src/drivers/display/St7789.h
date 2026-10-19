#pragma once

#include <cstdint>
#include <initializer_list>

// ── ST7789 240x240 display controller driver ────────────────────────────────
// Generic over a display bus (see hal/DisplayBus.h): the driver owns the panel
// init sequence, the address window, and the scale/centre present; the bus owns
// how bytes reach the panel. The same driver runs on the RP2350 (SPI bus) and
// under host tests (a recording mock bus), which is what makes it verifiable off
// hardware.
//
// Header-only because it is a template: the definitions must be visible where it
// is instantiated, and keeping them here is what lets `drivers/` stay free of any
// platform header (a split .cpp would have to name each concrete bus and pull the
// platform in, breaking that separation).

namespace drivers
{
    template <class Bus>
    class St7789
    {
    public:
        static constexpr uint16_t kWidth  = 240;
        static constexpr uint16_t kHeight = 240;

        explicit St7789(Bus& bus) : m_bus(bus) {}

        // Reset + register init. Sequence and timings match the panel on the
        // GamePi13 (portrait, RGB565, IPS inversion on).
        void init()
        {
            m_bus.reset();

            m_bus.writeCommand(0x11);                 // SLPOUT
            m_bus.delayMs(120);
            writeReg(0x36, {0x00});                   // MADCTL: portrait, RGB
            writeReg(0x3A, {0x05});                   // COLMOD: 16bpp RGB565

            writeReg(0xB2, {0x1F, 0x1F, 0x00, 0x33, 0x33});
            writeReg(0xB7, {0x00});
            writeReg(0xBB, {0x3F});
            writeReg(0xC0, {0x2C});
            writeReg(0xC2, {0x01});
            writeReg(0xC3, {0x0F});
            writeReg(0xC4, {0x20});
            writeReg(0xC6, {0x13});
            writeReg(0xD0, {0xA4, 0xA1});
            writeReg(0xE0, {0xF0, 0x06, 0x0D, 0x0B, 0x0A, 0x07, 0x2E, 0x43,
                            0x45, 0x38, 0x14, 0x13, 0x25, 0x29});
            writeReg(0xE1, {0xF0, 0x07, 0x0A, 0x08, 0x07, 0x23, 0x2E, 0x33,
                            0x44, 0x3A, 0x16, 0x17, 0x26, 0x2C});
            writeReg(0xE4, {0x1D, 0x00, 0x00});

            m_bus.writeCommand(0x21);                 // INVON (IPS panel)
            m_bus.writeCommand(0x29);                 // DISPON
            m_bus.writeCommand(0x2C);                 // RAMWR
        }

        void setBacklight(uint8_t level) { m_bus.setBacklight(level); }

        // Scale a source RGB565 frame to the panel width and centre it vertically
        // (letter-boxed), streaming one row at a time. No full-panel framebuffer:
        // a single converted row lives on the driver. Source is already the
        // panel's pixel format, so this is a pure scale + copy.
        void present(const uint16_t* frame, uint16_t srcW, uint16_t srcH)
        {
            if (frame == nullptr || srcW == 0 || srcH == 0) { return; }

            const uint32_t scaledH = (static_cast<uint32_t>(srcH) * kWidth) / srcW;
            const uint32_t drawH   = scaledH < kHeight ? scaledH : kHeight;
            const uint32_t yOff    = (kHeight - drawH) / 2;
            const uint32_t stepX   = (static_cast<uint32_t>(srcW) << 16) / kWidth;
            const uint32_t stepY   = (static_cast<uint32_t>(srcH) << 16) /
                                     (scaledH ? scaledH : 1);

            setWindow(0, 0, kWidth - 1, kHeight - 1);

            m_bus.beginPixels();
            for (uint32_t dy = 0; dy < kHeight; ++dy)
            {
                if (dy < yOff || dy >= yOff + drawH)
                {
                    for (uint16_t& px : m_line) { px = 0; }   // letter-box bar
                }
                else
                {
                    uint32_t sy = ((dy - yOff) * stepY) >> 16;
                    if (sy >= srcH) { sy = srcH - 1; }
                    const uint16_t* srcRow = frame + sy * srcW;

                    uint32_t srcXFp = 0;
                    for (uint32_t dx = 0; dx < kWidth; ++dx)
                    {
                        uint32_t sx = srcXFp >> 16;
                        if (sx >= srcW) { sx = srcW - 1; }
                        m_line[dx] = srcRow[sx];
                        srcXFp += stepX;
                    }
                }
                m_bus.writePixels(m_line, kWidth);
            }
            m_bus.endPixels();
        }

    private:
        void writeReg(uint8_t cmd, std::initializer_list<uint8_t> data)
        {
            m_bus.writeCommand(cmd);
            m_bus.writeData(data.begin(), static_cast<uint32_t>(data.size()));
        }

        void setWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
        {
            m_bus.writeCommand(0x2A);                 // CASET
            const uint8_t cols[] = {uint8_t(x0 >> 8), uint8_t(x0),
                                    uint8_t(x1 >> 8), uint8_t(x1)};
            m_bus.writeData(cols, 4);
            m_bus.writeCommand(0x2B);                 // RASET
            const uint8_t rows[] = {uint8_t(y0 >> 8), uint8_t(y0),
                                    uint8_t(y1 >> 8), uint8_t(y1)};
            m_bus.writeData(rows, 4);
            m_bus.writeCommand(0x2C);                 // RAMWR
        }

        Bus&     m_bus;
        uint16_t m_line[kWidth] = {};
    };
}
