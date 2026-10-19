#pragma once

#include <cstdint>

// ── Display bus (the PHY contract for command/data display controllers) ─────
// A display controller like the ST7789 speaks a simple command/data protocol:
// a command byte (DC line low) optionally followed by data bytes (DC high), and
// bulk RGB565 pixel writes for the framebuffer. How those bytes reach the panel
// (SPI peripheral, PIO, bit-bang) and which GPIOs carry DC/CS/RST/backlight is
// platform business — it lives behind this contract, in a port's bus type.
//
// This is a *documented* contract, not a base class: drivers are templated on
// the bus type and call these methods directly, so the calls inline with no
// vtable. A platform supplies a concrete bus (port/rp2350/phy/SpiDisplayBus);
// host tests supply a recording mock (test/MockDisplayBus). Both satisfy:
//
//   void     reset();                                  // hardware reset pulse
//   void     delayMs(uint32_t ms);                     // blocking delay
//   void     writeCommand(uint8_t cmd);                // DC=command, 1 byte
//   void     writeData(uint8_t byte);                  // DC=data, 1 byte
//   void     writeData(const uint8_t* buf, uint32_t n);// DC=data, n bytes
//   void     beginPixels();                            // open the RGB565 stream
//   void     writePixels(const uint16_t* px, uint32_t n); // a chunk (e.g. a row)
//   void     endPixels();                              // close the stream
//   void     setBacklight(uint8_t level);              // 0..255
//
// Pixels are bracketed because a controller's RAM write is one transaction: the
// bus holds CS for the whole stream and may run wider transfer words inside it
// (the ST7789 sends 16-bit pixels), then restores command framing on endPixels().
// The granularity is deliberate — a chunk (row) per writePixels, no per-pixel
// entry point — so the per-frame indirection stays a handful of bulk calls.
