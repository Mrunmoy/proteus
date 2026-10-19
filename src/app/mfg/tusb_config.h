#ifndef PROTEUS_TUSB_CONFIG_H
#define PROTEUS_TUSB_CONFIG_H

// ── TinyUSB device config — Proteus TestAgent (tester firmware) ─────────────
// A single CDC interface carrying the binary aether RPC link. The product /
// emulator firmware uses pico_stdio_usb for debug logs; this tester firmware
// owns TinyUSB directly so CDC is a clean binary transport.

#define CFG_TUSB_RHPORT0_MODE  (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
#define CFG_TUD_ENABLED        1

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif
#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN     __attribute__((aligned(4)))
#endif

#define CFG_TUD_CDC            2   // CDC0 = debug + reset, CDC1 = binary RPC
#define CFG_TUD_MSC            0
#define CFG_TUD_HID            0
#define CFG_TUD_MIDI           0
#define CFG_TUD_VENDOR         0

// Generous FIFOs — RPC frames are tens of bytes but bursts may queue.
#define CFG_TUD_CDC_RX_BUFSIZE 512
#define CFG_TUD_CDC_TX_BUFSIZE 512
#define CFG_TUD_CDC_EP_BUFSIZE 64

#endif /* PROTEUS_TUSB_CONFIG_H */
