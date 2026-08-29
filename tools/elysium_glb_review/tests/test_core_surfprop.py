"""Contract tests for surface-property inheritance.

Eighteen of the sixty-three units declare no physics of their own. Reading one without
walking its base chain reports nothing where the game reads a value, so the walk is not
a convenience.
"""

from __future__ import annotations

import tempfile
from pathlib import Path

from core import glb, surfprop

from . import support


def _corpus(root: Path, units: dict[str, bytes]) -> None:
    support.write_corpus(root, units)


def _resolve(root: Path, name: str) -> surfprop.Resolved:
    document = glb.read_json(root / "surface-properties" / (name + ".glb"))
    return surfprop.resolve(document, root)


def test_a_unit_that_declares_nothing_reads_its_base(tmp_path: Path) -> None:
    _corpus(tmp_path,
        {
            "surface-properties/brick.glb": support.surface_property_unit(
                "brick", base="concrete"
            ),
            "surface-properties/concrete.glb": support.surface_property_unit(
                "concrete", physics={"density": 2400.0, "friction": 0.8}
            ),
        }
    )
    resolved = _resolve(tmp_path, "brick")
    assert resolved.values["physics"] == {"density": 2400.0, "friction": 0.8}
    assert resolved.is_inherited("physics", "density")


def test_a_nearer_declaration_wins_over_a_further_one(tmp_path: Path) -> None:
    _corpus(tmp_path,
        {
            "surface-properties/a.glb": support.surface_property_unit(
                "a", base="b", physics={"friction": 0.1}
            ),
            "surface-properties/b.glb": support.surface_property_unit(
                "b", physics={"friction": 0.9, "density": 100.0}
            ),
        }
    )
    resolved = _resolve(tmp_path, "a")
    assert resolved.values["physics"]["friction"] == 0.1
    assert resolved.values["physics"]["density"] == 100.0
    assert not resolved.is_inherited("physics", "friction")
    assert resolved.is_inherited("physics", "density")


def test_the_chain_records_every_unit_it_walked_nearest_first(tmp_path: Path) -> None:
    _corpus(tmp_path,
        {
            "surface-properties/a.glb": support.surface_property_unit("a", base="b"),
            "surface-properties/b.glb": support.surface_property_unit("b", base="c"),
            "surface-properties/c.glb": support.surface_property_unit(
                "c", physics={"density": 1.0}
            ),
        }
    )
    resolved = _resolve(tmp_path, "a")
    assert resolved.chain == (
        "vtmb:surface-property:a",
        "vtmb:surface-property:b",
        "vtmb:surface-property:c",
    )


def test_origins_name_the_unit_each_value_came_from(tmp_path: Path) -> None:
    _corpus(tmp_path,
        {
            "surface-properties/a.glb": support.surface_property_unit("a", base="b"),
            "surface-properties/b.glb": support.surface_property_unit(
                "b", physics={"density": 7.0}
            ),
        }
    )
    resolved = _resolve(tmp_path, "a")
    assert resolved.origins[("physics", "density")] == "vtmb:surface-property:b"


def test_a_root_unit_walks_only_itself(tmp_path: Path) -> None:
    _corpus(tmp_path,
        {
            "surface-properties/concrete.glb": support.surface_property_unit(
                "concrete", physics={"density": 2400.0}
            )
        }
    )
    resolved = _resolve(tmp_path, "concrete")
    assert resolved.chain == ("vtmb:surface-property:concrete",)
    assert not resolved.is_inherited("physics", "density")


def test_a_base_with_no_file_is_reported_rather_than_ignored(tmp_path: Path) -> None:
    # The seam treats an unresolvable base as a hard failure, so the tool must not
    # quietly present a unit as complete when its inherited half is missing.
    _corpus(tmp_path,
        {"surface-properties/a.glb": support.surface_property_unit("a", base="gone")}
    )
    resolved = _resolve(tmp_path, "a")
    assert resolved.broken_base == "vtmb:surface-property:gone"
    assert not resolved.cyclic


def test_a_cycle_terminates_and_is_flagged(tmp_path: Path) -> None:
    _corpus(tmp_path,
        {
            "surface-properties/a.glb": support.surface_property_unit(
                "a", base="b", physics={"friction": 1.0}
            ),
            "surface-properties/b.glb": support.surface_property_unit("b", base="a"),
        }
    )
    resolved = _resolve(tmp_path, "a")
    assert resolved.cyclic
    assert resolved.values["physics"]["friction"] == 1.0


def test_scalars_inherit_from_the_nearest_unit_that_declares_them(tmp_path: Path) -> None:
    _corpus(tmp_path,
        {
            "surface-properties/a.glb": support.surface_property_unit("a", base="b"),
            "surface-properties/b.glb": support.surface_property_unit(
                "b", game_material="C"
            ),
        }
    )
    assert _resolve(tmp_path, "a").values["gameMaterial"] == "C"


def test_the_walk_stops_at_the_depth_limit(tmp_path: Path) -> None:
    units = {}
    for step in range(8):
        units["surface-properties/s%d.glb" % step] = support.surface_property_unit(
            "s%d" % step, base="s%d" % (step + 1)
        )
    _corpus(tmp_path, units)
    document = glb.read_json(tmp_path / "surface-properties/s0.glb")
    resolved = surfprop.resolve(document, tmp_path, max_depth=3)
    assert len(resolved.chain) == 3
