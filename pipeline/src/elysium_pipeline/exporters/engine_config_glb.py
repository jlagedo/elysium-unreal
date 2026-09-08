"""Isolated one-member/one-GLB engine-config product writer.

Every unit is scene-less and carries no BIN chunk: every engine-config member the seam names is
text or a small typed binary blob, none of it a general glTF-drawable payload.
"""

from __future__ import annotations

from pathlib import Path

from elysium_pipeline.formats.engine_config_glb import (
    ENGINE_CONFIG_EXTENSION,
    KIND_TITLE,
    SCHEMA_VERSION,
    decode_engine_config,
    load_source_closure,
    member_resolved,
    output_relative_path,
)
from elysium_pipeline.formats.engine_config_glb import source_keys as _source_keys  # re-export
from elysium_pipeline.formats.engine_config_glb.coverage import coverage_block
from elysium_pipeline.formats.unit_contract import (
    asset_block,
    dependency,
    extension_root,
    identity_block,
    plain,
    source_resolution,
    write_glb,
)

#: Re-exported so the plural export command can enumerate this seam's units from an index without
#: importing `formats.engine_config_glb` directly.
source_keys = _source_keys


def build_document(model) -> tuple[dict, bytes]:
    dependencies = [
        dependency(row["role"], row["asset"], row["sourcePath"], row["resolved"])
        for row in model.dependencies
    ]
    identity_extra = {"residue": True} if model.key in _RESIDUE_KEYS() else {}
    # The extension root opens with the five contract keys, in order,
    # before any kind-specific one; the seam doc's own illustrative JSON interleaves `grammar`
    # and the per-grammar table keys before `dependencies` and `comments`/`anomalies`/`omissions`
    # before `coverage`, which `validate_extension_root` (and every other seam) would refuse.
    # This follows the contract, not the sample (see `specDeviations`).
    extension = extension_root(
        schema_version=SCHEMA_VERSION,
        identity=identity_block(model.asset, model.member.path, **identity_extra),
        source_resolution=source_resolution([model.member]),
        dependencies=dependencies,
        coverage=coverage_block(
            ledger_row=model.ledger_row,
            grammar=model.grammar,
            typed_unidentified=model.typed_unidentified,
            omitted_proven=model.omissions,
            unresolved=model.unresolved,
            unsupported=model.unsupported,
        ),
        grammar=model.grammar,
        commands=model.commands,
        bindings=model.bindings,
        aliases=model.aliases,
        cvars=model.cvars,
        scriptExpressions=model.script_expressions,
        textureLights=model.texture_lights,
        detailTypes=model.detail_types,
        maps=model.maps,
        packer=model.packer,
        packerKeys=model.packer_keys,
        categories=model.categories,
        binary=model.binary,
        saveFragment=model.save_fragment,
        comments=model.comments,
        anomalies=model.anomalies,
        omissions=model.omissions,
    )
    document = {
        "asset": asset_block(KIND_TITLE),
        "extensionsUsed": [ENGINE_CONFIG_EXTENSION],
        "extensionsRequired": [ENGINE_CONFIG_EXTENSION],
        "extensions": {ENGINE_CONFIG_EXTENSION: plain(extension)},
    }
    # No member this seam names holds a general glTF-drawable payload: one JSON chunk, no BIN.
    return document, b""


def _RESIDUE_KEYS() -> frozenset[str]:
    from elysium_pipeline.formats.engine_config_glb import RESIDUE_KEYS

    return RESIDUE_KEYS


def export(
    index: dict,
    key: str,
    output_root: Path,
    *,
    read_bytes=None,
) -> Path:
    """Write and validate one engine-config unit."""

    closure = load_source_closure(index, key, read_bytes=read_bytes)
    resolver = lambda path: member_resolved(index, path)  # noqa: E731 - shared decode/validate resolver
    model = decode_engine_config(closure, resolver=resolver)
    document, binary = build_document(model)
    from elysium_pipeline.validation import engine_config_glb as validation

    validation.validate_document(
        document, binary, source_members=closure.members(), resolver=resolver
    )
    destination = output_root / Path(*output_relative_path(closure.key).parts)
    write_glb(document, binary, destination)
    return destination
