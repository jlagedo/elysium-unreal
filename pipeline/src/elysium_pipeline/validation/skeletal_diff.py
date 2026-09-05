"""Independent, ordered R8 product comparison against the frozen legacy export.

Topology, names, ownership, flags and row order are exact. Only explicitly bounded numerical
round trips and the named retired DYNM/BDYN/albedo products can be classified as divergences.
This reader imports no producer code, so producer bugs cannot rewrite the expected answer.
"""
from __future__ import annotations

from dataclasses import dataclass, field
import hashlib
import itertools
import json
from pathlib import Path
import struct

import numpy as np


class SkeletalDiffError(ValueError):
    pass


@dataclass
class Comparison:
    differences: list[dict] = field(default_factory=list)
    divergences: list[dict] = field(default_factory=list)
    equal_sections: list[str] = field(default_factory=list)

    @property
    def passed(self):
        return not self.differences

    def as_dict(self):
        return {"passed": self.passed, "differences": self.differences,
                "divergences": self.divergences, "equalSections": self.equal_sections}


def sections(blob):
    if len(blob) < 16:
        raise SkeletalDiffError("truncated skeletal header")
    magic, version, count, reserved = struct.unpack_from("<4sIII", blob)
    if magic != b"ESKM" or version != 8 or reserved:
        raise SkeletalDiffError(f"invalid skeletal header: {magic!r}, {version}, {reserved}")
    end = 16 + 20 * count
    if end > len(blob):
        raise SkeletalDiffError("truncated skeletal directory")
    result = {}
    for index in range(count):
        tag, start, size = struct.unpack_from("<4sQQ", blob, 16 + 20 * index)
        name = tag.decode("ascii")
        if name in result or start != end or start + size > len(blob):
            raise SkeletalDiffError(f"duplicate, overlapping or out-of-bounds section: {name}")
        result[name] = memoryview(blob)[start:start + size]
        end = start + size
    if end != len(blob):
        raise SkeletalDiffError("unclaimed bytes after skeletal sections")
    return result


class Cursor:
    def __init__(self, data):
        self.data, self.at = data, 0

    def take(self, size):
        if size < 0 or self.at + size > len(self.data):
            raise SkeletalDiffError(f"section truncated at {self.at}, need {size} bytes")
        value = self.data[self.at:self.at + size]
        self.at += size
        return value

    def number(self, code="I"):
        return struct.unpack("<" + code, self.take(struct.calcsize("<" + code)))[0]

    def string(self):
        return bytes(self.take(self.number())).decode("utf-8")

    def floats(self, count):
        return np.frombuffer(self.take(count * 4), dtype="<f4")


def records(tag, payload):
    """(field, value, allowed absolute round-trip error); zero means exact."""
    c = Cursor(payload)
    count = c.number()
    yield "count", count, 0
    if tag in ("SKEL", "ATCH"):
        for i in range(count):
            yield f"{i}.name", c.string(), 0
            yield f"{i}.parent", c.number("i" if tag == "SKEL" else "I"), 0
            yield f"{i}.position", c.floats(3), 1e-4
            yield f"{i}.rotation", c.floats(4), 1e-6
    elif tag == "MATL":
        for i in range(count):
            yield f"{i}.name", c.string(), 0
            yield f"{i}.albedo", c.string(), 0
    elif tag == "MASK":
        for i in range(count):
            width = c.number()
            yield f"{i}.width", width, 0
            values = bytes(c.take(width))
            if any(v not in (0, 1) for v in values):
                raise SkeletalDiffError("non-binary skeletal mask")
            yield f"{i}.weights", values, 0
    elif tag == "MESH":
        triangles, slots = c.number(), c.number()
        yield "triangles", triangles, 0
        yield "slots", slots, 0
        for i in range(slots):
            yield f"{i}.name", c.string(), 0
            yield f"{i}.start", c.number(), 0
            yield f"{i}.length", c.number(), 0
        dtype = np.dtype([("position", "<f4", 3), ("normal", "<f4", 3), ("uv", "<f4", 2),
                          ("joints", "<u2", 4), ("weights", "<f4", 4)])
        vertices = np.frombuffer(c.take(count * dtype.itemsize), dtype=dtype)
        for name, tolerance in (("position", 1e-4), ("normal", 1e-6), ("uv", 0),
                                ("joints", 0), ("weights", 0)):
            yield name, vertices[name], tolerance
        yield "indices", bytes(c.take(triangles * 12)), 0
    elif tag == "MORF":
        dtype = np.dtype([("vertex", "<u4"), ("position", "<f4", 3), ("normal", "<f4", 3)])
        for i in range(count):
            yield f"{i}.name", c.string(), 0
            size = c.number()
            yield f"{i}.count", size, 0
            rows = np.frombuffer(c.take(size * dtype.itemsize), dtype=dtype)
            for name, tolerance in (("vertex", 0), ("position", 1e-4), ("normal", 1e-6)):
                yield f"{i}.{name}", rows[name], tolerance
    elif tag == "ANIM":
        for i in range(count):
            yield f"{i}.name", c.string(), 0
            yield f"{i}.base", c.string(), 0
            frames = c.number()
            yield f"{i}.frames", frames, 0
            yield f"{i}.fps", c.number("f"), 0
            yield f"{i}.flags", c.number(), 0
            yield f"{i}.mask", c.number("i"), 0
            tracks = c.number()
            yield f"{i}.tracks", tracks, 0
            for j in range(tracks):
                bone, pos, rot = c.number(), c.number("B"), c.number("B")
                if pos not in (0, 1) or rot not in (0, 1):
                    raise SkeletalDiffError("invalid track presence bits")
                yield f"{i}.{j}.header", (bone, pos, rot), 0
                if pos:
                    yield f"{i}.{j}.position", c.floats(frames * 3), 1e-4
                if rot:
                    yield f"{i}.{j}.rotation", c.floats(frames * 4), 1e-6
    else:
        raise SkeletalDiffError(f"no semantic reader for section {tag}")
    if c.at != len(payload):
        raise SkeletalDiffError(f"{tag}: {len(payload) - c.at} unclaimed section bytes")


def compare_payloads(legacy: bytes, staged: bytes) -> Comparison:
    result = Comparison()
    before, after = sections(legacy), sections(staged)
    if [tag for tag in before if tag not in ("DYNM", "BDYN")] != [
            tag for tag in after if tag not in ("DYNM", "BDYN")]:
        result.differences.append({"reason": "section order/presence changed"})
    for tag in dict.fromkeys([*before, *after]):
        if tag in ("DYNM", "BDYN") and tag in before and tag not in after:
            result.divergences.append({"section": tag, "class": "retired-unused-section"})
            continue
        if tag not in before or tag not in after:
            result.differences.append({"section": tag, "reason": "section presence changed"})
            continue
        if before[tag] == after[tag]:
            result.equal_sections.append(tag)
            continue
        try:
            changed_floats = 0
            largest_error = 0.0
            for left, right in itertools.zip_longest(records(tag, before[tag]), records(tag, after[tag])):
                if left is None or right is None or left[0] != right[0]:
                    result.differences.append({"section": tag, "reason": "record order/count changed"})
                    break
                name, a, tolerance = left
                b = right[1]
                if tag == "MATL" and name.endswith(".albedo") and a and b == "":
                    result.divergences.append({"section": tag, "field": name,
                                               "class": "albedo-owned-by-texture-lane"})
                    continue
                if isinstance(a, np.ndarray):
                    equal = a.shape == b.shape and np.array_equal(a, b)
                    if not equal and tolerance and a.shape == b.shape:
                        delta = np.abs(a.astype(np.float64) - b.astype(np.float64))
                        error = float(np.max(delta)) if delta.size else 0.0
                        if np.isfinite(delta).all() and error <= tolerance:
                            changed_floats += int(np.count_nonzero(delta))
                            largest_error = max(largest_error, error)
                            continue
                else:
                    equal = a == b
                if not equal:
                    result.differences.append({"section": tag, "field": name,
                                               "reason": "value changed outside declared tolerance"})
                    break
            if changed_floats:
                result.divergences.append({"section": tag, "class": "float32-basis-roundtrip",
                                           "changedValues": changed_floats, "maxAbsoluteError": largest_error})
        except (ValueError, struct.error, IndexError) as exc:
            result.differences.append({"section": tag, "reason": str(exc)})
    return result


def compare_json(before, after, path="$") -> list[dict]:
    """JSON object order is irrelevant; every array's authored order remains significant."""
    if isinstance(before, dict) and isinstance(after, dict):
        if before.keys() != after.keys():
            return [{"field": path, "reason": "object keys changed"}]
        return [difference for key in before
                for difference in compare_json(before[key], after[key], f"{path}.{key}")]
    if isinstance(before, list) and isinstance(after, list):
        if len(before) != len(after):
            return [{"field": path, "reason": "row count changed"}]
        return [difference for i, (a, b) in enumerate(zip(before, after))
                for difference in compare_json(a, b, f"{path}[{i}]")]
    if type(before) is not type(after) or before != after:
        return [{"field": path, "reason": "value or row order changed"}]
    return []


def compare_trees(legacy_root: Path, staged_root: Path) -> dict:
    """All skeletal/JSON products, with missing AND extra files counted as failures."""
    def inventory(root):
        return {p.relative_to(root).as_posix(): p for p in root.rglob("*")
                if p.is_file() and p.suffix in (".eskm", ".skel", ".json")
                and p.name != "baseline.json"}
    before, after = inventory(legacy_root), inventory(staged_root)
    rows = []
    for key in sorted(before.keys() | after.keys()):
        if key not in before or key not in after:
            rows.append({"path": key, "passed": False, "reason": "missing" if key not in after else "extra"})
        elif key.endswith((".eskm", ".skel")):
            try:
                rows.append({"path": key, **compare_payloads(before[key].read_bytes(), after[key].read_bytes()).as_dict()})
            except (ValueError, struct.error) as exc:
                rows.append({"path": key, "passed": False, "reason": str(exc)})
        else:
            a = json.loads(before[key].read_text(encoding="utf-8-sig"))
            b = json.loads(after[key].read_text(encoding="utf-8-sig"))
            differences = compare_json(a, b)
            rows.append({"path": key, "passed": not differences, "differences": differences[:20]})
    return {"passed": bool(rows) and all(row["passed"] for row in rows),
            "compared": len(rows), "failed": sum(not row["passed"] for row in rows), "products": rows}


def compare_staged_payloads(legacy_root: Path, staging_root: Path, *, export_root: Path | None = None) -> dict:
    """Compare staged unit payloads to the same source models in the frozen legacy manifest.

    This is the payload gate only. It explicitly reports legacy sections absent from the staged
    representation and does not certify sidecar, bank-family or cooked-asset migration.
    """
    manifest = json.loads((staging_root / "manifest.json").read_text(encoding="utf-8"))
    baseline_file = legacy_root / "baseline.json"
    baseline = ({row["path"]: row for row in json.loads(baseline_file.read_text(encoding="utf-8-sig"))["files"]}
                if baseline_file.is_file() else None)
    def read_legacy(path):
        data = path.read_bytes()
        if baseline is not None:
            key = path.relative_to(legacy_root).as_posix()
            row = baseline.get(key)
            if row is None or len(data) != row["size"] or hashlib.sha256(data).hexdigest() != row["sha256"]:
                raise SkeletalDiffError(f"frozen baseline integrity failed: {key}")
        return data
    npc = legacy_root / "npc"
    legacy = json.loads(read_legacy(npc / "npc_manifest.json"))
    actor_banks = {}
    for model, cinematic in legacy["cinematics"].items():
        id = "vtmb:model:" + model.replace("\\", "/").lower().removeprefix("models/").removesuffix(".mdl")
        actor_banks[id] = {row["root"].lower(): npc / "banks" / (row["bank"] + ".eskm")
                           for row in cinematic["roots"] if row.get("root")}
    by_id = {}
    for family, directory in (("npcs", ""), ("banks", "banks"), ("placed_models", "placed_models")):
        for stem, record in legacy[family].items():
            model = record["model"].replace("\\", "/").lower()
            id = "vtmb:model:" + model.removeprefix("models/").removesuffix(".mdl")
            path = npc / record.get("eskm", f"{directory}/{stem}.eskm".lstrip("/"))
            if path.is_file():
                by_id.setdefault(id, []).append((family, path, record))
    wield_path = legacy_root / "items/wield_models.json"
    if wield_path.is_file():
        wield = json.loads(read_legacy(wield_path))
        for record in wield["models"].values():
            id = "vtmb:model:" + record["source"].lower().replace("\\", "/").removeprefix("models/").removesuffix(".mdl")
            path = legacy_root / record["eskm"]
            if path.is_file():
                by_id.setdefault(id, []).append(("wield", path, record))
    rows = []
    for entry in manifest["assets"]:
        id = entry["assetId"]
        staged_bytes = (staging_root / entry["payload"]).read_bytes()
        if hashlib.sha256(staged_bytes).hexdigest() != entry["recipe"]["payloadSha256"]:
            rows.append({"assetId": id, "passed": False, "reason": "staged payload digest disagrees with manifest"})
            continue
        body_bytes = (staging_root / entry["body"]).read_bytes()
        if entry["recipe"].get("bodySha256") and hashlib.sha256(body_bytes).hexdigest() != entry["recipe"]["bodySha256"]:
            rows.append({"assetId": id, "passed": False, "reason": "staged body digest disagrees with manifest"})
            continue
        actors = entry.get("actors", [])
        if actors:
            expected = actor_banks.get(id, {})
            details = []
            actual_roots = {actor["root"].lower() for actor in actors}
            if actual_roots != set(expected):
                details.append({"passed": False, "reason": "cinematic actor root inventory changed",
                                "missing": sorted(set(expected) - actual_roots),
                                "extra": sorted(actual_roots - set(expected))})
            for actor in actors:
                path = expected.get(actor["root"].lower())
                data = (staging_root / actor["payload"]).read_bytes()
                if hashlib.sha256(data).hexdigest() != actor["payloadSha256"]:
                    details.append({"root": actor["root"], "passed": False, "reason": "actor payload digest mismatch"})
                elif path is not None and path.is_file():
                    details.append({"root": actor["root"], "legacy": str(path),
                                    **compare_payloads(read_legacy(path), data).as_dict()})
                else:
                    details.append({"root": actor["root"], "passed": False, "reason": "no legacy actor counterpart"})
            if not entry.get("nativeMainOwner", True):
                rows.append({"assetId": id, "actors": details,
                             "passed": bool(details) and all(row["passed"] for row in details)})
                continue
        choices = by_id.get(id, [])
        preferred = [r for r in choices if r[0] == ("npcs" if entry["meshAsset"] else "banks")]
        choices = preferred or choices
        if not choices:
            if entry.get("aggregateOnly"):
                body = json.loads(body_bytes)
                mdl = body["sourceSemantics"]["mdl"]
                contents = sections(staged_bytes)
                no_clips = "ANIM" not in contents or bytes(contents["ANIM"]) == b"\0\0\0\0"
                if (body["assetId"] == id and mdl["includeModels"] and not mdl["sequences"]
                        and not mdl["localAnimations"] and not entry["meshAsset"]
                        and "MESH" not in contents and no_clips):
                    rows.append({"assetId": id, "passed": True,
                                 "classification": "include-only-aggregator-with-no-native-product"})
                    continue
            rows.append({"assetId": id, "passed": False, "reason": "no legacy counterpart; inventory review required"})
            continue
        from elysium_pipeline.validation.skeletal_projection import compare_legacy_projection
        family, path, record = choices[0]
        try:
            result = compare_legacy_projection(read_legacy(path), staged_bytes,
                clip_mode=record.get("clip_mode", "full"),
                source_path=export_root / entry["unitGlb"] if export_root is not None else None,
                source_sha=entry["recipe"]["unitSha256"])
        except (ValueError, KeyError, IndexError) as exc:
            result = Comparison(differences=[{"reason": str(exc)}])
        rows.append({"assetId": id, "legacy": str(path), "family": family, **result.as_dict()})
        other_wields = [choice for choice in by_id.get(id, ()) if choice[0] == "wield" and choice[1] != path]
        if other_wields:
            rows[-1]["wield"] = [{"legacy": str(other_path), **compare_payloads(read_legacy(other_path), staged_bytes).as_dict()}
                                  for _, other_path, _ in other_wields]
            rows[-1]["passed"] = rows[-1]["passed"] and all(x["passed"] for x in rows[-1]["wield"])
        if actors:
            rows[-1]["actors"] = details
            rows[-1]["passed"] = rows[-1]["passed"] and all(row["passed"] for row in details)
    return {"scope": "staged-payloads-only", "sidecarsCompared": False, "frozenBaselineVerified": baseline is not None,
            "passed": bool(rows) and not manifest["stageFailures"] and all(row["passed"] for row in rows),
            "compared": len(rows), "failed": sum(not row["passed"] for row in rows), "products": rows}
