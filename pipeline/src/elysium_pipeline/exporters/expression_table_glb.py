"""Isolated one-stem Expression-table GLB product writer."""

from __future__ import annotations

from pathlib import Path

from elysium_pipeline.formats.expression_table_glb import (
    EXPRESSION_TABLE_EXTENSION,
    SCHEMA_VERSION,
    decode_expression_table,
    load_source_closure,
    output_relative_path,
)
from elysium_pipeline.formats.expression_table_glb import source as expression_table_source
from elysium_pipeline.formats.unit_contract import (
    asset_block,
    coverage_block,
    extension_root,
    identity_block,
    plain,
    source_resolution,
    write_glb,
)


def build_document(model) -> tuple[dict, bytes]:
    """The unit is scene-less with no BIN chunk: a table is at most 48x48 numbers, carried
    entirely inside the extension rather than restated through a core accessor."""

    identity = identity_block(
        model.asset,
        [member.path for member in model.members],
        stem=model.stem,
        **{"class": model.identity_class_},
        sourceKind=model.source_kind,
        runtimeLoadable=model.runtime_loadable,
    )
    mapped = [
        name
        for name, value in (
            ("vfe", model.vfe),
            ("table", model.table),
            ("txt", model.txt),
            ("authoring", model.authoring),
        )
        if value is not None
    ]
    mapped.append("comparison")
    coverage = coverage_block(
        mapped=mapped,
        typed_unidentified=model.typed_unidentified,
        byte_ledger=model.byte_ledger,
        unresolved=model.unresolved,
        unsupported=model.unsupported,
    )
    root = extension_root(
        schema_version=SCHEMA_VERSION,
        identity=identity,
        source_resolution=source_resolution(model.members),
        dependencies=[],
        coverage=coverage,
        vfe=model.vfe,
        table=model.table,
        txt=model.txt,
        authoring=model.authoring,
        comparison=model.comparison,
        selectedBy=[],
        anomalies=model.anomalies,
        omissions=model.omissions,
    )
    document = {
        "asset": asset_block("Expression-table"),
        "extensionsUsed": [EXPRESSION_TABLE_EXTENSION],
        "extensionsRequired": [EXPRESSION_TABLE_EXTENSION],
        "extensions": {EXPRESSION_TABLE_EXTENSION: plain(root)},
    }
    return document, b""


def source_keys(index: dict) -> list[str]:
    """Every stem the UP-first install resolves under `expressions/`."""

    return expression_table_source.source_keys(index)


def export(index: dict, stem: str, output_root: Path, *, read_bytes=None) -> Path:
    closure = load_source_closure(index, stem, read_bytes=read_bytes)
    model = decode_expression_table(closure)
    document, binary = build_document(model)

    from elysium_pipeline.validation import expression_table_glb as validation

    validation.validate_document(document, binary, source_members=closure.members())
    destination = output_root / Path(*output_relative_path(model.stem).parts)
    write_glb(document, binary, destination)
    return destination
