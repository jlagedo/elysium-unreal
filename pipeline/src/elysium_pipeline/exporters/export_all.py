"""Reusable orchestration for VtMB intermediate exports.

This module deliberately contains no command-line parser.  The public
``elysium`` command owns argument handling, path configuration, cleaning,
incremental manifests, Unreal content generation, and process exit codes.
The functions here expose the existing decoders as strict library operations.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
import json
import time
import tomllib
import traceback
from typing import Any, Iterable, Sequence


PROFILES = Path(__file__).with_name("profiles.toml")


@dataclass(slots=True)
class ExportTaskResult:
    """Result of one map or global bundle operation."""

    name: str
    status: str
    seconds: float = 0.0
    error: str | None = None

    @property
    def ok(self) -> bool:
        return self.status in {"ok", "skipped"}


@dataclass(slots=True)
class ExportBatchResult:
    """Structured result returned even when independent tasks fail."""

    maps: list[ExportTaskResult] = field(default_factory=list)
    bundles: list[ExportTaskResult] = field(default_factory=list)

    @property
    def failures(self) -> list[ExportTaskResult]:
        return [item for item in (*self.maps, *self.bundles) if not item.ok]

    def require_success(self) -> "ExportBatchResult":
        if self.failures:
            detail = "; ".join(
                f"{item.name}: {item.error or item.status}" for item in self.failures
            )
            raise ExportFailed(detail, self)
        return self


class ExportFailed(RuntimeError):
    """One or more requested exports failed."""

    def __init__(self, message: str, result: ExportBatchResult):
        super().__init__(message)
        self.result = result


def load_profiles(path: Path | None = None) -> dict[str, dict[str, Any]]:
    """Load the one canonical map/profile registry."""

    with (path or PROFILES).open("rb") as handle:
        document = tomllib.load(handle)
    profiles = document.get("profiles")
    if not isinstance(profiles, dict):
        raise ValueError(f"{path or PROFILES}: missing [profiles] table")
    return profiles


def maps_for_profile(name: str, *, available: Sequence[str] | None = None) -> list[str]:
    """Resolve a named profile to patch-first map names.

    ``all``-style profiles opt into discovery rather than carrying a second
    static inventory.  Discovery remains owned by ``install.all_map_names``.
    """

    try:
        profile = load_profiles()[name]
    except KeyError as exc:
        raise KeyError(f"unknown export profile: {name}") from exc
    if profile.get("discover"):
        if available is None:
            from elysium_pipeline.formats import install

            available = install.all_map_names()
        return sorted(dict.fromkeys(available))
    maps = profile.get("maps", [])
    if not isinstance(maps, list) or not all(isinstance(item, str) for item in maps):
        raise ValueError(f"profile {name!r} has an invalid maps list")
    return list(dict.fromkeys(maps))


def bundles_for_profile(name: str) -> list[str]:
    try:
        bundles = load_profiles()[name].get("bundles", [])
    except KeyError as exc:
        raise KeyError(f"unknown export profile: {name}") from exc
    if not isinstance(bundles, list) or not all(isinstance(item, str) for item in bundles):
        raise ValueError(f"profile {name!r} has an invalid bundles list")
    return list(dict.fromkeys(bundles))


def export_maps(
    names: Iterable[str],
    *,
    out_root: Path,
    index: dict[str, Any] | None = None,
    skip_existing: bool = False,
    continue_on_error: bool = True,
    available: Sequence[str] | None = None,
) -> list[ExportTaskResult]:
    """Export named maps with one shared patch-first install index.

    ``available`` is the discovered map inventory; a profile run passes its one
    `install.all_map_names()` result rather than re-walking the install."""

    # `UE_map_sidecars`/`particles`/`weather` are imported here, in `export_maps`'s own body,
    # rather than inside `rewrite_sidecars_via_producer` below, so the incremental-build
    # fingerprint sees them: `_map_tasks` hashes `_DecoderClosures.function_entries('...export_all',
    # 'export_maps')`, which walks only this function's own AST, not a sibling function's. An
    # import hidden in `rewrite_sidecars_via_producer` would be invisible to that closure, so an
    # edit to `UE_map_sidecars` (etc.) would not invalidate the incremental cache.
    from elysium_pipeline.exporters import UE_bsp_to_scene, UE_map_sidecars
    from elysium_pipeline.formats import install, particles, weather

    requested = list(dict.fromkeys(names))
    available = set(available if available is not None else install.all_map_names())
    shared_index = index if index is not None else install.build_index()
    results: list[ExportTaskResult] = []

    for position, name in enumerate(requested, 1):
        started = time.monotonic()
        if name not in available:
            result = ExportTaskResult(
                name=name,
                status="failed",
                error=f"no map {name!r} in the patch-first install",
            )
            results.append(result)
            if not continue_on_error:
                break
            continue
        out_dir = out_root / name
        if skip_existing and (out_dir / f"{name}.obj").is_file():
            results.append(ExportTaskResult(name=name, status="skipped"))
            continue
        print(f"\n[{position}/{len(requested)}] {name} ...", flush=True)
        try:
            out_dir.mkdir(parents=True, exist_ok=True)
            legacy_report = UE_bsp_to_scene.main(
                install.map_path(name),
                out_dir,
                index=shared_index,
            )
            rewrite_sidecars_via_producer(
                name, out_dir, shared_index, legacy_report,
                sidecars_module=UE_map_sidecars, particles_module=particles,
                weather_module=weather,
            )
            results.append(
                ExportTaskResult(
                    name=name,
                    status="ok",
                    seconds=time.monotonic() - started,
                )
            )
        except (Exception, SystemExit) as exc:
            traceback.print_exc()
            results.append(
                ExportTaskResult(
                    name=name,
                    status="failed",
                    seconds=time.monotonic() - started,
                    error=str(exc) or type(exc).__name__,
                )
            )
            if not continue_on_error:
                break
    return results


def rewrite_sidecars_via_producer(
    name: str,
    out_dir: Path,
    index: dict[str, Any],
    legacy_report: dict[str, Any] | None,
    *,
    sidecars_module: Any = None,
    particles_module: Any = None,
    weather_module: Any = None,
) -> dict[str, Any]:
    """R3.5: the R3.2 producer is the default source of the eight legacy sidecars.

    `UE_bsp_to_scene.main` has just written `.ents`/`.hulls`/`.dispcol`/`.lights`/`.env`/`.sky`/
    `.spawn`/`.ropes` into ``out_dir`` (plus `.obj`/`.mtl`/`.props`/`.decals`/`.water`,
    which the producer does not reproduce and this call leaves alone). `UE_map_sidecars.write_sidecars`
    (`docs/architecture/seam_map_map.md` -> "Producer join") overwrites the eight it does own with
    its own bytes, reading the map's published V2 units rather than the BSP.

    `.weather`/`.particles` read `.ents` back off disk, so they are re-run here against the
    producer's `.ents` instead of the legacy one `UE_bsp_to_scene.main` already used and discarded
    -- see `seam_migration.md` -> "Roadmap -- one pipeline" R3.5. Weather's mesh-derived inputs
    (`cover_triangles`/bounds) are geometry, not entity data, so `UE_bsp_to_scene.main` hands them
    back in ``legacy_report`` rather than this call recomputing them.

    `UE_bsp_to_scene.py` itself is not deleted, and every sidecar it writes internally is still
    written exactly as before -- the only change there is one additive `return` at the end of
    `main`, handing back the geometry `.weather` needs. It is only unwired from this default path
    (deletion is R8, once the 108-map differ has run against it).

    ``sidecars_module``/``particles_module``/``weather_module`` let `export_maps` hand in the
    modules it already imported in its own body -- see the comment there -- rather than this
    function re-importing them itself; a direct caller (a test) may omit them and get the same
    lazily-imported singleton modules.
    """

    if sidecars_module is None:
        from elysium_pipeline.exporters import UE_map_sidecars as sidecars_module
    if particles_module is None or weather_module is None:
        from elysium_pipeline.formats import particles as _particles, weather as _weather
        particles_module = particles_module or _particles
        weather_module = weather_module or _weather

    producer_report = sidecars_module.write_sidecars(name, out_dir=out_dir)

    with open(out_dir / f"{name}.ents", encoding="utf-8") as ents_file:
        entity_document = json.load(ents_file)
    particles_path = particles_module.write_particles(name, out_dir, entity_document, index)
    if particles_path:
        print(f"wrote {particles_path}")

    weather_inputs = (legacy_report or {}).get("weather_inputs")
    if weather_inputs:
        cover_triangles, bounds_min, bounds_max = weather_inputs
        weather_path = weather_module.write_weather(
            name, out_dir, entity_document, index, cover_triangles, bounds_min, bounds_max
        )
        if weather_path:
            print(f"weather: re-pointed at the producer's .ents -> {weather_path.name}")

    return producer_report


def _run_bundle(
    name: str,
    *,
    maps: Sequence[str],
    force: bool,
    index: dict[str, Any] | None,
) -> None:
    """Invoke one existing global exporter without duplicating its policy."""

    if name == "audio":
        from elysium_pipeline.exporters import UE_extract_sounds

        # The shared index cannot serve audio: its loose walk omits `sound/`, so the sound
        # exporter builds its own SOUND_DIRS index or a patch's loose sound overrides vanish.
        UE_extract_sounds.main(list(maps))
    elif name == "particles":
        from elysium_pipeline.exporters import UE_extract_particles

        UE_extract_particles.main(force=force, index=index)
    elif name == "scripts":
        from elysium_pipeline.exporters import UE_extract_scripts

        UE_extract_scripts.main(force=force, index=index)
    elif name == "signs":
        from elysium_pipeline.exporters import UE_extract_signs

        UE_extract_signs.main(force=force, index=index)
    elif name == "vdata":
        from elysium_pipeline.exporters import UE_extract_vdata

        UE_extract_vdata.main(force=force, index=index)
    elif name == "corpus":
        from elysium_pipeline.exporters import UE_extract_corpus

        UE_extract_corpus.main(index=index, force=force)
    elif name == "items":
        from elysium_pipeline.exporters import UE_extract_items, UE_extract_wield

        UE_extract_items.main(index=index, force=force)
        UE_extract_wield.main(index=index, force=force)
    elif name == "cfg":
        from elysium_pipeline.exporters import UE_extract_cfg

        UE_extract_cfg.main(force=force, index=index)
    elif name == "scenes":
        from elysium_pipeline.exporters import UE_extract_scenes

        UE_extract_scenes.main(force=force, index=index)
    elif name == "ui":
        from elysium_pipeline.exporters import UE_extract_ui

        UE_extract_ui.main(force=force, index=index)
    elif name == "npc":
        from elysium_pipeline.exporters import npc_export

        npc_export.main(index=index, strict=True)
    else:
        raise KeyError(f"unknown export bundle: {name}")


def export_bundles(
    names: Iterable[str],
    *,
    maps: Sequence[str] = (),
    force: bool = False,
    index: dict[str, Any] | None = None,
    continue_on_error: bool = True,
) -> list[ExportTaskResult]:
    """Run global mirrors once each and return strict structured results."""

    results: list[ExportTaskResult] = []
    for name in dict.fromkeys(names):
        started = time.monotonic()
        print(f"\n[{name}] exporting global bundle ...", flush=True)
        try:
            _run_bundle(
                name,
                maps=maps,
                force=force,
                index=index,
            )
            results.append(
                ExportTaskResult(
                    name=name,
                    status="ok",
                    seconds=time.monotonic() - started,
                )
            )
        except (Exception, SystemExit) as exc:
            traceback.print_exc()
            results.append(
                ExportTaskResult(
                    name=name,
                    status="failed",
                    seconds=time.monotonic() - started,
                    error=str(exc) or type(exc).__name__,
                )
            )
            if not continue_on_error:
                break
    return results


def export_profile(
    name: str,
    *,
    out_root: Path,
    force: bool = False,
    continue_on_error: bool = True,
) -> ExportBatchResult:
    """Export one complete offline profile.

    Unreal policy packages and map baking intentionally remain separate: they
    run in editor processes and are coordinated by the public CLI.
    """

    from elysium_pipeline.formats import install

    available = install.all_map_names()
    map_names = maps_for_profile(name, available=available)
    shared_index = install.build_index()
    result = ExportBatchResult()
    result.maps = export_maps(
        map_names,
        out_root=out_root,
        index=shared_index,
        skip_existing=False,
        continue_on_error=continue_on_error,
        available=available,
    )
    completed = [item.name for item in result.maps if item.ok]
    if continue_on_error or not result.failures:
        result.bundles = export_bundles(
            bundles_for_profile(name),
            maps=completed,
            force=force,
            index=shared_index,
            continue_on_error=continue_on_error,
        )
    return result
