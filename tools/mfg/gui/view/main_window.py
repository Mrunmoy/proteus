"""MainWindow — the two tabs (Manufacturing, Database/RMA). Pure composition."""
from PySide6.QtWidgets import QMainWindow, QTabWidget

from view.panels.mfg_panel import MfgPanel
from view.panels.rma_panel import RmaPanel


class MainWindow(QMainWindow):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Proteus Manufacturing Tester")
        self.resize(640, 720)
        self.mfg = MfgPanel()
        self.rma = RmaPanel()
        tabs = QTabWidget()
        tabs.addTab(self.mfg, "Manufacturing")
        tabs.addTab(self.rma, "Database / RMA")
        self.setCentralWidget(tabs)
