#!/usr/bin/env python3
"""Device-type registry — the single source of truth for product identity.

Both the factory programmer (writes the device-info block) AND the test-station
profile selector read from here, so a model/variant string or VID/PID can never
drift between "what we program" and "what the line expects". Adding a new product
variant is one entry in REGISTRY — the framework is untouched.

Also owns the SERIAL-NUMBER scheme (PRZ-<plant>-<YYWW>-<seq>-<check>): the serial
encodes where/when a unit was MADE, never its destination (that's a DB attribute).
"""
import time

# One shared open-source vendor id (pid.codes). NOT 0x2E8A (Raspberry Pi's), and
# not a private USB-IF VID (~$6k). Each PRODUCT LINE gets its own PID under it;
# VARIANTS of the same board share the PID and differ by the `variant` string.
VENDOR_ID = 0x1209


class DeviceType:
    """One manufactured product line + the variant + how to identify/ship it."""

    def __init__(self, model, variant, pid, hw_rev, product_fw,
                 fw_version="0.1.0"):
        self.model = model                      # the device TYPE the line builds
        self.variant = variant                  # HAT/config (a SKU could suffix this)
        self.vid = VENDOR_ID
        self.pid = pid                           # per product line
        self.hw_rev = hw_rev
        self.product_fw = product_fw             # the image shipped after serialize
        self.fw_version = fw_version             # product firmware version recorded per unit

    def matches(self, info: dict) -> bool:
        """Cross-check a device's CLAIMED identity vs this type."""
        return (info.get("model") == self.model
                and info.get("vid") == self.vid
                and info.get("pid") == self.pid)


# ── The registry. Add a line = add a product. ───────────────────────────────
REGISTRY = [
    DeviceType(
        model="RP2350-PiZero",
        variant="GamePi13",
        pid=0x7201,                              # "Proteus" RP2350-PiZero line
        hw_rev=1,
        product_fw="build/proteus_gb.uf2",       # the Game Boy gift app (shipping image)
        fw_version="1.0",                         # shipped firmware version (matches bcdDevice)
    ),
    # Future: DeviceType("RP2350-PiZero", "bare", 0x7201, 1, ...),
    #         DeviceType("RP2350-Foo",     "...",  0x7202, 1, ...),
]


def by_model(model: str):
    """The device type the LINE is configured to build (model is the key)."""
    return next((d for d in REGISTRY if d.model == model), None)


# ── Serial-number scheme: PRZ-<plant>-<YYWW>-<seq>-<check> ───────────────────
SERIAL_PREFIX = "PRZ"           # product family (Proteus RP2350-Zero)
_CHECK_ALPHABET = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"   # base-36 check char


def _check_char(body: str) -> str:
    """Mod-36 check character over the alphanumerics (catches scan/typo errors)."""
    total = sum(_CHECK_ALPHABET.index(c) for c in body if c in _CHECK_ALPHABET)
    return _CHECK_ALPHABET[total % 36]


def make_serial(plant: str, seq: int, when_utc: float = None) -> str:
    """Build a serial. plant=line code (A,B…); seq=monotonic per plant; date from now."""
    if when_utc is None:
        when_utc = time.time()
    tm = time.gmtime(when_utc)
    yy = tm.tm_year % 100
    week = int(time.strftime("%V", tm))         # ISO week 01-53
    body = f"{SERIAL_PREFIX}{plant}{yy:02d}{week:02d}{seq:05d}"
    return f"{SERIAL_PREFIX}-{plant}-{yy:02d}{week:02d}-{seq:05d}-{_check_char(body)}"


def serial_is_valid(serial: str) -> bool:
    """Verify the check character of a serial (used on read-back-verify)."""
    parts = serial.split("-")          # PRZ-A-2624-00042-7  -> 5 segments
    if len(parts) != 5 or parts[0] != SERIAL_PREFIX:
        return False
    _, plant, date, seq, check = parts
    body = f"{SERIAL_PREFIX}{plant}{date}{seq}"
    return _check_char(body) == check


if __name__ == "__main__":
    s = make_serial("A", 42, when_utc=1_750_000_000)
    print("sample serial:", s)
    assert s.startswith("PRZ-A-") and s.count("-") == 4, s
    assert serial_is_valid(s), s
    # A typo in the body must fail the check character.
    bad = s[:-1] + ("0" if s[-1] != "0" else "1")
    assert not serial_is_valid(bad), bad
    # Deterministic for a fixed time + seq.
    assert make_serial("A", 42, when_utc=1_750_000_000) == s
    print("device_types.py self-test OK")
