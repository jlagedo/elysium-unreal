"""Stage one map's entity table for the `UElysiumMapEntities` data asset (R4.1).

`uv run elysium bake map --maps <map>...` turns each named map's published GLB units
into one `/ElysiumBaked/<map>/DA_<map>_Entities` asset carrying the same rows the `<map>.ents`
document carries, in the same order.
This module is the offline stage half: it runs the R3.2 producer's own entity join
(`exporters.UE_map_sidecars.build_entities`), asserts def-count and per-index parity against the
`.ents` file the asset replaces, and writes one `manifest.json` the editor phase
(`pipeline/unreal/bake_map_entities.py`) executes. `uv run elysium bake map` runs both halves in
one command; 0018 story 21-2 retired the `import map-entities` that used to be its own.

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

#: The name of the staged manifest the editor phase reads.
MANIFEST_NAME = "manifest.json"

#: Bumped whenever this lane's mapping changes in a way that must re-author every asset.
RECIPE_VERSION = 2   # 2: R6.4 `cull_max_cm` rides the row

#: The mount every per-map asset lands under. The C++ twin is
#: `FElysiumContentPaths::BakedMapDir` / `BakedMapEntities`.
BAKED_MOUNT = "/ElysiumBaked"


def staging_root(work_root: Path) -> Path:
    return Path(work_root) / "import" / FAMILY


def package_root(map_name: str) -> str:
    """`/ElysiumBaked/Maps/<map>` -- the map's own baked package folder, beside its `.umap`."""

    from elysium_pipeline.asset_paths import map_package
    return map_package(map_name)


def asset_name(map_name: str) -> str:
    return f"DA_{map_name}_Entities"


def asset_path(map_name: str) -> str:
    """`/ElysiumBaked/Maps/<map>/DA_<map>_Entities` -- the twin of `FElysiumContentPaths::BakedMapEntities`."""

    return f"{package_root(map_name)}/{asset_name(map_name)}"


class MapEntityStageError(RuntimeError):
    """This map's entity table cannot be staged as the asset the contract describes."""


@dataclass
class StagedMapEntities:
    """One entity-table staging run: what landed in the manifest and what refused."""

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


def stage_map(
    map_name: str,
    *,
    export_v2_root: Path | None = None,
) -> dict[str, Any]:
    """One map's manifest entry: its rows and its asset path.

    **The parity check against `<map>.ents` is gone** (0018 story 21-4). R4.1 introduced it to
    prove the cooked asset equalled the file it replaced, and it was a real assertion while the
    sidecar was the BSP decoder's. Since R3.5 the sidecar is written by `producer.write_sidecars`
    from the same `prepare_join` + `build_entities` this function calls, so the comparison had
    become the join against itself -- and since 21-4 the bake writes that sidecar itself moments
    earlier, which would have made it a comparison against a file this very run produced. What
    the flags the join reads may diverge on is 21-7's, and it is measured there against retail
    rather than against another copy of the same reading.
    """

    join = producer.prepare_join(map_name, Path(export_v2_root) if export_v2_root else None)
    rows, stats = producer.build_entities(
        join.units, join.sky, join.pair_blocks, join.brush_meshes
    )
    return {
        "map": map_name,
        "packageRoot": package_root(map_name),
        "assetPath": asset_path(map_name),
        "recipeVersion": RECIPE_VERSION,
        "stats": stats,
        "entities": rows,
    }


def stage_map_entities(
    export_v2_root: Path,
    staging: Path,
    *,
    maps: Sequence[str],
) -> StagedMapEntities:
    """Stage every named map and write the manifest. `maps` is required and may not be empty."""

    if not maps:
        raise MapEntityStageError(
            "the entity-table stage refuses to run unscoped: pass --maps <stem> (repeatable)"
        )
    staging = Path(staging)
    staging.mkdir(parents=True, exist_ok=True)
    manifest_path = staging / MANIFEST_NAME
    staged = StagedMapEntities(manifest_path=manifest_path)

    entries: list[dict[str, Any]] = []
    for map_name in maps:
        try:
            entry = stage_map(map_name, export_v2_root=export_v2_root)
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
