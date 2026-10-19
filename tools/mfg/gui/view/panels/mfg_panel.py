"""MfgPanel — the operator's working surface for one unit, start to finish.

Pure View: slots that the controller's signals connect to, and intents it emits
(startRequested, operatorAnswered, rerunRequested, rmaTestRequested, dismissRequested).
No hardware, no DB. A fresh board shows START (runs ALL tests → serialize); a board
already in the DB shows an action bar: RMA (mark + run the checked tests, no rewrite/
re-serialize) or Re-manufacture (keep/new serial, full run).
"""
import time as _time

from PySide6.QtCore import Signal
from PySide6.QtWidgets import (
    QGroupBox, QHBoxLayout, QLabel, QLineEdit, QPlainTextEdit, QPushButton,
    QVBoxLayout, QWidget)

from domain.models import Phase, TestState
from view.widgets.operator_bar import OperatorBar
from view.widgets.serial_banner import SerialBanner
from view.widgets.test_row import TestRowWidget

_TESTS = ["testLCD", "testTearing", "testButtons", "testAudio", "testSD", "testHDMI"]


class MfgPanel(QWidget):
    startRequested = Signal(list)        # tests the operator ticked for this run
    operatorAnswered = Signal(bool)
    rerunRequested = Signal(bool, list)  # (keep_serial, ticked tests to run)
    rmaTestRequested = Signal(list)      # selected test names (RMA diagnostic)
    dismissRequested = Signal()
    recipientChanged = Signal(str)       # gift name for the board on the line

    def __init__(self, parent=None):
        super().__init__(parent)
        self._uid = None
        root = QVBoxLayout(self)

        self._identity = QLabel("No board connected")
        self._identity.setStyleSheet("font-size: 14px; color:#333; padding:4px;")
        root.addWidget(self._identity)

        # Gift recipient: written into the splash during SHIP. Optional — left blank
        # ships a generic splash.
        recip_row = QHBoxLayout()
        recip_row.addWidget(QLabel("Gift name:"))
        self._recipient = QLineEdit()
        self._recipient.setMaxLength(31)            # matches kRecipientMax
        self._recipient.setPlaceholderText("recipient shown on the splash (e.g. Adam)")
        self._recipient.textChanged.connect(self.recipientChanged)
        recip_row.addWidget(self._recipient)
        root.addLayout(recip_row)

        self._start = QPushButton("▶  START")
        self._start.setStyleSheet("font-size:16px; font-weight:bold; padding:10px;")
        self._start.setEnabled(False)
        self._start.clicked.connect(lambda: self.startRequested.emit(self.selected_tests()))
        root.addWidget(self._start)

        # ── action bar for a board already in the DB (hidden until then) ──────
        self._action_box = QGroupBox("Already manufactured")
        abl = QVBoxLayout(self._action_box)
        self._action_label = QLabel()
        self._action_label.setWordWrap(True)
        abl.addWidget(self._action_label)
        brow = QHBoxLayout()
        b_rma = QPushButton("RMA — mark && test checked")
        b_keep = QPushButton("Re-manufacture · keep serial")
        b_new = QPushButton("Re-manufacture · new serial")
        b_dismiss = QPushButton("Dismiss")
        b_rma.setStyleSheet("background:#d98a00; color:white; padding:6px 10px;")
        b_rma.clicked.connect(lambda: self.rmaTestRequested.emit(self.selected_tests()))
        b_keep.clicked.connect(lambda: self.rerunRequested.emit(True, self.selected_tests()))
        b_new.clicked.connect(lambda: self.rerunRequested.emit(False, self.selected_tests()))
        b_dismiss.clicked.connect(self._on_dismiss)
        for b in (b_rma, b_keep, b_new, b_dismiss):
            brow.addWidget(b)
        abl.addLayout(brow)
        self._action_box.setVisible(False)
        root.addWidget(self._action_box)

        grid = QGroupBox("Test suite  (untick a test to skip it)")
        gl = QVBoxLayout(grid)
        self._rows = {}
        for name in _TESTS:
            row = TestRowWidget(name)
            row.set_selectable(False)        # full manufacture runs all; RMA enables it
            self._rows[name] = row
            gl.addWidget(row)
        root.addWidget(grid)

        self._operator = OperatorBar()
        self._operator.answered.connect(self.operatorAnswered)
        root.addWidget(self._operator)

        self._banner = SerialBanner()
        root.addWidget(self._banner)

        self._log = QPlainTextEdit()
        self._log.setReadOnly(True)
        self._log.setMaximumBlockCount(500)
        self._log.setFixedHeight(120)
        root.addWidget(self._log)

    def selected_tests(self) -> list:
        return [name for name, row in self._rows.items() if row.is_checked()]

    # ── slots wired to the controller ────────────────────────────────────────
    def on_board_ready(self, uid: str) -> None:
        self._uid = uid
        self._identity.setText(f"Board present — chipUID {uid}")
        self._recipient.clear()              # fresh board → blank gift name
        self._action_box.setVisible(False)
        self._start.setVisible(True)
        self._start.setEnabled(True)
        self._set_all_checked()              # default: run every test…
        self._set_selectable(True)           # …but the operator may untick to skip
        self._banner.on_ready()
        self._reset_rows()

    def on_board_lost(self) -> None:
        self._uid = None
        self._identity.setText("No board connected")
        self._action_box.setVisible(False)
        self._start.setVisible(True)
        self._start.setEnabled(False)
        self._banner.on_phase(Phase.IDLE)

    def on_already_manufactured(self, rec: dict) -> None:
        self._uid = rec["chip_uid"]
        when = _time.strftime("%Y-%m-%d %H:%M UTC", _time.gmtime(rec["built_utc"]))
        status = "PASSED" if rec["passed"] else "FAILED"
        self._identity.setText(
            f"Already manufactured: {rec['serial']} · built {when} · {status}")
        shipped, current = rec.get("shipped_fw") or "—", rec.get("current_fw") or "?"
        if current not in ("", "?") and shipped not in ("", "—") and current != shipped:
            fw = f"firmware: shipped {shipped} · ON DEVICE {current}  ⚠ updated since shipping"
        elif current not in ("", "?"):
            fw = f"firmware: shipped {shipped} · on device {current}"
        else:
            fw = f"firmware: shipped {shipped}"
        self._action_label.setText(
            f"Serial {rec['serial']} · built {when} · tests {status}\n"
            f"{fw}\n"
            f"RMA (returned) unit?  Tick the tests to run, then click RMA — it records "
            f"the RMA and runs only those tests (no re-serialize, no device-info rewrite).\n"
            f"Otherwise Re-manufacture runs the full suite and serializes.")
        self._start.setVisible(False)
        self._action_box.setVisible(True)
        self._set_all_checked()              # default to all; operator unticks to skip
        self._set_selectable(True)           # operator chooses tests for the RMA run
        self._reset_rows()

    def on_complete(self, serial: str) -> None:
        self._identity.setText(f"✓ {serial} done — disconnect and connect the next board")
        self._action_box.setVisible(False)
        self._start.setVisible(True)
        self._start.setEnabled(False)
        self._banner.on_serial(serial)

    def on_identity(self, info: dict) -> None:
        if info:
            self._identity.setText(
                f"chipUID {self._uid}  ·  {info.get('model','')} / {info.get('variant','')}  ·  "
                f"{info.get('vid',0):#06x}:{info.get('pid',0):#06x}")

    def on_phase(self, phase: Phase) -> None:
        self._banner.on_phase(phase)
        busy = phase not in (Phase.IDLE, Phase.COMPLETE, Phase.REJECTED)
        if busy:
            self._action_box.setVisible(False)
            self._start.setEnabled(False)
            self._set_selectable(False)
        if phase == Phase.PROGRAM:
            self._reset_rows()

    def on_test_state(self, name: str, state: TestState) -> None:
        if name in self._rows:
            self._rows[name].set_state(state)

    def on_operator_prompt(self, text: str) -> None:
        self._operator.on_prompt(text)

    def on_serial(self, serial: str) -> None:
        self._banner.on_serial(serial)

    def on_reject(self, reason: str) -> None:
        self._banner.on_reject(reason)

    def on_log(self, msg: str) -> None:
        self._log.appendPlainText(msg)

    # ── helpers ──
    def _on_dismiss(self) -> None:
        self._action_box.setVisible(False)
        self._identity.setText("Dismissed — disconnect the unit.")
        self.dismissRequested.emit()

    def _set_selectable(self, on: bool) -> None:
        for row in self._rows.values():
            row.set_selectable(on)

    def _set_all_checked(self) -> None:
        for row in self._rows.values():
            row.set_checked(True)

    def _reset_rows(self) -> None:
        for row in self._rows.values():
            row.set_state(TestState.PENDING)
