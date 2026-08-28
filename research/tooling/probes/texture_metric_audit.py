"""Test the final VtMB ``StudioTexture`` float against decoded surface geometry.

Usage::

    uv run elysium research texture_metric_audit --model models/character/x.mdl
    uv run elysium research texture_metric_audit --limit 200

The 20-byte VtMB texture record is ``name, flags, width, height, float``.  The
runtime material walk reads only ``name``; the other four values are compiler
output.  Shipped ``flags/width/height`` are zero, while the final float is
normally populated.  The Bloodlines SDK leaves that field unnamed, but its
nearby compiler structure carries ``dPdu``/``dPdv`` values described as world
units per texture coordinate.

This probe does not import that proposed meaning.  It independently decodes
LOD0 triangles, solves dP/du and dP/dv for every non-degenerate UV triangle,
resolves both current and retail texture dimensions, and scores the stored
float against normalized-UV, per-axis world-per-texel, combined-axis and area
summaries.  A candidate is only useful when both its median relative error and
its exact-ish match rate hold across unrelated models; correlation alone does
not name the field.

Generated rows are game-derived and are written below ``ELYSIUM_WORK_ROOT``.
"""

from __future__ import annotations

import argparse
import json
import math
import statistics
import struct
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable

from elysium_pipeline.formats import install, mdl, tex_to_png, vpk
from elysium_pipeline.paths import research_root


REPORT_NAME = "texture-metric-audit.json"
EPSILON = 1.0e-12


def _i32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<i", data, offset)[0]


def _f32(data: bytes, offset: int) -> float:
    return struct.unpack_from("<f", data, offset)[0]


def _cstr(data: bytes, offset: int) -> str:
    end = data.find(b"\0", offset)
    if end < 0:
        end = len(data)
    return data[offset:end].decode("ascii", "replace")


def _sub(
    a: tuple[float, float, float], b: tuple[float, float, float]
) -> tuple[float, float, float]:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _mul(a: tuple[float, float, float], scale: float) -> tuple[float, float, float]:
    return (a[0] * scale, a[1] * scale, a[2] * scale)


def _add(
    a: tuple[float, float, float], b: tuple[float, float, float]
) -> tuple[float, float, float]:
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def _length(a: tuple[float, float, float]) -> float:
    return math.sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2])


def _cross(
    a: tuple[float, float, float], b: tuple[float, float, float]
) -> tuple[float, float, float]:
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


@dataclass
class SurfaceSamples:
    values: dict[str, list[float]] = field(default_factory=lambda: defaultdict(list))
    world_areas: list[float] = field(default_factory=list)
    uv_areas: list[float] = field(default_factory=list)

    def add_triangle(self, vertices: list[tuple[float, float, float, float, float]],
                     triangle: tuple[int, int, int]) -> None:
        v0, v1, v2 = (vertices[index] for index in triangle)
        p0, p1, p2 = v0[:3], v1[:3], v2[:3]
        edge1, edge2 = _sub(p1, p0), _sub(p2, p0)
        du1, dv1 = v1[3] - v0[3], v1[4] - v0[4]
        du2, dv2 = v2[3] - v0[3], v2[4] - v0[4]
        determinant = du1 * dv2 - dv1 * du2
        world_area = 0.5 * _length(_cross(edge1, edge2))
        uv_area = 0.5 * abs(determinant)
        if world_area <= EPSILON or uv_area <= EPSILON:
            return

        inverse = 1.0 / determinant
        dpdu = _mul(_add(_mul(edge1, dv2), _mul(edge2, -dv1)), inverse)
        dpdv = _mul(_add(_mul(edge2, du1), _mul(edge1, -du2)), inverse)
        len_u, len_v = _length(dpdu), _length(dpdv)
        if len_u <= EPSILON or len_v <= EPSILON:
            return

        uv_per_u = 1.0 / len_u
        uv_per_v = 1.0 / len_v
        samples = {
            "worldPerNormalizedU": len_u,
            "worldPerNormalizedV": len_v,
            "uvPerWorldU": uv_per_u,
            "uvPerWorldV": uv_per_v,
            "uvPerWorldGeometric": math.sqrt(uv_per_u * uv_per_v),
            "uvPerWorldHarmonic": 2.0 / (len_u + len_v),
            "uvPerWorldRms": math.sqrt(2.0 / (len_u * len_u + len_v * len_v)),
            "uvAreaPerWorldAreaSqrt": math.sqrt(uv_area / world_area),
        }
        for name, value in samples.items():
            if math.isfinite(value) and value >= 0.0:
                self.values[name].append(value)
        self.world_areas.append(world_area)
        self.uv_areas.append(uv_area)

    def summaries(self, texture_width: int | None = None,
                  texture_height: int | None = None) -> dict[str, float]:
        values_by_name = dict(self.values)
        world_u = self.values.get("worldPerNormalizedU", [])
        world_v = self.values.get("worldPerNormalizedV", [])
        if (texture_width and texture_height and len(world_u) == len(world_v)
                and len(world_u) == len(self.world_areas)):
            texel_u = [value / texture_width for value in world_u]
            texel_v = [value / texture_height for value in world_v]
            values_by_name.update(
                {
                    "worldPerTexelU": texel_u,
                    "worldPerTexelV": texel_v,
                    "worldPerTexelMaxAxis": [
                        max(u, v) for u, v in zip(texel_u, texel_v)
                    ],
                    "worldPerTexelMinAxis": [
                        min(u, v) for u, v in zip(texel_u, texel_v)
                    ],
                    "worldPerTexelGeometric": [
                        math.sqrt(u * v) for u, v in zip(texel_u, texel_v)
                    ],
                    "worldPerTexelHarmonic": [
                        2.0 * u * v / (u + v) for u, v in zip(texel_u, texel_v)
                    ],
                    "worldPerTexelRms": [
                        math.sqrt((u * u + v * v) / 2.0)
                        for u, v in zip(texel_u, texel_v)
                    ],
                    "worldAreaPerTexelAreaSqrt": [
                        math.sqrt(world_area / (
                            uv_area * texture_width * texture_height
                        ))
                        for world_area, uv_area in zip(self.world_areas, self.uv_areas)
                    ],
                }
            )
        out: dict[str, float] = {}
        for name, values in values_by_name.items():
            if not values:
                continue
            out[f"{name}.mean"] = statistics.fmean(values)
            out[f"{name}.rms"] = math.sqrt(statistics.fmean(
                value * value for value in values
            ))
            out[f"{name}.first"] = values[0]
            out[f"{name}.last"] = values[-1]
            out[f"{name}.median"] = statistics.median(values)
            ordered = sorted(values)
            for percentile in (10, 25, 75, 90):
                position = round((len(ordered) - 1) * percentile / 100.0)
                out[f"{name}.p{percentile}"] = ordered[position]
            out[f"{name}.min"] = min(values)
            out[f"{name}.max"] = max(values)
            if len(values) == len(self.world_areas):
                world_total = sum(self.world_areas)
                uv_total = sum(self.uv_areas)
                if world_total > EPSILON:
                    out[f"{name}.worldAreaWeighted"] = sum(
                        value * weight for value, weight in zip(values, self.world_areas)
                    ) / world_total
                    out[f"{name}.worldAreaWeightedRms"] = math.sqrt(sum(
                        value * value * weight
                        for value, weight in zip(values, self.world_areas)
                    ) / world_total)
                if uv_total > EPSILON:
                    out[f"{name}.uvAreaWeighted"] = sum(
                        value * weight for value, weight in zip(values, self.uv_areas)
                    ) / uv_total
                    out[f"{name}.uvAreaWeightedRms"] = math.sqrt(sum(
                        value * value * weight
                        for value, weight in zip(values, self.uv_areas)
                    ) / uv_total)
        if self.world_areas and sum(self.world_areas) > EPSILON:
            out["aggregateAreaRatioSqrt"] = math.sqrt(
                sum(self.uv_areas) / sum(self.world_areas)
            )
        for suffix in (
            "mean", "rms", "first", "last", "median", "p10", "p25", "p75", "p90",
            "min", "max", "worldAreaWeighted", "worldAreaWeightedRms",
            "uvAreaWeighted", "uvAreaWeightedRms",
        ):
            u_name = f"worldPerTexelU.{suffix}"
            v_name = f"worldPerTexelV.{suffix}"
            if u_name in out and v_name in out:
                out[f"worldPerTexelAxisMax.{suffix}"] = max(out[u_name], out[v_name])
                out[f"worldPerTexelAxisMin.{suffix}"] = min(out[u_name], out[v_name])
        return out


def _texture_records(data: bytes) -> list[dict[str, Any]]:
    count, base = _i32(data, 292), _i32(data, 296)
    rows = []
    for index in range(count):
        record = base + index * 20
        if record < 0 or record + 20 > len(data):
            break
        rows.append(
            {
                "index": index,
                "name": _cstr(data, record + _i32(data, record)),
                "flags": _i32(data, record + 4),
                "width": _f32(data, record + 8),
                "height": _f32(data, record + 12),
                "stored": _f32(data, record + 16),
            }
        )
    return rows


def _model_keys(index: dict[str, Any], requested: list[str], limit: int | None) -> list[str]:
    if requested:
        keys = []
        for value in requested:
            key = value.replace("\\", "/").lstrip("/")
            if not key.startswith("models/"):
                key = "models/" + key
            if not key.endswith(".mdl"):
                key += ".mdl"
            keys.append(key)
    else:
        keys = sorted(
            key for key in index if key.startswith("models/") and key.endswith(".mdl")
        )
    return keys[:limit] if limit is not None else keys


def _relative_error(expected: float, actual: float) -> float | None:
    if expected <= EPSILON or not math.isfinite(expected) or not math.isfinite(actual):
        return None
    return abs(actual - expected) / expected


def _texture_dimensions(
    material: str,
    search_paths: list[str],
    read: Callable[[str], bytes | None],
    source: str,
    cache: dict[tuple[str, tuple[str, ...], str], dict[str, Any] | None],
) -> dict[str, Any] | None:
    key = (source, tuple(search_paths), material.casefold())
    if key in cache:
        return cache[key]

    vmt_path, info = mdl.resolve_vmt(material, search_paths, read)
    if not vmt_path or info is None:
        cache[key] = None
        return None
    texture = info.get("basetexture") or info.get("iris")
    if not texture:
        cache[key] = None
        return None
    texture = str(texture).replace("\\", "/").lstrip("/")
    tth = read(f"materials/{texture}.tth")
    if not tth:
        cache[key] = None
        return None
    try:
        width, height, _format, _mips = tex_to_png.parse_tth(tth)
    except (IndexError, struct.error, ValueError):
        cache[key] = None
        return None
    result = {
        "source": source,
        "vmt": vmt_path,
        "texture": texture,
        "width": width,
        "height": height,
    }
    cache[key] = result
    return result


def audit(requested: list[str], limit: int | None) -> dict[str, Any]:
    index = install.build_index()
    retail_index = {key: ("vpk", value) for key, value in vpk.index_all(install.GAME).items()}
    rows: list[dict[str, Any]] = []
    faults: list[dict[str, str]] = []
    counts = defaultdict(int)
    dimension_cache: dict[
        tuple[str, tuple[str, ...], str], dict[str, Any] | None
    ] = {}

    for model_key in _model_keys(index, requested, limit):
        counts["modelsConsidered"] += 1
        data = install.read(index, model_key)
        model_source = index.get(model_key, ("missing", None))[0]
        vtx_key = model_key[:-4] + ".dx80.vtx"
        vtx = install.read(index, vtx_key)
        if not data or not vtx:
            counts["modelsWithoutPair"] += 1
            continue
        try:
            meshes = mdl.decode(data, vtx)
        except (AssertionError, IndexError, KeyError, struct.error, ValueError) as error:
            counts["modelsDecodeFailed"] += 1
            faults.append({"model": model_key, "error": str(error)})
            continue
        counts["modelsDecoded"] += 1
        search_paths = mdl.search_paths(data)

        by_material: dict[str, SurfaceSamples] = defaultdict(SurfaceSamples)
        for mesh in meshes:
            samples = by_material[str(mesh.material).casefold()]
            for triangle in mesh.tris:
                samples.add_triangle(mesh.verts, triangle)

        for texture in _texture_records(data):
            samples = by_material.get(str(texture["name"]).casefold())
            if samples is None:
                counts["textureSlotsWithoutLod0Geometry"] += 1
                continue
            runtime_dimensions = _texture_dimensions(
                str(texture["name"]), search_paths,
                lambda path: install.read(index, path), "runtime", dimension_cache
            )
            retail_dimensions = _texture_dimensions(
                str(texture["name"]), search_paths,
                lambda path: install.read(retail_index, path), "retail", dimension_cache
            )
            dimensions = (
                runtime_dimensions if model_source == "loose"
                else retail_dimensions or runtime_dimensions
            )
            summaries = samples.summaries()
            for label, candidate_dimensions in (
                ("selected", dimensions),
                ("runtime", runtime_dimensions),
                ("retail", retail_dimensions),
            ):
                if not candidate_dimensions:
                    continue
                dimensioned = samples.summaries(
                    candidate_dimensions["width"], candidate_dimensions["height"]
                )
                summaries.update(
                    {
                        f"{label}.{name}": value
                        for name, value in dimensioned.items()
                        if "Texel" in name
                    }
                )
            if not summaries:
                counts["textureSlotsDegenerate"] += 1
                continue
            stored = float(texture["stored"])
            errors = {
                name: error
                for name, candidate in summaries.items()
                if (error := _relative_error(stored, candidate)) is not None
            }
            rows.append(
                {
                    "model": model_key,
                    "modelSource": model_source,
                    **texture,
                    "compilerTexture": dimensions,
                    "runtimeTexture": runtime_dimensions,
                    "retailTexture": retail_dimensions,
                    "triangles": len(samples.world_areas),
                    "candidates": summaries,
                    "relativeErrors": errors,
                }
            )
            counts["textureSlotsCompared"] += 1

    candidate_errors: dict[str, list[float]] = defaultdict(list)
    for row in rows:
        for name, error in row["relativeErrors"].items():
            candidate_errors[name].append(error)

    scores = []
    for name, errors in candidate_errors.items():
        if not errors:
            continue
        scores.append(
            {
                "candidate": name,
                "records": len(errors),
                "medianRelativeError": statistics.median(errors),
                "meanRelativeError": statistics.fmean(errors),
                "within1Percent": sum(error <= 0.01 for error in errors),
                "within5Percent": sum(error <= 0.05 for error in errors),
                "within10Percent": sum(error <= 0.10 for error in errors),
            }
        )
    scores.sort(key=lambda row: (row["medianRelativeError"], row["meanRelativeError"]))
    return {
        "counts": dict(counts),
        "candidateScores": scores,
        "faults": faults,
        "rows": rows,
    }


def summarize(report: dict[str, Any], top: int) -> str:
    counts = report["counts"]
    lines = [
        "texture metric audit",
        f"  models considered {counts.get('modelsConsidered', 0):,}; "
        f"decoded {counts.get('modelsDecoded', 0):,}; "
        f"missing pair {counts.get('modelsWithoutPair', 0):,}; "
        f"decode faults {counts.get('modelsDecodeFailed', 0):,}; "
        f"texture slots compared {counts.get('textureSlotsCompared', 0):,}",
        "",
        "candidate ranking:",
    ]
    for row in report["candidateScores"][:top]:
        total = row["records"]
        lines.append(
            f"  {row['candidate']:<46} median {row['medianRelativeError']:.6f}  "
            f"mean {row['meanRelativeError']:.6f}  "
            f"<=1% {row['within1Percent']}/{total}  "
            f"<=5% {row['within5Percent']}/{total}"
        )
    lines.extend(("", "best per-record examples:"))
    best_name = report["candidateScores"][0]["candidate"] if report["candidateScores"] else None
    if best_name:
        examples = sorted(
            (row for row in report["rows"] if best_name in row["relativeErrors"]),
            key=lambda row: row["relativeErrors"][best_name],
        )[:top]
        for row in examples:
            lines.append(
                f"  {row['model']} :: {row['name']}  stored={row['stored']:.9g}  "
                f"candidate={row['candidates'][best_name]:.9g}  "
                f"relerr={row['relativeErrors'][best_name]:.6g}  tris={row['triangles']}"
            )
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", action="append", default=[])
    parser.add_argument("--limit", type=int)
    parser.add_argument("--top", type=int, default=12)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()

    report = audit(args.model, args.limit)
    destination = args.report or (research_root() / REPORT_NAME)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(summarize(report, args.top))
    print(f"\n  report: {destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
