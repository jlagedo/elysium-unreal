"""Deploy the vdata corpus out of the published GLB units.

`uv run elysium import vdata` reads `$ELYSIUM_EXPORT_V2_ROOT/vdata/**/*.glb`, lifts each unit's
source capsule -- the exact bytes the UP-first policy selected -- and writes them to

```text
Content/ElysiumCorpus/vdata/<subtree>/<name>.txt
```

No install and no engine are involved: the unit is self-contained, so the deploy is a copy out of
a file the export already validated, weighed once more against the `byteLength` and `sha256` that
unit published for its member.

The `signs/` subtree is excluded, matching the legacy mirror (`exporters/UE_extract_vdata.py`
excludes it because `UE_extract_signs.py` owns the sign panels together with their background
art). Signs migrate on their own slice; until then they stay on the legacy flat export.

Deploying is idempotent: a destination that already holds exactly these bytes is left alone, so
running the command twice writes nothing the second time and touches no file the editor may have
open.

A file left behind by a renamed or retired vdata unit is an orphan a re-export never cleans up on
its own, so every deploy also prunes: anything under `Content/ElysiumCorpus/vdata` that this run
did not just write, other than the `signs/` subtree, is deleted, and any directory the pruning
empties is removed with it.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath

from elysium_pipeline.formats.unit_contract import (
    CapsuleError,
    GlbContainerError,
    read_glb,
    source_capsules,
)
from elysium_pipeline.formats.vdata_glb.model import (
    SOURCE_ROOT,
    SOURCE_SUFFIX,
    VDATA_EXTENSION,
)

#: Where the runtime resolves loose corpus files from, below the repository's `Content/`.
CORPUS_DIRECTORY = "ElysiumCorpus"

#: The family directory this importer reads, under both roots.
FAMILY = "vdata"

#: Subtrees slice 1 leaves on the legacy export. One entry, spelled as a key prefix.
EXCLUDED_SUBTREES = ("signs/",)


class VdataImportError(RuntimeError):
    """The vdata corpus could not be deployed."""


@dataclass(slots=True)
class ImportResult:
    """What one deploy did, unit by unit."""

    destination_root: Path
    written: int = 0
    unchanged: int = 0
    skipped: int = 0
    pruned: int = 0
    failures: list[tuple[str, str]] = field(default_factory=list)

    def summary(self) -> str:
        return (
            f"vdata corpus import: {self.written} written, {self.unchanged} unchanged, "
            f"{self.skipped} signs skipped, {self.pruned} pruned, {len(self.failures)} failed "
            f"-> {self.destination_root}"
        )


def corpus_root(repo_root: Path) -> Path:
    """`Content/ElysiumCorpus` below one checkout. The deployed corpus is gitignored."""

    return Path(repo_root) / "Content" / CORPUS_DIRECTORY


def unit_root(export_v2_root: Path) -> Path:
    return Path(export_v2_root) / FAMILY


def units(export_v2_root: Path) -> list[Path]:
    """Every published vdata unit, in a stable order."""

    root = unit_root(export_v2_root)
    return sorted(root.rglob("*.glb")) if root.is_dir() else []


def unit_key(export_v2_root: Path, unit: Path) -> str:
    """The unit's key: its path below the family root, forward-slashed, without `.glb`."""

    relative = Path(unit).relative_to(unit_root(export_v2_root))
    return PurePosixPath(*relative.parts).with_suffix("").as_posix()


def is_excluded(key: str) -> bool:
    return key.startswith(EXCLUDED_SUBTREES)


def _destination(destination_root: Path, source_path: str) -> Path:
    """Where one member's install-relative path deploys to, refusing anything else.

    The path is the one the unit published for its own member, so it is checked rather than
    trusted: a member that does not name a `vdata/**.txt` file, or that tries to climb out of the
    corpus, is a defect in the unit and not a file this command writes.
    """

    if not source_path.startswith(SOURCE_ROOT) or not source_path.endswith(SOURCE_SUFFIX):
        raise VdataImportError(f"{source_path!r} is not a vdata source path")
    parts = PurePosixPath(source_path).parts
    if any(part in ("", ".", "..") or ":" in part for part in parts) or "\\" in source_path:
        raise VdataImportError(f"{source_path!r} is not a relative corpus path")
    if len(parts) < 3:
        raise VdataImportError(f"{source_path!r} names no vdata subtree")
    destination = Path(destination_root, *parts)
    # Belt and braces: the checks above are meant to catch every escape already, but a resolved
    # path outside the corpus is refused unconditionally rather than trusted to have been caught.
    assert destination.resolve().is_relative_to(Path(destination_root).resolve())
    return destination


def extract_unit(unit: Path) -> dict[str, bytes]:
    """The source bytes one published unit carries, keyed by install-relative path."""

    document, binary = read_glb(unit)
    root = (document.get("extensions") or {}).get(VDATA_EXTENSION)
    if not isinstance(root, dict):
        raise VdataImportError(f"{unit.name} is not a {VDATA_EXTENSION} unit")
    return source_capsules(document, binary, root)


def deploy(unit: Path, destination_root: Path) -> tuple[int, int, set[Path]]:
    """Write one unit's capsuled members. Returns `(written, unchanged, deployed paths)`."""

    written = unchanged = 0
    deployed: set[Path] = set()
    for source_path, data in sorted(extract_unit(unit).items()):
        destination = _destination(destination_root, source_path)
        deployed.add(destination)
        if destination.is_file() and destination.read_bytes() == data:
            unchanged += 1
            continue
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)
        written += 1
    return written, unchanged, deployed


def _prune_stale(destination_root: Path, deployed: set[Path]) -> int:
    """Remove every file under `destination_root/vdata` this deploy did not just write.

    `signs/` is never touched -- it is out of this slice's remit and stays on the legacy flat
    export until its own migration -- and the walk cleans up any empty directory it leaves behind.
    """

    family_root = Path(destination_root) / FAMILY
    signs_root = family_root / "signs"
    if not family_root.is_dir():
        return 0
    pruned = 0
    for path in list(family_root.rglob("*")):
        if path.is_dir() or path in deployed:
            continue
        if signs_root in path.parents or path == signs_root:
            continue
        path.unlink()
        pruned += 1
    for directory in sorted(
        (path for path in family_root.rglob("*") if path.is_dir()),
        key=lambda path: len(path.parts),
        reverse=True,
    ):
        if directory == signs_root or signs_root in directory.parents:
            continue
        try:
            directory.rmdir()
        except OSError:
            pass  # not empty -- still holds something this deploy kept or could not prune.
    return pruned


def import_vdata(export_v2_root: Path, destination_root: Path) -> ImportResult:
    """Deploy every non-`signs/` vdata unit under `export_v2_root` into `destination_root`.

    A unit that carries no capsule -- an old-format GLB from before schema 1.1.0 -- is one
    failure among many rather than the end of the run, so a partial corpus still deploys and the
    operator is told exactly which units to re-export.
    """

    export_v2_root = Path(export_v2_root)
    result = ImportResult(destination_root=Path(destination_root))
    found = units(export_v2_root)
    if not found:
        raise VdataImportError(
            f"no vdata units under {unit_root(export_v2_root)}; "
            "run `uv run elysium export_v2 vdatas-glb` first"
        )
    deployed: set[Path] = set()
    for unit in found:
        key = unit_key(export_v2_root, unit)
        if is_excluded(key):
            result.skipped += 1
            continue
        try:
            written, unchanged, unit_deployed = deploy(unit, result.destination_root)
        except (CapsuleError, GlbContainerError, VdataImportError, OSError, ValueError) as error:
            result.failures.append((key, str(error)))
            continue
        result.written += written
        result.unchanged += unchanged
        deployed |= unit_deployed
    result.pruned = _prune_stale(result.destination_root, deployed)
    return result
