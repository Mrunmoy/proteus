"""RmaPanel — database lookup for RMA: search by serial or chipUID, show the record.

Decoupled: emits `lookupRequested(text)`; the app wires that to a query and calls
`show_result(...)`. The panel never touches the repository directly.
"""
import time as _time
from typing import List

from PySide6.QtCore import Signal
from PySide6.QtWidgets import (
    QHBoxLayout, QLabel, QLineEdit, QPushButton, QTextEdit, QVBoxLayout, QWidget)

from domain.models import RmaEvent, Unit


class RmaPanel(QWidget):
    lookupRequested = Signal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        root = QVBoxLayout(self)
        row = QHBoxLayout()
        self._field = QLineEdit()
        self._field.setPlaceholderText("Serial (PRZ-A-…) or chipUID")
        self._field.returnPressed.connect(self._emit_lookup)
        btn = QPushButton("Search")
        btn.clicked.connect(self._emit_lookup)
        row.addWidget(QLabel("Lookup:"))
        row.addWidget(self._field)
        row.addWidget(btn)
        root.addLayout(row)

        self._out = QTextEdit()
        self._out.setReadOnly(True)
        self._out.setStyleSheet("font-family: monospace;")
        root.addWidget(self._out)

    def _emit_lookup(self) -> None:
        text = self._field.text().strip()
        if text:
            self.lookupRequested.emit(text)

    @staticmethod
    def _fmt(utc) -> str:
        try:
            return _time.strftime("%Y-%m-%d %H:%M:%S UTC", _time.gmtime(int(utc)))
        except (ValueError, OSError, TypeError):
            return str(utc)

    # ── slot: the app hands back the query result ────────────────────────────
    def show_result(self, units: List[Unit], events: List[RmaEvent]) -> None:
        if not units:
            self._out.setPlainText("No matching unit found.")
            return
        lines = []
        for u in units:
            lines.append(f"SERIAL   {u.serial}")
            lines.append(f"chipUID  {u.chip_uid}")
            lines.append(f"product  {u.model} / {u.variant}   "
                         f"{u.vid:#06x}:{u.pid:#06x}  hw_rev {u.hw_rev}")
            lines.append(f"shipped  {self._fmt(u.built_utc)} · plant {u.plant} · "
                         f"firmware {u.fw_version or '—'}")
            lines.append("tests:")
            for o in u.outcomes:
                lines.append(f"   {'PASS' if o.passed else 'FAIL'}  {o.name}  {o.detail}")
            lines.append("")
        if events:
            lines.append("RMA events:")
            for e in events:
                lines.append(f"   {self._fmt(e.event_utc)}  {e.kind}: {e.note}")
        self._out.setPlainText("\n".join(lines))
