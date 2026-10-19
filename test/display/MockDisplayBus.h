#pragma once

#include <cstdint>
#include <vector>

// Recording display bus for host tests: satisfies the hal/DisplayBus contract and
// logs every command/data/pixel the driver emits, so a test can assert on the
// exact wire sequence without any hardware. Test-only, so STL is fine here.
struct MockDisplayBus
{
    std::vector<uint8_t>  commands;   // every writeCommand() byte, in order
    std::vector<uint8_t>  data;       // every writeData() byte, in order
    std::vector<uint16_t> pixels;     // every writePixels() pixel, in order
    int      resets       = 0;
    uint32_t delayedMs    = 0;
    int      backlight    = -1;
    int      pixelStreams = 0;     // begin/end bracket count
    bool     streaming    = false;

    void reset() { ++resets; }
    void delayMs(uint32_t ms) { delayedMs += ms; }
    void writeCommand(uint8_t cmd) { commands.push_back(cmd); }
    void writeData(uint8_t byte) { data.push_back(byte); }
    void writeData(const uint8_t* buf, uint32_t n) { data.insert(data.end(), buf, buf + n); }
    void beginPixels() { streaming = true; ++pixelStreams; }
    void writePixels(const uint16_t* px, uint32_t n) { pixels.insert(pixels.end(), px, px + n); }
    void endPixels() { streaming = false; }
    void setBacklight(uint8_t level) { backlight = level; }
};
