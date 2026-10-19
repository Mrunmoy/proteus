"""UnitRepository — the manufacturing database (SQLite), for the line + RMA lookup.

PURE: depends only on the domain + stdlib sqlite3. No Qt. A thin QObject store in the
model layer can wrap this to emit change signals; keeping the repo itself Qt-free means
it is trivially unit-testable (see tests/test_repository.py) and reusable by the CLI.

Schema:
  units(serial PK, chip_uid, model, variant, vid, pid, hw_rev, plant, built_utc)
  test_results(serial, name, passed, detail)        — the verdicts that passed the unit
  rma_events(id, serial, event_utc, kind, note)      — post-sale history for RMA lookup
A chip_uid may map to several serials over its life (reprogram/reserialize); serial is
the unique unit key, so RMA lookup is by serial, with chip_uid for cross-reference.
"""
from __future__ import annotations

import sqlite3
from typing import List, Optional

from domain.models import RmaEvent, TestOutcome, Unit

_SCHEMA = """
CREATE TABLE IF NOT EXISTS units (
    serial    TEXT PRIMARY KEY,
    chip_uid  TEXT NOT NULL,
    model     TEXT NOT NULL,
    variant   TEXT NOT NULL,
    vid       INTEGER NOT NULL,
    pid       INTEGER NOT NULL,
    hw_rev    INTEGER NOT NULL,
    plant      TEXT NOT NULL,
    built_utc  INTEGER NOT NULL,
    fw_version TEXT NOT NULL DEFAULT ''
);
CREATE TABLE IF NOT EXISTS test_results (
    serial TEXT NOT NULL REFERENCES units(serial) ON DELETE CASCADE,
    name   TEXT NOT NULL,
    passed INTEGER NOT NULL,
    detail TEXT,
    PRIMARY KEY (serial, name)
);
CREATE TABLE IF NOT EXISTS rma_events (
    id        INTEGER PRIMARY KEY AUTOINCREMENT,
    serial    TEXT NOT NULL REFERENCES units(serial) ON DELETE CASCADE,
    event_utc INTEGER NOT NULL,
    kind      TEXT NOT NULL,
    note      TEXT
);
CREATE INDEX IF NOT EXISTS idx_units_chip ON units(chip_uid);
"""


class UnitRepository:
    """Single-file manufacturing DB. Use `:memory:` for tests."""

    def __init__(self, path: str = "tools/mfg/proteus_units.db"):
        self._conn = sqlite3.connect(path)
        self._conn.row_factory = sqlite3.Row
        self._conn.execute("PRAGMA foreign_keys = ON")
        self._conn.executescript(_SCHEMA)
        self._migrate()
        self._conn.commit()

    def _migrate(self) -> None:
        # Add columns that older DBs predate (CREATE TABLE IF NOT EXISTS won't alter them).
        cols = {r["name"] for r in self._conn.execute("PRAGMA table_info(units)")}
        if "fw_version" not in cols:
            self._conn.execute("ALTER TABLE units ADD COLUMN fw_version TEXT NOT NULL DEFAULT ''")

    # ── Line: minting + recording ───────────────────────────────────────────
    def next_seq(self, plant: str) -> int:
        """Next monotonic sequence for a plant (1-based) — drives the serial."""
        row = self._conn.execute(
            "SELECT COUNT(*) AS n FROM units WHERE plant = ?", (plant,)).fetchone()
        return int(row["n"]) + 1

    def record(self, unit: Unit) -> None:
        """Insert a passed unit + its outcomes in one transaction."""
        with self._conn:  # transaction
            self._conn.execute(
                "INSERT INTO units(serial, chip_uid, model, variant, vid, pid, "
                "hw_rev, plant, built_utc, fw_version) VALUES (?,?,?,?,?,?,?,?,?,?)",
                (unit.serial, unit.chip_uid, unit.model, unit.variant, unit.vid,
                 unit.pid, unit.hw_rev, unit.plant, unit.built_utc, unit.fw_version))
            self._conn.executemany(
                "INSERT INTO test_results(serial, name, passed, detail) VALUES (?,?,?,?)",
                [(unit.serial, o.name, 1 if o.passed else 0, o.detail)
                 for o in unit.outcomes])

    # ── RMA lookup ───────────────────────────────────────────────────────────
    def find_by_serial(self, serial: str) -> Optional[Unit]:
        row = self._conn.execute(
            "SELECT * FROM units WHERE serial = ?", (serial,)).fetchone()
        return self._row_to_unit(row) if row else None

    def find_by_chip(self, chip_uid: str) -> List[Unit]:
        rows = self._conn.execute(
            "SELECT * FROM units WHERE chip_uid = ? ORDER BY built_utc DESC",
            (chip_uid,)).fetchall()
        return [self._row_to_unit(r) for r in rows]

    def add_rma_event(self, event: RmaEvent) -> None:
        with self._conn:
            self._conn.execute(
                "INSERT INTO rma_events(serial, event_utc, kind, note) VALUES (?,?,?,?)",
                (event.serial, event.event_utc, event.kind, event.note))

    def rma_events(self, serial: str) -> List[RmaEvent]:
        rows = self._conn.execute(
            "SELECT * FROM rma_events WHERE serial = ? ORDER BY event_utc",
            (serial,)).fetchall()
        return [RmaEvent(r["serial"], r["event_utc"], r["kind"], r["note"] or "")
                for r in rows]

    def count(self) -> int:
        return int(self._conn.execute("SELECT COUNT(*) AS n FROM units")
                   .fetchone()["n"])

    def close(self) -> None:
        self._conn.close()

    # ── internal ─────────────────────────────────────────────────────────────
    def _row_to_unit(self, row: sqlite3.Row) -> Unit:
        outs = self._conn.execute(
            "SELECT name, passed, detail FROM test_results WHERE serial = ?",
            (row["serial"],)).fetchall()
        outcomes = tuple(
            TestOutcome(o["name"], bool(o["passed"]), o["detail"] or "") for o in outs)
        return Unit(
            serial=row["serial"], chip_uid=row["chip_uid"], model=row["model"],
            variant=row["variant"], vid=row["vid"], pid=row["pid"],
            hw_rev=row["hw_rev"], plant=row["plant"], built_utc=row["built_utc"],
            fw_version=row["fw_version"] if "fw_version" in row.keys() else "",
            outcomes=outcomes)
