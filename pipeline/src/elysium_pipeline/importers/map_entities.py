"""Stage one map's entity table for the `UElysiumMapEntities` data asset (R4.1).

`uv run elysium import map-entities --maps <map>...` turns each named map's published GLB units
into one `/ElysiumBaked/<map>/DA_<map>_Entities` asset carrying the same rows the `<map>.ents`
document carries, in the same order (`docs/architecture/seam_map_map_entities.md` -> "Import").
This module is the offline stage half: it runs the R3.2 producer's own entity join
(`exporters.UE_map_sidecars.build_entities`), asserts def-count and per-index parity against the
`.ents` file the asset replaces, and writes one `manifest.json` the editor phase
(`pipeline/unreal/import_map_entities.py`) executes.

**Parity is a stage failure, not a warning.** The asset is a transport change and nothing else, so
a row that does not equal the sidecar's row -- by count, by field set or by value -- stops the run
and names the index and the field. Since R3.5 the `.ents` on disk is written by this same producer,
so this check proves the freshly derived rows and the shipped file agree (staleness, a flipped R3.4
divergence flag, a JSON round-trip loss); the two-reader parity that proves the C++ asset
deserializer and the C++ JSON parser produce the same defs is `Elysium.Content.MapEntities.*`.

**Scope.** The stage refuses to run unscoped: there is no `--all` here, because one asset per map
over 108 maps is a separately approved operation, not this lane's working mode.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import json
from pathlib import Path
from typing import Any, Sequence

from elysium_pipeline.exporters import UE_map_sidecars as producer

#: The lane's own name -- the staging directory below `$ELYSIUM_WORK_ROOT/import/` and the recipe
#: stage label the editor phase fingerprints under.
FAMILY = "map_entities"

#: Manifest schema the editor phase understands. Bumped when the row shape changes.
MANIFEST_SCHEMA = "1.0.0"

#: The name of the staged manifest, and of the report the editor phase writes beside it.
MANIFEST_NAME = "manifest.json"
IMPORT_REPORT_NAME = "import_report.json"

#: Bumped whenever this lane's mapping changes in a way that must re-author every asset.
RECIPE_VERSION = 2   # 2: R6.4 `cull_max_cm` rides the row

#: The mount every per-map asset lands under. The C++ twin is
#: `FElysiumContentPaths::BakedMapDir` / `BakedMapEntities`.
BAKED_MOUNT = "/ElysiumBaked"


def staging_root(work_root: Path) -> Path:
    return Path(work_root) / "import" / FAMILY


def package_root(map_name: str) -> str:
    """`/ElysiumBaked/<map>` -- the map's own baked package folder, beside its `.umap`."""

    return f"{BAKED_MOUNT}/{map_name}"


def asset_name(map_name: str) -> str:
    return f"DA_{map_name}_Entities"


def asset_path(map_name: str) -> str:
    """`/ElysiumBaked/<map>/DA_<map>_Entities` -- the twin of `FElysiumContentPaths::BakedMapEntities`."""

    return f"{package_root(map_name)}/{asset_name(map_name)}"


class MapEntityStageError(RuntimeError):
    """This map's entity table cannot be staged as the asset the contract describes."""


@dataclass
class StagedMapEntities:
    """One `import map-entities` run: what landed in the manifest and what refused."""

    manifest_path: Path
    maps: list[str] = field(default_factory=list)
    failures: list[tuple[str, str]] = field(default_factory=list)
    stats: dict[str, dict[str, int]] = field(default_factory=dict)

    def summary(self) -> str:
        entities = sum(row.get("entities", 0) for row in self.stats.values())
        return (
            f"map entities staged: {len(self.maps)} map(s), {entities} entity row(s), "
            f"{len(self.failures)} refused -> {self.manifest_path}"
        )


def sidecar_entities(ents_path: Path) -> list[dict[str, Any]]:
    """The `entities[]` rows of a `<map>.ents` document on disk."""

    with Path(ents_path).open("r", encoding="ascii") as handle:
        document = json.load(handle)
    rows = document.get("entities")
    if not isinstance(rows, list):
        raise MapEntityStageError(f"{ents_path}: no entities[] array")
    return rows


def compare_rows(
    staged: Sequence[dict[str, Any]],
    sidecar: Sequence[dict[str, Any]],
    *,
    limit: int = 10,
) -> dict[str, Any]:
    """Def-count and per-index parity between the staged rows and the `.ents` rows they replace.

    Index parity is by lump ordinal, which is the running game's entity handle and a save key, so
    the comparison never sorts, matches by name or tolerates a shift: row `i` is compared with row
    `i` and nothing else. Values are compared exactly -- both sides are the same producer's own
    numbers, one of them through a JSON round-trip, and the asset stores them at the same width.
    """

    mismatches: list[dict[str, Any]] = []
    total = 0
    for index in range(min(len(staged), len(sidecar))):
        left, right = staged[index], sidecar[index]
        for key in sorted(set(left) | set(right)):
            if key not in left:
                detail = {"index": index, "field": key, "staged": None, "sidecar": right[key]}
            elif key not in right:
                detail = {"index": index, "field": key, "staged": left[key], "sidecar": None}
            elif left[key] != right[key]:
                detail = {"index": index, "field": key, "staged": left[key], "sidecar": right[key]}
            else:
                continue
            total += 1
            if len(mismatches) < limit:
                mismatches.append(detail)
    return {
        "defCount": {"staged": len(staged), "sidecar": len(sidecar),
                     "equal": len(staged) == len(sidecar)},
        "mismatchCount": total,
        "indexMismatches": mismatches,
        "equal": total == 0 and len(staged) == len(sidecar),
    }


def stage_map(
    map_name: str,
    *,
    export_v2_root: Path | None = None,
    ents_path: Path | None = None,
    verify: bool = True,
) -> dict[str, Any]:
    """One map's manifest entry: its rows, its asset path and its parity verdict.

    `ents_path` is the `<map>.ents` the asset replaces; when it is not given the caller has already
    resolved it (`FElysiumContentPaths::MapEnts`'s offline twin is
    `$ELYSIUM_EXPORT_ROOT/<map>/<map>.ents`, which `paths` owns).
    """

    join = producer.prepare_join(map_name, Path(export_v2_root) if export_v2_root else None)
    rows, stats = producer.build_entities(
        join.units, join.sky, join.pair_blocks, join.brush_meshes
    )

    parity: dict[str, Any] = {"checked": False}
    if verify:
        if ents_path is None or not Path(ents_path).is_file():
            raise MapEntityStageError(
                f"{map_name}: no .ents at {ents_path} to assert parity against; the asset must be "
                "provably equal to the file it replaces (export the map first)"
            )
        # The staged rows go through JSON before they are compared, because that is what the
        # sidecar's own rows went through: comparing a live float against a re-read one would
        # measure the round-trip rather than the join.
        round_tripped = json.loads(json.dumps(rows, separators=(",", ":")))
        parity = compare_rows(round_tripped, sidecar_entities(Path(ents_path)))
        parity["checked"] = True
        parity["sidecar"] = str(ents_path)
        if not parity["equal"]:
            head = parity["indexMismatches"][:3]
            raise MapEntityStageError(
                f"{map_name}: staged rows do not match {ents_path} "
                f"({parity['defCount']['staged']} vs {parity['defCount']['sidecar']} rows, "
                f"{parity['mismatchCount']} field mismatch(es)); first: {head}"
            )

    return {
        "map": map_name,
        "packageRoot": package_root(map_name),
        "assetPath": asset_path(map_name),
        "recipeVersion": RECIPE_VERSION,
        "stats": stats,
        "parity": parity,
        "entities": rows,
    }


def stage_map_entities(
    export_v2_root: Path,
    staging: Path,
    *,
    maps: Sequence[str],
    ents_for: Any = None,
    verify: bool = True,
) -> StagedMapEntities:
    """Stage every named map and write the manifest. `maps` is required and may not be empty.

    `ents_for` maps a stem to its `<map>.ents` path; the CLI passes
    `lambda stem: config.export_root / stem / f"{stem}.ents"`.
    """

    if not maps:
        raise MapEntityStageError(
            "import map-entities refuses to run unscoped: pass --maps <stem> (repeatable)"
        )
    staging = Path(staging)
    staging.mkdir(parents=True, exist_ok=True)
    manifest_path = staging / MANIFEST_NAME
    staged = StagedMapEntities(manifest_path=manifest_path)

    entries: list[dict[str, Any]] = []
    for map_name in maps:
        try:
            entry = stage_map(
                map_name,
                export_v2_root=export_v2_root,
                ents_path=Path(ents_for(map_name)) if ents_for else None,
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
