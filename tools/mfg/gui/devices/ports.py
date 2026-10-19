"""Serial-port resolution + the BOOTSEL touch — one-shot actions, not polling.

The by-id paths carry the chip uid; we glob them once when an event tells us the
board is present (the DeviceMonitor fires first, so the symlink already exists).
"""
from __future__ import annotations

import glob
from typing import Optional


def find_rpc_port(uid: str) -> Optional[str]:
    """The TestAgent binary-RPC CDC (interface 02)."""
    hits = glob.glob(f"/dev/serial/by-id/*TestAgent_{uid}-if02*")
    return hits[0] if hits else None


def find_debug_port(uid: str) -> Optional[str]:
    """The TestAgent debug CDC (interface 00) — the 1200-baud reset rides this."""
    hits = glob.glob(f"/dev/serial/by-id/*TestAgent_{uid}-if00*")
    return hits[0] if hits else None


try:
    from PySide6.QtCore import QIODeviceBase
    from PySide6.QtSerialPort import QSerialPort

    def pulse_1200(port_path: str) -> bool:
        """Open the debug port at 1200 baud and close — the firmware's
        tud_cdc_line_coding_cb sees 1200 and calls reset_usb_boot → BOOTSEL.
        One-shot; the resulting transition is observed via DeviceMonitor."""
        port = QSerialPort()
        port.setPortName(port_path)
        port.setBaudRate(1200)
        if port.open(QIODeviceBase.OpenModeFlag.ReadWrite):
            port.close()
            return True
        return False

except ImportError:  # PySide6 absent on a pure-logic host
    def pulse_1200(port_path: str) -> bool:  # type: ignore
        raise RuntimeError("PySide6 required for pulse_1200")
