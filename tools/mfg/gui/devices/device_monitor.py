"""DeviceMonitor — board attach / BOOTSEL / app re-enumeration / removal as Qt signals.

Event-driven via pyudev on a QSocketNotifier: the kernel writes a uevent to the
monitor's fd, the notifier fires, we drain the queued events. NO sysfs/lsusb polling.

Quirk handled here: udev ADD events carry the full identity (vid/pid/serial), but
REMOVE events carry NO serial (the device is gone). So we can't classify a removal by
serial. Instead we remember each device's sysfs path → uid on add, and match removals
by that path. The add-event classification is a PURE function (unit-tested); the
path-tracking lives in the Qt adapter (needs the live device objects).
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Optional

# ── pure classification (no Qt, no udev) — ADD/BIND events only ──────────────
_BOOTSEL_ID = ("2e8a", "000f")                  # RP2350 bootrom (USB BOOTSEL)
_APP_IDS = {("1209", "7201"), ("2e8a", "4001")}  # TestAgent: pid.codes + legacy
_PRODUCT_ID = ("2e8a", "0009")                  # DemoApp = a SHIPPED product (pico stdio)


@dataclass(frozen=True)
class DeviceEvent:
    kind: str   # 'bootsel' | 'app' | 'product'
    uid: str    # chip unique id (USB iSerial)


def classify_event(action: Optional[str], vid: Optional[str],
                   pid: Optional[str], serial: Optional[str]) -> Optional[DeviceEvent]:
    """Map an ADD/BIND udev usb_device event to a DeviceEvent, or None.

    Removals carry no serial in udev, so detach is NOT handled here — DeviceMonitor
    matches removals by sysfs path. Returns None for anything that isn't a known
    board appearing.
    """
    if not serial:
        return None
    a = (action or "").lower()
    if a not in ("add", "bind"):
        return None
    key = ((vid or "").lower(), (pid or "").lower())
    if key == _BOOTSEL_ID:
        return DeviceEvent("bootsel", serial)
    if key in _APP_IDS:
        return DeviceEvent("app", serial)
    if key == _PRODUCT_ID:
        return DeviceEvent("product", serial)
    return None


# ── Qt adapter ────────────────────────────────────────────────────────────────
try:
    import pyudev
    from PySide6.QtCore import QObject, QSocketNotifier, Signal

    class DeviceMonitor(QObject):
        enteredBootsel = Signal(str)   # uid
        enteredApp = Signal(str)       # uid
        productAttached = Signal(str)  # uid — a shipped product (DemoApp) appeared
        detached = Signal(str)         # uid

        def __init__(self, parent=None):
            super().__init__(parent)
            self._seen = {}            # sysfs device_path -> uid (to match removals)
            self._ctx = pyudev.Context()
            self._mon = pyudev.Monitor.from_netlink(self._ctx)
            self._mon.filter_by(subsystem="usb", device_type="usb_device")
            self._mon.start()
            self._notifier = QSocketNotifier(
                self._mon.fileno(), QSocketNotifier.Type.Read, self)
            self._notifier.activated.connect(self._drain)

        def scan_existing(self) -> None:
            """One-shot enumeration of already-attached boards at startup (not a poll).
            Emits the same signals as live events so the controller sees current state."""
            for dev in self._ctx.list_devices(subsystem="usb", DEVTYPE="usb_device"):
                ev = classify_event("add", dev.get("ID_VENDOR_ID"),
                                    dev.get("ID_MODEL_ID"), dev.get("ID_SERIAL_SHORT"))
                if ev is not None:
                    self._seen[dev.device_path] = ev.uid
                    self._emit(ev)

        def _drain(self) -> None:
            dev = self._mon.poll(timeout=0)
            while dev is not None:
                action = (dev.action or "").lower()
                path = dev.device_path
                if action in ("remove", "unbind"):
                    uid = self._seen.pop(path, None)   # removals lack a serial → match by path
                    if uid is not None:
                        self.detached.emit(uid)
                else:
                    ev = classify_event(dev.action, dev.get("ID_VENDOR_ID"),
                                        dev.get("ID_MODEL_ID"), dev.get("ID_SERIAL_SHORT"))
                    if ev is not None:
                        self._seen[path] = ev.uid
                        self._emit(ev)
                dev = self._mon.poll(timeout=0)

        def _emit(self, ev: "DeviceEvent") -> None:
            {"bootsel": self.enteredBootsel,
             "app": self.enteredApp,
             "product": self.productAttached}[ev.kind].emit(ev.uid)

except ImportError:  # pyudev/PySide6 absent (e.g. pure-logic unit host) — helpers still import
    DeviceMonitor = None  # type: ignore
