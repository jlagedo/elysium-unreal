"""Independent structural validator for the Expression-table GLB seam.

Re-decodes the selected source independently of `exporters.expression_table_glb.build_document`
and compares every field it publishes, per `docs/architecture/seam_map_unit_contract.md`'s
validation split.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any

from elysium_pipeline.formats.expression_table_glb.decode import decode_expression_table
from elysium_pipeline.formats.expression_table_glb.model import (
    EXPRESSION_TABLE_EXTENSION,
    SCHEMA_VERSION,
    normalize_stem,
)
from elysium_pipeline.formats.expression_table_glb.source import ExpressionTableSourceClosure
from elysium_pipeline.formats.unit_contract import (
    UnitValidationError,
    completeness,
    read_glb,
    reject_opaque_source,
    validate_container,
    validate_extension_root,
    validate_index_rows,
    validate_ledgers,
    validate_sceneless,
)
from elysium_pipeline.formats.unit_contract import warnings_for as _warnings_for

ASSET_PREFIX = "vtmb:expression-table:"


class ExpressionTableGlbValidationError(ValueError):
    pass


_IDENTITY_KEYS = ("stem", "class", "sourceKind", "runtimeLoadable")
_ROOT_EXTRA_KEYS = (
    "vfe",
    "table",
    "txt",
    "authoring",
    "comparison",
    "selectedBy",
    "anomalies",
    "omissions",
)


def _closure_from_members(source_members) -> ExpressionTableSourceClosure:
    vfe = next((member for member in source_members if member.role == "vfe"), None)
    txt = next((member for member in source_members if member.role == "txt"), None)
    if vfe is None and txt is None:
        raise ExpressionTableGlbValidationError("prepublication source carries neither VFE nor TXT")
    stem = normalize_stem((vfe or txt).path)
    return ExpressionTableSourceClosure(stem, ASSET_PREFIX + stem, vfe, txt)


def _check_independent_decode(root: dict[str, Any], source_members) -> None:
    """Re-decode the members the exporter selected and compare every published field."""

    closure = _closure_from_members(source_members)
    fresh = decode_expression_table(closure)
    identity = root.get("identity") or {}
    if identity.get("asset") != fresh.asset:
        raise ExpressionTableGlbValidationError("identity.asset disagrees with an independent decode")
    if identity.get("stem") != fresh.stem:
        raise ExpressionTableGlbValidationError("identity.stem disagrees with an independent decode")
    if identity.get("class") != fresh.identity_class_:
        raise ExpressionTableGlbValidationError("identity.class disagrees with an independent decode")
    if identity.get("sourceKind") != fresh.source_kind:
        raise ExpressionTableGlbValidationError(
            "identity.sourceKind disagrees with an independent decode"
        )
    if identity.get("runtimeLoadable") != fresh.runtime_loadable:
        raise ExpressionTableGlbValidationError(
            "identity.runtimeLoadable disagrees with an independent decode"
        )
    fresh_by_key = {
        "vfe": fresh.vfe,
        "table": fresh.table,
        "txt": fresh.txt,
        "authoring": fresh.authoring,
        "comparison": fresh.comparison,
        "anomalies": fresh.anomalies,
        "omissions": fresh.omissions,
    }
    for key, expected in fresh_by_key.items():
        if root.get(key) != expected:
            raise ExpressionTableGlbValidationError(f"{key} disagrees with an independent decode")
    coverage = root.get("coverage") or {}
    for key, expected in (
        ("typedUnidentified", fresh.typed_unidentified),
        ("unresolved", fresh.unresolved),
        ("unsupported", fresh.unsupported),
        ("byteLedger", fresh.byte_ledger),
    ):
        if coverage.get(key) != expected:
            raise ExpressionTableGlbValidationError(
                f"coverage.{key} disagrees with an independent decode"
            )


def _check_table_shape(name: str, block: dict[str, Any] | None) -> None:
    """Every row's `values` (and `weights`, when `hasWeighting`) has one entry per `keys`."""

    if block is None:
        return
    keys = block.get("keys") or []
    has_weighting = bool(block.get("hasWeighting"))
    for index, row in enumerate(block.get("rows") or []):
        values = row.get("values") or []
        if len(values) != len(keys):
            raise ExpressionTableGlbValidationError(
                f"{name}.rows[{index}].values has {len(values)} entries, {name}.keys has {len(keys)}"
            )
        if has_weighting:
            weights = row.get("weights") or []
            if len(weights) != len(keys):
                raise ExpressionTableGlbValidationError(
                    f"{name}.rows[{index}].weights has {len(weights)} entries, "
                    f"{name}.keys has {len(keys)}"
                )


def validate_document(document: dict, binary: bytes, *, source_members=None) -> dict[str, Any]:
    root = validate_extension_root(
        document, EXPRESSION_TABLE_EXTENSION, asset_prefix=ASSET_PREFIX, schema_version=SCHEMA_VERSION
    )
    validate_container(document, binary)
    validate_sceneless(document)
    if binary:
        raise ExpressionTableGlbValidationError("an expression-table unit carries no BIN chunk")

    identity = root.get("identity") or {}
    missing_identity = [key for key in _IDENTITY_KEYS if key not in identity]
    if missing_identity:
        raise ExpressionTableGlbValidationError(f"identity is missing {', '.join(missing_identity)}")
    if identity["class"] not in ("expressions", "phonemes", "none"):
        raise ExpressionTableGlbValidationError(f"identity.class {identity['class']!r} is unknown")
    if identity["sourceKind"] not in ("vfe+txt", "vfe-only", "txt-only"):
        raise ExpressionTableGlbValidationError(
            f"identity.sourceKind {identity['sourceKind']!r} is unknown"
        )
    if identity["runtimeLoadable"] != (identity["sourceKind"] != "txt-only"):
        raise ExpressionTableGlbValidationError(
            "identity.runtimeLoadable disagrees with identity.sourceKind"
        )

    missing_root = [key for key in _ROOT_EXTRA_KEYS if key not in root]
    if missing_root:
        raise ExpressionTableGlbValidationError(f"extension root is missing {', '.join(missing_root)}")
    # Empty as the seam exported it, one `{from, role}` row per selecting model or scene once
    # the corpus index has written its inverse into the unit.
    try:
        validate_index_rows(root["selectedBy"], "selectedBy")
    except UnitValidationError as error:
        raise ExpressionTableGlbValidationError(str(error)) from error
    if root["dependencies"] != []:
        raise ExpressionTableGlbValidationError("an expression-table unit resolves no dependency")
    if (identity["sourceKind"] == "vfe-only") != (root["txt"] is None):
        raise ExpressionTableGlbValidationError("txt presence disagrees with sourceKind")
    if (identity["sourceKind"] == "txt-only") != (root["vfe"] is None):
        raise ExpressionTableGlbValidationError("vfe presence disagrees with sourceKind")
    if root["vfe"] is not None and root["vfe"].get("alternateLayout") is False and root["table"] is None:
        raise ExpressionTableGlbValidationError("a normal-layout VFE publishes no table")

    coverage = root.get("coverage") or {}
    _check_table_shape("table", root.get("table"))
    _check_table_shape("authoring", root.get("authoring"))
    if root.get("table") is not None and root.get("authoring") is not None:
        table_rows = len(root["table"].get("rows") or [])
        authoring_rows = len(root["authoring"].get("rows") or [])
        if table_rows != authoring_rows:
            # A row-count disagreement between the VFE-derived `table` and the TXT-derived
            # `authoring` is a real, disclosed provenance fact for a handful of corpus units (the
            # internal-name-mismatched VFEs whose content belongs to a different table entirely);
            # `compare_tables` records it as an `unresolved` row rather than refusing the unit. An
            # *undisclosed* disagreement -- no such row -- is a tampered or malformed publication.
            disclosed = any(
                row.get("field") == "rows" for row in coverage.get("unresolved") or []
            )
            if not disclosed:
                raise ExpressionTableGlbValidationError(
                    f"table has {table_rows} rows, authoring has {authoring_rows}, "
                    "undisclosed in coverage.unresolved"
                )
    # A complete unit has zero `unresolved`/`unsupported` rows, but neither failure is this
    # seam's to enforce here: a comparison the doc's six named states cannot classify (an
    # internal-name-mismatched VFE whose content belongs to a different table entirely) is a
    # provenance fact, not a decode defect, and the contract's own rule is to publish what the
    # install holds and warn rather than refuse the unit.
    counts = completeness(root)

    members = (root.get("sourceResolution") or {}).get("members") or []
    validate_ledgers(root, source_members)
    reject_opaque_source(document, binary, source_members)

    if source_members is not None:
        _check_independent_decode(root, source_members)

    warnings = _warnings_for(root)
    rows = 0
    if root.get("table"):
        rows = len(root["table"].get("rows") or [])
    elif root.get("authoring"):
        rows = len(root["authoring"].get("rows") or [])
    source_bytes = sum(int(row.get("byteLength", 0)) for row in coverage.get("byteLedger") or [])
    accounted_bytes = sum(int(row.get("accountedBytes", 0)) for row in coverage.get("byteLedger") or [])
    return {
        "asset": identity["asset"],
        "stem": identity["stem"],
        "sourceKind": identity["sourceKind"],
        "comparisonState": root["comparison"].get("state"),
        "rows": rows,
        "members": len(members),
        "typedUnidentified": counts["typedUnidentified"],
        "sourceBytes": source_bytes,
        "accountedBytes": accounted_bytes,
        "byteCoveragePercent": 100.0 if source_bytes == accounted_bytes else 0.0,
        "warnings": warnings,
    }


def validate(path: Path) -> dict[str, Any]:
    document, binary = read_glb(path)
    return validate_document(document, binary)


def warnings_for(summary: dict[str, Any]) -> list[str]:
    return list(summary.get("warnings") or [])
