"""Reconcile stale A_ declarations against the existing native publication contract.

Only owners of missing declarations are read, sequentially, with no track decoding.
This is downstream validation: it neither stages producers nor edits their manifests.
"""
from copy import deepcopy
import hashlib
import json
from pathlib import Path

from elysium_pipeline.asset_paths import baked_unit
from elysium_pipeline.formats import eskm


def is_native_clip(clip):
    """Match ElysiumSkeletalBuild/import_characters: raw deltas remain products."""
    return not (clip.flags & 4 and clip.base)


def _read(root, relative, expected, inputs, *, limit=64 * 1024 * 1024):
    path = (root / relative).resolve()
    if not path.is_relative_to(root.resolve()):
        raise ValueError("animation evidence escapes character stage: " + relative)
    before = path.stat()
    if before.st_size > limit:
        raise ValueError("animation evidence exceeds bounded read: " + str(path))
    raw = path.read_bytes()
    after = path.stat()
    actual = hashlib.sha256(raw).hexdigest()
    if actual != expected or (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
        raise ValueError("stale animation evidence: " + str(path))
    inputs.append({"kind": "animation-publication-evidence", "path": str(path), "sha256": actual})
    return raw


def _strings(value):
    if isinstance(value, dict):
        for key, item in value.items():
            yield key
            yield from _strings(item)
    elif isinstance(value, list):
        for item in value:
            yield from _strings(item)
    elif isinstance(value, str):
        yield value


def audit_missing(manifest_path, manifest_sha256, missing_packages):
    """A false declaration needs retained ANIM + descriptors + native metadata/reference proof.

    Unmatched or unproved addresses remain required. The exact raw replacement is
    required even if it was accidentally absent from the producer's declaration.
    Source descriptors and native metadata are copied into the packaging root ledger;
    host-composed intermediate samples remain in the hash-checked offline payload.
    """
    path = Path(manifest_path).resolve()
    inputs = []
    manifest = json.loads(_read(path.parent, path.name, manifest_sha256, inputs))
    candidates = set(missing_packages)
    dispositions, required, owners = [], set(), []
    for entry in manifest["assets"]:
        for owner in ([entry] if entry.get("nativeMainOwner", True) else []) + entry.get("actors", []):
            affected = candidates.intersection(owner.get("animationAssets", []))
            if not affected:
                continue
            role, asset_id = owner.get("root", ""), entry["assetId"]
            recipe = entry["recipe"]
            payload_sha = owner.get("payloadSha256", recipe["payloadSha256"])
            blob = _read(path.parent, owner["payload"], payload_sha, inputs, limit=128 * 1024 * 1024)
            clips = eskm.clip_payloads(blob)
            del blob  # Never retain multiple banks or decode numerical animation samples.
            by_path = {baked_unit(asset_id, "A", role=role or None, label=c.name): c for c in clips}
            if len(by_path) != len(clips):
                raise ValueError("ambiguous animation product paths: " + asset_id)
            by_name = {c.name: c for c in clips}
            meta = json.loads(_read(path.parent, owner["clipData"], owner["clipDataSha256"], inputs))
            body = json.loads(_read(path.parent, owner["bodyData"], owner["bodyDataSha256"], inputs))
            source = json.loads(_read(path.parent, entry["body"], recipe["bodySha256"], inputs))
            if any(d.get("assetId") != asset_id for d in (meta, body, source)) or any(
                    d.get("ownerRoot", "") != role for d in (meta, body)):
                raise ValueError("animation evidence owner mismatch: " + asset_id)
            descriptors = {d["label"]: d for d in source["sequences"]}
            authored = source["sourceSemantics"]["mdl"]["sequences"]
            references = set(_strings(body))
            evidence = {"assetId": asset_id, "ownerRoot": role, "payload": owner["payload"],
                        "payloadSha256": payload_sha, "unitGlb": entry["unitGlb"],
                        "unitSha256": recipe["unitSha256"], "body": entry["body"],
                        "bodySha256": recipe["bodySha256"], "clipData": owner["clipData"],
                        "clipDataSha256": owner["clipDataSha256"], "bodyData": owner["bodyData"],
                        "bodyDataSha256": owner["bodyDataSha256"], "stagedRecords": len(clips),
                        "nativeRecords": sum(is_native_clip(c) for c in clips),
                        "sourceDescriptors": {}, "authoredDescriptors": {}, "nativeMetadata": {}}
            for package in sorted(affected):
                clip = by_path.get(package)
                if clip is None or is_native_clip(clip):
                    continue
                raw_name, separator, host = clip.name.rpartition("@")
                raw = by_name.get(raw_name)
                native = meta["clips"].get(raw_name)
                if (not separator or host != clip.base or raw is None or raw.base
                        or raw.flags != clip.flags or not raw.frames or not raw.tracks
                        or native is None or clip.name in meta["clips"]):
                    continue
                raw_package = baked_unit(asset_id, "A", role=role or None, label=raw_name)
                raw_object = raw_package + "." + raw_package.rsplit("/", 1)[-1]
                obj = package + "." + package.rsplit("/", 1)[-1]
                source_name = native["sourceLabel"]
                desc, host_desc = descriptors.get(source_name), descriptors.get(host)
                source_rows = [d for d in authored if d["label"] == source_name]
                if (package in references or obj in references
                        or body["nativeSequences"].get(raw_name) != raw_object
                        or desc is None or host_desc is None or desc["flags"] != raw.flags
                        or not source_rows or any(d["flags"] != raw.flags for d in source_rows)
                        or native["slice"]["clips"][raw_name][0][3] != raw.flags):
                    continue
                required.add(raw_package)
                dispositions.append({"packagePath": package, "assetId": asset_id, "ownerRoot": role,
                    "recordIndex": clips.index(clip), "name": clip.name, "base": clip.base,
                    "flags": clip.flags, "frames": clip.frames, "tracks": clip.tracks,
                    "rawNativePackage": raw_package,
                    "disposition": "host-composed-additive-intermediate",
                    "retention": "complete ANIM record in hashed staged payload; raw native clip and metadata retained"})
                for name, value in ((source_name, desc), (host, host_desc)):
                    evidence["sourceDescriptors"][name] = deepcopy(value)
                    evidence["authoredDescriptors"][name] = deepcopy([d for d in authored if d["label"] == name])
                evidence["nativeMetadata"][raw_name] = deepcopy(native)
            owners.append(evidence)
    removed = {row["packagePath"] for row in dispositions}
    return {"nonProducts": dispositions, "requiredPackages": sorted(required),
            "unresolvedPackages": sorted(candidates - removed), "owners": owners, "inputs": inputs}
