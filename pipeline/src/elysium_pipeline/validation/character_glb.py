"""Independent structural reader for Character GLB Exporter products."""

from __future__ import annotations

from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
from typing import Any

from elysium_pipeline.formats.character_glb.model import (
    CHARACTER_EXTENSION,
    MATERIAL_EXTENSION,
    SCHEMA_VERSION,
)


class CharacterGlbValidationError(ValueError):
    """A written Character GLB violates glTF or the Elysium extension contract."""


_COMPONENT_BYTES = {5120: 1, 5121: 1, 5122: 2, 5123: 2, 5125: 4, 5126: 4}
_TYPE_WIDTH = {
    "SCALAR": 1,
    "VEC2": 2,
    "VEC3": 3,
    "VEC4": 4,
    "MAT2": 4,
    "MAT3": 9,
    "MAT4": 16,
}
_BYTE_STATES = {
    "mapped",
    "mapped-string",
    "mapped-text",
    "derived",
    "omitted-proven",
    "padding-zero",
    "reserved-zero",
}


def read_glb(path: Path) -> tuple[dict[str, Any], bytes]:
    data = path.read_bytes()
    if len(data) < 20:
        raise CharacterGlbValidationError(f"{path} is only {len(data)} bytes")
    magic, version, total = struct.unpack_from("<III", data)
    if magic != 0x46546C67 or version != 2 or total != len(data):
        raise CharacterGlbValidationError(
            f"invalid GLB header magic=0x{magic:08x} version={version} "
            f"length={total}/{len(data)}"
        )
    position = 12
    chunks = []
    while position < len(data):
        if position + 8 > len(data):
            raise CharacterGlbValidationError("truncated GLB chunk header")
        length, kind = struct.unpack_from("<II", data, position)
        position += 8
        end = position + length
        if end > len(data):
            raise CharacterGlbValidationError("GLB chunk overruns the file")
        chunks.append((kind, data[position:end]))
        position = end
    if len(chunks) != 2 or chunks[0][0] != 0x4E4F534A or chunks[1][0] != 0x004E4942:
        raise CharacterGlbValidationError("Character GLB must carry JSON then BIN chunks")
    try:
        document = json.loads(chunks[0][1].decode("utf-8").rstrip(" "))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise CharacterGlbValidationError(f"invalid GLB JSON: {error}") from error
    return document, chunks[1][1]


def _check_accessors(document: dict[str, Any], binary: bytes) -> None:
    buffers = document.get("buffers") or []
    if len(buffers) != 1 or buffers[0].get("byteLength", -1) > len(binary):
        raise CharacterGlbValidationError("GLB buffer declaration does not match BIN chunk")
    views = document.get("bufferViews") or []
    for index, view in enumerate(views):
        if view.get("buffer") != 0:
            raise CharacterGlbValidationError(f"bufferView {index} does not use buffer 0")
        start = int(view.get("byteOffset", 0))
        length = int(view.get("byteLength", -1))
        if start < 0 or length < 0 or start + length > len(binary):
            raise CharacterGlbValidationError(f"bufferView {index} runs outside BIN chunk")
    for index, accessor in enumerate(document.get("accessors") or []):
        view_index = accessor.get("bufferView")
        if not isinstance(view_index, int) or not 0 <= view_index < len(views):
            raise CharacterGlbValidationError(f"accessor {index} has invalid bufferView")
        component = accessor.get("componentType")
        shape = accessor.get("type")
        if component not in _COMPONENT_BYTES or shape not in _TYPE_WIDTH:
            raise CharacterGlbValidationError(
                f"accessor {index} has component/type {component}/{shape}"
            )
        count = accessor.get("count")
        if not isinstance(count, int) or count < 0:
            raise CharacterGlbValidationError(f"accessor {index} has invalid count")
        required = count * _COMPONENT_BYTES[component] * _TYPE_WIDTH[shape]
        if required > views[view_index]["byteLength"]:
            raise CharacterGlbValidationError(
                f"accessor {index} needs {required} bytes; view carries "
                f"{views[view_index]['byteLength']}"
            )


def _reject_opaque_source(value: Any, path: str = "$") -> None:
    if isinstance(value, dict):
        for key, item in value.items():
            lowered = str(key).lower().replace("_", "")
            if lowered in {"rawdata", "sourcebytes", "opaquebytes", "base64"}:
                raise CharacterGlbValidationError(
                    f"opaque source payload is forbidden at {path}.{key}"
                )
            _reject_opaque_source(item, f"{path}.{key}")
    elif isinstance(value, list):
        for index, item in enumerate(value):
            _reject_opaque_source(item, f"{path}[{index}]")


def _check_byte_ledgers(
    coverage: dict[str, Any],
    members: list[dict[str, Any]],
    source_members=None,
) -> None:
    ledgers = coverage.get("byteLedger")
    if not isinstance(ledgers, list) or len(ledgers) != len(members):
        raise CharacterGlbValidationError(
            f"byte ledger count {len(ledgers) if isinstance(ledgers, list) else 0} "
            f"does not match {len(members)} source members"
        )
    identities = {str(member.get("path")): member for member in members}
    source_data = (
        {str(member.path): member.data for member in source_members}
        if source_members is not None
        else {}
    )
    if source_members is not None and set(source_data) != set(identities):
        raise CharacterGlbValidationError(
            "prepublication source members do not match sourceResolution"
        )

    seen = set()
    for ledger in ledgers:
        if not isinstance(ledger, dict):
            raise CharacterGlbValidationError("byte ledger row is not an object")
        path = str(ledger.get("sourcePath", ""))
        if path in seen or path not in identities:
            raise CharacterGlbValidationError(f"byte ledger has invalid source {path!r}")
        seen.add(path)
        identity = identities[path]
        length = int(ledger.get("byteLength", -1))
        digest = str(ledger.get("sourceSha256", ""))
        if length != int(identity.get("byteLength", -2)):
            raise CharacterGlbValidationError(f"{path}: byte ledger length disagrees")
        if digest != str(identity.get("sha256", "")):
            raise CharacterGlbValidationError(f"{path}: byte ledger SHA-256 disagrees")
        if int(ledger.get("accountedBytes", -1)) != length:
            raise CharacterGlbValidationError(f"{path}: byte ledger is not 100% accounted")
        if float(ledger.get("coveragePercent", -1.0)) != 100.0:
            raise CharacterGlbValidationError(f"{path}: byte coverage is not 100%")

        ranges = ledger.get("ranges")
        if not isinstance(ranges, list) or (length and not ranges):
            raise CharacterGlbValidationError(f"{path}: byte ledger has no ranges")
        cursor = 0
        state_bytes = Counter()
        raw = source_data.get(path)
        if raw is not None:
            if len(raw) != length or hashlib.sha256(raw).hexdigest() != digest:
                raise CharacterGlbValidationError(
                    f"{path}: prepublication source bytes disagree with the ledger"
                )
        for index, row in enumerate(ranges):
            if not isinstance(row, dict):
                raise CharacterGlbValidationError(f"{path}: range {index} is not an object")
            offset = int(row.get("offset", -1))
            size = int(row.get("length", -1))
            state = str(row.get("state", ""))
            owner = str(row.get("owner", ""))
            if offset != cursor or size <= 0 or state not in _BYTE_STATES or not owner:
                raise CharacterGlbValidationError(
                    f"{path}: invalid range {index} at {offset}+{size} state={state!r}"
                )
            if offset + size > length:
                raise CharacterGlbValidationError(f"{path}: range {index} overruns source")
            if raw is not None and state.endswith("-zero") and any(raw[offset:offset + size]):
                raise CharacterGlbValidationError(
                    f"{path}: {state} range {index} contains non-zero bytes"
                )
            state_bytes[state] += size
            cursor += size
        if cursor != length:
            raise CharacterGlbValidationError(
                f"{path}: byte ledger ends at {cursor}/{length}"
            )
        declared_states = {
            str(key): int(value) for key, value in (ledger.get("stateBytes") or {}).items()
        }
        if declared_states != dict(sorted(state_bytes.items())):
            raise CharacterGlbValidationError(f"{path}: byte ledger state totals disagree")
        canonical = json.dumps(
            {"path": path, "byteLength": length, "ranges": ranges},
            sort_keys=True,
            separators=(",", ":"),
        ).encode("utf-8")
        if hashlib.sha256(canonical).hexdigest() != ledger.get("rangesSha256"):
            raise CharacterGlbValidationError(f"{path}: byte ledger digest disagrees")
    if seen != set(identities):
        raise CharacterGlbValidationError("byte ledgers do not cover every source member")


def validate_document(
    document: dict[str, Any],
    binary: bytes,
    *,
    source_members=None,
) -> dict[str, Any]:
    if document.get("asset", {}).get("version") != "2.0":
        raise CharacterGlbValidationError("asset.version is not 2.0")
    used = set(document.get("extensionsUsed") or [])
    required = set(document.get("extensionsRequired") or [])
    expected = {CHARACTER_EXTENSION, MATERIAL_EXTENSION}
    if not expected <= used or not expected <= required:
        raise CharacterGlbValidationError(
            "Character and material-reference extensions must be used and required"
        )
    extension = (document.get("extensions") or {}).get(CHARACTER_EXTENSION)
    if not isinstance(extension, dict):
        raise CharacterGlbValidationError("missing root character extension")
    if extension.get("schemaVersion") != SCHEMA_VERSION:
        raise CharacterGlbValidationError(
            f"character extension schema {extension.get('schemaVersion')!r}, "
            f"expected {SCHEMA_VERSION}"
        )
    identity = extension.get("identity") or {}
    if not str(identity.get("asset", "")).startswith("vtmb:character-body:"):
        raise CharacterGlbValidationError("character extension has no stable body identity")
    coverage = extension.get("coverage") or {}
    if coverage.get("unresolved") or coverage.get("unsupported"):
        raise CharacterGlbValidationError("character extension is not complete")
    members = ((extension.get("sourceResolution") or {}).get("members") or [])
    if not members:
        raise CharacterGlbValidationError("character extension carries no source members")
    for member in members:
        digest = str(member.get("sha256", ""))
        if len(digest) != 64 or any(character not in "0123456789abcdef" for character in digest):
            raise CharacterGlbValidationError(
                f"source member {member.get('path')} has invalid SHA-256"
            )
        if int(member.get("byte_length", member.get("byteLength", 0))) <= 0:
            raise CharacterGlbValidationError(
                f"source member {member.get('path')} has no byte length"
            )
    _check_byte_ledgers(coverage, members, source_members)
    _reject_opaque_source(extension)
    _check_accessors(document, binary)
    nodes = document.get("nodes") or []
    meshes = document.get("meshes") or []
    skins = document.get("skins") or []
    if not nodes or not meshes or len(skins) != 1:
        raise CharacterGlbValidationError("character core needs nodes, meshes, and one skin")
    for joint in skins[0].get("joints") or []:
        if not isinstance(joint, int) or not 0 <= joint < len(nodes):
            raise CharacterGlbValidationError(f"skin joint {joint} is invalid")
    accessors = document.get("accessors") or []
    for mesh_index, mesh in enumerate(meshes):
        for primitive_index, primitive in enumerate(mesh.get("primitives") or []):
            attributes = primitive.get("attributes") or {}
            expected_attributes = {"POSITION", "NORMAL", "TEXCOORD_0", "JOINTS_0", "WEIGHTS_0"}
            if not expected_attributes <= set(attributes):
                raise CharacterGlbValidationError(
                    f"mesh {mesh_index} primitive {primitive_index} lacks core attributes"
                )
            attribute_accessors = [attributes[name] for name in expected_attributes]
            if any(
                not isinstance(index, int) or not 0 <= index < len(accessors)
                for index in attribute_accessors
            ):
                raise CharacterGlbValidationError(
                    f"mesh {mesh_index} primitive {primitive_index} has an invalid "
                    "attribute accessor"
                )
            counts = [accessors[index]["count"] for index in attribute_accessors]
            if len(set(counts)) != 1:
                raise CharacterGlbValidationError(
                    f"mesh {mesh_index} primitive {primitive_index} attribute counts differ"
                )
            indices = primitive.get("indices")
            if not isinstance(indices, int) or not 0 <= indices < len(accessors):
                raise CharacterGlbValidationError(
                    f"mesh {mesh_index} primitive {primitive_index} has invalid indices"
                )
            index_accessor = accessors[indices]
            if index_accessor["count"] % 3:
                raise CharacterGlbValidationError(
                    f"mesh {mesh_index} primitive {primitive_index} is not triangles"
                )
    return {
        "asset": identity["asset"],
        "sources": len(members),
        "sourceBytes": sum(int(member["byteLength"]) for member in members),
        "accountedBytes": sum(
            int(ledger["accountedBytes"]) for ledger in coverage["byteLedger"]
        ),
        "byteCoveragePercent": 100.0,
        "bones": len((extension.get("mdl") or {}).get("bones") or []),
        "lods": len((extension.get("vtx") or {}).get("lods") or []),
        "meshes": len(meshes),
        "animations": len(document.get("animations") or []),
    }


def validate(path: Path) -> dict[str, Any]:
    document, binary = read_glb(path)
    return validate_document(document, binary)
