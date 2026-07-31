"""Validate VtMB character skeleton/animation bytes through the generated glTF seam.

This is deliberately stricter than the Unreal content tests.  Those tests prove that every
generated GLB is structurally loadable and numerically finite; this audit independently reads
the user's patch-first VtMB install and compares the source values with the generated nodes,
inverse binds, skin weights, and every animation sample.

It also inventories source semantics which the current first-pass exporter does not consume
(procedural bones, blend grids, root motion, and events), and audits included-model donor/target
bind differences.  Those are reported as fidelity gaps, not malformed generated files.

Internal validation library module; not a public project-tooling entrypoint.
"""
from __future__ import annotations

import argparse
from collections import Counter
import json
import math
from pathlib import Path
import struct
import sys
import time

import numpy as np

from elysium_pipeline.formats import install, mdl
from elysium_pipeline.formats import mdl_skel as S
from elysium_pipeline.paths import export_root


NPC_DIR = export_root() / "npc"
MANIFEST = NPC_DIR / "npc_manifest.json"
SCALE = 0.0254
GLB_MAGIC = 0x46546C67
JSON_CHUNK = 0x4E4F534A
BIN_CHUNK = 0x004E4942
COMPONENTS = {
    5120: ("i1", 1),
    5121: ("u1", 1),
    5122: ("<i2", 2),
    5123: ("<u2", 2),
    5125: ("<u4", 4),
    5126: ("<f4", 4),
}
WIDTH = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def _i32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<i", data, offset)[0]


def _load_glb(path: Path) -> tuple[dict, memoryview]:
    data = path.read_bytes()
    magic, version, total = struct.unpack_from("<III", data, 0)
    if magic != GLB_MAGIC or version != 2 or total != len(data):
        raise ValueError(f"invalid GLB header: {path}")
    offset = 12
    json_length, json_kind = struct.unpack_from("<II", data, offset)
    offset += 8
    if json_kind != JSON_CHUNK:
        raise ValueError(f"missing GLB JSON chunk: {path}")
    root = json.loads(data[offset:offset + json_length])
    offset += json_length
    bin_length, bin_kind = struct.unpack_from("<II", data, offset)
    offset += 8
    if bin_kind != BIN_CHUNK:
        raise ValueError(f"missing GLB BIN chunk: {path}")
    return root, memoryview(data)[offset:offset + bin_length]


def _accessor(root: dict, binary: memoryview, index: int) -> np.ndarray:
    acc = root["accessors"][index]
    view = root["bufferViews"][acc["bufferView"]]
    dtype, size = COMPONENTS[acc["componentType"]]
    width = WIDTH[acc["type"]]
    stride = view.get("byteStride", width * size)
    start = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    if stride == width * size:
        return np.frombuffer(binary, dtype=dtype, count=acc["count"] * width,
                             offset=start).reshape(acc["count"], width)
    rows = [
        np.frombuffer(binary, dtype=dtype, count=width, offset=start + row * stride)
        for row in range(acc["count"])
    ]
    return np.stack(rows)


def _source_quat_to_gltf(quat: np.ndarray) -> np.ndarray:
    """The independent closed form of M R M^-1 for M:(x,y,z)->(x,z,-y)."""
    return quat[..., (0, 2, 1, 3)] * np.array((1.0, 1.0, -1.0, 1.0))


def _source_pos_to_gltf(pos: np.ndarray) -> np.ndarray:
    return pos[..., (0, 2, 1)] * np.array((SCALE, SCALE, -SCALE))


def _quat_matrix(quat, pos) -> np.ndarray:
    x, y, z, w = quat
    result = np.eye(4, dtype=np.float64)
    result[:3, :3] = (
        (1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)),
        (2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)),
        (2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)),
    )
    result[:3, 3] = pos
    return result


def _quat_distance(a: np.ndarray, b: np.ndarray) -> float:
    a = a / np.maximum(np.linalg.norm(a, axis=-1, keepdims=True), 1e-30)
    b = b / np.maximum(np.linalg.norm(b, axis=-1, keepdims=True), 1e-30)
    dots = np.abs(np.sum(a * b, axis=-1))
    return float(np.max(1.0 - np.clip(dots, 0.0, 1.0)))


def _quat_left_multiply(left, right: np.ndarray) -> np.ndarray:
    """Hamilton product `left * right` for one xyzw quat and an Nx4 xyzw array."""
    lx, ly, lz, lw = left
    rx, ry, rz, rw = np.moveaxis(right, -1, 0)
    return np.stack((
        lw * rx + lx * rw + ly * rz - lz * ry,
        lw * ry - lx * rz + ly * rw + lz * rx,
        lw * rz + lx * ry - ly * rx + lz * rw,
        lw * rw - lx * rx - ly * ry - lz * rz,
    ), axis=-1)


def _quat_angle_degrees(a, b) -> float:
    a = np.asarray(a, dtype=np.float64)
    b = np.asarray(b, dtype=np.float64)
    a /= np.linalg.norm(a) or 1.0
    b /= np.linalg.norm(b) or 1.0
    return math.degrees(2.0 * math.acos(min(1.0, abs(float(np.dot(a, b))))))


def _rle(data: bytes, offset: int, frames: int) -> np.ndarray:
    out = np.empty(frames, dtype=np.float64)
    written = 0
    while written < frames:
        if offset + 2 > len(data):
            raise ValueError("RLE header outside MDL")
        valid, total = data[offset], data[offset + 1]
        offset += 2
        if not 1 <= valid <= total:
            raise ValueError(f"invalid RLE run valid={valid} total={total}")
        end = offset + valid * 2
        if end > len(data):
            raise ValueError("RLE keys outside MDL")
        keys = np.frombuffer(data, dtype="<i2", count=valid, offset=offset)
        offset = end
        count = min(total, frames - written)
        explicit = min(valid, count)
        out[written:written + explicit] = keys[:explicit]
        if count > explicit:
            out[written + explicit:written + count] = keys[-1]
        written += count
    return out


def _validate_rle(data: bytes, offset: int, frames: int):
    """Bounds/shape check without allocating the decoded channel."""
    covered = 0
    while covered < frames:
        if offset + 2 > len(data):
            raise ValueError("RLE header outside MDL")
        valid, total = data[offset], data[offset + 1]
        offset += 2
        if not 1 <= valid <= total:
            raise ValueError(f"invalid RLE run valid={valid} total={total}")
        offset += valid * 2
        if offset > len(data):
            raise ValueError("RLE keys outside MDL")
        covered += total


def _source_tracks(data: bytes, bones: list[S.Bone], sequence) -> dict[tuple[int, str], np.ndarray]:
    records = sequence.base + _i32(data, sequence.base + 48)
    frames = sequence.frames
    tracks = {}
    for bone in bones:
        record = records + bone.index * 32
        offsets = struct.unpack_from("<7i", data, record + 4)
        if any(offsets[3:]):
            values = np.tile(np.asarray(bone.quat, dtype=np.float64), (frames, 1))
            for component in range(4):
                if offsets[3 + component]:
                    values[:, component] = (
                        _rle(data, record + offsets[3 + component], frames)
                        * bone.rotscale[component]
                    )
            values /= np.maximum(np.linalg.norm(values, axis=1, keepdims=True), 1e-30)
            tracks[(bone.index, "rotation")] = _source_quat_to_gltf(values)
        if any(offsets[:3]):
            values = np.tile(np.asarray(bone.pos, dtype=np.float64), (frames, 1))
            for component in range(3):
                if offsets[component]:
                    values[:, component] += (
                        _rle(data, record + offsets[component], frames)
                        * bone.posscale[component]
                    )
            tracks[(bone.index, "translation")] = _source_pos_to_gltf(values)
    return tracks


class Audit:
    def __init__(self):
        self.counts = Counter()
        self.errors: list[str] = []
        self.anomalies: list[dict] = []
        self.max_errors = Counter()
        self.legacy_split_assets: set[str] = set()

    def error(self, message: str):
        self.errors.append(message)

    def source_inventory(self, index: dict):
        keys = sorted(
            key for key in index
            if key.startswith("models/character/") and key.endswith(".mdl")
        )
        for key in keys:
            data = install.read(index, key)
            try:
                bones = S.read_bones(data)
            except Exception as exc:
                self.error(f"{key}: bone decode failed: {exc}")
                continue
            if len(bones) <= 5:
                continue
            self.counts["source_models"] += 1
            self.counts["source_bones"] += len(bones)
            anomaly_start = len(self.anomalies)
            bind_world = []
            for bone in bones:
                if bone.parent < -1 or bone.parent >= bone.index:
                    self.error(f"{key}: bone {bone.index} has invalid parent {bone.parent}")
                    continue
                quat_norm = math.sqrt(sum(value * value for value in bone.quat))
                self.max_errors["source_bind_quat_norm"] = max(
                    self.max_errors["source_bind_quat_norm"], abs(quat_norm - 1.0)
                )
                if not math.isfinite(quat_norm) or not 0.9 <= quat_norm <= 1.1:
                    self.error(f"{key}: bone {bone.index} has invalid bind quaternion")
                local = _quat_matrix(bone.quat, bone.pos)
                world = local if bone.parent < 0 else bind_world[bone.parent] @ local
                bind_world.append(world)
                pose_to_bone = np.eye(4)
                pose_to_bone[:3, :] = np.asarray(bone.pose_to_bone).reshape(3, 4)
                inverse_error = float(np.max(np.abs(world @ pose_to_bone - np.eye(4))))
                self.max_errors["source_inverse_bind"] = max(
                    self.max_errors["source_inverse_bind"], inverse_error
                )
                if inverse_error > 1e-3:
                    self.anomalies.append({
                        "kind": "source_pose_to_bone_not_conventional",
                        "model": key,
                        "bone": bone.name,
                        "bone_index": bone.index,
                        "max_identity_error": inverse_error,
                    })
                if bone.flags & 0x2:
                    self.counts["source_split_bones"] += 1
                    self.max_errors["source_split_inverse_bind"] = max(
                        self.max_errors["source_split_inverse_bind"], inverse_error
                    )
                    if bone.name != "Bip01 Spine1":
                        self.error(f"{key}: unexpected Flags&2 bone {bone.name!r}")
                    if inverse_error > 1e-3:
                        self.error(f"{key}: split bone {bone.name!r} has nonstandard inverse bind")
                bone_base = _i32(data, 244) + bone.index * 160
                if _i32(data, bone_base + 140):
                    self.counts["source_procedural_bones"] += 1

            anim_count, anim_base = _i32(data, 264), _i32(data, 268)
            self.counts["source_animdescs"] += anim_count
            for anim_index in range(anim_count):
                anim = anim_base + anim_index * 72
                frames = _i32(data, anim + 12)
                movements = _i32(data, anim + 16)
                ik_rules = _i32(data, anim + 52)
                self.counts["source_movement_anims"] += int(movements > 0)
                self.counts["source_movement_records"] += max(0, movements)
                self.counts["source_ik_anims"] += int(ik_rules > 0)
                self.counts["source_ik_rules"] += max(0, ik_rules)
                records = anim + _i32(data, anim + 48)
                if frames <= 0 or records < 0 or records + len(bones) * 32 > len(data):
                    self.error(f"{key}: animdesc {anim_index} has invalid records/frames")
                    continue
                for bone in bones:
                    record = records + bone.index * 32
                    for channel, relative in enumerate(struct.unpack_from("<7i", data, record + 4)):
                        if relative:
                            try:
                                _validate_rle(data, record + relative, frames)
                            except Exception as exc:
                                self.error(
                                    f"{key}: anim {anim_index} bone {bone.index} "
                                    f"channel {channel}: {exc}"
                                )

            sequence_count, sequence_base = _i32(data, 272), _i32(data, 276)
            self.counts["source_sequences"] += sequence_count
            for sequence_index in range(sequence_count):
                sequence = sequence_base + sequence_index * 764
                blends = _i32(data, sequence + 52)
                events = _i32(data, sequence + 20)
                self.counts["source_multiblend_sequences"] += int(blends > 1)
                self.counts["source_event_sequences"] += int(events > 0)
                self.counts["source_events"] += max(0, events)
                self.counts["source_max_blends"] = max(
                    self.counts["source_max_blends"], blends
                )

            pair = mdl.load(index, key)
            weighted_bones = set()
            if pair:
                try:
                    surfaces = S.decode_skinned(*pair)
                    for surface in surfaces.values():
                        self.counts["source_weighted_vertices"] += len(surface["pos"])
                        self.counts["source_triangles"] += len(surface["tris"])
                        for joints, weights in zip(surface["joints"], surface["weights"]):
                            weighted_bones.update(
                                joint for joint, weight in zip(joints, weights) if weight > 0
                            )
                            if (
                                any(joint < 0 or joint >= len(bones) for joint in joints)
                                or abs(sum(weights) - 1.0) > 1e-6
                                or any(not math.isfinite(weight) or weight < 0 for weight in weights)
                            ):
                                self.error(f"{key}: invalid decoded skin influence")
                except Exception as exc:
                    self.error(f"{key}: skin decode failed: {exc}")
            for anomaly in self.anomalies[anomaly_start:]:
                anomaly["weighted"] = anomaly["bone_index"] in weighted_bones

    def _expected_bones(self, bones: list[S.Bone], cinematic_root: str | None):
        if not cinematic_root:
            return bones, {bone.index: bone.index for bone in bones}
        low = cinematic_root.lower()
        subset = [
            bone for bone in bones
            if (bone.name.split()[0] if bone.name.split() else bone.name).lower() == low
        ]
        return subset, {bone.index: index for index, bone in enumerate(subset)}

    def generated_asset(
        self,
        index: dict,
        glb_path: Path,
        model_key: str,
        mesh: bool,
        cinematic_root: str | None = None,
        split_metadata: list[str] | None = None,
    ):
        try:
            root, binary = _load_glb(glb_path)
            data = install.read(index, model_key)
            if data is None:
                raise ValueError(f"source model missing: {model_key}")
            bones = S.read_bones(data)
            expected_bones, old_to_new = self._expected_bones(bones, cinematic_root)
            if len(root.get("nodes", [])) < len(expected_bones):
                raise ValueError("fewer GLB nodes than source bones")

            expected_names = []
            for new_index, bone in enumerate(expected_bones):
                name = bone.name
                if cinematic_root and name[:len(cinematic_root)].lower() == cinematic_root.lower():
                    name = "Bip01" + name[len(cinematic_root):]
                expected_names.append(name)
                node = root["nodes"][new_index]
                if node.get("name") != name:
                    self.error(f"{glb_path.name}: node {new_index} name mismatch")
                expected_pos = _source_pos_to_gltf(np.asarray(bone.pos))
                actual_pos = np.asarray(node.get("translation", (0, 0, 0)), dtype=np.float64)
                pos_error = float(np.max(np.abs(actual_pos - expected_pos)))
                self.max_errors["generated_bind_position"] = max(
                    self.max_errors["generated_bind_position"], pos_error
                )
                expected_quat = _source_quat_to_gltf(np.asarray(bone.quat))
                actual_quat = np.asarray(node.get("rotation", (0, 0, 0, 1)), dtype=np.float64)
                quat_error = _quat_distance(actual_quat[None, :], expected_quat[None, :])
                self.max_errors["generated_bind_rotation"] = max(
                    self.max_errors["generated_bind_rotation"], quat_error
                )
                if pos_error > 2e-6 or quat_error > 2e-6:
                    self.error(f"{glb_path.name}: node {name!r} bind transform mismatch")

            expected_split = [bone.name for bone in bones if bone.flags & 0x2]
            if split_metadata is not None and split_metadata != expected_split:
                self.error(f"{glb_path.name}: split_bones metadata differs from source flags")

            if mesh:
                pair = mdl.load(index, model_key)
                if pair is None:
                    raise ValueError("mesh source lacks mdl/dx80.vtx pair")
                surfaces = S.decode_skinned(*pair)
                primitives = root["meshes"][0]["primitives"]
                if len(primitives) != len(surfaces):
                    self.error(f"{glb_path.name}: primitive/material surface count mismatch")
                for primitive, surface in zip(primitives, surfaces.values()):
                    attrs = primitive["attributes"]
                    actual_pos = _accessor(root, binary, attrs["POSITION"]).astype(np.float64)
                    expected_pos = _source_pos_to_gltf(np.asarray(surface["pos"]))
                    actual_joints = _accessor(root, binary, attrs["JOINTS_0"])
                    expected_joints = np.asarray(
                        [(joints + [0, 0, 0, 0])[:4] for joints in surface["joints"]]
                    )
                    actual_weights = _accessor(root, binary, attrs["WEIGHTS_0"]).astype(np.float64)
                    expected_weights = np.asarray(
                        [(weights + [0.0, 0.0, 0.0, 0.0])[:4]
                         for weights in surface["weights"]]
                    )
                    if (
                        actual_pos.shape != expected_pos.shape
                        or np.max(np.abs(actual_pos - expected_pos), initial=0.0) > 2e-6
                        or not np.array_equal(actual_joints, expected_joints)
                        or np.max(np.abs(actual_weights - expected_weights), initial=0.0) > 2e-6
                    ):
                        self.error(f"{glb_path.name}: mesh skin/position payload differs from source")
                    self.counts["generated_weighted_vertices_compared"] += len(actual_pos)

                skin = root["skins"][0]
                actual_ibm = _accessor(root, binary, skin["inverseBindMatrices"]).astype(np.float64)
                bind_world = []
                expected_ibm = []
                for bone in bones:
                    local = _quat_matrix(
                        _source_quat_to_gltf(np.asarray(bone.quat)),
                        _source_pos_to_gltf(np.asarray(bone.pos)),
                    )
                    world = local if bone.parent < 0 else bind_world[bone.parent] @ local
                    bind_world.append(world)
                    expected_ibm.append(np.linalg.inv(world).T.reshape(16))
                ibm_error = float(np.max(np.abs(actual_ibm - np.asarray(expected_ibm))))
                self.max_errors["generated_inverse_bind"] = max(
                    self.max_errors["generated_inverse_bind"], ibm_error
                )
                if ibm_error > 2e-5:
                    self.error(f"{glb_path.name}: inverse binds differ from converted source FK")

            source_sequences = {
                sequence.label.lstrip("@").lower(): sequence
                for sequence in S.local_sequences(data)
            }
            expected_node_by_old = old_to_new
            for animation in root.get("animations", []):
                label = animation.get("name", "").lower()
                sequence = source_sequences.get(label)
                if sequence is None:
                    self.error(f"{glb_path.name}: animation {label!r} absent from source model")
                    continue
                expected_tracks = {
                    (expected_node_by_old[old_index], path): values
                    for (old_index, path), values in _source_tracks(data, bones, sequence).items()
                    if old_index in expected_node_by_old
                }
                actual_tracks = {}
                for channel in animation["channels"]:
                    sampler = animation["samplers"][channel["sampler"]]
                    node = channel["target"]["node"]
                    path = channel["target"]["path"]
                    times = _accessor(root, binary, sampler["input"])[:, 0].astype(np.float64)
                    expected_times = np.arange(sequence.frames, dtype=np.float64) / (
                        sequence.fps or 30.0
                    )
                    time_error = float(np.max(np.abs(times - expected_times)))
                    self.max_errors["generated_timeline"] = max(
                        self.max_errors["generated_timeline"], time_error
                    )
                    if times.shape != expected_times.shape or time_error > 2e-5:
                        self.error(f"{glb_path.name}:{label}: timeline differs from source FPS")
                    actual_tracks[(node, path)] = _accessor(
                        root, binary, sampler["output"]
                    ).astype(np.float64)
                if set(actual_tracks) != set(expected_tracks):
                    self.error(f"{glb_path.name}:{label}: emitted channel set differs from source")
                    continue
                for key, expected in expected_tracks.items():
                    actual = actual_tracks[key]
                    if actual.shape != expected.shape:
                        self.error(f"{glb_path.name}:{label}:{key}: sample count mismatch")
                        continue
                    if key[1] == "rotation":
                        error = _quat_distance(actual, expected)
                        self.max_errors["generated_animation_rotation"] = max(
                            self.max_errors["generated_animation_rotation"], error
                        )
                        bad = error > 2e-6
                        if bad:
                            old_bone_index = next(
                                old for old, new in expected_node_by_old.items() if new == key[0]
                            )
                            # A discarded exporter experiment left this exact fixed transform in
                            # some local generated files.  Classify it explicitly so the report
                            # says "stale output", rather than printing thousands of anonymous
                            # quaternion mismatches.
                            legacy = _quat_left_multiply(
                                np.array((0.5, 0.5, -0.5, 0.5)), expected
                            )
                            if (
                                bones[old_bone_index].flags & 0x2
                                and _quat_distance(actual, legacy) <= 2e-6
                            ):
                                self.counts["generated_legacy_split_rotation_tracks"] += 1
                                self.counts["generated_legacy_split_rotation_samples"] += len(actual)
                                self.legacy_split_assets.add(glb_path.name)
                                bad = False
                    else:
                        error = float(np.max(np.abs(actual - expected), initial=0.0))
                        self.max_errors["generated_animation_position"] = max(
                            self.max_errors["generated_animation_position"], error
                        )
                        bad = error > 2e-6
                    if bad:
                        self.error(f"{glb_path.name}:{label}:{key}: values differ from source")
                    self.counts["generated_animation_samples_compared"] += len(actual)

            self.counts["generated_glbs_compared"] += 1
            self.counts["generated_bones_compared"] += len(expected_bones)
        except Exception as exc:
            self.error(f"{glb_path}: validation failed: {exc}")

    def included_bind_audit(self, index: dict, manifest: dict, cinematic_stems: set[str]):
        model_cache = {}

        def bones_for(model_key):
            if model_key not in model_cache:
                data = install.read(index, model_key)
                bones = S.read_bones(data)
                model_cache[model_key] = (data, bones, {bone.name.lower(): bone for bone in bones})
            return model_cache[model_key]

        for target_stem, target_record in manifest["npcs"].items():
            target_data, target_bones, target_by_name = bones_for(target_record["model"])
            owners = set(target_record.get("clips", {}).values()) - {target_stem}
            for owner in owners:
                if owner in cinematic_stems or owner not in manifest["banks"]:
                    continue
                owner_record = manifest["banks"][owner]
                owner_data, owner_bones, owner_by_name = bones_for(owner_record["model"])
                shared = set(target_by_name) & set(owner_by_name)
                if not shared:
                    self.error(f"{target_stem}/{owner}: included model has no shared bones")
                    continue
                self.counts["included_target_owner_pairs"] += 1
                pair_diff = False
                parent_diff = False
                differing_pos = set()
                differing_rot = set()
                for name in shared:
                    target = target_by_name[name]
                    donor = owner_by_name[name]
                    position_error = max(abs(a - b) for a, b in zip(target.pos, donor.pos))
                    rotation_error = _quat_angle_degrees(target.quat, donor.quat)
                    if position_error > 1e-4:
                        differing_pos.add(donor.index)
                    if rotation_error > 1e-3:
                        differing_rot.add(donor.index)
                    pair_diff |= position_error > 1e-4 or rotation_error > 1e-3
                    target_parent = (
                        target_bones[target.parent].name.lower() if target.parent >= 0 else ""
                    )
                    donor_parent = (
                        owner_bones[donor.parent].name.lower() if donor.parent >= 0 else ""
                    )
                    parent_diff |= target_parent != donor_parent
                    self.max_errors["included_bind_position_inches"] = max(
                        self.max_errors["included_bind_position_inches"], position_error
                    )
                    self.max_errors["included_bind_rotation_degrees"] = max(
                        self.max_errors["included_bind_rotation_degrees"], rotation_error
                    )
                self.counts["included_pairs_with_bind_difference"] += int(pair_diff)
                self.counts["included_pairs_with_parent_difference"] += int(parent_diff)

                used_labels = {
                    label.lstrip("@").lower()
                    for label, clip_owner in target_record.get("clips", {}).items()
                    if clip_owner == owner
                }
                sparse_mismatch = False
                for sequence in S.local_sequences(owner_data):
                    if sequence.label.lstrip("@").lower() not in used_labels:
                        continue
                    records = sequence.base + _i32(owner_data, sequence.base + 48)
                    for bone_index in differing_pos | differing_rot:
                        offsets = struct.unpack_from(
                            "<7i", owner_data, records + bone_index * 32 + 4
                        )
                        if (
                            bone_index in differing_pos and not any(offsets[:3])
                            or bone_index in differing_rot and not any(offsets[3:])
                        ):
                            sparse_mismatch = True
                            break
                    if sparse_mismatch:
                        break
                self.counts["included_pairs_with_sparse_bind_fallback_mismatch"] += int(
                    sparse_mismatch
                )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, default=MANIFEST)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()

    started = time.time()
    if not args.manifest.is_file():
        raise SystemExit(f"missing NPC manifest: {args.manifest}; run pipeline/src/elysium_pipeline/exporters/npc_export.py")
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    index = install.build_index()
    audit = Audit()

    print("[1/3] source character inventory and byte contracts", flush=True)
    audit.source_inventory(index)

    cinematic_roots = {}
    for record in manifest.get("cinematics", {}).values():
        for root in record.get("roots", []):
            cinematic_roots[root["bank"]] = root["root"]
    cinematic_stems = set(cinematic_roots)

    print("[2/3] generated mesh, bind, skin, and animation values", flush=True)
    for stem, record in manifest.get("npcs", {}).items():
        audit.generated_asset(
            index,
            args.manifest.parent / record["glb"],
            record["model"],
            mesh=True,
            split_metadata=record.get("split_bones", []),
        )
    for stem, record in manifest.get("animated_props", {}).items():
        audit.generated_asset(
            index,
            args.manifest.parent / record["glb"],
            record["model"],
            mesh=True,
            split_metadata=record.get("split_bones", []),
        )
    for stem, record in manifest.get("banks", {}).items():
        audit.generated_asset(
            index,
            args.manifest.parent / record["glb"],
            record["model"],
            mesh=False,
            cinematic_root=cinematic_roots.get(stem),
        )

    print("[3/3] included-model target/donor bind contract", flush=True)
    audit.included_bind_audit(index, manifest, cinematic_stems)
    if audit.legacy_split_assets:
        audit.counts["generated_legacy_split_assets"] = len(audit.legacy_split_assets)
        audit.error(
            f"{len(audit.legacy_split_assets)} generated GLBs contain "
            f"{audit.counts['generated_legacy_split_rotation_tracks']} stale Flags&2 tracks "
            "left-multiplied by quaternion (0.5,0.5,-0.5,0.5); regenerate with the current exporter"
        )

    unsupported = {
        "procedural_bones": audit.counts["source_procedural_bones"],
        "multi_blend_sequences": audit.counts["source_multiblend_sequences"],
        "movement_anims": audit.counts["source_movement_anims"],
        "event_sequences": audit.counts["source_event_sequences"],
        "split_inheritance_bones": audit.counts["source_split_bones"],
        "included_sparse_bind_mismatch_pairs":
            audit.counts["included_pairs_with_sparse_bind_fallback_mismatch"],
    }
    report = {
        "source_to_generated_contract_pass": not audit.errors,
        "retail_semantics_complete": not any(unsupported.values()),
        "elapsed_seconds": round(time.time() - started, 3),
        "counts": dict(sorted(audit.counts.items())),
        "max_errors": dict(sorted(audit.max_errors.items())),
        "unsupported_or_unapplied_semantics": unsupported,
        "source_anomalies": audit.anomalies,
        "errors": audit.errors,
    }
    print(json.dumps(report, indent=2))
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(f"report: {args.report}")
    return 0 if not audit.errors else 1


if __name__ == "__main__":
    raise SystemExit(main())
