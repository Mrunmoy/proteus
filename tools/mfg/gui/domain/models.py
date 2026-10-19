"""Domain models — the vocabulary the whole app speaks, with zero dependencies.

Deliberately pure (no Qt, no sqlite, no serial): the controller, the views and the
repository all import these, but this module imports nothing of theirs. That one-way
dependency is what keeps the layers decoupled.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum, auto
from typing import Optional, Tuple


class Phase(Enum):
    """Where a unit is on the factory line."""
    IDLE = auto()
    PROGRAM = auto()
    TEST = auto()
    SERIALIZE = auto()
    SHIP = auto()
    COMPLETE = auto()
    REJECTED = auto()


class TestState(Enum):
    """Live state of a single test row in the grid."""
    PENDING = auto()
    RUNNING = auto()
    AWAITING_OPERATOR = auto()
    PASSED = auto()
    FAILED = auto()
    SKIPPED = auto()


@dataclass(frozen=True)
class TestOutcome:
    """The verdict the PC reached for one test (the device never judges)."""
    name: str
    passed: bool
    detail: str = ""


@dataclass(frozen=True)
class Identity:
    """The device-type identity programmed into a unit (no serial)."""
    model: str
    variant: str
    vid: int
    pid: int
    hw_rev: int


@dataclass(frozen=True)
class Unit:
    """A fully manufactured + serialized unit — one row in the database."""
    serial: str
    chip_uid: str
    model: str
    variant: str
    vid: int
    pid: int
    hw_rev: int
    plant: str
    built_utc: int
    fw_version: str = ""
    outcomes: Tuple[TestOutcome, ...] = field(default_factory=tuple)

    @property
    def all_passed(self) -> bool:
        return bool(self.outcomes) and all(o.passed for o in self.outcomes)


@dataclass(frozen=True)
class RmaEvent:
    """A post-sale event recorded against a unit (returns / repairs / notes)."""
    serial: str
    event_utc: int
    kind: str
    note: str = ""
