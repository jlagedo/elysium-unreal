"""The per-map census over export_v2 GLB units (R2.2, MP-1.2).

Fixtures here are synthetic minimal documents -- just the extension keys `map_census.py` reads --
written through the shared `unit_contract.write_glb`, not full BSP-derived units. The independent
GLB validators (`test_map_entities_glb.py`, `test_map_lighting_glb.py`, `test_map_glb.py`) already
cover that a published unit's `entities[]`, `worldLights[]` and `physics.models[]` rows are correct;
this module only has to prove it aggregates those rows honestly once they exist.
"""
from __future__ import annotations

import json
from pathlib import Path

import pytest

from elysium_pipeline.formats.map_entities_glb.model import MAP_ENTITIES_EXTENSION
from elysium_pipeline.formats.map_glb.model import MAP_EXTENSION
from elysium_pipeline.formats.map_lighting_glb.model import MAP_LIGHTING_EXTENSION
from elysium_pipeline.formats.unit_contract import write_glb
from elysium_pipeline.validation import map_census


def _entity(classname, *, model=None, outputs=None):
    return {"classname": classname, "model": model, "outputs": outputs or []}


def test_entity_class_rows_totals_brush_hull_and_output_counts():
    entities = [
        _entity("func_door", model={"kind": "brush", "index": 1}, outputs=[{"target": "a"}]),
        _entity("func_door", model={"kind": "brush", "index": 2}),
        _entity("func_illusionary", model={"kind": "brush", "index": 3}),
        _entity("info_target"),
    ]
    # Only brush model 1 owns a compiled VPhysics hull.
    rows = map_census.entity_class_rows(entities, hull_model_indices={1})

    by_class = {row["classname"]: row for row in rows}
    assert by_class["func_door"] == {
        "classname": "func_door", "count": 2, "brushCount": 2, "hullCount": 1, "outputCount": 1,
    }
    assert by_class["func_illusionary"] == {
        "classname": "func_illusionary", "count": 1, "brushCount": 1, "hullCount": 0,
        "outputCount": 0,
    }
    assert by_class["info_target"] == {
        "classname": "info_target", "count": 1, "brushCount": 0, "hullCount": 0, "outputCount": 0,
    }
    # Sorted by classname, not first-seen order.
    assert [row["classname"] for row in rows] == sorted(by_class)


def test_light_rows_group_by_type_and_count_styled_lights():
    lights = [
        {"type": 1, "typeName": "point", "style": 0},
        {"type": 1, "typeName": "point", "style": 3},
        {"type": 2, "typeName": "spot", "style": 0},
        {"type": 2, "typeName": "spot", "style": 0},
    ]
    rows, styled_total = map_census.light_rows(lights)

    assert rows == [
        {"type": 1, "name": "point", "count": 2, "styledCount": 1},
        {"type": 2, "name": "spot", "count": 2, "styledCount": 0},
    ]
    assert styled_total == 1


def test_effects_class_counts_zero_fills_absent_classes():
    entities = [
        _entity("env_sprite"), _entity("env_sprite"), _entity("point_spotlight"),
        _entity("func_door"),
    ]
    counts = map_census.effects_class_counts(entities)

    assert counts["env_sprite"] == 2
    assert counts["point_spotlight"] == 1
    assert counts["env_dustmote"] == 0  # never authored by this map, still a zero row
    assert set(counts) == set(map_census.EFFECTS_CLASSES)


def _write_fixture_units(root: Path, map_name: str) -> None:
    maps_dir = root / "maps"
    maps_dir.mkdir(parents=True, exist_ok=True)
    write_glb(
        {"asset": {}, "extensions": {MAP_EXTENSION: {
            "physics": {"models": [{"modelIndex": 1}, {"modelIndex": 4}]},
        }}},
        b"", maps_dir / f"{map_name}.glb",
    )
    write_glb(
        {"asset": {}, "extensions": {MAP_ENTITIES_EXTENSION: {
            "entities": [
                {"classname": "func_door", "model": {"kind": "brush", "index": 1},
                 "outputs": [{"target": "x"}]},
                {"classname": "env_sprite", "model": None, "outputs": []},
                {"classname": "worldspawn", "model": None, "outputs": []},
            ],
        }}},
        b"", maps_dir / f"{map_name}.entities.glb",
    )
    write_glb(
        {"asset": {}, "extensions": {MAP_LIGHTING_EXTENSION: {
            "worldLights": [
                {"type": 1, "typeName": "point", "style": 0},
                {"type": 3, "typeName": "skylight", "style": 0},
            ],
        }}},
        b"", maps_dir / f"{map_name}.lighting.glb",
    )


def test_census_for_map_reads_the_three_units_and_pins_json(tmp_path: Path):
    _write_fixture_units(tmp_path, "fx_map_1")

    destination = map_census.write_census("fx_map_1", root=tmp_path)

    assert destination == tmp_path / "_census" / "fx_map_1.json"
    on_disk = json.loads(destination.read_text(encoding="utf-8"))
    assert on_disk["map"] == "fx_map_1"
    assert "sourceCommit" in on_disk
    assert on_disk["entities"]["total"] == 3
    door_row = next(
        row for row in on_disk["entities"]["classes"] if row["classname"] == "func_door"
    )
    assert door_row == {
        "classname": "func_door", "count": 1, "brushCount": 1, "hullCount": 1, "outputCount": 1,
    }
    assert on_disk["lights"]["total"] == 2
    assert on_disk["lights"]["styledCount"] == 0
    assert on_disk["effects"]["env_sprite"] == 1
    assert on_disk["effects"]["env_dustmote"] == 0


def test_a_missing_unit_raises_a_named_error(tmp_path: Path):
    with pytest.raises(map_census.MapCensusError, match="export_v2 the map's units first"):
        map_census.census_for_map("no_such_map", root=tmp_path)
