#include "port/rp2350/DviDisplay.h"

#include "common/FrameScaler.h"
#include "hal/Pixel.h"

#include "dvi.h"
#include "dvi_serialiser.h"

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/vreg.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include <cstdint>
#include <cstdio>

namespace
{
    // ── Board pin configuration (Waveshare RP2350-PiZero) ───────────────────
    // EXACTLY the vendor demo's pico_sock_cfg (RP2350-PiZero/{C,Arduino}/01-DVI,
    // common_dvi_pin_configs.h) — verified on this board's HDMI connector:
    //   TMDS lane 0 (blue/sync) = GPIO 36/37   ← was wrongly set to 32 (lanes
    //   TMDS lane 1 (green)     = GPIO 34/35      reversed) → monitor never synced
    //   TMDS lane 2 (red)       = GPIO 32/33      → black screen.
    //   pixel clock             = GPIO 38/39
    //   invert_diffpairs        = false
    // GPIOs >= 32 need the PIO GPIO window shifted to 16..47 (RP2350B has 48).
    constexpr uint kGpioBase = 16;

    const dvi_serialiser_cfg kSerialCfg = {
        .pio              = pio0,
        .sm_tmds          = { 0, 1, 2 },
        .pins_tmds        = { 36, 34, 32 },
        .pins_clk         = 38,
        .invert_diffpairs = false,
        .prog_offs        = 0,
    };

    // 1.2 V core for the DVI bit clock (the demo runs 1.1–1.2 V here).
    constexpr vreg_voltage kVregVoltage = VREG_VOLTAGE_1_20;

    // Single DVI instance at file scope so the bare core-1 entry point (which
    // takes no argument) can reach it. Only one DVI output exists on this board.
    dvi_inst s_dvi;

    void core1Main()
    {
        // Use DMA_IRQ_1 (not _0): the SDIO card driver installs an EXCLUSIVE handler
        // on DMA_IRQ_0 at boot and does not release it on unmount, so libdvi claiming
        // DMA_IRQ_0 hard-faults when an SD-loaded ROM precedes DVI bring-up (the
        // emulator-over-HDMI path). DMA_IRQ_1 is free. Harmless for the SD-free
        // standalone DVI test (proteus_dvi).
        dvi_register_irqs_this_core(&s_dvi, DMA_IRQ_1);
        dvi_start(&s_dvi);
        // Framebuffer worker: scans the persistent back/front framebuffer
        // CONTINUOUSLY at the DVI frame rate, independent of how fast core 0
        // produces frames. A sub-realtime emulator therefore never starves the
        // encoder (which previously collapsed the TMDS output to a solid colour).
        // init() primed q_colour_valid with the first framebuffer pointer.
        dvi_framebuf_main_16bpp(&s_dvi);  // never returns
    }
}

namespace rp2350
{
    int DviDisplay::init()
    {
        // Core voltage + system clock must match the DVI bit clock before libdvi
        // claims the PIO/PWM/DMA resources.
        vreg_set_voltage(kVregVoltage);
        sleep_ms(10);
        set_sys_clock_khz(dvi_timing_640x480p_60hz.bit_clk_khz, true);

        // RP2350B: shift the PIO GPIO window up so it can reach GPIO 32..47.
        pio_set_gpio_base(kSerialCfg.pio, kGpioBase);

        s_dvi.timing  = &dvi_timing_640x480p_60hz;
        s_dvi.ser_cfg = kSerialCfg;
        dvi_init(&s_dvi, next_striped_spin_lock_num(), next_striped_spin_lock_num());

        // Prime the framebuffer worker with the first buffer; core 0 draws into the
        // OTHER buffer and hands it over via present().
        const uint16_t* first = m_fb[0];
        queue_add_blocking_u32(&s_dvi.q_colour_valid, &first);
        m_back = 1;

        multicore_launch_core1(core1Main);
        return 0;
    }

    hal::Surface DviDisplay::backBuffer()
    {
        hal::Surface s;
        s.pixels = m_fb[m_back];
        s.width  = kWidth;
        s.height = kHeight;
        s.stride = kWidth;
        return s;
    }

    void DviDisplay::present()
    {
        // Hand the just-drawn buffer to the core-1 framebuffer worker, then block
        // until it releases the previously displayed buffer — that freed buffer
        // becomes our next draw target. Guarantees core 0 never draws into the
        // buffer currently being scanned (no tearing), and self-paces to ≤ the DVI
        // frame rate (the emulator is slower anyway).
        const uint16_t* shown = m_fb[m_back];
        queue_add_blocking_u32(&s_dvi.q_colour_valid, &shown);

        const uint16_t* freed = nullptr;
        queue_remove_blocking_u32(&s_dvi.q_colour_free, &freed);
        m_back = (freed == m_fb[0]) ? 0u : 1u;
        m_started = true;
    }

    void DviDisplay::presentFrame(const uint16_t* frame, uint32_t srcW, uint32_t srcH,
                                  uint16_t border)
    {
        common::blitScaledCentered(frame, srcW, srcH, m_fb[m_back], kWidth, kHeight, kWidth, border);
        present();
    }

    void DviDisplay::presentFrameDma(const uint16_t* frame, uint32_t srcW, uint32_t srcH)
    {
        // Falls back to the scaled CPU blit if the frame doesn't fit 1:1.
        if (srcW > kWidth || srcH > kHeight)
        {
            presentFrame(frame, srcW, srcH, 0);
            return;
        }

        static int s_chan = -1;
        if (s_chan < 0) { s_chan = dma_claim_unused_channel(true); }

        uint16_t*      dst  = m_fb[m_back];
        const uint32_t xoff = (kWidth  - srcW) / 2u;
        const uint32_t yoff = (kHeight - srcH) / 2u;

        // One DMA per source row (srcW 16-bit pixels): the CPU only programs the
        // transfers; the DMA engine moves the bytes. Borders are never written, so
        // they stay black (the buffers start zeroed). The DVI scan-out that
        // follows is itself DMA-driven in libdvi.
        for (uint32_t y = 0; y < srcH; ++y)
        {
            dma_channel_config c = dma_channel_get_default_config(s_chan);
            channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
            channel_config_set_read_increment(&c, true);
            channel_config_set_write_increment(&c, true);
            dma_channel_configure(s_chan, &c,
                                  dst + (yoff + y) * kWidth + xoff,  // write (strided)
                                  frame + static_cast<size_t>(y) * srcW,  // read (contiguous)
                                  srcW, true);
            dma_channel_wait_for_finish_blocking(s_chan);
        }
        present();
    }

    void DviDisplay::drawTestPattern()
    {
        // SMPTE-ish vertical colour bars across the top 2/3, gradient below.
        static const uint16_t kBars[8] = {
            hal::rgb565(255, 255, 255),  // white
            hal::rgb565(255, 255, 0),    // yellow
            hal::rgb565(0, 255, 255),    // cyan
            hal::rgb565(0, 255, 0),      // green
            hal::rgb565(255, 0, 255),    // magenta
            hal::rgb565(255, 0, 0),      // red
            hal::rgb565(0, 0, 255),      // blue
            hal::rgb565(0, 0, 0),        // black
        };

        uint16_t* fb         = m_fb[m_back];
        const uint16_t barsH = (kHeight * 2) / 3;
        for (uint16_t y = 0; y < kHeight; ++y)
        {
            uint16_t* row = fb + static_cast<size_t>(y) * kWidth;
            if (y < barsH)
            {
                for (uint16_t x = 0; x < kWidth; ++x)
                {
                    row[x] = kBars[(x * 8) / kWidth];
                }
            }
            else
            {
                // Greyscale ramp left→right.
                for (uint16_t x = 0; x < kWidth; ++x)
                {
                    const uint8_t v = static_cast<uint8_t>((x * 255) / (kWidth - 1));
                    row[x]          = hal::rgb565(v, v, v);
                }
            }
        }
    }
}
