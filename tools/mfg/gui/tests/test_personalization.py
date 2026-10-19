"""Personalization block format — must stay in lockstep with the firmware struct
src/port/rp2350/flash/Personalization.h (magic + 32-byte NUL-padded name)."""
import struct
import unittest

import personalization as pers


class PersonalizationTest(unittest.TestCase):
    def test_block_layout_matches_firmware(self):
        block = pers.build("Adam")
        self.assertEqual(len(block), 4 + pers.NAME_FIELD)        # 36 bytes total
        (magic,) = struct.unpack_from("<I", block, 0)
        self.assertEqual(magic, pers.MAGIC)
        name = block[4:].split(b"\x00", 1)[0].decode()
        self.assertEqual(name, "Adam")

    def test_name_nul_terminated_and_padded(self):
        block = pers.build("Al")
        self.assertEqual(block[4 + 2], 0)                        # NUL right after the name
        self.assertEqual(block[4:], b"Al" + b"\x00" * (pers.NAME_FIELD - 2))

    def test_too_long_name_rejected(self):
        with self.assertRaises(ValueError):
            pers.build("X" * pers.NAME_FIELD)

    def test_xip_addr_matches_flash_offset(self):
        self.assertEqual(pers.XIP_ADDR, 0x10000000 + 0x3FD000)


if __name__ == "__main__":
    unittest.main()
