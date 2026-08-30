"""Isolated one-export-root/one-GLB corpus-index product writer.

The corpus index is written last by `export-all`, after every other kind, and rewritten by any
single-unit command so that its `units[]` row for that unit is current.

The guarantee this seam exists for: `summary.unclaimed` is the count of `unclaimed` members and
the export **fails while it is non-zero**, printing every unclaimed path. A failed cross-unit
check fails it the same way, as does a member that names a unit no seam published or one whose
winning bytes the owning unit says it decoded other bytes for. A new file type in a future
install, a seam whose selection rule misses a spelling, a residue category applied without
evidence, or a seam reading the install through a different index than the walk therefore
surfaces as a failed export rather than a silently smaller corpus.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any, Sequence

from elysium_pipeline.formats.corpus_index_glb import (
    CORPUS_INDEX_EXTENSION,
    KIND_TITLE,
    InstallWalk,
    build_root,
    decode_corpus_index,
    output_relative_path,
    walk as install_walk,
    write_inverse,
)
from elysium_pipeline.formats.unit_contract import asset_block, write_glb


class CorpusIndexGlbError(RuntimeError):
    """The corpus index refuses to publish what the corpus is."""


def build_document(model) -> tuple[dict[str, Any], bytes]:
    """The scene-less document: one JSON chunk and no BIN chunk.

    The unit carries no accessor, because it is a product over other products: every number in it
    is a count, a length or a digest of a file another unit is authoritative for.
    """

    document = {
        "asset": asset_block(KIND_TITLE),
        "extensionsUsed": [CORPUS_INDEX_EXTENSION],
        "extensionsRequired": [CORPUS_INDEX_EXTENSION],
        "extensions": {CORPUS_INDEX_EXTENSION: build_root(model)},
    }
    return document, b""


def _rows(failures: Sequence[dict], limit: int = 12) -> list[str]:
    """One indented line per failing row, and how many more there were."""

    lines = [
        "    " + ", ".join(f"{key}={row[key]!r}" for key in sorted(row))
        for row in failures[:limit]
    ]
    if len(failures) > limit:
        lines.append(f"    ... {len(failures) - limit} more")
    return lines


def _refuse(model) -> None:
    """Every condition the seam map says fails the corpus export.

    The unclaimed member and the failed cross-unit check are the two the guarantee is written on.
    The other two reconcile the walk against the corpus: a member naming a unit no seam published,
    and a member whose winning bytes the owning unit says it decoded other bytes for. Either one
    means the index would state a complete corpus over a hole.
    """

    unclaimed = model.unclaimed
    failed = model.failed_checks
    unpublished = model.unpublished_claims
    disagreements = model.source_disagreements
    if not unclaimed and not failed and not unpublished and not disagreements:
        return
    lines = []
    if unclaimed:
        lines.append(f"{len(unclaimed)} unclaimed member(s):")
        lines += [f"    {path}" for path in unclaimed]
    if unpublished:
        lines.append(f"{len(unpublished)} member(s) naming a unit no seam published:")
        lines += _rows(unpublished)
    if disagreements:
        lines.append(f"{len(disagreements)} member(s) whose unit was decoded from other bytes:")
        lines += _rows(disagreements)
    for row in model.cross_unit_checks:
        if row["passed"]:
            continue
        lines.append(f"cross-unit check {row['name']} failed on {len(row['failures'])} row(s):")
        lines += _rows(row["failures"])
    raise CorpusIndexGlbError("\n".join(lines))


def export(
    index: InstallWalk | dict | None,
    export_root: Path,
    seams: Sequence[str] | None = None,
) -> Path:
    """Write and validate the one corpus index of `export_root`; return the file written.

    `index` is the complete UP-first walk of the install. A caller that already has one passes it;
    a plain install index is accepted too and is walked into one; `None` walks the real install.
    `seams` narrows which seams are asked to claim members, for a caller indexing part of a
    corpus.
    """

    if isinstance(index, InstallWalk):
        walk = index
    elif isinstance(index, dict):
        walk = install_walk.collect(index, seams=seams)
    else:
        walk = install_walk.collect(seams=seams)

    root = Path(export_root)
    model = decode_corpus_index(walk, root)
    _refuse(model)
    # The back-fill is the index's last step, and it runs only over a corpus the index accepts.
    # It rewrites published units, so the checks and the reconciliation are recomputed over what
    # it wrote and judged again: the document published is the one that was refused.
    model = write_inverse(model)
    _refuse(model)
    document, binary = build_document(model)

    from elysium_pipeline.validation import corpus_index_glb as validation

    validation.validate_document(document, binary, export_root=root, export_time=True)
    destination = root / Path(*output_relative_path().parts)
    write_glb(document, binary, destination)
    return destination


def index_path(export_root: Path) -> Path:
    """Where the one corpus index of `export_root` lives."""

    return Path(export_root) / Path(*output_relative_path().parts)


def refresh_unit(export_root: Path, unit_file: Path) -> bool:
    """Bring the index's `units[]` row for one published unit up to date, in place.

    This is what "rewritten by any single-unit command so that its `units[]` row for that unit is
    current" costs: the row and its `corpus-unit` dependency are re-read from the file the command
    just wrote, and `summary.units` follows. Nothing else is recomputed -- an edge the re-exported
    unit gained is not in `references[]` until the next whole-corpus run, and neither is a member
    disposition, because both are properties of the corpus rather than of the one unit. Returns
    False when no index exists yet, which is the ordinary state before the first `export-all`.
    """

    from elysium_pipeline.formats.corpus_index_glb import graph
    from elysium_pipeline.formats.corpus_index_glb.graph import by_kind
    from elysium_pipeline.formats.corpus_index_glb.model import DEPENDENCY_ROLE
    from elysium_pipeline.formats.unit_contract import dependency, encode_glb, read_glb

    root = Path(export_root)
    published = index_path(root)
    unit_file = Path(unit_file)
    if not published.is_file() or published == unit_file or not unit_file.is_file():
        return False
    document, binary = read_glb(published)
    index_root = (document.get("extensions") or {}).get(CORPUS_INDEX_EXTENSION)
    if not isinstance(index_root, dict):
        raise CorpusIndexGlbError(f"{published} carries no corpus-index extension root")
    unit, _ = graph.read_unit(unit_file, root)
    row = unit.to_json()

    units = list(index_root.get("units") or ())
    for position, existing in enumerate(units):
        if existing.get("asset") == unit.asset:
            units[position] = row
            break
    else:
        units.append(row)
        units.sort(key=lambda entry: str(entry.get("asset", "")))
    index_root["units"] = units

    pin = dependency(DEPENDENCY_ROLE, unit.asset, unit.path, True,
                     byteLength=unit.byte_length, sha256=unit.sha256)
    pins = list(index_root.get("dependencies") or ())
    for position, existing in enumerate(pins):
        if existing.get("asset") == unit.asset:
            pins[position] = pin
            break
    else:
        pins.append(pin)
        pins.sort(key=lambda entry: str(entry.get("asset", "")))
    index_root["dependencies"] = pins

    summary = dict(index_root.get("summary") or {})
    summary["units"] = len(units)
    summary["warnings"] = sum(
        int((entry.get("warnings") or {}).get("count", 0)) for entry in units
    )
    index_root["summary"] = summary

    # `census.byKind` is a total over `units[]`, so it follows the row that just changed; leaving
    # it behind would publish a census that contradicts the table it was counted from.
    census = dict(index_root.get("census") or {})
    if "byKind" in census:
        census["byKind"] = by_kind(units)
        index_root["census"] = census

    published.write_bytes(encode_glb(document, binary))
    return True
