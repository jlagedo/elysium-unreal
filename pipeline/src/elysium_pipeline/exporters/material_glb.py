"""Isolated one-VMT/one-GLB material product writer."""

from __future__ import annotations

import json
import os
from pathlib import Path
import struct

from elysium_pipeline.formats.material_glb import (
    MATERIAL_EXTENSION,
    SCHEMA_VERSION,
    decode_material,
    load_source_closure,
    normalize_material_path,
    output_relative_path,
)
from elysium_pipeline.formats.material_glb.model import plain

KHR_UNLIT = "KHR_materials_unlit"

#: Shaders whose pixel program applies no scene lighting, so the core approximation is unlit.
UNLIT_SHADERS = frozenset({
    "unlitgeneric", "unlitgeneric_dx6", "unlittwotexture", "sprite", "cable", "wireframe",
    "modulate", "decalmodulate", "screenfeedback", "redvision", "heatglow", "skyfog", "cloud",
})


class MaterialGlbError(RuntimeError):
    pass


def _numbers(value: str) -> list[float]:
    parts = value.strip().strip("{}[]").split()
    try:
        return [float(part) for part in parts]
    except ValueError:
        return []


def _scalar(parameters: dict[str, str], key: str, default: float) -> float:
    numbers = _numbers(parameters.get(key, ""))
    return numbers[0] if len(numbers) == 1 else default


def _flag(parameters: dict[str, str], key: str) -> bool:
    """A Source boolean parameter: present and non-zero."""

    raw = parameters.get(key)
    if raw is None:
        return False
    text = raw.strip().strip('"')
    if not text:
        return True                                   # a valueless key is the flag itself
    numbers = _numbers(text)
    return bool(numbers[0]) if numbers else True


def _colour(parameters: dict[str, str], key: str) -> list[float]:
    """A Source colour triple. Braces conventionally mean 0-255; any component above 1 says so."""

    numbers = _numbers(parameters.get(key, ""))
    if len(numbers) < 3:
        return [1.0, 1.0, 1.0]
    triple = numbers[:3]
    if any(component > 1.0 for component in triple):
        triple = [component / 255.0 for component in triple]
    return [max(0.0, min(1.0, component)) for component in triple]


def core_material(model) -> tuple[dict, list[str]]:
    """The core glTF approximation of the material, plus the Khronos extensions it uses.

    This is an approximation by construction: VtMB's shaders are fixed-function ps.1.1 programs
    with no metallic-roughness parameterization. Authoritative values stay in the VtMB extension;
    this exists so a plain glTF reader sees something rather than an empty material.
    """

    parameters = {
        parameter.key: parameter.value
        for parameter in model.parameters
        if not parameter.block
    }
    base = _colour(parameters, "$color")
    alpha = _scalar(parameters, "$alpha", 1.0)
    material: dict = {
        "name": model.material_path,
        "pbrMetallicRoughness": {
            "baseColorFactor": [base[0], base[1], base[2], max(0.0, min(1.0, alpha))],
            "metallicFactor": 0.0,
            "roughnessFactor": 1.0,
        },
    }
    if _flag(parameters, "$alphatest"):
        material["alphaMode"] = "MASK"
        material["alphaCutoff"] = _scalar(parameters, "$alphatestreference", 0.5)
    elif _flag(parameters, "$translucent") or _flag(parameters, "$additive") or alpha < 1.0:
        material["alphaMode"] = "BLEND"
    else:
        material["alphaMode"] = "OPAQUE"
    if _flag(parameters, "$nocull"):
        material["doubleSided"] = True
    if _flag(parameters, "$selfillum"):
        material["emissiveFactor"] = [1.0, 1.0, 1.0]
    used: list[str] = []
    if model.shader in UNLIT_SHADERS:
        material.setdefault("extensions", {})[KHR_UNLIT] = {}
        used.append(KHR_UNLIT)
    return material, used


def build_document(model) -> tuple[dict, bytes]:
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
    parameters = [
        {
            "index": parameter.index,
            "block": parameter.block,
            "key": parameter.key,
            "sourceKey": parameter.source_key,
            "value": parameter.value,
            "valueType": parameter.value_type,
            "quotedKey": parameter.quoted_key,
            "quotedValue": parameter.quoted_value,
            "offset": parameter.offset,
        }
        for parameter in model.parameters
    ]
    proxies = [
        {
            "index": proxy.index,
            "name": proxy.name,
            "sourceName": proxy.source_name,
            "parameters": [parameter.index for parameter in proxy.parameters],
        }
        for proxy in model.proxies
    ]
    extension = {
        "schemaVersion": SCHEMA_VERSION,
        "identity": {"asset": model.asset_id, "materialPath": model.material_path},
        "sourceResolution": {"policy": "up-first", "members": sources},
        "shader": model.shader,
        "sourceShader": model.source_shader,
        "shaderResolution": model.shader_resolution,
        "parameters": parameters,
        "blocks": model.blocks,
        "proxies": proxies,
        "textureBindings": model.texture_bindings,
        "patch": model.patch,
        "patchOf": model.patch_of,
        "surfaceProperty": model.surface_property,
        "environment": model.environment,
        "dependencies": model.dependencies,
        "comments": model.comments,
        "anomalies": model.anomalies,
        "omissions": model.omissions,
        "coverage": {
            "mapped": [
                "identity", "sourceResolution", "shader", "parameters", "blocks", "proxies",
                "shaderResolution", "textureBindings", "dependencies", "comments", "anomalies",
                "omissions", "patchOf",
            ],
            "byteLedger": model.byte_coverage,
            "unresolved": model.unresolved,
            "unsupported": model.unsupported,
        },
    }
    material, khronos = core_material(model)
    document = {
        "asset": {"version": "2.0", "generator": "Elysium Material GLB Exporter"},
        "extensionsUsed": [MATERIAL_EXTENSION, *khronos],
        "extensionsRequired": [MATERIAL_EXTENSION],
        "extensions": {MATERIAL_EXTENSION: plain(extension)},
        "materials": [material],
    }
    # A material carries no binary payload: every datum it owns is a name, a flag, or a value the
    # source wrote as text. Packing those into an accessor would restate `parameters` in a second,
    # weaker form, so the unit publishes one JSON chunk and no BIN chunk.
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


def _map_probe_stems(index: dict, map_name: str, read_bytes) -> frozenset[str]:
    """Every `.tth` stem one map's PAKFILE carries, as `$envmap` values name them.

    A patched material's own `$envmap` override names its baked probe the same way the texture
    seam's `source_keys` does -- `maps/<map>/<stem>`, no `materials/` prefix -- so this is that
    same PAKFILE enumeration, filtered to one map and to stems alone.
    """

    from elysium_pipeline.formats.map_glb.pakfile_index import pakfile_members

    # The same stem rule `texture_glb.source_keys` applies to every PAKFILE member: lower-case
    # the whole name and drop the `materials/` prefix, keeping every subdirectory below it (a
    # patched material's own directory, e.g. `plaster/`, is part of its stem). `rsplit('/', 1)`
    # flattened those subdirectories away and skipped the lower-casing, so it could produce a stem
    # `$envmap` never actually binds to.
    prefix, suffix = "materials/", ".tth"
    members = pakfile_members(index, read_bytes=read_bytes).get(map_name, ())
    stems = set()
    for member in members:
        name = member.name.replace("\\", "/").lower()
        if not name.endswith(suffix):
            continue
        stem = name[len(prefix):] if name.startswith(prefix) else name
        stems.add(stem[:-len(suffix)])
    return frozenset(stems)


def export(
    index: dict,
    material_path: str,
    output_root: Path,
    *,
    read_bytes=None,
    texture_exists=None,
) -> Path:
    closure = load_source_closure(index, material_path, read_bytes=read_bytes)
    if texture_exists is None:
        normalized = normalize_material_path(material_path)
        if normalized.startswith("maps/"):
            map_name = normalized[len("maps/"):].split("/", 1)[0]
            probe_stems = _map_probe_stems(index, map_name, read_bytes)

            def texture_exists(path: str) -> bool:
                return f"materials/{path}.tth" in index or path in probe_stems
        else:
            def texture_exists(path: str) -> bool:
                return f"materials/{path}.tth" in index

    model = decode_material(closure, texture_exists=texture_exists)
    document, binary = build_document(model)
    from elysium_pipeline.validation import material_glb as validation

    validation.validate_document(document, binary, source_members=closure.members())
    destination = output_root / Path(*output_relative_path(closure.material_path).parts)
    write_glb(document, binary, destination)
    return destination
