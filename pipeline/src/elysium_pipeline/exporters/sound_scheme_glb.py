"""Isolated one-scheme/one-GLB sound-scheme product writer.

Every unit is scene-less and carries no BIN chunk: a sound scheme names filenames, flags and
numbers, all of which the source wrote as text (`docs/architecture/seam_map_sound_scheme.md`).
"""

from __future__ import annotations

from pathlib import Path

from elysium_pipeline.formats.sound_scheme_glb import (
    KIND_TITLE,
    SCHEMA_VERSION,
    SOUND_SCHEME_EXTENSION,
    decode_sound_scheme,
    load_dsp_preset_ids,
    load_source_closure,
    output_relative_path,
    sound_resolved,
)
from elysium_pipeline.formats.sound_scheme_glb import source_keys as _source_keys  # re-export
from elysium_pipeline.formats.sound_scheme_glb.coverage import coverage_block
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
#: importing `formats.sound_scheme_glb` directly.
source_keys = _source_keys


def build_document(model) -> tuple[dict, bytes]:
    parameters = [
        {
            "index": parameter.index,
            "block": parameter.block,
            "key": parameter.key,
            "sourceKey": parameter.source_key,
            "value": parameter.value,
            "quotedKey": parameter.quoted_key,
            "quotedValue": parameter.quoted_value,
            "offset": parameter.offset,
        }
        for parameter in model.parameters
    ]
    scheme = {
        "params": model.params,
        "music": model.music,
        "combat": model.combat,
        "alert": model.alert,
        "ambient": model.ambient,
        "randomSounds": model.random_sounds,
    }
    dependencies = [
        dependency(row["role"], row["asset"], row["sourcePath"], row["resolved"])
        for row in model.dependencies
    ]
    # `seam_map_unit_contract.md`'s extension root opens with the five contract keys, in order,
    # before any kind-specific one; the seam doc's own illustrative JSON interleaves `parameters`
    # and `scheme` before `dependencies` and `comments`/`anomalies`/`omissions` before `coverage`,
    # which `validate_extension_root` (and every other seam) would refuse. This follows the
    # contract, not the sample (see `specDeviations`).
    extension = extension_root(
        schema_version=SCHEMA_VERSION,
        identity=identity_block(model.asset_id, model.member.path),
        source_resolution=source_resolution([model.member]),
        dependencies=dependencies,
        coverage=coverage_block(
            ledger_row=model.ledger_row, unresolved=model.unresolved, unsupported=model.unsupported
        ),
        parameters=parameters,
        scheme=scheme,
        comments=model.comments,
        anomalies=model.anomalies,
        omissions=model.omissions,
    )
    document = {
        "asset": asset_block(KIND_TITLE),
        "extensionsUsed": [SOUND_SCHEME_EXTENSION],
        "extensionsRequired": [SOUND_SCHEME_EXTENSION],
        "extensions": {SOUND_SCHEME_EXTENSION: plain(extension)},
    }
    # A sound scheme carries no binary payload: every datum it owns is a name, a flag, or a number
    # the source wrote as text. One JSON chunk, no BIN chunk.
    return document, b""


def export(
    index: dict,
    key: str,
    output_root: Path,
    *,
    read_bytes=None,
    dsp_preset_ids=None,
) -> Path:
    """Write and validate one sound-scheme unit.

    `dsp_preset_ids` lets a corpus run scan `scripts/dsp_presets.txt` once; on its own the call
    scans it itself, so one unit still costs one command.
    """

    if dsp_preset_ids is None:
        dsp_preset_ids = load_dsp_preset_ids(index, read_bytes=read_bytes)
    closure = load_source_closure(index, key, read_bytes=read_bytes)
    model = decode_sound_scheme(
        closure,
        sound_exists=lambda path: sound_resolved(index, path),
        dsp_preset_exists=lambda preset_id: preset_id in dsp_preset_ids,
    )
    document, binary = build_document(model)
    from elysium_pipeline.validation import sound_scheme_glb as validation

    validation.validate_document(document, binary, source_members=closure.members())
    destination = output_root / Path(*output_relative_path(closure.stem).parts)
    write_glb(document, binary, destination)
    return destination
