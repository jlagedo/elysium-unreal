"""Stage one map's collision for the `UElysiumMapCollisionPayload` asset (R4.2).

`uv run elysium bake map --maps <map>...` turns each named map's collision sidecars
into one `/ElysiumBaked/<map>/DA_<map>_Collision` asset carrying the same convex sets and the same
triangle soup, cooked once offline instead of on every map load. This module is the offline
stage half: it reads
`<map>.hulls`, `<map>.dispcol` and the brush-entity `hulls` of `<map>.ents`, applies the one
transform the runtime applies (the 3D-skybox scale on a `sky` brush entity), asserts parity against
the files it read, and writes one `manifest.json` the editor phase
(`pipeline/unreal/bake_map_collision.py`) executes. `uv run elysium bake map` runs both halves
in one command; 0018 story 21-2 retired the `import map-collision` that used to be its own.

**The rows come from the sidecars, not from a second port of the hull solver.** Since R3.5 those
files are written by the R3.2 producer from the published GLB units, so re-deriving them here would
mean a second implementation of `brush_hull` with its own tolerances -- exactly the divergence the
producer-join rule exists to prevent.

**Scope.** The stage refuses to run unscoped: there is no `--all` here, because one asset per map
over 108 maps is a separately approved operation, not this lane's working mode.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import json
from pathlib import Path
from typing import Any, Sequence

from elysium_pipeline.formats import contents_signature

#: The lane's own name -- the staging directory below `$ELYSIUM_WORK_ROOT/import/` and the recipe
#: stage label the editor phase fingerprints under.
FAMILY = "map_collision"

#: Manifest schema the editor phase understands. Bumped when the row shape changes.
MANIFEST_SCHEMA = "2.0.0"

#: The name of the staged manifest the editor phase reads.
MANIFEST_NAME = "manifest.json"

#: Bumped whenever this lane's mapping changes in a way that must re-author every asset.
RECIPE_VERSION = 2

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
    """`/ElysiumBaked/Maps/<map>` -- the map's own baked package folder, beside its `.umap`."""

    from elysium_pipeline.asset_paths import map_package
    return map_package(map_name)


def asset_name(map_name: str) -> str:
    return f"DA_{map_name}_Collision"


def asset_path(map_name: str) -> str:
    """`/ElysiumBaked/Maps/<map>/DA_<map>_Collision` -- the twin of `BakedMapCollision`."""

    return f"{package_root(map_name)}/{asset_name(map_name)}"


class MapCollisionStageError(RuntimeError):
    """This map's collision cannot be staged as the asset the contract describes."""


@dataclass
class StagedMapCollision:
    """One collision staging run: what landed in the manifest and what refused."""

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
    """Every staged world hull, in file order, regardless of signature.

    Kept as the parity subject -- the check compares what was staged against what the file holds,
    and that is simplest over one flat list. `read_hull_partitions` is what the payload authors
    from.
    """

    return [hull for _signature, hulls in read_hull_partitions(path) for hull in hulls]


def read_hull_partitions(path: Path) -> list[tuple[int, list[list[float]]]]:
    """`<map>.hulls` grouped by contents signature, each group in file order.

    The acceptance rule was `UElysiumMapCollision::LoadHulls`'s (deleted with the sidecar
    transport, 0018 story 21) and is kept verbatim here: a contents word then at
    least four whole vertex triples, anything else skipped rather than staged. A row answering no
    mask was never written and cannot become a body.

    Groups are ordered by first appearance, so the payload's bodies land in a stable order that
    does not depend on how a dict happens to iterate.
    """

    groups: dict[int, list[list[float]]] = {}
    if not Path(path).is_file():
        return []
    with Path(path).open("r", encoding="utf-8") as handle:
        for line in handle:
            tokens = line.split()
            if len(tokens) < MIN_HULL_VERTICES * 3 + 1 or len(tokens) % 3 != 1:
                continue
            if not tokens[0].startswith("0x"):
                continue
            signature = contents_signature.signature_of(int(tokens[0], 16))
            if not signature:
                continue
            groups.setdefault(signature, []).append([float(token) for token in tokens[1:]])
    return list(groups.items())


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

    **A 3D-skybox brush entity is not composed into the collision at all** (R7.4, G25). Its hulls
    are authored in miniature units and the one transform this stage used to apply was the map's
    sky scale, because `UElysiumMapEntities::Deserialize` (and `FElysiumEntityDefs::Parse` before
    it) scales a miniature's hulls and a cooked convex cannot be rescaled afterwards. Measured on
    `sm_pier_1`: that scale (x16) carries `brush_8/9/10`, sky-flagged `func_brush` Solids, from raw
    z ~= 4939 -- above the map's own `world_maxs.z 512`, where nothing in VtMB can reach them -- to
    world z -644..-628, three invisible slabs 21 inches under the harbour surface. The miniature is
    drawn from its own camera and no body ever enters it, so its collider is a port artefact with no
    VtMB counterpart, and the sky scale has nothing left to apply to. The producer already drops the
    same hulls at the source (`UE_map_sidecars.build_entities`); this reads the row's own `sky` flag
    so a `.ents` written before that rule still stages the same collision. `sky_scale` stays on the
    signature and on the manifest entry -- it is the map's own number and the editor phase records
    it -- but no staged hull is multiplied by anything any more.
    """

    with Path(ents_path).open("r", encoding="ascii") as handle:
        document = json.load(handle)
    entities = document.get("entities")
    if not isinstance(entities, list):
        raise MapCollisionStageError(f"{ents_path}: no entities[] array")

    rows: list[dict[str, Any]] = []
    for index, entity in enumerate(entities):
        hulls = entity.get("hulls") or []
        if not hulls or entity.get("sky"):
            continue
        # Per-hull contents, parallel to `hulls`. A mover answers the retail masks by its own
        # brushes, so the body's signature is the OR of its kept brushes' signatures rather than
        # the entity's class: a door blocks sight and both pawns through MOVEABLE, a glass
        # `func_brush` blocks neither pawn's sight. A `.ents` without the column is refused: it
        # predates the contents seam and its bodies would answer every mask wrongly (story 21).
        per_hull = entity.get("hull_contents") or []
        staged: list[list[float]] = []
        signature = 0
        for position, hull in enumerate(hulls):
            if len(hull) < MIN_HULL_VERTICES * 3 or len(hull) % 3 != 0:
                continue
            staged.append(list(hull))
            if position < len(per_hull):
                signature |= contents_signature.signature_of(int(per_hull[position]))
        if not staged:
            continue
        if not per_hull:
            raise ValueError(
                f"brush entity {index} ({entity.get('classname', '')!r}) carries no "
                "`hull_contents`; re-export this map's `.ents`"
            )
        rows.append({
            "entityIndex": index,
            "classname": entity.get("classname", ""),
            "sky": bool(entity.get("sky")),
            "signature": signature,
            "hulls": staged,
        })
    return rows


def _sky_brush_bodies(ents_path: Path) -> int:
    """How many brush entities G25 kept out of the collision -- the excluded miniatures."""

    with Path(ents_path).open("r", encoding="ascii") as handle:
        entities = json.load(handle).get("entities") or []
    return sum(1 for entity in entities if entity.get("sky") and (entity.get("hulls") or []))


def displacement_soup(rows: Sequence[Sequence[float]]) -> tuple[list[float], list[int]]:
    """`<map>.dispcol` rows as one vertex buffer plus flat index triples.

    Three vertices per triangle, un-welded and in file order -- the same soup
    `UElysiumMapCollision::LoadDispCol` handed the procedural mesh before story 21 deleted it;
    the cooked trimesh keeps that soup's shape, so it and the
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


#: The signature a priced pedestrian volume wears: `0x2000` and NO clip bit -- the brushes that
#: actually reach a link. A brush also carrying MONSTERCLIP stops `InitLinks`' own walk test, so
#: no ground link is ever built across it and its `0x2000` never reaches one; on `sm_hub_1` 455 of
#: 461 flagged links cross one of the 9 clip-free boxes and 0 of 1,185 unflagged links does
#: (`navigation-jump-links.md`).
PEDESTRIAN_SIGNATURE = contents_signature.signature_of(0x2000)


def nav_area_rows(partitions: Sequence[tuple[int, list[list[float]]]]) -> list[dict[str, Any]]:
    """The hull rows that get a nav-area mark, by area. Only the roadway, for now."""

    rows: list[dict[str, Any]] = []
    for signature, hulls in partitions:
        if signature != PEDESTRIAN_SIGNATURE:
            continue
        rows.append({"area": "pedestrian", "signature": signature, "hulls": hulls})
    return rows


def nav_door_rows(map_name: str, brush_rows: Sequence[dict[str, Any]],
                  ents_path: Path) -> dict[str, Any]:
    """Every door and whether retail's graph runs through it, or an empty answer with a reason.

    A map whose nav graph has not been exported yet stages no door answer rather than guessing:
    cutting every door because the graph is missing would wall off the ones NPCs use, and that is
    exactly the failure the door rule exists to prevent.
    """

    from elysium_pipeline.importers import map_nav_doors

    try:
        block = map_nav_doors.load_graph_block(map_name)
    except (FileNotFoundError, ValueError) as error:
        return {"doors": 0, "traversable": 0, "cut": 0, "rows": [],
                "skipped": f"no usable nav graph: {error}"}

    with Path(ents_path).open("r", encoding="ascii") as handle:
        entities = json.load(handle).get("entities") or []
    origins = {index: entity["origin"] for index, entity in enumerate(entities)
               if isinstance(entity.get("origin"), list) and len(entity["origin"]) == 3}
    return map_nav_doors.stage(block, brush_rows, origins, map_nav_doors.hull_radius_cm)


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

    partitions = read_hull_partitions(hulls_path)
    # Only the seven signatures the game actually ships have a generated collision profile. A
    # profile name that DefaultEngine.ini does not declare falls back to the engine default
    # silently, so a brush answering an eighth combination would stand wearing a profile that
    # answers the wrong masks -- and nothing downstream would say so. The generator's own note
    # promises this refusal; this is it.
    for signature, _hulls in partitions:
        spelled = contents_signature.spell(signature)
        if spelled not in contents_signature.SHIPPED:
            raise MapCollisionStageError(
                f"{map_name}: signature {spelled} has no generated collision profile "
                f"(shipped: {', '.join(contents_signature.SHIPPED)}); re-run "
                "`uv run elysium research gen_contents_signatures` if this map needs it")
    hull_rows = [hull for _signature, hulls in partitions for hull in hulls]
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
        # One entry per contents signature the map carries, each `{signature, hulls}`. The flat
        # `worldHulls` stays beside it as the parity subject and as what a reader that predates
        # the partition still understands.
        "worldBodies": [
            {"signature": signature, "hulls": hulls} for signature, hulls in partitions
        ],
        "worldHulls": hull_rows,
        "displacementVertices": vertices,
        "displacementIndices": indices,
        "brushBodies": brush_rows,
        # Which hull rows are a priced roadway, and which doors the graph runs through. Staged
        # here because this lane is the one holding both the hulls and their signatures; a second
        # lane computing it could disagree with the solids it is describing.
        "navAreas": nav_area_rows(partitions),
        "navDoors": nav_door_rows(map_name, brush_rows, ents_path),
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
        # Brush hulls: the file's own hulls, verbatim, for every row that is not a 3D-skybox
        # miniature (G25) -- stated here a second time and independently of `brush_entity_rows`, so
        # the rule is asserted rather than assumed.
        expected: list[list[float]] = []
        staged_hulls: list[list[float]] = []
        for row in round_tripped["brushBodies"]:
            staged_hulls.extend(row["hulls"])
        with Path(ents_path).open("r", encoding="ascii") as handle:
            source_entities = json.load(handle)["entities"]
        for row in brush_rows:
            source = source_entities[row["entityIndex"]]
            assert not source.get("sky")            # `brush_entity_rows` staged no miniature
            expected.extend(
                list(hull) for hull in (source.get("hulls") or [])
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
        # G25: staged rows are never miniatures now, so this counts what the rule excluded rather
        # than what it scaled -- the number to watch if a map ever loses collision it should keep.
        "skyBrushBodies": sum(1 for row in brush_rows if row["sky"]),
        "skyBrushBodiesExcluded": _sky_brush_bodies(ents_path),
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
            "the collision stage refuses to run unscoped: pass --maps <stem> (repeatable)"
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
