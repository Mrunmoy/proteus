"""Pure picotool argv-builder tests (no Qt, nothing executed).

picotool runs directly in-container now (no docker wrapper), so argv = the picotool
arguments, with --ser appended to target one board.
"""
import unittest

from devices.picotool import DEVICE_INFO_XIP_ADDR, picotool_argv


class PicotoolArgvTest(unittest.TestCase):
    def test_plain_args_passthrough(self):
        self.assertEqual(picotool_argv(["info"]), ["info"])

    def test_ser_appended_last(self):
        self.assertEqual(picotool_argv(["reboot", "-u", "-f"], ser="70814E6F"),
                         ["reboot", "-u", "-f", "--ser", "70814E6F"])

    def test_no_ser_omits_flag(self):
        self.assertNotIn("--ser", picotool_argv(["info"]))

    def test_program_load_offset_form(self):
        self.assertEqual(
            picotool_argv(["load", "/tmp/d.bin", "-o", hex(DEVICE_INFO_XIP_ADDR)], ser="X"),
            ["load", "/tmp/d.bin", "-o", "0x10ffc000", "--ser", "X"])

    def test_no_docker_wrapper(self):
        # The old design shelled out to `docker run …`; that must be gone.
        argv = picotool_argv(["reboot"])
        self.assertNotIn("docker", argv)
        self.assertNotIn("run", argv)


if __name__ == "__main__":
    unittest.main()
