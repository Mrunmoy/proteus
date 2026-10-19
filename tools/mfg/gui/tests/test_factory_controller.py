"""FactoryController happy-path integration test — fake adapters, no hardware.

A small driver loop reacts to whatever token the controller is awaiting and emits the
matching fake signal, exactly as real adapters would. The device-info block the
controller writes to /tmp is read back to build the GetDeviceInfo reply, so the
serialize read-back-verify is genuine (the serial the controller mints must come back).
"""
import struct
import unittest

from PySide6.QtCore import QObject, Signal
from PySide6.QtWidgets import QApplication

import device_info as dinfo
import device_types as dt
import controller.factory_controller as fc
from controller.factory_controller import (
    FactoryController, _App, _Bootsel, _Buttons, _Delay, _Operator, _Pico, _Product, _Rpc)
from devices.frame import (
    BUTTON_EVENT_MSG, M_GET_DEVICE_INFO, M_HDMI_SHOW_PATTERN, M_SCAN_BUTTONS,
    M_SD_PROBE)
from domain.models import Phase
from persistence.repository import UnitRepository

_app = QApplication.instance() or QApplication([])


class _FakeMonitor(QObject):
    enteredBootsel = Signal(str)
    enteredApp = Signal(str)
    productAttached = Signal(str)
    detached = Signal(str)


class _FakeRpc(QObject):
    responseReceived = Signal(int, int, bytes)
    notifyReceived = Signal(int, bytes)

    def __init__(self):
        super().__init__()
        self._seq = 0x1000
        self.sent = {}

    def _send(self, method, payload=b""):
        self._seq += 1
        self.sent[self._seq] = (method, payload)
        return self._seq

    def get_device_info(self): return self._send(M_GET_DEVICE_INFO)
    def set_led(self, on): return self._send(3, bytes([1 if on else 0]))
    def lcd_show_pattern(self, p): return self._send(4, bytes([p]))
    def scan_buttons(self): return self._send(M_SCAN_BUTTONS)
    def play_tone(self, f, m): return self._send(6)
    def sd_probe(self): return self._send(M_SD_PROBE)
    def hdmi_show_pattern(self, p=0): return self._send(M_HDMI_SHOW_PATTERN, bytes([p]))
    def open(self, port): return True
    def close(self): pass


class _FakePico(QObject):
    finished = Signal(bool, str, str)
    def reboot_to_bootsel(self, ser): pass
    def reboot_to_app(self, ser): pass
    def program_block(self, path, addr, ser): pass
    def flash_image(self, path, ser): pass


def _devinfo_reply():
    """Build a GetDeviceInfo wire payload from what the controller just wrote."""
    with open("/tmp/proteus_devinfo_A.bin", "rb") as f:
        di = dinfo.parse(f.read())
    body = struct.pack("<BBHHH", 1, 1 if di["serialized"] else 0,
                       di["hw_rev"], di["vid"], di["pid"])
    body += di["model"].encode().ljust(17, b"\x00")
    body += di["variant"].encode().ljust(17, b"\x00")
    body += di["serial"].encode().ljust(25, b"\x00")
    return body


class FactoryControllerTest(unittest.TestCase):
    def setUp(self):
        # Neutralise the hardware-touching helpers.
        fc.find_debug_port = lambda uid: "/dev/null"
        fc.find_rpc_port = lambda uid: "/dev/null"
        fc.pulse_1200 = lambda path: True

        self.repo = UnitRepository(":memory:")
        self.mon, self.rpc, self.pico = _FakeMonitor(), _FakeRpc(), _FakePico()
        self.dtype = dt.by_model("RP2350-PiZero")
        self.ctrl = FactoryController(self.repo, self.rpc, self.mon, self.pico,
                                      self.dtype, plant="A")
        self.events = []
        self.ctrl.serialAssigned.connect(lambda s: self.events.append(("serial", s)))
        self.ctrl.lineComplete.connect(lambda s: self.events.append(("complete", s)))
        self.ctrl.unitRejected.connect(lambda r: self.events.append(("reject", r)))
        self.ctrl.phaseChanged.connect(lambda p: self.events.append(("phase", p)))

    def tearDown(self):
        self.repo.close()

    def _drive(self):
        """React to each awaited token like real hardware until the line ends."""
        # Board appears in app mode (so the controller latches it).
        self.mon.enteredApp.emit("70814E6FEE6185BD")
        self.ctrl.start()
        self._pump()

    def _pump(self):
        for _ in range(2000):                 # safety bound
            a = self.ctrl._await
            if a is None:
                break
            if isinstance(a, _Bootsel):
                self.mon.enteredBootsel.emit(a.uid)
            elif isinstance(a, _App):
                self.mon.enteredApp.emit(a.uid)
            elif isinstance(a, _Pico):
                self.pico.finished.emit(True, "", "")
            elif isinstance(a, _Delay):
                self.ctrl._resume_if(a, None)
            elif isinstance(a, _Operator):
                self.ctrl.operator_answer(True)
            elif isinstance(a, _Buttons):
                for b in range(12):                           # push a press notify per button
                    self.rpc.notifyReceived.emit(BUTTON_EVENT_MSG, bytes([b, 0]))
            elif isinstance(a, _Product):
                self.mon.productAttached.emit(a.uid)          # product re-enumerated
            elif isinstance(a, _Rpc):
                method, sent = self.rpc.sent[a.seq]
                if method == M_GET_DEVICE_INFO:
                    payload = _devinfo_reply()
                elif method == M_SCAN_BUTTONS:
                    payload = struct.pack("<I", 0xFFF)        # all buttons held
                elif method == M_SD_PROBE:
                    payload = bytes([1]) + struct.pack("<I", 3)  # present + 3 files
                elif method == M_HDMI_SHOW_PATTERN:
                    payload = bytes([1])                      # driver came up
                else:
                    payload = bytes([1])                      # applied
                self.rpc.responseReceived.emit(a.seq, 0, payload)

    def test_happy_path_serializes_and_records(self):
        self._drive()
        kinds = [e[0] for e in self.events]
        self.assertIn("serial", kinds)
        self.assertIn("complete", kinds)
        self.assertNotIn("reject", kinds)
        # A unit was recorded, serial bound to the chip uid.
        self.assertEqual(self.repo.count(), 1)
        serial = dict(self.events)["serial"]
        unit = self.repo.find_by_serial(serial)
        self.assertEqual(unit.chip_uid, "70814E6FEE6185BD")
        self.assertTrue(unit.all_passed)
        # Reached COMPLETE.
        phases = [e[1] for e in self.events if e[0] == "phase"]
        self.assertIn(Phase.COMPLETE, phases)

    def test_failed_test_rejects_with_no_serial(self):
        # Operator says NO to the first prompt (LCD red) → reject, no serial recorded.
        answered = {"n": 0}

        def drive_reject():
            self.mon.enteredApp.emit("CHIP")
            self.ctrl.start()
            for _ in range(2000):
                a = self.ctrl._await
                if a is None:
                    break
                if isinstance(a, _Bootsel):
                    self.mon.enteredBootsel.emit(a.uid)
                elif isinstance(a, _App):
                    self.mon.enteredApp.emit(a.uid)
                elif isinstance(a, _Pico):
                    self.pico.finished.emit(True, "", "")
                elif isinstance(a, _Delay):
                    self.ctrl._resume_if(a, None)
                elif isinstance(a, _Operator):
                    self.ctrl.operator_answer(False)        # fail it
                elif isinstance(a, _Buttons):
                    self.ctrl.operator_answer(False)        # (not reached: LCD fails first)
                elif isinstance(a, _Rpc):
                    method, _ = self.rpc.sent[a.seq]
                    payload = _devinfo_reply() if method == M_GET_DEVICE_INFO else bytes([1])
                    self.rpc.responseReceived.emit(a.seq, 0, payload)

        drive_reject()
        kinds = [e[0] for e in self.events]
        self.assertIn("reject", kinds)
        self.assertNotIn("serial", kinds)
        self.assertEqual(self.repo.count(), 0)

    def test_reconnect_of_recorded_board_is_recognized(self):
        # A board already in the DB → alreadyManufactured (not offered as a fresh unit).
        from domain.models import TestOutcome, Unit
        self.repo.record(Unit("PRZ-A-2624-00001-4", "DEADBEEF", "RP2350-PiZero",
                              "GamePi13", 0x1209, 0x7201, 1, "A", 1_750_000_000,
                              outcomes=(TestOutcome("testLCD", True),)))
        seen, ready = [], []
        self.ctrl.alreadyManufactured.connect(lambda r: seen.append(r))
        self.ctrl.boardReady.connect(lambda u: ready.append(u))
        self.mon.enteredApp.emit("DEADBEEF")
        self.assertEqual(len(seen), 1)
        self.assertEqual(seen[0]["serial"], "PRZ-A-2624-00001-4")
        self.assertTrue(seen[0]["passed"])
        self.assertEqual(ready, [])

    def test_rma_records_event_runs_selected_only_no_serialize(self):
        # Pre-record a unit + the device-info file the fake GetDeviceInfo reads back.
        from domain.models import TestOutcome, Unit
        self.repo.record(Unit("PRZ-A-2624-00001-4", "RMACHIP", "RP2350-PiZero", "GamePi13",
                              0x1209, 0x7201, 1, "A", 1_750_000_000,
                              outcomes=(TestOutcome("testLCD", True),)))
        with open("/tmp/proteus_devinfo_A.bin", "wb") as f:
            f.write(dinfo.build("RP2350-PiZero", vid=0x1209, pid=0x7201, hw_rev=1,
                                variant="GamePi13", serial="PRZ-A-2624-00001-4", serial_utc=1))
        self.mon.enteredApp.emit("RMACHIP")        # → already-manufactured (sets uid, app mode)
        self.ctrl.rma_test(["testAudio"])           # RMA: run ONLY audio
        self._pump()
        # No new unit serialized (still the original record); RMA + diagnostic logged.
        self.assertEqual(self.repo.count(), 1)
        kinds = [e.kind for e in self.repo.rma_events("PRZ-A-2624-00001-4")]
        self.assertIn("rma", kinds)
        self.assertIn("diagnostic", kinds)

    def test_watchdog_aborts_instead_of_hanging(self):
        # A device that stops responding (no RPC reply) must surface a clean reject,
        # not freeze. Simulate mid-line, awaiting an RPC that never arrives.
        rejected = []
        self.ctrl.unitRejected.connect(lambda r: rejected.append(r))
        self.ctrl._busy = True
        self.ctrl._gen = (x for x in [])      # any non-None generator
        self.ctrl._await = _Rpc(42)
        self.ctrl._on_timeout()
        self.assertEqual(len(rejected), 1)
        self.assertFalse(self.ctrl._busy)
        self.assertIsNone(self.ctrl._gen)


if __name__ == "__main__":
    unittest.main()
