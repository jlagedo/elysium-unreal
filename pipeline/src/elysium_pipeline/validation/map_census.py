"""Per-map entity/light/effects-class censuses over the export_v2 GLB units (R2.2, MP-1.2; the effects vocabulary is R7.3's).

The three units a map publishes already carry their own per-field census rows -- the entities
unit's `classCensus[]`, the lighting unit's `lightTypeCensus[]` -- but neither states brush/hull/
output totals per class, a light's styled share, or the fixed effects-class vocabulary R7.3
consumes by prevalence. This module re-aggregates those facts from the published units without
touching the exporter or its independent validators, and pins the result as one JSON document per
map so the differ (R3.3) and the effects work (R7.3) have a ground truth that does not require
re-reading multi-hundred-megabyte GLBs.

Like `shots_diff.py`, this is an internal library module: `uv run elysium` does not wire it in as
a subcommand, and it is run directly (`uv run python -m elysium_pipeline.validation.map_census
<map> [<map> ...]`) or imported by a caller that already has the export_v2 root.
"""

from __future__ import annotations

import argparse
import json
from collections import Counter
from pathlib import Path
from typing import Any

from elysium_pipeline.formats.map_entities_glb.model import MAP_ENTITIES_EXTENSION
from elysium_pipeline.formats.map_glb.model import MAP_EXTENSION
from elysium_pipeline.formats.map_lighting_glb.model import MAP_LIGHTING_EXTENSION
from elysium_pipeline.formats.unit_contract import read_glb
from elysium_pipeline.paths import export_v2_root
from elysium_pipeline.validation.shots_diff import git_commit

#: The effects-entity family R7.3 lands (`docs/architecture/effects-architecture.md` section 5.8,
#: the real census over the 108 maps -- R2.2's `env_fire` / `env_embers` / `env_lightglow` /
#: `point_spotlight` / `env_sun` guesses have 0 placements and are gone); a class this map never
#: authors still gets a zero row, so a later diff sees the vocabulary hold steady across a
#: re-export. `env_sprite` is R6.1's billboard, kept because it is the one other placed
#: effects-family class.
EFFECTS_CLASSES = [
    "env_particle",
    "func_particle",
    "func_dustmotes",
    "env_steam",
    "env_beam",
    "params_particle",
    "params_explosion",
    "point_explosion",
    "env_shake",
    "env_physexplosion",
    "env_physimpact",
    "env_shooter",
    "env_particle_hud",
    "env_sprite",
]

#: `$ELYSIUM_WORK_ROOT/exports_v2/_census/<map>.json` -- beside the GLB units it summarizes,
#: mirroring `shots_diff.py`'s `_shots/_baseline` placement under the export root it reads.
CENSUS_DIR_NAME = "_census"


class MapCensusError(ValueError):
    """A GLB unit the census tool needs is missing or carries no usable extension."""


def _read_extension(path: Path, extension_key: str) -> dict[str, Any]:
    if not path.is_file():
        raise MapCensusError(f"{path} does not exist -- export_v2 the map's units first")
    document, _binary = read_glb(path)
    extension = (document.get("extensions") or {}).get(extension_key)
    if not isinstance(extension, dict):
        raise MapCensusError(f"{path} carries no {extension_key!r} extension")
    return extension


def _unit_paths(map_name: str, root: Path) -> dict[str, Path]:
    maps_dir = root / "maps"
    return {
        "root": maps_dir / f"{map_name}.glb",
        "entities": maps_dir / f"{map_name}.entities.glb",
        "lighting": maps_dir / f"{map_name}.lighting.glb",
    }


def entity_class_rows(
    entities: list[dict[str, Any]], hull_model_indices: set[int]
) -> list[dict[str, Any]]:
    """One row per classname: entity count, brush-model count, hull count, output count.

    `brushCount` is every entity of the class whose `model` keyvalue names a brush model (`*N`);
    `hullCount` is the subset of those whose brush-model index also owns a PHYSCOLLIDE entry in
    the root unit's `physics.models[]` (a solid the map compiled a VPhysics hull for, not merely a
    BSP brush). `outputCount` sums `entities[i].outputs[]` across the class.
    """

    totals: dict[str, dict[str, int]] = {}
    for entity in entities:
        classname = str(entity.get("classname") or "")
        row = totals.setdefault(
            classname, {"count": 0, "brushCount": 0, "hullCount": 0, "outputCount": 0}
        )
        row["count"] += 1
        row["outputCount"] += len(entity.get("outputs") or [])
        model = entity.get("model") or {}
        if model.get("kind") == "brush":
            row["brushCount"] += 1
            if model.get("index") in hull_model_indices:
                row["hullCount"] += 1
    return [
        {"classname": classname, **totals[classname]} for classname in sorted(totals)
    ]


def light_rows(world_lights: list[dict[str, Any]]) -> tuple[list[dict[str, Any]], int]:
    """One row per `dworldlight_t.type`, its count and how many of that type carry a lightstyle
    (`style != 0`, the flicker/pulse patterns `docs/vtmb/lighting.md` names); the second return
    value is the styled total across every type.
    """

    counts: Counter[int] = Counter()
    styled: Counter[int] = Counter()
    names: dict[int, str] = {}
    for light in world_lights:
        light_type = int(light.get("type", -1))
        counts[light_type] += 1
        names[light_type] = str(light.get("typeName") or light_type)
        if int(light.get("style", 0)) != 0:
            styled[light_type] += 1
    rows = [
        {
            "type": light_type,
            "name": names[light_type],
            "count": counts[light_type],
            "styledCount": styled[light_type],
        }
        for light_type in sorted(counts)
    ]
    return rows, sum(styled.values())


def effects_class_counts(entities: list[dict[str, Any]]) -> dict[str, int]:
    """Occurrences of the R7.3 effects-entity vocabulary, zero-filled for a class the map omits."""

    counts = Counter(str(entity.get("classname") or "") for entity in entities)
    return {classname: counts.get(classname, 0) for classname in EFFECTS_CLASSES}


def census_for_map(map_name: str, *, root: Path | None = None) -> dict[str, Any]:
    """The full per-map census, read from the map's three published export_v2 units."""

    root = root if root is not None else export_v2_root()
    paths = _unit_paths(map_name, root)

    root_ext = _read_extension(paths["root"], MAP_EXTENSION)
    entities_ext = _read_extension(paths["entities"], MAP_ENTITIES_EXTENSION)
    lighting_ext = _read_extension(paths["lighting"], MAP_LIGHTING_EXTENSION)

    entities = entities_ext.get("entities") or []
    hull_model_indices = {
        int(model["modelIndex"])
        for model in (root_ext.get("physics") or {}).get("models") or []
        if "modelIndex" in model
    }
    class_rows = entity_class_rows(entities, hull_model_indices)
    world_lights = lighting_ext.get("worldLights") or []
    light_type_rows, styled_total = light_rows(world_lights)

    return {
        "map": map_name,
        "sourceCommit": git_commit(Path(__file__).resolve().parents[3]),
        "entities": {
            "total": len(entities),
            "classes": class_rows,
        },
        "lights": {
            "total": len(world_lights),
            "types": light_type_rows,
            "styledCount": styled_total,
        },
        "effects": effects_class_counts(entities),
    }


def census_path(map_name: str, *, root: Path | None = None) -> Path:
    root = root if root is not None else export_v2_root()
    return root / CENSUS_DIR_NAME / f"{map_name}.json"


def write_census(map_name: str, *, root: Path | None = None) -> Path:
    """Compute and pin one map's census beside the export_v2 corpus it summarizes."""

    census = census_for_map(map_name, root=root)
    destination = census_path(map_name, root=root)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(census, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return destination


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("maps", nargs="+", help="map stems to census (e.g. sp_tutorial_1)")
    args = parser.parse_args(argv)

    for map_name in args.maps:
        destination = write_census(map_name)
        census = json.loads(destination.read_text(encoding="utf-8"))
        classes = len(census["entities"]["classes"])
        lights = census["lights"]["total"]
        styled = census["lights"]["styledCount"]
        print(
            f"{map_name}: {census['entities']['total']} entities across {classes} class(es), "
            f"{lights} light(s) ({styled} styled) -> {destination}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
