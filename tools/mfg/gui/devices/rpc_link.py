"""RpcLink — the aether RPC over CDC, event-driven via QtSerialPort.

NO blocking reads, NO polling: bytes arrive on QSerialPort.readyRead, get fed to the
incremental FrameParser, and complete frames are re-emitted as Qt signals. send() is
fire-and-forget and returns the request seq; the controller correlates responses by seq.
"""
from __future__ import annotations

from PySide6.QtCore import QIODeviceBase, QObject, Signal
from PySide6.QtSerialPort import QSerialPort

from devices.frame import (
    FRAME_REQUEST, M_GET_DEVICE_INFO, M_GET_IDENTITY, M_HDMI_SHOW_PATTERN,
    M_LCD_SHOW_PATTERN, M_PLAY_TONE, M_SCAN_BUTTONS, M_SD_PROBE, M_SET_LED,
    FrameParser, build_frame)


class RpcLink(QObject):
    # seq, rc(signed), payload
    responseReceived = Signal(int, int, bytes)
    # method, payload  (device-initiated, e.g. a future button-change push)
    notifyReceived = Signal(int, bytes)
    opened = Signal()
    closed = Signal()
    errorOccurred = Signal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._port = QSerialPort(self)
        self._parser = FrameParser()
        self._seq = 0x1000
        self._port.readyRead.connect(self._on_ready_read)
        self._port.errorOccurred.connect(self._on_port_error)

    # ── connection ───────────────────────────────────────────────────────────
    def open(self, port_path: str, baud: int = 115200) -> bool:
        if self._port.isOpen():
            self._port.close()
        self._port.setPortName(port_path)
        self._port.setBaudRate(baud)
        self._parser = FrameParser()
        if self._port.open(QIODeviceBase.OpenModeFlag.ReadWrite):
            self.opened.emit()
            return True
        self.errorOccurred.emit(self._port.errorString())
        return False

    def close(self) -> None:
        if self._port.isOpen():
            self._port.close()
            self.closed.emit()

    def is_open(self) -> bool:
        return self._port.isOpen()

    # ── requests (return the seq; response arrives via responseReceived) ──────
    def send(self, method: int, payload: bytes = b"") -> int:
        self._seq += 1
        self._port.write(build_frame(FRAME_REQUEST, method, self._seq, payload))
        return self._seq

    def get_identity(self) -> int:
        return self.send(M_GET_IDENTITY)

    def get_device_info(self) -> int:
        return self.send(M_GET_DEVICE_INFO)

    def set_led(self, on: bool) -> int:
        return self.send(M_SET_LED, bytes([1 if on else 0]))

    def lcd_show_pattern(self, pattern: int) -> int:
        return self.send(M_LCD_SHOW_PATTERN, bytes([pattern & 0xFF]))

    def scan_buttons(self) -> int:
        return self.send(M_SCAN_BUTTONS)

    def play_tone(self, freq_hz: int, ms: int) -> int:
        import struct
        return self.send(M_PLAY_TONE, struct.pack("<II", freq_hz, ms))

    def sd_probe(self) -> int:
        return self.send(M_SD_PROBE)

    def hdmi_show_pattern(self, pattern: int = 0) -> int:
        return self.send(M_HDMI_SHOW_PATTERN, bytes([pattern & 0xFF]))

    # ── inbound (event-driven) ────────────────────────────────────────────────
    def _on_ready_read(self) -> None:
        self._dispatch(bytes(self._port.readAll()))

    def _dispatch(self, data: bytes) -> None:
        """Pure-ish: feed bytes, emit a signal per complete frame. Unit-testable."""
        for frame in self._parser.feed(data):
            if frame.is_notify:
                self.notifyReceived.emit(frame.method, frame.payload)
            elif frame.is_response:
                self.responseReceived.emit(frame.seq, frame.rc, frame.payload)

    def _on_port_error(self, err) -> None:
        if err != QSerialPort.SerialPortError.NoError and self._port.isOpen():
            self.errorOccurred.emit(self._port.errorString())
