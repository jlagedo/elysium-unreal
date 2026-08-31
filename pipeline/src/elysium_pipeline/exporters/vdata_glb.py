"""Isolated one-file/one-GLB vdata product writer.

Every unit is scene-less and declares no accessor: a vdata table names strings, flags and numbers,
all of which the source wrote as text (`docs/architecture/seam_map_vdata.md`). The BIN chunk it
does carry is the source capsule alone -- the member's own bytes, so the unit is everything a
reader needs to reproduce the install file (`seam_map_unit_contract.md`, "Source capsule").
"""

from __future__ import annotations

from pathlib import Path
from typing import Callable

from elysium_pipeline.formats.unit_contract import (
    asset_block,
    buffer_table,
    encapsulate,
    extension_root,
    identity_block,
    plain,
    write_glb,
)
from elysium_pipeline.formats.vdata_glb import (
    KIND_TITLE,
    SCHEMA_VERSION,
    VDATA_EXTENSION,
    VdataDecodeError,
    VdataSourceError,
    decode_vdata,
    load_source_closure,
    output_relative_path,
)
from elysium_pipeline.formats.vdata_glb import source_keys as _source_keys  # re-export
from elysium_pipeline.formats.vdata_glb.coverage import coverage_block
from elysium_pipeline.formats.vdata_glb.model import VdataModel

#: Re-exported so the plural export command can enumerate this seam's units from an index without
#: importing `formats.vdata_glb` directly.
source_keys = _source_keys


class VdataGlbError(RuntimeError):
    pass


def build_document(model: VdataModel) -> tuple[dict, bytes]:
    identity = identity_block(
        model.asset,
        model.member.path,
        vdataPath=model.member.path,
        subtree=model.subtree,
        variant=model.variant,
    )
    resolution, buffer_views, binary = encapsulate([model.member])
    root = extension_root(
        schema_version=SCHEMA_VERSION,
        identity=identity,
        source_resolution=resolution,
        dependencies=model.dependencies,
        coverage=coverage_block(
            model=model,
            ledger_row=model.ledger_row,
            typed_unidentified=model.typed_unidentified,
            omitted_proven=model.omissions,
            unresolved=model.unresolved,
            unsupported=model.unsupported,
        ),
        grammar=model.grammar,
        rootKey=model.root_key,
        tree=model.tree,
        rows=model.rows,
        projection=model.projection,
        comments=model.comments,
        anomalies=model.anomalies,
        omissions=model.omissions,
    )
    document: dict = {
        "asset": asset_block(KIND_TITLE),
        "extensionsUsed": [VDATA_EXTENSION],
        "extensionsRequired": [VDATA_EXTENSION],
    }
    # The whole BIN chunk is the capsule: one buffer holding the member's own bytes, addressed by
    # the one view `sourceResolution.members[0].capsule` names. An empty source file capsules to
    # nothing, and that unit carries no BIN chunk at all.
    if binary:
        document["buffers"] = buffer_table(binary)
        document["bufferViews"] = buffer_views
    document["extensions"] = {VDATA_EXTENSION: plain(root)}
    return document, binary


def _model_resolver(index: dict) -> Callable[[str], bool]:
    def resolve(path: str) -> bool:
        return bool(path) and path in index

    return resolve


def _sound_group_resolver(index: dict) -> Callable[[str], list[str]]:
    def resolve(folded: str) -> list[str]:
        if not folded:
            return []
        needle = f"/{folded}/"
        return sorted(
            key for key in index
            if key.startswith("sound/weapons/") and needle in (key + "/")
        )

    return resolve


def export(
    index: dict,
    key: str,
    output_root: Path,
    *,
    read_bytes=None,
) -> Path:
    """Write and validate one vdata unit."""

    try:
        closure = load_source_closure(index, key, read_bytes=read_bytes)
        model = decode_vdata(
            closure,
            resolve_model=_model_resolver(index),
            resolve_sound_group=_sound_group_resolver(index),
            resolve_asset=_model_resolver(index),
        )
    except (VdataSourceError, VdataDecodeError) as error:
        raise VdataGlbError(f"{key}: {error}") from error
    document, binary = build_document(model)
    from elysium_pipeline.validation import vdata_glb as validation

    validation.validate_document(document, binary, source_members=closure.members())
    destination = output_root / Path(*output_relative_path(closure.key).parts)
    write_glb(document, binary, destination)
    return destination
