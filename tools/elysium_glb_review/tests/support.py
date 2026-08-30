"""Synthetic fixtures.

The tests build their own corpus rather than reading the exported one: a unit test that
needs 4 GB of assets on a particular drive is not a unit test. Fixtures are minimal but
structurally faithful -- same chunk layout, same identity strings, same extension keys --
so a change that would break against the real corpus breaks here first.
"""

from __future__ import annotations

import json
import struct
from pathlib import Path

from core import ktx2, seams


def build_glb(document: dict, binary: bytes = b"") -> bytes:
    """A binary glTF with the given JSON and optional BIN chunk."""
    json_chunk = json.dumps(document).encode("utf-8")
    json_chunk += b" " * (-len(json_chunk) % 4)
    chunks = struct.pack("<II", len(json_chunk), 0x4E4F534A) + json_chunk
    if binary:
        padded = binary + b"\x00" * (-len(binary) % 4)
        chunks += struct.pack("<II", len(padded), 0x004E4942) + padded
    return b"glTF" + struct.pack("<II", 2, 12 + len(chunks)) + chunks


def build_ktx2(
    *,
    vk_format: int = ktx2.VK_BC1_RGBA_UNORM,
    width: int = 8,
    height: int = 8,
    levels: int = 1,
    layers: int = 0,
    faces: int = 1,
    supercompression: int = 0,
) -> tuple[bytes, list[bytes]]:
    """A KTX2 file plus the image payloads it contains, in index order.

    Level data is written smallest-level-first, the way a conformant writer emits it,
    precisely so a reader that assumes ascending offsets fails this fixture.
    """
    block = ktx2.FORMATS[vk_format]
    images_per_level = max(1, layers) * max(1, faces)

    sizes = []
    for level in range(max(1, levels)):
        level_width = max(1, width >> level)
        level_height = max(1, height >> level)
        if block.compressed:
            size = max(1, (level_width + 3) // 4) * max(1, (level_height + 3) // 4) * block.unit_bytes
        else:
            size = level_width * level_height * block.unit_bytes
        sizes.append(size)

    index_size = max(1, levels) * 24
    payload_start = ktx2.LEVEL_INDEX_OFFSET + index_size

    # Smallest level first in the file, so offsets descend as the index ascends.
    offsets: dict[int, int] = {}
    cursor = payload_start
    for level in reversed(range(max(1, levels))):
        offsets[level] = cursor
        cursor += sizes[level] * images_per_level

    header = bytearray(payload_start)
    header[0:12] = ktx2.IDENTIFIER
    struct.pack_into(
        "<9I", header, 12,
        vk_format, 1, width, height, 0, layers, faces, levels, supercompression,
    )
    for level in range(max(1, levels)):
        struct.pack_into(
            "<QQQ",
            header,
            ktx2.LEVEL_INDEX_OFFSET + level * 24,
            offsets[level],
            sizes[level] * images_per_level,
            sizes[level] * images_per_level,
        )

    data = bytearray(header)
    images: list[bytes] = []
    per_level: dict[int, list[bytes]] = {}
    for level in reversed(range(max(1, levels))):
        blobs = []
        for image in range(images_per_level):
            blob = bytes([(level * 16 + image) & 0xFF]) * sizes[level]
            blobs.append(blob)
            data.extend(blob)
        per_level[level] = blobs
    for level in range(max(1, levels)):
        images.extend(per_level[level])

    return bytes(data), images


def texture_unit(**kwargs) -> bytes:
    """A texture GLB carrying a synthetic KTX2 payload."""
    payload, _images = build_ktx2(**kwargs)
    document = {
        "asset": {"version": "2.0", "generator": "Elysium Texture GLB Exporter"},
        "extensionsUsed": [seams.TEXTURE_EXTENSION],
        "extensionsRequired": [seams.TEXTURE_EXTENSION],
        "buffers": [{"byteLength": len(payload)}],
        "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": len(payload)}],
        "extensions": {
            seams.TEXTURE_EXTENSION: {
                "schemaVersion": "1.1.0",
                "identity": {"asset": "vtmb:texture:test/unit", "texturePath": "test/unit"},
                "payload": {"bufferView": 0, "mimeType": "image/ktx2"},
                "coverage": {},
            }
        },
    }
    return build_glb(document, payload)


def material_unit(
    identity: str,
    *,
    shader: str = "vertexlitgeneric",
    textures: dict[str, str] | None = None,
    surface_property: str | None = None,
    resolved: bool = True,
    anomalies: list | None = None,
) -> bytes:
    bindings = [
        {"parameter": parameter, "value": target, "kind": "texture",
         "asset": "vtmb:texture:%s" % target, "resolved": True}
        for parameter, target in (textures or {}).items()
    ]
    dependencies = [
        {"role": "texture", "asset": binding["asset"], "sourcePath": ""}
        for binding in bindings
    ]
    if surface_property:
        dependencies.append(
            {"role": "surface-property",
             "asset": "vtmb:surface-property:%s" % surface_property,
             "sourcePath": ""}
        )
    document = {
        "asset": {"version": "2.0", "generator": "Elysium Material GLB Exporter"},
        "extensionsUsed": [seams.MATERIAL_EXTENSION],
        "extensionsRequired": [seams.MATERIAL_EXTENSION],
        "materials": [{"name": identity.split(":")[-1]}],
        "extensions": {
            seams.MATERIAL_EXTENSION: {
                "schemaVersion": "1.1.0",
                "identity": {"asset": identity},
                "shader": shader,
                "shaderResolution": {"resolved": resolved, "reason": None if resolved else "selector-not-transcribed"},
                "parameters": [],
                "textureBindings": bindings,
                "surfaceProperty": surface_property,
                "dependencies": dependencies,
                "anomalies": anomalies or [],
                "omissions": [],
                "coverage": {},
            }
        },
    }
    return build_glb(document)


def surface_property_unit(
    name: str, *, base: str | None = None, physics: dict | None = None,
    game_material: str | None = None,
) -> bytes:
    payload = {
        "schemaVersion": "1.0.0",
        "identity": {"asset": "vtmb:surface-property:%s" % name, "name": name},
        "base": None if base is None else {
            "name": base, "asset": "vtmb:surface-property:%s" % base, "resolved": True,
        },
        "physics": physics or {},
        "movement": {},
        "footsteps": {},
        "impacts": {},
        "sounds": {},
        "gameMaterial": game_material,
        "parameters": [],
        "dependencies": [] if base is None else [
            {"role": "surface-property", "asset": "vtmb:surface-property:%s" % base, "sourcePath": ""}
        ],
        "anomalies": [],
        "omissions": [],
        "coverage": {},
    }
    document = {
        "asset": {"version": "2.0", "generator": "Elysium Surface-property GLB Exporter"},
        "extensionsUsed": [seams.SURFACE_PROPERTY_EXTENSION],
        "extensionsRequired": [seams.SURFACE_PROPERTY_EXTENSION],
        "extensions": {seams.SURFACE_PROPERTY_EXTENSION: payload},
    }
    return build_glb(document)


def model_unit(
    identity: str,
    *,
    materials: list[str] | None = None,
    includes: list[str] | None = None,
    animations: int = 0,
    bones: list[str] | None = None,
    bone_flags: dict[str, int] | None = None,
    split_rotation: list[dict] | None = None,
) -> bytes:
    """A model GLB. `split_rotation` is written verbatim as `mdl.splitRotationBones`.

    A unit that declares the key is a 1.2.0 unit; one that leaves the rule to
    `mdl.bones[i].flags` is the 1.1.0 corpus, and both are read.
    """
    mdl_bones = [
        {"name": name, "index": index, "flags": (bone_flags or {}).get(name, 0)}
        for index, name in enumerate(bones or [])
    ]
    mdl = {"bones": mdl_bones, "includeModels": [], "header": {}}
    if split_rotation is not None:
        mdl["splitRotationBones"] = split_rotation
    document = {
        "asset": {"version": "2.0", "generator": "Elysium Model GLB Exporter"},
        "extensionsUsed": [seams.MODEL_EXTENSION, seams.MATERIAL_REFERENCE_EXTENSION],
        "extensionsRequired": [seams.MODEL_EXTENSION, seams.MATERIAL_REFERENCE_EXTENSION],
        "materials": [
            {
                "name": target.split("/")[-1],
                "extensions": {seams.MATERIAL_REFERENCE_EXTENSION: {"asset": target}},
            }
            for target in (materials or [])
        ],
        "animations": [{"name": "%d:clip" % i} for i in range(animations)],
        "extensions": {
            seams.MODEL_EXTENSION: {
                "schemaVersion": "1.1.0" if split_rotation is None else "1.2.0",
                "identity": {"asset": identity},
                "mdl": mdl,
                "dependencies": [
                    {"role": "model", "asset": included, "sourcePath": ""}
                    for included in (includes or [])
                ],
                "coverage": {},
            }
        },
    }
    return build_glb(document)


def write_corpus(root: Path, units: dict[str, bytes]) -> Path:
    """Write `corpus-relative path -> bytes` into a scratch corpus root."""
    for relative, payload in units.items():
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(payload)
    return root


def document_of(payload: bytes) -> dict:
    """Parse the JSON chunk out of fixture bytes, without touching the filesystem."""
    length = struct.unpack_from("<I", payload, 12)[0]
    return json.loads(payload[20 : 20 + length])
