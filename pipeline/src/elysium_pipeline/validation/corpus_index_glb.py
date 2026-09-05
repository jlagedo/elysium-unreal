"""Independent read-back validation for the corpus-index GLB unit.

Standalone validation checks the scene-less rule, the absence of a BIN chunk, the identity, and
the internal consistency of `members[]`, `units[]`, `references[]` and `summary`, including that
every member naming a unit names one `units[]` publishes. Where the export root the index names
is present it also re-hashes every unit file against its `units[]` row -- a row whose file is
missing or whose hash differs fails the index -- and compares each `unit` member's winning bytes
against the `sourceResolution.members[]` of the unit that claims to have been decoded from them.

Export-time validation adds the one pass a published index cannot be judged on later: that every
`references[]` edge is exactly one unit's `dependencies` row. A single-unit re-export refreshes
its `units[]` row and leaves the edge table to the next whole-corpus run, so that comparison
belongs to the run that wrote both.

Nothing here imports the writer: the validator re-derives every total from the published tables.
"""

from __future__ import annotations

import hashlib
import os
from pathlib import Path
from typing import Any, Mapping

from elysium_pipeline.formats.corpus_index_glb.model import (
    ASSET_ID,
    CHECK_NAMES,
    CORPUS_INDEX_EXTENSION,
    DISPOSITIONS,
    OUTPUT_NAME,
    RESIDUE_CATEGORIES,
    SCHEMA_VERSION,
    kind_of,
)
from elysium_pipeline.formats.unit_contract import (
    UnitValidationError,
    decode_glb,
    read_glb,
    validate_container,
    validate_extension_root,
    validate_sceneless,
)

#: The kind-specific keys the extension root carries after the contract's five, in writer order.
INDEX_KEYS = (
    "excludedTrees",
    "members",
    "units",
    "references",
    "inverse",
    "danglingReferences",
    "orphans",
    "crossUnitChecks",
    "census",
    "summary",
)


class CorpusIndexValidationError(UnitValidationError):
    """The published corpus index contradicts its own tables."""


def _root(document: Mapping[str, Any]) -> dict[str, Any]:
    root = validate_extension_root(
        document,
        CORPUS_INDEX_EXTENSION,
        asset_prefix=ASSET_ID,
        schema_version=SCHEMA_VERSION,
    )
    if str((root.get("identity") or {}).get("asset")) != ASSET_ID:
        raise CorpusIndexValidationError(f"the index identity is not {ASSET_ID}")
    missing = [key for key in INDEX_KEYS if key not in root]
    if missing:
        raise CorpusIndexValidationError(f"the index publishes no {', '.join(missing)}")
    return root


def _members(root: Mapping[str, Any]) -> dict[str, Any]:
    """Member table consistency: unique keys, a legal disposition, and the fields it implies."""

    counts = {name: 0 for name in DISPOSITIONS}
    seen: set[str] = set()
    previous = ""
    for index, row in enumerate(root.get("members") or ()):
        if not isinstance(row, Mapping):
            raise CorpusIndexValidationError(f"member {index} is not a record")
        path = str(row.get("path", ""))
        if not path:
            raise CorpusIndexValidationError(f"member {index} names no path")
        if path != path.lower().replace("\\", "/"):
            raise CorpusIndexValidationError(f"member {path!r} is not a folded install key")
        if path in seen:
            raise CorpusIndexValidationError(f"member {path!r} is published twice")
        if path < previous:
            raise CorpusIndexValidationError(f"member {path!r} is out of key order")
        seen.add(path)
        previous = path
        disposition = str(row.get("disposition", ""))
        if disposition not in counts:
            raise CorpusIndexValidationError(f"{path}: unknown disposition {disposition!r}")
        counts[disposition] += 1
        if int(row.get("byteLength", -1)) < 0 or len(str(row.get("sha256", ""))) != 64:
            raise CorpusIndexValidationError(f"{path}: no winning byte length and digest")
        if disposition in ("unit", "companion") and not row.get("asset"):
            raise CorpusIndexValidationError(f"{path}: a {disposition} member names no unit")
        from elysium_pipeline.formats.unit_contract.source_policy import unit_source_policy
        if row.get("unitSourcePolicy") != unit_source_policy(row.get("asset"), path):
            raise CorpusIndexValidationError(f"{path}: unknown or missing named source policy")
        if disposition == "residue":
            evidence = row.get("evidence") or {}
            if evidence.get("category") not in RESIDUE_CATEGORIES:
                raise CorpusIndexValidationError(f"{path}: residue names no known category")
            if not evidence.get("evidence"):
                raise CorpusIndexValidationError(f"{path}: residue carries no evidence")
    return counts


def _units(root: Mapping[str, Any]) -> dict[str, dict[str, Any]]:
    units: dict[str, dict[str, Any]] = {}
    for index, row in enumerate(root.get("units") or ()):
        if not isinstance(row, Mapping):
            raise CorpusIndexValidationError(f"unit {index} is not a record")
        asset = str(row.get("asset", ""))
        if not asset.startswith("vtmb:"):
            raise CorpusIndexValidationError(f"unit {index} names no stable identity")
        if asset in units:
            raise CorpusIndexValidationError(f"{asset} has two rows")
        if not str(row.get("path", "")):
            raise CorpusIndexValidationError(f"{asset} names no published path")
        if len(str(row.get("sha256", ""))) != 64:
            raise CorpusIndexValidationError(f"{asset} carries no digest")
        units[asset] = dict(row)
    return units


def _references(root: Mapping[str, Any], units: Mapping[str, Any]) -> None:
    """Every edge starts at a published unit, and `inverse` is the edge table's transpose."""

    transpose: dict[str, list[dict[str, str]]] = {}
    for index, row in enumerate(root.get("references") or ()):
        if not isinstance(row, Mapping):
            raise CorpusIndexValidationError(f"reference {index} is not a record")
        source = str(row.get("from", ""))
        target = str(row.get("to", ""))
        if source not in units:
            raise CorpusIndexValidationError(f"reference {index} starts at unpublished {source!r}")
        if not target.startswith("vtmb:"):
            raise CorpusIndexValidationError(f"reference {index} names no target identity")
        if not isinstance(row.get("resolved"), bool):
            raise CorpusIndexValidationError(f"reference {index} does not state resolution")
        edge = {"from": source, "role": str(row.get("role", ""))}
        rows = transpose.setdefault(target, [])
        if edge not in rows:
            rows.append(edge)
    published = root.get("inverse")
    if not isinstance(published, Mapping):
        raise CorpusIndexValidationError("the index publishes no inverse graph")
    if dict(published) != transpose:
        raise CorpusIndexValidationError("inverse is not the transpose of references")


def _member_assets(row: Mapping[str, Any]) -> list[str]:
    """Every unit one member row names: the several of a cut table, or its one `asset`."""

    assets = [str(asset) for asset in row.get("assets") or ()]
    if not assets and row.get("asset"):
        assets = [str(row["asset"])]
    return assets


def _reconcile(root: Mapping[str, Any], units: Mapping[str, Any]) -> None:
    """Every member that names a unit names one `units[]` publishes.

    Without this the member table and the unit table are two lists that never meet: a member can
    be dispositioned `unit` against an identity no seam ever published, and the index would state
    a complete corpus over the hole. `embedded[]` is held to the same rule, because "the unit it
    became" is the same claim about a PAKFILE member.
    """

    for row in root.get("members") or ():
        if not isinstance(row, Mapping):
            continue
        path = str(row.get("path", ""))
        if str(row.get("disposition", "")) in ("unit", "companion"):
            for asset in _member_assets(row):
                if asset not in units:
                    raise CorpusIndexValidationError(
                        f"{path}: no unit is published for {asset}"
                    )
        for embedded in row.get("embedded") or ():
            asset = (embedded or {}).get("asset") if isinstance(embedded, Mapping) else None
            if asset is not None and str(asset) not in units:
                raise CorpusIndexValidationError(
                    f"{path}: embedded {embedded.get('member')!r} names unpublished {asset}"
                )


def _checks(root: Mapping[str, Any]) -> list[str]:
    rows = root.get("crossUnitChecks") or []
    names = [str(row.get("name", "")) for row in rows if isinstance(row, Mapping)]
    if names != list(CHECK_NAMES):
        raise CorpusIndexValidationError(
            f"the index publishes checks {names}, not {list(CHECK_NAMES)}"
        )
    failed = []
    for row in rows:
        passed = row.get("passed")
        if not isinstance(passed, bool):
            raise CorpusIndexValidationError(f"check {row.get('name')!r} does not state passed")
        if bool(row.get("failures")) == passed:
            raise CorpusIndexValidationError(
                f"check {row.get('name')!r} states passed={passed} with "
                f"{len(row.get('failures') or ())} failure(s)"
            )
        if not passed:
            failed.append(str(row["name"]))
    return failed


def _census(root: Mapping[str, Any], units: Mapping[str, Mapping[str, Any]]) -> None:
    """`census.byKind` is a total over `units[]`, so it is recounted from the published rows.

    A single-unit refresh rewrites one row; a census left at its pre-refresh totals would
    contradict the table it was counted from, and nothing else in the index would notice.
    """

    published = (root.get("census") or {}).get("byKind")
    if published is None:
        raise CorpusIndexValidationError("the index publishes no census.byKind")
    table: dict[str, dict[str, Any]] = {}
    for asset in sorted(units):
        row = units[asset]
        kind = kind_of(asset)
        entry = table.setdefault(
            kind, {"kind": kind, "count": 0, "byteLength": 0, "warnings": 0}
        )
        entry["count"] += 1
        entry["byteLength"] += int(row.get("byteLength", 0))
        entry["warnings"] += int((row.get("warnings") or {}).get("count", 0))
    expected = [table[kind] for kind in sorted(table)]
    if [dict(row) for row in published] != expected:
        raise CorpusIndexValidationError("census.byKind is not the total of units[]")


def _summary(root: Mapping[str, Any], counts: Mapping[str, int], units: Mapping[str, Any]) -> None:
    summary = root.get("summary")
    if not isinstance(summary, Mapping):
        raise CorpusIndexValidationError("the index publishes no summary")
    expected = {
        "members": sum(counts.values()),
        "units": len(units),
        "references": len(root.get("references") or ()),
        "unit_members": counts["unit"],
        "companions": counts["companion"],
        "residue": counts["residue"],
        "unclaimed": counts["unclaimed"],
    }
    for key, value in expected.items():
        if int(summary.get(key, -1)) != value:
            raise CorpusIndexValidationError(
                f"summary.{key} is {summary.get(key)!r}; the tables say {value}"
            )
    unresolved = (root.get("coverage") or {}).get("unresolved") or []
    if len(unresolved) != counts["unclaimed"]:
        raise CorpusIndexValidationError(
            f"coverage.unresolved holds {len(unresolved)} row(s) against "
            f"{counts['unclaimed']} unclaimed member(s)"
        )


def _files(
    units: Mapping[str, Mapping[str, Any]], export_root: Path | None
) -> dict[str, dict[str, Any]]:
    """Re-hash every unit file against its row, and keep the two tables read out of it.

    A row whose file is missing fails the index. The file is read once: its digest answers the
    `units[]` row, its `sourceResolution.members[]` answers the member table and its
    `dependencies` answer the edge table, so neither later comparison opens the corpus again.
    """

    if export_root is None:
        return {}
    root = Path(export_root)
    if not root.is_dir():
        return {}
    read: dict[str, dict[str, Any]] = {}
    for asset in sorted(units):
        row = units[asset]
        path = root / Path(*str(row["path"]).split("/"))
        if not path.is_file():
            raise CorpusIndexValidationError(f"{asset}: {row['path']} is not under the export root")
        data = path.read_bytes()
        if len(data) != int(row.get("byteLength", -1)):
            raise CorpusIndexValidationError(f"{asset}: {row['path']} is not the indexed length")
        if hashlib.sha256(data).hexdigest() != str(row.get("sha256")):
            raise CorpusIndexValidationError(f"{asset}: {row['path']} is not the indexed bytes")
        document, _ = decode_glb(data, path)
        unit_root = _unit_root(document, path)
        read[asset] = {
            "sources": list((unit_root.get("sourceResolution") or {}).get("members") or ()),
            "dependencies": list(unit_root.get("dependencies") or ()),
        }
    return read


def _unit_root(document: Mapping[str, Any], path: Path) -> Mapping[str, Any]:
    """The one `ELYSIUM_vtmb_<kind>` root a published unit declares."""

    extensions = document.get("extensions") or {}
    for name in document.get("extensionsRequired") or ():
        root = extensions.get(name)
        if str(name).startswith("ELYSIUM_vtmb_") and isinstance(root, Mapping):
            return root
    raise CorpusIndexValidationError(f"{path} declares no ELYSIUM_vtmb_<kind> extension root")


def _whole_file(row: Mapping[str, Any]) -> bool:
    """True where a unit's source row states a member's whole bytes rather than a cut span."""

    return "span" not in row and "#" not in str(row.get("path", ""))


def _origin_agrees(member: Mapping[str, Any], source: Mapping[str, Any]) -> bool:
    """The unit's origin answers the same search path the member's does.

    A seam may say more about where its bytes came from than the walk does -- a cut-table seam
    carries the table it was cut out of inside the origin -- so the member's own fields are what
    must agree, not the record as a whole.
    """

    published = source.get("origin")
    stated = member.get("origin")
    if not isinstance(published, Mapping) or not isinstance(stated, Mapping):
        return published == stated
    return all(published.get(key) == value for key, value in stated.items())


def _sources(root: Mapping[str, Any], read: Mapping[str, Mapping[str, Any]]) -> None:
    """Every `unit` member states the bytes its own unit says it was decoded from.

    The member table and the unit table are two different reads of the same install, so this is
    the one comparison that catches them describing two different files. A source row cut from
    part of a member states the part's length and digest, so it is compared on its origin alone.
    """

    if not read:
        return
    tables: dict[str, dict[str, list[Mapping[str, Any]]]] = {}
    for row in root.get("members") or ():
        if not isinstance(row, Mapping) or str(row.get("disposition", "")) != "unit":
            continue
        path = str(row.get("path", ""))
        for asset in _member_assets(row):
            unit = read.get(asset)
            if unit is None:
                continue
            table = tables.get(asset)
            if table is None:
                table = tables[asset] = {}
                for source in unit["sources"]:
                    if isinstance(source, Mapping):
                        key = str(source.get("path", "")).split("#", 1)[0]
                        table.setdefault(key, []).append(source)
            sources = table.get(path)
            if not sources:
                raise CorpusIndexValidationError(
                    f"{path}: {asset} names no source member cut from it"
                )
            from elysium_pipeline.formats.unit_contract.source_policy import selected_source
            selected = selected_source(row, asset)
            for source in sources:
                if not _origin_agrees(selected, source):
                    continue
                if not _whole_file(source):
                    break
                if int(source.get("byteLength", -1)) == int(selected.get("byteLength", -2)) and str(
                    source.get("sha256")
                ) == str(selected.get("sha256")):
                    break
            else:
                raise CorpusIndexValidationError(
                    f"{path}: the member's bytes are not the bytes {asset} was decoded from"
                )


def _dependency_edges(root: Mapping[str, Any], read: Mapping[str, Mapping[str, Any]]) -> None:
    """Every `references[]` edge is exactly one `dependencies` row of the unit it starts at."""

    if not read:
        return
    published: dict[str, list[tuple[str, str, str, bool]]] = {}
    for row in root.get("references") or ():
        if not isinstance(row, Mapping):
            continue
        published.setdefault(str(row.get("from", "")), []).append(
            (str(row.get("role", "")), str(row.get("to", "")), str(row.get("sourcePath", "")),
             bool(row.get("resolved", False)))
        )
    for asset in sorted(read):
        declared = [
            (str(row.get("role", "")), str(row.get("asset", "")), str(row.get("sourcePath", "")),
             bool(row.get("resolved", False)))
            for row in read[asset]["dependencies"]
            if isinstance(row, Mapping)
        ]
        edges = published.get(asset, [])
        if edges != declared:
            first = next(
                (row for row in declared if row not in edges),
                next((row for row in edges if row not in declared), None),
            )
            reason = (
                f"{first!r} is in one and not the other" if first is not None
                else "the two are the same rows in a different order"
            )
            raise CorpusIndexValidationError(
                f"{asset}: the edge table holds {len(edges)} edge(s) against {len(declared)} "
                f"dependency row(s) in the unit; {reason}"
            )
    for asset in published:
        if asset not in read:
            raise CorpusIndexValidationError(f"{asset}: edges from a unit the corpus does not hold")


def validate_document(
    document: Mapping[str, Any],
    binary: bytes,
    *,
    export_root: Path | None = None,
    export_time: bool = False,
) -> dict[str, Any]:
    """Validate the index before it is written, against the corpus it describes.

    `export_time` adds the edge-versus-`dependencies` comparison, which holds of the run that
    wrote both tables: a later single-unit re-export refreshes that unit's `units[]` row and
    leaves `references[]` to the next whole-corpus run, by design.
    """

    if binary:
        raise CorpusIndexValidationError("the corpus index carries no BIN chunk")
    validate_container(document, binary)
    validate_sceneless(document)
    root = _root(document)
    if (root.get("coverage") or {}).get("byteLedger"):
        raise CorpusIndexValidationError(
            "the corpus index carries no byte ledger: every byte it describes is ledgered by the "
            "unit that owns it"
        )
    counts = _members(root)
    units = _units(root)
    _reconcile(root, units)
    _references(root, units)
    failed = _checks(root)
    _census(root, units)
    _summary(root, counts, units)
    read = _files(units, export_root)
    _sources(root, read)
    if export_time:
        _dependency_edges(root, read)
    return {
        "asset": ASSET_ID,
        "schemaVersion": root.get("schemaVersion"),
        "exportRoot": (root.get("identity") or {}).get("exportRoot"),
        "members": sum(counts.values()),
        "units": len(units),
        "references": len(root.get("references") or ()),
        "byDisposition": dict(counts),
        "unclaimed": [
            str(row.get("path"))
            for row in (root.get("coverage") or {}).get("unresolved") or ()
        ],
        "failedChecks": failed,
        "orphans": sum(int(row.get("count", 0)) for row in root.get("orphans") or ()),
        "danglingReferences": sum(
            int(row.get("count", 0)) for row in root.get("danglingReferences") or ()
        ),
        "warnings": sum(
            int((row.get("warnings") or {}).get("count", 0)) for row in root.get("units") or ()
        ),
    }


def validate(path: Path) -> dict[str, Any]:
    """Read one published `index.glb` back and validate it independently of the writer.

    The export root is taken from the file's own location, so a published index found beside the
    corpus it indexes is checked against those files; one copied elsewhere is checked on its own
    tables alone.
    """

    path = Path(path)
    if path.name != OUTPUT_NAME:
        raise CorpusIndexValidationError(f"{path} is not the corpus index's {OUTPUT_NAME}")
    document, binary = read_glb(path)
    export_root = path.parent
    declared = (
        ((document.get("extensions") or {}).get(CORPUS_INDEX_EXTENSION) or {}).get("identity")
        or {}
    ).get("exportRoot")
    if declared and os.path.normcase(str(declared)) != os.path.normcase(str(export_root)):
        # The index was moved away from the corpus it describes; its tables are still checked,
        # but re-hashing files under a directory it never indexed would prove nothing.
        export_root = None
    return validate_document(document, binary, export_root=export_root)


def warnings_for(summary: Mapping[str, Any]) -> list[str]:
    """What the operator should hear about a published index that is nevertheless valid."""

    warnings: list[str] = []
    dangling = int(summary.get("danglingReferences", 0))
    orphans = int(summary.get("orphans", 0))
    warned = int(summary.get("warnings", 0))
    if dangling:
        warnings.append(f"{dangling} dangling reference(s) across the corpus")
    if orphans:
        warnings.append(f"{orphans} unit(s) no reference targets")
    if warned:
        warnings.append(f"{warned} warning(s) published by indexed units")
    return warnings
