#pragma once

// ── GamePi13 on RP2350-PiZero — verified pin map ───────────────────────────
//
// RP2350 GPIO numbers (NOT the wiki's Raspberry-Pi BCM column). Sourced from the
// vendor demo Gamepi_RP2040_C. Pins that differ from the wiki BCM table are
// flagged; cross-check those against the schematic before trusting in anger.

#include <cstdint>

namespace bsp
{
    // ── Display (ST7789, 240x240, SPI1) ────────────────────────────────────
    constexpr uint32_t kLcdSck       = 10;   // SPI1 SCK   (wiki BCM=11)  [diff]
    constexpr uint32_t kLcdMosi      = 11;   // SPI1 TX    (wiki BCM=10)  [diff]
    constexpr uint32_t kLcdCs        = 8;    // chip select, active low
    constexpr uint32_t kLcdDc        = 25;   // data / command
    constexpr uint32_t kLcdRst       = 27;   // reset, active low
    constexpr uint32_t kLcdBacklight = 7;    // PWM brightness

    // ── Audio ──────────────────────────────────────────────────────────────
    constexpr uint32_t kAudio        = 18;   // PWM buzzer / speaker

    // ── Status LED (GamePi13 STA) ──────────────────────────────────────────
    constexpr uint32_t kLedStatus    = 28;

    // ── microSD card (SDIO) ────────────────────────────────────────────────
    // The board wires the microSD for 4-bit SDIO (driven by PIO, see sd/hw_config.c),
    // not SPI. The SDIO driver hardcodes CLK and derives DAT1..DAT3 = DAT0+1..3.
    constexpr uint32_t kSdClk        = 30;
    constexpr uint32_t kSdCmd        = 31;
    constexpr uint32_t kSdDat0       = 40;   // DAT1..DAT3 = GP41..GP43
    constexpr uint32_t kSdCardDetect = 22;   // card-detect (0 == present)

    // ── Buttons (INPUT + PULL_UP, pressed == 0). Order matches hal::Button. ─
    constexpr uint32_t kKeyUp        = 15;   // (wiki BCM=5)   [diff]
    constexpr uint32_t kKeyDown      = 6;
    constexpr uint32_t kKeyLeft      = 16;
    constexpr uint32_t kKeyRight     = 13;
    constexpr uint32_t kKeyA         = 21;
    constexpr uint32_t kKeyB         = 20;
    constexpr uint32_t kKeyX         = 5;    // (wiki BCM=15)  [diff]
    constexpr uint32_t kKeyY         = 9;    // (wiki BCM=12)  [diff]
    constexpr uint32_t kKeyL         = 23;   // left shoulder
    constexpr uint32_t kKeyR         = 4;    // right shoulder (wiki BCM=14) [diff]
    constexpr uint32_t kKeyStart     = 26;
    constexpr uint32_t kKeySelect    = 19;
}
