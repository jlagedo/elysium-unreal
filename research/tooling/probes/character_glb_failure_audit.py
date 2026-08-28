"""Classify Character GLB corpus refusals without rewriting successful products.

Usage::

    uv run elysium research character_glb_failure_audit

The all-character exporter is intentionally strict and continues after a model
failure.  Its console output is too large for the run-report detail field, so
this probe replays only models whose expected GLB is absent and records the
precise source-closure/decode/non-finite failure under ``ELYSIUM_WORK_ROOT``.
No game-derived bytes or decompiler output enter the checkout.
"""

from __future__ import annotations

import argparse
import gc
import json
import math
from pathlib import Path
import re
import struct
from typing import Any

from elysium_pipeline import export_manager
from elysium_pipeline.formats import install, mdl, mdl_cloth, mdl_skel
from elysium_pipeline.formats.character_glb import expressions
from elysium_pipeline.formats.character_glb.decode import decode_character
from elysium_pipeline.formats.character_glb.coverage import cover_closure
from elysium_pipeline.formats.character_glb.model import output_relative_path, plain
from elysium_pipeline.formats.character_glb.source import load_source_closure
from elysium_pipeline.paths import export_root, research_root


REPORT_NAME = "character-glb-failure-audit.json"
LEDGER_REPORT_NAME = "character-glb-ledger-context.json"
FACIAL_REPORT_NAME = "character-glb-facial-context.json"
ANIMATION_REPORT_NAME = "character-glb-animation-gap-context.json"
CLOTH_REPORT_NAME = "character-glb-cloth-map-context.json"


def _i32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<i", data, offset)[0]


def _animation_descriptor_candidate(data: bytes, offset: int) -> dict[str, Any] | None:
    """Recognise a StudioAnimDesc by its record-relative name and scalar fields."""

    if offset < 0 or offset + 72 > len(data):
        return None
    relative = _i32(data, offset)
    name_at = offset + relative
    if relative <= 0 or not 0 <= name_at < len(data):
        return None
    end = data.find(b"\0", name_at, min(len(data), name_at + 256))
    if end <= name_at:
        return None
    try:
        name = data[name_at:end].decode("ascii", "strict")
    except UnicodeDecodeError:
        return None
    if not all(0x20 <= ord(char) < 0x7F for char in name):
        return None
    fps = struct.unpack_from("<f", data, offset + 4)[0]
    frames = _i32(data, offset + 12)
    movements = _i32(data, offset + 16)
    if not math.isfinite(fps) or not 0.0 < fps <= 240.0:
        return None
    if not 0 < frames <= 1_000_000 or not 0 <= movements <= 1024:
        return None
    return {
        "offset": offset,
        "name": name,
        "fps": fps,
        "flags": _i32(data, offset + 8),
        "frames": frames,
        "movementCount": movements,
        "movementOffset": _i32(data, offset + 20),
        "animationDataOffset": _i32(data, offset + 48),
    }


def _nonfinite(value: Any, path: str = "$", output=None) -> list[dict[str, Any]]:
    if output is None:
        output = []
    if isinstance(value, dict):
        for key, item in value.items():
            _nonfinite(item, f"{path}.{key}", output)
    elif isinstance(value, list):
        for index, item in enumerate(value):
            _nonfinite(item, f"{path}[{index}]", output)
    elif isinstance(value, float) and not math.isfinite(value):
        output.append({"path": path, "value": repr(value)})
    return output


def _category(message: str, stage: str) -> str:
    lowered = message.lower()
    if "checksum mismatch" in lowered:
        return "companion-checksum-mismatch"
    if "txt/vfe" in lowered or "txt and vfe" in lowered or "facial" in lowered:
        return "facial-pair"
    if "unclaimed non-zero byte" in lowered or "overlaps" in lowered:
        return "byte-ledger"
    if "phy keyvalues" in lowered:
        return "phy-keyvalues"
    if "different topology" in lowered:
        return "vtx-variant-disagreement"
    if "header length" in lowered:
        return "declared-length"
    return stage


def build_report() -> dict[str, Any]:
    index = install.build_index()
    models = export_manager._character_glb_models(index)
    output_root = export_root() / "glb" / "characters"
    failures = [
        model
        for model in models
        if not (output_root / Path(*output_relative_path(model).parts)).is_file()
    ]
    anorms = mdl_skel.load_anorms()
    rows = []
    for ordinal, model in enumerate(failures, 1):
        row: dict[str, Any] = {"model": model}
        closure = None
        try:
            closure = load_source_closure(index, model)
            row["sources"] = [
                {
                    "role": member.role,
                    "path": member.path,
                    "origin": member.origin,
                    "byteLength": len(member.data),
                }
                for member in closure.members()
            ]
        except Exception as error:
            row.update(
                stage="source-closure",
                category=_category(str(error), "source-closure"),
                errorType=type(error).__name__,
                error=str(error),
            )
            rows.append(row)
            continue

        try:
            decoded = decode_character(closure, index, anorms=anorms)
        except Exception as error:
            row.update(
                stage="semantic-decode",
                category=_category(str(error), "semantic-decode"),
                errorType=type(error).__name__,
                error=str(error),
            )
            rows.append(row)
            gc.collect()
            continue

        values = _nonfinite(plain(decoded))
        if values:
            row.update(
                stage="serialization",
                category="non-finite-values",
                errorType="NonFiniteValue",
                error=f"{len(values)} non-finite semantic values",
                nonFinite=values[:128],
                nonFiniteTruncated=max(0, len(values) - 128),
            )
        else:
            row.update(
                stage="unexpected-success",
                category="unexpected-success",
                errorType="",
                error="semantic decode is now clean but no GLB exists",
            )
        rows.append(row)
        del decoded
        gc.collect()
        if ordinal % 10 == 0 or ordinal == len(failures):
            print(f"  audited {ordinal}/{len(failures)} refused models")

    counts: dict[str, int] = {}
    for row in rows:
        counts[row["category"]] = counts.get(row["category"], 0) + 1
    return {
        "admittedModels": len(models),
        "publishedModels": len(models) - len(failures),
        "refusedModels": len(failures),
        "categoryCounts": dict(sorted(counts.items())),
        "failures": rows,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ledger-only", action="store_true")
    parser.add_argument("--facial-only", action="store_true")
    parser.add_argument("--animation-only", action="store_true")
    parser.add_argument("--cloth-only", action="store_true")
    args = parser.parse_args()
    if args.cloth_only:
        prior = json.loads((research_root() / LEDGER_REPORT_NAME).read_text(encoding="utf-8"))
        rows = []
        index = install.build_index()
        for failure in prior:
            if ".cloth.selectors" not in failure["error"] and not (
                "overlaps" in failure["error"] and ".cloth.tangents" in failure["error"]
            ):
                continue
            match = re.search(
                r"bodyPart\[(\d+)\]\.model\[(\d+)\]\.mesh\[(\d+)\]",
                failure["error"],
            )
            if match is None:
                continue
            bodypart_index, model_index, mesh_index = map(int, match.groups())
            closure = load_source_closure(index, failure["model"])
            data = closure.mdl.data
            vtx_data = closure.primary_vtx.data

            bodyparts = _i32(data, 324)
            bodypart = bodyparts + bodypart_index * 16
            model_base = bodypart + _i32(data, bodypart + 12) + model_index * mdl_skel.MODEL_STRIDE
            mesh_base = model_base + _i32(data, model_base + 140)
            mesh = mesh_base + mesh_index * mdl_skel.MESH_STRIDE
            selector = mesh + _i32(data, mesh + 48)
            position = mesh + _i32(data, mesh + 52)
            tangent = mesh + _i32(data, mesh + 56)

            vtx_bodyparts = _i32(vtx_data, 32)
            vtx_bodypart = vtx_bodyparts + bodypart_index * 8
            vtx_model = (
                vtx_bodypart
                + _i32(vtx_data, vtx_bodypart + 4)
                + model_index * 8
            )
            lod_count = _i32(vtx_data, vtx_model)
            lod_base = vtx_model + _i32(vtx_data, vtx_model + 4)
            lods = []
            for lod_index in range(lod_count):
                lod = lod_base + lod_index * 12
                vtx_mesh = lod + _i32(vtx_data, lod + 4) + mesh_index * 8
                group_count = struct.unpack_from("<H", vtx_data, vtx_mesh)[0]
                group_base = vtx_mesh + _i32(vtx_data, vtx_mesh + 4)
                raw_vertices = 0
                original_vertices = []
                for group_index in range(group_count):
                    group = group_base + group_index * mdl.STRIPGROUP_STRIDE
                    count = struct.unpack_from("<H", vtx_data, group)[0]
                    stride, _ = mdl._vtable_form(vtx_data, group)
                    vertices = group + _i32(vtx_data, group + 8)
                    raw_vertices += count
                    original_vertices.extend(
                        struct.unpack_from("<H", vtx_data, vertices + vertex * stride + 10)[0]
                        for vertex in range(count)
                    )
                lods.append(
                    {
                        "lod": lod_index,
                        "rawVertexEntries": raw_vertices,
                        "uniqueOriginalVertices": len(set(original_vertices)),
                        "maxOriginalVertexPlusOne": max(original_vertices, default=-1) + 1,
                    }
                )
            _table, definition_rows, definition_columns, _records = (
                mdl_cloth.definition_table(data, model_base)
            )
            rows.append(
                {
                    "model": failure["model"],
                    "mesh": mesh_index,
                    "mdlVertexCount": _i32(data, mesh + 8),
                    "definitionRows": definition_rows,
                    "definitionColumns": definition_columns,
                    "vtxLodCount": lod_count,
                    "selectorEntriesFromSpan": position - selector,
                    "positionEntriesFromSpan": (tangent - position) // 2,
                    "lods": lods,
                    "error": failure["error"],
                }
            )
        destination = research_root() / CLOTH_REPORT_NAME
        destination.write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")
        print(f"cloth map contexts: {len(rows)}")
        print(f"report: {destination}")
        return 0

    if args.animation_only:
        prior = json.loads((research_root() / LEDGER_REPORT_NAME).read_text(encoding="utf-8"))
        rows = []
        index = install.build_index()
        for failure in prior:
            if "animationDescriptors" not in failure["error"]:
                continue
            match = re.search(r"gap (\d+)\+(\d+) between", failure["error"])
            if match is None:
                continue
            gap_start, gap_length = map(int, match.groups())
            closure = load_source_closure(index, failure["model"])
            data = next(member.data for member in closure.members() if member.role == "mdl")
            gap_end = gap_start + gap_length
            candidates = []
            for offset in range(gap_start, gap_end - 71, 4):
                candidate = _animation_descriptor_candidate(data, offset)
                if candidate is not None:
                    candidates.append(candidate)
            indexed = []
            animation_count = _i32(data, 264)
            animation_base = _i32(data, 268)
            for animation_index in range(animation_count):
                candidate = _animation_descriptor_candidate(
                    data, animation_base + animation_index * 72
                )
                if candidate is not None:
                    candidate["index"] = animation_index
                    indexed.append(candidate)
            indexed_signatures = {
                (row["name"], row["fps"], row["flags"], row["frames"])
                for row in indexed
            }
            for candidate in candidates:
                candidate["duplicatesIndexedSemanticRecord"] = (
                    candidate["name"],
                    candidate["fps"],
                    candidate["flags"],
                    candidate["frames"],
                ) in indexed_signatures
            rows.append(
                {
                    "model": failure["model"],
                    "gapOffset": gap_start,
                    "gapLength": gap_length,
                    "indexedAnimationCount": animation_count,
                    "indexedAnimationOffset": animation_base,
                    "candidateCount": len(candidates),
                    "candidates": candidates,
                }
            )
        destination = research_root() / ANIMATION_REPORT_NAME
        destination.write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")
        print(f"animation gap contexts: {len(rows)}")
        print(f"report: {destination}")
        return 0

    if args.facial_only:
        prior = json.loads((research_root() / REPORT_NAME).read_text(encoding="utf-8"))
        models = [
            row["model"] for row in prior["failures"] if row["category"] == "facial-pair"
        ]
        index = install.build_index()
        rows = []
        for model in models:
            basename = model.rsplit("/", 1)[-1][:-4]
            families = []
            for family in ("expressions", "phonemes"):
                txt = install.read(index, f"expressions/{basename}_{family}.txt")
                vfe = install.read(index, f"expressions/{basename}_{family}.vfe")
                if txt is None or vfe is None:
                    families.append(
                        {"family": family, "txt": txt is not None, "vfe": vfe is not None}
                    )
                    continue
                source = expressions.decode_txt(txt)
                compiled = expressions.decode_vfe(vfe)
                detail: dict[str, Any] = {
                    "family": family,
                    "keysEqual": source["keys"] == compiled["keys"],
                    "sourceRows": len(source["rows"]),
                    "compiledRows": len(compiled["settings"]),
                    "nameMismatches": [],
                    "maxValueDelta": 0.0,
                    "maxWeightDelta": 0.0,
                    "valueDeltasAbove1e5": 0,
                    "valueDeltasAboveRoundedText": 0,
                }
                for index_row, (a, b) in enumerate(zip(source["rows"], compiled["settings"])):
                    if a["name"] != b["name"]:
                        detail["nameMismatches"].append(
                            {"row": index_row, "txt": a["name"], "vfe": b["name"]}
                        )
                    compiled_values = {value["controller"]: value for value in b["values"]}
                    for value in a["values"]:
                        other = compiled_values.get(
                            value["controller"], {"value": 0.0, "weight": 0.0}
                        )
                        value_delta = abs(value["value"] - other["value"])
                        weight_delta = abs(value.get("weight", 1.0) - other["weight"])
                        detail["maxValueDelta"] = max(detail["maxValueDelta"], value_delta)
                        detail["maxWeightDelta"] = max(detail["maxWeightDelta"], weight_delta)
                        detail["valueDeltasAbove1e5"] += value_delta > 1e-5
                        detail["valueDeltasAboveRoundedText"] += value_delta > 0.000501
                families.append(detail)
            rows.append({"model": model, "families": families})
        destination = research_root() / FACIAL_REPORT_NAME
        destination.write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")
        print(f"facial contexts: {len(rows)}")
        print(f"report: {destination}")
        return 0

    if args.ledger_only:
        prior = json.loads((research_root() / REPORT_NAME).read_text(encoding="utf-8"))
        models = [
            row["model"] for row in prior["failures"] if row["category"] == "byte-ledger"
        ]
        index = install.build_index()
        rows = []
        for model in models:
            try:
                closure = load_source_closure(index, model)
                cover_closure(closure.members())
                error = "coverage now succeeds"
            except Exception as caught:
                error = str(caught)
            rows.append({"model": model, "error": error})
        destination = research_root() / LEDGER_REPORT_NAME
        destination.write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")
        print(f"ledger contexts: {len(rows)}")
        print(f"report: {destination}")
        return 0

    report = build_report()
    destination = research_root() / REPORT_NAME
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report["categoryCounts"], indent=2))
    print(f"report: {destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
