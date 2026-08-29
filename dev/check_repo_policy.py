#!/usr/bin/env python3
"""Enforce the repository's authored-code/prohibited-artifact boundary."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys


REPO = Path(__file__).resolve().parent.parent

UNREAL_PACKAGE_EXTENSIONS = {
    ".uasset", ".umap", ".ubulk", ".uexp", ".uptnl",
}
AUTHORED_UNREAL_PREFIXES = (
    "Content/ElysiumAuthored/",
)
AUTHORED_UNREAL_LFS_PATTERNS = {
    f"Content/ElysiumAuthored/**/*{suffix}"
    for suffix in UNREAL_PACKAGE_EXTENSIONS
}
FORBIDDEN_EXTENSIONS = {
    *UNREAL_PACKAGE_EXTENSIONS,
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
    "Content/ElysiumGenerated/",
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
# Managed outputs: ignored, regenerable package roots a generator owns end to end. The first two
# are game-derived; the rest are original project content whose tracked source is the generator
# under pipeline/unreal/ (and, for a captured graph, its .t3d text). A package here is expected to
# be absent until it is built and to be overwritten wholesale when it is.
GENERATED_PACKAGE_ROOTS = (
    "Plugins/ElysiumBaked/Content/",
    "Content/ElysiumGenerated/",
)
# Local tool state: caches, build products and fetched dependencies. Never authored, never tracked.
LOCAL_TOOL_ROOTS = (
    ".claude/",
    ".pytest_cache/",
    ".vs/",
    ".vscode/",
    "Binaries/",
    "DerivedDataCache/",
    "Intermediate/",
    ".venv/",
    "pipeline/.venv/",
    "Plugins/External/",
    "Saved/",
    "Source/ElysiumUE/ThirdParty/CPython27/",
)
IGNORED_AUTHORED_ALLOWLIST = GENERATED_PACKAGE_ROOTS + LOCAL_TOOL_ROOTS


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
    if suffix in UNREAL_PACKAGE_EXTENSIONS and any(
        lower.startswith(prefix.lower()) for prefix in AUTHORED_UNREAL_PREFIXES
    ):
        return None
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
    for line in attributes.splitlines():
        fields = line.split()
        if "filter=lfs" in fields and fields[0] not in AUTHORED_UNREAL_LFS_PATTERNS:
            errors.append(
                "Git LFS is allowed only for Unreal packages under Content/ElysiumAuthored/**"
            )
    return errors


def ignored_authored_warnings() -> list[str]:
    warnings: list[str] = []
    output = git("ls-files", "--others", "--ignored", "--exclude-standard", "-z")
    for raw in output.split("\0"):
        path = normalized(raw)
        if not path or path.endswith("/"):
            continue
        lower = path.lower()
        if any(lower.startswith(prefix.lower()) for prefix in IGNORED_AUTHORED_ALLOWLIST):
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
    for plugin in locked_plugins:
        root = REPO / "Plugins" / "External" / plugin
        marker = root / ".elysium-managed.json"
        if not marker.is_file():
            warnings.append(f"managed plugin is missing: {plugin}; run `uv run elysium deps sync`")
            continue
        data = json.loads(marker.read_text(encoding="utf-8-sig"))
        expected = locked_plugins[plugin]
        if (
            data.get("revision") != expected["revision"]
            or data.get("post_patch_tree") != expected["post_patch_tree"]
        ):
            warnings.append(
                f"managed plugin does not match the lock: {plugin}; "
                "run `uv run elysium deps sync`"
            )
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
            warnings.append(
                f"managed plugin is stale or modified: {plugin}; "
                "run `uv run elysium deps sync`"
            )
    generated = (
        REPO / "Content" / "ElysiumGenerated" / "Boot.umap",
        REPO / "Content" / "ElysiumGenerated" / "Materials" / "M_World_Opaque.uasset",
    )
    for path in generated:
        if not path.is_file():
            warnings.append(
                f"generated project package is missing: {path.relative_to(REPO)}; "
                "run `uv run elysium export bundle policy`"
            )
    baked = REPO / "Plugins" / "ElysiumBaked" / "Content"
    if not baked.is_dir() or not any(baked.rglob("*.umap")):
        warnings.append("no generated baked map package is present; run export map <map>")
    export_root = os.environ.get("ELYSIUM_EXPORT_ROOT", "").strip()
    if not export_root:
        work_root = os.environ.get("ELYSIUM_WORK_ROOT", "").strip()
        export_root = os.fspath(Path(work_root) / "exports") if work_root else ""
    if export_root and (Path(export_root) / ".elysium-incomplete").is_file():
        warnings.append(
            f"export corpus is marked incomplete: {Path(export_root) / '.elysium-incomplete'}"
        )
    npc_index = Path(export_root) / "npc" / "npc_index.json" if export_root else None
    if npc_index and npc_index.is_file():
        try:
            npc_data = json.loads(npc_index.read_text(encoding="utf-8"))
            for item in npc_data.get("warnings", []):
                warnings.append(
                    "NPC export warning "
                    f"[{item.get('code', 'unknown')}] {item.get('model', 'unknown')}: "
                    f"{item.get('detail', 'no detail')} "
                    f"(fallback: {item.get('fallback', 'none')})"
                )
        except (OSError, ValueError, TypeError) as exc:
            warnings.append(f"could not read NPC export warnings from {npc_index}: {exc}")
    return warnings


TEST_MACRO = re.compile(
    r'IMPLEMENT_(?:SIMPLE|COMPLEX)_AUTOMATION_TEST\s*\(\s*(\w+)\s*,\s*'
    r'(?:TEXT\(\s*)?"([^"]+)"',
    re.S,
)


def automation_test_names() -> list[tuple[str, str, str, str]]:
    """(name, file, class, kind) for every registered automation test in the runtime module.

    `kind` is SIMPLE or COMPLEX. A complex test's macro name is a BRANCH by design -- the framework
    registers `<base>.<row>` for each row `GetTests` emits -- so the prefix rule below applies to it
    in reverse: the branch is fine, but a simple test sharing or sitting under that branch is not.
    """
    found: list[tuple[str, str, str, str]] = []
    tests_dir = REPO / "Source" / "ElysiumUE" / "Private"
    for source in sorted(tests_dir.rglob("*.cpp")):
        text = source.read_text(encoding="utf-8", errors="replace")
        for match in TEST_MACRO.finditer(text):
            found.append((match.group(2), source.name, match.group(1), match.group(0).split("_")[1]))
    return found


def automation_name_violations() -> list[str]:
    """Names Unreal's automation registry silently drops or collides.

    A name that is a strict prefix of another becomes a *branch* in the automation tree, so the
    test registered under it is never run and never reported -- the suite is simply smaller than
    the source says, with nothing emitted to say so. A duplicated name loses one of its two
    registrations the same way.
    """
    errors: list[str] = []
    found = automation_test_names()
    leaves = {name for name, _, _, kind in found if kind == "SIMPLE"}
    branches = {name for name, _, _, kind in found if kind == "COMPLEX"}

    seen: dict[str, tuple[str, str]] = {}
    for name, source, klass, _kind in found:
        if name in seen:
            first_source, first_class = seen[name]
            errors.append(
                f"automation test name '{name}' is registered twice "
                f"({first_class} in {first_source}, {klass} in {source}); "
                f"one of the two never runs"
            )
        else:
            seen[name] = (source, klass)

    for name, source, klass, kind in found:
        if kind == "COMPLEX":
            # The branch itself is intended. What is not: a simple test parked on or under it,
            # which the framework would shadow with this test's own rows.
            buried = sorted(o for o in leaves if o == name or o.startswith(name + "."))
            if buried:
                errors.append(
                    f"simple test(s) {buried} sit on the branch '{name}' ({klass} in {source}) "
                    f"registers for its rows; the framework shadows them and they never run"
                )
            continue
        shadowing = sorted(o for o in (leaves | branches) if o.startswith(name + "."))
        if shadowing:
            errors.append(
                f"automation test name '{name}' ({klass} in {source}) is a strict prefix of "
                f"{len(shadowing)} other test(s) such as '{shadowing[0]}', so Unreal registers it "
                f"as a tree branch and the test never runs; give it a leaf name"
            )
    return errors


def audit(*, history: bool = False, repo_only: bool = False) -> tuple[list[str], list[str]]:
    errors = tracked_violations()
    errors.extend(automation_name_violations())
    warnings = ignored_authored_warnings()
    if history:
        errors.extend(history_violations())
    if not repo_only:
        warnings.extend(workspace_warnings())
    return errors, warnings


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--history", action="store_true", help="also audit every reachable Git object")
    parser.add_argument("--repo-only", action="store_true", help="skip local workspace prerequisites")
    parser.add_argument("--json", action="store_true", help="write a stable JSON result")
    args = parser.parse_args()

    errors, warnings = audit(history=args.history, repo_only=args.repo_only)

    if args.json:
        print(json.dumps({
            "ok": not errors,
            "errors": errors,
            "warnings": warnings,
        }, indent=2, sort_keys=True))
        return 1 if errors else 0
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
