#!/usr/bin/env python3
"""Device-info block — host side (must match src/common/device_info.h byte-for-byte).

Builds/parses the identity block and writes/reads it via picotool in BOOTSEL.
Writer is the PC: static identity at programming, serial at serialize. The device
firmware only reads the block.
"""
import struct
import subprocess
import zlib

MAGIC = 0x544F5250          # "PROT" little-endian
STRUCT_VERSION = 1
FLASH_OFFSET = 0x00FFC000   # within the flash chip
XIP_ADDR = 0x10FFC000       # CPU read address
SECTOR_SIZE = 0x1000

MODEL_LEN, VARIANT_LEN, SERIAL_LEN = 16, 16, 24   # NUL-terminated: usable = LEN-1
# magic, ver, hw_rev, vid, pid, model, variant, serial, serial_utc  (the CRC'd prefix)
_PREFIX_FMT = f"<IHHHH{MODEL_LEN}s{VARIANT_LEN}s{SERIAL_LEN}sQ"
PREFIX_LEN = struct.calcsize(_PREFIX_FMT)   # 76 (CRC'd prefix)
TOTAL_LEN = PREFIX_LEN + 4                   # 80 (+ crc32)


def build(model, vid, pid, hw_rev=1, variant="", serial="", serial_utc=0):
    # Truncate to LEN-1 so the fixed field is always NUL-terminated.
    prefix = struct.pack(
        _PREFIX_FMT, MAGIC, STRUCT_VERSION, hw_rev, vid, pid,
        model.encode()[:MODEL_LEN - 1], variant.encode()[:VARIANT_LEN - 1],
        serial.encode()[:SERIAL_LEN - 1], serial_utc,
    )
    crc = zlib.crc32(prefix) & 0xFFFFFFFF
    return prefix + struct.pack("<I", crc)


def parse(data):
    if len(data) < TOTAL_LEN:
        return None
    blk = data[:TOTAL_LEN]
    magic, ver, hw_rev, vid, pid, model, variant, serial, serial_utc = \
        struct.unpack(_PREFIX_FMT, blk[:PREFIX_LEN])
    crc = struct.unpack("<I", blk[PREFIX_LEN:TOTAL_LEN])[0]
    if magic != MAGIC or ver != STRUCT_VERSION:
        return None
    if (zlib.crc32(blk[:PREFIX_LEN]) & 0xFFFFFFFF) != crc:
        return None

    def cstr(b):
        return b.split(b"\x00", 1)[0].decode("ascii", "replace")

    return {
        "hw_rev": hw_rev, "vid": vid, "pid": pid,
        "model": cstr(model), "variant": cstr(variant),
        "serial": cstr(serial), "serial_utc": serial_utc,
        "serialized": serial[0:1] != b"\x00",
    }


# ── picotool I/O (BOOTSEL) ──────────────────────────────────────────────────
# `picotool_cmd` is the argv prefix that runs picotool against the target board,
# e.g. ["docker","run","--rm","--privileged","-v","/dev/bus/usb:/dev/bus/usb",
#       "-v",f"{cwd}:/work","proteus-picotool","picotool"]  (+ "--ser <serial>"
# to pick one of several boards). It must operate on a board in BOOTSEL mode.

def write(picotool_cmd, info_bytes, copy="A", workdir="/tmp", ser=None):
    """Write the 80-byte block to the device-info region (copy A and/or B).

    `ser` (chip unique id) appends `--ser <s>` so the right board is targeted
    when several are in BOOTSEL.
    """
    import os
    sel = ["--ser", ser] if ser else []
    offsets = {"A": 0, "B": SECTOR_SIZE}
    for which in (["A", "B"] if copy == "AB" else [copy]):
        path = os.path.join(workdir, f"devinfo_{which}.bin")
        with open(path, "wb") as f:
            f.write(info_bytes)
        addr = XIP_ADDR + offsets[which]
        subprocess.run(picotool_cmd + ["load", path, "-o", hex(addr)] + sel, check=True)


def read(picotool_cmd, copy="A", workdir="/tmp", ser=None):
    """Read + parse the device-info block from copy A (fallback B)."""
    import os
    sel = ["--ser", ser] if ser else []
    offsets = {"A": 0, "B": SECTOR_SIZE}
    for which in ([copy] if copy != "AB" else ["A", "B"]):
        addr = XIP_ADDR + offsets[which]
        path = os.path.join(workdir, f"devinfo_read_{which}.bin")
        subprocess.run(picotool_cmd + ["save", "-r", hex(addr), hex(addr + TOTAL_LEN), path] + sel,
                       check=True)
        with open(path, "rb") as f:
            di = parse(f.read())
        if di:
            return di
    return None


if __name__ == "__main__":
    # Self-test: build -> parse round-trip (no hardware).
    b = build("RP2350-PiZero", vid=0x1209, pid=0x7201, hw_rev=1, variant="GamePi13",
              serial="PRZ-A-2624-00042-7", serial_utc=1_750_000_000)
    assert len(b) == TOTAL_LEN, len(b)
    di = parse(b)
    assert di and di["model"] == "RP2350-PiZero" and di["serial"] == "PRZ-A-2624-00042-7"
    assert di["vid"] == 0x1209 and di["pid"] == 0x7201
    assert parse(b[:-1] + b"\x00") is None  # corrupt CRC -> rejected
    print("device_info.py self-test OK:", di)
