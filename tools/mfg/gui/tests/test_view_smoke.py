"""View smoke tests — build the window offscreen, drive slots, no crash."""
import unittest

from PySide6.QtWidgets import QApplication

from domain.models import Phase, TestState
from view.main_window import MainWindow

_app = QApplication.instance() or QApplication([])


class ViewSmokeTest(unittest.TestCase):
    def test_window_builds_and_slots_run(self):
        win = MainWindow()
        s = win.mfg
        s.on_board_ready("70814E6FEE6185BD")
        s.on_identity({"model": "RP2350-PiZero", "variant": "GamePi13",
                       "vid": 0x1209, "pid": 0x7201})
        s.on_phase(Phase.PROGRAM)
        s.on_test_state("testLCD", TestState.RUNNING)
        s.on_operator_prompt("Is the LCD solid RED?")
        s.on_operator_prompt("")               # clears
        s.on_serial("PRZ-A-2624-00001-4")
        s.on_phase(Phase.COMPLETE)
        s.on_log("hello")
        self.assertEqual(win.mfg._rows["testLCD"]._pill.text(), "running…")

    def test_rma_panel_no_result(self):
        win = MainWindow()
        win.rma.show_result([], [])
        self.assertIn("No matching", win.rma._out.toPlainText())

    def test_app_module_wires_without_hardware(self):
        # build() constructs every layer + wires signals (no exec, no monitor scan).
        import app
        win, controller, monitor = app.build(app_repo_path=":memory:")
        self.assertIsNotNone(win.mfg)
        self.assertIsNotNone(controller)


if __name__ == "__main__":
    unittest.main()
