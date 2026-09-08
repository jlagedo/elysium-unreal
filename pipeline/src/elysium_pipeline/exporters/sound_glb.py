"""Isolated one-member/one-GLB sound product writer.

The unit is scene-less: glTF has no audio object, so the payload is a buffer view reached through
`ELYSIUM_vtmb_sound` and no core object is declared. A `.wav` publishes interleaved 16-bit PCM; an
`.mp3` publishes its own MPEG frame stream, and `frames[i]` locates each frame in both the source
and the payload so a payload byte and a ledger range name the same frame.

The payload is a *decode*, not the file: a `.wav` unit's BIN chunk is not its RIFF member and a
tagged `.mp3`'s is not its member either. So the BIN chunk also carries the **source capsules**
(schema 1.1.0) -- the audio member and, when the install ships one, the `.lip` companion, verbatim
-- appended after the payload, which keeps `bufferView` 0. That is what lets `uv run elysium
import sound` deploy a byte-exact `.wav`/`.mp3`/`.lip`.
"""

from __future__ import annotations

import hashlib
from pathlib import Path
from typing import Any

from elysium_pipeline.formats.sound_glb import coverage as sound_coverage
from elysium_pipeline.formats.sound_glb import (
    SCHEMA_VERSION,
    SOUND_EXTENSION,
    decode_sound,
    load_source_closure,
    output_relative_path,
    source_keys,
)
from elysium_pipeline.formats.unit_contract import container
from elysium_pipeline.formats.unit_contract.capsule import buffer_table, encapsulate
from elysium_pipeline.formats.unit_contract.coverage import extension_root, identity_block

__all__ = ["build_document", "export", "source_keys"]

#: glTF component types the payload accessor uses: int16 samples, or raw frame bytes.
COMPONENT_SHORT = 5122
COMPONENT_UNSIGNED_BYTE = 5121


def _chunk_rows(model) -> list[dict[str, Any]]:
    return [
        {
            "index": chunk.index,
            "id": chunk.id,
            "sourceOffset": chunk.offset,
            "length": chunk.length,
            "padded": chunk.padded,
            "beyondEnvelope": chunk.beyond_envelope,
            "body": chunk.body,
        }
        for chunk in model.chunks
    ]


def _frame_rows(model) -> list[dict[str, Any]]:
    return [
        {
            "index": frame.index,
            "sourceOffset": frame.offset,
            "length": frame.length,
            "payloadOffset": frame.payload_offset,
            "version": frame.version,
            "layer": frame.layer,
            "bitrate": frame.bitrate,
            "sampleRate": frame.sample_rate,
            "padding": frame.padding,
            "private": frame.private,
            "mode": frame.mode,
            "modeExtension": frame.mode_extension,
            "copyright": frame.copyright,
            "original": frame.original,
            "emphasis": frame.emphasis,
            "crcPresent": frame.crc_present,
            "crcValid": frame.crc_valid,
            "mainDataBegin": frame.main_data_begin,
        }
        for frame in model.frames
    ]


def _lip_block(model) -> dict[str, Any] | None:
    document = model.lip
    if document is None:
        return None
    caption = document.close_caption
    return {
        "version": document.version,
        "plaintext": document.plaintext,
        "words": [
            {
                "text": word.text,
                "start": word.start,
                "end": word.end,
                "sourceOffset": word.source_offset,
                "malformed": word.malformed,
                "phonemes": [
                    {
                        "code": phoneme.code,
                        "text": phoneme.text,
                        "start": phoneme.start,
                        "end": phoneme.end,
                        "volume": phoneme.volume,
                        "flag": phoneme.flag,
                        "sourceOffset": phoneme.source_offset,
                    }
                    for phoneme in word.phonemes
                ],
            }
            for word in document.words
        ],
        "emphasis": list(document.emphasis),
        "closeCaption": None if caption is None else {
            "language": caption["language"],
            "phrases": [
                {
                    "kind": phrase.kind,
                    "count": phrase.count,
                    "text": phrase.text,
                    "start": phrase.start,
                    "end": phrase.end,
                    "sourceOffset": phrase.source_offset,
                }
                for phrase in caption["phrases"]
            ],
        },
        "options": document.options,
    }


def build_document(model) -> tuple[dict, bytes]:
    """The unit's JSON document and its BIN payload."""

    payload = model.payload
    # The BIN chunk is the decoded payload first, then the source capsules `encapsulate` appends
    # after it. The payload view keeps index 0, so the accessor below is untouched by adoption.
    payload_views = (
        [{"buffer": 0, "byteOffset": 0, "byteLength": len(payload)}] if payload else []
    )
    resolution, buffer_views, binary = encapsulate(
        model.members, binary=payload, buffer_views=payload_views
    )
    identity = identity_block(
        model.asset_id,
        [member.path for member in model.members],
        key=model.key,
        # The corpus index owns the inverse join: which surfaces, scripts and dialogue lines name
        # this member is a property of those units, not of this one.
        referencedBy=[],
    )
    extension = extension_root(
        schema_version=SCHEMA_VERSION,
        identity=identity,
        source_resolution=resolution,
        dependencies=model.dependencies,
        coverage=sound_coverage.build(model),
        payload={
            "accessor": 0 if payload else None,
            "sampleFormat": model.sample_format,
            "byteLength": len(payload),
            "sha256": hashlib.sha256(payload).hexdigest(),
        },
        codec=model.codec,
        chunks=_chunk_rows(model),
        frames=_frame_rows(model),
        tags=model.tags,
        lip=_lip_block(model),
        resolution=model.resolution,
        anomalies=model.anomalies,
        omissions=model.omissions,
    )
    document: dict[str, Any] = {
        "asset": container.asset_block("Sound"),
        "extensionsUsed": [SOUND_EXTENSION],
        "extensionsRequired": [SOUND_EXTENSION],
    }
    if payload:
        short = model.sample_format == "int16-interleaved"
        component = COMPONENT_SHORT if short else COMPONENT_UNSIGNED_BYTE
        document["accessors"] = [{
            "bufferView": 0,
            "byteOffset": 0,
            "componentType": component,
            "count": len(payload) // (2 if short else 1),
            "type": "SCALAR",
        }]
    if binary:
        document["buffers"] = buffer_table(binary)
        document["bufferViews"] = buffer_views
    document["extensions"] = {SOUND_EXTENSION: container.plain(extension)}
    return document, binary


def export(
    index: dict,
    key: str,
    output_root: Path,
    *,
    read_bytes=None,
) -> Path:
    """Decode, validate against the members that were read, then write one sound unit."""

    closure = load_source_closure(index, key, read_bytes=read_bytes)
    model = decode_sound(closure)
    document, binary = build_document(model)
    from elysium_pipeline.validation import sound_glb as validation

    # Export-time validation re-reads the members rather than re-using the bytes the decode held,
    # so a member that changed under the export -- or a read that was corrupted once -- fails the
    # re-hash instead of being blessed by the copy that produced the document.
    reread = load_source_closure(index, key, read_bytes=read_bytes)
    validation.validate_document(document, binary, source_members=reread.members())
    destination = Path(output_root) / Path(*output_relative_path(closure.key).parts)
    container.write_glb(document, binary, destination)
    return destination
