"""Proteus manufacturing Station — PySide6 GUI (event-driven, MVC, decoupled).

Layering (bottom→top), each layer ignorant of the ones above it:
  domain/       pure data + enums (no Qt, no I/O)
  persistence/  SQLite repository (no Qt)
  devices/      event-driven hardware adapters — QSerialPort/QProcess/pyudev (QObject)
  controller/   the factory state machine (QObject)
  view/         widgets → panels → tabs → window (pure presentation)
  app.py        composition root: the ONLY place signals↔slots are wired
"""
