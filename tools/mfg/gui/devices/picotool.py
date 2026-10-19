"""Picotool — flash/program/reboot via the picotool binary, async (QProcess).

picotool runs DIRECTLY inside this (privileged, /dev/bus/usb) container — NOT via a
nested `docker run` (there is no docker inside the container). NO blocking subprocess:
each op launches a QProcess and the result arrives on `finished`. A start failure
(errorOccurred) is ALSO surfaced as finished(False, ...), so the controller can never
hang waiting for a process that never ran. The argv builder is a PURE, unit-tested fn.
"""
from __future__ import annotations

from typing import List, Optional

# Device-info region (mirrors src/common/device_info.h / device_info.py).
DEVICE_INFO_XIP_ADDR = 0x10FFC000


def picotool_argv(args: List[str], ser: Optional[str] = None) -> List[str]:
    """picotool arguments; --ser pins one board when several are in BOOTSEL.

    Paths are container-absolute: device-info .bin in /tmp, firmware under /work
    (the mounted repo). picotool itself is the program — not docker.
    """
    return list(args) + (["--ser", ser] if ser else [])


try:
    from PySide6.QtCore import QObject, QProcess, Signal

    class Picotool(QObject):
        # ok, merged-output-text, tag (so the controller knows which op finished)
        finished = Signal(bool, str, str)

        def __init__(self, parent=None):
            super().__init__(parent)
            self._procs = []   # keep refs so QProcess isn't GC'd mid-run

        def _run(self, args: List[str], ser: Optional[str], tag: str) -> None:
            proc = QProcess(self)
            proc.setProcessChannelMode(QProcess.ProcessChannelMode.MergedChannels)
            state = {"done": False}

            def emit(ok, text, p=proc, t=tag):
                if state["done"]:
                    return
                state["done"] = True
                self.finished.emit(ok, text, t)
                if p in self._procs:
                    self._procs.remove(p)

            def on_finished(code, _status, p=proc):
                emit(code == 0, bytes(p.readAllStandardOutput()).decode("utf-8", "replace"))

            def on_error(_err, p=proc):
                # FailedToStart etc. → surface as failure so the line never hangs.
                emit(False, f"picotool process error: {p.errorString()}")

            proc.finished.connect(on_finished)
            proc.errorOccurred.connect(on_error)
            self._procs.append(proc)
            proc.start("picotool", picotool_argv(args, ser))

        # ── operations (all async; result via `finished`) ────────────────────
        def reboot_to_bootsel(self, ser: str) -> None:
            self._run(["reboot", "-u", "-f"], ser, "reboot_bootsel")

        def reboot_to_app(self, ser: str) -> None:
            self._run(["reboot", "-a"], ser, "reboot_app")

        def program_block(self, tmp_bin_path: str, addr: int, ser: str) -> None:
            self._run(["load", tmp_bin_path, "-o", hex(addr)], ser, "program")

        def flash_image(self, work_uf2_path: str, ser: str) -> None:
            self._run(["load", "-x", work_uf2_path], ser, "flash")

except ImportError:  # PySide6 absent — the pure argv builder still imports
    Picotool = None  # type: ignore
