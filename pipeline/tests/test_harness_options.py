from __future__ import annotations

import unittest

from elysium_pipeline.unreal import _take_options


class HarnessOptionTests(unittest.TestCase):
    """A switch left in the positional list becomes an argument.

    This is the whole reason `_take_options` exists: `gr <stem> --drive` would otherwise reach the
    green room as `-GreenRoomClip=--drive` and stand a clip that does not exist, with the drive mode
    never armed and nothing saying so.
    """

    def test_drive_is_taken_out_of_the_positional_list(self) -> None:
        positional, options = _take_options(["tremere_male_armor_0", "--drive"])
        self.assertEqual(positional, ["tremere_male_armor_0"])
        self.assertTrue(options.drive)

    def test_drive_is_off_by_default(self) -> None:
        positional, options = _take_options(["tremere_male_armor_0"])
        self.assertEqual(positional, ["tremere_male_armor_0"])
        self.assertFalse(options.drive)

    def test_drive_may_precede_the_model(self) -> None:
        positional, options = _take_options(["--drive", "tremere_male_armor_0"])
        self.assertEqual(positional, ["tremere_male_armor_0"])
        self.assertTrue(options.drive)

    def test_drive_composes_with_the_other_switches(self) -> None:
        positional, options = _take_options(
            ["--drive", "--set", "elysium.PlayerGraph 1", "tremere_male_armor_0"]
        )
        self.assertEqual(positional, ["tremere_male_armor_0"])
        self.assertTrue(options.drive)
        self.assertEqual(options.exec_cmds, ("elysium.PlayerGraph 1",))


if __name__ == "__main__":
    unittest.main()
