"""Independent structural validator for Material GLB products."""

from __future__ import annotations

from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
from typing import Any

from elysium_pipeline.formats.material_glb.model import MATERIAL_EXTENSION, SCHEMA_VERSION


class MaterialGlbValidationError(ValueError):
    pass


BYTE_STATES = {
    "mapped", "derived", "omitted-proven", "padding-zero", "reserved-zero",
}

#: The grammar departures the decode is allowed to record. A row naming anything else means the
#: writer invented a tolerance the seam never agreed to.
ANOMALY_ROLES = {
    "unterminated-quoted-string",
    "valueless-key",
    "anonymous-block",
    "unclosed-block-at-end-of-file",
    "content-after-shader-block",
}

FORBIDDEN_CORE = ("images", "textures", "samplers", "scenes", "nodes", "meshes", "animations")


def read_glb(path: Path) -> tuple[dict[str, Any], bytes]:
    data = path.read_bytes()
    if len(data) < 20:
        raise MaterialGlbValidationError(f"{path} is only {len(data)} bytes")
    magic, version, total = struct.unpack_from("<III", data)
    if magic != 0x46546C67 or version != 2 or total != len(data):
        raise MaterialGlbValidationError("invalid GLB header")
    position = 12
    chunks = []
    while position < len(data):
        if position + 8 > len(data):
            raise MaterialGlbValidationError("truncated GLB chunk header")
        size, kind = struct.unpack_from("<II", data, position)
        position += 8
        end = position + size
        if end > len(data):
            raise MaterialGlbValidationError("GLB chunk overruns file")
        chunks.append((kind, data[position:end]))
        position = end
    if not chunks or chunks[0][0] != 0x4E4F534A:
        raise MaterialGlbValidationError("Material GLB must open with a JSON chunk")
    if len(chunks) > 2 or (len(chunks) == 2 and chunks[1][0] != 0x004E4942):
        raise MaterialGlbValidationError("Material GLB carries at most a JSON then a BIN chunk")
    try:
        document = json.loads(chunks[0][1].decode("utf-8").rstrip(" "))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise MaterialGlbValidationError(f"invalid GLB JSON: {error}") from error
    return document, chunks[1][1] if len(chunks) == 2 else b""


def _reject_opaque_source(value: Any, path: str = "$") -> None:
    if isinstance(value, dict):
        for key, item in value.items():
            lowered = str(key).lower().replace("_", "")
            if lowered in {"rawdata", "sourcebytes", "opaquebytes", "base64", "sourcetext"}:
                raise MaterialGlbValidationError(
                    f"opaque source payload is forbidden at {path}.{key}"
                )
            _reject_opaque_source(item, f"{path}.{key}")
    elif isinstance(value, list):
        for index, item in enumerate(value):
            _reject_opaque_source(item, f"{path}[{index}]")


def _check_ledgers(coverage, members, source_members=None) -> tuple[int, int]:
    ledgers = coverage.get("byteLedger")
    if not isinstance(ledgers, list) or len(ledgers) != len(members):
        raise MaterialGlbValidationError("byte ledger count disagrees with source members")
    identities = {str(member.get("path")): member for member in members}
    source_data = (
        {str(member.path): member.data for member in source_members}
        if source_members is not None else {}
    )
    if source_members is not None and set(source_data) != set(identities):
        raise MaterialGlbValidationError(
            "prepublication source members do not match sourceResolution"
        )
    seen = set()
    source_total = accounted_total = 0
    for ledger in ledgers:
        path = str(ledger.get("sourcePath", ""))
        if path in seen or path not in identities:
            raise MaterialGlbValidationError(f"invalid byte ledger source {path!r}")
        seen.add(path)
        identity = identities[path]
        length = int(ledger.get("byteLength", -1))
        digest = str(ledger.get("sourceSha256", ""))
        if length != int(identity.get("byteLength", -2)) or digest != identity.get("sha256"):
            raise MaterialGlbValidationError(f"{path}: ledger identity disagrees")
        raw = source_data.get(path)
        if raw is not None and (len(raw) != length or hashlib.sha256(raw).hexdigest() != digest):
            raise MaterialGlbValidationError(f"{path}: prepublication source bytes disagree")
        cursor = 0
        totals = Counter()
        ranges = ledger.get("ranges")
        if not isinstance(ranges, list):
            raise MaterialGlbValidationError(f"{path}: byte ledger ranges are missing")
        for index, row in enumerate(ranges):
            offset, size = int(row.get("offset", -1)), int(row.get("length", -1))
            state, owner = str(row.get("state", "")), str(row.get("owner", ""))
            if offset != cursor or size <= 0 or state not in BYTE_STATES or not owner:
                raise MaterialGlbValidationError(f"{path}: invalid byte range {index}")
            if offset + size > length:
                raise MaterialGlbValidationError(f"{path}: byte range {index} overruns source")
            if raw is not None and state.endswith("-zero") and any(raw[offset:offset + size]):
                raise MaterialGlbValidationError(f"{path}: false zero range {index}")
            cursor += size
            totals[state] += size
        if cursor != length or int(ledger.get("accountedBytes", -1)) != length:
            raise MaterialGlbValidationError(f"{path}: byte ledger is not gapless")
        if float(ledger.get("coveragePercent", -1)) != 100.0:
            raise MaterialGlbValidationError(f"{path}: byte coverage is not 100%")
        if dict(sorted(totals.items())) != ledger.get("stateBytes"):
            raise MaterialGlbValidationError(f"{path}: state totals disagree")
        canonical = json.dumps(
            {"path": path, "byteLength": length, "ranges": ranges},
            sort_keys=True, separators=(",", ":"),
        ).encode("utf-8")
        if hashlib.sha256(canonical).hexdigest() != ledger.get("rangesSha256"):
            raise MaterialGlbValidationError(f"{path}: range digest disagrees")
        source_total += length
        accounted_total += cursor
    if seen != set(identities):
        raise MaterialGlbValidationError("byte ledgers do not cover every source member")
    return source_total, accounted_total


def _check_parameters(extension: dict[str, Any]) -> None:
    """Every parameter must be indexed in source order and own a distinct source offset."""

    parameters = extension.get("parameters")
    if not isinstance(parameters, list):
        raise MaterialGlbValidationError("the material declares no parameter table")
    offsets = set()
    for index, row in enumerate(parameters):
        if not isinstance(row, dict) or row.get("index") != index:
            raise MaterialGlbValidationError(f"parameter {index} is not in source order")
        key, source_key = row.get("key"), row.get("sourceKey")
        if not isinstance(key, str) or not key:
            raise MaterialGlbValidationError(f"parameter {index} has no key")
        if not isinstance(source_key, str) or source_key.strip().lower() != key:
            raise MaterialGlbValidationError(f"parameter {index} key disagrees with its source")
        offset = row.get("offset")
        if not isinstance(offset, int) or offset < 0 or offset in offsets:
            raise MaterialGlbValidationError(f"parameter {index} has no distinct source offset")
        offsets.add(offset)
    if len(parameters) > 1:
        ordered = [row["offset"] for row in parameters]
        if ordered != sorted(ordered):
            raise MaterialGlbValidationError("the parameter table is not in source order")
    for proxy in extension.get("proxies") or []:
        for member in proxy.get("parameters") or []:
            if not isinstance(member, int) or not 0 <= member < len(parameters):
                raise MaterialGlbValidationError(
                    f"proxy {proxy.get('name')!r} names parameter {member}, which does not exist"
                )


def _check_bindings(extension: dict[str, Any]) -> list[str]:
    """Bindings and dependencies must agree; returns the unresolved texture values."""

    dependencies = extension.get("dependencies") or []
    texture_assets = {
        str(row.get("asset"))
        for row in dependencies
        if isinstance(row, dict) and row.get("role") == "texture"
    }
    missing = []
    bound = set()
    for row in extension.get("textureBindings") or []:
        if not isinstance(row, dict) or not row.get("parameter"):
            raise MaterialGlbValidationError("a texture binding names no parameter")
        kind = row.get("kind")
        if kind not in ("texture", "render-target"):
            raise MaterialGlbValidationError(f"unknown texture binding kind {kind!r}")
        if kind == "render-target":
            if row.get("asset") is not None:
                raise MaterialGlbValidationError("a render target resolves to no texture asset")
            continue
        asset = str(row.get("asset") or "")
        if not asset.startswith("vtmb:texture:"):
            raise MaterialGlbValidationError("a texture binding carries no stable texture id")
        if row.get("resolved"):
            if asset not in texture_assets:
                raise MaterialGlbValidationError(
                    f"resolved binding {asset} has no dependency row"
                )
            bound.add(asset)
        else:
            if asset in texture_assets:
                raise MaterialGlbValidationError(
                    f"unresolved binding {asset} must not claim a dependency"
                )
            missing.append(str(row.get("value") or asset))
    if texture_assets - bound:
        raise MaterialGlbValidationError("a texture dependency has no binding that produced it")
    surface = extension.get("surfaceProperty")
    declared = {
        str(row.get("asset"))
        for row in dependencies
        if isinstance(row, dict) and row.get("role") == "surface-property"
    }
    expected = {"vtmb:surface-property:" + surface} if surface else set()
    if declared != expected:
        raise MaterialGlbValidationError("the surface-property dependency disagrees with the key")
    _check_material_references(extension, dependencies)
    return missing


def _check_material_references(extension: dict[str, Any], dependencies: list[Any]) -> None:
    """A material-shaped value is stated once as a reference and, when it resolves, once as a
    dependency -- the same rule the texture bindings above keep, in the material namespace."""

    parameter_assets = {
        str(row.get("asset"))
        for row in dependencies
        if isinstance(row, dict) and row.get("role") == "material" and row.get("parameter")
    }
    resolved = set()
    for row in extension.get("materialReferences") or []:
        if not isinstance(row, dict) or not row.get("parameter"):
            raise MaterialGlbValidationError("a material reference names no parameter")
        asset = str(row.get("asset") or "")
        if not asset.startswith("vtmb:material:"):
            raise MaterialGlbValidationError("a material reference carries no stable material id")
        if row.get("resolved"):
            if asset not in parameter_assets:
                raise MaterialGlbValidationError(
                    f"resolved material reference {asset} has no dependency row"
                )
            resolved.add(asset)
        elif asset in parameter_assets:
            raise MaterialGlbValidationError(
                f"unresolved material reference {asset} must not claim a dependency"
            )
    if parameter_assets - resolved:
        raise MaterialGlbValidationError(
            "a parameter material dependency has no reference that produced it"
        )


def _check_shader_resolution(extension: dict[str, Any]) -> bool:
    """The resolved programs must agree with the family and be complete when they claim to be."""

    resolution = extension.get("shaderResolution")
    if not isinstance(resolution, dict):
        raise MaterialGlbValidationError("the material declares no shader resolution")
    if resolution.get("family") != extension.get("shader"):
        raise MaterialGlbValidationError("the shader resolution names a different family")
    programs = resolution.get("programs")
    if not isinstance(programs, list):
        raise MaterialGlbValidationError("the shader resolution carries no program list")
    if not resolution.get("resolved"):
        if not resolution.get("reason"):
            raise MaterialGlbValidationError("an unresolved shader gives no reason")
        if programs:
            raise MaterialGlbValidationError("an unresolved shader names a program anyway")
        return False
    if not programs:
        raise MaterialGlbValidationError("a resolved shader names no program")
    if not resolution.get("inputs"):
        raise MaterialGlbValidationError("a resolved shader records no inputs")
    seen = set()
    passes = set()
    for index, program in enumerate(programs):
        if not isinstance(program, dict):
            raise MaterialGlbValidationError(f"program {index} is not a record")
        for field in ("pixelShader", "vertexShader"):
            value = program.get(field)
            if not isinstance(value, str) or not value:
                raise MaterialGlbValidationError(f"program {index} names no {field}")
        condition = program.get("condition")
        if not isinstance(condition, str):
            raise MaterialGlbValidationError(f"program {index} carries no condition")
        draw_pass = program.get("drawPass")
        if not isinstance(draw_pass, int) or draw_pass < 0:
            raise MaterialGlbValidationError(f"program {index} carries no draw pass")
        key = (condition, draw_pass)
        if key in seen:
            raise MaterialGlbValidationError(
                f"program condition {condition!r} is repeated for pass {draw_pass}"
            )
        seen.add(key)
        passes.add(draw_pass)
    # A pass numbering that skips a draw would leave a reader guessing what runs between them.
    if passes != set(range(max(passes) + 1)):
        raise MaterialGlbValidationError("draw passes are not contiguous from zero")
    # Whatever the render config, some program must draw the first pass, so exactly one row must
    # be the unconditional default a reader falls back to.
    if ("", 0) not in seen:
        raise MaterialGlbValidationError("the programs declare no default first pass")
    return True


def validate_document(document: dict, binary: bytes, *, source_members=None) -> dict[str, Any]:
    if document.get("asset", {}).get("version") != "2.0":
        raise MaterialGlbValidationError("asset.version is not 2.0")
    used = set(document.get("extensionsUsed") or [])
    required = set(document.get("extensionsRequired") or [])
    if MATERIAL_EXTENSION not in used or MATERIAL_EXTENSION not in required:
        raise MaterialGlbValidationError("material extension must be used and required")
    if any(document.get(name) for name in FORBIDDEN_CORE):
        raise MaterialGlbValidationError("Material GLB carries no scene, mesh or image core")
    materials = document.get("materials") or []
    if len(materials) != 1:
        raise MaterialGlbValidationError("Material GLB must carry exactly one core material")
    for name in (materials[0].get("extensions") or {}):
        if name not in used:
            raise MaterialGlbValidationError(f"core material uses undeclared extension {name}")
    if document.get("buffers") or document.get("bufferViews") or document.get("accessors"):
        if not binary:
            raise MaterialGlbValidationError("a declared buffer has no BIN chunk")
    elif binary:
        raise MaterialGlbValidationError("a BIN chunk is present that nothing references")
    extension = (document.get("extensions") or {}).get(MATERIAL_EXTENSION)
    if not isinstance(extension, dict) or extension.get("schemaVersion") != SCHEMA_VERSION:
        raise MaterialGlbValidationError("missing or unsupported material extension")
    identity = extension.get("identity") or {}
    asset = str(identity.get("asset", ""))
    if not asset.startswith("vtmb:material:"):
        raise MaterialGlbValidationError("material extension has no stable identity")
    if asset != "vtmb:material:" + str(identity.get("materialPath", "")):
        raise MaterialGlbValidationError("material identity disagrees with its path")
    if not str(extension.get("shader") or ""):
        raise MaterialGlbValidationError("the material declares no shader")
    if str(extension.get("sourceShader") or "").strip().lower() != extension.get("shader"):
        raise MaterialGlbValidationError("the shader disagrees with its source spelling")
    if materials[0].get("name") != identity.get("materialPath"):
        raise MaterialGlbValidationError("the core material is not named for its unit")
    coverage = extension.get("coverage") or {}
    if coverage.get("unresolved") or coverage.get("unsupported"):
        raise MaterialGlbValidationError("material extension is incomplete")
    for row in extension.get("anomalies") or []:
        if not isinstance(row, dict) or row.get("role") not in ANOMALY_ROLES:
            raise MaterialGlbValidationError(f"unknown source anomaly {row!r}")
    members = ((extension.get("sourceResolution") or {}).get("members") or [])
    if len(members) != 1 or members[0].get("role") != "vmt":
        raise MaterialGlbValidationError("a material unit owns exactly one VMT member")
    source_total, accounted_total = _check_ledgers(coverage, members, source_members)
    _reject_opaque_source(extension)
    _check_parameters(extension)
    missing = _check_bindings(extension)
    shader_resolved = _check_shader_resolution(extension)
    patch = extension.get("patch")
    if patch is not None and not str(patch.get("asset", "")).startswith("vtmb:material:"):
        raise MaterialGlbValidationError("a patch edge names no base material")
    patch_of = extension.get("patchOf")
    if patch_of is not None:
        if not str(patch_of.get("asset", "")).startswith("vtmb:material:"):
            raise MaterialGlbValidationError("a patchOf edge names no base material")
        origin = patch_of.get("cubemapOrigin")
        if (
            not isinstance(origin, list)
            or len(origin) != 3
            or not all(isinstance(value, int) for value in origin)
        ):
            raise MaterialGlbValidationError("a patchOf edge carries no integer cubemap origin")
        if not any(
            isinstance(row, dict) and row.get("role") == "material" and row.get("asset") == patch_of.get("asset")
            for row in extension.get("dependencies") or []
        ):
            raise MaterialGlbValidationError("a patchOf edge names no dependency row")
    return {
        "asset": asset,
        "materialPath": identity.get("materialPath"),
        "shader": extension.get("shader"),
        "shaderResolved": shader_resolved,
        "programs": [
            program.get("pixelShader")
            for program in (extension.get("shaderResolution") or {}).get("programs") or []
        ],
        "parameters": len(extension.get("parameters") or []),
        "proxies": len(extension.get("proxies") or []),
        "textureBindings": len(extension.get("textureBindings") or []),
        "dependencies": len(extension.get("dependencies") or []),
        "anomalies": [str(row.get("role")) for row in extension.get("anomalies") or []],
        "missingTextures": missing,
        "sourceBytes": source_total,
        "accountedBytes": accounted_total,
        "byteCoveragePercent": 100.0,
    }


def validate(path: Path) -> dict[str, Any]:
    document, binary = read_glb(path)
    return validate_document(document, binary)


def warnings_for(summary: dict[str, Any]) -> list[str]:
    """What a published unit could not resolve, phrased for the operator.

    Both the in-process and the pooled export path call this, because a warning derived in only
    one of them is a warning the corpus run does not print.
    """

    warnings = []
    missing = summary.get("missingTextures") or []
    if missing:
        warnings.append(
            "the install carries no texture for "
            + ", ".join(sorted(missing)[:4])
            + (f" and {len(missing) - 4} more" if len(missing) > 4 else "")
        )
    if summary.get("shaderResolved") is False:
        warnings.append(
            f"no transcribed selector for shader {summary.get('shader')!r}; "
            "the shipped program this material draws with is not identified"
        )
    anomalies = summary.get("anomalies") or []
    if anomalies:
        counted = ", ".join(
            f"{role}x{count}" for role, count in sorted(Counter(anomalies).items())
        )
        warnings.append(f"the source departs from the KeyValues grammar: {counted}")
    return warnings
