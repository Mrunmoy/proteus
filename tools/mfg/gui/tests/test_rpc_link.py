"""RpcLink event-path smoke test — feeds bytes, asserts signals (offscreen Qt)."""
import unittest

from PySide6.QtWidgets import QApplication

from devices.frame import (
    FRAME_NOTIFY, FRAME_RESPONSE, M_SET_LED, build_frame)
from devices.rpc_link import RpcLink

_app = QApplication.instance() or QApplication([])


class RpcLinkTest(unittest.TestCase):
    def setUp(self):
        self.link = RpcLink()
        self.responses = []
        self.notifies = []
        # bytes(pl) coerces whether PySide6 hands us bytes or QByteArray.
        self.link.responseReceived.connect(
            lambda s, rc, pl: self.responses.append((s, rc, bytes(pl))))
        self.link.notifyReceived.connect(
            lambda m, pl: self.notifies.append((m, bytes(pl))))

    def test_dispatch_emits_response(self):
        self.link._dispatch(build_frame(FRAME_RESPONSE, M_SET_LED, 0x2000, b"\x01"))
        self.assertEqual(self.responses, [(0x2000, 0, b"\x01")])

    def test_dispatch_emits_notify(self):
        self.link._dispatch(build_frame(FRAME_NOTIFY, 99, 0, b"evt"))
        self.assertEqual(self.notifies, [(99, b"evt")])

    def test_dispatch_reassembles_split_bytes(self):
        raw = build_frame(FRAME_RESPONSE, 1, 7, b"xy")
        self.link._dispatch(raw[:5])
        self.assertEqual(self.responses, [])       # incomplete → nothing yet
        self.link._dispatch(raw[5:])
        self.assertEqual(self.responses, [(7, 0, b"xy")])

    def test_send_increments_seq(self):
        s1 = self.link.send(M_SET_LED, b"\x00")
        s2 = self.link.send(M_SET_LED, b"\x01")
        self.assertEqual(s2, s1 + 1)


if __name__ == "__main__":
    unittest.main()
