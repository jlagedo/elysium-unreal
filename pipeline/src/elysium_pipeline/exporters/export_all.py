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


def _run_bundle(
    name: str,
    *,
    maps: Sequence[str],
    force: bool,
    index: dict[str, Any] | None,
) -> None:
    """Invoke one existing global exporter without duplicating its policy.

    `maps` is threaded through but consumed by no bundle: the only one that read per-map export
    products was `audio`, retired in AUD0.4 with `UE_extract_sounds.py`.
    """

    if name == "particles":
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
    elif name == "cfg":
        from elysium_pipeline.exporters import UE_extract_cfg

        UE_extract_cfg.main(force=force, index=index)
    elif name == "scenes":
        from elysium_pipeline.exporters import UE_extract_scenes

        UE_extract_scenes.main(force=force, index=index)
    elif name == "ui":
        from elysium_pipeline.exporters import UE_extract_ui

        UE_extract_ui.main(force=force, index=index)
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
    """Export one complete offline profile: its bundles.

    A profile used to decode its map list first. 0018 story 21-5 deleted the BSP decoder, so a
    profile carries no map half at all -- a map is published as `export_v2` units and goes to a
    level through `bake map`. Unreal policy packages and map baking remain separate: they run in
    editor processes and are coordinated by the public CLI.
    """

    from elysium_pipeline.formats import install

    result = ExportBatchResult()
    result.bundles = export_bundles(
        bundles_for_profile(name),
        maps=[],
        force=force,
        index=install.build_index(),
        continue_on_error=continue_on_error,
    )
    return result
