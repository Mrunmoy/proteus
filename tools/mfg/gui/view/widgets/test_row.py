"""TestRowWidget — one row in the test grid: a select checkbox + name + state pill.

The checkbox selects the test for RMA/test-only runs. For a full manufacture it's
forced on and disabled (a new unit must pass every test to serialize).
"""
from PySide6.QtCore import Qt
from PySide6.QtWidgets import QCheckBox, QHBoxLayout, QLabel, QWidget

from domain.models import TestState

_STYLE = {
    TestState.PENDING:           ("—",                 "#666"),
    TestState.RUNNING:           ("running…",          "#1675d1"),
    TestState.AWAITING_OPERATOR: ("awaiting operator", "#d98a00"),
    TestState.PASSED:            ("PASS",              "#2a9d2a"),
    TestState.FAILED:            ("FAIL",              "#cc2222"),
    TestState.SKIPPED:           ("skipped",           "#888"),
}


class TestRowWidget(QWidget):
    def __init__(self, name: str, parent=None):
        super().__init__(parent)
        self.name = name
        layout = QHBoxLayout(self)
        layout.setContentsMargins(8, 4, 8, 4)
        self._check = QCheckBox()
        self._check.setChecked(True)
        self._label = QLabel(name)
        self._label.setStyleSheet("font-size: 14px;")
        self._pill = QLabel()
        self._pill.setMinimumWidth(150)
        self._pill.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(self._check)
        layout.addWidget(self._label)
        layout.addStretch(1)
        layout.addWidget(self._pill)
        self.set_state(TestState.PENDING)

    def is_checked(self) -> bool:
        return self._check.isChecked()

    def set_checked(self, on: bool) -> None:
        self._check.setChecked(on)

    def set_selectable(self, on: bool) -> None:
        """Enable/disable the checkbox WITHOUT changing its state — disabling must never
        wipe the operator's RMA selection (that was the 're-selects all' bug)."""
        self._check.setEnabled(on)

    def set_state(self, state: TestState) -> None:
        text, colour = _STYLE[state]
        self._pill.setText(text)
        self._pill.setStyleSheet(
            f"background:{colour}; color:white; border-radius:7px; "
            f"padding:4px 10px; font-weight:bold;")
