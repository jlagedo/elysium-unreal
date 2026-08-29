"""Independent structural validator for Surface-property GLB products."""

from __future__ import annotations

from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
from typing import Any

from elysium_pipeline.formats.surface_property_glb.model import (
    SCHEMA_VERSION,
    SURFACE_PROPERTY_EXTENSION,
)


class SurfacePropertyGlbValidationError(ValueError):
    pass


BYTE_STATES = {
    "mapped", "derived", "omitted-proven", "padding-zero", "reserved-zero",
}

#: The grammar and convention departures the decode is allowed to record. A row naming anything
#: else means the writer invented a tolerance the seam never agreed to.
ANOMALY_ROLES = {
    "unterminated-quoted-string",
    "valueless-key",
    "unclosed-block-at-end-of-file",
    "repeated-scalar-key",
    "base-is-not-the-first-key",
}

#: The vocabulary is restated here rather than imported, so a decode that widens its own tables
#: still has to answer to the shape the seam publishes.
PHYSICS_FIELDS = {"density", "elasticity", "friction", "thickness"}
MOVEMENT_FIELDS = {"maxSpeedFactor", "jumpFactor", "climbable"}
FOOTSTEP_FIELDS = {"left", "right"}
WEAPON_CLASSES = {"bullet", "metal", "wood", "blade", "fist"}
DAMAGE_OUTCOMES = {"soak", "norm", "crit"}
SOUND_SCRIPT_FIELDS = {"impact", "scrape"}
DEPENDENCY_ROLES = {"surface-property", "sound", "sound-script"}

FORBIDDEN_CORE = (
    "images", "textures", "samplers", "scenes", "nodes", "meshes", "animations", "materials",
)


def read_glb(path: Path) -> tuple[dict[str, Any], bytes]:
    data = path.read_bytes()
    if len(data) < 20:
        raise SurfacePropertyGlbValidationError(f"{path} is only {len(data)} bytes")
    magic, version, total = struct.unpack_from("<III", data)
    if magic != 0x46546C67 or version != 2 or total != len(data):
        raise SurfacePropertyGlbValidationError("invalid GLB header")
    position = 12
    chunks = []
    while position < len(data):
        if position + 8 > len(data):
            raise SurfacePropertyGlbValidationError("truncated GLB chunk header")
        size, kind = struct.unpack_from("<II", data, position)
        position += 8
        end = position + size
        if end > len(data):
            raise SurfacePropertyGlbValidationError("GLB chunk overruns file")
        chunks.append((kind, data[position:end]))
        position = end
    if not chunks or chunks[0][0] != 0x4E4F534A:
        raise SurfacePropertyGlbValidationError(
            "Surface-property GLB must open with a JSON chunk"
        )
    if len(chunks) > 2 or (len(chunks) == 2 and chunks[1][0] != 0x004E4942):
        raise SurfacePropertyGlbValidationError(
            "Surface-property GLB carries at most a JSON then a BIN chunk"
        )
    try:
        document = json.loads(chunks[0][1].decode("utf-8").rstrip(" "))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise SurfacePropertyGlbValidationError(f"invalid GLB JSON: {error}") from error
    return document, chunks[1][1] if len(chunks) == 2 else b""


def _reject_opaque_source(value: Any, path: str = "$") -> None:
    if isinstance(value, dict):
        for key, item in value.items():
            lowered = str(key).lower().replace("_", "")
            if lowered in {"rawdata", "sourcebytes", "opaquebytes", "base64", "sourcetext"}:
                raise SurfacePropertyGlbValidationError(
                    f"opaque source payload is forbidden at {path}.{key}"
                )
            _reject_opaque_source(item, f"{path}.{key}")
    elif isinstance(value, list):
        for index, item in enumerate(value):
            _reject_opaque_source(item, f"{path}[{index}]")


def _check_ledgers(coverage, members, source_members=None) -> tuple[int, int]:
    ledgers = coverage.get("byteLedger")
    if not isinstance(ledgers, list) or len(ledgers) != len(members):
        raise SurfacePropertyGlbValidationError(
            "byte ledger count disagrees with source members"
        )
    identities = {str(member.get("path")): member for member in members}
    source_data = (
        {str(member.path): member.data for member in source_members}
        if source_members is not None else {}
    )
    if source_members is not None and set(source_data) != set(identities):
        raise SurfacePropertyGlbValidationError(
            "prepublication source members do not match sourceResolution"
        )
    seen = set()
    source_total = accounted_total = 0
    for ledger in ledgers:
        path = str(ledger.get("sourcePath", ""))
        if path in seen or path not in identities:
            raise SurfacePropertyGlbValidationError(f"invalid byte ledger source {path!r}")
        seen.add(path)
        identity = identities[path]
        length = int(ledger.get("byteLength", -1))
        digest = str(ledger.get("sourceSha256", ""))
        if length != int(identity.get("byteLength", -2)) or digest != identity.get("sha256"):
            raise SurfacePropertyGlbValidationError(f"{path}: ledger identity disagrees")
        raw = source_data.get(path)
        if raw is not None and (len(raw) != length or hashlib.sha256(raw).hexdigest() != digest):
            raise SurfacePropertyGlbValidationError(
                f"{path}: prepublication source bytes disagree"
            )
        cursor = 0
        totals = Counter()
        ranges = ledger.get("ranges")
        if not isinstance(ranges, list):
            raise SurfacePropertyGlbValidationError(f"{path}: byte ledger ranges are missing")
        for index, row in enumerate(ranges):
            offset, size = int(row.get("offset", -1)), int(row.get("length", -1))
            state, owner = str(row.get("state", "")), str(row.get("owner", ""))
            if offset != cursor or size <= 0 or state not in BYTE_STATES or not owner:
                raise SurfacePropertyGlbValidationError(f"{path}: invalid byte range {index}")
            if offset + size > length:
                raise SurfacePropertyGlbValidationError(
                    f"{path}: byte range {index} overruns source"
                )
            if raw is not None and state.endswith("-zero") and any(raw[offset:offset + size]):
                raise SurfacePropertyGlbValidationError(f"{path}: false zero range {index}")
            cursor += size
            totals[state] += size
        if cursor != length or int(ledger.get("accountedBytes", -1)) != length:
            raise SurfacePropertyGlbValidationError(f"{path}: byte ledger is not gapless")
        if float(ledger.get("coveragePercent", -1)) != 100.0:
            raise SurfacePropertyGlbValidationError(f"{path}: byte coverage is not 100%")
        if dict(sorted(totals.items())) != ledger.get("stateBytes"):
            raise SurfacePropertyGlbValidationError(f"{path}: state totals disagree")
        canonical = json.dumps(
            {"path": path, "byteLength": length, "ranges": ranges},
            sort_keys=True, separators=(",", ":"),
        ).encode("utf-8")
        if hashlib.sha256(canonical).hexdigest() != ledger.get("rangesSha256"):
            raise SurfacePropertyGlbValidationError(f"{path}: range digest disagrees")
        source_total += length
        accounted_total += cursor
    if seen != set(identities):
        raise SurfacePropertyGlbValidationError(
            "byte ledgers do not cover every source member"
        )
    return source_total, accounted_total


def _check_parameters(extension: dict[str, Any]) -> list[dict[str, Any]]:
    """Every pair must be indexed in source order and own a distinct offset in the entry.

    The table is entitled to declare a name and nothing else -- `weapon` does -- so an empty
    parameter table is a complete unit, not a decode that read nothing.
    """

    parameters = extension.get("parameters")
    if not isinstance(parameters, list):
        raise SurfacePropertyGlbValidationError("the surface declares no parameter table")
    offsets = []
    for index, row in enumerate(parameters):
        if not isinstance(row, dict) or row.get("index") != index:
            raise SurfacePropertyGlbValidationError(f"parameter {index} is not in source order")
        key, source_key = row.get("key"), row.get("sourceKey")
        if not isinstance(key, str) or not key:
            raise SurfacePropertyGlbValidationError(f"parameter {index} has no key")
        if not isinstance(source_key, str) or source_key.strip().lower() != key:
            raise SurfacePropertyGlbValidationError(
                f"parameter {index} key disagrees with its source"
            )
        offset = row.get("offset")
        if not isinstance(offset, int) or offset < 0 or offset in offsets:
            raise SurfacePropertyGlbValidationError(
                f"parameter {index} has no distinct source offset"
            )
        offsets.append(offset)
    if offsets != sorted(offsets):
        raise SurfacePropertyGlbValidationError("the parameter table is not in source order")
    return parameters


def _sound_records(extension: dict[str, Any], parameters: list[dict[str, Any]]) -> list[dict]:
    """Every footstep and impact record, checked against the parameter that produced it."""

    records: list[dict[str, Any]] = []

    def take(record: Any, where: str) -> None:
        if not isinstance(record, dict):
            raise SurfacePropertyGlbValidationError(f"{where} is not a sound record")
        asset = str(record.get("asset") or "")
        if not asset.startswith("vtmb:sound:"):
            raise SurfacePropertyGlbValidationError(f"{where} carries no stable sound id")
        slot = record.get("parameter")
        if not isinstance(slot, int) or not 0 <= slot < len(parameters):
            raise SurfacePropertyGlbValidationError(f"{where} names no parameter of this entry")
        if parameters[slot].get("value") != record.get("path"):
            raise SurfacePropertyGlbValidationError(f"{where} disagrees with its source value")
        records.append(record)

    footsteps = extension.get("footsteps") or {}
    if not isinstance(footsteps, dict) or set(footsteps) - FOOTSTEP_FIELDS:
        raise SurfacePropertyGlbValidationError("footsteps name a foot the table has no key for")
    for foot, pool in footsteps.items():
        if not isinstance(pool, list) or not pool:
            raise SurfacePropertyGlbValidationError(f"footstep {foot} carries no variation")
        for ordinal, record in enumerate(pool):
            take(record, f"footsteps.{foot}[{ordinal}]")

    impacts = extension.get("impacts") or {}
    if not isinstance(impacts, dict) or set(impacts) - (WEAPON_CLASSES | {"legacy"}):
        raise SurfacePropertyGlbValidationError("impacts name a weapon class outside the matrix")
    for weapon, outcomes in impacts.items():
        if weapon == "legacy":
            if not isinstance(outcomes, list) or not outcomes:
                raise SurfacePropertyGlbValidationError("the legacy impact key carries no sound")
            for ordinal, record in enumerate(outcomes):
                take(record, f"impacts.legacy[{ordinal}]")
            continue
        if not isinstance(outcomes, dict) or set(outcomes) - DAMAGE_OUTCOMES:
            raise SurfacePropertyGlbValidationError(
                f"impacts.{weapon} names an outcome outside the damage model"
            )
        for outcome, pool in outcomes.items():
            if not isinstance(pool, list) or not pool:
                raise SurfacePropertyGlbValidationError(
                    f"impacts.{weapon}.{outcome} carries no variation"
                )
            for ordinal, record in enumerate(pool):
                take(record, f"impacts.{weapon}.{outcome}[{ordinal}]")
    return records


def _check_references(
    extension: dict[str, Any], parameters: list[dict[str, Any]]
) -> tuple[list[dict], list[str]]:
    """Base, sounds and sound scripts must each own the dependency row they produced.

    Returns the sound-script records and the script names that resolved against nothing.
    """

    referenced: dict[str, str] = {}
    base = extension.get("base")
    if base is not None:
        if not isinstance(base, dict):
            raise SurfacePropertyGlbValidationError("base is not an inheritance record")
        asset = str(base.get("asset") or "")
        if asset != "vtmb:surface-property:" + str(base.get("name") or ""):
            raise SurfacePropertyGlbValidationError("base names no stable surface identity")
        if asset == extension.get("identity", {}).get("asset"):
            raise SurfacePropertyGlbValidationError("a surface inherits from itself")
        referenced[asset] = "surface-property"

    for record in _sound_records(extension, parameters):
        referenced[str(record["asset"])] = "sound"

    scripts = extension.get("sounds") or {}
    if not isinstance(scripts, dict) or set(scripts) - SOUND_SCRIPT_FIELDS:
        raise SurfacePropertyGlbValidationError("sounds name a key outside the physics pair")
    script_records: list[dict[str, Any]] = []
    unresolved_scripts: list[str] = []
    for key, pool in scripts.items():
        if not isinstance(pool, list) or not pool:
            raise SurfacePropertyGlbValidationError(f"sounds.{key} carries no script")
        for ordinal, record in enumerate(pool):
            if not isinstance(record, dict):
                raise SurfacePropertyGlbValidationError(f"sounds.{key}[{ordinal}] is not a record")
            asset = str(record.get("asset") or "")
            if not asset.startswith("vtmb:sound-script:"):
                raise SurfacePropertyGlbValidationError(
                    f"sounds.{key}[{ordinal}] carries no stable sound-script id"
                )
            slot = record.get("parameter")
            if not isinstance(slot, int) or not 0 <= slot < len(parameters):
                raise SurfacePropertyGlbValidationError(
                    f"sounds.{key}[{ordinal}] names no parameter of this entry"
                )
            referenced[asset] = "sound-script"
            script_records.append(record)
            if not record.get("resolved"):
                unresolved_scripts.append(str(record.get("script") or asset))

    declared: dict[str, str] = {}
    for row in extension.get("dependencies") or []:
        if not isinstance(row, dict):
            raise SurfacePropertyGlbValidationError("a dependency row is not a record")
        role, asset = str(row.get("role") or ""), str(row.get("asset") or "")
        if role not in DEPENDENCY_ROLES:
            raise SurfacePropertyGlbValidationError(f"unknown dependency role {role!r}")
        if asset in declared:
            raise SurfacePropertyGlbValidationError(f"dependency {asset} is declared twice")
        declared[asset] = role
    if declared != referenced:
        raise SurfacePropertyGlbValidationError(
            "the dependency set disagrees with the references that produced it"
        )
    return script_records, unresolved_scripts


def _check_values(extension: dict[str, Any]) -> None:
    physics = extension.get("physics") or {}
    if not isinstance(physics, dict) or set(physics) - PHYSICS_FIELDS:
        raise SurfacePropertyGlbValidationError("physics names a key the table has no field for")
    for field, value in physics.items():
        if not isinstance(value, (int, float)) or isinstance(value, bool):
            raise SurfacePropertyGlbValidationError(f"physics.{field} is not a number")
    movement = extension.get("movement") or {}
    if not isinstance(movement, dict) or set(movement) - MOVEMENT_FIELDS:
        raise SurfacePropertyGlbValidationError("movement names a key the table has no field for")
    if "climbable" in movement and not isinstance(movement["climbable"], bool):
        raise SurfacePropertyGlbValidationError("movement.climbable is not a flag")
    for field in ("maxSpeedFactor", "jumpFactor"):
        if field in movement and not isinstance(movement[field], (int, float)):
            raise SurfacePropertyGlbValidationError(f"movement.{field} is not a number")
    material = extension.get("gameMaterial")
    if material is not None and (not isinstance(material, str) or not material):
        raise SurfacePropertyGlbValidationError("gameMaterial is neither a class nor absent")


def validate_document(document: dict, binary: bytes, *, source_members=None) -> dict[str, Any]:
    if document.get("asset", {}).get("version") != "2.0":
        raise SurfacePropertyGlbValidationError("asset.version is not 2.0")
    used = set(document.get("extensionsUsed") or [])
    required = set(document.get("extensionsRequired") or [])
    if SURFACE_PROPERTY_EXTENSION not in used or SURFACE_PROPERTY_EXTENSION not in required:
        raise SurfacePropertyGlbValidationError(
            "surface-property extension must be used and required"
        )
    if any(document.get(name) for name in FORBIDDEN_CORE):
        raise SurfacePropertyGlbValidationError(
            "Surface-property GLB carries no scene, mesh, material or image core"
        )
    if document.get("buffers") or document.get("bufferViews") or document.get("accessors"):
        if not binary:
            raise SurfacePropertyGlbValidationError("a declared buffer has no BIN chunk")
    elif binary:
        raise SurfacePropertyGlbValidationError(
            "a BIN chunk is present that nothing references"
        )
    extension = (document.get("extensions") or {}).get(SURFACE_PROPERTY_EXTENSION)
    if not isinstance(extension, dict) or extension.get("schemaVersion") != SCHEMA_VERSION:
        raise SurfacePropertyGlbValidationError(
            "missing or unsupported surface-property extension"
        )
    identity = extension.get("identity") or {}
    asset = str(identity.get("asset", ""))
    name = str(identity.get("name", ""))
    if not asset.startswith("vtmb:surface-property:"):
        raise SurfacePropertyGlbValidationError(
            "surface-property extension has no stable identity"
        )
    if asset != "vtmb:surface-property:" + name or name != name.lower():
        raise SurfacePropertyGlbValidationError("surface identity disagrees with its name")
    if str(identity.get("sourceName", "")).strip().lower() != name:
        raise SurfacePropertyGlbValidationError("the name disagrees with its source spelling")
    coverage = extension.get("coverage") or {}
    if coverage.get("unresolved") or coverage.get("unsupported"):
        raise SurfacePropertyGlbValidationError("surface-property extension is incomplete")
    for row in extension.get("anomalies") or []:
        if not isinstance(row, dict) or row.get("role") not in ANOMALY_ROLES:
            raise SurfacePropertyGlbValidationError(f"unknown source anomaly {row!r}")
    members = ((extension.get("sourceResolution") or {}).get("members") or [])
    if len(members) != 1 or members[0].get("role") != "entry":
        raise SurfacePropertyGlbValidationError("a surface unit owns exactly one table entry")
    if not str(members[0].get("path", "")).endswith("#" + name):
        raise SurfacePropertyGlbValidationError("the source member is not this surface's entry")
    source_total, accounted_total = _check_ledgers(coverage, members, source_members)
    _reject_opaque_source(extension)
    parameters = _check_parameters(extension)
    _check_values(extension)
    scripts, unresolved_scripts = _check_references(extension, parameters)
    return {
        "asset": asset,
        "name": name,
        "base": (extension.get("base") or {}).get("name"),
        "physics": len(extension.get("physics") or {}),
        "movement": len(extension.get("movement") or {}),
        "footsteps": sum(len(pool) for pool in (extension.get("footsteps") or {}).values()),
        "impacts": sum(
            len(pool) if isinstance(pool, list) else sum(len(row) for row in pool.values())
            for pool in (extension.get("impacts") or {}).values()
        ),
        "soundScripts": len(scripts),
        "gameMaterial": extension.get("gameMaterial"),
        "parameters": len(parameters),
        "dependencies": len(extension.get("dependencies") or []),
        "anomalies": [str(row.get("role")) for row in extension.get("anomalies") or []],
        "missingSoundScripts": unresolved_scripts,
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
    missing = summary.get("missingSoundScripts") or []
    if missing:
        warnings.append(
            "the install carries no sound script named "
            + ", ".join(sorted(set(missing))[:4])
            + (f" and {len(set(missing)) - 4} more" if len(set(missing)) > 4 else "")
        )
    anomalies = summary.get("anomalies") or []
    if anomalies:
        counted = ", ".join(
            f"{role}x{count}" for role, count in sorted(Counter(anomalies).items())
        )
        warnings.append(f"the source departs from the table's conventions: {counted}")
    return warnings
