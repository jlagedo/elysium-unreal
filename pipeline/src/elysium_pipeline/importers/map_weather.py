"""Stage one map's outdoor-rain contract for the bake (0018 story 21-4).

VtMB's rain is two halves. The **entity half** -- the `env_particle` emitters, their definition
closure, the `logic_timer`s that switch them and `worldspawn`'s wetness fade -- has run on the
producer's own `.ents` since R3.5 and is unchanged here; it is `formats.weather`'s
``build_weather_document``. The **geometry half** is what this module ports: the rain-cover height
map, a top-down R16 raster of the highest surface over every point of the map, so the runtime can
stop a raindrop under a roof.

Until this story that half was the BSP decoder's: it summed the world scene's triangles straight
out of its own OBJ builder and read each solid prop's mesh from ``shared/props/<stem>.obj``, then
handed the result across to ``formats.weather.write_weather`` through ``export_all``'s
``weather_inputs``. Both inputs are published: the world scene is the map-geometry stage's own
``Scene``, and a prop's mesh is its model unit's LOD0.

**The cover set is the decoder's, filter for filter** (``UE_bsp_to_scene`` 1691-1709): the world
scene only -- not the 3D-skybox miniature, not a brush model -- minus water groups, minus
``$refract`` groups, minus ``$additive`` groups, plus every solid non-sky static prop placed at
its own transform. The bounds are the world scene's own vertex AABB, taken BEFORE the prop
triangles join, because a prop outside the world's extent must not widen the raster.

**The world triangles are the meshed scene's, not a second triangulation.** The decoder's cover
set *was* its meshed world scene, and `seam_map_map.md` already states the rule this follows:
"two implementations of which faces are the miniature is exactly the divergence R3.3 exists to
catch". So the geometry half consumes `map_geometry`'s `Scene` rather than re-deriving one in the
producer, which is where the decal projector had to go because it needs a face class the mesh
deliberately drops.

**Measured against the decoder on `sm_hub_1`, the only map that ships rain, 2026-09-21.** The
inputs reproduce: 245,571 cover triangles either way (24,214 world, 221,357 prop), the same 278
world material groups keeping the same 24,214 triangles under the three-way filter, world
vertices within 7.9e-4 cm of the decoder's and prop vertices within 4.0e-4 cm, and bounds within
6e-4 cm -- so the pinned footprint 28971.24 x 19639.28 cm holds. The raster's COVERAGE reproduces
exactly: 964,071 covered texels, zero sentinel disagreements.

**The encoded heights do not, on 0.75% of samples**, and the reason is a precision limit rather
than a decision: 31,614 of 4,194,304 samples differ, ±1 to ±4 LSB (0.25-1 cm) for the bulk and
156 LSB (38.8 cm) at the tail. Re-rasterising with the decoder's own bounds moves the count by
460, so the quantisation is not the cause -- the vertices are. **27.65% of the cover triangles
are near edge-on seen from above** (a sub-texel footprint with a median 22.7 cm and up to 15.8 m
of vertical extent; a further 42,719 are fully degenerate and the rasteriser skips them), and for
those the barycentric denominator is ill-conditioned, so a sub-millimetre shift in a vertex moves
the interpolated height by decimetres and flips which triangle wins the max at an edge texel.
That shift is the binary32 recovery rule `source_position` states and `displacement_triangles`
already names for `.dispcol`. It is a rain-occlusion height map, read by a material to fade a
raindrop: the difference is named here and in `seam_map_map.md` rather than chased.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any, Sequence

import numpy as np

from elysium_pipeline.exporters import UE_map_sidecars as producer
from elysium_pipeline.formats import weather
from elysium_pipeline.formats.unit_contract import read_glb

# `formats.install` resolves `ELYSIUM_VTMB_ROOT` at import time, and the particle closure is the
# only thing here that needs the install, so it is imported where it is used: a map with no rain
# must stage without an install configured.

#: 1: the first recipe (0018 story 21-4).
RECIPE_VERSION = 1

#: A model unit publishes one mesh per VTX LOD, named `<path>:lod<n>`. Cover is LOD0's, which is
#: the mesh `UE_extract_corpus.decode_prop_models` wrote into `shared/props/<stem>.obj`.
LOD0_SUFFIX = ":lod0"

_ACCESSOR_COMPONENT = {5120: "b", 5121: "B", 5122: "h", 5123: "H", 5125: "I", 5126: "f"}
_ACCESSOR_WIDTH = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}


class MapWeatherError(ValueError):
    """A map states rain the stage cannot build its cover for."""


def _accessor(document: dict[str, Any], binary: bytes, index: int) -> np.ndarray:
    accessor = document["accessors"][index]
    view = document["bufferViews"][accessor["bufferView"]]
    start = int(view.get("byteOffset", 0)) + int(accessor.get("byteOffset", 0))
    width = _ACCESSOR_WIDTH[accessor["type"]]
    dtype = np.dtype(_ACCESSOR_COMPONENT[accessor["componentType"]]).newbyteorder("<")
    flat = np.frombuffer(binary, dtype=dtype, count=accessor["count"] * width, offset=start)
    return flat.reshape(accessor["count"], width)


def _unit_path(root: Path, model_path: str) -> Path:
    """The published model unit for one install `.mdl` path, through the seam's own key rule."""

    from elysium_pipeline.formats.model_glb import model as model_unit
    from elysium_pipeline.importers import models as model_lane

    # `normalize_model_key` already tolerates the `models/` root and the `.mdl` extension, which
    # is exactly the spelling a `staticProps` placement carries.
    return model_lane.unit_path_for(root, model_unit.normalize_model_key(model_path))


class ModelMeshes:
    """Each model unit's LOD0 triangles, in the bake's frame, read once per model."""

    def __init__(self, root: Path) -> None:
        self._root = Path(root)
        self._cache: dict[str, np.ndarray] = {}

    def triangles(self, model_path: str) -> np.ndarray:
        """`(n, 3, 3)` LOD0 triangles for one install `.mdl` path, Unreal centimetres."""

        if model_path in self._cache:
            return self._cache[model_path]
        path = _unit_path(self._root, model_path)
        if not path.is_file():
            raise MapWeatherError(f"rain-blocking static prop is missing its model unit: {path}")
        document, binary = read_glb(path)
        mesh = next(
            (row for row in document.get("meshes") or ()
             if str(row.get("name") or "").endswith(LOD0_SUFFIX)),
            None,
        )
        if mesh is None:
            raise MapWeatherError(f"{path} publishes no {LOD0_SUFFIX!r} mesh")
        corners: list[np.ndarray] = []
        for primitive in mesh.get("primitives") or ():
            positions = _accessor(document, binary, primitive["attributes"]["POSITION"])
            indices = _accessor(document, binary, primitive["indices"])[:, 0]
            corners.append(positions[indices])
        from elysium_pipeline.importers.map_geometry import GLTF_TO_UNREAL

        stacked = (
            np.concatenate(corners) if corners else np.zeros((0, 3), dtype=np.float32)
        ).astype(np.float64)
        # `map_geometry.gltf_position_to_unreal`, vectorised: glTF metres, Y-up -> Unreal
        # centimetres, Z-up. The axis swap carries the handedness flip, so there is no negation.
        unreal = np.stack(
            [stacked[:, 0], stacked[:, 2], stacked[:, 1]], axis=1) * GLTF_TO_UNREAL
        self._cache[model_path] = unreal.reshape(-1, 3, 3)
        return self._cache[model_path]


def _quaternion_matrix(values: Sequence[float]) -> np.ndarray:
    """A placement's Unreal quaternion as its rotation matrix, `formats.weather`'s own."""

    return weather._quaternion_matrix(tuple(float(value) for value in values))


def world_cover(geometry: Any, materials: producer.MaterialUnits) -> list[np.ndarray]:
    """The world scene's cover triangles, under the decoder's own three-way material filter."""

    from elysium_pipeline.importers.map_geometry import split_section_key

    positions = np.asarray(geometry.world.positions, dtype=np.float64)
    kept: list[np.ndarray] = []
    for key, indices in geometry.world.groups.items():
        # The decoder read its three flags off the group's BASE material's own VMT record
        # (`resolve_record` prefers the base over the patched name), so the R7.4 section suffixes
        # and the `@<cubemap>` decoration come off before the unit is asked.
        base = split_section_key(key)[0].split("@", 1)[0]
        flags = materials.render_flags(base)
        if flags["water"] or flags["refract"] or flags["additive"]:
            continue
        kept.append(positions[np.asarray(indices, dtype=np.int64)].reshape(-1, 3, 3))
    return kept


def prop_cover(geometry: Any, meshes: ModelMeshes) -> list[np.ndarray]:
    """Every solid, non-sky static prop's LOD0 triangles, placed at its own transform.

    The placement record is the authority on both tests, not the model: `solid` is vbsp's own
    `SOLID_*` word and `sky` is the 3D-skybox area rule. `formats.weather` read exactly these two
    fields out of the legacy `.props` line (fields 8 and 10); the staged `Placement` carries them
    as `solid_blocks` and `sky`.
    """

    placed: list[np.ndarray] = []
    for placement in geometry.placements:
        if not placement.solid_blocks or placement.sky:
            continue
        triangles = meshes.triangles(placement.model_path)
        if not len(triangles):
            continue
        rotation = _quaternion_matrix(placement.rotation)
        origin = np.asarray(placement.position, dtype=np.float64)
        placed.append(triangles @ rotation.T + origin)
    return placed


def stage_for_geometry(
    geometry: Any,
    map_name: str,
    *,
    root: Path | None = None,
    index: Any = None,
    materials: producer.MaterialUnits | None = None,
) -> dict[str, Any] | None:
    """One map's whole weather payload, or `None` when the map states no rain.

    The raster is built HERE rather than in the bake because Unreal's embedded CPython carries
    neither numpy nor Pillow; the bake reads the encoded bytes and imports them as `T_RainHeight`.
    """

    from elysium_pipeline import paths

    root = Path(root) if root is not None else paths.export_v2_root()
    materials = materials if materials is not None else producer.MaterialUnits(root)

    # The same document `write_entities` writes to `<map>.ents`, built rather than read: R3.5
    # already re-ran the weather half against the PRODUCER's entities rather than the decoder's,
    # so this is that join without the file in between.
    rows, _stats = producer.build_entities(
        geometry.join.units, geometry.join.sky, geometry.join.pair_blocks,
        geometry.join.brush_meshes,
    )
    entity_document = {"map": map_name, "entities": rows}
    if not weather._rain_entities(entity_document):
        return None

    positions = np.asarray(geometry.world.positions, dtype=np.float64)
    if not len(positions):
        raise MapWeatherError(f"{map_name} states rain but its world scene has no geometry")
    bounds_min = tuple(float(value) for value in positions.min(axis=0))
    bounds_max = tuple(float(value) for value in positions.max(axis=0))

    cover = world_cover(geometry, materials)
    world_triangles = int(sum(len(block) for block in cover))
    cover += prop_cover(geometry, ModelMeshes(root))
    triangles = [
        tuple(tuple(float(value) for value in corner) for corner in triangle)
        for block in cover for triangle in block
    ]

    staging = _height_dir(map_name)
    metadata = weather.rasterize_height(
        triangles, staging, bounds_min, bounds_max)
    metadata["path"] = HEIGHT_NAME
    from elysium_pipeline.formats import install

    document = weather.build_weather_document(
        map_name, entity_document,
        index if index is not None else install.build_index(verbose=False),
        bounds_min, bounds_max, metadata,
    )
    payload = {
        "version": RECIPE_VERSION,
        "map": map_name,
        "counts": {
            "worldTriangles": world_triangles,
            "propTriangles": len(triangles) - world_triangles,
            "coverTriangles": len(triangles),
            "emitters": len(document["emitters"]),
            "coveredTexels": metadata["covered_texels"],
        },
        "document": document,
        # The raster is a file beside the manifest, not bytes inside it: it is a 2048 R16 PNG and
        # the bake imports it by path. `height_png` reads it back for a caller that wants the
        # bytes (the parity probe does).
        "heightFile": HEIGHT_NAME,
    }
    payload["sha256"] = hashlib.sha256(
        json.dumps(payload, sort_keys=True, separators=(",", ":")).encode()
        + staging.read_bytes()).hexdigest()
    return payload


def height_png(map_name: str) -> bytes:
    """The staged raster's bytes, for a caller comparing them rather than importing them."""

    return _height_dir(map_name).read_bytes()


#: The staged raster's file name, beside the geometry manifest; the bake imports it from there.
HEIGHT_NAME = "rain_height.png"


def _height_dir(map_name: str) -> Path:
    from elysium_pipeline.importers import map_geometry

    out = map_geometry.staging_dir(map_name)
    out.mkdir(parents=True, exist_ok=True)
    return out / HEIGHT_NAME


def stage_map(map_name: str, root: Path | None = None) -> dict[str, Any] | None:
    """The payload for one map straight from its units, meshing its world scene on the way."""

    from elysium_pipeline.importers import map_geometry

    geometry = map_geometry.read_geometry(map_name, root)
    return stage_for_geometry(geometry, map_name, root=root)
