"""Typed configuration and path resolution for project tooling."""

from __future__ import annotations

from dataclasses import dataclass
import json
import os
from pathlib import Path
from typing import Mapping


class ConfigError(RuntimeError):
    """A required local path is missing or invalid."""


def _repository_root() -> Path:
    return Path(__file__).resolve().parents[3]


def _read_local_environment(path: Path) -> dict[str, str]:
    """Read the intentionally small KEY=VALUE local configuration format."""

    values: dict[str, str] = {}
    if not path.is_file():
        return values
    for line_number, raw_line in enumerate(
        path.read_text(encoding="utf-8-sig").splitlines(), 1
    ):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            raise ConfigError(f"{path}:{line_number}: expected NAME=VALUE")
        name, value = line.split("=", 1)
        name = name.strip()
        value = value.strip()
        if not name:
            raise ConfigError(f"{path}:{line_number}: environment name is empty")
        if len(value) >= 2 and value[0] == value[-1] and value[0] in {"'", '"'}:
            value = value[1:-1]
        values[name] = value
    return values


def _configured_path(
    argument: Path | None,
    name: str,
    environment: Mapping[str, str],
    local: Mapping[str, str],
) -> Path | None:
    raw = argument if argument is not None else environment.get(name) or local.get(name)
    if raw is None or not str(raw).strip():
        return None
    return Path(raw).expanduser().resolve()


def _detect_unreal(environment: Mapping[str, str]) -> Path | None:
    """Find the exact UE 5.8 launcher installation without guessing versions."""

    program_data = environment.get("ProgramData", "").strip()
    if not program_data:
        return None
    manifest = (
        Path(program_data)
        / "Epic"
        / "UnrealEngineLauncher"
        / "LauncherInstalled.dat"
    )
    if not manifest.is_file():
        return None
    try:
        data = json.loads(manifest.read_text(encoding="utf-8-sig"))
    except (OSError, ValueError):
        return None
    for entry in data.get("InstallationList", []):
        if entry.get("AppName") != "UE_5.8" and entry.get("ArtifactId") != "UE_5.8":
            continue
        location = entry.get("InstallLocation")
        if location:
            candidate = Path(location).expanduser().resolve()
            if candidate.is_dir():
                return candidate
    return None


def _require_directory(path: Path | None, name: str, required: bool) -> None:
    if not required:
        return
    if path is None:
        raise ConfigError(
            f"{name} is not configured; copy dev/paths.example.env to "
            ".elysium.local.env and set the local path"
        )
    if not path.is_dir():
        raise ConfigError(f"{name} does not exist or is not a directory: {path}")


#: One checkout's own UnrealBuildTool settings. UBT reads project scope last, so this file
#: overrides the machine-wide ``%APPDATA%\Unreal Engine`` configuration that every engine copy
#: shares. An installed engine's own ``Engine/Saved`` configuration is skipped entirely.
UNREAL_BUILD_CONFIG = Path("Saved") / "UnrealBuildTool" / "BuildConfiguration.xml"


def validate_engine_root(path: Path) -> Path:
    """Reject an engine root that cannot build before a checkout is wired to it."""

    root = Path(path).expanduser().resolve()
    if not root.is_dir():
        raise ConfigError(f"engine root does not exist or is not a directory: {root}")
    for relative in (
        Path("Engine/Build/BatchFiles/Build.bat"),
        Path("Engine/Binaries/Win64/UnrealEditor.exe"),
    ):
        if not (root / relative).is_file():
            raise ConfigError(
                f"{root} is not an Unreal installation: {relative.as_posix()} is missing"
            )
    # An installed engine ships UnrealBuildTool's rules assembly prebuilt and refuses to
    # regenerate it. A copy taken without Engine/Intermediate therefore passes every other
    # check and only fails on its first build, so the copy is rejected here instead.
    if (root / "Engine/Build/InstalledBuild.txt").is_file():
        rules = Path("Engine/Intermediate/Build/BuildRules/UE5Rules.dll")
        if not (root / rules).is_file():
            raise ConfigError(
                f"{root} is an installed engine whose precompiled rules assembly is "
                f"missing ({rules.as_posix()}); copy Engine/Intermediate from the source "
                "installation before using it as a build lane"
            )
    return root


def default_build_parallelism(concurrent_builds: int = 3) -> int:
    """One checkout's share of the machine when that many engine copies build at once."""

    if concurrent_builds < 1:
        raise ConfigError(f"concurrent builds must be positive: {concurrent_builds}")
    return max(2, (os.cpu_count() or 1) // concurrent_builds)


def write_build_parallelism(project_root: Path, max_parallel_actions: int) -> Path:
    """Pin one checkout's UnrealBuildTool action count so parallel builds do not thrash.

    ``MaxParallelActions`` short-circuits every other core heuristic, including the
    machine-wide ``bAllCores``, and applies to the UBA, parallel, hybrid and local executors.
    """

    if max_parallel_actions < 1:
        raise ConfigError(
            f"max parallel actions must be positive: {max_parallel_actions}"
        )
    path = Path(project_root) / UNREAL_BUILD_CONFIG
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = (
        '<?xml version="1.0" encoding="utf-8" ?>',
        '<Configuration xmlns="https://www.unrealengine.com/BuildConfiguration">',
        "\t<BuildConfiguration>",
        f"\t\t<MaxParallelActions>{max_parallel_actions}</MaxParallelActions>",
        "\t</BuildConfiguration>",
        "</Configuration>",
        "",
    )
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    return path


def _prepend_environment_path(name: str, entries: list[Path]) -> None:
    current = os.environ.get(name, "")
    parts = [os.fspath(path) for path in entries]
    if current:
        parts.append(current)
    os.environ[name] = os.pathsep.join(parts)


@dataclass(frozen=True, slots=True)
class ProjectConfig:
    """Resolved project configuration shared by every public command."""

    repo_root: Path
    project: Path
    game_root: Path | None
    work_root: Path | None
    export_root: Path | None
    ue_root: Path | None
    unreal_zen_data_path: Path | None
    unreal_local_data_cache_path: Path | None
    unreal_shader_work_root: Path | None
    temp_root: Path | None

    @classmethod
    def resolve(
        cls,
        game: Path | None,
        work: Path | None,
        ue: Path | None,
        export: Path | None = None,
        require_game: bool = False,
        require_work: bool = False,
        require_ue: bool = False,
    ) -> "ProjectConfig":
        """Resolve CLI argument, process environment, local file, then UE detection."""

        repo = _repository_root()
        local = _read_local_environment(repo / ".elysium.local.env")
        environment = os.environ
        game_root = _configured_path(game, "ELYSIUM_VTMB_ROOT", environment, local)
        work_root = _configured_path(work, "ELYSIUM_WORK_ROOT", environment, local)
        export_root = _configured_path(
            export, "ELYSIUM_EXPORT_ROOT", environment, local
        )
        if export_root is None and work_root is not None:
            export_root = work_root / "exports"
        ue_root = _configured_path(ue, "ELYSIUM_UE_ROOT", environment, local)
        if ue_root is None:
            ue_root = _detect_unreal(environment)
        unreal_zen_data_path = _configured_path(
            None, "UE-ZenDataPath", environment, local
        )
        unreal_local_data_cache_path = _configured_path(
            None, "UE-LocalDataCachePath", environment, local
        )
        unreal_shader_work_root = _configured_path(
            None, "ELYSIUM_UNREAL_SHADER_WORK_ROOT", environment, local
        )
        temp_root = _configured_path(None, "ELYSIUM_TEMP_ROOT", environment, local)

        _require_directory(game_root, "ELYSIUM_VTMB_ROOT", require_game)
        _require_directory(work_root, "ELYSIUM_WORK_ROOT", require_work)
        _require_directory(ue_root, "ELYSIUM_UE_ROOT", require_ue)
        for path, name in (
            (unreal_zen_data_path, "UE-ZenDataPath"),
            (unreal_local_data_cache_path, "UE-LocalDataCachePath"),
            (unreal_shader_work_root, "ELYSIUM_UNREAL_SHADER_WORK_ROOT"),
            (temp_root, "ELYSIUM_TEMP_ROOT"),
        ):
            _require_directory(path, name, path is not None)
        return cls(
            repo_root=repo,
            project=repo / "ElysiumUE.uproject",
            game_root=game_root,
            work_root=work_root,
            export_root=export_root,
            ue_root=ue_root,
            unreal_zen_data_path=unreal_zen_data_path,
            unreal_local_data_cache_path=unreal_local_data_cache_path,
            unreal_shader_work_root=unreal_shader_work_root,
            temp_root=temp_root,
        )

    @property
    def log_root(self) -> Path | None:
        return self.work_root / "logs" if self.work_root else None

    @property
    def cache_root(self) -> Path | None:
        return self.work_root / "cache" if self.work_root else None

    def apply_environment(self) -> None:
        """Expose resolved paths to legacy modules and Unreal editor Python."""

        values = {
            "ELYSIUM_UE_ROOT": self.ue_root,
            "ELYSIUM_VTMB_ROOT": self.game_root,
            "ELYSIUM_WORK_ROOT": self.work_root,
            "ELYSIUM_EXPORT_ROOT": self.export_root,
            "UE-ZenDataPath": self.unreal_zen_data_path,
            "UE-LocalDataCachePath": self.unreal_local_data_cache_path,
            "ELYSIUM_UNREAL_SHADER_WORK_ROOT": self.unreal_shader_work_root,
            "ELYSIUM_TEMP_ROOT": self.temp_root,
        }
        for name, value in values.items():
            if value is not None:
                os.environ[name] = os.fspath(value)
        if self.temp_root is not None:
            os.environ["TEMP"] = os.fspath(self.temp_root)
            os.environ["TMP"] = os.fspath(self.temp_root)
        python_roots = [self.repo_root, self.repo_root / "pipeline" / "src"]
        _prepend_environment_path("PYTHONPATH", python_roots)
        _prepend_environment_path("UE_PYTHONPATH", python_roots)
