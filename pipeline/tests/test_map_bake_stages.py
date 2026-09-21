"""`--from <stage>`: the map bake's stage vocabulary (0018 story 21-2).

The list is shared by the two halves of the lane -- the CLI refuses an unknown stage before
paying for an editor boot, the commandlet turns the name into the set it forces -- so these pin
the list itself as well as what a name means.
"""

from __future__ import annotations

import pytest

from elysium_pipeline.map_bake_stages import STAGE_ORDER, stages_from


def test_the_stage_order_is_the_order_bake_one_runs() -> None:
    # The list is the `--from` vocabulary AND the sequence, so drift between it and `bake_one`
    # would silently mean `--from collision` forces the level but not the collision cook.
    assert STAGE_ORDER == (
        "textures", "materials", "world", "sky", "particles",
        "entities", "environment", "collision", "level")


def test_from_a_stage_forces_it_and_everything_after() -> None:
    assert stages_from("collision") == frozenset({"collision", "level"})
    assert stages_from("level") == frozenset({"level"})
    # `--force` is `--from textures`, and the two must agree on what that means.
    assert stages_from("textures") == frozenset(STAGE_ORDER)


def test_no_from_forces_nothing() -> None:
    assert stages_from("") == frozenset()


def test_an_unknown_stage_is_refused_by_name() -> None:
    # `nav` is the tempting wrong answer: navigation is cut inside `level` and is not separable
    # from it, so asking for it has to say so rather than silently forcing nothing.
    with pytest.raises(ValueError) as caught:
        stages_from("nav")
    assert "nav" in str(caught.value) and "collision" in str(caught.value)
