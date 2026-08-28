"""Independent structural validator for Texture GLB and embedded KTX2 products."""

from __future__ import annotations

from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import struct
from typing import Any

from elysium_pipeline.formats.texture_glb.model import SCHEMA_VERSION, TEXTURE_EXTENSION


class TextureGlbValidationError(ValueError):
    pass


KTX_IDENTIFIER = b"\xabKTX 20\xbb\r\n\x1a\n"
FORMATS = {
    23: (1, 1, 3),
    37: (1, 1, 4),
    41: (1, 1, 4),
    44: (1, 1, 4),
    133: (4, 4, 8),
    135: (4, 4, 16),
    137: (4, 4, 16),
}
DFD_SHAPES = {
    23: (1, 3),
    37: (1, 4),
    41: (1, 4),
    44: (1, 4),
    133: (128, 1),
    135: (129, 2),
    137: (130, 2),
}
BYTE_STATES = {
    "mapped", "derived", "omitted-proven", "padding-zero", "reserved-zero",
}


def read_glb(path: Path) -> tuple[dict[str, Any], bytes]:
    data = path.read_bytes()
    if len(data) < 28:
        raise TextureGlbValidationError(f"{path} is only {len(data)} bytes")
    magic, version, total = struct.unpack_from("<III", data)
    if magic != 0x46546C67 or version != 2 or total != len(data):
        raise TextureGlbValidationError("invalid GLB header")
    position = 12
    chunks = []
    while position < len(data):
        if position + 8 > len(data):
            raise TextureGlbValidationError("truncated GLB chunk header")
        size, kind = struct.unpack_from("<II", data, position)
        position += 8
        end = position + size
        if end > len(data):
            raise TextureGlbValidationError("GLB chunk overruns file")
        chunks.append((kind, data[position:end]))
        position = end
    if len(chunks) != 2 or chunks[0][0] != 0x4E4F534A or chunks[1][0] != 0x004E4942:
        raise TextureGlbValidationError("Texture GLB must carry JSON then BIN chunks")
    try:
        document = json.loads(chunks[0][1].decode("utf-8").rstrip(" "))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise TextureGlbValidationError(f"invalid GLB JSON: {error}") from error
    return document, chunks[1][1]


def _reject_opaque_source(value: Any, path: str = "$") -> None:
    if isinstance(value, dict):
        for key, item in value.items():
            lowered = str(key).lower().replace("_", "")
            if lowered in {"rawdata", "sourcebytes", "opaquebytes", "base64"}:
                raise TextureGlbValidationError(f"opaque source payload is forbidden at {path}.{key}")
            _reject_opaque_source(item, f"{path}.{key}")
    elif isinstance(value, list):
        for index, item in enumerate(value):
            _reject_opaque_source(item, f"{path}[{index}]")


def _check_ledgers(coverage, members, source_members=None) -> tuple[int, int]:
    ledgers = coverage.get("byteLedger")
    if not isinstance(ledgers, list) or len(ledgers) != len(members):
        raise TextureGlbValidationError("byte ledger count disagrees with source members")
    identities = {str(member.get("path")): member for member in members}
    source_data = (
        {str(member.path): member.data for member in source_members}
        if source_members is not None else {}
    )
    if source_members is not None and set(source_data) != set(identities):
        raise TextureGlbValidationError("prepublication source members do not match sourceResolution")
    seen = set()
    source_total = accounted_total = 0
    for ledger in ledgers:
        path = str(ledger.get("sourcePath", ""))
        if path in seen or path not in identities:
            raise TextureGlbValidationError(f"invalid byte ledger source {path!r}")
        seen.add(path)
        identity = identities[path]
        length = int(ledger.get("byteLength", -1))
        digest = str(ledger.get("sourceSha256", ""))
        if length != int(identity.get("byteLength", -2)) or digest != identity.get("sha256"):
            raise TextureGlbValidationError(f"{path}: ledger identity disagrees")
        raw = source_data.get(path)
        if raw is not None and (len(raw) != length or hashlib.sha256(raw).hexdigest() != digest):
            raise TextureGlbValidationError(f"{path}: prepublication source bytes disagree")
        cursor = 0
        totals = Counter()
        ranges = ledger.get("ranges")
        if not isinstance(ranges, list):
            raise TextureGlbValidationError(f"{path}: byte ledger ranges are missing")
        for index, row in enumerate(ranges):
            offset, size = int(row.get("offset", -1)), int(row.get("length", -1))
            state, owner = str(row.get("state", "")), str(row.get("owner", ""))
            if offset != cursor or size <= 0 or state not in BYTE_STATES or not owner:
                raise TextureGlbValidationError(f"{path}: invalid byte range {index}")
            if offset + size > length:
                raise TextureGlbValidationError(f"{path}: byte range {index} overruns source")
            if raw is not None and state.endswith("-zero") and any(raw[offset:offset + size]):
                raise TextureGlbValidationError(f"{path}: false zero range {index}")
            cursor += size
            totals[state] += size
        if cursor != length or int(ledger.get("accountedBytes", -1)) != length:
            raise TextureGlbValidationError(f"{path}: byte ledger is not gapless")
        if float(ledger.get("coveragePercent", -1)) != 100.0:
            raise TextureGlbValidationError(f"{path}: byte coverage is not 100%")
        if dict(sorted(totals.items())) != ledger.get("stateBytes"):
            raise TextureGlbValidationError(f"{path}: state totals disagree")
        canonical = json.dumps(
            {"path": path, "byteLength": length, "ranges": ranges},
            sort_keys=True, separators=(",", ":"),
        ).encode("utf-8")
        if hashlib.sha256(canonical).hexdigest() != ledger.get("rangesSha256"):
            raise TextureGlbValidationError(f"{path}: range digest disagrees")
        source_total += length
        accounted_total += cursor
    if seen != set(identities):
        raise TextureGlbValidationError("byte ledgers do not cover every source member")
    return source_total, accounted_total


def _parse_ktx2(payload: bytes) -> dict[str, Any]:
    if len(payload) < 104 or payload[:12] != KTX_IDENTIFIER:
        raise TextureGlbValidationError("payload is not KTX 2.0")
    values = struct.unpack_from("<9I4I2Q", payload, 12)
    (vk_format, type_size, width, height, depth, layers, faces, level_count,
     supercompression, dfd_offset, dfd_length, kvd_offset, kvd_length,
     sgd_offset, sgd_length) = values
    if vk_format not in FORMATS or type_size != 1 or not width or not height or depth != 0:
        raise TextureGlbValidationError("unsupported or malformed KTX2 header")
    if faces not in (1, 6) or level_count < 1 or supercompression != 0:
        raise TextureGlbValidationError("invalid KTX2 texture type or compression")
    if kvd_offset or kvd_length or sgd_offset or sgd_length:
        raise TextureGlbValidationError("KTX2 carries forbidden auxiliary data")
    index_end = 80 + 24 * level_count
    if dfd_offset != index_end or dfd_length < 28 or dfd_offset + dfd_length > len(payload):
        raise TextureGlbValidationError("invalid KTX2 DFD range")
    if struct.unpack_from("<I", payload, dfd_offset)[0] != dfd_length:
        raise TextureGlbValidationError("KTX2 DFD length disagrees")
    vendor_type, version_size, model_color, block_shape, bytes_planes = struct.unpack_from(
        "<5I", payload, dfd_offset + 4
    )
    block_size = version_size >> 16
    model, samples = DFD_SHAPES[vk_format]
    if (
        vendor_type != 0
        or (version_size & 0xFFFF) != 2
        or block_size != dfd_length - 4
        or block_size != 24 + samples * 16
        or (model_color & 0xFF) != model
        or (block_shape & 0xFFFF) != ((FORMATS[vk_format][0] - 1) | ((FORMATS[vk_format][1] - 1) << 8))
        or (bytes_planes & 0xFF) != FORMATS[vk_format][2]
    ):
        raise TextureGlbValidationError("KTX2 DFD does not describe vkFormat")
    levels_out = []
    block_w, block_h, block_bytes = FORMATS[vk_format]
    alignment = math.lcm(block_bytes, 4)
    image_count = max(1, layers) * faces
    for level in range(level_count):
        offset, size, uncompressed = struct.unpack_from("<3Q", payload, 80 + level * 24)
        level_w, level_h = max(1, width >> level), max(1, height >> level)
        per_image = max(1, (level_w + block_w - 1) // block_w) * max(
            1, (level_h + block_h - 1) // block_h
        ) * block_bytes
        expected = per_image * image_count
        if (
            size != expected
            or uncompressed != expected
            or offset < dfd_offset + dfd_length
            or offset % alignment
            or offset + size > len(payload)
        ):
            raise TextureGlbValidationError(f"KTX2 level {level} has invalid extent")
        levels_out.append(payload[offset:offset + size])
    return {
        "vkFormat": vk_format,
        "width": width,
        "height": height,
        "layers": layers,
        "faces": faces,
        "levelCount": level_count,
        "levels": levels_out,
    }


def _check_decoded_pixels(extension: dict[str, Any], ktx: dict[str, Any], source_members) -> None:
    from elysium_pipeline.formats.texture_glb.decode import decode_texture
    from elysium_pipeline.formats.texture_glb.source import TextureSourceClosure

    tth = next((member for member in source_members if getattr(member, "role", None) == "tth"), None)
    if tth is None:
        raise TextureGlbValidationError("prepublication source members omit TTH")
    ttz = next((member for member in source_members if getattr(member, "role", None) == "ttz"), None)
    identity = extension.get("identity") or {}
    decoded = decode_texture(
        TextureSourceClosure(
            str(identity.get("texturePath") or ""),
            str(identity.get("asset") or ""),
            tth,
            ttz,
        )
    )
    if len(decoded.levels) != len(ktx["levels"]):
        raise TextureGlbValidationError("decoded mip count disagrees with KTX2")
    if decoded.format.vk_format != ktx["vkFormat"]:
        raise TextureGlbValidationError("decoded vkFormat disagrees with KTX2")
    for level, payload in zip(decoded.levels, ktx["levels"]):
        if b"".join(level.images) != payload:
            raise TextureGlbValidationError(
                f"decoded pixels disagree with KTX2 level {level.ktx_level}"
            )


def validate_document(document: dict, binary: bytes, *, source_members=None) -> dict[str, Any]:
    if document.get("asset", {}).get("version") != "2.0":
        raise TextureGlbValidationError("asset.version is not 2.0")
    used, required = set(document.get("extensionsUsed") or []), set(document.get("extensionsRequired") or [])
    if TEXTURE_EXTENSION not in used or TEXTURE_EXTENSION not in required:
        raise TextureGlbValidationError("texture extension must be used and required")
    if any(document.get(name) for name in ("images", "textures", "samplers", "scenes", "nodes", "meshes")):
        raise TextureGlbValidationError("Texture GLB must not carry fallback core images")
    extension = (document.get("extensions") or {}).get(TEXTURE_EXTENSION)
    if not isinstance(extension, dict) or extension.get("schemaVersion") != SCHEMA_VERSION:
        raise TextureGlbValidationError("missing or unsupported texture extension")
    identity = extension.get("identity") or {}
    if not str(identity.get("asset", "")).startswith("vtmb:texture:"):
        raise TextureGlbValidationError("texture extension has no stable identity")
    coverage = extension.get("coverage") or {}
    if coverage.get("unresolved") or coverage.get("unsupported"):
        raise TextureGlbValidationError("texture extension is incomplete")
    members = ((extension.get("sourceResolution") or {}).get("members") or [])
    if not members:
        raise TextureGlbValidationError("texture extension carries no source members")
    source_total, accounted_total = _check_ledgers(coverage, members, source_members)
    _reject_opaque_source(extension)
    buffers, views = document.get("buffers") or [], document.get("bufferViews") or []
    if len(buffers) != 1 or len(views) != 1 or views[0].get("buffer") != 0:
        raise TextureGlbValidationError("Texture GLB must carry one buffer and bufferView")
    declared = int(buffers[0].get("byteLength", -1))
    start, length = int(views[0].get("byteOffset", 0)), int(views[0].get("byteLength", -1))
    if declared != length or start != 0 or length < 0 or length > len(binary):
        raise TextureGlbValidationError("KTX2 bufferView does not match BIN chunk")
    payload_meta = extension.get("payload") or {}
    if payload_meta.get("bufferView") != 0 or payload_meta.get("mimeType") != "image/ktx2":
        raise TextureGlbValidationError("invalid KTX2 payload reference")
    payload = binary[:length]
    if int(payload_meta.get("byteLength", -1)) != length or hashlib.sha256(payload).hexdigest() != payload_meta.get("sha256"):
        raise TextureGlbValidationError("KTX2 payload identity disagrees")
    ktx = _parse_ktx2(payload)
    dimensions = extension.get("dimensions") or {}
    if (ktx["width"], ktx["height"], ktx["faces"], ktx["levelCount"]) != (
        dimensions.get("width"), dimensions.get("height"), dimensions.get("faces"), dimensions.get("mipCount")
    ):
        raise TextureGlbValidationError("KTX2 dimensions disagree with extension")
    source_format = (extension.get("sourceFormat") or {}).get("sourceFormatEnum")
    expected_vk = {
        0: 37,
        3: 23,
        12: 44,
        13: 133,
        14: 135,
        15: 137,
        23: 41,
    }.get(source_format)
    if expected_vk is not None and ktx["vkFormat"] != expected_vk:
        raise TextureGlbValidationError("KTX2 vkFormat disagrees with source format")
    mip_rows = extension.get("mips") or []
    if len(mip_rows) != ktx["levelCount"]:
        raise TextureGlbValidationError("KTX2 mip ledger count disagrees")
    for index, level in enumerate(ktx["levels"]):
        row = mip_rows[index]
        if row.get("ktxLevel") != index or row.get("byteLength") != len(level) or hashlib.sha256(level).hexdigest() != row.get("sha256"):
            raise TextureGlbValidationError(f"KTX2 level {index} digest disagrees")
    if source_members is not None:
        _check_decoded_pixels(extension, ktx, source_members)
    return {
        "asset": identity["asset"],
        "width": ktx["width"],
        "height": ktx["height"],
        "mips": ktx["levelCount"],
        "faces": ktx["faces"],
        "sourceBytes": source_total,
        "accountedBytes": accounted_total,
        "byteCoveragePercent": 100.0,
    }


def validate(path: Path) -> dict[str, Any]:
    document, binary = read_glb(path)
    return validate_document(document, binary)
