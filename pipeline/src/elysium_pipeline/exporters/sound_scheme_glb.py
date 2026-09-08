"""Isolated one-scheme/one-GLB sound-scheme product writer.

Every unit is scene-less: a sound scheme names filenames, flags and numbers, all of which the
source wrote as text, so the decode itself needs no binary payload and declares no accessor.

The BIN chunk it does carry is the **source capsule** (schema 1.1.0) -- the scheme `.txt`, byte
for byte, as the UP-first policy selected it. That is what lets `uv run elysium import
sound-schemes` write `Content/ElysiumCorpus/sound/schemes/<stem>.txt` out of the published unit
alone, with no install present, and it is the only reason this kind has a buffer at all.
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
    buffer_table,
    dependency,
    encapsulate,
    extension_root,
    identity_block,
    plain,
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
    # The one member's bytes are the whole BIN chunk: there is no decode payload to keep ahead of
    # them, so `encapsulate` starts from an empty buffer and the capsule owns bufferView 0.
    resolution, buffer_views, binary = encapsulate([model.member])
    # The extension root opens with the five contract keys, in order,
    # before any kind-specific one; the seam doc's own illustrative JSON interleaves `parameters`
    # and `scheme` before `dependencies` and `comments`/`anomalies`/`omissions` before `coverage`,
    # which `validate_extension_root` (and every other seam) would refuse. This follows the
    # contract, not the sample (see `specDeviations`).
    extension = extension_root(
        schema_version=SCHEMA_VERSION,
        identity=identity_block(model.asset_id, model.member.path),
        source_resolution=resolution,
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
    }
    if binary:
        document["buffers"] = buffer_table(binary)
        document["bufferViews"] = buffer_views
    document["extensions"] = {SOUND_SCHEME_EXTENSION: plain(extension)}
    # No accessor: the capsule is a file, not typed elements a glTF consumer reads. An empty
    # scheme file (there is none in the install, but the contract admits one) capsules to zero
    # bytes and the unit keeps its no-BIN-chunk shape.
    return document, binary


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
