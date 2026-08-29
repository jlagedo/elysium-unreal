"""Blender add-on packaging and the corpus review harness.

The add-on under `tools/elysium_glb_review` is self-contained and works without any of
this; these are the conveniences that keep the build, install and sweep commands in one
place rather than in a reviewer's shell history.

The sweep itself needs no Blender at all. The add-on's `core` package imports no `bpy`
precisely so a full pass over the corpus can run in this process.
"""

from __future__ import annotations

import importlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

from .reporting import ExitCode

ADDON_RELATIVE = Path("tools") / "elysium_glb_review"
ADDON_ID = "elysium_glb_review"

#: Repository the add-on installs into. Extensions are addressed by
#: `bl_ext.<repository>.<id>`, so this name is part of the module path.
REPOSITORY = "elysium"

#: Checked in order when locating the Blender executable.
EXECUTABLE_VARIABLES = ("ELYSIUM_BLENDER", "BLENDER")
KNOWN_EXECUTABLES = (
    Path("D:/blender/blender.exe"),
    Path("C:/Program Files/Blender Foundation/Blender 5.2/blender.exe"),
)


class BlenderError(RuntimeError):
    """Blender is unavailable or refused the request."""


def executable() -> Path:
    """Locate the Blender executable, or say plainly that it could not be found."""
    for name in EXECUTABLE_VARIABLES:
        value = os.environ.get(name, "").strip()
        if value and Path(value).is_file():
            return Path(value)
    found = shutil.which("blender")
    if found:
        return Path(found)
    for candidate in KNOWN_EXECUTABLES:
        if candidate.is_file():
            return candidate
    raise BlenderError(
        "no Blender executable found; set ELYSIUM_BLENDER to its full path"
    )


def addon_source(config) -> Path:
    path = config.repo_root / ADDON_RELATIVE
    if not (path / "blender_manifest.toml").is_file():
        raise BlenderError("add-on source is not at %s" % path)
    return path


def _work(config) -> Path:
    """Where build products go. Never inside the repository."""
    destination = config.work_root / "glb_review"
    destination.mkdir(parents=True, exist_ok=True)
    return destination


def _run(runner, arguments: list[str]) -> None:
    runner.run(arguments, check=True, category=int(ExitCode.DEPENDENCY_OR_TOOLCHAIN))


def build(config, runner) -> Path:
    """Validate the manifest and build the extension archive."""
    source = addon_source(config)
    output = _work(config) / "dist"
    output.mkdir(parents=True, exist_ok=True)
    blender = executable()

    _run(runner, [str(blender), "--command", "extension", "validate", str(source)])
    _run(
        runner,
        [str(blender), "--command", "extension", "build",
         "--source-dir", str(source), "--output-dir", str(output)],
    )

    archives = sorted(output.glob("%s-*.zip" % ADDON_ID))
    if not archives:
        raise BlenderError("build produced no archive in %s" % output)
    return archives[-1]


def _repositories(blender: Path) -> set[str]:
    """Extension repository ids Blender already knows about."""
    result = subprocess.run(
        [str(blender), "--command", "extension", "repo-list"],
        capture_output=True,
        text=True,
        check=False,
    )
    return {
        line.rstrip(":")
        for line in result.stdout.splitlines()
        if line and not line[0].isspace() and line.rstrip().endswith(":")
    }


def install(config, runner, *, archive: Path | None = None) -> str:
    """Install the add-on into a named repository and enable it.

    Returns the module name the glTF importer will see, which depends on the repository
    and so must not be assumed anywhere else.
    """
    archive = archive or build(config, runner)
    blender = executable()

    # repo-add does not update an existing repository; called again it adds a second
    # one named Elysium.001, and the next install lands somewhere new. Blender chooses
    # the directory itself, under the user's extensions root.
    if REPOSITORY not in _repositories(blender):
        subprocess.run(
            [str(blender), "--command", "extension", "repo-add", REPOSITORY,
             "--name", "Elysium"],
            capture_output=True,
            text=True,
            check=False,
        )
    _run(
        runner,
        [str(blender), "--command", "extension", "install-file",
         "-r", REPOSITORY, "-e", str(archive)],
    )
    return "bl_ext.%s.%s" % (REPOSITORY, ADDON_ID)


def review(config, runner, target: Path | None = None) -> None:
    """Open Blender with the add-on enabled, optionally importing one unit."""
    blender = executable()
    module = "bl_ext.%s.%s" % (REPOSITORY, ADDON_ID)
    script = (
        "import bpy;"
        "bpy.ops.preferences.addon_enable(module=%r);" % module
    )
    if target is not None:
        script += "bpy.ops.import_scene.gltf(filepath=%r)" % str(target)
    # Not --factory-startup: that discards the extension repository the add-on lives in.
    _run(runner, [str(blender), "--python-expr", script])


def _load_core(config):
    """Import the add-on's Blender-free core into this process."""
    source = addon_source(config)
    if str(source) not in sys.path:
        sys.path.insert(0, str(source))
    return importlib.import_module("core.report")


def corpus_report(config, *, write: bool = True) -> tuple[str, dict, Path | None]:
    """Sweep the corpus once and return its summary, its data and where it was written."""
    report = _load_core(config)
    root = config.export_v2_root
    if root is None or not root.is_dir():
        raise BlenderError("corpus root does not exist: %s" % root)

    result = report.scan(root)
    data = report.to_dict(result)
    destination: Path | None = None
    if write:
        destination = _work(config) / "corpus_report.json"
        destination.write_text(
            json.dumps(data, indent=2, sort_keys=True), encoding="utf-8"
        )
    return report.summary(result), data, destination
