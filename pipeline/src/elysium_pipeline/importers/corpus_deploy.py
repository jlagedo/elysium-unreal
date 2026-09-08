"""The shared machinery every `uv run elysium import <corpus lane>` runs on.

A corpus lane deploys **loose source files** out of published `export_v2` units: it lifts each
unit's source capsule -- the exact bytes the UP-first policy selected -- and writes them where the
runtime reads them, below `Content/ElysiumCorpus`. No install and no engine are involved, because
a capsuled unit is self-contained; the deploy is a copy out of a file the export already validated,
weighed once more
against the `byteLength` and `sha256` the unit published for that member.

Three properties every lane gets from here, matching the texture lane's own shape
(`importers/textures.py`):

**Recipe stamps.** Each lane keeps `<corpus>/_import/<lane>/recipes.json`: per unit, the size and
modification time of the GLB it was deployed from, the lane's `recipeVersion`, and every file it
wrote with that file's size and digest. A re-run whose unit is untouched and whose targets are
still exactly what the stamp recorded skips the unit **without opening the GLB at all**, so a
second run over an unchanged corpus is a no-op that costs a `stat` per unit rather than a parse
and a hash of every published byte.

**Per-unit failure isolation.** A unit that cannot be read, whose capsule is absent (a unit from
before the seam's schema 1.1.0), or whose capsule disagrees with its own digest is one failure
among many: it is named with its reason, the run goes on, and the command exits non-zero at the
end. Its previously deployed files are kept, never pruned, so a transient read error costs one
relaunch and never a good file.

**Byte-equality verification.** Every file this lane writes is read back and compared with the
capsule bytes it came from before the run is called a success, and the digest of what landed is
what the stamp records -- so a stamp can never bless bytes that are not on disk.

An orphan -- a file left behind by a renamed or retired unit -- is pruned: anything under a lane's
own destination directories that this run neither wrote nor kept for a failed unit is deleted, and
any directory the pruning empties goes with it.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
from typing import Any, Callable, Sequence

from elysium_pipeline.formats.unit_contract import (
    CapsuleError,
    GlbContainerError,
    read_glb,
    source_capsules,
)

#: Where the runtime resolves loose corpus files from, below the repository's `Content/`.
CORPUS_DIRECTORY = "ElysiumCorpus"

#: The lane's own bookkeeping directory below the corpus root. Never a runtime read.
BOOKKEEPING_DIRECTORY = "_import"
RECIPES_NAME = "recipes.json"
IMPORT_REPORT_NAME = "import_report.json"


class CorpusImportError(RuntimeError):
    """A corpus lane could not deploy what it was asked to."""


def corpus_root(repo_root: Path) -> Path:
    """`Content/ElysiumCorpus` below one checkout. The deployed corpus is gitignored."""

    return Path(repo_root) / "Content" / CORPUS_DIRECTORY


def bookkeeping_root(destination_root: Path, lane: str) -> Path:
    return Path(destination_root) / BOOKKEEPING_DIRECTORY / lane


# --- destination safety ---------------------------------------------------------------------------


def safe_destination(destination_root: Path, relative: str) -> Path:
    """`destination_root/relative`, refusing anything that is not a plain relative corpus path.

    The relative path is derived from what a *unit* published for its own member, so it is checked
    rather than trusted: a member that tries to climb out of the corpus, or that carries a drive
    letter (`vdata/C:/x` folds to two innocent-looking POSIX parts and then reroots on Windows),
    is a defect in the unit and not a file this command writes.
    """

    if not relative or "\\" in relative:
        raise CorpusImportError(f"{relative!r} is not a relative corpus path")
    parts = PurePosixPath(relative).parts
    if not parts or any(part in ("", ".", "..") or ":" in part for part in parts):
        raise CorpusImportError(f"{relative!r} is not a relative corpus path")
    destination = Path(destination_root, *parts)
    # Belt and braces: the checks above are meant to catch every escape already, but a resolved
    # path outside the corpus is refused unconditionally rather than trusted to have been caught.
    if not destination.resolve().is_relative_to(Path(destination_root).resolve()):
        raise CorpusImportError(f"{relative!r} resolves outside the corpus")
    return destination


# --- results --------------------------------------------------------------------------------------


@dataclass(slots=True)
class ImportResult:
    """What one lane's deploy did, unit by unit and file by file.

    Two things the numbers do *not* say. `written`/`unchanged`/`verified` count **(unit, target)
    pairs**, not distinct files, so a file two units both deploy -- a `.lip` beside a stem that
    ships as both `.wav` and `.mp3` -- is counted once per unit. And `verified` counts only what
    this run read back: a unit taken by the stamp fast path is not opened and its files are not
    re-hashed, so a fully current re-run reports `verified == 0`, which is the point of the stamp.
    """

    lane: str
    destination_root: Path
    units: int = 0
    units_current: int = 0
    written: int = 0
    unchanged: int = 0
    verified: int = 0
    pruned: int = 0
    failures: list[tuple[str, str]] = field(default_factory=list)

    def summary(self) -> str:
        return (
            f"{self.lane} corpus import: {self.units} unit(s), {self.written} file(s) written, "
            f"{self.unchanged} unchanged, {self.units_current} unit(s) already current, "
            f"{self.pruned} pruned, {len(self.failures)} failed -> {self.destination_root}"
        )

    def report(self) -> dict[str, Any]:
        return {
            "lane": self.lane,
            "destinationRoot": str(self.destination_root),
            "units": self.units,
            "unitsAlreadyCurrent": self.units_current,
            "filesWritten": self.written,
            "filesUnchanged": self.unchanged,
            "filesVerified": self.verified,
            "filesPruned": self.pruned,
            "failures": [{"unit": key, "reason": reason} for key, reason in self.failures],
        }


# --- unit enumeration -----------------------------------------------------------------------------


def units(family_root: Path) -> list[Path]:
    """Every published unit below one family root, in a stable order."""

    root = Path(family_root)
    return sorted(root.rglob("*.glb")) if root.is_dir() else []


def unit_key(family_root: Path, unit: Path) -> str:
    """The unit's key: its path below the family root, forward-slashed, without `.glb`."""

    relative = Path(unit).relative_to(Path(family_root))
    return PurePosixPath(*relative.parts).with_suffix("").as_posix()


def extract_unit(unit: Path, extension: str) -> dict[str, bytes]:
    """The source bytes one published unit carries, keyed by its install-relative member path."""

    document, binary = read_glb(unit)
    root = (document.get("extensions") or {}).get(extension)
    if not isinstance(root, dict):
        raise CorpusImportError(f"{Path(unit).name} is not a {extension} unit")
    return source_capsules(document, binary, root)


# --- recipe stamps --------------------------------------------------------------------------------


def _digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def load_recipes(destination_root: Path, lane: str, recipe_version: str) -> dict[str, Any]:
    """The stamp file for one lane, or an empty book when it is absent, stale or unreadable.

    A stamp written by a different `recipeVersion` is discarded whole: the version is the lane's
    own statement that the mapping from a unit to the files it produces has changed, so nothing
    recorded under the old one may excuse a skip.
    """

    path = bookkeeping_root(destination_root, lane) / RECIPES_NAME
    try:
        book = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}
    if not isinstance(book, dict) or book.get("recipeVersion") != recipe_version:
        return {}
    entries = book.get("units")
    return entries if isinstance(entries, dict) else {}


def save_recipes(
    destination_root: Path, lane: str, recipe_version: str, entries: dict[str, Any]
) -> Path:
    path = bookkeeping_root(destination_root, lane) / RECIPES_NAME
    path.parent.mkdir(parents=True, exist_ok=True)
    body = {"recipeVersion": recipe_version, "units": dict(sorted(entries.items()))}
    _atomic_write(path, (json.dumps(body, indent=1, sort_keys=True) + "\n").encode("utf-8"))
    return path


def save_report(destination_root: Path, lane: str, result: ImportResult) -> Path:
    path = bookkeeping_root(destination_root, lane) / IMPORT_REPORT_NAME
    path.parent.mkdir(parents=True, exist_ok=True)
    body = json.dumps(result.report(), indent=1, sort_keys=True) + "\n"
    _atomic_write(path, body.encode("utf-8"))
    return path


def unit_signature(unit: Path) -> dict[str, int]:
    """The cheap identity of a published unit: its size and modification time.

    Deliberately not its digest. A lane holds thousands of units and gigabytes of published bytes;
    hashing all of them on every run would cost more than the deploy the stamp exists to skip, and
    the *bytes that matter* -- the ones on disk in the corpus -- are digested unconditionally.
    """

    stat = Path(unit).stat()
    return {"size": stat.st_size, "mtimeNs": stat.st_mtime_ns}


def _atomic_write(path: Path, data: bytes) -> None:
    """Write through a temporary sibling, so a run killed mid-write leaves no truncated file."""

    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_bytes(data)
    os.replace(temporary, path)


# --- the deploy -----------------------------------------------------------------------------------


@dataclass(frozen=True, slots=True)
class Lane:
    """One corpus lane: which units it reads and where each member's bytes land.

    `families` maps a family directory below the `export_v2` root to the extension its units
    declare. `target_of` turns one member's install-relative path into the corpus-relative paths
    it deploys to -- more than one when a file is deliberately mirrored -- or an empty tuple when
    the lane does not deploy that member at all.
    """

    name: str
    recipe_version: str
    families: tuple[tuple[str, str], ...]
    target_of: Callable[[str], Sequence[str]]
    #: The corpus-relative directories this lane owns and therefore prunes.
    owned_directories: tuple[str, ...]
    #: What to tell the operator when the export root holds none of this lane's units.
    empty_hint: str


def _targets_current(destination_root: Path, targets: dict[str, Any]) -> bool:
    """Whether every file a stamp recorded is still on disk at the size it recorded."""

    for relative, record in targets.items():
        try:
            path = safe_destination(destination_root, relative)
        except CorpusImportError:
            return False
        try:
            if path.stat().st_size != int(record["size"]):
                return False
        except (OSError, KeyError, TypeError, ValueError):
            return False
    return True


def deploy_lane(export_v2_root: Path, destination_root: Path, lane: Lane) -> ImportResult:
    """Run one lane end to end: stamp, deploy, verify, prune, report."""

    export_v2_root = Path(export_v2_root)
    destination_root = Path(destination_root)
    result = ImportResult(lane=lane.name, destination_root=destination_root)

    discovered: list[tuple[str, Path, str]] = []
    for family, extension in lane.families:
        family_root = export_v2_root / family
        for unit in units(family_root):
            discovered.append((f"{family}/{unit_key(family_root, unit)}", unit, extension))
    if not discovered:
        raise CorpusImportError(
            f"no {lane.name} units under {export_v2_root}; {lane.empty_hint}"
        )

    stamps = load_recipes(destination_root, lane.name, lane.recipe_version)
    fresh: dict[str, Any] = {}
    kept: set[Path] = set()

    for key, unit, extension in discovered:
        result.units += 1
        try:
            signature = unit_signature(unit)
        except OSError as error:
            result.failures.append((key, str(error)))
            continue

        stamp = stamps.get(key)
        if (
            isinstance(stamp, dict)
            and stamp.get("unit") == signature
            and isinstance(stamp.get("targets"), dict)
            and _targets_current(destination_root, stamp["targets"])
        ):
            # Nothing about this unit or the files it produced has moved; skip it unopened.
            result.units_current += 1
            result.unchanged += len(stamp["targets"])
            fresh[key] = stamp
            kept |= {
                safe_destination(destination_root, relative) for relative in stamp["targets"]
            }
            continue

        try:
            targets = _deploy_unit(unit, extension, destination_root, lane, result)
        except (CapsuleError, GlbContainerError, CorpusImportError, OSError, ValueError) as error:
            result.failures.append((key, str(error)))
            # A failed unit's previously deployed files are protected, not orphaned: whatever the
            # last good run recorded for it stays on disk and out of the prune.
            if isinstance(stamp, dict) and isinstance(stamp.get("targets"), dict):
                for relative in stamp["targets"]:
                    try:
                        kept.add(safe_destination(destination_root, relative))
                    except CorpusImportError:
                        pass
            continue

        fresh[key] = {"unit": signature, "targets": targets}
        kept |= {safe_destination(destination_root, relative) for relative in targets}

    result.pruned = _prune(destination_root, lane, kept)
    save_recipes(destination_root, lane.name, lane.recipe_version, fresh)
    save_report(destination_root, lane.name, result)
    return result


def _deploy_unit(
    unit: Path,
    extension: str,
    destination_root: Path,
    lane: Lane,
    result: ImportResult,
) -> dict[str, dict[str, Any]]:
    """Write every corpus file one unit's capsuled members map to. Returns the stamp's targets."""

    targets: dict[str, dict[str, Any]] = {}
    for source_path, data in sorted(extract_unit(unit, extension).items()):
        for relative in lane.target_of(source_path):
            destination = safe_destination(destination_root, relative)
            # Byte equality against the capsule is proven by reading the destination back, never
            # assumed: the same read decides "already correct" for an existing file and confirms
            # what landed for a written one, and it is that proof the stamp then records, so a
            # stamp can never bless bytes that are not on disk.
            current = (
                destination.read_bytes()
                if destination.is_file() and destination.stat().st_size == len(data)
                else None
            )
            if current == data:
                result.unchanged += 1
            else:
                _atomic_write(destination, data)
                result.written += 1
                landed = destination.read_bytes()
                if landed != data:
                    raise CorpusImportError(
                        f"{relative}: {len(landed)} byte(s) landed for a "
                        f"{len(data)}-byte capsule of {source_path}"
                    )
            result.verified += 1
            targets[relative] = {"size": len(data), "sha256": _digest(data)}
    return targets


def _prune(destination_root: Path, lane: Lane, kept: set[Path]) -> int:
    """Delete every file below the lane's own directories that this run did not write or keep."""

    pruned = 0
    for owned in lane.owned_directories:
        root = Path(destination_root) / owned
        if not root.is_dir():
            continue
        for path in list(root.rglob("*")):
            if path.is_dir() or path in kept:
                continue
            path.unlink()
            pruned += 1
        for directory in sorted(
            (path for path in root.rglob("*") if path.is_dir()),
            key=lambda path: len(path.parts),
            reverse=True,
        ):
            try:
                directory.rmdir()
            except OSError:
                pass  # not empty -- still holds something this deploy kept.
    return pruned


__all__ = [
    "CORPUS_DIRECTORY",
    "CorpusImportError",
    "ImportResult",
    "Lane",
    "bookkeeping_root",
    "corpus_root",
    "deploy_lane",
    "extract_unit",
    "safe_destination",
    "unit_key",
    "units",
]
