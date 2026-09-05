"""Offline receipt/hash gate around the independent native geometry comparator.

Only manifest.selectedUnits participates. Read native_verify_report.json after the fresh
editor exits; its geometrySnapshots receipts name canonical stage-relative envelope files.
The sole output is native_geometry_report.json (atomically replaced). No native process,
asset writer, generated-state command, or geometry threshold is changed here.
"""
from __future__ import annotations

from collections import Counter
import hashlib
import json
import math
import os
from pathlib import Path
import re
import tempfile

from elysium_pipeline.formats.unit_contract.container import decode_glb
from elysium_pipeline.validation.geometry_inventory import morph_source_inventory
from elysium_pipeline.validation.native_geometry import verify_native_geometry
from elysium_pipeline.validation.skeletal_diff import sections


class GeometryStageError(ValueError):
    pass


def _require(condition, reason):
    if not condition:
        raise GeometryStageError(reason)


def _object(pairs):
    value = {}
    for key, item in pairs:
        _require(key not in value, "duplicate JSON key: " + key)
        value[key] = item
    return value


def _invalid_number(value):
    raise GeometryStageError("nonfinite JSON number: " + value)


def _float(value):
    number = float(value)
    _require(math.isfinite(number), "nonfinite JSON number: " + value)
    return number


def _json(data):
    result = json.loads(data, object_pairs_hook=_object, parse_constant=_invalid_number, parse_float=_float)
    _require(isinstance(result, dict), "JSON root must be an object")
    return result


def _sha(data):
    return hashlib.sha256(data).hexdigest()


def _digest(value, field):
    _require(isinstance(value, str) and re.fullmatch(r"[0-9a-fA-F]{64}", value), "invalid " + field)
    return value.lower()


def _relative(value):
    _require(isinstance(value, str) and bool(value), "missing relative file path")
    parts = value.replace("\\", "/").split("/")
    _require(all(part not in ("", ".", "..") and not part.endswith((" ", "."))
                 and not any(ord(c) < 32 or c in ':<>"|?*' for c in part) for part in parts),
             "invalid relative file path: " + value)
    return "/".join(parts)


def _path(root, relative):
    path = root.joinpath(*_relative(relative).split("/"))
    _require(path.resolve().is_relative_to(root), "file path escapes its root: " + str(relative))
    return path


def _asset_id(value):
    _require(isinstance(value, str) and value.startswith("vtmb:model:"), "invalid model assetId")
    suffix = value.removeprefix("vtmb:model:")
    _require(_relative(suffix) == suffix, "noncanonical model assetId: " + value)
    return suffix


def _checked(root, relative, expected, field):
    data = _path(root, relative).read_bytes()
    digest = _digest(expected, field)
    _require(_sha(data) == digest, field + " mismatch: " + relative)
    return data


def _error(exc, *, asset_id=None):
    row = {"reason": str(exc)[:1200]}
    if asset_id is not None:
        row["assetId"] = str(asset_id)[:500]
    return row


def _compact_result(result):
    """Explicit allowlists ensure no snapshot/vertex/morph arrays escape into reports."""
    row = {"passed": result["passed"], "differenceCount": len(result.get("differences", [])),
           "tangentsVerified": result.get("tangentsVerified", False),
           "fullChannelVerificationPassed": result.get("fullChannelVerificationPassed", False),
           "differences": [_error(r.get("reason", "unspecified numeric difference"))
                           for r in result.get("differences", [])[:8]]}
    fields = {
        "sourceInventory": ("passed", "scope", "orderedRecordSha256", "glbSha256", "primitiveRecords",
                            "explicitZeroRecords", "repeatedContributions", "unrenderedVertexRecords",
                            "unrenderedMeshRecords", "stagedMorphRecords", "vertices", "triangles",
                            "nativeDenseMorphsProveSourceInventory", "sourceTangentsVerified", "tangentVectors", "zeroTangentVectors"),
        "authoring": ("passed", "vertices", "instances", "morphs", "tangentsVerified", "tangentVectors", "zeroTangentVectors", "tangentComponentBound"),
        "render": ("passed", "vertices", "sourceVertices", "representedSourceIds", "sourceAliasCount",
                   "sourceAliasSha256", "exactEquivalenceClasses", "splitCopies", "sections", "morphs", "weightBits",
                   "normalComponentBound", "packedGpuMorphStreamsVerified", "tangentsVerified", "tangentVectors", "zeroTangentVectors", "tangentComponentBound"),
    }
    for name, keys in fields.items():
        data = result.get(name, {})
        row[name] = {key: data[key] for key in keys if key in data}
        if name == "sourceInventory" and data.get("passed"):
            for field, count in (("sourceNormalFallbacks", "normalFallbackVertices"),
                                 ("boneNameProjection", "renamedBones"),
                                 ("materialBindings", "materialSlots"),
                                 ("higherLodsRetainedInGlb", "higherLodCount")):
                if field in data:
                    row[name][count] = len(data[field])
        if "quantizedInfluenceLosses" in data:
            row[name]["quantizedInfluenceLossCount"] = len(data["quantizedInfluenceLosses"])
    return row


def _selection(manifest):
    _require(manifest.get("producer") == "characters" and manifest.get("schemaVersion") == "1.0.0",
             "unsupported character manifest producer/schema")
    _require(manifest.get("stageFailures") == [], "manifest stageFailures are missing or nonempty")
    assets, selected = manifest.get("assets"), manifest.get("selectedUnits")
    _require(isinstance(assets, list) and isinstance(selected, list) and bool(selected), "invalid/empty manifest selection")
    indexed, folded = {}, set()
    for entry in assets:
        _require(isinstance(entry, dict), "invalid manifest asset row")
        identity = entry.get("assetId")
        _asset_id(identity)
        _require(identity.casefold() not in folded, "duplicate manifest assetId: " + identity)
        indexed[identity] = entry
        folded.add(identity.casefold())
    folded.clear()
    entries = []
    mesh_paths = set()
    for identity in selected:
        _asset_id(identity)
        _require(identity.casefold() not in folded, "duplicate selectedUnits assetId: " + identity)
        folded.add(identity.casefold())
        _require(identity in indexed, "selectedUnits assetId missing from assets: " + identity)
        entry = indexed[identity]
        _require("meshAsset" in entry and (entry["meshAsset"] is None or
                 isinstance(entry["meshAsset"], str) and entry["meshAsset"].startswith("/") and "." not in entry["meshAsset"]),
                 "invalid manifest meshAsset: " + identity)
        if entry["meshAsset"] is not None:
            folded_mesh = entry["meshAsset"].casefold()
            _require(folded_mesh not in mesh_paths, "duplicate selected native mesh package: " + entry["meshAsset"])
            mesh_paths.add(folded_mesh)
        entries.append(entry)
    return entries


def _receipts(editor, mesh_ids, differences):
    receipts = editor.get("geometrySnapshots")
    _require(isinstance(receipts, list), "editor report geometrySnapshots are missing/invalid")
    by_id, files, duplicates = {}, {}, set()
    for receipt in receipts:
        identity = receipt.get("assetId") if isinstance(receipt, dict) else None
        try:
            suffix = _asset_id(identity)
            _require(identity in mesh_ids, "extra/unselected/source-only geometry receipt: " + identity)
            if identity in by_id:
                duplicates.add(identity)
                raise GeometryStageError("duplicate geometry receipt: " + identity)
            # Keep even a malformed first receipt so a second cannot replace it silently.
            by_id[identity] = receipt
            relative = _relative(receipt.get("file"))
            _require(relative == "native_geometry/" + suffix + ".json", "noncanonical geometry receipt file: " + relative)
            _require(relative.casefold() not in files, "duplicate geometry receipt file: " + relative)
            files[relative.casefold()] = identity
            _digest(receipt.get("sha256"), "snapshot receipt sha256")
        except (ValueError, TypeError, KeyError) as exc:
            differences.append(_error(exc, asset_id=identity))
            if isinstance(identity, str) and identity in mesh_ids:
                duplicates.add(identity)  # Refuse scoring ambiguous or malformed receipt rows.
    for identity in sorted(mesh_ids - by_id.keys()):
        differences.append(_error("missing geometry receipt", asset_id=identity))
    return by_id, duplicates


def _inputs(entry, stage, export):
    recipe = entry["recipe"]
    payload = _checked(stage, entry["payload"], recipe["payloadSha256"], "payloadSha256")
    body_bytes = _checked(stage, entry["body"], recipe["bodySha256"], "bodySha256")
    source = _checked(export, entry["unitGlb"], recipe["unitSha256"], "unitSha256")
    body = _json(body_bytes)
    _require(body.get("assetId") == entry["assetId"], "body assetId differs from manifest")
    return payload, body, source


def _source_only(entry, payload, body, source):
    _require("MESH" not in sections(payload), "source-only manifest entry contains staged mesh geometry")
    document, _ = decode_glb(source)
    root = document["extensions"]["ELYSIUM_vtmb_model"]
    _require(root["identity"]["asset"] == entry["assetId"], "source-only GLB identity differs from manifest")
    semantics = body.get("sourceSemantics")
    _require(isinstance(semantics, dict) and "mdl" in semantics and "facial" in semantics,
             "source-only body lacks source semantics")
    for key, values in semantics.items():
        _require(key in root and values == root[key], "source-only body/source semantics differ: " + key)
    _require(body.get("renderVertexMap") == [], "source-only body contains a native vertex join")
    inventory = morph_source_inventory(document)
    inventory["glbSha256"] = _sha(source)
    return {"passed": True, "differences": [], "sourceInventory": inventory}


def _mesh(entry, receipt, payload, body, source, stage):
    data = _checked(stage, receipt["file"], receipt["sha256"], "snapshot receipt sha256")
    envelope = _json(data)
    _require(type(envelope.get("snapshotVersion")) is int and envelope["snapshotVersion"] == 1,
             "unsupported geometry snapshotVersion")
    for key in ("assetId", "meshAsset"):
        _require(envelope.get(key) == entry[key], "snapshot envelope " + key + " differs from manifest")
    for key in ("payloadSha256", "bodySha256", "unitSha256"):
        _require(_digest(envelope.get(key), "envelope " + key) == _digest(entry["recipe"][key], key),
                 "stale snapshot envelope " + key)
    _require(isinstance(envelope.get("native"), dict) and isinstance(envelope.get("materialPaths"), dict),
             "snapshot envelope native/materialPaths missing or invalid")
    _require(all(isinstance(k, str) and isinstance(v, str) and v.startswith("/")
                 for k, v in envelope["materialPaths"].items()), "invalid snapshot materialPaths")
    result = verify_native_geometry(payload, envelope["native"], body=body, source_glb=source,
                                    expected_source_sha256=_digest(entry["recipe"]["unitSha256"], "unitSha256"),
                                    material_paths=envelope["materialPaths"])
    # Manifest routing is a separate identity join from resolved native package paths.
    inventory = result.get("sourceInventory", {})
    if inventory.get("passed") and entry.get("materials") != inventory["materialBindings"]:
        result["passed"] = False
        result["differences"].append({"reason": "manifest material bindings differ from GLB"})
    return result


def _write_report(stage, export, report):
    destination = _path(stage, "native_geometry_report.json")
    _require(not destination.is_symlink() and not destination.resolve().is_relative_to(export),
             "geometry evidence output aliases an input/export path")
    encoded = json.dumps(report, indent=2, allow_nan=False).encode("utf-8")
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=stage, prefix="native_geometry_report.", suffix=".tmp", delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(encoded)
        os.replace(temporary, destination)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def verify_geometry_stage(stage_root, export_root):
    """Score selected receipt envelopes sequentially; write/return a compact evidence report.

    geometryPassed covers selection, receipts, pinned inputs and numeric/source-inventory
    checks. passed additionally requires no failures in the editor's native report.
    Unrelated native failures never suppress valid geometry diagnostics. Missing/stale
    receipts fail closed. native_verify_report.json is the sole receipt authority;
    unreceipted files in native_geometry are not evidence and are never scanned or pruned.
    Failure to write the requested report propagates to the caller as an exception.
    """
    stage, export = Path(stage_root).resolve(), Path(export_root).resolve()
    report = {"schemaVersion": 1, "scope": "selected-source-stage-editor-and-built-render-LOD0",
              "passed": False, "geometryPassed": False, "editorNativePassed": False,
              "evaluatedSkinningVerified": False, "renderedAcceptance": False,
              "tangentsVerified": False, "fullSourceGeometryPreservationVerified": False,
              "unverifiedChannels": ["TANGENT"],
              "differences": [], "nativeFailures": [], "products": [], "counts": {}}
    counts = Counter()
    manifest_bytes = editor_bytes = None
    try:
        manifest_bytes = _path(stage, "manifest.json").read_bytes()
        editor_bytes = _path(stage, "native_verify_report.json").read_bytes()
        report["manifestSha256"], report["editorReportSha256"] = _sha(manifest_bytes), _sha(editor_bytes)
        manifest, editor = _json(manifest_bytes), _json(editor_bytes)
        entries = _selection(manifest)
        counts["selectedUnits"] = len(entries)
        counts["unselectedManifestEntries"] = len(manifest["assets"]) - len(entries)
        _require(_digest(editor.get("manifestSha256"), "editor manifestSha256") == report["manifestSha256"],
                 "stale editor report manifestSha256")
        native_failures = editor.get("failed")
        _require(isinstance(native_failures, list), "editor report failed list missing/invalid")
        report["nativeFailures"] = [_error(r.get("reason", r), asset_id=r.get("assetId"))
                                    if isinstance(r, dict) else _error(r) for r in native_failures]
        report["editorNativePassed"] = not native_failures and editor.get("passed", True) is not False
        counts["nativeFailures"] = len(native_failures)
        mesh_ids = {e["assetId"] for e in entries if e["meshAsset"] is not None}
        counts["expectedMeshes"], counts["expectedSourceOnly"] = len(mesh_ids), len(entries) - len(mesh_ids)
        receipts, invalid = _receipts(editor, mesh_ids, report["differences"])
        counts["receipts"] = len(editor["geometrySnapshots"])
        for entry in entries:
            identity, is_mesh = entry["assetId"], entry["meshAsset"] is not None
            row = {"assetId": identity, "kind": "mesh" if is_mesh else "source-only", "passed": False}
            try:
                payload, body, source = _inputs(entry, stage, export)
                if is_mesh:
                    _require(identity in receipts and identity not in invalid, "missing/invalid/duplicate geometry receipt")
                    result = _mesh(entry, receipts[identity], payload, body, source, stage)
                    counts["meshesCompared"] += 1
                else:
                    result = _source_only(entry, payload, body, source)
                    counts["sourceOnlyCompared"] += 1
                row.update(_compact_result(result))
                for key in ("primitiveRecords", "explicitZeroRecords", "repeatedContributions", "unrenderedVertexRecords",
                            "unrenderedMeshRecords", "stagedMorphRecords", "vertices", "triangles"):
                    counts[key] += row["sourceInventory"].get(key, 0)
                if is_mesh:
                    row["snapshotSha256"] = _digest(receipts[identity]["sha256"], "snapshot receipt sha256")
            except (OSError, ValueError, KeyError, TypeError, IndexError, OverflowError) as exc:
                row.update(differenceCount=1, differences=[_error(exc)])
            counts["passedUnits" if row["passed"] else "failedUnits"] += 1
            report["products"].append(row)
        # A checkpoint/report or manifest rewritten during this run cannot be accepted as
        # one consistent editor pass. Per-file hashes are checked on the bytes compared.
        _require(_path(stage, "manifest.json").read_bytes() == manifest_bytes, "manifest changed during geometry verification")
        _require(_path(stage, "native_verify_report.json").read_bytes() == editor_bytes,
                 "editor report changed during geometry verification")
        report["geometryPassed"] = not report["differences"] and counts["failedUnits"] == 0
        report["passed"] = report["geometryPassed"] and report["editorNativePassed"]
        mesh_results = [row for row in report["products"] if row["kind"] == "mesh"]
        report["tangentsVerified"] = report["geometryPassed"] and bool(mesh_results) and all(
            row.get("tangentsVerified") and row.get("fullChannelVerificationPassed") for row in mesh_results)
        report["fullSourceGeometryPreservationVerified"] = report["passed"] and report["tangentsVerified"]
        report["unverifiedChannels"] = [] if report["tangentsVerified"] else ["TANGENT"]
    except (OSError, ValueError, KeyError, TypeError, IndexError, OverflowError) as exc:
        report["differences"].append(_error(exc))
    report["counts"] = dict(counts)
    _write_report(stage, export, report)
    return report
