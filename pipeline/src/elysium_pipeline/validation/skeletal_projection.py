"""Independent comparisons for deliberate legacy omissions, never blanket exclusions."""
from __future__ import annotations

from collections import OrderedDict
import hashlib
import struct

import numpy as np

from elysium_pipeline.formats.unit_contract.container import decode_glb
from elysium_pipeline.formats.unit_contract.precision import decode as precise
from elysium_pipeline.validation.skeletal_diff import Cursor, SkeletalDiffError, sections, records, compare_payloads


def assemble(parts):
    offset = 16 + 20 * len(parts)
    directory, data = bytearray(), bytearray()
    for tag, content in parts.items():
        directory += struct.pack("<4sQQ", tag.encode("ascii"), offset, len(content))
        data += content
        offset += len(content)
    return struct.pack("<4sIII", b"ESKM", 8, len(parts), 0) + directory + data


def clip_blocks(section):
    cursor = Cursor(section)
    result = []
    for _ in range(cursor.number()):
        start = cursor.at
        name, base = cursor.string(), cursor.string()
        frames = cursor.number()
        cursor.take(8)  # fps, flags
        mask_at = cursor.at - start
        mask = cursor.number("i")
        for _ in range(cursor.number()):
            cursor.take(4)  # bone
            position, rotation = cursor.number("B"), cursor.number("B")
            if position not in (0, 1) or rotation not in (0, 1):
                raise SkeletalDiffError("invalid animation channel bits")
            cursor.take(frames * (12 * position + 16 * rotation))
        result.append({"name": name, "base": base, "mask": mask, "maskAt": mask_at,
                       "data": bytes(section[start:cursor.at])})
    if cursor.at != len(section):
        raise SkeletalDiffError("unclaimed animation bytes")
    return result


def project_clip_subset(before, after):
    """Keep the legacy clip selection, proving its original order remains in the full set."""
    old = clip_blocks(before.get("ANIM", b"\0\0\0\0"))
    new = clip_blocks(after.get("ANIM", b"\0\0\0\0"))
    names = [row["name"] for row in old]
    wanted = set(names)
    selected = [row for row in new if row["name"] in wanted]
    if [row["name"] for row in selected] != names:
        raise SkeletalDiffError("legacy clip selection is missing or reordered")
    extra = [row["name"] for row in new if row["name"] not in wanted]
    if not extra:
        return after, []
    masks = []
    if "MASK" in after:
        cursor = Cursor(after["MASK"])
        for _ in range(cursor.number()):
            masks.append(bytes(cursor.take(cursor.number())))
        if cursor.at != len(after["MASK"]):
            raise SkeletalDiffError("unclaimed mask bytes")
    used, clips = [], []
    for row in selected:
        data = bytearray(row["data"])
        if row["mask"] >= 0:
            if row["mask"] >= len(masks):
                raise SkeletalDiffError("clip names a missing mask")
            value = masks[row["mask"]]
            if value not in used:
                used.append(value)
            struct.pack_into("<i", data, row["maskAt"], used.index(value))
        clips.append(data)
    result = OrderedDict(after)
    result["ANIM"] = struct.pack("<I", len(clips)) + b"".join(clips)
    if used:
        result["MASK"] = struct.pack("<I", len(used)) + b"".join(struct.pack("<I", len(m)) + m for m in used)
    else:
        result.pop("MASK", None)
    return result, extra


def _indices(document, binary, index):
    row = document["accessors"][index]
    if row["type"] != "SCALAR":
        raise SkeletalDiffError("indices must be scalar")
    code, size = {5121: ("B", 1), 5123: ("H", 2), 5125: ("I", 4)}[row["componentType"]]
    view = document["bufferViews"][row["bufferView"]]
    start = view.get("byteOffset", 0) + row.get("byteOffset", 0)
    stride = view.get("byteStride", size)
    return [struct.unpack_from("<" + code, binary, start + i * stride)[0] for i in range(row["count"])]


def source_geometry(path, expected_sha):
    raw = path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != expected_sha:
        raise SkeletalDiffError("source GLB changed since staging")
    document, binary = decode_glb(raw, path)
    root = document["extensions"]["ELYSIUM_vtmb_model"]
    lod = next(row for row in root["vtx"]["lods"] if row["index"] == 0)
    groups, remaps = OrderedDict(), []
    for primitive in document["meshes"][lod["mesh"]]["primitives"]:
        source = primitive["extensions"]["ELYSIUM_vtmb_model"]
        material = root["mdl"]["textures"][source["skinReference"]]["name"]
        model = root["mdl"]["bodyParts"][source["bodyPart"]]["models"][source["model"]]
        count = len(source["sourceVertices"])
        positions = np.asarray(precise(document, binary, source["sourcePositions"], (count, 3))).reshape(count, 3)
        normals = np.asarray(precise(document, binary, source["sourceNormals"], (count, 3))).reshape(count, 3)
        indices = _indices(document, binary, primitive["indices"])
        order = list(dict.fromkeys(indices))
        group = groups.setdefault(material, {"positions": [], "normals": [], "types": [], "triangles": []})
        remap = {local: len(group["positions"]) + i for i, local in enumerate(order)}
        group["positions"].extend(positions[order])
        group["normals"].extend(normals[order])
        group["types"].extend([model["vertexListType"]] * len(order))
        group["triangles"].extend(tuple(remap[j] for j in indices[i:i + 3]) for i in range(0, len(indices), 3))
        remaps.append((source, material, remap))
    offsets, total = {}, 0
    for material, group in groups.items():
        offsets[material] = total
        total += len(group["positions"])
    positions = np.asarray([v for group in groups.values() for v in group["positions"]]) * (2.54, -2.54, 2.54)
    normals = np.asarray([v for group in groups.values() for v in group["normals"]]) * (1., -1., 1.)
    types = np.asarray([v for group in groups.values() for v in group["types"]])
    indices = [i + offsets[name] for name, group in groups.items()
               for a, b, c in group["triangles"] for i in (a, c, b)]
    morphs = []
    for target in root["facial"]["morphTargets"]:
        deltas = {}
        for source, material, remap in remaps:
            for record in source["morphRecords"]:
                if record["target"] != target["index"] or record["vertex"] is None:
                    continue
                vertex = offsets[material] + remap[record["vertex"]]
                values = np.asarray([*record["position"], *record["normal"]]) * (2.54, -2.54, 2.54, 1., -1., 1.)
                deltas.setdefault(vertex, np.zeros(6))[:] += values
        morphs.append((target["name"], deltas))
    return {"positions": positions.astype(np.float32), "positions64": positions, "normals": normals.astype(np.float32),
            "types": types, "indices": np.asarray(indices, dtype="<u4").tobytes(),
            "slots": list(groups), "morphs": morphs}


def _mesh_fields(section):
    return {key: value for key, value, _ in records("MESH", section)}


def _replace_normals(section, normals):
    data = bytearray(section)
    cursor = Cursor(section)
    count = cursor.number()
    cursor.number()
    for _ in range(cursor.number()):
        cursor.string()
        cursor.take(8)
    for i in range(count):
        struct.pack_into("<3f", data, cursor.at + 56 * i + 12, *normals[i])
    return data


def _legacy_geometric_normals(source):
    positions = source["positions64"]
    result = np.zeros_like(positions)
    for a, b, c in np.frombuffer(source["indices"], dtype="<u4").reshape(-1, 3):
        # Undo the emitted winding to reconstruct the legacy writer's own fallback rule.
        normal = np.cross(positions[c] - positions[a], positions[b] - positions[a])
        for vertex in (a, b, c):
            result[vertex] += normal
    lengths = np.linalg.norm(result, axis=1)
    valid = lengths > 1e-12
    result[valid] /= lengths[valid, None]
    result[~valid] = (0., 0., 1.)
    return result.astype(np.float32)


def _direct_source_clips_match(path, expected_sha, blocks):
    """Prove new direct clips in a formerly 'full' export, without accepting derived poses blindly."""
    raw = path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != expected_sha:
        raise SkeletalDiffError("source GLB changed since staging")
    document, binary = decode_glb(raw, path)
    root = document["extensions"]["ELYSIUM_vtmb_model"]
    bones = root["mdl"]["bones"]
    if sum(b["parent"] < 0 for b in bones) != 1 or any(b["flags"] & 2 and b["parent"] >= 0 for b in bones):
        return False
    sequences = {}
    for seq in root["mdl"]["sequences"]:
        if seq["label"] and 0 <= seq["baseAnimation"] < len(root["mdl"]["localAnimations"]):
            sequences.setdefault(seq["label"], seq)
    for block in blocks:
        seq = sequences.get(block["name"])
        if seq is None or seq["flags"] & 4 or block["base"]:
            return False
        animation = root["mdl"]["localAnimations"][seq["baseAnimation"]]
        # Only full ownership here. Partial/derived additions need their own projection proof.
        if any(weight != 1 for weight in animation["boneWeights"]):
            return False
        samples = np.asarray(precise(document, binary, animation["sourceSamples"],
                                    (animation["frameCount"], len(bones), 7))).reshape(animation["frameCount"], len(bones), 7)
        cursor = Cursor(block["data"])
        cursor.string()
        cursor.string()
        frames, fps, flags, mask, count = cursor.number(), cursor.number("f"), cursor.number(), cursor.number("i"), cursor.number()
        if (frames != animation["frameCount"] or fps != animation["fps"] or flags != seq["flags"]
                or mask != -1 or count != len(bones)):
            return False
        seen = set()
        for _ in range(count):
            bone, position, rotation = cursor.number(), cursor.number("B"), cursor.number("B")
            if bone in seen or not 0 <= bone < len(bones) or not position or not rotation:
                return False
            seen.add(bone)
            expected_p = (samples[:, bone, :3] * (2.54, -2.54, 2.54)).astype(np.float32).reshape(-1)
            expected_q = (samples[:, bone, 3:] * (-1., 1., -1., 1.)).astype(np.float32).reshape(-1)
            if not np.array_equal(cursor.floats(3 * frames), expected_p) or not np.array_equal(cursor.floats(4 * frames), expected_q):
                return False
        if cursor.at != len(block["data"]):
            return False
    return bool(blocks)


def compare_legacy_projection(legacy, staged, *, clip_mode="full", source_path=None, source_sha=None):
    before, after = sections(legacy), sections(staged)
    additions = []
    if clip_mode not in ("full", "rest", "required"):
        raise SkeletalDiffError(f"unknown legacy clip selection mode: {clip_mode}")
    if clip_mode in ("rest", "required"):
        after, extra = project_clip_subset(before, after)
        if extra:
            additions.append({"class": "additional-source-clips", "legacyMode": clip_mode, "labels": extra})
    elif source_path is not None:
        old_names = {b["name"] for b in clip_blocks(before.get("ANIM", b"\0\0\0\0"))}
        new_blocks = clip_blocks(after.get("ANIM", b"\0\0\0\0"))
        extras = [b for b in new_blocks if b["name"] not in old_names]
        if extras and _direct_source_clips_match(source_path, source_sha, extras):
            after, extra = project_clip_subset(before, after)
            additions.append({"class": "additional-declared-source-clips", "labels": extra})
    needs_normals = ("MESH" in before and "MESH" in after and
                     not np.array_equal(_mesh_fields(before["MESH"])["normal"], _mesh_fields(after["MESH"])["normal"]))
    needs_morphs = "MORF" not in before and "MORF" in after
    if source_path is not None and (needs_normals or needs_morphs):
        source = source_geometry(source_path, source_sha)
        mesh = _mesh_fields(after["MESH"])
        geometry_agrees = (np.array_equal(mesh["position"], source["positions"])
                           and mesh["indices"] == source["indices"]
                           and [mesh[f"{i}.name"] for i in range(mesh["slots"])] == source["slots"])
        if geometry_agrees and needs_normals:
            old = _mesh_fields(before["MESH"])
            changed = np.any(mesh["normal"] != old["normal"], axis=1)
            if (np.array_equal(mesh["normal"], source["normals"])
                    and np.all(np.isin(source["types"][changed], (1, 2)))
                    and np.allclose(old["normal"][changed], _legacy_geometric_normals(source)[changed], rtol=0., atol=1e-6)):
                after["MESH"] = _replace_normals(after["MESH"], old["normal"])
                additions.append({"class": "restored-authored-compact-normals", "vertices": int(changed.sum())})
        if geometry_agrees and needs_morphs:
            morph = {key: value for key, value, _ in records("MORF", after["MORF"])}
            agrees = morph["count"] == len(source["morphs"])
            for i, (name, deltas) in enumerate(source["morphs"]):
                keys = sorted(deltas)
                values = np.asarray([deltas[k] for k in keys], dtype=np.float32).reshape(-1, 6)
                agrees = agrees and (morph.get(f"{i}.name") == name
                    and np.array_equal(morph.get(f"{i}.vertex"), keys)
                    and np.array_equal(morph.get(f"{i}.position"), values[:, :3])
                    and np.array_equal(morph.get(f"{i}.normal"), values[:, 3:]))
            if agrees:
                after.pop("MORF")
                additions.append({"class": "restored-source-morphs", "targets": len(source["morphs"])})
    result = compare_payloads(legacy, assemble(after))
    result.divergences.extend(additions)
    return result
