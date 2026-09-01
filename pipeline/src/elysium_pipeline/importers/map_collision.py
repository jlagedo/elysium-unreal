"""Stage one map's collision for the `UElysiumMapCollisionPayload` asset (R4.2).

`uv run elysium import map-collision --maps <map>...` turns each named map's collision sidecars
into one `/ElysiumBaked/<map>/DA_<map>_Collision` asset carrying the same convex sets and the same
triangle soup, cooked once offline instead of on every map load
(`docs/architecture/seam_map_map.md` -> "Import"). This module is the offline stage half: it reads
`<map>.hulls`, `<map>.dispcol` and the brush-entity `hulls` of `<map>.ents`, applies the one
transform the runtime applies (the 3D-skybox scale on a `sky` brush entity), asserts parity against
the files it read, and writes one `manifest.json` the editor phase
(`pipeline/unreal/import_map_collision.py`) executes.

**The rows come from the sidecars, not from a second port of the hull solver.** Since R3.5 those
files are written by the R3.2 producer from the published GLB units, so re-deriving them here would
mean a second implementation of `brush_hull` with its own tolerances -- exactly the divergence
`seam_map_map.md` -> "Producer join" exists to prevent.

**Scope.** The stage refuses to run unscoped: there is no `--all` here, because one asset per map
over 108 maps is a separately approved operation, not this lane's working mode.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import json
from pathlib import Path
from typing import Any, Sequence

#: The lane's own name -- the staging directory below `$ELYSIUM_WORK_ROOT/import/` and the recipe
#: stage label the editor phase fingerprints under.
FAMILY = "map_collision"

#: Manifest schema the editor phase understands. Bumped when the row shape changes.
MANIFEST_SCHEMA = "1.0.0"

#: The name of the staged manifest, and of the report the editor phase writes beside it.
MANIFEST_NAME = "manifest.json"
IMPORT_REPORT_NAME = "import_report.json"

#: Bumped whenever this lane's mapping changes in a way that must re-author every asset.
RECIPE_VERSION = 1

#: The mount every per-map asset lands under. The C++ twin is
#: `FElysiumContentPaths::BakedMapDir` / `BakedMapCollision`.
BAKED_MOUNT = "/ElysiumBaked"

#: A convex needs at least a tetrahedron; `UElysiumBrushComponent::InitBrush` and the payload's own
#: `FillConvexElems` drop anything smaller, so the stage drops it too rather than staging a row the
#: editor phase would silently discard.
MIN_HULL_VERTICES = 4


def staging_root(work_root: Path) -> Path:
    return Path(work_root) / "import" / FAMILY


def package_root(map_name: str) -> str:
    """`/ElysiumBaked/<map>` -- the map's own baked package folder, beside its `.umap`."""

    return f"{BAKED_MOUNT}/{map_name}"


def asset_name(map_name: str) -> str:
    return f"DA_{map_name}_Collision"


def asset_path(map_name: str) -> str:
    """`/ElysiumBaked/<map>/DA_<map>_Collision` -- the twin of `BakedMapCollision`."""

    return f"{package_root(map_name)}/{asset_name(map_name)}"


class MapCollisionStageError(RuntimeError):
    """This map's collision cannot be staged as the asset the contract describes."""


@dataclass
class StagedMapCollision:
    """One `import map-collision` run: what landed in the manifest and what refused."""

    manifest_path: Path
    maps: list[str] = field(default_factory=list)
    failures: list[tuple[str, str]] = field(default_factory=list)
    stats: dict[str, dict[str, int]] = field(default_factory=dict)

    def summary(self) -> str:
        hulls = sum(row.get("worldHulls", 0) for row in self.stats.values())
        tris = sum(row.get("displacementTriangles", 0) for row in self.stats.values())
        bodies = sum(row.get("brushBodies", 0) for row in self.stats.values())
        return (
            f"map collision staged: {len(self.maps)} map(s), {hulls} world hull(s), "
            f"{tris} displacement triangle(s), {bodies} brush body(ies), "
            f"{len(self.failures)} refused -> {self.manifest_path}"
        )


def read_hull_rows(path: Path) -> list[list[float]]:
    """`<map>.hulls`: one flat `x y z x y z ...` point cloud per line, Unreal cm.

    The acceptance rule is `UElysiumMapCollision::LoadHulls`'s, verbatim: at least four whole
    vertex triples, anything else skipped rather than staged.
    """

    rows: list[list[float]] = []
    if not Path(path).is_file():
        return rows
    with Path(path).open("r", encoding="utf-8") as handle:
        for line in handle:
            tokens = line.split()
            if len(tokens) < MIN_HULL_VERTICES * 3 or len(tokens) % 3 != 0:
                continue
            rows.append([float(token) for token in tokens])
    return rows


def read_displacement_rows(path: Path) -> list[list[float]]:
    """`<map>.dispcol`: one collision triangle per line, nine Unreal-cm floats (A, B, C)."""

    rows: list[list[float]] = []
    if not Path(path).is_file():
        return rows
    with Path(path).open("r", encoding="utf-8") as handle:
        for line in handle:
            tokens = line.split()
            if len(tokens) != 9:
                continue
            rows.append([float(token) for token in tokens])
    return rows


def read_sky_scale(path: Path) -> float:
    """The `<map>.sky` placement scale, or 1.0 when the map has no `sky_camera`.

    `FElysiumSkyDef::Parse`'s own file: `origin <x> <y> <z>` and `scale <s>`, one per line.
    """

    if not Path(path).is_file():
        return 1.0
    with Path(path).open("r", encoding="utf-8") as handle:
        for line in handle:
            tokens = line.split()
            if len(tokens) == 2 and tokens[0].lower() == "scale":
                return float(tokens[1])
    return 1.0


def brush_entity_rows(ents_path: Path, sky_scale: float) -> list[dict[str, Any]]:
    """One row per brush entity of `<map>.ents` that carries hulls, keyed by lump ordinal.

    The ordinal is the array position -- the running game's entity handle index, which is what
    `FElysiumEntityWorld::BuildBrushBody` looks the body up by -- so rows are never sorted or
    renumbered, only skipped when an entity has no hulls.

    The one transform: a `sky` row's hulls are multiplied by the map's sky scale, because
    `UElysiumMapEntities::Deserialize` (and `FElysiumEntityDefs::Parse` before it) scales a
    miniature's hulls and a cooked convex cannot be rescaled afterwards. Hulls take the scale and
    not the translation, exactly as the deserializer states it.
    """

    with Path(ents_path).open("r", encoding="ascii") as handle:
        document = json.load(handle)
    entities = document.get("entities")
    if not isinstance(entities, list):
        raise MapCollisionStageError(f"{ents_path}: no entities[] array")

    rows: list[dict[str, Any]] = []
    for index, entity in enumerate(entities):
        hulls = entity.get("hulls") or []
        if not hulls:
            continue
        scale = sky_scale if entity.get("sky") and sky_scale != 1.0 else 1.0
        staged = [
            [c * scale for c in hull] for hull in hulls
            if len(hull) >= MIN_HULL_VERTICES * 3 and len(hull) % 3 == 0
        ]
        if not staged:
            continue
        rows.append({
            "entityIndex": index,
            "classname": entity.get("classname", ""),
            "sky": bool(entity.get("sky")),
            "hulls": staged,
        })
    return rows


def displacement_soup(rows: Sequence[Sequence[float]]) -> tuple[list[float], list[int]]:
    """`<map>.dispcol` rows as one vertex buffer plus flat index triples.

    Three vertices per triangle, un-welded and in file order -- the same soup
    `UElysiumMapCollision::LoadDispCol` hands the procedural mesh, so the cooked trimesh and the
    runtime-built one take the same input.
    """

    vertices: list[float] = []
    indices: list[int] = []
    for row in rows:
        base = len(vertices) // 3
        vertices.extend(float(c) for c in row)
        indices.extend((base, base + 1, base + 2))
    return vertices, indices


def compare_geometry(
    staged: Sequence[Sequence[float]],
    source: Sequence[Sequence[float]],
    *,
    label: str,
    limit: int = 10,
) -> dict[str, Any]:
    """Row-count and value parity between staged geometry and the sidecar rows it came from.

    The staged side has been through JSON by the time this runs, which is the point: the editor
    phase reads the manifest, not this process's floats, so the comparison measures the numbers
    that will actually be authored.
    """

    mismatches: list[dict[str, Any]] = []
    total = 0
    for index in range(min(len(staged), len(source))):
        left, right = list(staged[index]), list(source[index])
        if left != right:
            total += 1
            if len(mismatches) < limit:
                mismatches.append({
                    "row": index,
                    "stagedLength": len(left),
                    "sourceLength": len(right),
                })
    return {
        "label": label,
        "rowCount": {"staged": len(staged), "source": len(source),
                     "equal": len(staged) == len(source)},
        "mismatchCount": total,
        "rowMismatches": mismatches,
        "equal": total == 0 and len(staged) == len(source),
    }


def stage_map(
    map_name: str,
    *,
    hulls_path: Path,
    dispcol_path: Path,
    ents_path: Path,
    sky_path: Path,
    verify: bool = True,
) -> dict[str, Any]:
    """One map's manifest entry: its three payloads, its asset path and its parity verdict."""

    hull_rows = read_hull_rows(hulls_path)
    if not hull_rows:
        raise MapCollisionStageError(
            f"{map_name}: no world hulls at {hulls_path}; the world collider is required, not "
            "optional (export the map first)"
        )
    disp_rows = read_displacement_rows(dispcol_path)
    if not Path(ents_path).is_file():
        raise MapCollisionStageError(
            f"{map_name}: no .ents at {ents_path}; the brush-entity bodies are keyed by its lump "
            "ordinals"
        )
    sky_scale = read_sky_scale(sky_path)
    brush_rows = brush_entity_rows(ents_path, sky_scale)
    vertices, indices = displacement_soup(disp_rows)

    entry = {
        "map": map_name,
        "packageRoot": package_root(map_name),
        "assetPath": asset_path(map_name),
        "recipeVersion": RECIPE_VERSION,
        "skyScale": sky_scale,
        "worldHulls": hull_rows,
        "displacementVertices": vertices,
        "displacementIndices": indices,
        "brushBodies": brush_rows,
    }

    parity: dict[str, Any] = {"checked": False}
    if verify:
        round_tripped = json.loads(json.dumps(entry, separators=(",", ":")))
        world = compare_geometry(round_tripped["worldHulls"], hull_rows, label="worldHulls")
        # The displacement soup is compared as the triangles it was read from, so the check covers
        # the vertex/index split as well as the values.
        staged_tris = [
            round_tripped["displacementVertices"][row * 9:(row + 1) * 9]
            for row in range(len(round_tripped["displacementIndices"]) // 3)
        ]
        disp = compare_geometry(staged_tris, disp_rows, label="displacement")
        # Brush hulls: the file's own hulls, times the sky scale for a `sky` row -- stated here a
        # second time and independently of `brush_entity_rows`, so the transform is asserted rather
        # than assumed.
        expected: list[list[float]] = []
        staged_hulls: list[list[float]] = []
        for row in round_tripped["brushBodies"]:
            staged_hulls.extend(row["hulls"])
        with Path(ents_path).open("r", encoding="ascii") as handle:
            source_entities = json.load(handle)["entities"]
        for row in brush_rows:
            source = source_entities[row["entityIndex"]]
            scale = sky_scale if source.get("sky") and sky_scale != 1.0 else 1.0
            expected.extend(
                [c * scale for c in hull] for hull in (source.get("hulls") or [])
                if len(hull) >= MIN_HULL_VERTICES * 3 and len(hull) % 3 == 0
            )
        brush = compare_geometry(staged_hulls, expected, label="brushHulls")

        parity = {
            "checked": True,
            "world": world,
            "displacement": disp,
            "brush": brush,
            "equal": world["equal"] and disp["equal"] and brush["equal"],
            "sources": {"hulls": str(hulls_path), "dispcol": str(dispcol_path),
                        "ents": str(ents_path)},
        }
        if not parity["equal"]:
            failing = [part["label"] for part in (world, disp, brush) if not part["equal"]]
            raise MapCollisionStageError(
                f"{map_name}: staged collision does not match its sidecars ({', '.join(failing)}); "
                f"world {world['rowCount']}, displacement {disp['rowCount']}, "
                f"brush {brush['rowCount']}"
            )

    entry["parity"] = parity
    entry["stats"] = {
        "worldHulls": len(hull_rows),
        "worldHullVertices": sum(len(row) // 3 for row in hull_rows),
        "displacementTriangles": len(disp_rows),
        "brushBodies": len(brush_rows),
        "brushHulls": sum(len(row["hulls"]) for row in brush_rows),
        "skyBrushBodies": sum(1 for row in brush_rows if row["sky"]),
    }
    return entry


def stage_map_collision(
    staging: Path,
    *,
    maps: Sequence[str],
    sidecar_dir: Any,
    verify: bool = True,
) -> StagedMapCollision:
    """Stage every named map and write the manifest. `maps` is required and may not be empty.

    `sidecar_dir` maps a stem to the directory its sidecars live in; the CLI passes
    `lambda stem: config.export_root / stem`.
    """

    if not maps:
        raise MapCollisionStageError(
            "import map-collision refuses to run unscoped: pass --maps <stem> (repeatable)"
        )
    staging = Path(staging)
    staging.mkdir(parents=True, exist_ok=True)
    manifest_path = staging / MANIFEST_NAME
    staged = StagedMapCollision(manifest_path=manifest_path)

    entries: list[dict[str, Any]] = []
    for map_name in maps:
        directory = Path(sidecar_dir(map_name))
        try:
            entry = stage_map(
                map_name,
                hulls_path=directory / f"{map_name}.hulls",
                dispcol_path=directory / f"{map_name}.dispcol",
                ents_path=directory / f"{map_name}.ents",
                sky_path=directory / f"{map_name}.sky",
                verify=verify,
            )
        except Exception as error:                       # noqa: BLE001 - reported, not raised
            staged.failures.append((map_name, str(error)))
            continue
        entries.append(entry)
        staged.maps.append(map_name)
        staged.stats[map_name] = entry["stats"]

    manifest = {
        "schemaVersion": MANIFEST_SCHEMA,
        "family": FAMILY,
        "mount": BAKED_MOUNT,
        "recipeVersion": RECIPE_VERSION,
        "selection": list(maps),
        "stageFailures": [{"map": name, "reason": reason} for name, reason in staged.failures],
        "maps": entries,
    }
    with manifest_path.open("w", encoding="utf-8") as handle:
        json.dump(manifest, handle, separators=(",", ":"))
    return staged
