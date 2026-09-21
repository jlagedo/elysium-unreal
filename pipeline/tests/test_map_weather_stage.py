"""The weather stage (0018 story 21-4): the rain cover, built from units rather than the decoder.

`formats/weather.py`'s own contract -- the R16 encoding, the sentinel, the emitter document -- is
`test_weather_contract.py`'s and is unchanged by this story. What is new is where the cover comes
from: the world scene the map-geometry stage already meshes, and each solid prop's LOD0 out of its
model unit, in place of the decoder's OBJ builder and `shared/props/<stem>.obj`.

The corpus cases pin `sm_hub_1`, the only map in the install that ships rain, against the numbers
the decoder produced for it (skipped without a V2 export root). The raster's BYTES are deliberately
not pinned: `importers.map_weather`'s docstring records why -- a quarter of the cover triangles are
near edge-on seen from above, so the binary32 vertex recovery moves their interpolated height at a
fraction of a percent of texels. What must hold is the cover set, the coverage and the bounds.
"""
from __future__ import annotations

import numpy as np
import pytest

from elysium_pipeline import paths
from elysium_pipeline.importers import map_weather as weather_lane


#: The map that ships rain, and the only one. Every other map stages `None`.
RAIN_MAP = "sm_hub_1"

#: Measured against the decoder's own `sm_hub_1.weather.json`, 2026-09-21. `verify_sm_hub_1_weather`
#: pins the footprint these bounds imply to 0.01 cm: 28971.24 x 19639.28.
COVER_PINS = {
    "worldTriangles": 24214,
    "propTriangles": 221357,
    "coverTriangles": 245571,
    "emitters": 2,
    "coveredTexels": 964071,
}
FOOTPRINT_CM = (28971.24, 19639.28)

#: A model the hub places as rain cover, used to pin the unit-path fold and the LOD choice.
SAMPLE_MODEL = "models/scenery/structural/shoplight/shoplightwithbulbs.mdl"


def _export_root():
    try:
        root = paths.export_v2_root()
    except RuntimeError:
        pytest.skip("V2 export root not configured")
    if not (root / "maps").is_dir():
        pytest.skip("V2 map units unavailable")
    return root


def test_a_model_path_resolves_to_its_published_unit():
    """A `staticProps` placement names `models/<path>.mdl`; the unit lives at `models/<path>.glb`
    below the export_v2 root. The fold is the model seam's own `normalize_model_key`, so this lane
    invents no second naming rule."""

    root = _export_root()
    path = weather_lane._unit_path(root, SAMPLE_MODEL)
    assert path == root / "models" / "scenery/structural/shoplight/shoplightwithbulbs.glb"
    assert path.is_file()
    assert weather_lane._unit_path(root, SAMPLE_MODEL.upper()) == path


def test_a_missing_model_unit_fails_the_stage_rather_than_dropping_cover():
    """`formats.weather` raised on a missing `shared/props/<stem>.obj` for the same reason: a
    rain-blocking prop that silently contributes nothing puts rain through a roof."""

    root = _export_root()
    meshes = weather_lane.ModelMeshes(root)
    with pytest.raises(weather_lane.MapWeatherError, match="missing its model unit"):
        meshes.triangles("models/nothing/at/all.mdl")


def test_lod0_triangles_are_the_mesh_the_corpus_obj_carried():
    """164 triangles is what `UE_extract_corpus.decode_prop_models` wrote into
    `shared/props/models_scenery_structural_shoplight_shoplightwithbulbs.obj`, whose vertices the
    unit's LOD0 reproduces to 2.6e-6 cm. Picking LOD0 rather than any other VTX LOD is the whole
    of the model-side port."""

    root = _export_root()
    triangles = weather_lane.ModelMeshes(root).triangles(SAMPLE_MODEL)
    assert triangles.shape == (164, 3, 3)
    # Centimetres, not metres: the glTF-metre scale is applied exactly once.
    flat = triangles.reshape(-1, 3)
    extent = flat.max(axis=0) - flat.min(axis=0)
    assert 10.0 < float(extent.max()) < 1000.0


def test_only_the_rain_map_stages_a_payload():
    _export_root()
    assert weather_lane.stage_map("sp_tutorial_1") is None
    assert weather_lane.stage_map("sm_pawnshop_1") is None


def test_the_rain_map_reproduces_the_decoder_s_cover_set():
    _export_root()
    payload = weather_lane.stage_map(RAIN_MAP)
    assert payload is not None
    assert payload["counts"] == COVER_PINS
    assert payload["document"]["schema"] == "elysium.map-weather"
    assert payload["document"]["version"] == 1
    assert payload["heightFile"] == weather_lane.HEIGHT_NAME
    assert len(payload["sha256"]) == 64


def test_the_footprint_the_bake_pins_survives_the_port():
    _export_root()
    bounds = weather_lane.stage_map(RAIN_MAP)["document"]["world_bounds_cm"]
    footprint = (bounds["max"][0] - bounds["min"][0], bounds["max"][1] - bounds["min"][1])
    assert footprint[0] == pytest.approx(FOOTPRINT_CM[0], abs=0.01)
    assert footprint[1] == pytest.approx(FOOTPRINT_CM[1], abs=0.01)


def test_the_bounds_are_the_world_scene_s_and_no_prop_widens_them():
    """The decoder took its AABB over the world vertices BEFORE appending prop cover, so a prop
    hanging outside the world's extent cannot stretch the raster. Measured: the hub's prop cover
    does reach past the world on every axis, so this is a live rule, not a formality."""

    root = _export_root()
    from elysium_pipeline.importers import map_geometry

    geometry = map_geometry.read_geometry(RAIN_MAP)
    bounds = weather_lane.stage_map(RAIN_MAP)["document"]["world_bounds_cm"]
    world = np.asarray(geometry.world.positions, dtype=np.float64)
    assert bounds["min"] == pytest.approx(world.min(axis=0).tolist(), abs=1e-6)
    assert bounds["max"] == pytest.approx(world.max(axis=0).tolist(), abs=1e-6)

    props = np.concatenate(
        weather_lane.prop_cover(geometry, weather_lane.ModelMeshes(root))).reshape(-1, 3)
    assert (props.min(axis=0) < world.min(axis=0)).any()
    assert (props.max(axis=0) > world.max(axis=0)).any()


def test_water_refract_and_additive_groups_are_not_cover():
    """The decoder dropped all three before rasterising -- rain falls through a canal and through
    an additive glow card. The counts differ, which is what proves the filter ran."""

    _export_root()
    from elysium_pipeline.exporters import UE_map_sidecars as producer
    from elysium_pipeline.importers import map_geometry

    geometry = map_geometry.read_geometry(RAIN_MAP)
    kept = sum(len(block) for block in
               weather_lane.world_cover(geometry, producer.MaterialUnits()))
    assert kept == COVER_PINS["worldTriangles"]
    assert kept < geometry.world.tri_count


def test_only_solid_non_sky_props_are_cover():
    """The placement record decides both, not the model: `solid` is vbsp's own `SOLID_*` word and
    `sky` is the 3D-skybox area rule. `formats.weather` read exactly these two fields off the
    legacy `.props` line."""

    root = _export_root()
    from elysium_pipeline.importers import map_geometry

    geometry = map_geometry.read_geometry(RAIN_MAP)
    meshes = weather_lane.ModelMeshes(root)
    chosen = [p for p in geometry.placements if p.solid_blocks and not p.sky]
    assert 0 < len(chosen) < len(geometry.placements)
    expected = sum(len(meshes.triangles(p.model_path)) for p in chosen)
    assert expected == COVER_PINS["propTriangles"]
