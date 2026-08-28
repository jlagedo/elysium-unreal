"""Isolated one-texture/one-KTX2 GLB product writer."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import struct

from elysium_pipeline.formats.texture_glb import (
    SCHEMA_VERSION,
    TEXTURE_EXTENSION,
    decode_texture,
    load_source_closure,
    output_relative_path,
)
from elysium_pipeline.formats.texture_glb import ktx2
from elysium_pipeline.formats.texture_glb.model import plain


class TextureGlbError(RuntimeError):
    pass


def build_document(model) -> tuple[dict, bytes]:
    payload, payload_meta = ktx2.build(model)
    payload_meta["bufferView"] = 0
    sources = [
        {
            "role": source.role,
            "path": source.path,
            "origin": source.origin,
            "byteLength": source.byte_length,
            "sha256": source.sha256,
        }
        for source in model.sources
    ]
    mip_rows = []
    for level in model.levels:
        content = b"".join(level.images)
        mip_rows.append({
            "ktxLevel": level.ktx_level,
            "sourceMip": level.source_mip,
            "width": level.width,
            "height": level.height,
            "images": len(level.images),
            "sha256": hashlib.sha256(content).hexdigest(),
            "byteLength": len(content),
        })
    extension = {
        "schemaVersion": SCHEMA_VERSION,
        "identity": {"asset": model.asset_id, "texturePath": model.texture_path},
        "sourceResolution": {"policy": "up-first", "members": sources},
        "payload": payload_meta,
        "dimensions": {
            "width": model.width,
            "height": model.height,
            "declaredWidth": model.declared_width,
            "declaredHeight": model.declared_height,
            "frames": model.frames,
            "faces": 6 if model.cubemap else 1,
            "mipCount": model.mip_count,
        },
        "sourceFormat": model.header,
        "sampling": model.sampling,
        "mips": mip_rows,
        "faces": model.faces,
        "omissions": model.omissions,
        "coverage": {
            "mapped": [
                "identity", "sourceResolution", "payload", "dimensions", "sourceFormat",
                "sampling", "mips", "faces", "omissions",
            ],
            "byteLedger": model.byte_coverage,
            "unresolved": [],
            "unsupported": [],
        },
    }
    document = {
        "asset": {"version": "2.0", "generator": "Elysium Texture GLB Exporter"},
        "extensionsUsed": [TEXTURE_EXTENSION],
        "extensionsRequired": [TEXTURE_EXTENSION],
        "extensions": {TEXTURE_EXTENSION: plain(extension)},
        "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": len(payload)}],
        "buffers": [{"byteLength": len(payload)}],
    }
    return document, payload


def write_glb(document: dict, binary: bytes, destination: Path) -> None:
    json_data = json.dumps(
        plain(document), separators=(",", ":"), ensure_ascii=False, allow_nan=False
    ).encode("utf-8")
    json_data += b" " * (-len(json_data) % 4)
    padded_binary = binary + b"\0" * (-len(binary) % 4)
    total = 12 + 8 + len(json_data) + 8 + len(padded_binary)
    output = bytearray(struct.pack("<III", 0x46546C67, 2, total))
    output.extend(struct.pack("<II", len(json_data), 0x4E4F534A))
    output.extend(json_data)
    output.extend(struct.pack("<II", len(padded_binary), 0x004E4942))
    output.extend(padded_binary)
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix(destination.suffix + ".tmp")
    temporary.write_bytes(output)
    os.replace(temporary, destination)


def export(index: dict, texture_path: str, output_root: Path, *, read_bytes=None) -> Path:
    closure = load_source_closure(index, texture_path, read_bytes=read_bytes)
    model = decode_texture(closure)
    document, binary = build_document(model)
    from elysium_pipeline.validation import texture_glb as validation

    validation.validate_document(document, binary, source_members=closure.members())
    destination = output_root / Path(*output_relative_path(closure.texture_path).parts)
    write_glb(document, binary, destination)
    return destination
