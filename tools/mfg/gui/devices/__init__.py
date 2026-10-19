"""Device adapters — event-driven QObjects (QSerialPort / QProcess / pyudev).

Each adapter turns an external event source into Qt signals; none of them poll or
block. The pure protocol logic (frame.py) and the pure helpers (udev classification,
picotool argv) live beside them with NO Qt dependency, so they're unit-testable.
"""
