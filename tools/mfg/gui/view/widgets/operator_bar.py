"""OperatorBar — the operator dialog seam: a question + Yes/No, as buttons not typing.

Emits `answered(bool)`. The controller drives it via its `operatorPrompt(str)` signal
(empty string clears it). Decoupled: the bar knows nothing of what it's asking about.
"""
from PySide6.QtCore import Signal
from PySide6.QtWidgets import QHBoxLayout, QLabel, QPushButton, QWidget


class OperatorBar(QWidget):
    answered = Signal(bool)

    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QHBoxLayout(self)
        self._question = QLabel()
        self._question.setStyleSheet("font-size: 15px; font-weight: bold;")
        self._yes = QPushButton("✓  Yes")
        self._no = QPushButton("✗  No")
        self._yes.setStyleSheet("background:#2a9d2a; color:white; padding:6px 16px;")
        self._no.setStyleSheet("background:#cc2222; color:white; padding:6px 16px;")
        self._yes.clicked.connect(lambda: self.answered.emit(True))
        self._no.clicked.connect(lambda: self.answered.emit(False))
        layout.addWidget(self._question)
        layout.addStretch(1)
        layout.addWidget(self._yes)
        layout.addWidget(self._no)
        self.clear()

    # slot for controller.operatorPrompt
    def on_prompt(self, text: str) -> None:
        if text:
            self._question.setText(text)
            self.setVisible(True)
        else:
            self.clear()

    def clear(self) -> None:
        self._question.setText("")
        self.setVisible(False)
