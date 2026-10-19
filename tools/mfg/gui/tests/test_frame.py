"""Wire-protocol tests — pure, no Qt. Exercises the incremental parser hard."""
import struct
import unittest

from devices.frame import (
    FRAME_REQUEST, FRAME_RESPONSE, M_GET_IDENTITY, FrameParser, build_frame)


class FrameTest(unittest.TestCase):
    def test_build_parse_round_trip(self):
        raw = build_frame(FRAME_RESPONSE, M_GET_IDENTITY, 0x1234, b"hello")
        frames = FrameParser().feed(raw)
        self.assertEqual(len(frames), 1)
        f = frames[0]
        self.assertTrue(f.is_response)
        self.assertEqual(f.method, M_GET_IDENTITY)
        self.assertEqual(f.seq, 0x1234)
        self.assertEqual(f.payload, b"hello")
        self.assertEqual(f.rc, 0)

    def test_error_code_is_signed(self):
        # aux = 0xFFFFFFFF should decode as rc == -1.
        raw = bytearray(build_frame(FRAME_RESPONSE, 7, 1, b""))
        # aux is the last 4 bytes of the 24B header; patch + fix CRC by re-building.
        # Easier: craft via build then verify the helper interprets aux signedness.
        import zlib
        from devices.frame import SYNC, _HEADER_FMT, PROTO_VER, SERVICE  # type: ignore
        header = struct.pack(_HEADER_FMT, PROTO_VER, FRAME_RESPONSE, SERVICE, 7, 1, 0,
                             0xFFFFFFFF)
        length = struct.pack("<I", len(header) + 4)
        crc = zlib.crc32(length + header) & 0xFFFFFFFF
        raw = SYNC + length + header + struct.pack("<I", crc)
        f = FrameParser().feed(raw)[0]
        self.assertEqual(f.rc, -1)

    def test_split_across_multiple_feeds(self):
        raw = build_frame(FRAME_REQUEST, 3, 5, b"\x01\x02\x03")
        p = FrameParser()
        # Feed one byte at a time — must surface exactly when complete.
        got = []
        for b in raw:
            got += p.feed(bytes([b]))
        self.assertEqual(len(got), 1)
        self.assertEqual(got[0].payload, b"\x01\x02\x03")

    def test_two_frames_in_one_feed(self):
        a = build_frame(FRAME_RESPONSE, 1, 10, b"a")
        b = build_frame(FRAME_RESPONSE, 2, 11, b"bb")
        frames = FrameParser().feed(a + b)
        self.assertEqual([f.seq for f in frames], [10, 11])

    def test_junk_before_sync_is_skipped(self):
        raw = build_frame(FRAME_RESPONSE, 1, 1, b"x")
        frames = FrameParser().feed(b"\x00\xde\xad\xbe\xef" + raw)
        self.assertEqual(len(frames), 1)

    def test_corrupt_crc_dropped(self):
        raw = bytearray(build_frame(FRAME_RESPONSE, 1, 1, b"payload"))
        raw[-1] ^= 0xFF                       # corrupt the CRC
        self.assertEqual(FrameParser().feed(bytes(raw)), [])

    def test_recovers_after_corrupt_frame(self):
        bad = bytearray(build_frame(FRAME_RESPONSE, 1, 1, b"bad"))
        bad[-1] ^= 0xFF
        good = build_frame(FRAME_RESPONSE, 2, 2, b"good")
        frames = FrameParser().feed(bytes(bad) + good)
        # Bad one dropped; the following good one still parses.
        self.assertEqual(len(frames), 1)
        self.assertEqual(frames[0].payload, b"good")

    def test_bogus_length_resyncs(self):
        # AA 55 then a huge length, then a real frame after.
        good = build_frame(FRAME_RESPONSE, 1, 9, b"ok")
        frames = FrameParser().feed(b"\xAA\x55\xff\xff\xff\xff" + good)
        self.assertEqual(len(frames), 1)
        self.assertEqual(frames[0].seq, 9)

    def test_decode_button_event(self):
        from devices.frame import decode_button_event
        self.assertEqual(decode_button_event(bytes([4, 0])), (4, 0))     # A pressed (Down)
        self.assertEqual(decode_button_event(bytes([11, 1])), (11, 1))   # Select released
        self.assertEqual(decode_button_event(b""), (None, None))


if __name__ == "__main__":
    unittest.main()
