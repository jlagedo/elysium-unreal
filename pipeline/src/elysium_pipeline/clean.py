"""Ownership checks and exact deletion rules for reproducible exports."""

from __future__ import annotations

from dataclasses import dataclass
import json
import os
from pathlib import Path
import shutil


OWNERSHIP_FILE = ".elysium-owned.json"
INCOMPLETE_FILE = ".elysium-incomplete"
MANIFEST_FILE = ".elysium-manifest.json"


class UnsafeClean(RuntimeError):
    pass


def _same(left: Path, right: Path) -> bool:
    return os.path.normcase(str(left.resolve())) == os.path.normcase(str(right.resolve()))


def _is_within(path: Path, root: Path) -> bool:
    try:
        path.resolve().relative_to(root.resolve())
        return True
    except ValueError:
        return False


def _dangerous_roots(repo: Path, game: Path, work: Path) -> tuple[Path, ...]:
    home = Path.home().resolve()
    anchor = Path(repo.resolve().anchor)
    return anchor, home, repo.resolve(), game.resolve(), work.resolve()


def ensure_export_ownership(
    export_root: Path, work_root: Path, *, adopt_standard: bool = True
) -> Path:
    export_root = export_root.resolve()
    work_root = work_root.resolve()
    standard = (work_root / "exports").resolve()
    if not _is_within(export_root, work_root) or _same(export_root, work_root):
        raise UnsafeClean(f"export root must be a child of the configured work root: {export_root}")
    marker = export_root / OWNERSHIP_FILE
    if marker.is_file():
        try:
            data = json.loads(marker.read_text(encoding="utf-8"))
            recorded_root = Path(data["root"])
        except (OSError, ValueError, KeyError, TypeError) as exc:
            raise UnsafeClean(f"invalid export ownership marker: {marker}") from exc
        if (
            data.get("schema") != 1
            or data.get("owner") != "elysium"
            or not _same(recorded_root, export_root)
        ):
            raise UnsafeClean(f"invalid export ownership marker: {marker}")
        return marker
    if not adopt_standard or not _same(export_root, standard):
        raise UnsafeClean(
            f"custom export root has no ownership marker: {marker}; "
            "create a schema-1 marker only after verifying this directory is dedicated "
            "to Elysium generated output"
        )
    export_root.mkdir(parents=True, exist_ok=True)
    marker.write_text(
        json.dumps({"schema": 1, "owner": "elysium", "root": str(export_root)}, indent=2)
        + "\n",
        encoding="utf-8",
    )
    return marker


def adopt_export_root(export_root: Path, work_root: Path) -> Path:
    """Validate ownership, adopting only the standard ``work/exports`` root."""
    return ensure_export_ownership(export_root, work_root, adopt_standard=True)


@dataclass(frozen=True)
class CleanTargets:
    export_root: Path
    project_content: Path
    boot_map: Path
    baked_content: Path


def validate_clean_targets(
    *, repo_root: Path, game_root: Path, work_root: Path, export_root: Path
) -> CleanTargets:
    repo = repo_root.resolve()
    game = game_root.resolve()
    work = work_root.resolve()
    export = export_root.resolve()
    ensure_export_ownership(export, work)

    for dangerous in _dangerous_roots(repo, game, work):
        if _same(export, dangerous):
            raise UnsafeClean(f"refusing dangerous export root: {export}")
    project_content = (repo / "Content" / "VtMB").resolve()
    boot_map = (repo / "Content" / "Elysium.umap").resolve()
    baked_content = (repo / "Plugins" / "ElysiumBaked" / "Content").resolve()
    expected = (
        repo / "Content" / "VtMB",
        repo / "Content" / "Elysium.umap",
        repo / "Plugins" / "ElysiumBaked" / "Content",
    )
    actual = (project_content, boot_map, baked_content)
    if any(not _same(left, right) for left, right in zip(expected, actual, strict=True)):
        raise UnsafeClean("generated Unreal targets did not resolve to the exact project paths")
    return CleanTargets(export, project_content, boot_map, baked_content)


def clean_generated(targets: CleanTargets) -> Path:
    marker = targets.export_root / OWNERSHIP_FILE
    for child in targets.export_root.iterdir():
        if child == marker:
            continue
        if child.is_dir():
            shutil.rmtree(child)
        else:
            child.unlink()
    if targets.project_content.exists():
        shutil.rmtree(targets.project_content)
    if targets.boot_map.exists():
        targets.boot_map.unlink()
    if targets.baked_content.exists():
        shutil.rmtree(targets.baked_content)
    incomplete = targets.export_root / INCOMPLETE_FILE
    incomplete.write_text(
        "The generated corpus is incomplete. Run `uv run elysium export grid`, "
        "`uv run elysium export all`, or `uv run elysium reconstruct`.\n",
        encoding="utf-8",
    )
    return incomplete


def mark_complete(export_root: Path) -> None:
    incomplete = export_root / INCOMPLETE_FILE
    if incomplete.exists():
        incomplete.unlink()
