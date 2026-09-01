"""The R3.3 differ: `UE_map_sidecars.py`'s output against the legacy `UE_bsp_to_scene.py`'s, for a
map list (`docs/project/seam_migration.md` -> "Roadmap -- one pipeline" R3.3, MP-2.3).

Every sidecar R3.2 reproduces (`.ents`, `.hulls`, `.dispcol`, `.lights`, `.env`, `.sky`, `.spawn`,
`.ropes`) is compared byte for byte between a legacy directory (default `$ELYSIUM_EXPORT_ROOT/<map>/`)
and `$ELYSIUM_EXPORT_V2_ROOT/_sidecars/<map>/` (the producer's, R3.2). `.ents` additionally
gets a structural diff -- per-entity field diff, hull vertex counts and AABBs per brush entity, and
the `outputs` list -- because a byte difference there needs to say *which* entity and *which*
field moved, not just that the files differ.

R3.5 made the producer the default writer of `$ELYSIUM_EXPORT_ROOT/<map>/`'s eight legacy sidecars,
so that directory is no longer the legacy exporter's own output unless the caller points this tool
somewhere else. `--legacy-root` (or `diff_map(..., legacy_root=...)`) lets a caller supply a scratch
tree built by calling `UE_bsp_to_scene.main` directly. Because only the producer ever writes the
R2.4 readiness marker `<map>.ready` (the legacy exporter never did, and R3.5 now writes that marker
into `$ELYSIUM_EXPORT_ROOT/<map>/` too), `diff_map` treats a legacy directory carrying that marker
as a self-comparison and refuses to classify it as `byte_equal`/`unexpected_divergence` -- see
`classification == "self_comparison"` below.

Two divergences are pre-declared in the roadmap and are expected **by name**, not treated as a
defect this tool found:

1. `la_hub_1`'s legacy `.hulls`/`.ents` are corrupted by a signed-int16 `planenum` overflow in the
   legacy exporter itself (R3.1); the producer does not reproduce the corruption, so that map is
   never byte-compared -- `diff_map` short-circuits on the name.
2. `.dispcol`'s printed 4th decimal, on every map that has displacements, because the root unit
   publishes no numeric DISP_VERTS/VERTEXES and the producer recovers displacement geometry from
   the mesh's own float32 `POSITION` accessor instead (`UE_map_sidecars.displacement_triangles`).
   `classify_dispcol` proves this by measurement -- same row count, same row order, max per-row
   delta under `DISPCOL_TOLERANCE_CM` -- rather than assuming every `.dispcol` difference is this
   one.

A map's `classification` is `"byte_equal"` when every sidecar matches exactly,
`"named_divergence_only"` when every difference is one of the two above, and
`"unexpected_divergence"` otherwise -- the number R3.3 lands on has to be one of the first two.

Like `map_census.py` and `shots_diff.py`, this is an internal library module: it is run directly

    uv run python -m elysium_pipeline.validation.map_sidecar_diff sp_tutorial_1 sm_pawnshop_1 sm_hub_1

and pins one JSON report per map under `$ELYSIUM_WORK_ROOT/exports_v2/_sidecar_diff/<map>.json`,
beside `_census/` and `_sidecars/`.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any

from elysium_pipeline import paths
from elysium_pipeline.exporters.UE_map_sidecars import SIDECAR_DIR_NAME
from elysium_pipeline.validation.shots_diff import git_commit

#: The eight legacy sidecars R3.2 reproduces. `.ready` (R2.4) is producer-only -- the legacy
#: exporter never wrote a readiness marker -- so it is never compared.
SIDECAR_SUFFIXES = (".ents", ".hulls", ".dispcol", ".lights", ".env", ".sky", ".spawn", ".ropes")

#: Measured max |delta| on the three-map corpus is 0.0019 cm (`sp_tutorial_1`); this is a
#: deliberate margin above that, not a re-derivation -- see `UE_map_sidecars.displacement_triangles`.
DISPCOL_TOLERANCE_CM = 0.01

#: `seam_migration.md` R3.3: pre-declared, named divergences that a byte diff must not flag as a
#: producer defect.
DISPCOL_NAMED_DIVERGENCE = (
    "seam-precision limit: DISP_VERTS is not published numerically in the root unit, so the "
    "producer recovers displacement geometry from the mesh's float32 POSITION accessor instead "
    "(UE_map_sidecars.displacement_triangles); R3.4 decided to accept this rather than publish "
    "DISP_VERTS numerically (seam_map_map.md -> \"R3.4 -- the two the port surfaced\")."
)
KNOWN_MAP_DIVERGENCES: dict[str, str] = {
    "la_hub_1": "the legacy exporter's own signed-int16 planenum overflow corrupts its "
    ".hulls/.ents on this map (R3.1); the producer does not reproduce the corruption, so the map "
    "is never byte-compared.",
}

#: `$ELYSIUM_WORK_ROOT/exports_v2/_sidecar_diff/<map>.json`.
DIFF_DIR_NAME = "_sidecar_diff"

#: R3.5 made the producer the default writer of the legacy directory, so it now also carries the
#: R2.4 readiness marker the legacy exporter never wrote (`UE_map_sidecars.write_sidecars`,
#: "written last and only when every sidecar Travel depends on is on disk"). Its presence in the
#: *legacy* directory is exact evidence the two sides being compared are the same producer output.
SELF_COMPARISON_REASON = (
    "the legacy directory carries the producer-only R2.4 '.ready' marker (R3.5 made the producer "
    "the default writer of $ELYSIUM_EXPORT_ROOT/<map>/'s sidecars), so this run would be comparing "
    "the producer's output against itself; pass --legacy-root (or legacy_root=) pointed at a tree "
    "built by calling UE_bsp_to_scene.main directly to get a real comparison."
)


# ---------------------------------------------------------------- file-level (byte) comparison


def legacy_dir(map_name: str, root: Path | None = None) -> Path:
    """The legacy exporter's per-map output directory."""

    return (root or paths.export_root()) / map_name


def producer_dir(map_name: str, root: Path | None = None) -> Path:
    """The R3.2 producer's per-map sidecar directory."""

    return (root or paths.export_v2_root()) / SIDECAR_DIR_NAME / map_name


def file_diff(legacy_path: Path, producer_path: Path) -> dict[str, Any]:
    """Byte comparison of one sidecar. Presence on only one side is itself a result, not an error --
    a map with no displacements ships no `.dispcol` on either side, which is `equal=True`."""

    legacy_bytes = legacy_path.read_bytes() if legacy_path.is_file() else None
    producer_bytes = producer_path.read_bytes() if producer_path.is_file() else None
    result: dict[str, Any] = {
        "legacyPath": str(legacy_path),
        "producerPath": str(producer_path),
        "legacyPresent": legacy_bytes is not None,
        "producerPresent": producer_bytes is not None,
    }
    if legacy_bytes is None and producer_bytes is None:
        result["equal"] = True
        return result
    if legacy_bytes is None or producer_bytes is None:
        result["equal"] = False
        return result
    result["legacyBytes"] = len(legacy_bytes)
    result["producerBytes"] = len(producer_bytes)
    if legacy_bytes == producer_bytes:
        result["equal"] = True
        return result
    result["equal"] = False
    shortest = min(len(legacy_bytes), len(producer_bytes))
    result["firstDiffOffset"] = next(
        (i for i in range(shortest) if legacy_bytes[i] != producer_bytes[i]), shortest
    )
    result["legacySha256"] = hashlib.sha256(legacy_bytes).hexdigest()
    result["producerSha256"] = hashlib.sha256(producer_bytes).hexdigest()
    return result


def classify_dispcol(diff: dict[str, Any], legacy_path: Path, producer_path: Path) -> str:
    """`"byte_equal"` / `"named_divergence"` / `"unexpected"` for a `.dispcol` file diff.

    A row-count or row-shape mismatch is never the named divergence -- the pre-declared limit is a
    precision difference on identical geometry, not a different triangle soup -- so those, and any
    delta over `DISPCOL_TOLERANCE_CM`, come back `"unexpected"`.
    """

    if diff["equal"]:
        return "byte_equal"
    if not (diff["legacyPresent"] and diff["producerPresent"]):
        return "unexpected"
    legacy_rows = _read_float_rows(legacy_path)
    producer_rows = _read_float_rows(producer_path)
    if len(legacy_rows) != len(producer_rows):
        return "unexpected"
    max_delta = 0.0
    for legacy_row, producer_row in zip(legacy_rows, producer_rows):
        if len(legacy_row) != len(producer_row):
            return "unexpected"
        max_delta = max(max_delta, max(
            abs(a - b) for a, b in zip(legacy_row, producer_row)
        ) if legacy_row else 0.0)
    return "named_divergence" if max_delta <= DISPCOL_TOLERANCE_CM else "unexpected"


def _read_float_rows(path: Path) -> list[list[float]]:
    lines = path.read_text(encoding="ascii").splitlines()
    return [[float(token) for token in line.split()] for line in lines if line.strip()]


# ---------------------------------------------------------------- `.ents` structural diff


def _read_entities(path: Path) -> list[dict[str, Any]]:
    if not path.is_file():
        return []
    document = json.loads(path.read_text(encoding="ascii"))
    return document.get("entities") or []


def hull_vertex_counts(entity: dict[str, Any]) -> list[int]:
    """Vertex count of each hull on a brush-entity row, ascending brush index (the field list's
    own emission order), so a count diff says which hull moved rather than only that one did."""

    return [len(hull) // 3 for hull in entity.get("hulls") or []]


def hull_aabb(entity: dict[str, Any]) -> list[float] | None:
    """`[minX, minY, minZ, maxX, maxY, maxZ]` over every vertex of every hull on the row, or
    `None` when the row carries no hulls at all."""

    points = [
        (hull[i], hull[i + 1], hull[i + 2])
        for hull in entity.get("hulls") or []
        for i in range(0, len(hull), 3)
    ]
    if not points:
        return None
    xs, ys, zs = zip(*points)
    return [min(xs), min(ys), min(zs), max(xs), max(ys), max(zs)]


#: Fields compared as their own summary rather than the raw value: a hull list is large and the
#: field list's own semantics (counts, bounds) are what a differ needs to explain a divergence.
_SUMMARIZED_FIELDS = frozenset({"hulls", "outputs"})


def entity_field_diff(legacy: dict[str, Any], producer: dict[str, Any]) -> dict[str, Any]:
    """One `.ents` row's differences: added/removed/changed plain fields, the `outputs` list when
    it differs, and `hulls` as vertex counts + AABB rather than the raw flat arrays."""

    legacy_keys = set(legacy) - _SUMMARIZED_FIELDS
    producer_keys = set(producer) - _SUMMARIZED_FIELDS
    result: dict[str, Any] = {
        "added": sorted(producer_keys - legacy_keys),
        "removed": sorted(legacy_keys - producer_keys),
        "changed": {
            key: {"legacy": legacy[key], "producer": producer[key]}
            for key in sorted(legacy_keys & producer_keys)
            if legacy[key] != producer[key]
        },
    }
    if legacy.get("outputs") != producer.get("outputs"):
        result["outputs"] = {"legacy": legacy.get("outputs"), "producer": producer.get("outputs")}
    legacy_counts, producer_counts = hull_vertex_counts(legacy), hull_vertex_counts(producer)
    if legacy_counts != producer_counts:
        result["hullVertexCounts"] = {"legacy": legacy_counts, "producer": producer_counts}
    legacy_aabb, producer_aabb = hull_aabb(legacy), hull_aabb(producer)
    if legacy_aabb != producer_aabb:
        result["hullAabb"] = {"legacy": legacy_aabb, "producer": producer_aabb}
    return result


def _row_differs(diff: dict[str, Any]) -> bool:
    return bool(
        diff["added"] or diff["removed"] or diff["changed"]
        or "outputs" in diff or "hullVertexCounts" in diff or "hullAabb" in diff
    )


def ents_structural_diff(legacy_path: Path, producer_path: Path) -> dict[str, Any]:
    """Every `.ents` divergence, one row per entity index that differs -- `entities[]` is one row
    per lump block in lump order with no drops (`seam_map_map.md`), so comparing by index is the
    same alignment the game's save system depends on."""

    legacy_entities = _read_entities(legacy_path)
    producer_entities = _read_entities(producer_path)
    rows: list[dict[str, Any]] = []
    for index in range(max(len(legacy_entities), len(producer_entities))):
        legacy_row = legacy_entities[index] if index < len(legacy_entities) else None
        producer_row = producer_entities[index] if index < len(producer_entities) else None
        if legacy_row is None or producer_row is None:
            rows.append({"index": index, "onlyIn": "legacy" if producer_row is None else "producer"})
            continue
        diff = entity_field_diff(legacy_row, producer_row)
        if _row_differs(diff):
            rows.append({"index": index, **diff})
    return {
        "legacyEntityCount": len(legacy_entities),
        "producerEntityCount": len(producer_entities),
        "entityDiffCount": len(rows),
        "entityDiffs": rows,
    }


# ---------------------------------------------------------------- per-map run


def diff_map(
    map_name: str, *, legacy_root: Path | None = None, producer_root: Path | None = None
) -> dict[str, Any]:
    """One map's full sidecar diff: every byte comparison, the `.ents` structural diff, and the
    roll-up classification R3.3 lands on."""

    if map_name in KNOWN_MAP_DIVERGENCES:
        return {
            "map": map_name,
            "classification": "named_divergence_only",
            "reason": KNOWN_MAP_DIVERGENCES[map_name],
            "files": {},
            "ents": None,
        }

    legacy_base = legacy_dir(map_name, legacy_root)
    producer_base = producer_dir(map_name, producer_root)

    if (legacy_base / f"{map_name}.ready").is_file():
        return {
            "map": map_name,
            "classification": "self_comparison",
            "reason": SELF_COMPARISON_REASON,
            "files": {},
            "ents": None,
        }

    files: dict[str, Any] = {}
    for suffix in SIDECAR_SUFFIXES:
        legacy_path = legacy_base / f"{map_name}{suffix}"
        producer_path = producer_base / f"{map_name}{suffix}"
        diff = file_diff(legacy_path, producer_path)
        if suffix == ".dispcol" and not diff["equal"]:
            diff["classification"] = classify_dispcol(diff, legacy_path, producer_path)
            if diff["classification"] == "named_divergence":
                diff["reason"] = DISPCOL_NAMED_DIVERGENCE
        else:
            diff["classification"] = "byte_equal" if diff["equal"] else "unexpected"
        files[suffix] = diff

    ents_diff = ents_structural_diff(
        legacy_base / f"{map_name}.ents", producer_base / f"{map_name}.ents"
    )

    clean = ents_diff["entityDiffCount"] == 0 and all(
        diff["classification"] in ("byte_equal", "named_divergence") for diff in files.values()
    )
    all_equal = ents_diff["entityDiffCount"] == 0 and all(
        diff["classification"] == "byte_equal" for diff in files.values()
    )
    classification = (
        "byte_equal" if all_equal else "named_divergence_only" if clean else "unexpected_divergence"
    )

    return {
        "map": map_name,
        "classification": classification,
        "files": files,
        "ents": ents_diff,
    }


def diff_path(map_name: str, root: Path | None = None) -> Path:
    return (root or paths.export_v2_root()) / DIFF_DIR_NAME / f"{map_name}.json"


def write_diff(map_name: str, *, root: Path | None = None, **kwargs: Any) -> Path:
    """Run `diff_map` and pin the report beside `_census/` and `_sidecars/`."""

    report = diff_map(map_name, **kwargs)
    report["sourceCommit"] = git_commit(paths.repo_root())
    destination = diff_path(map_name, root)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return destination


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("maps", nargs="+", help="map stems to diff (e.g. sp_tutorial_1)")
    parser.add_argument(
        "--legacy-root", type=Path, default=None,
        help="directory holding <map>/ legacy sidecar trees to compare against the producer's "
             "(default $ELYSIUM_EXPORT_ROOT). Since R3.5 the default root is normally the "
             "producer's own output -- point this at a scratch tree built by calling "
             "UE_bsp_to_scene.main directly for a real comparison.",
    )
    args = parser.parse_args(argv)

    unexpected_maps = []
    self_comparison_maps = []
    for map_name in args.maps:
        destination = write_diff(map_name, legacy_root=args.legacy_root)
        report = json.loads(destination.read_text(encoding="utf-8"))
        classification = report["classification"]
        if classification == "unexpected_divergence":
            unexpected_maps.append(map_name)
        elif classification == "self_comparison":
            self_comparison_maps.append(map_name)
        print(f"{map_name}: {classification} -> {destination}")
    if self_comparison_maps:
        print(f"\n{len(self_comparison_maps)} map(s) compared the producer against itself "
              f"(pass --legacy-root): {', '.join(self_comparison_maps)}")
    if unexpected_maps:
        print(f"\n{len(unexpected_maps)} map(s) with an unexpected divergence: "
              f"{', '.join(unexpected_maps)}")
    return 1 if unexpected_maps or self_comparison_maps else 0


if __name__ == "__main__":
    raise SystemExit(main())
