"""R8.5 expression assets, exclusively from published expression-table GLBs.

The editor cooks typed VFE/authoring rows plus the entire JSON document as evidence.
No TXT substitution, row sorting, controller folding, clamping, or facial evaluation.
Call stage_expression_tables under the CLI's generated-state lease; this module does
not acquire a lease or launch Unreal. A full inventory is required, even for unused units.
"""
from __future__ import annotations

from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import re
import struct

from elysium_pipeline.asset_paths import assert_unique_paths, baked_unit, corpus_path
from elysium_pipeline.formats.expression_table_glb.model import EXPRESSION_TABLE_EXTENSION
from elysium_pipeline.formats.unit_contract import decode_glb
from elysium_pipeline.validation.expression_table_glb import validate_document

PRODUCER = "expression-tables"
SCHEMA = "1.0.0"
CORPUS_ASSET = corpus_path("expression-table", "DA", "ExpressionTables")
ASSET_PREFIX = "vtmb:expression-table:"


def staging_root(work_root):
    return Path(work_root) / "import" / "expression-tables"


def json_text(value):
    return json.dumps(value, ensure_ascii=True, allow_nan=False, sort_keys=True, separators=(",", ":"))


def _sha(data):
    return hashlib.sha256(data).hexdigest()


def _table(block, *, compiled):
    if block is None:
        return {"rows": 0, "controllers": 0, "values": 0, "weights": 0}
    keys, rows = block["keys"], block["rows"]
    if not isinstance(keys, list) or not all(isinstance(key, str) for key in keys):
        raise ValueError("expression keys must be ordered strings")
    if not isinstance(rows, list) or type(block["hasWeighting"]) is not bool:
        raise ValueError("invalid expression rows/weighting")
    previous = -1
    for row in rows:
        index = row["index"]
        if type(index) is not int or not previous < index <= 2147483647:
            raise ValueError("expression source row indices must be increasing int32 values")
        previous = index
        if not all(isinstance(row[field], str) for field in ("name", "class", "description")):
            raise ValueError("expression row strings are malformed")
        code = row["phonemeCode"]
        if code is not None and (type(code) is not int or not 0 <= code <= 2147483647):
            raise ValueError("invalid published phoneme code")
        for field in ("values", "weights"):
            values = row[field]
            count = len(keys) if field == "values" or block["hasWeighting"] else 0
            if not isinstance(values, list) or len(values) != count:
                raise ValueError("expression row/controller alignment is invalid")
            for value in values:
                if type(value) not in (float, int) or not math.isfinite(value):
                    raise ValueError("expression numbers must be finite")
                # The compatibility view uses the existing float evaluator. A compiled
                # float32 must survive it exactly; authoring decimals remain native doubles.
                if compiled and struct.unpack("<f", struct.pack("<f", value))[0] != value:
                    raise ValueError("compiled expression value is not exactly float32")
    return {"rows": len(rows), "controllers": len(keys),
            "values": sum(len(r["values"]) for r in rows),
            "weights": sum(len(r["weights"]) for r in rows)}


def runtime_status(root):
    if not root["identity"]["runtimeLoadable"]:
        return "authoring-only"
    if root["table"] is None:
        return "undecoded-vfe"
    if root["coverage"]["unsupported"]:
        return "unsupported-vfe"
    return "ready"


def expression_projection(document, source_glb_sha256, binary=b""):
    """Validate and preserve *all* published data, including future/unused JSON fields."""
    validation = validate_document(document, binary)
    root = document["extensions"][EXPRESSION_TABLE_EXTENSION]
    identity = root["identity"]
    stem = identity["stem"]
    if (not re.fullmatch(r"[a-z0-9][a-z0-9_.-]*", stem)
            or identity["asset"] != ASSET_PREFIX + stem):
        raise ValueError("expression identity/stem mismatch or unsafe stem")
    if not re.fullmatch(r"[0-9a-f]{64}", source_glb_sha256):
        raise ValueError("invalid source GLB digest")
    runtime = _table(root["table"], compiled=True)
    authoring = _table(root["authoring"], compiled=False)
    status = runtime_status(root)
    if status == "ready":
        settings = root["vfe"]["settings"]
        if (root["vfe"]["numFlexSettings"] != runtime["rows"]
                or len(settings) != runtime["rows"]
                or root["vfe"]["keys"] != root["table"]["keys"]
                or any(s["index"] != r["index"] or s["type"] != "normal"
                       for s, r in zip(settings, root["table"]["rows"]))):
            raise ValueError("compiled expression projection lost/reordered settings or controllers")
    return {"schemaVersion": SCHEMA, "assetId": identity["asset"],
            "assetPath": baked_unit(identity["asset"], "DA"),
            "sourceGlbSha256": source_glb_sha256,
            # Deliberately cooked, not WITH_EDITORONLY_DATA: opaque hex, unused
            # settings, key mappings, authoring and coverage obligations survive cook.
            "sourceDocumentJson": json_text(document), "runtimeStatus": status,
            "counts": {"runtime": runtime, "authoring": authoring,
                       **{key: len(root["coverage"][key])
                          for key in ("typedUnidentified", "unresolved", "unsupported")},
                       "omissions": len(root["omissions"]),
                       "sourceBytes": validation["sourceBytes"],
                       "accountedBytes": validation["accountedBytes"]}}


def _read_projection(path):
    data = Path(path).read_bytes()
    document, binary = decode_glb(data, str(path))
    return expression_projection(document, _sha(data), binary)


def _write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json_text(value) + "\n", encoding="utf-8")
    temporary.replace(path)


def inventory_expression_tables(export_root):
    """Read-only full projection; caller chooses whether/where to publish it."""
    family = Path(export_root) / "expression-tables"
    files = sorted(family.rglob("*.glb"))
    if not files:
        raise ValueError("no published expression tables")
    projections = []
    for path in files:
        projection = _read_projection(path)
        stem = projection["assetId"].removeprefix(ASSET_PREFIX)
        if path.relative_to(family).as_posix() != stem + ".glb":
            raise ValueError("expression publication path disagrees with its identity: " + str(path))
        projections.append(projection)
    assert_unique_paths((p["assetId"], p["assetPath"]) for p in projections)
    return projections


def stage_expression_tables(export_root, stage_root, log=print):
    """Full, failure-closed stage. Never deletes assets or sweeps legacy/stale files."""
    export_root, stage_root = Path(export_root).resolve(), Path(stage_root).resolve()
    if stage_root.is_relative_to(export_root) or export_root.is_relative_to(stage_root):
        raise ValueError("expression stage and exports must not overlap")
    manifest = {"schemaVersion": SCHEMA, "producer": PRODUCER,
                "packageRoot": "/ElysiumBaked/ExpressionTables", "exportRoot": str(export_root),
                "complete": False, "stageFailures": [], "assets": [], "keep": []}
    # Invalidate an earlier successful receipt before any adaptive work can fail.
    _write(stage_root / "manifest.json", manifest)
    try:
        projections = inventory_expression_tables(export_root)
        for projection in projections:
            stem = projection["assetId"].removeprefix(ASSET_PREFIX)
            relative = stem + ".expression.json"
            _write(stage_root / relative, projection)
            manifest["assets"].append({"assetId": projection["assetId"],
                "assetPath": projection["assetPath"], "file": relative,
                "sha256": _sha((stage_root / relative).read_bytes()),
                "source": "expression-tables/" + stem + ".glb",
                "sourceGlbSha256": projection["sourceGlbSha256"],
                "runtimeStatus": projection["runtimeStatus"], "counts": projection["counts"]})
            if projection["runtimeStatus"] != "ready":
                log("Warning: retained expression " + stem + ": " + projection["runtimeStatus"])
            if projection["counts"]["unresolved"]:
                log("Warning: retained unresolved source comparison for expression " + stem)
        corpus = {"schemaVersion": SCHEMA, "assetPath": CORPUS_ASSET,
                  "tables": {p["assetId"]: p["assetPath"] for p in projections}}
        _write(stage_root / "corpus.json", corpus)
        manifest["corpus"] = {"file": "corpus.json", "assetPath": CORPUS_ASSET,
                              "sha256": _sha((stage_root / "corpus.json").read_bytes())}
        manifest["keep"] = [p["assetPath"] for p in projections] + [CORPUS_ASSET]
        manifest["summary"] = dict(Counter(p["runtimeStatus"] for p in projections))
        manifest["complete"] = True
    except (ValueError, KeyError, TypeError, OSError, OverflowError) as exc:
        manifest["stageFailures"].append({"assetId": "expression-corpus", "reason": str(exc)})
        _write(stage_root / "manifest.json", manifest)
        raise
    _write(stage_root / "manifest.json", manifest)
    return manifest


def _checked(root, relative, digest):
    path = (root / relative).resolve()
    if not path.is_relative_to(root.resolve()):
        raise ValueError("expression stage path escapes root")
    data = path.read_bytes()
    if _sha(data) != digest:
        raise ValueError("expression stage digest mismatch: " + relative)
    return json.loads(data)


def verify_expression_stage(manifest_path):
    """Recheck source inventory and every field before an editor writes any packages."""
    path = Path(manifest_path)
    manifest = json.loads(path.read_bytes())
    if (manifest.get("schemaVersion") != SCHEMA or manifest.get("producer") != PRODUCER
            or manifest.get("packageRoot") != "/ElysiumBaked/ExpressionTables"
            or not manifest.get("complete") or manifest.get("stageFailures")):
        raise ValueError("incomplete or invalid expression manifest")
    projections = inventory_expression_tables(manifest["exportRoot"])
    entries = manifest["assets"]
    if [p["assetId"] for p in projections] != [e["assetId"] for e in entries]:
        raise ValueError("expression manifest does not account for every published unit")
    for projection, entry in zip(projections, entries):
        if _checked(path.parent, entry["file"], entry["sha256"]) != projection:
            raise ValueError("expression stage differs from published GLB: " + entry["assetId"])
        for field in ("assetId", "assetPath", "sourceGlbSha256", "runtimeStatus", "counts"):
            if entry[field] != projection[field]:
                raise ValueError("expression manifest differs in " + field)
        if entry["source"] != "expression-tables/" + projection["assetId"].removeprefix(ASSET_PREFIX) + ".glb":
            raise ValueError("expression source path differs")
    corpus = _checked(path.parent, manifest["corpus"]["file"], manifest["corpus"]["sha256"])
    expected = {"schemaVersion": SCHEMA, "assetPath": CORPUS_ASSET,
                "tables": {p["assetId"]: p["assetPath"] for p in projections}}
    if (corpus != expected or manifest["corpus"]["assetPath"] != CORPUS_ASSET
            or manifest["keep"] != [p["assetPath"] for p in projections] + [CORPUS_ASSET]
            or manifest["summary"] != dict(Counter(p["runtimeStatus"] for p in projections))):
        raise ValueError("expression corpus/keep/summary differs from inventory")
    return manifest, projections, corpus


def expression_model_projection(extension):
    """Retail SetModel gender flag plus ordered model-selected tables; no filename inference."""
    flags = extension["mdl"]["header"]["flags"]["value"]
    if type(flags) is not int or not 0 <= flags <= 0xffffffff:
        raise ValueError("expression modelFlags must be an exact uint32 source value")
    return {"schemaVersion": SCHEMA, "modelFlags": flags, "modelIsMale": not bool(flags & 0x100),
            "selections": expression_selection_projection((extension["facial"] or {}).get("selectedTables", []))}


def expression_selection_projection(selected_tables):
    """For the owning body/provenance writer; preserve declared fallback order and evidence.

    FElysiumExpressionSelection is the native twin. Runtime chooses male vs generic
    explicitly; it must not walk generic-first and mask the male fallback.
    """
    result = []
    classes = set()
    for row in selected_tables:
        if row["class"] not in ("expressions", "phonemes") or row["class"] in classes:
            raise ValueError("invalid/duplicate expression selection class")
        classes.add(row["class"])
        candidates = [row, *row["fallbacks"]]
        for candidate in candidates:
            if type(candidate["resolved"]) is not bool:
                raise ValueError("expression selection resolved must be a boolean")
            prefix = ASSET_PREFIX if candidate["resolved"] else "vtmb:missing-expression-table:"
            if candidate["asset"] != prefix + candidate["stem"]:
                raise ValueError("expression selection identity disagrees with resolution")
            if candidate["resolved"]:
                baked_unit(candidate["asset"], "DA")
        result.append({"tableClass": row["class"],
                       "primaryAssetId": row["asset"] if row["resolved"] else "",
                       "fallbackAssetIds": [c["asset"] if c["resolved"] else "" for c in row["fallbacks"]],
                       "sourceSelectionJson": json_text(row)})
    return result
