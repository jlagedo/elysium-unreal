"""Stage one map's environment for the `UElysiumMapEnvironment` asset (R4.4).

`uv run elysium import map-environment --maps <map>...` turns each named map's `<map>.env`,
`<map>.sky` and `<map>.spawn` sidecars into one `/ElysiumBaked/Maps/<map>/DA_<map>_Environment` asset
carrying the same values -- the 2D-sky flag and its two fog sets, the 3D-skybox miniature's
placement transform, and the initial player spawn (`docs/architecture/seam_map_map.md` -> "Import —
environment"). This module is the offline stage half: it reads the three sidecars verbatim, asserts
parity against them, and writes one `manifest.json` the editor phase
(`pipeline/unreal/import_map_environment.py`) executes.

**The rows come from the sidecars, not from a re-derivation of them.** Since R3.5 those files are
written by the R3.2 producer straight off the BSP entity lump (`UE_map_sidecars.write_environment` /
`write_sky` / `write_spawn`), so this stage is a pure carry: every number the asset gets is the
number the sidecar already states.

**Scope.** The stage refuses to run unscoped: there is no `--all` here, matching `map-entities` and
`map-collision` -- one asset per map over 108 maps is a separately approved operation.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import json
from pathlib import Path
from typing import Any, Sequence

#: The lane's own name -- the staging directory below `$ELYSIUM_WORK_ROOT/import/` and the recipe
#: stage label the editor phase fingerprints under.
FAMILY = "map_environment"

#: Manifest schema the editor phase understands. Bumped when the row shape changes.
MANIFEST_SCHEMA = "1.0.0"

#: The name of the staged manifest, and of the report the editor phase writes beside it.
MANIFEST_NAME = "manifest.json"
IMPORT_REPORT_NAME = "import_report.json"

#: Bumped whenever this lane's mapping changes in a way that must re-author every asset.
RECIPE_VERSION = 1

#: The mount every per-map asset lands under. The C++ twin is
#: `FElysiumContentPaths::BakedMapDir` / `BakedMapEnvironment`.
BAKED_MOUNT = "/ElysiumBaked"


def staging_root(work_root: Path) -> Path:
    return Path(work_root) / "import" / FAMILY


def package_root(map_name: str) -> str:
    """`/ElysiumBaked/Maps/<map>` -- the map's own baked package folder, beside its `.umap`."""

    from elysium_pipeline.asset_paths import map_package
    return map_package(map_name)


def asset_name(map_name: str) -> str:
    return f"DA_{map_name}_Environment"


def asset_path(map_name: str) -> str:
    """`/ElysiumBaked/Maps/<map>/DA_<map>_Environment` -- the twin of `BakedMapEnvironment`."""

    return f"{package_root(map_name)}/{asset_name(map_name)}"


class MapEnvironmentStageError(RuntimeError):
    """This map's environment cannot be staged as the asset the contract describes."""


@dataclass
class StagedMapEnvironment:
    """One `import map-environment` run: what landed in the manifest and what refused."""

    manifest_path: Path
    maps: list[str] = field(default_factory=list)
    failures: list[tuple[str, str]] = field(default_factory=list)

    def summary(self) -> str:
        return (
            f"map environment staged: {len(self.maps)} map(s), "
            f"{len(self.failures)} refused -> {self.manifest_path}"
        )


def read_env(path: Path) -> dict[str, Any]:
    """`<map>.env`: the space-separated `key value...` lines `FElysiumEnvDef::Parse` reads.

    Absent file: the all-default row `FElysiumEnvDef::Parse`'s own failure leaves the struct at.
    """

    out: dict[str, Any] = {
        "sky": False, "skyName": "", "skyConvention": 0,
        "fog": False, "fogColor": [0.0, 0.0, 0.0], "fogStart": 0.0, "fogEnd": 0.0,
        "skyFog": False, "skyFogColor": [0.0, 0.0, 0.0], "skyFogStart": 0.0, "skyFogEnd": 0.0,
    }
    if not Path(path).is_file():
        return out
    with Path(path).open("r", encoding="utf-8") as handle:
        for line in handle:
            tok = line.split()
            if not tok:
                continue
            key = tok[0]
            if key == "skybox" and len(tok) >= 2:
                out["sky"] = tok[1] == "1"
            elif key == "skyname" and len(tok) >= 2:
                out["skyName"] = tok[1]
            elif key == "skyconv" and len(tok) >= 2:
                out["skyConvention"] = int(tok[1])
            elif key == "fog" and len(tok) >= 2:
                out["fog"] = tok[1] == "1"
            elif key == "fogcolor" and len(tok) >= 4:
                out["fogColor"] = [float(tok[1]), float(tok[2]), float(tok[3])]
            elif key == "fogstart" and len(tok) >= 2:
                out["fogStart"] = float(tok[1])
            elif key == "fogend" and len(tok) >= 2:
                out["fogEnd"] = float(tok[1])
            elif key == "skyfog" and len(tok) >= 2:
                out["skyFog"] = tok[1] == "1"
            elif key == "skyfogcolor" and len(tok) >= 4:
                out["skyFogColor"] = [float(tok[1]), float(tok[2]), float(tok[3])]
            elif key == "skyfogstart" and len(tok) >= 2:
                out["skyFogStart"] = float(tok[1])
            elif key == "skyfogend" and len(tok) >= 2:
                out["skyFogEnd"] = float(tok[1])
    return out


def read_sky(path: Path) -> dict[str, Any]:
    """`<map>.sky`: the 3D-skybox miniature's origin and scale, absent on a map with no miniature.

    `FElysiumSkyDef::Parse`'s own contract: `origin <x> <y> <z>` and `scale <s>`, and a zero or
    negative scale is refused (the identity, `hasMiniature: false`), matching the C++ reader.
    """

    if not Path(path).is_file():
        return {"hasMiniature": False, "origin": [0.0, 0.0, 0.0], "scale": 1.0}
    origin = [0.0, 0.0, 0.0]
    scale = 1.0
    with Path(path).open("r", encoding="utf-8") as handle:
        for line in handle:
            tok = line.split()
            if len(tok) >= 4 and tok[0] == "origin":
                origin = [float(tok[1]), float(tok[2]), float(tok[3])]
            elif len(tok) >= 2 and tok[0] == "scale":
                scale = float(tok[1])
    if scale <= 0.0:
        return {"hasMiniature": False, "origin": [0.0, 0.0, 0.0], "scale": 1.0}
    return {"hasMiniature": True, "origin": origin, "scale": scale}


def read_spawn(path: Path) -> dict[str, Any]:
    """`<map>.spawn`: `info_player_start`'s origin (feet) and yaw, absent on a backdrop-only map."""

    if not Path(path).is_file():
        return {"hasSpawn": False, "origin": [0.0, 0.0, 0.0], "yaw": 0.0}
    origin = [0.0, 0.0, 0.0]
    yaw = 0.0
    has_origin = False
    with Path(path).open("r", encoding="utf-8") as handle:
        for line in handle:
            tok = line.split()
            if len(tok) == 4 and tok[0] == "origin":
                origin = [float(tok[1]), float(tok[2]), float(tok[3])]
                has_origin = True
            elif len(tok) == 2 and tok[0] == "yaw":
                yaw = float(tok[1])
    if not has_origin:
        return {"hasSpawn": False, "origin": [0.0, 0.0, 0.0], "yaw": 0.0}
    return {"hasSpawn": True, "origin": origin, "yaw": yaw}


def compare_scalars(staged: dict[str, Any], source: dict[str, Any], *, label: str) -> dict[str, Any]:
    """Field-for-field parity between a staged row and the sidecar row it was read from."""

    mismatches = [key for key in source if staged.get(key) != source.get(key)]
    return {"label": label, "mismatches": mismatches, "equal": not mismatches}


def stage_map(
    map_name: str,
    *,
    env_path: Path,
    sky_path: Path,
    spawn_path: Path,
    verify: bool = True,
) -> dict[str, Any]:
    """One map's manifest entry: its three sidecars' values, its asset path and its parity verdict."""

    env = read_env(env_path)
    sky = read_sky(sky_path)
    spawn = read_spawn(spawn_path)

    entry = {
        "map": map_name,
        "packageRoot": package_root(map_name),
        "assetPath": asset_path(map_name),
        "recipeVersion": RECIPE_VERSION,
        "env": env,
        "sky": sky,
        "spawn": spawn,
    }

    parity: dict[str, Any] = {"checked": False}
    if verify:
        round_tripped = json.loads(json.dumps(entry, separators=(",", ":")))
        env_part = compare_scalars(round_tripped["env"], env, label="env")
        sky_part = compare_scalars(round_tripped["sky"], sky, label="sky")
        spawn_part = compare_scalars(round_tripped["spawn"], spawn, label="spawn")
        parity = {
            "checked": True,
            "env": env_part,
            "sky": sky_part,
            "spawn": spawn_part,
            "equal": env_part["equal"] and sky_part["equal"] and spawn_part["equal"],
            "sources": {"env": str(env_path), "sky": str(sky_path), "spawn": str(spawn_path)},
        }
        if not parity["equal"]:
            failing = [part["label"] for part in (env_part, sky_part, spawn_part)
                       if not part["equal"]]
            raise MapEnvironmentStageError(
                f"{map_name}: staged environment does not match its sidecars "
                f"({', '.join(failing)})"
            )

    entry["parity"] = parity
    entry["stats"] = {
        "hasSkyMiniature": sky["hasMiniature"],
        "hasSpawn": spawn["hasSpawn"],
        "skybox": env["sky"],
        "fog": env["fog"],
        "skyFog": env["skyFog"],
    }
    return entry


def stage_map_environment(
    staging: Path,
    *,
    maps: Sequence[str],
    sidecar_dir: Any,
    verify: bool = True,
) -> StagedMapEnvironment:
    """Stage every named map and write the manifest. `maps` is required and may not be empty.

    `sidecar_dir` maps a stem to the directory its sidecars live in; the CLI passes
    `lambda stem: config.export_root / stem`.
    """

    if not maps:
        raise MapEnvironmentStageError(
            "import map-environment refuses to run unscoped: pass --maps <stem> (repeatable)"
        )
    staging = Path(staging)
    staging.mkdir(parents=True, exist_ok=True)
    manifest_path = staging / MANIFEST_NAME
    staged = StagedMapEnvironment(manifest_path=manifest_path)

    entries: list[dict[str, Any]] = []
    for map_name in maps:
        directory = Path(sidecar_dir(map_name))
        try:
            entry = stage_map(
                map_name,
                env_path=directory / f"{map_name}.env",
                sky_path=directory / f"{map_name}.sky",
                spawn_path=directory / f"{map_name}.spawn",
                verify=verify,
            )
        except Exception as error:                       # noqa: BLE001 - reported, not raised
            staged.failures.append((map_name, str(error)))
            continue
        entries.append(entry)
        staged.maps.append(map_name)

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
