"""Isolated one-entry/one-GLB surface-property product writer."""

from __future__ import annotations

import json
import os
from pathlib import Path
import struct

from elysium_pipeline.formats.surface_property_glb import (
    SCHEMA_VERSION,
    SURFACE_PROPERTY_EXTENSION,
    decode_surface_property,
    load_sound_script_names,
    load_table,
    output_relative_path,
)
from elysium_pipeline.formats.surface_property_glb.model import plain


class SurfacePropertyGlbError(RuntimeError):
    pass


def build_document(model) -> tuple[dict, bytes]:
    sources = [
        {
            "role": source.role,
            "path": source.path,
            "origin": source.origin,
            "byteLength": source.byte_length,
            "sha256": source.sha256,
            "tableOffset": source.table_offset,
        }
        for source in model.sources
    ]
    parameters = [
        {
            "index": parameter.index,
            "key": parameter.key,
            "sourceKey": parameter.source_key,
            "value": parameter.value,
            "quotedKey": parameter.quoted_key,
            "quotedValue": parameter.quoted_value,
            "offset": parameter.offset,
        }
        for parameter in model.parameters
    ]
    extension = {
        "schemaVersion": SCHEMA_VERSION,
        "identity": {
            "asset": model.asset_id,
            "name": model.name,
            "sourceName": model.source_name,
        },
        "sourceResolution": {"policy": "up-first", "members": sources},
        "base": model.base,
        "physics": model.physics,
        "movement": model.movement,
        "footsteps": model.footsteps,
        "impacts": model.impacts,
        "sounds": model.sounds,
        "gameMaterial": model.game_material,
        "parameters": parameters,
        "dependencies": model.dependencies,
        "comments": model.comments,
        "anomalies": model.anomalies,
        "omissions": model.omissions,
        "coverage": {
            "mapped": [
                "identity", "sourceResolution", "base", "physics", "movement", "footsteps",
                "impacts", "sounds", "gameMaterial", "parameters", "dependencies", "comments",
                "anomalies", "omissions",
            ],
            "byteLedger": model.byte_coverage,
            "unresolved": model.unresolved,
            "unsupported": model.unsupported,
        },
    }
    document = {
        "asset": {"version": "2.0", "generator": "Elysium Surface-property GLB Exporter"},
        "extensionsUsed": [SURFACE_PROPERTY_EXTENSION],
        "extensionsRequired": [SURFACE_PROPERTY_EXTENSION],
        "extensions": {SURFACE_PROPERTY_EXTENSION: plain(extension)},
    }
    # A surface property carries no binary payload: every datum it owns is a name, a flag, or a
    # number the source wrote as text. Packing those into an accessor would restate `parameters`
    # in a second, weaker form, so the unit publishes one JSON chunk and no BIN chunk.
    return document, b""


def write_glb(document: dict, binary: bytes, destination: Path) -> None:
    json_data = json.dumps(
        plain(document), separators=(",", ":"), ensure_ascii=False, allow_nan=False
    ).encode("utf-8")
    json_data += b" " * (-len(json_data) % 4)
    total = 12 + 8 + len(json_data)
    padded_binary = b""
    if binary:
        padded_binary = binary + b"\0" * (-len(binary) % 4)
        total += 8 + len(padded_binary)
    output = bytearray(struct.pack("<III", 0x46546C67, 2, total))
    output.extend(struct.pack("<II", len(json_data), 0x4E4F534A))
    output.extend(json_data)
    if binary:
        output.extend(struct.pack("<II", len(padded_binary), 0x004E4942))
        output.extend(padded_binary)
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix(destination.suffix + ".tmp")
    temporary.write_bytes(output)
    os.replace(temporary, destination)


def export(
    index: dict,
    name: str,
    output_root: Path,
    *,
    read_bytes=None,
    table=None,
    sound_scripts=None,
) -> Path:
    """Write and validate one surface-property unit.

    `table` and `sound_scripts` let a corpus run parse each shared source once; on their own the
    call reads both tables itself, so one unit costs one command.
    """

    if table is None:
        table = load_table(index, read_bytes=read_bytes)
    if sound_scripts is None:
        sound_scripts = load_sound_script_names(index, read_bytes=read_bytes)
    closure = table.closure(name)
    model = decode_surface_property(
        closure,
        base_exists=lambda candidate: candidate in table.spans,
        sound_script_exists=lambda candidate: candidate.lower() in sound_scripts,
        sound_exists=lambda candidate: candidate in index,
    )
    document, binary = build_document(model)
    from elysium_pipeline.validation import surface_property_glb as validation

    validation.validate_document(document, binary, source_members=closure.members())
    destination = output_root / Path(*output_relative_path(closure.name).parts)
    write_glb(document, binary, destination)
    return destination
