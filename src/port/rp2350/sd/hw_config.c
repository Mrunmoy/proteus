/* hw_config.c — Proteus / RP2350-PiZero microSD hardware configuration
 *
 * Board-specific glue for the vendored carlk3 no-OS-FatFS-SD library
 * (external/no-OS-FatFS-SD, Apache-2.0). One microSD socket on **SDIO** (PIO).
 *
 * WHY SDIO, NOT SPI: this board wires the microSD for SDIO. The card clock sits
 * on GPIO30 while CMD/DATA are on GPIO31 and GPIO40-43. On the RP2350 those pins
 * span BOTH hardware-SPI blocks' pin groups (e.g. GPIO40 is SPI1-only, the clock
 * group differs), so no single hardware-SPI peripheral can drive the socket.
 * The vendor RP2350-PiZero C/03-MicroSD demo therefore uses the PIO-based SDIO
 * driver — and so do we. (Confirmed on metal: an SPI-mode config mount()-fails.)
 *
 * SDIO pins (RP2350B GPIO numbers): CMD = GP31, D0 = GP40. The SDIO driver
 * (sd_driver/SDIO/sd_card_sdio.c) HARDCODES CLK = GP30 and derives
 * D1..D3 = D0+1..3 (= GP41..GP43), and asserts that CLK_gpio/D1/D2/D3 are left 0
 * here — so we set ONLY CMD_gpio and D0_gpio. The driver also calls
 * pio_set_gpio_base(pio1, 16) so PIO1 can reach GP31..GP43 on the RP2350B.
 *   card-detect = GP22 (0 == card present).
 *
 * The library's diskio glue (src/glue.c) walks sd_get_num()/sd_get_by_num(),
 * so this file is the single source of truth for the SD wiring.
 */

#include "hw_config.h"

#include <assert.h>

// ── SDIO interface (PIO1 + DMA drive the single card) ───────────────────────
// Set ONLY CMD_gpio and D0_gpio; the driver fills CLK (=GP30) and D1..D3.
static sd_sdio_if_t sdio_ifs[] = {
    {
        .CMD_gpio = 31,   // card CMD  (kSdCmd)
        .D0_gpio  = 40,   // card DAT0 (kSdDat0); D1..D3 = 41..43
        .CLK_gpio_drive_strength = GPIO_DRIVE_STRENGTH_12MA,
        .CMD_gpio_drive_strength = GPIO_DRIVE_STRENGTH_4MA,
        .D0_gpio_drive_strength  = GPIO_DRIVE_STRENGTH_4MA,
        .D1_gpio_drive_strength  = GPIO_DRIVE_STRENGTH_4MA,
        .D2_gpio_drive_strength  = GPIO_DRIVE_STRENGTH_4MA,
        .D3_gpio_drive_strength  = GPIO_DRIVE_STRENGTH_4MA,
        .SDIO_PIO    = pio1,
        // MUST be DMA_IRQ_0: libdvi (HDMI output) claims DMA_IRQ_1 EXCLUSIVELY on
        // core1, and this driver does not release its DMA IRQ handler on unmount.
        // If both land on IRQ_1, DVI bring-up hits irq_set_exclusive_handler ->
        // hard_assert on core1 (encoder dies, HDMI present() hangs forever).
        .DMA_IRQ_num = DMA_IRQ_0,
        .baud_rate   = 125 * 1000 * 1000 / 7  // ~17.8 MHz (vendor-proven value)
    }
};

// ── SD card socket ──────────────────────────────────────────────────────────
static sd_card_t sd_cards[] = {
    {
        .type      = SD_IF_SDIO,
        .sdio_if_p = &sdio_ifs[0],
        // Card-detect DISABLED — matches the vendor RP2350-PiZero SDIO config.
        // Our GP22 card-detect proved unreliable (read present in cardPresent()
        // but absent inside mount() after sd_init_driver() reconfigured pins),
        // and the vendor disables it too (their CD pin is 24, also unused). With
        // it off, sd_card_detect() always reports present and mount() proceeds
        // to f_mount, which talks to the card and reports the real result.
        .use_card_detect      = false,
        .card_detect_gpio     = 22,
        .card_detected_true   = 0,
        .card_detect_use_pull = true,
        .card_detect_pull_hi  = true
    }
};

size_t sd_get_num(void)
{
    return count_of(sd_cards);
}

sd_card_t *sd_get_by_num(size_t num)
{
    assert(num < sd_get_num());
    if (num < sd_get_num())
    {
        return &sd_cards[num];
    }
    return NULL;
}

/* [] END OF FILE */
