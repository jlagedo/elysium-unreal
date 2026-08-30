"""Independent structural validator for vdata GLB products.

The kind-independent checks (container, scene-less rule, extension root, byte ledger, opaque
source) are `elysium_pipeline.formats.unit_contract`'s. What is specific here is: the grammar
shape (`tree` xor `rows`), and -- when `source_members` is supplied, i.e. at export time -- an
independent re-tokenize and re-parse of the member bytes, compared field-for-field against what
the document actually published. This re-decode calls the seam's own lexer and its own
`projection.build_projection` directly rather than `exporters.vdata_glb.build_document` or
`formats.vdata_glb.decode`'s own row-classification helpers, so a writer bug that decoded once and
emitted something else is still caught rather than compared against itself. It skips the
*resolution*-dependent half of `projection` (whether a model, sound group or precache asset exists
in the install), because verifying that would need the install index this validator is never
given, the same limit `validation.surface_property_glb` accepts for `base_exists`/
`sound_script_exists`.
"""

from __future__ import annotations

from collections import Counter
from pathlib import Path
from typing import Any, Mapping

from elysium_pipeline.formats.unit_contract import (
    UnitValidationError,
    completeness,
    read_glb as _read_glb,
    reject_opaque_source,
    validate_container,
    validate_extension_root,
    validate_ledgers,
    validate_sceneless,
)
from elysium_pipeline.formats.vdata_glb import lexer
from elysium_pipeline.formats.vdata_glb import projection as projection_module
from elysium_pipeline.formats.vdata_glb.model import SCHEMA_VERSION, VDATA_EXTENSION

ASSET_PREFIX = "vtmb:vdata:"

_GRAMMARS = {"keyvalues", "delimited", "freeform"}

#: Every `anomalies[]` role the decoder is allowed to publish (`seam_map_vdata.md`, "Anomalies and
#: omissions"), plus the three this decoder needed and named in `specDeviations`.
_ANOMALY_ROLES = {
    "repeated-scalar-key",
    "unbalanced-braces",
    "unquoted-token-with-space",
    "non-ascii-byte",
    "commented-key",
    "duplicate-declaration",
    "terminal-field-over-limit",
    "variant-divergence",
    "valueless-key",
}
_VARIANTS = {"base", "vampire", "hunter"}


class VdataGlbValidationError(UnitValidationError):
    pass


def read_glb(path: Path) -> tuple[dict[str, Any], bytes]:
    try:
        return _read_glb(Path(path))
    except UnitValidationError as error:
        raise VdataGlbValidationError(str(error)) from error


def _check_node(node: Any, path: str) -> None:
    if not isinstance(node, Mapping):
        raise VdataGlbValidationError(f"tree node {path} is not a record")
    kind = node.get("kind")
    if kind not in ("scalar", "block", "directive"):
        raise VdataGlbValidationError(f"tree node {path} has unknown kind {kind!r}")
    if not isinstance(node.get("offset"), int) or not isinstance(node.get("length"), int):
        raise VdataGlbValidationError(f"tree node {path} has no byte span")
    if kind == "block":
        children = node.get("children")
        if not isinstance(children, list):
            raise VdataGlbValidationError(f"tree node {path} has no children list")
        for child in children:
            _check_node(child, f"{path}.{child.get('index') if isinstance(child, Mapping) else '?'}")
    elif kind == "scalar":
        if "value" not in node or "escapes" not in node:
            raise VdataGlbValidationError(f"tree node {path} carries no value/escapes")
    else:
        if "name" not in node or "argument" not in node:
            raise VdataGlbValidationError(f"tree node {path} carries no directive name/argument")


def _check_rows(rows: Any) -> None:
    if not isinstance(rows, list):
        raise VdataGlbValidationError("a unit publishes no row list")
    for row in rows:
        if not isinstance(row, Mapping):
            raise VdataGlbValidationError("a row is not a record")
        if not isinstance(row.get("offset"), int) or not isinstance(row.get("length"), int):
            raise VdataGlbValidationError("a row has no byte span")
        if not isinstance(row.get("kind"), str):
            raise VdataGlbValidationError("a row names no kind")


def _line_spans(text: str) -> list[tuple[int, int]]:
    """`(offset, end)` for every physical line, terminator included, gapless over `text`.

    A fresh implementation of the same rule `formats.vdata_glb.decode._line_spans` states, kept
    separate so a bug in that module's own copy cannot cancel itself out in the comparison below.
    """

    spans: list[tuple[int, int]] = []
    start, total = 0, len(text)
    while start < total:
        newline = text.find("\n", start)
        end = total if newline < 0 else newline + 1
        spans.append((start, end))
        start = end
    return spans


def _redecode_delimited_rows(text: str) -> list[dict[str, Any]]:
    """An independent re-derivation of the delimited grammar's row classification, written fresh
    from `seam_map_vdata.md`'s own rule rather than by importing the writer's row builder."""

    rows: list[dict[str, Any]] = []
    for index, (start, end) in enumerate(_line_spans(text)):
        content = text[start:end].rstrip("\r\n")
        stripped = content.strip()
        if stripped.lower().startswith("> total experience value"):
            kind = "header"
        elif content.startswith(">"):
            kind = "comment"
        elif len(content) < 3:
            kind = "skipped"
        else:
            parts = content.split("|")
            kind = "data" if len(parts) >= 3 else "unparsed"
        row: dict[str, Any] = {"index": index, "kind": kind, "offset": start, "length": end - start}
        if kind == "data":
            parts = content.split("|")
            row["key"] = parts[0].strip()
            row["description"] = parts[1].strip()
            row["value"] = "|".join(parts[2:]).strip()
        else:
            row["text"] = content
        rows.append(row)
    return rows


def _redecode_freeform_rows(text: str) -> list[dict[str, Any]]:
    """An independent re-derivation of the freeform grammar's one row shape."""

    rows: list[dict[str, Any]] = []
    for index, (start, end) in enumerate(_line_spans(text)):
        content = text[start:end].rstrip("\r\n")
        rows.append({"index": index, "kind": "line", "text": content, "offset": start, "length": end - start})
    return rows


def _strip_resolution(value: Any) -> Any:
    """`projection` with every install-resolution-dependent leaf blanked out, so two projections
    built with different (or absent) resolvers can still be compared on everything else: field
    identity, shape and the values the source text alone determines. `resolved` is the field every
    resolver-backed lookup this seam publishes carries (`model`/`asset` presence, sound-group
    membership, precache entries); `members` is the sound-group's own resolver-built list."""

    if isinstance(value, Mapping):
        return {
            key: (None if key in ("resolved", "members") else _strip_resolution(child))
            for key, child in value.items()
        }
    if isinstance(value, list):
        return [_strip_resolution(item) for item in value]
    return value


def _verify_independent_decode(root: Mapping[str, Any], path: str, data: bytes) -> None:
    """Re-tokenize and re-parse `data` with no dependency on the writer, and compare."""

    grammar = root.get("grammar")
    text = lexer.decode_text(data)
    if grammar == "delimited":
        if _redecode_delimited_rows(text) != (root.get("rows") or []):
            raise VdataGlbValidationError("rows disagree with an independent re-decode")
        return
    if grammar == "freeform":
        if _redecode_freeform_rows(text) != (root.get("rows") or []):
            raise VdataGlbValidationError("rows disagree with an independent re-decode")
        return

    tokens = lexer.tokenize(text)
    significant = [token for token in tokens if token.kind in ("string", "open", "close")]
    tree = root.get("tree") or {}
    if significant and not lexer.has_block_structure(tokens):
        raise VdataGlbValidationError("the source no longer tokenizes as keyvalues")
    if not significant:
        top_nodes, unparsed_offset = [], None
    else:
        top_nodes, _, _, unparsed_offset = lexer.parse_tree(tokens, text)
    if top_nodes != (tree.get("children") or []):
        raise VdataGlbValidationError("tree disagrees with an independent re-decode")
    if unparsed_offset != tree.get("unparsedOffset"):
        raise VdataGlbValidationError("unparsed offset disagrees with an independent re-decode")

    root_key = top_nodes[0]["sourceKey"] if top_nodes else None
    rebuilt_projection, _, _ = projection_module.build_projection(root_key, top_nodes)
    if _strip_resolution(rebuilt_projection) != _strip_resolution(root.get("projection")):
        raise VdataGlbValidationError("projection disagrees with an independent re-decode")


def validate_document(
    document: Mapping[str, Any], binary: bytes, *, source_members=None
) -> dict[str, Any]:
    root = validate_extension_root(
        document, VDATA_EXTENSION, asset_prefix=ASSET_PREFIX, schema_version=SCHEMA_VERSION
    )
    validate_container(document, binary)
    validate_sceneless(document)
    validate_ledgers(root, source_members)
    reject_opaque_source(document, binary, source_members)

    grammar = root.get("grammar")
    if grammar not in _GRAMMARS:
        raise VdataGlbValidationError(f"unknown grammar {grammar!r}")
    tree, rows = root.get("tree"), root.get("rows")
    if grammar == "keyvalues":
        if not isinstance(tree, Mapping) or not isinstance(tree.get("children"), list):
            raise VdataGlbValidationError("a keyvalues unit publishes no tree")
        for child in tree["children"]:
            _check_node(child, str(child.get("index") if isinstance(child, Mapping) else "?"))
        children = tree["children"]
        root_key = root.get("rootKey")
        if children:
            if root_key != children[0].get("sourceKey"):
                raise VdataGlbValidationError("rootKey disagrees with the tree's first node")
        elif root_key is not None:
            raise VdataGlbValidationError("rootKey is set but the tree has no nodes")
        if rows:
            raise VdataGlbValidationError("a keyvalues unit publishes rows")
    else:
        if tree is not None:
            raise VdataGlbValidationError(f"a {grammar} unit publishes a tree")
        _check_rows(rows)

    for row in root.get("anomalies") or []:
        if not isinstance(row, Mapping) or row.get("role") not in _ANOMALY_ROLES:
            raise VdataGlbValidationError(f"unknown source anomaly {row!r}")

    projection = root.get("projection")
    if not isinstance(projection, Mapping) or projection.get("vocabulary") not in ("open", "closed"):
        raise VdataGlbValidationError("projection publishes no vocabulary state")

    identity = root.get("identity") or {}
    asset = str(identity.get("asset", ""))
    subtree = identity.get("subtree")
    variant = identity.get("variant")
    if not subtree:
        raise VdataGlbValidationError("identity names no subtree")
    if variant not in _VARIANTS:
        raise VdataGlbValidationError(f"identity names an unknown variant {variant!r}")
    if identity.get("vdataPath") != identity.get("sourcePath"):
        raise VdataGlbValidationError("vdataPath disagrees with sourcePath")

    completeness_counts = completeness(root)
    if completeness_counts["unresolved"] or completeness_counts["unsupported"]:
        raise VdataGlbValidationError("vdata extension is incomplete")

    if source_members:
        members = {member.path: member.data for member in source_members}
        member_path = ((root.get("sourceResolution") or {}).get("members") or [{}])[0].get("path")
        data = members.get(member_path)
        if data is None:
            raise VdataGlbValidationError("the source member for re-decode is missing")
        if data:
            _verify_independent_decode(root, member_path, data)
        elif not root.get("omissions"):
            raise VdataGlbValidationError("an empty member publishes no empty-member omission")

    ledger_rows = (root.get("coverage") or {}).get("byteLedger") or [{}]
    #: A dependency the install could not resolve is a fact about another seam's corpus, not a
    #: defect in this unit -- surfaced as a warning (`warnings_for`), never a validation failure
    #: (`validation/sound_script_glb.py` states the same rule for its own dependency table).
    unresolved_dependencies = [
        str(row.get("asset")) for row in root.get("dependencies") or [] if not row.get("resolved")
    ]
    return {
        "asset": asset,
        "subtree": subtree,
        "variant": variant,
        "grammar": grammar,
        "rootKey": root.get("rootKey"),
        "projectionKind": projection.get("kind"),
        "dependencies": len(root.get("dependencies") or []),
        "unresolvedDependencies": unresolved_dependencies,
        "anomalies": [str(row.get("role")) for row in root.get("anomalies") or []],
        "omissions": list(root.get("omissions") or []),
        "typedUnidentified": completeness_counts["typedUnidentified"],
        "unresolved": completeness_counts["unresolved"],
        "unsupported": completeness_counts["unsupported"],
        "byteCoveragePercent": ledger_rows[0].get("coveragePercent"),
    }


def validate(path: Path) -> dict[str, Any]:
    document, binary = read_glb(path)
    return validate_document(document, binary)


def warnings_for(summary: dict[str, Any]) -> list[str]:
    warnings: list[str] = []
    for row in summary.get("omissions") or []:
        reason = row.get("reason", row) if isinstance(row, Mapping) else row
        warnings.append(f"omitted: {reason}")
    anomalies = summary.get("anomalies") or []
    if anomalies:
        counted = ", ".join(f"{role}x{count}" for role, count in sorted(Counter(anomalies).items()))
        warnings.append(f"the source departs from the format's conventions: {counted}")
    if summary.get("typedUnidentified"):
        warnings.append(f"typed but unidentified: {summary['typedUnidentified']} value(s)")
    unresolved = summary.get("unresolvedDependencies") or []
    if unresolved:
        shown = sorted(set(unresolved))
        counted = ", ".join(shown[:4]) + (f" and {len(shown) - 4} more" if len(shown) > 4 else "")
        warnings.append(f"the install carries no target for: {counted}")
    return warnings
