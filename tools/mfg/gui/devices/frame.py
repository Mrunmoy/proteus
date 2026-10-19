"""aether-lite wire protocol — PURE encode/decode, no Qt, no serial.

Mirrors the firmware framing (src/app/mfg over aether-lite):
    [AA 55][len u32][header 24B LE][payload][crc32 LE]
  len   = bytes that follow the len field (= header + payload + crc)
  crc32 = standard IEEE CRC-32 over (len field bytes + header + payload)
  header = <version u16, flags u16, service u32, method u32, seq u32,
            payload_len u32, aux u32>   (aux carries the error code on responses)

This is the single source the GUI's RpcLink builds on. The FrameParser is INCREMENTAL:
feed it whatever bytes QSerialPort hands over (partial frames, multiple frames, junk)
and it yields only the complete, CRC-valid frames — no blocking, no fixed reads.
"""
from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass
from typing import List

SYNC = b"\xAA\x55"
PROTO_VER = 1

FRAME_REQUEST = 0x0001
FRAME_RESPONSE = 0x0002
FRAME_NOTIFY = 0x0004

SERVICE = 0xE131D8F3   # FNV-1a of "TestStation"

# Method ids — must match proto/TestStation.idl.
M_GET_IDENTITY = 1
M_GET_DEVICE_INFO = 2
M_SET_LED = 3
M_LCD_SHOW_PATTERN = 4
M_SCAN_BUTTONS = 5
M_PLAY_TONE = 6
M_SD_PROBE = 8
M_HDMI_SHOW_PATTERN = 9

# Device→PC notification message ids (al_send_notify), separate from method ids.
BUTTON_EVENT_MSG = 0x10   # payload: [button u8][type u8]  (type: 0=Down/press, 1=Up/release)

_HEADER_FMT = "<HHIIIII"
HEADER_LEN = struct.calcsize(_HEADER_FMT)   # 24


@dataclass(frozen=True)
class Frame:
    flags: int
    method: int
    seq: int
    aux: int
    payload: bytes

    @property
    def is_response(self) -> bool:
        return self.flags == FRAME_RESPONSE

    @property
    def is_notify(self) -> bool:
        return self.flags == FRAME_NOTIFY

    @property
    def rc(self) -> int:
        """aux as a signed error code (0 = ok)."""
        return struct.unpack("<i", struct.pack("<I", self.aux))[0]


def build_frame(flags: int, method: int, seq: int, payload: bytes = b"") -> bytes:
    header = struct.pack(_HEADER_FMT, PROTO_VER, flags, SERVICE, method, seq,
                         len(payload), 0)
    body = header + payload
    length = struct.pack("<I", len(body) + 4)
    crc = zlib.crc32(length + body) & 0xFFFFFFFF
    return SYNC + length + body + struct.pack("<I", crc)


class FrameParser:
    """Stateful byte sink → complete frames. Tolerant of partials/junk/batching."""

    _MAX_BUFFER = 1 << 16   # guard against a runaway length field

    def __init__(self):
        self._buf = bytearray()

    def feed(self, data: bytes) -> List[Frame]:
        self._buf += data
        out: List[Frame] = []
        while True:
            i = self._buf.find(SYNC)
            if i < 0:
                # No sync yet — keep only a possible trailing partial 0xAA.
                tail = self._buf[-1:] if self._buf[-1:] == b"\xAA" else b""
                self._buf = bytearray(tail)
                break
            if i > 0:
                del self._buf[:i]            # drop junk before the sync
            if len(self._buf) < len(SYNC) + 4:
                break                        # need the length field
            length = struct.unpack("<I", self._buf[2:6])[0]
            if length > self._MAX_BUFFER:    # bogus → resync past this sync
                del self._buf[:2]
                continue
            total = len(SYNC) + 4 + length
            if len(self._buf) < total:
                break                        # wait for the rest of the frame
            raw = bytes(self._buf[:total])
            del self._buf[:total]
            frame = self._parse(raw, length)
            if frame is not None:
                out.append(frame)
        return out

    @staticmethod
    def _parse(raw: bytes, length: int) -> "Frame | None":
        len_field = raw[2:6]
        body = raw[6:6 + length - 4]         # header + payload
        crc_rx = struct.unpack("<I", raw[6 + length - 4:6 + length])[0]
        if (zlib.crc32(len_field + body) & 0xFFFFFFFF) != crc_rx:
            return None                      # corrupt → drop, resync continues
        if len(body) < HEADER_LEN:
            return None
        ver, flags, service, method, seq, pl_len, aux = struct.unpack(
            _HEADER_FMT, body[:HEADER_LEN])
        return Frame(flags=flags, method=method, seq=seq, aux=aux,
                     payload=body[HEADER_LEN:])


# ── payload decoders (pure) — match the firmware TestStation handlers ────────
def _cstr(b: bytes) -> str:
    return b.split(b"\x00", 1)[0].decode("ascii", "replace")


def decode_device_info(payload: bytes) -> dict:
    """GetDeviceInfo reply: present(1) serialized(1) hwRev(2) vid(2) pid(2)
    model[17] variant[17] serial[25] (packed LE) — see src/port/rp2350-mfg."""
    present, serialized, hw_rev, vid, pid = struct.unpack("<BBHHH", payload[0:8])
    return {
        "present": present != 0,
        "serialized": serialized != 0,
        "hw_rev": hw_rev, "vid": vid, "pid": pid,
        "model": _cstr(payload[8:25]),
        "variant": _cstr(payload[25:42]),
        "serial": _cstr(payload[42:67]),
    }


def decode_identity(payload: bytes) -> dict:
    """GetIdentity reply: chipUid[8] fw(4) protoHash(4)."""
    return {
        "chip_uid": payload[:8].hex().upper(),
        "fw_version": struct.unpack("<I", payload[8:12])[0],
        "proto_hash": struct.unpack("<I", payload[12:16])[0],
    }


def decode_buttons(payload: bytes) -> int:
    """ScanButtons reply: held bitmask (u32 LE)."""
    return struct.unpack("<I", payload[:4])[0]


def decode_button_event(payload: bytes) -> tuple:
    """ButtonEvent notify: (button_index, type) — type 0=press(Down), 1=release(Up)."""
    return (payload[0], payload[1]) if len(payload) >= 2 else (None, None)
