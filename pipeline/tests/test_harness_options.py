from __future__ import annotations

import unittest

from elysium_pipeline.unreal import _take_options
import pytest


def test_drive_is_taken_out_of_the_positional_list() -> None:
    positional, options = _take_options(["tremere_male_armor_0", "--drive"])
    assert positional == ["tremere_male_armor_0"]
    assert options.drive


def test_drive_is_off_by_default() -> None:
    positional, options = _take_options(["tremere_male_armor_0"])
    assert positional == ["tremere_male_armor_0"]
    assert not options.drive


def test_drive_may_precede_the_model() -> None:
    positional, options = _take_options(["--drive", "tremere_male_armor_0"])
    assert positional == ["tremere_male_armor_0"]
    assert options.drive


def test_arena_is_taken_out_of_the_positional_list() -> None:
    positional, options = _take_options(["tremere_male_armor_0", "--arena"])
    assert positional == ["tremere_male_armor_0"]
    assert options.arena
    assert not options.drive


def test_arena_and_drive_are_independent_flags() -> None:
    """Both may be given. The launch branch, not the parser, resolves the pair.

    Refusing the combination here would fail a launch over a redundant switch; the green room
    takes the arena in that case, being the strictly larger request.
    """
    positional, options = _take_options(["--drive", "--arena", "tremere_male_armor_0"])
    assert positional == ["tremere_male_armor_0"]
    assert options.arena
    assert options.drive


def test_body_is_taken_out_of_the_positional_list() -> None:
    positional, options = _take_options(
        ["item_w_ithaca_m_37", "--body", "malkavian_male_armor_0"]
    )
    assert positional == ["item_w_ithaca_m_37"]
    assert options.bodies == ("malkavian_male_armor_0",)


def test_no_body_leaves_the_set_empty_for_the_harness_default() -> None:
    _, options = _take_options(["item_w_ithaca_m_37"])
    assert options.bodies == ()


def test_body_is_repeatable_and_keeps_its_order() -> None:
    _, options = _take_options(
        ["--body", "malkavian_male_armor_0", "--body", "malkavian_female_armor_0"]
    )
    assert options.bodies == ("malkavian_male_armor_0", "malkavian_female_armor_0")


def test_a_comma_list_means_the_same_as_repeating_the_switch() -> None:
    _, listed = _take_options(["--body", "a_0,b_0"])
    _, repeated = _take_options(["--body", "a_0", "--body", "b_0"])
    assert listed.bodies == ("a_0", "b_0")
    assert listed.bodies == repeated.bodies


def test_the_equals_form_is_accepted() -> None:
    positional, options = _take_options(["--body=malkavian_male_armor_0", "60"])
    assert positional == ["60"]
    assert options.bodies == ("malkavian_male_armor_0",)


def test_a_body_switch_with_nothing_after_it_is_refused() -> None:
    """Silently dropping it would run the default set under a command line that named one."""
    with pytest.raises(ValueError):
        _take_options(["item_w_ithaca_m_37", "--body"])
