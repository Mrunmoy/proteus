// ── USB descriptors — Proteus TestAgent CDC device ──────────────────────────
// One CDC-ACM interface for the aether RPC link. The serial-number string is
// the RP2350 unique board id (the manufacturing session key),
// so the host can identify and re-match the device across reboots.

#include "tusb.h"

#include "common/device_info.h"
#include "pico/unique_id.h"

#include <string.h>

// ── Device descriptor ───────────────────────────────────────────────────────
// VID/PID are the COMPILED-IN FALLBACK. The real identity is the device-info
// block in flash (pid.codes VID 0x1209): if a valid block is present, the device
// enumerates as whatever its identity block says — the block is the single source
// of truth. picotool writes the block in BOOTSEL; we only read it.
#define USB_VID_DEFAULT 0x1209u   // pid.codes (open-source shared VID)
#define USB_PID_DEFAULT 0x7201u   // Proteus RP2350-PiZero line

static tusb_desc_device_t desc_device =
{
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    // Use IAD for CDC so the composite enumerates cleanly across hosts.
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VID_DEFAULT,
    .idProduct          = USB_PID_DEFAULT,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

uint8_t const* tud_descriptor_device_cb(void)
{
    // Patch VID/PID from the identity block once, before the host reads it.
    static bool resolved = false;
    if (!resolved)
    {
        DeviceInfo di;
        if (device_info_read(&di) && di.vid != 0u && di.pid != 0u)
        {
            desc_device.idVendor  = di.vid;
            desc_device.idProduct = di.pid;
        }
        resolved = true;
    }
    return (uint8_t const*)&desc_device;
}

// ── Configuration descriptor — two CDCs (debug + RPC) ───────────────────────
enum { ITF_NUM_CDC_0 = 0, ITF_NUM_CDC_0_DATA, ITF_NUM_CDC_1, ITF_NUM_CDC_1_DATA, ITF_NUM_TOTAL };

#define EPNUM_CDC_0_NOTIF 0x81
#define EPNUM_CDC_0_OUT   0x02
#define EPNUM_CDC_0_IN    0x82
#define EPNUM_CDC_1_NOTIF 0x83
#define EPNUM_CDC_1_OUT   0x04
#define EPNUM_CDC_1_IN    0x84

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + 2 * TUD_CDC_DESC_LEN)

static uint8_t const desc_configuration[] =
{
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    // CDC0 — debug log + reset (string 4)
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_0, 4, EPNUM_CDC_0_NOTIF, 8, EPNUM_CDC_0_OUT, EPNUM_CDC_0_IN, 64),
    // CDC1 — binary aether RPC (string 5)
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_1, 5, EPNUM_CDC_1_NOTIF, 8, EPNUM_CDC_1_OUT, EPNUM_CDC_1_IN, 64),
};

uint8_t const* tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return desc_configuration;
}

// ── String descriptors ──────────────────────────────────────────────────────
static char const* const string_desc_arr[] =
{
    (const char[]){0x09, 0x04},  // 0: language = English (0x0409)
    "Proteus",                   // 1: manufacturer
    "Proteus TestAgent",         // 2: product
    NULL,                        // 3: serial — filled from the board unique id
    "Proteus Debug",             // 4: CDC0 — debug log + reset
    "Proteus RPC",               // 5: CDC1 — binary aether RPC
};

static uint16_t desc_str[32];

uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    uint8_t chr_count;

    if (index == 0)
    {
        desc_str[1] = 0x0409;
        chr_count = 1;
    }
    else if (index == 3)
    {
        // Serial = RP2350 unique board id as a hex string (matches the USB
        // iSerial seen by picotool / lsusb — the session key).
        pico_unique_board_id_t uid;
        pico_get_unique_board_id(&uid);
        static const char hex[] = "0123456789ABCDEF";
        chr_count = 0;
        for (uint8_t i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; ++i)
        {
            desc_str[1 + chr_count++] = hex[(uid.id[i] >> 4) & 0xF];
            desc_str[1 + chr_count++] = hex[uid.id[i] & 0xF];
        }
    }
    else
    {
        if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) { return NULL; }
        const char* str = string_desc_arr[index];
        chr_count = (uint8_t)strlen(str);
        if (chr_count > 31) { chr_count = 31; }
        for (uint8_t i = 0; i < chr_count; ++i) { desc_str[1 + i] = str[i]; }
    }

    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return desc_str;
}
