"""Stage one map's overhead cables for the baked-actor lane (0018 story 21-3).

VtMB strings its wires as chains of ``move_rope``/``keyframe_rope`` nodes linked by ``NextKey``;
both classnames construct the same ``CRopeKeyframe``, and the producer resolves each chain into one
row per SEGMENT, already in Unreal space and already carrying the RE'd runtime state rather than
the raw keyvalues (``UE_map_sidecars.rope_rows``, whose derivation is
``docs/vtmb/entity_visuals.md`` section 5).

Nothing is decided here. The rows are the producer's, verbatim; this module only names them as a
bake payload so the map bake can stand one ``AElysiumRopeActor`` per row in the level. Before this
story the runtime opened ``$ELYSIUM_EXPORT_ROOT/<map>/<map>.ropes`` at map load, which was the last
per-map file the game read from outside the project; the sidecar survives as an offline
intermediate and nothing reads it at run time.

Eight facts per row -- the material's ``vtmb:material:`` id, both endpoints, width, rest length,
node count, texture scale and the flag word. The flag word is not decoration: bit 0 (``Dangling``)
clears ``ROPE_LOCK_END_POINT``, and the runtime turns it into ``UCableComponent::bAttachEnd``.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any, Sequence

from elysium_pipeline.exporters import UE_map_sidecars as producer

#: 1: the first recipe (0018 story 21-3).
RECIPE_VERSION = 1

#: Every key one staged row carries, in the order the bake reads them.
ROW_FIELDS = ("index", "materialId", "aCm", "bCm", "widthCm", "restCm", "nodes", "texScale",
              "flags")

MATERIAL_PREFIX = "vtmb:material:"


class MapRopesError(ValueError):
    """A staged rope row that the bake must not be allowed to place."""


def stage_rows(map_name: str,
               pair_blocks: Sequence[Sequence[tuple[str, str]]]) -> dict[str, Any]:
    """The payload for one map, from the producer's own legacy pair reading.

    A malformed row raises rather than being dropped: the sidecar writer silently skipped a chain
    end, a mapper's typo and a zero-length segment, and those skips happen inside `rope_rows` where
    they belong. Anything that survives it and is still wrong is a producer defect, not authored
    dirt, so it fails the bake.
    """
    rows = producer.rope_rows(pair_blocks)
    for row in rows:
        _validate(map_name, row)
    result = {
        "version": RECIPE_VERSION,
        "map": map_name,
        "counts": {
            "segments": len(rows),
            "nodes": len(producer.rope_nodes(pair_blocks)),
            "materials": len({row["materialId"] for row in rows}),
        },
        "rows": rows,
    }
    result["sha256"] = hashlib.sha256(
        json.dumps(result, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    return result


def _validate(map_name: str, row: dict[str, Any]) -> None:
    missing = [field for field in ROW_FIELDS if field not in row]
    if missing:
        raise MapRopesError(f"{map_name}: rope row is missing {', '.join(missing)}")
    if not str(row["materialId"]).startswith(MATERIAL_PREFIX):
        raise MapRopesError(
            f"{map_name}: rope {row['index']} names {row['materialId']!r}, not a "
            f"{MATERIAL_PREFIX} id")
    for key in ("aCm", "bCm"):
        point = row[key]
        if len(point) != 3 or not all(isinstance(value, float) for value in point):
            raise MapRopesError(f"{map_name}: rope {row['index']} has a malformed {key}")
    # `CRopeKeyframe::KeyValue` clamps the simulated node count to [2, 10]; the runtime's
    # `NumSegments` is `nodes - 1`, so 1 would be a zero-segment cable.
    if not 2 <= int(row["nodes"]) <= 10:
        raise MapRopesError(
            f"{map_name}: rope {row['index']} simulates {row['nodes']} nodes, outside [2, 10]")
    if float(row["widthCm"]) <= 0.0:
        raise MapRopesError(f"{map_name}: rope {row['index']} has width {row['widthCm']}")


def stage_for_join(join: Any, map_name: str) -> dict[str, Any]:
    """The payload from a prepared `MapJoin` (the map-geometry stage already holds one)."""
    return stage_rows(map_name, join.pair_blocks)


def stage_map(map_name: str, root: Path | None = None) -> dict[str, Any]:
    """The payload for one map straight from its units, without meshing its geometry."""
    units = producer.read_units(map_name, root)
    lump_text = producer.entity_lump_text(units.entities["entities"])
    return stage_rows(map_name, producer.parse_entity_blocks(lump_text))
