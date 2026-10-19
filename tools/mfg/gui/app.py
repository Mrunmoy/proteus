"""Composition root — build the layers, wire signals↔slots, run.

This is the ONLY module that knows about all three layers at once; everywhere else the
dependency flows one way. Wiring lives here and nowhere else, which is what keeps the
controller/view/model independently testable.

Run (in Docker, on your desktop):
    xhost +local:root
    docker compose run --rm station-gui
"""
import os
import sys

from PySide6.QtWidgets import QApplication

from controller.factory_controller import FactoryController
from device_types import by_model
from devices.device_monitor import DeviceMonitor
from devices.picotool import Picotool
from devices.rpc_link import RpcLink
from persistence.repository import UnitRepository
from view.main_window import MainWindow

_HERE = os.path.dirname(os.path.abspath(__file__))
REPO_DIR = os.path.abspath(os.path.join(_HERE, "..", "..", ".."))   # /work
DB_PATH = os.path.join(_HERE, "..", "proteus_units.db")


def _lookup_handler(repo, panel):
    def handler(text: str) -> None:
        unit = repo.find_by_serial(text)
        if unit:
            panel.show_result([unit], repo.rma_events(unit.serial))
            return
        units = repo.find_by_chip(text.upper())
        events = [e for u in units for e in repo.rma_events(u.serial)]
        panel.show_result(units, events)
    return handler


def build(app_repo_path=None):
    """Construct + wire everything; return (window, controller, monitor). No exec()."""
    db = app_repo_path or DB_PATH
    repo = UnitRepository(db if db == ":memory:" else os.path.abspath(db))
    rpc = RpcLink()
    monitor = DeviceMonitor()
    picotool = Picotool()      # runs the picotool binary in-container (no nested docker)
    controller = FactoryController(repo, rpc, monitor, picotool,
                                   by_model("RP2350-PiZero"), plant="A")
    win = MainWindow()
    s = win.mfg

    # controller → view
    controller.boardReady.connect(s.on_board_ready)
    controller.boardLost.connect(s.on_board_lost)
    controller.identityRead.connect(s.on_identity)
    controller.phaseChanged.connect(s.on_phase)
    controller.testStateChanged.connect(s.on_test_state)
    controller.operatorPrompt.connect(s.on_operator_prompt)
    controller.serialAssigned.connect(s.on_serial)
    controller.unitRejected.connect(s.on_reject)
    controller.alreadyManufactured.connect(s.on_already_manufactured)
    controller.lineComplete.connect(s.on_complete)
    controller.logged.connect(s.on_log)

    # view → controller
    s.startRequested.connect(controller.start)
    s.recipientChanged.connect(controller.set_recipient)
    s.operatorAnswered.connect(controller.operator_answer)
    s.rerunRequested.connect(controller.rerun)
    s.rmaTestRequested.connect(controller.rma_test)
    s.dismissRequested.connect(controller.dismiss)

    # RMA lookup
    win.rma.lookupRequested.connect(_lookup_handler(repo, win.rma))
    return win, controller, monitor


def main():
    app = QApplication(sys.argv)
    if DeviceMonitor is None:
        sys.stderr.write("pyudev/PySide6 missing — run inside the station container.\n")
        return 2
    win, _controller, monitor = build()
    monitor.scan_existing()        # one-shot: pick up an already-attached board
    win.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
