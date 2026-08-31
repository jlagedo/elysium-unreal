"""Independent structural validator for ui-resource GLB products.

The kind-independent checks (container, scene-less rule, extension root, byte ledger, source
capsule) are `elysium_pipeline.formats.unit_contract`'s. What is specific here is: the grammar's
own field shape, and -- when `source_members` is supplied, i.e. at export time -- an independent
re-decode of the member bytes, compared against what the document actually published. This
re-decode calls `formats.ui_resource_glb.decode`/`grammars` directly rather than
`exporters.ui_resource_glb.build_document`, so a writer bug that decoded once and emitted
something else is still caught. It skips the *resolution*-dependent half of a dependency (whether
a material, texture, sound, particle, gameui token or included file exists in the install),
because verifying that would need the install index this validator is never given -- the same
limit `validation.vdata_glb` accepts for its own resolver-dependent projection fields.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any, Mapping

from elysium_pipeline.formats.unit_contract import (
    UnitValidationError,
    completeness,
    read_glb as _read_glb,
    validate_capsules,
    validate_container,
    validate_extension_root,
    validate_ledgers,
    validate_sceneless,
)
from elysium_pipeline.formats.unit_contract.origin import Origin, SourceMember
from elysium_pipeline.formats.ui_resource_glb import lexer
from elysium_pipeline.formats.ui_resource_glb.decode import Resolvers, decode_ui_resource
from elysium_pipeline.formats.ui_resource_glb.model import (
    CATEGORIES,
    GRAMMARS,
    SCHEMA_VERSION,
    UI_RESOURCE_EXTENSION,
    classify,
    dormant_evidence as _dormant_evidence_for,
    encoding_of,
    grammar_of,
)
from elysium_pipeline.formats.ui_resource_glb.source import UiResourceSourceClosure

ASSET_PREFIX = "vtmb:ui-resource:"

#: Every `anomalies[]` role this decoder is allowed to publish: the four `seam_map_ui_resource.md`
#: names, plus the ones the decoder needed and named in `specDeviations`.
_ANOMALY_ROLES = {
    "repeated-key",
    "unterminated-block",
    "mixed-line-endings",
    "non-ascii-latin1",
    "valueless-key",
    "unquoted-token-with-space",
}

_PROJECTION_FIELDS = (
    "scheme", "layout", "menu", "hud", "titles", "rows", "substitutions", "strings",
    "options", "menuScene",
)


class UiResourceGlbValidationError(UnitValidationError):
    pass


def read_glb(path: Path) -> tuple[dict[str, Any], bytes]:
    try:
        return _read_glb(Path(path))
    except UnitValidationError as error:
        raise UiResourceGlbValidationError(str(error)) from error


#: Keys whose value depends on an install-side resolver this validator is never given: whether a
#: reference resolves (`resolved`), and a substituted string's `$key` expansion, which depends on
#: the `#include`d file's own definitions (`substitutions.strings[].substituted`).
_RESOLVER_DEPENDENT_KEYS = frozenset({"resolved", "substituted"})


def _scrub_resolved(value: Any) -> Any:
    """Drop every resolver-dependent key so a comparison ignores the install-dependent half of a
    typed projection's embedded reference fields."""

    if isinstance(value, Mapping):
        return {
            key: _scrub_resolved(item)
            for key, item in value.items()
            if key not in _RESOLVER_DEPENDENT_KEYS
        }
    if isinstance(value, list):
        return [_scrub_resolved(item) for item in value]
    return value


def _verify_utf16_offsets(root: Mapping[str, Any], data: bytes) -> None:
    """`data[offset:offset+length]`, decoded and re-tokenized independently of the writer, names
    the same key and value the document publishes at that offset -- `seam_map_ui_resource.md`'s
    "Encoding" names this literal check for a UTF-16 LE member: "re-decodes the code units and
    checks each token's ... text against its stated offset and length". A `strings.tokens[]`
    entry's `byteOffset`/`byteLength` span the whole `"Key" "Value"` record (`decode.py`'s
    `_make_scalar`), so the span is re-tokenized rather than decoded and compared bare."""

    for token in (root.get("strings") or {}).get("tokens") or []:
        offset, length = token.get("byteOffset"), token.get("byteLength")
        if not isinstance(offset, int) or not isinstance(length, int):
            continue
        chunk = data[offset:offset + length]
        if len(chunk) != length:
            raise UiResourceGlbValidationError(
                f"strings token {token.get('name')!r} names an out-of-range byte span"
            )
        text = lexer.decode_text(chunk, "utf-16-le")
        record_tokens = [t for t in lexer.tokenize(text, "utf-16-le") if t.kind == "string"]
        if len(record_tokens) < 2:
            raise UiResourceGlbValidationError(
                f"strings token {token.get('name')!r} names a byte span with no key/value pair"
            )
        key_token, value_token = record_tokens[0], record_tokens[1]
        if key_token.decoded() != token.get("name") or value_token.decoded() != token.get("text"):
            raise UiResourceGlbValidationError(
                f"strings token {token.get('name')!r} disagrees with its stated byte offset/length"
            )
    for entry in (root.get("rows") or {}).get("entries") or []:
        for cell in entry.get("cells") or []:
            offset, length = cell.get("offset"), cell.get("length")
            if not isinstance(offset, int) or not isinstance(length, int):
                continue
            chunk = data[offset:offset + length]
            if len(chunk) != length:
                raise UiResourceGlbValidationError("a row cell names an out-of-range byte span")
            decoded = chunk.decode("utf-16-le", errors="strict")
            if cell.get("quoted") and len(decoded) >= 2:
                decoded = decoded[1:-1]
            if decoded != cell.get("text"):
                raise UiResourceGlbValidationError(
                    "a row cell disagrees with its stated byte offset/length"
                )


def _verify_dependencies(root: Mapping[str, Any], model) -> None:
    """The published `dependencies` table names exactly the references an independent re-decode
    finds -- `resolved` aside, which needs the install resolver this validator never has."""

    published = sorted(
        (str(row.get("role")), str(row.get("asset")), str(row.get("sourcePath")))
        for row in root.get("dependencies") or []
    )
    redecoded = sorted(
        (row["role"], row["asset"], row["sourcePath"]) for row in model.dependencies
    )
    if published != redecoded:
        raise UiResourceGlbValidationError("dependencies disagree with an independent re-decode")


def _verify_independent_decode(root: Mapping[str, Any], path: str, data: bytes) -> None:
    """Re-decode `data` with no dependency on the writer, and compare every projection field."""

    category = classify(path)
    grammar = grammar_of(path)
    if category is None or grammar is None:
        raise UiResourceGlbValidationError(f"{path} is not a ui-resource member")
    origin = Origin(kind="loose", root="revalidation")
    member = SourceMember(role="unit-selecting", path=path, data=data, origin=origin)
    closure = UiResourceSourceClosure(
        key=path, asset=root["identity"]["asset"], category=category, grammar=grammar,
        encoding=encoding_of(path), member=member,
    )
    model = decode_ui_resource(closure, resolvers=Resolvers())

    if root.get("grammar") != model.grammar:
        raise UiResourceGlbValidationError("grammar disagrees with an independent re-decode")
    if root.get("encoding") != model.encoding:
        raise UiResourceGlbValidationError("encoding disagrees with an independent re-decode")
    if _scrub_resolved(root.get("tree")) != _scrub_resolved(model.tree):
        raise UiResourceGlbValidationError("tree disagrees with an independent re-decode")

    field_map = {
        "scheme": model.scheme, "layout": model.layout, "menu": model.menu, "hud": model.hud,
        "titles": model.titles, "rows": model.rows, "substitutions": model.substitutions,
        "strings": model.strings, "options": model.options, "menuScene": model.menu_scene,
    }
    for field, decoded_value in field_map.items():
        if _scrub_resolved(root.get(field)) != _scrub_resolved(decoded_value):
            raise UiResourceGlbValidationError(f"{field} disagrees with an independent re-decode")

    if [row.get("text") for row in root.get("comments") or []] != [
        row.get("text") for row in model.comments
    ]:
        raise UiResourceGlbValidationError("comments disagree with an independent re-decode")
    published_anomaly_roles = sorted(str(row.get("role")) for row in root.get("anomalies") or [])
    redecoded_anomaly_roles = sorted(str(row.get("role")) for row in model.anomalies)
    if published_anomaly_roles != redecoded_anomaly_roles:
        raise UiResourceGlbValidationError("anomalies disagree with an independent re-decode")

    _verify_dependencies(root, model)
    if model.encoding == "utf-16-le":
        _verify_utf16_offsets(root, data)

    published_ledger = ((root.get("coverage") or {}).get("byteLedger") or [None])[0]
    if published_ledger != model.ledger_row:
        raise UiResourceGlbValidationError(
            "the published byte ledger disagrees with an independent re-decode"
        )


def validate_document(
    document: Mapping[str, Any], binary: bytes, *, source_members=None
) -> dict[str, Any]:
    root = validate_extension_root(
        document, UI_RESOURCE_EXTENSION, asset_prefix=ASSET_PREFIX, schema_version=SCHEMA_VERSION
    )
    validate_container(document, binary)
    validate_sceneless(document)
    validate_ledgers(root, source_members)
    validate_capsules(document, binary, root, source_members)

    # `seam_map_ui_resource.md`, "GLB structure": "Every unit is scene-less with no BIN chunk".
    # `validate_container` permits a *consistent* BIN chunk (buffer/view/binary agree); this seam
    # never carries one at all, so any BIN payload -- consistent or not -- is itself the defect.
    if binary or document.get("buffers") or document.get("bufferViews"):
        raise UiResourceGlbValidationError("a ui-resource unit publishes a BIN chunk")

    grammar = root.get("grammar")
    if grammar not in GRAMMARS:
        raise UiResourceGlbValidationError(f"unknown grammar {grammar!r}")
    if root.get("encoding") not in ("latin-1", "utf-16-le"):
        raise UiResourceGlbValidationError(f"unknown encoding {root.get('encoding')!r}")

    identity = root.get("identity") or {}
    category = identity.get("category")
    if category not in CATEGORIES:
        raise UiResourceGlbValidationError(f"identity names an unknown category {category!r}")

    identity_source_path = identity.get("sourcePath")
    if identity_source_path:
        expected_grammar = grammar_of(str(identity_source_path))
        expected_encoding = encoding_of(str(identity_source_path))
        expected_category = classify(str(identity_source_path))
        if grammar != expected_grammar:
            raise UiResourceGlbValidationError(
                f"grammar {grammar!r} disagrees with {identity_source_path!r}'s own "
                f"{expected_grammar!r}"
            )
        if root.get("encoding") != expected_encoding:
            raise UiResourceGlbValidationError(
                f"encoding {root.get('encoding')!r} disagrees with {identity_source_path!r}'s "
                f"own {expected_encoding!r}"
            )
        if category != expected_category:
            raise UiResourceGlbValidationError(
                f"category {category!r} disagrees with {identity_source_path!r}'s own "
                f"{expected_category!r}"
            )

    if not isinstance(identity.get("dormant"), bool):
        raise UiResourceGlbValidationError("identity does not state whether the unit is dormant")
    source_path = identity.get("sourcePath")
    expected_evidence = _dormant_evidence_for(str(source_path)) if source_path else None
    if identity.get("dormant") != (expected_evidence is not None):
        raise UiResourceGlbValidationError(
            f"identity.dormant disagrees with the source closure for {source_path!r}"
        )
    if expected_evidence is not None and not identity.get("dormantEvidence"):
        raise UiResourceGlbValidationError("a dormant unit publishes no dormantEvidence")
    if expected_evidence is None and "dormantEvidence" in identity:
        raise UiResourceGlbValidationError("a live unit publishes dormantEvidence")

    populated = [field for field in _PROJECTION_FIELDS if root.get(field) is not None]
    if len(populated) > 1:
        raise UiResourceGlbValidationError(
            f"a unit populates more than one typed projection: {populated}"
        )

    for row in root.get("anomalies") or []:
        if not isinstance(row, Mapping) or row.get("role") not in _ANOMALY_ROLES:
            raise UiResourceGlbValidationError(f"unknown source anomaly {row!r}")

    # seam_map_unit_contract.md's ledger table: `omitted-proven` is "an evidence-backed omission
    # carrying its reason in `omissions` or `coverage.omittedProven`" -- a range claiming that
    # state with neither published is an omission with no evidence behind it.
    ledger_rows_for_evidence = (root.get("coverage") or {}).get("byteLedger") or [{}]
    has_omitted_proven_range = any(
        row.get("state") == "omitted-proven" for row in ledger_rows_for_evidence[0].get("ranges") or []
    )
    if has_omitted_proven_range and not (
        (root.get("coverage") or {}).get("omittedProven") or root.get("omissions")
    ):
        raise UiResourceGlbValidationError(
            "a ledger range claims omitted-proven with no omittedProven/omissions evidence"
        )

    completeness_counts = completeness(root)
    if completeness_counts["unresolved"] or completeness_counts["unsupported"]:
        raise UiResourceGlbValidationError("ui-resource extension is incomplete")

    if source_members:
        members = {member.path: member.data for member in source_members}
        member_path = ((root.get("sourceResolution") or {}).get("members") or [{}])[0].get("path")
        data = members.get(member_path)
        if data is None:
            raise UiResourceGlbValidationError("the source member for re-decode is missing")
        if data:
            _verify_independent_decode(root, member_path, data)
        elif not root.get("omissions"):
            raise UiResourceGlbValidationError("an empty member publishes no empty-member omission")

    ledger_rows = (root.get("coverage") or {}).get("byteLedger") or [{}]
    return {
        "asset": str(identity.get("asset", "")),
        "category": category,
        "grammar": grammar,
        "encoding": root.get("encoding"),
        "dependencies": len(root.get("dependencies") or []),
        "unresolvedDependencies": [
            row for row in root.get("dependencies") or [] if row.get("resolved") is False
        ],
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
    if summary.get("anomalies"):
        warnings.append(f"the source departs from the format's conventions: {', '.join(summary['anomalies'])}")
    if summary.get("typedUnidentified"):
        warnings.append(f"typed but unidentified: {summary['typedUnidentified']} value(s)")
    for row in summary.get("unresolvedDependencies") or []:
        warnings.append(f"unresolved reference: {row.get('role')} {row.get('sourcePath')!r}")
    return warnings
