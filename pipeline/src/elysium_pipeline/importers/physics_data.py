"""Cooked PHYS1 source data from one selected skeletal GLB and its staged body.

No install/legacy reads, simulation, physical-parameter conversion, or shared
manifest mutation. Writers require an explicit separate destination. Geometry is
kept in published IVP axis-only metres; this is not a PhysicsAsset recipe.
"""
from __future__ import annotations

from copy import deepcopy
import hashlib
import json
import math
from pathlib import Path
import re
import struct

from elysium_pipeline.asset_names import rig_bone_name
from elysium_pipeline.asset_paths import baked_unit
from elysium_pipeline.formats.unit_contract.container import decode_glb

PRODUCER = "character-physics-data"
SCHEMA = "1.0.0"
EXTENSION = "ELYSIUM_vtmb_model"
GEOMETRY_FRAME = "IVP metres, axis-only"


class PhysicsDataError(ValueError):
    pass


def json_text(value):
    return json.dumps(value, ensure_ascii=True, allow_nan=False, separators=(",", ":"))


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def _int(value, field, minimum=0):
    if type(value) is not int or not minimum <= value <= 2147483647:
        raise PhysicsDataError(f"{field}: expected int32 >= {minimum}")
    return value


def _number(value, field):
    if type(value) not in (int, float) or not math.isfinite(value) or (isinstance(value, int) and int(float(value)) != value):
        raise PhysicsDataError(f"{field}: number cannot be retained as a native double")
    return value


def _string(value, field):
    if not isinstance(value, str):
        raise PhysicsDataError(f"{field}: expected string")
    return value


def _vector(value, field, length=3):
    if not isinstance(value, list) or len(value) != length:
        raise PhysicsDataError(f"{field}: expected {length} components")
    return [_number(v, field) for v in value]


def _numbers(values):
    # Absence is an absent named entry, never a guessed numeric default. Unknown
    # numeric parameters are typed too; every other field remains in cooked evidence.
    for key in ("mass", "damping", "rotdamping", "inertia", "volume", "massbias", "totalmass"):
        if key in values:
            _number(values[key], key)
    return [{"name": k, "value": _number(v, k)} for k, v in values.items() if type(v) in (int, float)]


def _optional(values, name):
    return {"bPresent": name in values,
            "value": _number(values[name], name) if name in values else 0.0}


def _accessor(document, binary, index, kind):
    _int(index, kind)
    try:
        row = document["accessors"][index]
        view_index = _int(row["bufferView"], "bufferView")
        view = document["bufferViews"][view_index]
    except (IndexError, KeyError) as error:
        raise PhysicsDataError(f"missing physics {kind} accessor/view {index}") from error
    expected = "VEC3" if kind == "positions" else "SCALAR"
    allowed = (5126,) if kind == "positions" else (5121, 5123, 5125)
    if row["type"] != expected or row["componentType"] not in allowed or "sparse" in row or row.get("normalized", False):
        raise PhysicsDataError(f"unsupported physics accessor {index}; no data omitted")
    if view.get("buffer", 0) != 0:
        raise PhysicsDataError("physics accessor uses an external buffer")
    count = _int(row["count"], "accessor.count")
    size = {5121: 1, 5123: 2, 5125: 4, 5126: 4}[row["componentType"]]
    packed = size * (3 if kind == "positions" else 1)
    stride = _int(view.get("byteStride", packed), "byteStride")
    relative = _int(row.get("byteOffset", 0), "accessor.byteOffset")
    start = _int(view.get("byteOffset", 0), "view.byteOffset")
    length = _int(view["byteLength"], "view.byteLength")
    occupied = (count-1)*stride+packed if count else 0
    if count > 2_000_000 or stride < packed or stride % size or relative % size or start % size or relative+occupied > length or start+length > len(binary):
        raise PhysicsDataError(f"out-of-bounds/misaligned physics accessor {index}")
    decoder = struct.Struct("<" + ("3f" if kind == "positions" else {5121: "B", 5123: "H", 5125: "I"}[row["componentType"]]))
    values = [decoder.unpack_from(binary, start+relative+i*stride) for i in range(count)]
    if kind != "positions":
        values = [v[0] for v in values]
    return values, {"index": index, "accessor": deepcopy(row), "bufferViewIndex": view_index,
                    "bufferView": deepcopy(view)}


def physics_projection(document, binary, staged_body, *, source_glb_sha256, staged_body_sha256):
    """Pure projection. Source evidence is complete JSON, deliberately cooked."""
    for value in (source_glb_sha256, staged_body_sha256):
        if not isinstance(value, str) or not re.fullmatch(r"[0-9a-f]{64}", value):
            raise PhysicsDataError("missing/invalid input SHA256")
    root = document["extensions"][EXTENSION]
    asset_id = root["identity"]["asset"]
    if not asset_id.startswith("vtmb:model:") or staged_body["assetId"] != asset_id:
        raise PhysicsDataError("physics GLB/body identity mismatch")
    physics = root["physics"]
    bones = root["mdl"]["bones"]
    semantics = staged_body["sourceSemantics"]
    if semantics["physics"] != physics or semantics["mdl"]["bones"] != bones:
        raise PhysicsDataError("staged physics/bones differ from their GLB")
    native_bones, by_name, folded, gaps, solids, joints, kvs, edit_params = [], {}, set(), [], [], [], [], []
    for i, bone in enumerate(bones):
        if _int(bone["index"], "bone.index") != i or not -1 <= _int(bone["parent"], "bone.parent", -1) < i:
            raise PhysicsDataError("source bones are not a parent-first indexed tree")
        name = _string(bone["name"], "bone.name")
        native = rig_bone_name(name)
        if not name or name.casefold() in by_name or native.casefold() in folded or native.casefold() == "none":
            raise PhysicsDataError("ambiguous/empty source or native bone name: " + name)
        by_name[name.casefold()] = i
        folded.add(native.casefold())
        native_bones.append({"index": i, "parent": bone["parent"], "sourceName": name,
                             "nativeName": native, "poseToBone": _vector(bone["poseToBone"], "poseToBone", 12)})

    def gap(kind, ordinal, field, name, reason):
        gaps.append({"kind": kind, "ordinal": ordinal, "field": field, "sourceName": name, "reason": reason})

    def bone_join(name, ordinal, field, required):
        if not name and not required:
            return -1
        index = by_name.get(name.casefold(), -1)
        if index < 0:
            gap("source-bone", ordinal, field, name, "name absent from source MDL bones" if name else "source solid has no bone name")
        return index

    accessor_evidence = {}
    if physics is not None:
        if not isinstance(physics, dict) or physics.get("coordinateSystem") != GEOMETRY_FRAME:
            raise PhysicsDataError("unrecognized published physics frame")
        for ordinal, solid in enumerate(physics["solids"]):
            properties = solid["properties"]
            name = _string(properties.get("name", ""), "solid.name")
            parent = _string(properties.get("parent", ""), "solid.parent")
            index = bone_join(name, ordinal, "name", True)
            parent_index = bone_join(parent, ordinal, "parent", bool(parent))
            hulls = []
            for ledge, hull in enumerate(solid["hulls"]):
                points, point_evidence = _accessor(document, binary, hull["positions"], "positions")
                indices, index_evidence = _accessor(document, binary, hull["indices"], "indices")
                for evidence in (point_evidence, index_evidence):
                    accessor_evidence.setdefault(evidence["index"], evidence)
                if not points or len(indices) % 3 or any(type(v) is not int or not 0 <= v < len(points) for v in indices):
                    raise PhysicsDataError(f"invalid hull topology: {ordinal}/{ledge}")
                hulls.append({"solidOrdinal": ordinal, "ledgeOrdinal": ledge,
                              "sourceOffset": _int(hull["sourceOffset"], "ledge.sourceOffset"),
                              "positionAccessor": hull["positions"], "indexAccessor": hull["indices"],
                              "vertices": [{"x": _number(x, "vertex.x"), "y": _number(y, "vertex.y"), "z": _number(z, "vertex.z")} for x, y, z in points],
                              "indices": indices})
            solids.append({"ordinal": ordinal, "binaryIndex": _int(solid["index"], "solid.index"),
                           "authoredIndex": _optional(properties, "index"),
                           "sourceOffset": _int(solid["sourceOffset"], "solid.sourceOffset"),
                           "sourceName": name, "sourceParent": parent, "sourceBoneIndex": index,
                           "parentBoneIndex": parent_index, "nativeBoneName": native_bones[index]["nativeName"] if index >= 0 else "",
                           "origin": _vector(properties["origin"], "origin") if "origin" in properties else [],
                           "angles": _vector(properties["angles"], "angles") if "angles" in properties else [],
                           "massCenterIvp": _vector(solid["massCenter"], "massCenter"),
                           "rotationInertiaIvp": _vector(solid["rotationInertia"], "rotationInertia"),
                           "surfaceProperty": _string(properties.get("surfaceprop", ""), "surfaceprop"),
                           "parameters": _numbers(properties), "hulls": hulls})
        # Neither file order nor binary ordinal is a substitute for authored solid ID.
        by_id = {}
        for solid in solids:
            authored = solid["authoredIndex"]
            if authored["bPresent"]:
                key = _int(authored["value"], "solid authored index")
                by_id.setdefault(key, []).append(solid["ordinal"])
        for ordinal, joint in enumerate(physics["constraints"]):
            endpoints = []
            for field in ("parent", "child"):
                key = _int(joint[field], "constraint." + field)
                matches = by_id.get(key, [])
                endpoints.append(matches[0] if len(matches) == 1 else -1)
                if len(matches) != 1:
                    gap("solid-reference", ordinal, field, str(key), "absent or ambiguous authored solid index")
            joints.append({"ordinal": ordinal, "parentSolidIndex": joint["parent"], "childSolidIndex": joint["child"],
                           "parentSolidOrdinal": endpoints[0], "childSolidOrdinal": endpoints[1],
                           "axes": [{"name": axis, "minimum": _optional(joint, axis+"min"),
                                     "maximum": _optional(joint, axis+"max"), "friction": _optional(joint, axis+"friction")} for axis in "xyz"]})
        for block in physics["keyValues"]:
            kvs.append({"blockType": _string(block["type"], "KV type"),
                        "pairs": [{"key": _string(p["key"], "KV key"), "value": _string(p["value"], "KV value")} for p in block["pairs"]]})
        edit_params = [{"ordinal": i, "parameters": _numbers(row)} for i, row in enumerate(physics["editParams"])]
    evidence = {"physics": deepcopy(physics), "bones": deepcopy(bones),
                "boneNodes": deepcopy(document.get("nodes", [])[:len(bones)]),
                "sourceResolution": deepcopy(root.get("sourceResolution")),
                "anomalies": deepcopy(root.get("anomalies", [])),
                "accessors": list(accessor_evidence.values())}
    result = {"schemaVersion": SCHEMA, "assetId": asset_id,
              "assetPath": baked_unit(asset_id, "DA", role="physics"),
              "sourceGlbSha256": source_glb_sha256, "stagedBodySha256": staged_body_sha256,
              "sourceEvidenceJson": json_text(evidence), "bHasPhysics": physics is not None,
              "geometryFrame": GEOMETRY_FRAME, "headerParameters": _numbers(physics["header"]) if physics is not None else [],
              "bones": native_bones, "solids": solids,
              "constraints": joints, "keyValues": kvs, "editParams": edit_params, "gaps": gaps}
    json_text(result)  # Refuse non-JSON/nonfinite values even in evidence-only future fields.
    return result


def _below(root, relative):
    root = Path(root).resolve()
    path = (root / relative).resolve()
    if Path(relative).is_absolute() or not path.is_relative_to(root):
        raise PhysicsDataError("source path escapes supplied root")
    return path


def project_selected_entry(entry, selected_units, export_root, stage_root):
    """Read-only, one unit at a time. Input hashes are the current stage contract."""
    if entry["assetId"] not in selected_units or not entry.get("meshAsset") or not entry.get("nativeMainOwner", True) or entry.get("aggregateOnly"):
        raise PhysicsDataError("entry is not a selected native skeletal mesh owner")
    glb_path = _below(export_root, entry["unitGlb"])
    body_path = _below(stage_root, entry["body"])
    if glb_path.stat().st_size > 256*1024*1024 or body_path.stat().st_size > 64*1024*1024:
        raise PhysicsDataError("single-unit physics read bound exceeded")
    glb, body = glb_path.read_bytes(), body_path.read_bytes()
    hashes = sha256(glb), sha256(body)
    if hashes != (entry["recipe"]["unitSha256"], entry["recipe"]["bodySha256"]):
        raise PhysicsDataError("GLB/body changed after skeletal staging")
    document, binary = decode_glb(glb, str(glb_path))
    result = physics_projection(document, binary, json.loads(body), source_glb_sha256=hashes[0], staged_body_sha256=hashes[1])
    if result["assetId"] != entry["assetId"]:
        raise PhysicsDataError("manifest identity differs from physics inputs")
    return result


def stage_entry(entry, selected_units, export_root, stage_root, output_root):
    """Optional separate stage product. Never edits the character manifest/input roots."""
    output = Path(output_root).resolve()
    for source in (Path(export_root).resolve(), Path(stage_root).resolve()):
        if output.is_relative_to(source) or source.is_relative_to(output):
            raise PhysicsDataError("physics output must be disjoint from GLB/character stage roots")
    projection = project_selected_entry(entry, selected_units, export_root, stage_root)
    relative = projection["assetId"].removeprefix("vtmb:model:") + ".physics.json"
    path = _below(output, relative)
    data = (json_text(projection)+"\n").encode("utf-8")
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_bytes(data)
    temporary.replace(path)
    return {"assetId": projection["assetId"], "assetPath": projection["assetPath"],
            "projectionFile": relative, "projectionSha256": sha256(data),
            "hasPhysics": projection["bHasPhysics"], "sourceGaps": len(projection["gaps"])}
