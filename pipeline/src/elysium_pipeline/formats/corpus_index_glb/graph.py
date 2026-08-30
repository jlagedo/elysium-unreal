"""The published corpus as a graph: units, references, the inverse, and the census.

`units[]` is one row per published unit, hashed from the file on disk. `references[]` is every
`dependencies` row of every unit as an edge, `inverse` is the same graph keyed by target, and
those two are what fill the fields no unit can write about itself. `danglingReferences[]` groups
every unresolved edge by role and `orphans[]` lists, by kind, every unit no edge targets, because
an orphan is either dead content or a missing seam rule and both are worth a query.
"""

from __future__ import annotations

import hashlib
import os
from pathlib import Path
from typing import Any, Iterable, Mapping, Sequence

from elysium_pipeline.formats.corpus_index_glb.model import (
    DISPOSITIONS,
    Member,
    Reference,
    Unit,
    kind_of,
)
from elysium_pipeline.formats.unit_contract import read_glb, warnings_for


class CorpusGraphError(RuntimeError):
    """The published corpus cannot be read as a graph."""


#: The published unit whose file this seam writes; it indexes the others and never itself.
INDEX_FILE = "index.glb"


def extension_root(document: Mapping[str, Any]) -> tuple[str, dict[str, Any]]:
    """The one `ELYSIUM_vtmb_<kind>` root a unit declares, and its name.

    Every unit declares its own extension in `extensionsRequired`, and the VTMB meaning is
    reachable only through it, so the required list is where the root is looked up rather than
    guessing from the file's path.
    """

    extensions = document.get("extensions") or {}
    for name in document.get("extensionsRequired") or ():
        root = extensions.get(name)
        if str(name).startswith("ELYSIUM_vtmb_") and isinstance(root, dict):
            return str(name), root
    raise CorpusGraphError("the unit declares no ELYSIUM_vtmb_<kind> extension root")


def _warnings(root: Mapping[str, Any]) -> dict[str, Any]:
    """The count of every warning the unit publishes, and the first reason of each kind.

    A published unit that warned is still a unit the corpus is weaker for, and the whole list of
    a thousand identical reasons would drown the index; the first of each is what identifies it.
    """

    messages = warnings_for(root)
    first: dict[str, str] = {}
    for message in messages:
        label, _, _ = message.partition(":")
        first.setdefault(label, message)
    return {"count": len(messages), "reasons": [first[label] for label in sorted(first)]}


def read_unit(path: Path, export_root: Path) -> tuple[Unit, dict[str, Any]]:
    """One published unit as a `units[]` row plus the extension root it was read from."""

    data = Path(path).read_bytes()
    document, _ = read_glb(path)
    _, root = extension_root(document)
    identity = root.get("identity") or {}
    asset = str(identity.get("asset", ""))
    if not asset.startswith("vtmb:"):
        raise CorpusGraphError(f"{path} publishes no stable identity")
    coverage = root.get("coverage") or {}
    relative = os.path.relpath(Path(path), Path(export_root)).replace("\\", "/")
    unit = Unit(
        asset=asset,
        kind=kind_of(asset),
        path=relative,
        schema_version=str(root.get("schemaVersion", "")),
        byte_length=len(data),
        sha256=hashlib.sha256(data).hexdigest(),
        warnings=_warnings(root),
        dependency_count=len(root.get("dependencies") or ()),
        unresolved_count=len(coverage.get("unresolved") or ()),
        unsupported_count=len(coverage.get("unsupported") or ()),
    )
    return unit, root


def published_files(export_root: Path) -> list[Path]:
    """Every `.glb` below the export root except the index itself, in sorted path order."""

    root = Path(export_root)
    if not root.is_dir():
        return []
    return sorted(
        path
        for path in root.rglob("*.glb")
        if not (path.parent == root and path.name == INDEX_FILE)
    )


def read_corpus(export_root: Path) -> tuple[list[Unit], dict[str, dict[str, Any]]]:
    """Every published unit under the export root, and its extension root keyed by identity."""

    units: list[Unit] = []
    roots: dict[str, dict[str, Any]] = {}
    seen: dict[str, str] = {}
    for path in published_files(export_root):
        unit, root = read_unit(path, export_root)
        if unit.asset in seen:
            raise CorpusGraphError(
                f"{unit.asset} is published twice: {seen[unit.asset]} and {unit.path}"
            )
        seen[unit.asset] = unit.path
        units.append(unit)
        roots[unit.asset] = root
    units.sort(key=lambda unit: unit.asset)
    return units, roots


def references(roots: Mapping[str, Mapping[str, Any]]) -> list[Reference]:
    """Every `dependencies` row of every unit, as an edge, in unit then declaration order."""

    edges: list[Reference] = []
    for asset in sorted(roots):
        for row in roots[asset].get("dependencies") or ():
            edges.append(
                Reference(
                    source=asset,
                    role=str(row.get("role", "")),
                    target=str(row.get("asset", "")),
                    source_path=str(row.get("sourcePath", "")),
                    resolved=bool(row.get("resolved", False)),
                )
            )
    return edges


def inverse(edges: Sequence[Reference]) -> dict[str, list[dict[str, str]]]:
    """The same graph keyed by target: what fills the fields no unit can write about itself."""

    table: dict[str, list[dict[str, str]]] = {}
    for edge in edges:
        rows = table.setdefault(edge.target, [])
        row = {"from": edge.source, "role": edge.role}
        if row not in rows:
            rows.append(row)
    return {target: table[target] for target in sorted(table)}


def dangling(edges: Sequence[Reference]) -> list[dict[str, Any]]:
    """Every `resolved: false` edge grouped by role, with the referrer and the authored path."""

    by_role: dict[str, list[dict[str, str]]] = {}
    for edge in edges:
        if edge.resolved:
            continue
        by_role.setdefault(edge.role, []).append(
            {"from": edge.source, "to": edge.target, "sourcePath": edge.source_path}
        )
    return [
        {"role": role, "count": len(by_role[role]), "edges": by_role[role]}
        for role in sorted(by_role)
    ]


def orphans(units: Sequence[Unit], edges: Sequence[Reference]) -> list[dict[str, Any]]:
    """Every unit no edge targets, by kind: a material nothing binds, a sound nothing plays."""

    targeted = {edge.target for edge in edges}
    by_kind: dict[str, list[str]] = {}
    for unit in units:
        if unit.asset not in targeted:
            by_kind.setdefault(unit.kind, []).append(unit.asset)
    return [
        {"kind": kind, "count": len(by_kind[kind]), "assets": sorted(by_kind[kind])}
        for kind in sorted(by_kind)
    ]


def by_kind(rows: Iterable[Mapping[str, Any]]) -> list[dict[str, Any]]:
    """`census.byKind`, counted from `units[]` rows in their published shape.

    The rows are what a reader of the index has, so the census is a total over the same table
    rather than over the objects that happened to build it; a single-unit refresh recounts it
    from the rewritten table for exactly that reason.
    """

    table: dict[str, dict[str, Any]] = {}
    for row in rows:
        kind = kind_of(str(row.get("asset", "")))
        entry = table.setdefault(
            kind, {"kind": kind, "count": 0, "byteLength": 0, "warnings": 0}
        )
        entry["count"] += 1
        entry["byteLength"] += int(row.get("byteLength", 0))
        entry["warnings"] += int((row.get("warnings") or {}).get("count", 0))
    return [table[kind] for kind in sorted(table)]


def census(members: Sequence[Member], units: Sequence[Unit]) -> dict[str, Any]:
    """The table the seams were designed against, restated from the walk that built the index."""

    by_extension: dict[str, dict[str, Any]] = {}
    # All four dispositions always, so a reader never has to tell absent from empty.
    by_disposition: dict[str, int] = {name: 0 for name in DISPOSITIONS}
    for member in members:
        by_disposition[member.disposition] = by_disposition.get(member.disposition, 0) + 1
        row = by_extension.setdefault(
            member.extension,
            {
                "extension": member.extension,
                "count": 0,
                "byteLength": 0,
                "byOrigin": {"vpk": 0, "retail-loose": 0, "patch-loose": 0},
                "bytesByOrigin": {"vpk": 0, "retail-loose": 0, "patch-loose": 0},
            },
        )
        row["count"] += 1
        row["byteLength"] += member.source.byte_length
        origin = origin_kind(member.source.origin)
        row["byOrigin"][origin] = row["byOrigin"].get(origin, 0) + 1
        row["bytesByOrigin"][origin] = (
            row["bytesByOrigin"].get(origin, 0) + member.source.byte_length
        )

    return {
        "byExtension": [by_extension[key] for key in sorted(by_extension)],
        "byKind": by_kind(unit.to_json() for unit in units),
        "byDisposition": {key: by_disposition.get(key, 0) for key in sorted(by_disposition)},
    }


def origin_kind(origin: Mapping[str, Any]) -> str:
    """Which of the census's three origin columns one member's origin belongs to."""

    if origin.get("kind") == "vpk":
        return "vpk"
    root = str(origin.get("root", "")).lower()
    return "patch-loose" if root == "unofficial_patch" else "retail-loose"


def dependencies(units: Iterable[Unit], role: str) -> list[dict[str, Any]]:
    """One `corpus-unit` dependency per unit, pinning kind, path and hash.

    That is the same list as `units[]` in the contract's row shape, so a reader that only
    understands the unit contract still sees the whole corpus.
    """

    from elysium_pipeline.formats.unit_contract import dependency

    return [
        dependency(role, unit.asset, unit.path, True,
                   byteLength=unit.byte_length, sha256=unit.sha256)
        for unit in units
    ]
