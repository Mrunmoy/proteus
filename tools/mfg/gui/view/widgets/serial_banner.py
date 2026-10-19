"""SerialBanner — the big status line: current phase, then the assigned serial."""
from PySide6.QtCore import Qt
from PySide6.QtWidgets import QLabel

from domain.models import Phase

_PHASE = {
    Phase.IDLE:      ("Idle — connect a board", "#444"),
    Phase.PROGRAM:   ("PROGRAMMING identity…",  "#1675d1"),
    Phase.TEST:      ("TESTING…",               "#1675d1"),
    Phase.SERIALIZE: ("SERIALIZING…",           "#d98a00"),
    Phase.SHIP:      ("SHIPPING…",              "#d98a00"),
    Phase.COMPLETE:  ("UNIT COMPLETE",          "#2a9d2a"),
    Phase.REJECTED:  ("UNIT REJECTED",          "#cc2222"),
}


class SerialBanner(QLabel):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.setMinimumHeight(56)
        self.on_phase(Phase.IDLE)

    def on_phase(self, phase: Phase) -> None:
        text, colour = _PHASE.get(phase, ("…", "#444"))
        self._render(text, colour)

    def on_ready(self) -> None:
        self._render("Board ready — press START", "#1675d1")

    def on_serial(self, serial: str) -> None:
        self._render(serial, "#2a9d2a")

    def on_reject(self, reason: str) -> None:
        self._render(f"REJECTED — {reason}", "#cc2222")

    def _render(self, text: str, colour: str) -> None:
        self.setText(text)
        self.setStyleSheet(
            f"background:{colour}; color:white; border-radius:8px; "
            f"font-size:20px; font-weight:bold; padding:10px;")
