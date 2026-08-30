"""Isolated shader-program GLB product writer.

Every record the decoders publish is already JSON-plain -- dictionaries, lists and scalars -- so
the extension root goes into the document as it stands. One compiled bundle holds half a million
tokens, and a coercion pass would copy the whole tree again for nothing.

Two kinds, one module: `export_shader_source` writes one `vtmb:shader-source:<stem>` unit below
`source/`, and `export` writes one `vtmb:shader-program:<subdir>/<stem>` unit below `<subdir>/`.
Both are scene-less, carry no BIN chunk and keep no copy of the bytes they were decoded from --
the byte ledger is what makes that absence safe -- and both run export-time validation against the
selected members before anything is written.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any

from elysium_pipeline.formats.shader_program_glb import (
    PROGRAM_GENERATOR_TITLE,
    SCHEMA_VERSION,
    SHADER_PROGRAM_EXTENSION,
    SHADER_SOURCE_EXTENSION,
    SOURCE_GENERATOR_TITLE,
    ShaderProgramModel,
    ShaderSourceModel,
    decode_shader_program,
    decode_shader_source,
    load_shader_source_closure,
    load_source_closure,
    program_keys,
    program_output_relative_path,
    shader_program_coverage_for,
    shader_source_coverage_for,
    shader_source_keys,
    source_output_relative_path,
    source_resolution_for,
)
from elysium_pipeline.formats.unit_contract import (
    asset_block,
    extension_root,
    identity_block,
    write_glb,
)


def build_source_document(model: ShaderSourceModel) -> tuple[dict[str, Any], bytes]:
    """The readable-source unit: `vtmb:shader-source:<stem>` from one `.psh`."""

    identity = identity_block(
        model.asset_id,
        model.members[0].path,
        shaderModel=model.shader_model,
        compiledTwin=model.compiled_twin,
    )
    root = extension_root(
        schema_version=SCHEMA_VERSION,
        identity=identity,
        source_resolution=source_resolution_for(model),
        dependencies=model.dependencies,
        coverage=shader_source_coverage_for(model),
        source=model.source_block(),
        anomalies=model.anomalies,
        omissions=model.omissions,
    )
    document = {
        "asset": asset_block(SOURCE_GENERATOR_TITLE),
        "extensionsUsed": [SHADER_SOURCE_EXTENSION],
        "extensionsRequired": [SHADER_SOURCE_EXTENSION],
        "extensions": {SHADER_SOURCE_EXTENSION: root},
    }
    return document, b""


def build_document(model: ShaderProgramModel) -> tuple[dict[str, Any], bytes]:
    """The compiled-bundle unit: `vtmb:shader-program:<subdir>/<stem>` from one `.vcs`."""

    identity = identity_block(
        model.asset_id,
        model.members[0].path,
        readableSource=model.readable_source,
    )
    root = extension_root(
        schema_version=SCHEMA_VERSION,
        identity=identity,
        source_resolution=source_resolution_for(model),
        dependencies=model.dependencies,
        coverage=shader_program_coverage_for(model),
        header=model.header,
        comboTable=model.combo_table,
        combos=model.combos,
        sourceComparison=model.source_comparison,
        selectedBy=[],
        anomalies=model.anomalies,
        omissions=model.omissions,
    )
    document = {
        "asset": asset_block(PROGRAM_GENERATOR_TITLE),
        "extensionsUsed": [SHADER_PROGRAM_EXTENSION],
        "extensionsRequired": [SHADER_PROGRAM_EXTENSION],
        "extensions": {SHADER_PROGRAM_EXTENSION: root},
    }
    return document, b""


def export_shader_source(index: dict, key: str, output_root: Path, *, read_bytes=None) -> Path:
    """Write and validate one readable-source unit below `<output_root>/source/`."""

    closure = load_shader_source_closure(index, key, read_bytes=read_bytes)
    model = decode_shader_source(closure)
    document, binary = build_source_document(model)
    from elysium_pipeline.validation import shader_program_glb as validation

    validation.validate_document(document, binary, source_members=closure.members())
    destination = Path(output_root) / Path(*source_output_relative_path(closure.key).parts)
    write_glb(document, binary, destination)
    return destination


def export(index: dict, key: str, output_root: Path, *, read_bytes=None) -> Path:
    """Write and validate one compiled-bundle unit below `<output_root>/<subdir>/`."""

    closure = load_source_closure(index, key, read_bytes=read_bytes)
    model = decode_shader_program(closure)
    document, binary = build_document(model)
    from elysium_pipeline.validation import shader_program_glb as validation

    validation.validate_document(document, binary, source_members=closure.members())
    destination = Path(output_root) / Path(*program_output_relative_path(closure.key).parts)
    write_glb(document, binary, destination)
    return destination


def source_keys(index: dict) -> list[str]:
    """Every `<subdir>/<stem>` the `shader-programs-glb` corpus command exports."""

    return program_keys(index)


def shader_source_source_keys(index: dict) -> list[str]:
    """Every `<stem>` the `shader-sources-glb` corpus command exports."""

    return shader_source_keys(index)
