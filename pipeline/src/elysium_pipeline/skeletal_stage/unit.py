"""Decoded skeletal records from a published model GLB; no install access."""
from __future__ import annotations

import json
import hashlib
from pathlib import Path
import struct

import numpy as np

from elysium_pipeline.formats import mdl_skel as records
from elysium_pipeline.formats.model_glb.model import MODEL_EXTENSION, SCHEMA_VERSION
from elysium_pipeline.formats.unit_contract.container import decode_glb


class SkeletalUnitError(ValueError):
    pass


def read_document(path: Path):
    """Read only the JSON chunk for inventory and include-closure walks."""
    with Path(path).open("rb") as stream:
        header = stream.read(20)
        if len(header) != 20:
            raise SkeletalUnitError(f"{path}: truncated GLB header")
        magic, version, size, length, kind = struct.unpack("<5I", header)
        if magic != 0x46546C67 or version != 2 or kind != 0x4E4F534A or size != path.stat().st_size:
            raise SkeletalUnitError(f"{path}: invalid GLB header")
        if length % 4 or length + 20 > size:
            raise SkeletalUnitError(f"{path}: invalid JSON chunk length")
        return json.loads(stream.read(length))


def source_position(value):
    x, y, z = map(float, value)
    return x / 0.0254, -z / 0.0254, y / 0.0254


def source_direction(value):
    x, y, z = map(float, value)
    return x, -z, y


def source_rotation(value):
    x, y, z, w = map(float, value)
    return x, -z, y, w


def typed(kind, value):
    if kind is records.SwingRecord and "unidentified" not in value:
        value = {**value, "unidentified": tuple([
            value["kickOnlyMarker"], *value["candidateCounts"], *value["resolvedKnockbackActivities"]])}
    return kind(**{key: value[key] for key in kind._fields})


class ModelUnit:
    def __init__(self, path: Path):
        self.path = Path(path)
        data = self.path.read_bytes()
        self.digest = hashlib.sha256(data).hexdigest()
        self.document, self.binary = decode_glb(data, self.path)
        self._read_records()

    @classmethod
    def metadata(cls, path, document=None):
        unit = cls.__new__(cls)
        unit.path = Path(path)
        unit.document = document if document is not None else read_document(unit.path)
        unit.binary = None
        unit.digest = None
        unit._read_records()
        return unit

    def _read_records(self):
        self.extension = self.document["extensions"][MODEL_EXTENSION]
        if self.extension["schemaVersion"] != SCHEMA_VERSION:
            raise SkeletalUnitError(f"{self.path}: re-export as model schema {SCHEMA_VERSION}")
        self.id = self.extension["identity"]["asset"]
        if not self.id.startswith("vtmb:model:"):
            raise SkeletalUnitError(f"{self.path}: not a model identity: {self.id}")
        self.mdl = self.extension["mdl"]
        self.bones = []
        for i, row in enumerate(self.mdl["bones"]):
            if row["index"] != i or row["parent"] >= i:
                raise SkeletalUnitError(f"{self.id}: bone order is not parent-first")
            node = self.document["nodes"][i]
            self.bones.append(records.Bone(
                index=i, name=row["name"], parent=row["parent"], flags=row["flags"],
                pos=tuple(float(np.float32(v)) for v in row.get("storedPosition", source_position(node.get("translation", (0, 0, 0))))),
                quat=tuple(float(np.float32(v)) for v in row.get("storedRotation", source_rotation(node.get("rotation", (0, 0, 0, 1))))),
                posscale=row["positionScale"], rotscale=row["rotationScale"],
                pose_to_bone=row["poseToBone"]))
        self.animations = self.mdl["localAnimations"]
        self._animations_by_base = {row["sourceOffset"]: row for row in self.animations}
        self._precise_cache = {}
        self._validated_animations = set()
        if len(self._animations_by_base) != len(self.animations):
            raise SkeletalUnitError(f"{self.id}: duplicate local animation identity")
        self.dropped_sequences = []
        self.sequences = self._sequences()
        self.attachments = []
        nodes = {n.get("extensions", {}).get(MODEL_EXTENSION, {}).get("attachmentIndex"): n
                 for n in self.document.get("nodes", ())
                 if "attachmentIndex" in n.get("extensions", {}).get(MODEL_EXTENSION, {})}
        for i, row in enumerate(self.mdl["attachments"]):
            node = nodes[row.get("index", len(self.bones) + i)]
            self.attachments.append(records.Attachment(
                row["name"], row["flags"], row["bone"],
                tuple(row.get("storedPosition", source_position(node["translation"]))),
                tuple(row.get("storedRotation", source_rotation(node["rotation"])))))

    def precise(self, record, shape):
        from elysium_pipeline.formats.unit_contract.precision import decode
        if self.binary is None:
            raise SkeletalUnitError(f"{self.id}: a metadata-only read cannot supply sampled values")
        index = (record["bufferView"], record.get("encoding"), tuple(record.get("shape", ())), record.get("sha256"))
        if index not in self._precise_cache:
            self._precise_cache[index] = np.asarray(
                decode(self.document, self.binary, record, shape)).reshape(shape)
        result = self._precise_cache[index]
        if result.shape != tuple(shape):
            raise SkeletalUnitError(f"{self.id}: precise source view has conflicting shapes")
        return result

    def accessor(self, index):
        row = self.document["accessors"][index]
        if row.get("sparse") or row.get("normalized"):
            raise SkeletalUnitError(f"{self.id}: unsupported sparse/normalized accessor {index}")
        dtype = np.dtype({5121: "u1", 5123: "<u2", 5125: "<u4", 5126: "<f4"}[row["componentType"]])
        width = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}[row["type"]]
        view = self.document["bufferViews"][row["bufferView"]]
        if view.get("buffer", 0) != 0:
            raise SkeletalUnitError(f"{self.id}: accessor references an external buffer")
        offset = row.get("byteOffset", 0)
        stride = view.get("byteStride", dtype.itemsize * width)
        count = row["count"]
        size = (count - 1) * stride + width * dtype.itemsize if count else 0
        start = view.get("byteOffset", 0) + offset
        if min(offset, count, start) < 0 or stride < width * dtype.itemsize or offset + size > view["byteLength"] or start + size > len(self.binary):
            raise SkeletalUnitError(f"{self.id}: accessor {index} exceeds its buffer view")
        return np.ndarray((count, width), dtype=dtype, buffer=self.binary, offset=start,
                          strides=(stride, dtype.itemsize))

    def local_animation(self, index):
        if not 0 <= index < len(self.animations):
            return None
        row = self.animations[index]
        return row["name"], row["sourceOffset"], row["frameCount"], row["fps"]

    def movements(self, base):
        return tuple(typed(records.Movement, row) for row in self._animations_by_base[base]["movement"])

    def bone_weights(self, base):
        weights = self._animations_by_base[base]["boneWeights"]
        if len(weights) != len(self.bones) or any(w not in (0, 1) for w in weights):
            raise SkeletalUnitError(f"{self.id}: non-binary or incomplete ownership mask at {base}")
        return weights

    def authored_channels(self, base):
        rows = self._animations_by_base[base]["channelOffsets"]
        if len(rows) != len(self.bones) or any(len(row) != 7 for row in rows):
            raise SkeletalUnitError(f"{self.id}: incomplete channel declaration at {base}")
        return [(any(row[:3]), any(row[3:])) for row in rows]

    def animation_frames(self, base, frame_count):
        row = self._animations_by_base[base]
        if not 0 <= frame_count <= row["frameCount"]:
            raise SkeletalUnitError(f"{self.id}: requested {frame_count} frames of {row['frameCount']}")
        weights = self.bone_weights(base)
        frames = [[((0., 0., 0.), (0., 0., 0., 0.)) for _ in self.bones] for _ in range(frame_count)]
        index = row["animation"]
        samples = (self.precise(row["sourceSamples"], (row["frameCount"], len(self.bones), 7))
                   if index is not None else None)
        if samples is not None and base in self._validated_animations:
            return [[(tuple(v[:3]), tuple(v[3:])) for v in f] for f in samples[:frame_count].tolist()]
        channels = [] if index is None else self.document["animations"][index]["channels"]
        seen = set()
        for channel in channels:
            bone, path = channel["target"]["node"], channel["target"]["path"]
            if (bone, path) in seen or path not in ("translation", "rotation") or not 0 <= bone < len(weights) or not weights[bone]:
                raise SkeletalUnitError(f"{self.id}: invalid or unowned channel {(bone, path)}")
            seen.add((bone, path))
            sampler = self.document["animations"][index]["samplers"][channel["sampler"]]
            values = self.accessor(sampler["output"])
            times = self.accessor(sampler["input"])
            if len(values) != row["frameCount"] or len(times) != row["frameCount"] or sampler.get("interpolation", "LINEAR") != "LINEAR":
                raise SkeletalUnitError(f"{self.id}: sample count/interpolation changed")
            expected = np.arange(row["frameCount"], dtype=np.float32) / float(row["fps"] or 30.)
            if not np.array_equal(times[:, 0], expected):
                raise SkeletalUnitError(f"{self.id}: non-uniform authored timeline")
            if path == "translation":
                projected = samples[:, bone, :3][:, (0, 2, 1)] * (0.0254, 0.0254, -0.0254)
                agrees = np.array_equal(values, projected.astype(np.float32))
            else:
                projected = samples[:, bone, 3:][:, (0, 2, 1, 3)] * (1., 1., -1., 1.)
                lengths = np.linalg.norm(projected, axis=1)
                valid = lengths > 1e-12
                projected[valid] /= lengths[valid, None]
                projected[~valid] = (0., 0., 0., 1.)
                agrees = np.allclose(values, projected.astype(np.float32), rtol=0., atol=2e-7)
            if not agrees:
                raise SkeletalUnitError(f"{self.id}: core animation disagrees with precise source values")
            convert = source_position if path == "translation" else source_rotation
            for f, value in enumerate(values[:frame_count]):
                pair = list(frames[f][bone])
                pair[0 if path == "translation" else 1] = convert(tuple(float(x) for x in value))
                frames[f][bone] = tuple(pair)
        expected_channels = {(i, path) for i, w in enumerate(weights) if w
                             for path in ("translation", "rotation")}
        if seen != expected_channels:
            raise SkeletalUnitError(f"{self.id}: owned channels missing: {expected_channels - seen}")
        if samples is not None:
            if any(not w and np.any(samples[:, i]) for i, w in enumerate(weights)):
                raise SkeletalUnitError(f"{self.id}: unowned bone carries precise animation values")
            self._validated_animations.add(base)
            return [[(tuple(v[:3]), tuple(v[3:])) for v in f] for f in samples[:frame_count].tolist()]
        return frames

    def _sequences(self):
        out, seen = [], set()
        for row in self.mdl["sequences"]:
            label = row["label"]
            grid = records.Grid(**{**row["grid"], "cells": tuple(
                typed(records.Cell, cell) for cell in row["grid"]["cells"])})
            base = self.local_animation(grid.cells[0].anim) if grid.cells else None
            reason = "empty-label" if not label else "duplicate-label" if label.lower() in seen else "invalid-base-cell" if base is None else ""
            if reason:
                self.dropped_sequences.append({"index": row["index"], "label": label, "reason": reason})
                continue
            seen.add(label.lower())
            _, offset, frames, fps = base
            out.append(records.Seq(
                label, offset, frames, fps, row["activity"], row["activityWeight"], row["flags"],
                grid=grid, bbmin=row["boundsMin"], bbmax=row["boundsMax"], fade=row["fade"][0],
                autolayers=tuple(layer["label"] for layer in row["autolayers"]),
                events=tuple(typed(records.Event, event) for event in row["events"]),
                reach=row["reach"], low_reach=row["lowReach"], blocked_reaction=row["blockedReaction"],
                swings=tuple(typed(records.SwingRecord, swing) for swing in row["swings"]),
                combo=typed(records.ComboChain, row["combo"]) if row["combo"] else None,
                movement=self.movements(offset), envelopes=tuple(row["envelopes"])))
        return out
