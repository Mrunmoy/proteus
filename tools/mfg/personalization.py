"""Per-board personalization block (recipient name).

The Game Boy gift firmware (src/app/gb) reads this block at boot and shows
"For <name>" on the splash. It lives in its own flash sector (0x3FD000), so it
survives app reflashes and the per-boot diagnostics capsule rewrites. Written
once per board — by the station's SHIP step or the standalone tools/personalize.py.

Layout must match src/port/rp2350/flash/Personalization.h:
    uint32_t magic;            # 'PRZN' little-endian
    char     recipient[32];    # NUL-terminated/padded
"""

import struct

MAGIC = 0x4E5A5250                       # 'PRZN' little-endian
NAME_FIELD = 32                          # kRecipientMax (31) + NUL
FLASH_OFFSET = 0x3FD000                  # kPersonalizationOffset
XIP_ADDR = 0x10000000 + FLASH_OFFSET     # picotool load -o address


def build(name):
    """Build the 36-byte personalization block for `name` (ASCII, <= 31 chars)."""
    raw = name.encode("ascii", "ignore")
    if len(raw) >= NAME_FIELD:
        raise ValueError(f"name too long (max {NAME_FIELD - 1} chars): {name!r}")
    return struct.pack("<I", MAGIC) + raw + b"\x00" * (NAME_FIELD - len(raw))
