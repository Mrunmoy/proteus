// ── Proteus PIO-USB bring-up (Stage 1) ──────────────────────────────────────
// Proves the board's SECOND USB-C port (the "PIO-USB" connector J4, bit-banged
// over PIO on GP28=D+/GP29=D-) works as a USB device, running alongside the
// native USB. Native USB stays a stdio CDC (carries the picotool reset path —
// flash/debug, no stuck board); PIO-USB runs the Pico-PIO-USB device stack on
// core1. Stage 1 enumerates a HID device on J4 to validate pins + dual-stack;
// the aether RPC moves onto PIO-USB in Stage 2. Adapted from the Pico-PIO-USB
// usb_device example.

#include <stdio.h>
#include <string.h>

#include "hardware/clocks.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "pio_usb.h"

#include "class/hid/hid_device.h"
#include "device/usbd.h"

// PIO-USB D+ pin on the RP2350-PiZero (D- is D+ + 1 = GP29).
#define PROTEUS_PIO_USB_DP_PIN 28

static usb_device_t* usb_device = NULL;

static const tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0110,   // USB 1.1 (full speed) — PIO-USB
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = 64,
    .idVendor           = 0x2E8A,
    .idProduct          = 0x4002,   // Proteus PIO-USB (distinct from native 0x4001)
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

enum { ITF_NUM_MOUSE, ITF_NUM_TOTAL };
enum { EPNUM_MOUSE = 0x81 };

static const uint8_t desc_hid_mouse_report[] = {TUD_HID_REPORT_DESC_MOUSE()};
static const uint8_t* report_desc[] = {desc_hid_mouse_report};

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)
static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(ITF_NUM_MOUSE, 0, HID_ITF_PROTOCOL_MOUSE,
                       sizeof(desc_hid_mouse_report), EPNUM_MOUSE,
                       CFG_TUD_HID_EP_BUFSIZE, 10),
};

static const char* const string_base[] = {
    (const char[]){0x09, 0x04},
    "Proteus",
    "Proteus PIO-USB",
    "PIOUSB1",
};

static string_descriptor_t str_desc[4];

static void init_string_desc(void)
{
    for (int idx = 0; idx < 4; idx++)
    {
        uint16_t* wchar_str = (uint16_t*)&str_desc[idx];
        uint8_t   len       = 0;
        if (idx == 0)
        {
            wchar_str[1] = (uint8_t)string_base[0][0] | ((uint16_t)string_base[0][1] << 8);
            len          = 1;
        }
        else
        {
            len = (uint8_t)strnlen(string_base[idx], 31);
            for (int i = 0; i < len; i++) { wchar_str[i + 1] = string_base[idx][i]; }
        }
        wchar_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * len + 2));
    }
}

static const usb_descriptor_buffers_t desc = {
    .device     = (uint8_t*)&desc_device,
    .config     = desc_configuration,
    .hid_report = report_desc,
    .string     = str_desc,
};

static void core1_main(void)
{
    sleep_ms(10);

    static pio_usb_configuration_t config = PIO_USB_DEFAULT_CONFIG;
    config.pin_dp = PROTEUS_PIO_USB_DP_PIN;

    init_string_desc();
    usb_device = pio_usb_device_init(&config, &desc);

    while (true) { pio_usb_device_task(); }
}

int main(void)
{
    // PIO-USB timing needs a sysclock that is a multiple of 12 MHz.
    set_sys_clock_khz(120000, true);

    stdio_init_all();   // native USB CDC: debug + picotool reset path

    sleep_ms(10);
    multicore_reset_core1();
    multicore_launch_core1(core1_main);

    uint32_t tick = 0;
    while (true)
    {
        printf("proteus pio-usb: device=%s tick=%lu\r\n",
               usb_device ? "up" : "init", (unsigned long)tick++);
        if (usb_device != NULL)
        {
            // Nudge the mouse so the host sees live traffic on the PIO-USB port.
            hid_mouse_report_t report = {0};
            report.x          = 2;
            endpoint_t* ep    = pio_usb_get_endpoint(usb_device, 1);
            if (ep) { pio_usb_set_out_data(ep, (uint8_t*)&report, sizeof(report)); }
        }
        sleep_ms(500);
    }
}
