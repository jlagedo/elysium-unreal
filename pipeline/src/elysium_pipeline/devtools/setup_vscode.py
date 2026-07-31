#!/usr/bin/env python3
"""Generate the local VS Code C/C++ IntelliSense configuration for Elysium-Unreal.

Two steps:

1. Run UnrealBuildTool's `-projectfiles -vscode` generator. That writes
   `.vscode/c_cpp_properties.json`, `.vscode/compileCommands_<Project>.json` and one
   `.rsp` per module — the compile database cpptools uses for every `.cpp`.
2. Mirror the main module's include paths and forced includes into
   `.vscode/settings.json` as `C_Cpp.default.*`. cpptools falls back to those for any
   file the compile database does not name — every header, i.e. most of the tree.

Step 2 is a separate file on purpose: UBT overwrites `c_cpp_properties.json` and the
`.code-workspace` on every regeneration, but never touches `settings.json`.

`.vscode/` is gitignored; re-run this script after adding a module dependency, a
plugin, or a source file that IntelliSense does not resolve.

    dev/elysium.ps1 ide vscode             # generate project files, then settings
    dev/elysium.ps1 ide vscode --no-ubt    # settings only, from the existing .rsp
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path
from elysium_pipeline.paths import repo_root

REPO = repo_root()

# Folders that are build output or regenerable content: noise in search results, and
# more file watchers than the editor should carry.
EXCLUDE_DIRS = [
    "Binaries",
    "DerivedDataCache",
    "Intermediate",
    "Saved",
    ".vs",
]


def find_uproject() -> Path:
    projects = sorted(REPO.glob("*.uproject"))
    if not projects:
        sys.exit(f"[vscode] no .uproject found in {REPO}")
    return projects[0]


def find_ue_root() -> Path:
    """UE install path from the shared local configuration."""
    env = os.environ.get("ELYSIUM_UE_ROOT")
    if env:
        return Path(env)
    sys.exit("[vscode] ELYSIUM_UE_ROOT is not configured")


def run_ubt(ue_root: Path, uproject: Path) -> None:
    ubt = ue_root / "Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe"
    if not ubt.is_file():
        sys.exit(f"[vscode] UnrealBuildTool not found at {ubt}")
    cmd = [
        str(ubt),
        "-projectfiles",
        f"-project={uproject}",
        "-game",
        "-engine",
        "-dotnet",
        "-vscode",
    ]
    print("[vscode] generating project files...")
    result = subprocess.run(cmd, cwd=REPO)
    if result.returncode != 0:
        sys.exit(f"[vscode] UnrealBuildTool failed ({result.returncode})")


def load_compile_db(project_name: str) -> list[dict]:
    db_path = REPO / ".vscode" / f"compileCommands_{project_name}.json"
    if not db_path.is_file():
        sys.exit(f"[vscode] {db_path} not found — run without --no-ubt first")
    return json.loads(db_path.read_text(encoding="utf-8"))


def main_module_rsp(db: list[dict]) -> tuple[Path, str]:
    """The response file and compiler of the first entry under Source/ — the game module."""
    source_root = os.path.normcase(str(REPO / "Source"))
    for entry in db:
        if os.path.normcase(entry["file"]).startswith(source_root):
            rsp = entry["arguments"][-1].lstrip("@")
            return Path(rsp), entry["arguments"][0]
    sys.exit("[vscode] compile database has no entry under Source/")


def parse_rsp(rsp: Path) -> tuple[list[str], list[str]]:
    """Pull the /I include directories and /FI forced includes out of a UBT response file."""
    text = rsp.read_text(encoding="utf-8", errors="replace")
    includes = re.findall(r'^/I\s+"([^"]+)"', text, re.MULTILINE)
    forced = re.findall(r'^/FI\s+"([^"]+)"', text, re.MULTILINE)
    return includes, forced


def load_settings(path: Path) -> dict:
    if not path.is_file():
        return {}
    raw = path.read_text(encoding="utf-8")
    # VS Code allows // comments in settings.json; strip whole-line ones before parsing.
    stripped = "\n".join(line for line in raw.splitlines() if not line.lstrip().startswith("//"))
    try:
        return json.loads(stripped) if stripped.strip() else {}
    except json.JSONDecodeError as exc:
        sys.exit(f"[vscode] {path} is not valid JSON ({exc}); fix or delete it and re-run")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--no-ubt", action="store_true", help="skip project-file generation")
    args = parser.parse_args()

    uproject = find_uproject()
    project_name = uproject.stem

    if not args.no_ubt:
        run_ubt(find_ue_root(), uproject)

    db = load_compile_db(project_name)
    rsp, compiler = main_module_rsp(db)
    includes, forced = parse_rsp(rsp)
    print(f"[vscode] {len(db)} compile-database entries; {rsp.name}: "
          f"{len(includes)} include dirs, {len(forced)} forced includes")

    for path in forced:
        if not Path(path).is_file():
            print(f"[vscode] WARNING missing forced include (build the editor target first): {path}")

    settings_path = REPO / ".vscode" / "settings.json"
    settings = load_settings(settings_path)

    settings["C_Cpp.default.compilerPath"] = compiler
    settings["C_Cpp.default.cStandard"] = "c17"
    settings["C_Cpp.default.cppStandard"] = "c++20"
    settings["C_Cpp.default.intelliSenseMode"] = "windows-msvc-x64"
    settings["C_Cpp.default.includePath"] = includes
    settings["C_Cpp.default.forcedInclude"] = forced
    settings["C_Cpp.default.browse.path"] = includes
    settings["C_Cpp.default.browse.limitSymbolsToIncludedHeaders"] = False
    settings["C_Cpp.autoAddFileAssociations"] = False
    settings["files.associations"] = {
        "*.h": "cpp",
        "*.inl": "cpp",
        "*.usf": "hlsl",
        "*.ush": "hlsl",
        "*.uproject": "json",
        "*.uplugin": "json",
    }
    settings["search.exclude"] = {f"**/{d}": True for d in EXCLUDE_DIRS} | {
        "Plugins/External": True,
        "Content/VtMB": True,
    }
    settings["files.watcherExclude"] = {f"**/{d}/**": True for d in EXCLUDE_DIRS} | {
        "**/Plugins/External/**": True,
        "**/Content/VtMB/**": True,
    }
    # The .slnx files are UE-generated; stop the C# extension from trying to load one.
    settings["dotnet.defaultSolution"] = "disable"

    settings_path.parent.mkdir(exist_ok=True)
    settings_path.write_text(json.dumps(settings, indent=4) + "\n", encoding="utf-8")
    print(f"[vscode] wrote {settings_path}")


if __name__ == "__main__":
    main()
