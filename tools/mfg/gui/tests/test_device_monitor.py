"""Pure udev-event classification tests (no Qt, no udev)."""
import unittest

from devices.device_monitor import DeviceEvent, classify_event


class ClassifyTest(unittest.TestCase):
    def test_bootsel(self):
        self.assertEqual(classify_event("add", "2e8a", "000f", "70814E6F"),
                         DeviceEvent("bootsel", "70814E6F"))

    def test_app_pidcodes(self):
        self.assertEqual(classify_event("add", "1209", "7201", "ABC"),
                         DeviceEvent("app", "ABC"))

    def test_app_legacy_vid_and_bind_action(self):
        self.assertEqual(classify_event("bind", "2e8a", "4001", "ABC"),
                         DeviceEvent("app", "ABC"))

    def test_removals_not_classified_here(self):
        # udev removals carry no serial; DeviceMonitor matches them by sysfs path,
        # so classify_event (add/bind only) returns None for them.
        self.assertIsNone(classify_event("remove", None, None, None))
        self.assertIsNone(classify_event("remove", "1209", "7201", "ABC"))
        self.assertIsNone(classify_event("unbind", "2e8a", "000f", "ABC"))

    def test_product_classified(self):
        # DemoApp (2e8a:0009) is a SHIPPED product — recognized for reconnect lookup.
        self.assertEqual(classify_event("add", "2e8a", "0009", "ABC"),
                         DeviceEvent("product", "ABC"))

    def test_unknown_pid_is_none(self):
        self.assertIsNone(classify_event("add", "2e8a", "1234", "ABC"))

    def test_no_serial_is_none(self):
        self.assertIsNone(classify_event("add", "2e8a", "000f", None))

    def test_uppercase_hex_ok(self):
        self.assertEqual(classify_event("add", "2E8A", "000F", "X"),
                         DeviceEvent("bootsel", "X"))


if __name__ == "__main__":
    unittest.main()
