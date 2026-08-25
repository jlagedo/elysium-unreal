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
            ["--drive", "--set", "elysium.Mute 0", "tremere_male_armor_0"]
        )
        self.assertEqual(positional, ["tremere_male_armor_0"])
        self.assertTrue(options.drive)
        self.assertEqual(options.exec_cmds, ("elysium.Mute 0",))

    def test_arena_is_taken_out_of_the_positional_list(self) -> None:
        positional, options = _take_options(["tremere_male_armor_0", "--arena"])
        self.assertEqual(positional, ["tremere_male_armor_0"])
        self.assertTrue(options.arena)
        self.assertFalse(options.drive)

    def test_arena_is_off_by_default(self) -> None:
        _, options = _take_options(["tremere_male_armor_0"])
        self.assertFalse(options.arena)

    def test_arena_and_drive_are_independent_flags(self) -> None:
        """Both may be given. The launch branch, not the parser, resolves the pair.

        Refusing the combination here would fail a launch over a redundant switch; the green room
        takes the arena in that case, being the strictly larger request.
        """
        positional, options = _take_options(["--drive", "--arena", "tremere_male_armor_0"])
        self.assertEqual(positional, ["tremere_male_armor_0"])
        self.assertTrue(options.arena)
        self.assertTrue(options.drive)

class ComposeBodyOptionTests(unittest.TestCase):
    """`--body` names the bodies a harness stands, and it must leave the positionals alone.

    The composed-pose harness seats one body per launch, so a set of bodies is a set of launches
    and the set has to survive one command line. Left in the positional list `--body` would reach
    the harness as its weapon argument and the run would refuse to grant an item class named
    `--body`.
    """

    def test_body_is_taken_out_of_the_positional_list(self) -> None:
        positional, options = _take_options(
            ["item_w_ithaca_m_37", "--body", "malkavian_male_armor_0"]
        )
        self.assertEqual(positional, ["item_w_ithaca_m_37"])
        self.assertEqual(options.bodies, ("malkavian_male_armor_0",))

    def test_no_body_leaves_the_set_empty_for_the_harness_default(self) -> None:
        _, options = _take_options(["item_w_ithaca_m_37"])
        self.assertEqual(options.bodies, ())

    def test_body_is_repeatable_and_keeps_its_order(self) -> None:
        _, options = _take_options(
            ["--body", "malkavian_male_armor_0", "--body", "malkavian_female_armor_0"]
        )
        self.assertEqual(
            options.bodies, ("malkavian_male_armor_0", "malkavian_female_armor_0")
        )

    def test_a_comma_list_means_the_same_as_repeating_the_switch(self) -> None:
        _, listed = _take_options(["--body", "a_0,b_0"])
        _, repeated = _take_options(["--body", "a_0", "--body", "b_0"])
        self.assertEqual(listed.bodies, ("a_0", "b_0"))
        self.assertEqual(listed.bodies, repeated.bodies)

    def test_the_equals_form_is_accepted(self) -> None:
        positional, options = _take_options(["--body=malkavian_male_armor_0", "60"])
        self.assertEqual(positional, ["60"])
        self.assertEqual(options.bodies, ("malkavian_male_armor_0",))

    def test_a_body_switch_with_nothing_after_it_is_refused(self) -> None:
        """Silently dropping it would run the default set under a command line that named one."""
        with self.assertRaises(ValueError):
            _take_options(["item_w_ithaca_m_37", "--body"])

    def test_body_composes_with_the_other_switches(self) -> None:
        positional, options = _take_options(
            ["--body", "malkavian_male_armor_0", "--set", "elysium.Mute 0", "item_w_ithaca_m_37"]
        )
        self.assertEqual(positional, ["item_w_ithaca_m_37"])
        self.assertEqual(options.bodies, ("malkavian_male_armor_0",))
        self.assertEqual(options.exec_cmds, ("elysium.Mute 0",))


if __name__ == "__main__":
    unittest.main()
