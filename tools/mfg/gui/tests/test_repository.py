"""UnitRepository tests — in-memory SQLite, no Qt, no hardware."""
import unittest

from domain.models import RmaEvent, TestOutcome, Unit
from persistence.repository import UnitRepository


def _unit(serial, chip="70814E6F", plant="A", utc=1_750_000_000, outcomes=()):
    return Unit(serial, chip, "RP2350-PiZero", "GamePi13", 0x1209, 0x7201, 1,
                plant, utc, outcomes=outcomes)


class RepositoryTest(unittest.TestCase):
    def setUp(self):
        self.repo = UnitRepository(":memory:")

    def tearDown(self):
        self.repo.close()

    def test_next_seq_starts_at_one_and_increments_per_plant(self):
        self.assertEqual(self.repo.next_seq("A"), 1)
        self.repo.record(_unit("PRZ-A-2624-00001-4", plant="A"))
        self.assertEqual(self.repo.next_seq("A"), 2)
        self.assertEqual(self.repo.next_seq("B"), 1)  # per-plant counter

    def test_record_and_find_by_serial_round_trip_with_outcomes(self):
        outs = (TestOutcome("testLEDs", True, "lit"),
                TestOutcome("testAudio", True, "heard"))
        self.repo.record(_unit("PRZ-A-2624-00001-4", outcomes=outs))
        got = self.repo.find_by_serial("PRZ-A-2624-00001-4")
        self.assertIsNotNone(got)
        self.assertEqual(got.chip_uid, "70814E6F")
        self.assertEqual(got.vid, 0x1209)
        self.assertEqual(len(got.outcomes), 2)
        self.assertTrue(got.all_passed)

    def test_find_by_serial_missing_returns_none(self):
        self.assertIsNone(self.repo.find_by_serial("nope"))

    def test_find_by_chip_returns_all_serials_for_a_chip(self):
        self.repo.record(_unit("PRZ-A-2624-00001-4", chip="ABCD", utc=100))
        self.repo.record(_unit("PRZ-A-2624-00002-9", chip="ABCD", utc=200))
        units = self.repo.find_by_chip("ABCD")
        self.assertEqual({u.serial for u in units},
                         {"PRZ-A-2624-00001-4", "PRZ-A-2624-00002-9"})

    def test_rma_events_recorded_and_listed_for_lookup(self):
        self.repo.record(_unit("PRZ-A-2624-00001-4"))
        self.repo.add_rma_event(RmaEvent("PRZ-A-2624-00001-4", 1_760_000_000,
                                         "return", "LCD cracked"))
        events = self.repo.rma_events("PRZ-A-2624-00001-4")
        self.assertEqual(len(events), 1)
        self.assertEqual(events[0].kind, "return")

    def test_duplicate_serial_rejected(self):
        self.repo.record(_unit("PRZ-A-2624-00001-4"))
        with self.assertRaises(Exception):
            self.repo.record(_unit("PRZ-A-2624-00001-4"))


if __name__ == "__main__":
    unittest.main()
