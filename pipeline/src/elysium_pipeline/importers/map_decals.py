"""Stage one map's `infodecal` projectors for the baked-actor lane (0018 story 21-4).

VtMB leaves the OVERLAYS lump empty: every poster, stain, sign and spray is an `infodecal`
entity carrying a `texture` and an `origin`, and the engine projects it onto the surfaces within
its radius. The producer recovers each decal's projector -- the visible face it projects squarely
onto, the room-facing normal, the face's texture axes and the half-extents -- already in Unreal
space (``UE_map_sidecars.decal_rows``, whose derivation is ``UE_bsp_to_scene``'s decal pass).

Nothing is decided here. The rows are the producer's, verbatim; this module only names them as a
bake payload so the map bake can stand one ``ADecalActor`` per row without opening
``$ELYSIUM_EXPORT_ROOT/<map>/<map>.decals``. That sidecar was the last decoder-only product with a
live V2 reader; it survives as an offline intermediate until story 21-5 deletes the decoder.

Seven facts per row plus its index -- the material's ``vtmb:material:`` id, the projected centre,
the projection axis, the two surface tangent axes, and the two half-extents. **Row order is the
decal sort order**: two decals on one wall layer in the order the entity lump names them, so the
rows are never sorted or de-duplicated.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any

from elysium_pipeline.exporters import UE_map_sidecars as producer

#: 1: the first recipe (0018 story 21-4).
RECIPE_VERSION = 1

#: Every key one staged row carries, in the order the bake reads them.
ROW_FIELDS = ("index", "materialId", "locCm", "normal", "sDir", "tDir", "halfWCm", "halfHCm")

MATERIAL_PREFIX = "vtmb:material:"

#: A projector axis that is not a unit vector would skew the baked actor's rotation basis.
UNIT_TOLERANCE = 1e-4


class MapDecalsError(ValueError):
    """A staged decal row that the bake must not be allowed to place."""


def stage_result(map_name: str, result: dict[str, Any]) -> dict[str, Any]:
    """The payload for one map, from the producer's own projector run.

    A malformed row raises rather than being dropped. The producer already skips and COUNTS the
    two things authored maps get wrong -- a decal whose material resolves no albedo, and one whose
    origin stands near no face it can bind -- so anything that survives `decal_rows` and is still
    wrong is a producer defect, not authored dirt, and it fails the bake.
    """
    rows = result["rows"]
    for row in rows:
        _validate(map_name, row)
    payload = {
        "version": RECIPE_VERSION,
        "map": map_name,
        "counts": {
            "placed": int(result["placed"]),
            # Split, because they mean opposite things: `unresolved` is a material the units
            # cannot size, which the decoder skipped too, and `unbound` is a decal that found no
            # projector face, which is the one way this lane can lose a row the decoder placed.
            "unresolved": int(result["unresolved"]),
            "unbound": int(result["unbound"]),
            "unmatched": int(result["unresolved"]) + int(result["unbound"]),
            "materials": int(result["materials"]),
            "projectorFaces": int(result["projectorFaces"]),
            # Faces the published unit states through their own displacement mesh, and so cannot
            # offer the projector search a flat winding. `decal_rows` documents the difference;
            # this row is what makes it visible per map rather than only in the probe.
            "dispFacesSkipped": int(result["dispFacesSkipped"]),
        },
        "rows": rows,
    }
    payload["sha256"] = hashlib.sha256(
        json.dumps(payload, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    return payload


def _validate(map_name: str, row: dict[str, Any]) -> None:
    missing = [field for field in ROW_FIELDS if field not in row]
    if missing:
        raise MapDecalsError(f"{map_name}: decal row is missing {', '.join(missing)}")
    if not str(row["materialId"]).startswith(MATERIAL_PREFIX):
        raise MapDecalsError(
            f"{map_name}: decal {row['index']} names {row['materialId']!r}, not a "
            f"{MATERIAL_PREFIX} id")
    for key in ("locCm", "normal", "sDir", "tDir"):
        value = row[key]
        if len(value) != 3 or not all(isinstance(component, float) for component in value):
            raise MapDecalsError(f"{map_name}: decal {row['index']} has a malformed {key}")
    for key in ("normal", "sDir", "tDir"):
        length = sum(component * component for component in row[key]) ** 0.5
        if abs(length - 1.0) > UNIT_TOLERANCE:
            raise MapDecalsError(
                f"{map_name}: decal {row['index']} has a {key} of length {length:.6f}")
    # A zero extent is a decal the engine would draw as nothing; a negative one inverts the
    # projection box. `$decalscale` defaults to 1.0 and an albedo has pixels, so neither can come
    # from a material that resolved -- both mean the producer let something through.
    for key in ("halfWCm", "halfHCm"):
        if float(row[key]) <= 0.0:
            raise MapDecalsError(f"{map_name}: decal {row['index']} has {key} {row[key]}")


def stage_for_join(join: Any, map_name: str,
                   materials: producer.MaterialUnits | None = None) -> dict[str, Any]:
    """The payload from a prepared `MapJoin` (the map-geometry stage already holds one)."""
    return stage_result(map_name, producer.decal_rows(join, materials=materials))


def stage_map(map_name: str, root: Path | None = None) -> dict[str, Any]:
    """The payload for one map straight from its units, without meshing its geometry."""
    join = producer.prepare_join(map_name, root)
    return stage_result(map_name, producer.decal_rows(join, root=root))
