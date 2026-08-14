"""Map weather sidecar and top-down rain-cover height rasterization."""

from __future__ import annotations

import json
import math
from pathlib import Path
from typing import Iterable

import numpy as np
from PIL import Image

from elysium_pipeline import shared_corpus
from elysium_pipeline.formats.particles import compile_closure


WEATHER_SCHEMA = "elysium.map-weather"
WEATHER_VERSION = 1
HEIGHT_RESOLUTION = 2048


def _obj_triangles(path: Path) -> list[tuple[tuple[float, float, float], ...]]:
    """Read the exporter-owned, already Unreal-native prop OBJ subset."""

    vertices: list[tuple[float, float, float]] = []
    triangles: list[tuple[tuple[float, float, float], ...]] = []
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        fields = raw.split()
        if not fields:
            continue
        if fields[0] == "v" and len(fields) >= 4:
            vertices.append(tuple(float(value) for value in fields[1:4]))
        elif fields[0] == "f" and len(fields) >= 4:
            indices = [int(value.split("/", 1)[0]) - 1 for value in fields[1:]]
            for offset in range(1, len(indices) - 1):
                triangles.append((
                    vertices[indices[0]], vertices[indices[offset]],
                    vertices[indices[offset + 1]],
                ))
    return triangles


def _quaternion_matrix(values: tuple[float, float, float, float]) -> np.ndarray:
    x, y, z, w = values
    length = math.sqrt(x * x + y * y + z * z + w * w)
    if length < 1e-8:
        raise ValueError("static-prop cover quaternion has zero length")
    x, y, z, w = (value / length for value in (x, y, z, w))
    return np.asarray([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
    ], dtype=np.float64)


def static_prop_cover_triangles(
    out_dir: Path, map_name: str
) -> list[tuple[tuple[float, float, float], ...]]:
    """Resolve solid, non-sky static props into placed Unreal-space rain cover."""

    placements = out_dir / f"{map_name}.props"
    if not placements.is_file():
        return []
    models: dict[str, list[tuple[tuple[float, float, float], ...]]] = {}
    result: list[tuple[tuple[float, float, float], ...]] = []
    for line_number, raw in enumerate(
        placements.read_text(encoding="utf-8", errors="replace").splitlines(), 1
    ):
        fields = raw.split()
        if not fields:
            continue
        # Eleven placement fields, plus the install model path the corpus join added.
        if len(fields) not in (11, 12):
            raise ValueError(f"{placements}:{line_number}: expected 11 static-prop fields")
        stem = fields[0]
        origin = np.asarray([float(value) for value in fields[1:4]], dtype=np.float64)
        rotation = _quaternion_matrix(tuple(float(value) for value in fields[4:8]))
        solid, sky = int(fields[8]), int(fields[10])
        if not solid or sky:
            continue
        if stem not in models:
            # The mesh is the shared corpus's -- a map holds no props directory of its own.
            model_path = shared_corpus.props_dir(out_dir.parent) / f"{stem}.obj"
            if not model_path.is_file():
                raise ValueError(f"rain-blocking static prop is missing {model_path}")
            models[stem] = _obj_triangles(model_path)
        for triangle in models[stem]:
            placed = np.asarray(triangle, dtype=np.float64) @ rotation.T + origin
            result.append(tuple(tuple(float(value) for value in point) for point in placed))
    return result


def _float_key(entity, name: str, default: float = 0.0) -> float:
    try:
        value = float(entity.get("keys", {}).get(name, default))
    except (TypeError, ValueError) as exc:
        raise ValueError(f"{entity.get('classname')} {entity.get('targetname')}: invalid {name}") from exc
    if not math.isfinite(value):
        raise ValueError(f"{entity.get('classname')} {entity.get('targetname')}: non-finite {name}")
    return value


def _rain_entities(document: dict) -> list[dict]:
    return [
        entity for entity in document.get("entities", [])
        if entity.get("classname", "").lower() == "env_particle"
        and entity.get("keys", {}).get("particle_definition", "").lower()
        == "rain_follow_emitter"
    ]


def build_weather_document(
    map_name: str,
    entity_document: dict,
    idx,
    bounds_min: tuple[float, float, float],
    bounds_max: tuple[float, float, float],
    height_metadata: dict,
) -> dict | None:
    from elysium_pipeline.formats import install

    rain = _rain_entities(entity_document)
    if not rain:
        return None

    roots = sorted({entity["keys"]["particle_definition"].lower() for entity in rain})

    def read_definition(name: str) -> str | None:
        raw = install.read(idx, f"particles/{name}.txt")
        return raw.decode("latin-1") if raw is not None else None

    closure = compile_closure(
        roots,
        read_definition,
        lambda name: install.read(idx, f"particles/{name}.tga") is not None,
    )
    world = next(
        (entity for entity in entity_document.get("entities", [])
         if entity.get("classname", "").lower() == "worldspawn"),
        {"keys": {}},
    )
    emitters = []
    for ordinal, entity in enumerate(rain):
        keys = entity.get("keys", {})
        emitters.append({
            "ordinal": ordinal,
            "targetname": entity.get("targetname", ""),
            "origin_cm": entity.get("origin", [0.0, 0.0, 0.0]),
            "particle_definition": keys["particle_definition"].lower(),
            "active": bool(int(keys.get("active", "0"))),
            "attach_type": int(keys.get("attach_type", "0")),
            "bounds_cm": _float_key(entity, "bounds") * 2.54,
            "ramp_scale": _float_key(entity, "ramp_scale", 1.0),
            "ramp_time_seconds": _float_key(entity, "ramp_time"),
        })

    timers = {}
    for name in ("rain_on_timer", "rain_off_timer"):
        timer = next(
            (entity for entity in entity_document.get("entities", [])
             if entity.get("classname", "").lower() == "logic_timer"
             and entity.get("targetname", "").lower() == name),
            None,
        )
        if timer:
            timers[name] = {
                "start_disabled": bool(int(timer.get("keys", {}).get("StartDisabled", "0"))),
                "random": bool(int(timer.get("keys", {}).get("UseRandomTime", "0"))),
                "lower_seconds": _float_key(timer, "LowerRandomBound"),
                "upper_seconds": _float_key(timer, "UpperRandomBound"),
            }

    return {
        "schema": WEATHER_SCHEMA,
        "version": WEATHER_VERSION,
        "map": map_name,
        "world_bounds_cm": {"min": list(bounds_min), "max": list(bounds_max)},
        "height_texture": height_metadata,
        "wetness": {
            "initial": _float_key(world, "wetness_fadetarget"),
            "fadein_seconds": _float_key(world, "wetness_fadein"),
            "fadeout_seconds": _float_key(world, "wetness_fadeout"),
            "fadetarget": _float_key(world, "wetness_fadetarget"),
        },
        "emitters": emitters,
        "timers": timers,
        "particles": closure,
        "hypotheses": {
            "attach_type_11": "viewer-follow; retail validation required",
            "bounds": "component culling extent; retail validation required",
        },
    }


def _raster_triangle(height: np.ndarray, triangle: np.ndarray,
                     bounds_min: np.ndarray, bounds_max: np.ndarray) -> None:
    resolution = height.shape[0]
    span = bounds_max[:2] - bounds_min[:2]
    xy = (triangle[:, :2] - bounds_min[:2]) / span * (resolution - 1)
    min_x = max(0, int(math.floor(float(xy[:, 0].min()))))
    max_x = min(resolution - 1, int(math.ceil(float(xy[:, 0].max()))))
    min_y = max(0, int(math.floor(float(xy[:, 1].min()))))
    max_y = min(resolution - 1, int(math.ceil(float(xy[:, 1].max()))))
    if min_x > max_x or min_y > max_y:
        return
    a, b, c = xy
    denominator = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
    if abs(float(denominator)) < 1e-8:
        return
    xs = np.arange(min_x, max_x + 1, dtype=np.float32) + 0.5
    ys = np.arange(min_y, max_y + 1, dtype=np.float32) + 0.5
    gx, gy = np.meshgrid(xs, ys)
    w0 = ((b[1] - c[1]) * (gx - c[0]) + (c[0] - b[0]) * (gy - c[1])) / denominator
    w1 = ((c[1] - a[1]) * (gx - c[0]) + (a[0] - c[0]) * (gy - c[1])) / denominator
    w2 = 1.0 - w0 - w1
    inside = (w0 >= -1e-5) & (w1 >= -1e-5) & (w2 >= -1e-5)
    if not inside.any():
        return
    z = w0 * triangle[0, 2] + w1 * triangle[1, 2] + w2 * triangle[2, 2]
    region = height[min_y:max_y + 1, min_x:max_x + 1]
    np.maximum(region, np.where(inside, z, -np.inf), out=region)


def rasterize_height(
    triangles: Iterable[tuple[tuple[float, float, float], ...]],
    path: Path,
    bounds_min: tuple[float, float, float],
    bounds_max: tuple[float, float, float],
    *,
    resolution: int = HEIGHT_RESOLUTION,
) -> dict:
    """Write an R16 maximum-height map. Zero is the explicit uncovered sentinel."""

    bmin = np.asarray(bounds_min, dtype=np.float64)
    bmax = np.asarray(bounds_max, dtype=np.float64)
    if resolution <= 0 or np.any(bmax <= bmin):
        raise ValueError("rain height bounds and resolution must be positive")
    height = np.full((resolution, resolution), -np.inf, dtype=np.float32)
    triangle_count = 0
    for triangle in triangles:
        tri = np.asarray(triangle, dtype=np.float64)
        if tri.shape != (3, 3) or not np.isfinite(tri).all():
            raise ValueError("rain height triangle must contain three finite XYZ vertices")
        _raster_triangle(height, tri, bmin, bmax)
        triangle_count += 1
    covered = np.isfinite(height)
    encoded = np.zeros_like(height, dtype=np.uint16)
    z_min, z_max = float(bmin[2]), float(bmax[2])
    z_scale = (z_max - z_min) / 65534.0
    if covered.any():
        encoded[covered] = np.clip(
            np.rint((height[covered] - z_min) / z_scale) + 1.0, 1, 65535
        ).astype(np.uint16)
    path.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(encoded).save(path)
    return {
        "path": path.name,
        "resolution": resolution,
        "format": "R16_UNORM",
        "sentinel": 0,
        "decoded_height_cm": "min_z_cm + (sample - 1) * z_scale_cm",
        "min_z_cm": z_min,
        "z_scale_cm": z_scale,
        "triangle_count": triangle_count,
        "covered_texels": int(covered.sum()),
    }


def write_weather(
    map_name: str,
    out_dir: Path,
    entity_document: dict,
    idx,
    triangles: Iterable[tuple[tuple[float, float, float], ...]],
    bounds_min: tuple[float, float, float],
    bounds_max: tuple[float, float, float],
) -> Path | None:
    if not _rain_entities(entity_document):
        return None
    texture_path = out_dir / "weather" / "rain_height.png"
    height = rasterize_height(triangles, texture_path, bounds_min, bounds_max)
    height["path"] = "weather/rain_height.png"
    document = build_weather_document(
        map_name, entity_document, idx, bounds_min, bounds_max, height
    )
    destination = out_dir / f"{map_name}.weather.json"
    destination.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return destination
