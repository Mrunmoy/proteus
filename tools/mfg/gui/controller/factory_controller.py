"""FactoryController — drives PROGRAM→TEST→SERIALIZE→SHIP, event-driven.

Written as a generator-coroutine: the line logic reads top-to-bottom and sequential,
but every `yield` SUSPENDS until the relevant signal fires (BOOTSEL seen, RPC reply,
picotool finished, operator answered, or a one-shot QTimer). There is no blocking and
no polling — `_advance()` resumes the generator, and each adapter signal handler resumes
it only when the awaited token matches.

Decoupled: it talks to the adapters through their public methods/signals and to the
model through the repository. It knows nothing about widgets — it only emits signals.
"""
from __future__ import annotations

import struct
import time
from dataclasses import dataclass
from typing import Optional

from PySide6.QtCore import QObject, QTimer, Signal

import device_info as dinfo
import device_types as dt
import personalization as pers
from devices.frame import BUTTON_EVENT_MSG, decode_button_event, decode_device_info
from devices.ports import find_debug_port, find_rpc_port, pulse_1200
from domain.models import Phase, RmaEvent, TestOutcome, TestState, Unit

_BUTTONS = [("Up", 0), ("Down", 1), ("Left", 2), ("Right", 3),
            ("A", 4), ("B", 5), ("X", 6), ("Y", 7),
            ("L", 8), ("R", 9), ("Start", 10), ("Select", 11)]
_LCD = [(1, "RED"), (2, "GREEN"), (3, "BLUE"), (4, "WHITE")]
_LCD_BLACK = 0
_LCD_RULER = 5   # fine vertical colour ruler — reveals a horizontal shift/tear
_ALL_TESTS = {"testLCD", "testTearing", "testButtons", "testAudio", "testSD", "testHDMI"}


# ── await tokens ─────────────────────────────────────────────────────────────
@dataclass
class _Bootsel: uid: str
@dataclass
class _App: uid: str
@dataclass
class _Rpc: seq: int
@dataclass
class _Pico: pass
@dataclass
class _Operator: question: str
@dataclass
class _Delay: ms: int
@dataclass
class _Buttons: pass        # resolved by button-press notifications (operator-paced)
@dataclass
class _Product: uid: str    # wait for the shipped product (DemoApp) to re-enumerate

# Watchdog ceilings (ms) per await type — operator waits are intentionally untimed.
_TIMEOUTS = {_Rpc: 8000, _Pico: 40000, _Bootsel: 20000, _App: 25000, _Product: 25000}


def _read_device_fw(uid: str) -> str:
    """The unit's CURRENT firmware version, from its USB bcdDevice (no RPC needed).
    The firmware encodes its version there; lets us spot a field-updated unit vs the
    version recorded at manufacture. Returns '' if not found."""
    import glob
    for d in glob.glob("/sys/bus/usb/devices/*"):
        try:
            with open(f"{d}/serial") as f:
                if f.read().strip() != uid:
                    continue
            with open(f"{d}/bcdDevice") as f:
                raw = f.read().strip()          # e.g. "0100" → "1.0"
            return f"{int(raw[:2])}.{int(raw[2:])}" if len(raw) >= 4 else raw
        except (OSError, ValueError):
            continue
    return ""


class FactoryController(QObject):
    # ── signals to the View ──
    phaseChanged = Signal(object)          # Phase
    boardReady = Signal(str)               # uid present in app mode, idle
    boardLost = Signal()
    identityRead = Signal(dict)            # device-info dict after PROGRAM
    testStateChanged = Signal(str, object)  # name, TestState
    operatorPrompt = Signal(str)           # question ("" clears)
    serialAssigned = Signal(str)
    unitRejected = Signal(str)             # reason
    lineComplete = Signal(str)             # serial
    alreadyManufactured = Signal(dict)     # a reconnected unit already in the DB
    logged = Signal(str)

    def __init__(self, repo, rpc, monitor, picotool, dtype, plant="A", parent=None):
        super().__init__(parent)
        self._repo = repo
        self._rpc = rpc
        self._mon = monitor
        self._pico = picotool
        self._dt = dtype
        self._plant = plant
        self._gen = None
        self._await = None
        self._uid: Optional[str] = None
        self._mode: Optional[str] = None    # current board firmware: 'app' | 'product'
        self._shipped_uid: Optional[str] = None  # the unit we just shipped (post-ship re-enum)
        self._busy = False
        self._pressed = set()      # button indices seen during the button test
        self._recipient = ""       # per-board gift name, written into the splash at SHIP
        self._selected = set(_ALL_TESTS)   # tests to run this line (operator's ticks)

        # Watchdog: a device that stops responding (e.g. loses power) must surface a
        # clean error, never freeze the line. Armed per non-operator await.
        self._watchdog = QTimer(self)
        self._watchdog.setSingleShot(True)
        self._watchdog.timeout.connect(self._on_timeout)

        monitor.enteredBootsel.connect(self._on_bootsel)
        monitor.enteredApp.connect(self._on_app)
        monitor.productAttached.connect(self._on_product)
        monitor.detached.connect(self._on_detached)
        rpc.responseReceived.connect(self._on_rpc)
        rpc.notifyReceived.connect(self._on_notify)
        picotool.finished.connect(self._on_pico)

    # ── view intents ─────────────────────────────────────────────────────────
    def set_recipient(self, name: str) -> None:
        """The gift recipient for the board currently on the line (written at SHIP)."""
        self._recipient = (name or "").strip()

    def start(self, selected=None) -> None:
        if self._busy or not self._uid:
            return
        # The operator's ticked tests (unticked ones are skipped). Empty/None = the
        # full suite. A unit only serializes if every test it RAN passed.
        self._selected = set(selected) if selected else set(_ALL_TESTS)
        self._busy = True
        self._gen = self._line(self._uid)
        self._advance(None)

    def operator_answer(self, yes: bool) -> None:
        if isinstance(self._await, _Operator):
            self.operatorPrompt.emit("")
            self._advance(yes)
        elif isinstance(self._await, _Buttons) and not yes:
            self.operatorPrompt.emit("")
            self._advance(False)        # operator flags a button that won't register

    def rerun(self, keep_serial: bool, selected=None) -> None:
        """Re-manufacture an already-recorded board (keep its serial or mint a new one).
        Runs the operator's ticked tests (empty/None = the full suite)."""
        if self._busy or not self._uid:
            return
        serial = None
        if keep_serial:
            units = self._repo.find_by_chip(self._uid)
            serial = units[0].serial if units else None
        self._selected = set(selected) if selected else set(_ALL_TESTS)
        self._busy = True
        self._gen = self._line(self._uid, keep_serial=serial)
        self._advance(None)

    def dismiss(self) -> None:
        pass                            # operator declined the re-run; leave the unit as-is

    def rma_test(self, selected) -> None:
        """RMA: record the unit as RMA'd in the DB, then run ONLY the selected tests.
        No device-info rewrite, no re-serialize — a pure diagnostic."""
        if self._busy or not self._uid:
            return
        units = self._repo.find_by_chip(self._uid)
        serial = units[0].serial if units else None
        if serial:
            self._repo.add_rma_event(RmaEvent(serial, int(time.time()), "rma",
                                              "RMA intake (operator)"))
            self.logged.emit(f"Recorded RMA for {serial}")
        self._busy = True
        self._gen = self._line_test_only(self._uid, set(selected or []))
        self._advance(None)

    # ── coroutine driver ──────────────────────────────────────────────────────
    def _advance(self, value) -> None:
        if self._gen is None:
            return
        self._watchdog.stop()
        try:
            token = self._gen.send(value)
        except StopIteration:
            self._gen = None
            self._await = None
            self._busy = False
            return
        self._await = token
        if isinstance(token, _Delay):
            QTimer.singleShot(token.ms, lambda t=token: self._resume_if(t, None))
        elif isinstance(token, _Operator):
            self.operatorPrompt.emit(token.question)   # operator-paced: no timeout
        else:
            ms = _TIMEOUTS.get(type(token))
            if ms:
                self._watchdog.start(ms)
        # _Bootsel/_App/_Rpc/_Pico are resumed by their signal handlers (or watchdog).

    def _on_timeout(self) -> None:
        if self._gen is None or self._await is None:
            return
        kind = type(self._await).__name__
        self._gen = None
        self._await = None
        self._busy = False
        self._rpc.close()
        self.logged.emit(f"TIMEOUT waiting for {kind} — device stopped responding.")
        self.unitRejected.emit("device stopped responding (timeout) — check USB/power")
        self.phaseChanged.emit(Phase.REJECTED)

    def _resume_if(self, token, value) -> None:
        if self._await is token:
            self._advance(value)

    # ── adapter signal handlers (resume only on the awaited token) ────────────
    def _on_bootsel(self, uid: str) -> None:
        if isinstance(self._await, _Bootsel) and self._await.uid == uid:
            self._advance(None)

    def _on_app(self, uid: str) -> None:
        if isinstance(self._await, _App) and self._await.uid == uid:
            self._advance(None)
        elif not self._busy and uid != self._uid:   # dedupe udev add+bind doubles
            self._board_detected(uid, "app")

    def _on_product(self, uid: str) -> None:
        # During SHIP the line waits for the product to come up (keeps busy=True across
        # the whole reboot, so the re-enum isn't mistaken for a reconnect).
        if isinstance(self._await, _Product) and self._await.uid == uid:
            self._advance(None)
            return
        # Otherwise: ignore the just-shipped unit / the current board; a genuine
        # reconnect of a known board → "already made".
        if self._busy or uid == self._shipped_uid or uid == self._uid:
            return
        self._board_detected(uid, "product")

    def _board_detected(self, uid: str, mode: str) -> None:
        self._uid = uid
        self._mode = mode
        units = self._repo.find_by_chip(uid)
        if units:
            u = units[0]               # most recent record for this chip
            self.alreadyManufactured.emit({
                "serial": u.serial, "chip_uid": u.chip_uid, "model": u.model,
                "variant": u.variant, "built_utc": u.built_utc,
                "passed": u.all_passed, "is_product": mode == "product",
                "shipped_fw": u.fw_version, "current_fw": _read_device_fw(uid),
            })
        else:
            self.boardReady.emit(uid)

    def _on_detached(self, uid: str) -> None:
        if self._busy:
            return
        if uid == self._shipped_uid:
            self._shipped_uid = None   # the shipped unit was removed → ready for the next
        if uid == self._uid:
            self._uid = None
            self._mode = None
            self.boardLost.emit()

    def _on_rpc(self, seq: int, rc: int, payload: bytes) -> None:
        if isinstance(self._await, _Rpc) and self._await.seq == seq:
            self._advance((rc, bytes(payload)))

    def _on_notify(self, message_id: int, payload: bytes) -> None:
        # Button-press notifications drive the (operator-paced) button test.
        if message_id != BUTTON_EVENT_MSG or not isinstance(self._await, _Buttons):
            return
        button, kind = decode_button_event(bytes(payload))
        if kind != 0 or button is None:        # 0 = Down/press; ignore releases
            return
        if button not in self._pressed:
            self._pressed.add(button)
            if len(self._pressed) >= len(_BUTTONS):
                self.operatorPrompt.emit("")
                self._advance(True)
            else:
                self.operatorPrompt.emit(self._button_prompt())

    def _on_pico(self, ok: bool, _out: str, _tag: str) -> None:
        if isinstance(self._await, _Pico):
            self._advance(ok)

    # ── the line ──────────────────────────────────────────────────────────────
    def _line(self, uid: str, keep_serial=None):
        # Re-run of a shipped product: put the test firmware back on first.
        if self._mode == "product":
            self.phaseChanged.emit(Phase.PROGRAM)
            self.logged.emit("Re-flashing test firmware onto the product…")
            if not (yield from self._reflash_testagent(uid)):
                return self._reject("could not reflash test firmware")

        # PHASE A — PROGRAM
        self.phaseChanged.emit(Phase.PROGRAM)
        self.logged.emit("Programming identity block…")
        yield from self._enter_bootsel(uid)
        block = dinfo.build(self._dt.model, vid=self._dt.vid, pid=self._dt.pid,
                            hw_rev=self._dt.hw_rev, variant=self._dt.variant, serial="")
        if not (yield from self._write_block(uid, block)):
            return self._reject("programming write failed")
        if not (yield from self._reboot_to_app(uid)):
            return self._reject("board did not come back after programming")
        info = yield from self._get_device_info()
        if not info or not info["present"] or info["model"] != self._dt.model:
            return self._reject("identity read-back failed")
        self.identityRead.emit(info)
        self.logged.emit(f"Identity OK: {info['model']} / {info['variant']} "
                         f"{info['vid']:#06x}:{info['pid']:#06x}")

        # PHASE B — TEST (every test the operator selected must pass to serialize)
        self.phaseChanged.emit(Phase.TEST)
        outcomes = yield from self._run_tests(self._selected)
        if not all(o.passed for o in outcomes):
            failed = next(o for o in outcomes if not o.passed)
            return self._reject(f"test failed: {failed.name} ({failed.detail})")

        # PHASE C — SERIALIZE
        self.phaseChanged.emit(Phase.SERIALIZE)
        utc = int(time.time())
        if keep_serial:
            serial = keep_serial
            self.logged.emit(f"Keeping serial {serial}")
        else:
            serial = dt.make_serial(self._plant, self._repo.next_seq(self._plant), when_utc=utc)
            self.logged.emit(f"Minting {serial}")
        yield from self._enter_bootsel(uid)
        block = dinfo.build(self._dt.model, vid=self._dt.vid, pid=self._dt.pid,
                            hw_rev=self._dt.hw_rev, variant=self._dt.variant,
                            serial=serial, serial_utc=utc)
        if not (yield from self._write_block(uid, block)):
            return self._reject("serialize write failed")
        if not (yield from self._reboot_to_app(uid)):
            return self._reject("board did not come back after serialize")
        info = yield from self._get_device_info()
        if not info or info.get("serial") != serial or not dt.serial_is_valid(serial):
            return self._reject("serial read-back mismatch")
        if keep_serial:
            self._repo.add_rma_event(RmaEvent(serial, utc, "re-test", "re-run passed"))
        else:
            unit = Unit(serial, uid, self._dt.model, self._dt.variant, self._dt.vid,
                        self._dt.pid, self._dt.hw_rev, self._plant, utc,
                        fw_version=self._dt.fw_version, outcomes=tuple(outcomes))
            self._repo.record(unit)
        self.serialAssigned.emit(serial)
        self.logged.emit(f"Recorded {serial} ↔ chipUID {uid}")

        # PHASE D — SHIP
        self.phaseChanged.emit(Phase.SHIP)
        yield from self._enter_bootsel(uid)
        # Write the gift name into its own flash sector while we're in BOOTSEL, before
        # the product image. It survives this (and any future) firmware flash.
        if self._recipient:
            if not (yield from self._write_recipient(uid, self._recipient)):
                return self._reject("recipient name write failed")
            self.logged.emit(f"Personalized for {self._recipient}")
        self._pico.flash_image(f"/work/{self._dt.product_fw}", uid)
        if not (yield _Pico()):
            return self._reject("ship flash failed")
        yield _Product(uid)              # wait for the product to re-enumerate (whole
                                          # reboot stays busy → not seen as a reconnect)
        self._shipped_uid = uid
        self._mode = "product"
        self.logged.emit(f"Shipped — flashed {self._dt.product_fw}")
        self.phaseChanged.emit(Phase.COMPLETE)
        self.lineComplete.emit(serial)

    def _line_test_only(self, uid, selected):
        # RMA diagnostic — run only the selected tests. NO device-info write, NO serialize,
        # NO ship. The unit's identity/serial is left exactly as it was.
        self.phaseChanged.emit(Phase.TEST)
        if self._mode == "product":
            self.logged.emit("RMA: re-flashing test firmware (device-info/serial untouched)…")
            if not (yield from self._reflash_testagent(uid)):
                return self._reject("could not reflash test firmware")
        else:
            port = find_rpc_port(uid)
            if not port or not self._rpc.open(port):
                return self._reject("could not open RPC link")
            yield _Delay(200)
        info = yield from self._get_device_info()
        self.identityRead.emit(info or {})
        serial = (info or {}).get("serial", "")
        outcomes = yield from self._run_tests(selected)
        if not outcomes:
            self.logged.emit("RMA recorded — no tests selected.")
            self.phaseChanged.emit(Phase.COMPLETE)
            return
        passed = all(o.passed for o in outcomes)
        summary = ", ".join(f"{o.name}={'PASS' if o.passed else 'FAIL'}" for o in outcomes)
        if serial:
            self._repo.add_rma_event(RmaEvent(serial, int(time.time()), "diagnostic", summary))
        self.logged.emit(f"RMA diagnostic: {summary}")
        self.phaseChanged.emit(Phase.COMPLETE if passed else Phase.REJECTED)

    # ── line helpers (sub-coroutines) ─────────────────────────────────────────
    def _enter_bootsel(self, uid):
        self._rpc.close()                 # release if02 before the board drops off USB
        dbg = find_debug_port(uid)
        if dbg:
            pulse_1200(dbg)
        yield _Bootsel(uid)

    def _write_block(self, uid, block):
        ok = True
        for which, off in (("A", 0), ("B", dinfo.SECTOR_SIZE)):
            path = f"/tmp/proteus_devinfo_{which}.bin"
            with open(path, "wb") as f:
                f.write(block)
            self._pico.program_block(path, dinfo.XIP_ADDR + off, uid)
            ok = (yield _Pico()) and ok
        return ok

    def _write_recipient(self, uid, name):
        path = "/tmp/proteus_recipient.bin"
        with open(path, "wb") as f:
            f.write(pers.build(name))
        self._pico.program_block(path, pers.XIP_ADDR, uid)
        return (yield _Pico())

    def _reboot_to_app(self, uid):
        self._pico.reboot_to_app(uid)
        yield _Pico()
        yield _App(uid)
        yield _Delay(400)            # let the CDC settle
        port = find_rpc_port(uid)
        if not port or not self._rpc.open(port):
            return False
        self._mode = "app"
        yield _Delay(200)
        return True

    def _reflash_testagent(self, uid):
        # A shipped product runs DemoApp (pico stdio) — reboot it to BOOTSEL, put the
        # test firmware back, and come up as the TestAgent so the line can run.
        self._rpc.close()
        self._pico.reboot_to_bootsel(uid)
        if not (yield _Pico()):
            return False
        yield _Bootsel(uid)
        self._pico.flash_image("/work/build/proteus_mfg.uf2", uid)
        if not (yield _Pico()):
            return False
        yield _App(uid)
        yield _Delay(400)
        port = find_rpc_port(uid)
        if not port or not self._rpc.open(port):
            return False
        self._mode = "app"
        yield _Delay(200)
        return True

    def _get_device_info(self):
        seq = self._rpc.get_device_info()
        rc, payload = yield _Rpc(seq)
        if rc != 0:
            return None
        return decode_device_info(payload)

    # ── tests (PC owns the verdict; device returns raw) ───────────────────────
    def _run_tests(self, selected):
        # `selected` = the tests to run; the rest are marked SKIPPED. A full manufacture
        # passes _ALL_TESTS; an RMA run passes the operator's checked subset.
        outcomes = []
        # HDMI runs last: its driver raises clk_sys + claims core1, so keep it after
        # the SPI/PWM/SD tests. SERIALIZE reboots the unit straight after, anyway.
        for name, fn in (("testLCD", self._test_lcd), ("testTearing", self._test_tearing),
                         ("testButtons", self._test_buttons),
                         ("testAudio", self._test_audio),
                         ("testSD", self._test_sd), ("testHDMI", self._test_hdmi)):
            if name in selected:
                outcomes.append((yield from fn()))
            else:
                self._state(name, TestState.SKIPPED)
        return outcomes

    def _state(self, name, state):
        self.testStateChanged.emit(name, state)

    def _test_lcd(self):
        name = "testLCD"
        self._state(name, TestState.RUNNING)
        for code, label in _LCD:
            yield _Rpc(self._rpc.lcd_show_pattern(code))
            self._state(name, TestState.AWAITING_OPERATOR)
            ok = yield _Operator(f"Is the LCD solid {label}?")
            if not ok:
                yield _Rpc(self._rpc.lcd_show_pattern(_LCD_BLACK))
                self._state(name, TestState.FAILED)
                return TestOutcome(name, False, f"operator: {label} wrong")
        yield _Rpc(self._rpc.lcd_show_pattern(_LCD_BLACK))
        self._state(name, TestState.PASSED)
        return TestOutcome(name, True, "all colours confirmed")

    def _test_tearing(self):
        # Shift/tear stress test. The RGB test proves the panel lights up; this proves
        # the LCD signalling survives button activity. A marginal LCD solder joint lets
        # a button's switching edge couple into DC/SCK and desync the SPI pixel stream,
        # sliding the image sideways — invisible on a solid colour, obvious on the fine
        # vertical ruler. So we show the ruler and have the operator agitate the buttons.
        name = "testTearing"
        self._state(name, TestState.RUNNING)
        yield _Rpc(self._rpc.lcd_show_pattern(_LCD_RULER))
        self._state(name, TestState.AWAITING_OPERATOR)
        stable = yield _Operator("Press and hold buttons at random for a few seconds. "
                                 "Did the ruler stay perfectly still (no shift or tearing)?")
        yield _Rpc(self._rpc.lcd_show_pattern(_LCD_BLACK))
        if not stable:
            self._state(name, TestState.FAILED)
            return TestOutcome(name, False, "operator: image shifted/tore under button stress")
        self._state(name, TestState.PASSED)
        return TestOutcome(name, True, "stable under button stress")

    def _test_buttons(self):
        # Event-driven: the device PUSHES a notify per button edge. The operator just
        # presses each button (any order); the row tracks N/12 live and auto-completes.
        # No polling, no press-and-click. _on_notify resolves the _Buttons await.
        name = "testButtons"
        self._state(name, TestState.RUNNING)
        self._pressed = set()
        self._state(name, TestState.AWAITING_OPERATOR)
        self.operatorPrompt.emit(self._button_prompt())
        ok = yield _Buttons()
        self.operatorPrompt.emit("")
        if not ok:
            missing = [lbl for lbl, bit in _BUTTONS if bit not in self._pressed]
            self._state(name, TestState.FAILED)
            return TestOutcome(name, False, "not seen: " + ", ".join(missing))
        self._state(name, TestState.PASSED)
        return TestOutcome(name, True, "all 12 buttons pressed")

    def _button_prompt(self) -> str:
        return (f"Press each of the 12 buttons — {len(self._pressed)}/12 done. "
                f"(Click No if a button won't register.)")

    def _test_audio(self):
        name = "testAudio"
        self._state(name, TestState.RUNNING)
        yield _Rpc(self._rpc.play_tone(440, 400))
        self._state(name, TestState.AWAITING_OPERATOR)
        heard = yield _Operator("Did you hear a ~440 Hz tone?")
        self._state(name, TestState.PASSED if heard else TestState.FAILED)
        return TestOutcome(name, heard, "operator heard" if heard else "operator: silent")

    def _test_sd(self):
        # Mount the microSD over the real SDIO driver and read its root directory.
        name = "testSD"
        self._state(name, TestState.RUNNING)
        self._state(name, TestState.AWAITING_OPERATOR)
        inserted = yield _Operator("Insert a microSD card, then click Yes.")
        if not inserted:
            self._state(name, TestState.FAILED)
            return TestOutcome(name, False, "operator: no card inserted")
        rc, pl = yield _Rpc(self._rpc.sd_probe())
        present = pl[0] if rc == 0 and len(pl) >= 1 else 0
        files = struct.unpack_from("<I", pl, 1)[0] if rc == 0 and len(pl) >= 5 else 0
        passed = rc == 0 and present == 1
        self._state(name, TestState.PASSED if passed else TestState.FAILED)
        return TestOutcome(name, passed, f"mounted={present} files={files}")

    def _test_hdmi(self):
        # Bring DVI/HDMI up on the device and show colour bars; operator confirms.
        name = "testHDMI"
        self._state(name, TestState.RUNNING)
        rc, pl = yield _Rpc(self._rpc.hdmi_show_pattern(0))
        shown = pl[0] if rc == 0 and pl else 0
        if rc != 0 or not shown:
            self._state(name, TestState.FAILED)
            return TestOutcome(name, False, "DVI driver did not start")
        self._state(name, TestState.AWAITING_OPERATOR)
        ok = yield _Operator("Connect an HDMI monitor to the unit — do you see colour bars?")
        self._state(name, TestState.PASSED if ok else TestState.FAILED)
        return TestOutcome(name, ok, "operator saw bars" if ok else "operator: no bars")

    # ── reject ─────────────────────────────────────────────────────────────────
    def _reject(self, reason: str) -> None:
        self.logged.emit(f"REJECTED: {reason}")
        self.unitRejected.emit(reason)
        self.phaseChanged.emit(Phase.REJECTED)
