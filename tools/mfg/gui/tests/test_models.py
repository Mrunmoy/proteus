"""Domain model tests — pure, no Qt."""
import unittest

from domain.models import Phase, TestOutcome, TestState, Unit


class ModelTest(unittest.TestCase):
    def test_unit_all_passed_true_when_every_outcome_passes(self):
        u = Unit("PRZ-A-2624-00001-4", "70814E6F", "RP2350-PiZero", "GamePi13",
                 0x1209, 0x7201, 1, "A", 1_750_000_000,
                 outcomes=(TestOutcome("testLEDs", True),
                           TestOutcome("testLCD", True)))
        self.assertTrue(u.all_passed)

    def test_unit_all_passed_false_on_any_failure(self):
        u = Unit("s", "c", "m", "v", 1, 2, 1, "A", 0,
                 outcomes=(TestOutcome("a", True), TestOutcome("b", False, "no tone")))
        self.assertFalse(u.all_passed)

    def test_unit_all_passed_false_when_no_outcomes(self):
        self.assertFalse(Unit("s", "c", "m", "v", 1, 2, 1, "A", 0).all_passed)

    def test_enums_are_distinct(self):
        self.assertNotEqual(Phase.PROGRAM, Phase.SHIP)
        self.assertNotEqual(TestState.RUNNING, TestState.PASSED)


if __name__ == "__main__":
    unittest.main()
