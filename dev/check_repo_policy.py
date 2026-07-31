#!/usr/bin/env python3
"""Enforce the repository's authored-code/prohibited-artifact boundary."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import subprocess
import sys


REPO = Path(__file__).resolve().parent.parent

FORBIDDEN_EXTENSIONS = {
    ".uasset", ".umap", ".ubulk", ".uexp", ".uptnl",
    ".pak", ".ucas", ".utoc",
    ".bsp", ".mdl", ".phy", ".vpk", ".tth", ".ttz", ".vtf", ".vmt",
    ".vcd", ".dlg", ".sav",
    ".gpr", ".gzf", ".gdt",
    ".dll", ".exe", ".pdb", ".lib", ".obj",
    ".orig", ".bak", ".pid", ".log",
}
FORBIDDEN_PREFIXES = (
    ".codex-patch/",
    "logs/",
    "scratch/",
    "tools/out/",
    "tools/ghidra",
    "tools/re/",
    "reference/",
    "sdk/",
    "game/",
    "extracted/",
    "decompilation/",
    "Content/VtMB/",
    "Plugins/ElysiumBaked/Content/",
    "research/evidence/",
    "research/generated/",
    "research/captures/",
    "research/reports/",
    "research/models/",
)
AUTHORED_EXTENSIONS = {
    ".py", ".ps1", ".java", ".cpp", ".c", ".h", ".hpp",
    ".cs", ".md", ".json", ".yaml", ".yml",
}
IGNORED_AUTHORED_ALLOWLIST = (
    ".claude/",
    ".vs/",
    ".vscode/",
    "Binaries/",
    "Content/VtMB/",
    "DerivedDataCache/",
    "Intermediate/",
    "pipeline/.venv/",
    "Plugins/External/",
    "Plugins/ElysiumBaked/Content/",
    "Saved/",
    "Source/ElysiumUE/ThirdParty/CPython27/",
)


def git(*args: str, check: bool = True) -> str:
    result = subprocess.run(
        ["git", "-C", str(REPO), *args],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if check and result.returncode:
        raise RuntimeError(result.stderr.strip() or f"git {' '.join(args)} failed")
    return result.stdout


def normalized(path: str) -> str:
    path = path.strip().replace("\\", "/")
    return path[2:] if path.startswith("./") else path


def prohibited(path: str) -> str | None:
    path = normalized(path)
    lower = path.lower()
    suffix = PurePosixPath(lower).suffix
    if suffix in FORBIDDEN_EXTENSIONS:
        return f"forbidden extension {suffix}"
    for prefix in FORBIDDEN_PREFIXES:
        if lower.startswith(prefix.lower()):
            return f"forbidden path {prefix}"
    return None


def tracked_violations() -> list[str]:
    errors: list[str] = []
    for raw in git("ls-files", "-z").split("\0"):
        if not raw:
            continue
        if reason := prohibited(raw):
            errors.append(f"tracked: {raw} ({reason})")
    attributes = (REPO / ".gitattributes").read_text(
        encoding="utf-8", errors="replace"
    ) if (REPO / ".gitattributes").is_file() else ""
    if "filter=lfs" in attributes:
        errors.append("Git LFS is not an exception for prohibited assets")
    return errors


def ignored_authored_warnings() -> list[str]:
    warnings: list[str] = []
    output = git("ls-files", "--others", "--ignored", "--exclude-standard", "-z")
    for raw in output.split("\0"):
        path = normalized(raw)
        if not path or path.endswith("/"):
            continue
        lower = path.lower()
        if (
            lower == "content/elysium.umap"
            or any(lower.startswith(prefix.lower()) for prefix in IGNORED_AUTHORED_ALLOWLIST)
        ):
            continue
        if reason := prohibited(path):
            warnings.append(f"prohibited ignored file outside a managed output: {path} ({reason})")
            continue
        if PurePosixPath(lower).suffix in AUTHORED_EXTENSIONS:
            warnings.append(f"authored-looking ignored file: {path}")
    return warnings


def history_violations() -> list[str]:
    errors: list[str] = []
    for line in git("rev-list", "--objects", "--all").splitlines():
        parts = line.split(" ", 1)
        if len(parts) != 2:
            continue
        path = normalized(parts[1])
        if reason := prohibited(path):
            errors.append(f"history: {path} ({reason})")
    return sorted(set(errors))


def workspace_warnings() -> list[str]:
    warnings: list[str] = []
    lock = json.loads((REPO / "dev" / "dependencies.lock.json").read_text(encoding="utf-8"))
    locked_plugins = {item["name"]: item for item in lock["plugins"]}
    required = ("ELYSIUM_UE_ROOT", "ELYSIUM_VTMB_ROOT", "ELYSIUM_WORK_ROOT")
    for name in required:
        raw = os.environ.get(name, "").strip()
        if not raw:
            warnings.append(f"{name} is not configured in the current environment")
        elif not Path(raw).is_dir():
            warnings.append(f"{name} does not exist: {raw}")
    for plugin in ("Cog", "glTFRuntime"):
        root = REPO / "Plugins" / "External" / plugin
        marker = root / ".elysium-managed.json"
        if not marker.is_file():
            warnings.append(f"managed plugin is missing: {plugin}; run bootstrap")
            continue
        data = json.loads(marker.read_text(encoding="utf-8-sig"))
        expected = locked_plugins[plugin]
        if (
            data.get("revision") != expected["revision"]
            or data.get("post_patch_tree") != expected["post_patch_tree"]
        ):
            warnings.append(f"managed plugin does not match the lock: {plugin}; run bootstrap")
            continue
        lines = []
        for path in (
            p for p in root.rglob("*")
            if p.is_file()
            and p != marker
            and not set(p.relative_to(root).parts).intersection({"Binaries", "Intermediate"})
        ):
            relative = path.relative_to(root).as_posix()
            lines.append(f"{relative}={hashlib.sha256(path.read_bytes()).hexdigest()}")
        lines.sort()
        actual = hashlib.sha256(("\n".join(lines) + "\n").encode()).hexdigest()
        if not data.get("content_hash") or actual != data["content_hash"]:
            warnings.append(f"managed plugin is stale or modified: {plugin}; run bootstrap")
    generated = (
        REPO / "Content" / "Elysium.umap",
        REPO / "Content" / "VtMB" / "Materials" / "M_World_Opaque.uasset",
    )
    for path in generated:
        if not path.is_file():
            warnings.append(f"generated project package is missing: {path.relative_to(REPO)}; run content")
    baked = REPO / "Plugins" / "ElysiumBaked" / "Content"
    if not baked.is_dir() or not any(baked.rglob("*.umap")):
        warnings.append("no generated baked map package is present; run bake <map>")
    return warnings


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--history", action="store_true", help="also audit every reachable Git object")
    parser.add_argument("--repo-only", action="store_true", help="skip local workspace prerequisites")
    args = parser.parse_args()

    errors = tracked_violations()
    warnings = ignored_authored_warnings()
    if args.history:
        errors.extend(history_violations())
    if not args.repo_only:
        warnings.extend(workspace_warnings())

    for warning in warnings:
        print(f"WARNING: {warning}")
    for error in errors:
        print(f"ERROR: {error}")
    if errors:
        print(f"repo policy failed: {len(errors)} error(s), {len(warnings)} warning(s)")
        return 1
    print(f"repo policy passed ({len(warnings)} warning(s))")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
