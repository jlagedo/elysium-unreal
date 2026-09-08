"""The V2 map bake's geometry and placement reader (R5.1).

This states where a map's
world/sky/brush meshes and its static-prop placements come from once a map is on the V2 lane. Two
things in that section can change shipped content without changing any other test: the **frame**
(one reflection and one quaternion conjugation, wrong in a way that is invisible until a whole map
is inside out) and the **placement mapping** (which record field becomes collision, which becomes a
skin, which becomes a cull distance).

The first two cases pin those against arithmetic. The last two pin the reader against the legacy
exporter's own output on the real corpus -- the only witness that "the producer changed" and "the
geometry changed" are separable -- and skip, loudly, when that corpus is not on this machine.
"""
from __future__ import annotations

from collections import Counter
import json
from pathlib import Path
import re
from types import SimpleNamespace

import numpy as np
import pytest

from elysium_pipeline import asset_names, map_transport, paths, shared_corpus
from elysium_pipeline.formats.bsp import source_quat_to_unreal, source_to_unreal
from elysium_pipeline.importers import map_geometry as MG


def _bake_map_v2_manifest_version() -> int:
    """`bake_map_v2.MANIFEST_VERSION`, read as text: that module imports `unreal`."""
    source = (Path(__file__).resolve().parents[1] / "unreal" / "bake_map_v2.py").read_text(
        encoding="utf-8")
    found = re.search(r"^MANIFEST_VERSION = (\d+)$", source, re.MULTILINE)
    assert found, "bake_map_v2.py states no MANIFEST_VERSION"
    return int(found.group(1))

#: The three-map working corpus (R1); a whole-corpus run is a separate,
#: owner-approved step and this module never asks for one.
WORKING_MAPS = ("sp_tutorial_1", "sm_pawnshop_1", "sm_hub_1")
#: The maps on the V2 model root. R7.1 adds `sm_pier_1` (the water scope) and, on the owner's call
#: of 2026-09-04, `sp_soc_3` -- the deep-water witness (Society of Leopold: a 464-inch
#: `dev_water2_cheap` basin, 40 drip emitters). Both are in `MapsOnV2Models` in
#: `Config/DefaultElysium.ini` and neither is in `WORKING_MAPS`, because the parametrized corpus
#: cases above walk the three-map corpus and neither ruling authorized a wider run of those.
V2_MODEL_MAPS = WORKING_MAPS + ("sm_pier_1", "sp_soc_3", "sp_theatre")


def test_gltf_frame_matches_source_to_unreal_through_the_units_own_transform():
    # The unit publishes `(x, y, z)_gltf = (x, z, -y)_source * 0.0254` and the bake wants
    # `source_to_unreal`. Composing the published transform with this module's inverse has to land
    # exactly on the value the legacy exporter wrote, for positions and for rotations alike.
    for source_point in ((0.0, 0.0, 0.0), (128.0, -64.0, 32.0), (-7831.0, 3759.0, 6441.0)):
        x, y, z = source_point
        gltf = (x * 0.0254, z * 0.0254, -y * 0.0254)
        assert MG.gltf_position_to_unreal(gltf) == pytest.approx(
            source_to_unreal(x, y, z), abs=1e-6)

    for source_quat in ((0.0, 0.0, 0.0, 1.0), (0.0, 0.0, 0.7071067811865476, 0.7071067811865476),
                        (0.1825741858350554, 0.3651483716701108, 0.5477225575051661,
                         0.7302967433402215)):
        qx, qy, qz, qw = source_quat
        gltf = (qx, qz, -qy, qw)
        expected = source_quat_to_unreal(qx, qy, qz, qw)
        actual = MG.gltf_quat_to_unreal(gltf)
        # q and -q name one rotation; the reader is free in the overall sign, nothing else.
        assert (actual == pytest.approx(expected, abs=1e-12)
                or actual == pytest.approx(tuple(-v for v in expected), abs=1e-12))


def test_placement_reads_solid_skin_and_fade_off_the_record_not_the_model():
    def placement(**overrides):
        fields = dict(
            index=0, stem="models_scenery_lamp", model_path="models/scenery/lamp.mdl",
            position=(0.0, 0.0, 0.0), rotation=(0.0, 0.0, 0.0, 1.0),
            solid=6, flags=0, skin=0, fade_min_cm=0.0, fade_max_cm=0.0, sky=False)
        fields.update(overrides)
        return MG.Placement(**fields)

    # SOLID_NONE is the one value that does not block; every other SolidType_t does, including the
    # VPHYSICS placements of models that ship no `.phy`.

    assert placement(solid=MG.SOLID_NONE).solid_blocks is False
    for solid in (1, 2, 3, 4, 5, 6):
        assert placement(solid=solid).solid_blocks is True

    # Fade needs BOTH the flag and a distance: 442 of sp_tutorial_1's 809 records carry (0, 0), and
    # culling those at zero would empty the map.
    assert placement(flags=0x1, fade_max_cm=6350.0).fades is True
    assert placement(flags=0x1, fade_max_cm=0.0).fades is False
    assert placement(flags=0x10, fade_max_cm=6350.0).fades is False
    assert MG.source_inches_to_unreal(2500.0) == pytest.approx(6350.0)


@pytest.mark.parametrize("map_name", WORKING_MAPS)
def test_reader_reproduces_the_legacy_scene_split_on_the_working_corpus(map_name):
    unit = MG.sidecars.unit_paths(map_name)["root"]
    legacy = paths.export_root() / map_name
    world_obj = legacy / f"{map_name}.obj"
    if not unit.is_file():
        pytest.skip(f"no exported map root unit at {unit}")
    if not world_obj.is_file():
        pytest.skip(f"no legacy scene at {world_obj}")

    geometry = MG.read_geometry(map_name)

    for scene, obj_path in ((geometry.world, world_obj),
                            (geometry.sky, legacy / f"{map_name}_sky.obj")):
        if not obj_path.is_file():
            assert scene.tri_count == 0
            continue
        vertices, groups = _read_obj_groups(obj_path)
        assert len(scene.positions) == vertices
        # R7.4 splits a face group further on two per-face facts the legacy OBJ had nowhere to put
        # -- `#underside` and `#style<n>` (`MG.section_key`). Folded back, the split is exactly the
        # legacy one: same faces, same triangles, same material per triangle, only more sections.
        merged: dict[str, int] = {}
        for key, indices in scene.groups.items():
            base, _underside, _style = MG.split_section_key(key)
            merged[base] = merged.get(base, 0) + len(indices) // 3
        assert merged == groups

    # The `WorldVertexTransition` blend channel is the one per-vertex value the OBJ itself does not
    # carry: the unit publishes a `DISP_VERT` alpha as the lump's own 0..255 byte and the bake's
    # vertex COLOR.r is 0..1, so a missing normalization would tint every sculpted surface hard onto
    # tex2 without changing a single triangle count above.
    blend_path = legacy / f"{map_name}.blend"
    if blend_path.is_file():
        legacy_blend = [float(value) for value in blend_path.read_text().split()]
        assert len(geometry.world.blend) == len(legacy_blend)
        # The sidecar prints four decimals, so 1/255 quantisation shows at 5e-5.
        assert geometry.world.blend == pytest.approx(legacy_blend, abs=1e-4)
    else:
        assert not any(geometry.world.blend)

    brush_dir = legacy / "brushes"
    if brush_dir.is_dir():
        assert sorted(geometry.brush_stems().values()) == sorted(
            path.stem for path in brush_dir.glob("brush_*.obj"))


@pytest.mark.parametrize("map_name", WORKING_MAPS)
def test_reader_reproduces_every_legacy_props_row_on_the_working_corpus(map_name):
    unit = MG.sidecars.unit_paths(map_name)["root"]
    props_path = paths.export_root() / map_name / f"{map_name}.props"
    if not unit.is_file():
        pytest.skip(f"no exported map root unit at {unit}")
    if not props_path.is_file():
        pytest.skip(f"no legacy placements at {props_path}")

    placements = MG.read_geometry(map_name).placements
    rows = [line.split() for line in props_path.read_text(
        encoding="utf-8", errors="replace").splitlines() if line.split()]
    assert len(placements) == len(rows)

    for placement, row in zip(placements, rows):
        # `.props` fields: stem, origin (cm), quaternion, solid, skin, sky, model path.
        assert placement.stem == row[0]
        assert placement.stem == shared_corpus.static_stem(placement.model_path)
        assert placement.position == pytest.approx(
            tuple(float(value) for value in row[1:4]), abs=0.01)
        legacy_quat = tuple(float(value) for value in row[4:8])
        assert (placement.rotation == pytest.approx(legacy_quat, abs=2e-4)
                or placement.rotation == pytest.approx(
                    tuple(-value for value in legacy_quat), abs=2e-4))
        assert placement.solid == int(row[8])
        assert placement.skin == int(row[9])
        assert placement.sky is bool(int(row[10]))


# --- R6.3: detail props -----------------------------------------------------------------------------
#
# Every `dprp` record becomes one instance of its
# model's instanced component, in lump order, through the same frame a static prop takes, with the
# record's `swayAmount` carried raw. The first case pins the mapping on a synthetic unit (the
# dictionary join, the order, the frame, the miniature flag, the loud out-of-range failure); the
# corpus case walks the three maps against the root unit's own records.


class _FakeSky:
    def __init__(self, threshold_x):
        self.threshold_x = threshold_x

    def is_sky(self, source_point):
        return source_point[0] > self.threshold_x


def _detail_units(records, dictionary):
    nodes = []
    for record in records:
        record["node"] = len(nodes)
        nodes.append({"translation": record.pop("translation"),
                      "rotation": record.pop("rotation")})
    return SimpleNamespace(
        name="fake_map",
        root={"detailProps": {"dictionary": [{"index": i, "name": name}
                                               for i, name in enumerate(dictionary)],
                              "records": records}},
        document={"nodes": nodes},
    )


def test_detail_records_map_to_instanced_placements_in_lump_order():
    dictionary = ["models\\scenery\\plants\\weedc\\weedc.mdl",
                  "models/scenery/plants/grass/grassa.mdl"]
    # Source (128, -64, 32) publishes as glTF (128, 32, 64) * 0.0254 and a source quaternion
    # (x, y, z, w) as (x, z, -y, w); the sky record sits past x.
    records = [
        {"index": 0, "detailModel": 1, "swayAmount": 0,
         "translation": [128 * 0.0254, 32 * 0.0254, 64 * 0.0254],
         "rotation": [0.0, 0.7071067811865476, 0.0, 0.7071067811865476]},
        {"index": 1, "detailModel": 0, "swayAmount": 37,
         "translation": [0.0, 0.0, 0.0], "rotation": [0.0, 0.0, 0.0, 1.0]},
        {"index": 2, "detailModel": 1, "swayAmount": 255,
         "translation": [9000 * 0.0254, 0.0, 0.0], "rotation": [0.0, 0.0, 0.0, 1.0]},
    ]
    details = MG._detail_placements(_detail_units(records, dictionary), _FakeSky(5000.0))

    assert [detail.index for detail in details] == [0, 1, 2]
    assert [detail.model for detail in details] == [1, 0, 1]
    assert [detail.stem for detail in details] == [
        "models_scenery_plants_grass_grassa", "models_scenery_plants_weedc_weedc",
        "models_scenery_plants_grass_grassa"]
    # A backslashed dictionary entry (sm_hub_1 authors them) folds to the same stem and path.
    assert details[1].model_path == "models/scenery/plants/weedc/weedc.mdl"
    assert details[1].stem == shared_corpus.static_stem("models/scenery/plants/weedc/weedc.mdl")
    # The frame is the static prop's: Source (128, -64, 32) in Unreal centimetres.
    assert details[0].position == pytest.approx(source_to_unreal(128.0, -64.0, 32.0), abs=1e-6)
    expected = source_quat_to_unreal(0.0, 0.0, 0.7071067811865476, 0.7071067811865476)
    assert (details[0].rotation == pytest.approx(expected, abs=1e-12)
            or details[0].rotation == pytest.approx(tuple(-v for v in expected), abs=1e-12))
    # `swayAmount` is carried raw; the bake normalises it.
    assert [detail.sway for detail in details] == [0, 37, 255]
    assert [detail.sky for detail in details] == [False, False, True]

    # A record outside the dictionary is the decoder's own anomaly and never a silent skip.
    broken = [{"index": 0, "detailModel": 5, "swayAmount": 0,
               "translation": [0.0, 0.0, 0.0], "rotation": [0.0, 0.0, 0.0, 1.0]}]
    with pytest.raises(MG.MapGeometryError):
        MG._detail_placements(_detail_units(broken, dictionary), _FakeSky(5000.0))


@pytest.mark.parametrize("map_name", WORKING_MAPS)
def test_reader_places_every_detail_record_of_the_root_unit(map_name):
    unit = MG.sidecars.unit_paths(map_name)["root"]
    if not unit.is_file():
        pytest.skip(f"no exported map root unit at {unit}")

    geometry = MG.read_geometry(map_name)
    block = MG.sidecars.read_units(map_name).root.get("detailProps") or {}
    records = block.get("records") or []
    dictionary = [row["name"] for row in block.get("dictionary") or []]

    # One placement per record, lump order, each resolving to its own dictionary entry.
    assert [detail.index for detail in geometry.details] == [r["index"] for r in records]
    assert geometry.counts["detailProps"] == len(records)
    by_model = Counter(r["detailModel"] for r in records)
    assert Counter(detail.model for detail in geometry.details) == by_model
    models = geometry.detail_models()
    assert {row["model"]: row["count"] for row in models} == dict(by_model)
    for row in models:
        assert row["stem"] == shared_corpus.static_stem(dictionary[row["model"]])
        assert row["stem"] and " " not in row["stem"]
    for detail, record in zip(geometry.details, records):
        assert detail.sway == record["swayAmount"]
        assert 0 <= detail.sway <= 255
        assert detail.sky in (False, True)
    # Every staged row carries the eleven columns the editor half reads positionally.
    assert all(len(MG.detail_record_row(d)) == len(MG.DETAIL_RECORD_FIELDS)
               for d in geometry.details)


def test_the_v2_model_flag_is_its_own_list():
    # The V2 model root is its own ini list, read from its own key rather than derived from the
    # R4.6 entity/collision/environment transport list, so a map joins it only by being named.
    v2 = map_transport.read_array(map_transport.V2_MODELS_KEY)
    transport = map_transport.read_array(map_transport.NEW_TRANSPORT_KEY)
    assert set(v2) == set(V2_MODEL_MAPS)
    assert "sp_theatre" in transport
    assert map_transport.is_map_on_v2_models("sp_theatre") is True
    assert map_transport.is_map_on_v2_models("sm_hub_2") is False
    # Matched case-insensitively, exactly as `ElysiumMapTransport::IsMapOnV2Models` matches.
    assert map_transport.is_map_on_v2_models("SP_Tutorial_1") is True


def _read_obj_groups(path):
    """`(vertex count, {material: triangle count})` for one exported OBJ."""
    groups: dict[str, int] = {}
    vertices = 0
    current = None
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        token = line.split()
        if not token:
            continue
        if token[0] == "v":
            vertices += 1
        elif token[0] == "usemtl":
            current = token[1]
            groups.setdefault(current, 0)
        elif token[0] == "f":
            groups[current] = groups.get(current, 0) + 1
    return vertices, {key: value for key, value in groups.items() if value}


# --- R5.4: the materials table ---------------------------------------------------------------------
#
# Every face group binds the imported `MI_`
# its `vtmb:material:*` unit became, resolved through the material lane's own provenance sidecars.
# The cases below pin the resolution (a patched `maps/<map>/...` id lands on its own map-scoped
# instance, the root master/blend come from the base through `patchBase`), the loud failure for a
# unit the material lane never staged, and the report's classification -- then walk the real
# three-map corpus against the staging tree actually on this machine.


def _sidecars(rows):
    """`unit key -> provenance dict` reader over an in-memory sidecar set."""
    return lambda key: rows.get(key)


_BASE = {
    "master": "/Game/ElysiumGenerated/Materials/V2/M_V2_LitTranslucent", "blendMode": "Translucent",
    "patched": False, "patchBase": None, "proxies": [], "omissions": [],
}
_PATCH = {
    "master": "/Game/ElysiumGenerated/Materials/V2/M_V2_LitTranslucent", "blendMode": None,
    "patched": True, "patchBase": "vtmb:material:brick/window", "proxies": [], "omissions": [],
}


def test_material_table_binds_patched_units_by_their_map_scoped_id_and_roots_them_on_the_base():
    read = _sidecars({
        "brick/window": _BASE,
        "maps/sm_test/brick/window_1_2_3": _PATCH,
        "stone/wall": {**_BASE, "master": "/Game/ElysiumGenerated/Materials/V2/M_V2_Lit",
                       "blendMode": "Masked"},
    })
    table = MG.resolve_material_table({
        "brick/window": "brick/window",
        "brick/window@c_1_2_3": "maps/sm_test/brick/window_1_2_3",
        "stone/wall": "stone/wall",
    }, read, map_name="sm_test")

    patched = table["brick/window@c_1_2_3"]
    assert patched.asset == "/ElysiumBaked/Materials/maps/sm_test/brick/MI_window_1_2_3"
    assert patched.unit == "vtmb:material:maps/sm_test/brick/window_1_2_3"
    assert patched.patched is True
    # The root master and blend are the BASE's, reached through `patchBase`, so the Nanite
    # question is answered exactly as it is for the unpatched surface.
    assert patched.master == "M_V2_LitTranslucent"
    assert patched.blend_mode == "Translucent"
    assert patched.opaque is False
    assert patched.provenance == "brick/window"

    assert table["brick/window"].asset == "/ElysiumBaked/Materials/brick/MI_window"
    assert table["stone/wall"].master == "M_V2_Lit"
    assert table["stone/wall"].opaque is True   # Masked chunks are Nanite-able, like Opaque
    assert table["stone/wall"].as_row()["opaque"] is True


def test_material_table_gates_opaque_on_the_masters_own_nanite_capability():
    # review fix, R5.4: an instance's `blendMode` override cannot make a non-Nanite master's
    # chunk drawable -- `M_V2_Water` and `M_V2_Refract` never set `used_with_nanite`
    # (`make_v2_materials.make_water`/`make_refract`), so a face bound to either master stays
    # off the Nanite path even when the lane forces its instance blend to Opaque (the case
    # `water/sewer_water` and `dev/dev_waterbeneath2` actually hit in sm_hub_1).
    read = _sidecars({
        "water/sewer_water": {**_BASE, "master": "/Game/ElysiumGenerated/Materials/V2/M_V2_Water",
                               "blendMode": "Opaque"},
        "glass/refract_pane": {**_BASE, "master": "/Game/ElysiumGenerated/Materials/V2/M_V2_Refract",
                                "blendMode": "Opaque"},
        "stone/wall": {**_BASE, "master": "/Game/ElysiumGenerated/Materials/V2/M_V2_Lit",
                       "blendMode": "Opaque"},
    })
    table = MG.resolve_material_table({
        "water/sewer_water": "water/sewer_water", "glass/refract_pane": "glass/refract_pane",
        "stone/wall": "stone/wall",
    }, read, map_name="sm_hub_1")

    assert table["water/sewer_water"].blend_mode == "Opaque"
    assert table["water/sewer_water"].opaque is False
    assert table["water/sewer_water"].as_row()["opaque"] is False
    assert table["glass/refract_pane"].opaque is False
    # A Nanite-capable master with the same Opaque blend still resolves true.
    assert table["stone/wall"].opaque is True


def test_material_table_fails_loudly_naming_every_unit_the_material_lane_never_staged():
    read = _sidecars({"brick/window": _BASE, "maps/sm_test/brick/orphan": {
        **_PATCH, "patchBase": "vtmb:material:brick/never_staged"}})
    with pytest.raises(MG.MapGeometryError) as caught:
        MG.resolve_material_table({
            "brick/window": "brick/window",
            "tile/missing": "tile/missing",
            "brick/orphan@cubemapdefault": "maps/sm_test/brick/orphan",
        }, read, map_name="sm_test")
    message = str(caught.value)
    assert "sm_test" in message
    assert "tile/missing" in message and "uv run elysium import materials" in message
    assert "maps/sm_test/brick/orphan" in message


def test_material_report_classifies_animation_and_appearance_class_from_provenance():
    binding = MG.MaterialBinding(
        key="signs/ticker", unit="vtmb:material:signs/ticker",
        asset="/ElysiumBaked/Materials/signs/MI_ticker", master="M_V2_Unlit",
        blend_mode="Additive", patched=False, provenance="signs/ticker")
    provenance = {**_BASE, "proxies": [{"kind": "texturescroll"}, {"kind": "animatedtexture"}],
                  "omissions": [{"reason": MG.FRAMES_UNAVAILABLE_OMISSION}],
                  "wetnessScale": None, "isDecalSurface": False}
    row = MG.classify_material(binding, provenance, {"additive": True})
    # `texturescroll` runs live on the V2 instance; `animatedtexture` does not, because its frames
    # array never staged -- the provenance says so, and the row must say the same.
    assert row["animatedNow"] is True
    assert row["liveProxies"] == ["texturescroll"]
    assert row["animatedFramesUnavailable"] is True
    # Legacy `additive 1` -> M_Additive -> class "additive"; V2 Additive blend -> "additive".
    assert row["legacyMaster"] == "M_Additive"
    assert row["classChanged"] is False

    # R7.2 rulings 2 and 3: a `$decal 1` world face binds the PROJECTOR instance the material lane
    # staged beside its surface one, as its mesh slot -- so its class is "decal" (the class means
    # "projector instance bound", not "a flag in the legacy .mtl"), it is never Nanite, and the
    # rebind off the legacy lane's ordinary opaque card is the class change the report states.
    decal = MG.MaterialBinding(
        key="decals/n0", unit="vtmb:material:decals/n0", asset="/ElysiumBaked/Materials/decals/MI_n0",
        master="M_V2_LitTranslucent", blend_mode="Translucent", patched=False, provenance="decals/n0",
        decal_asset="/ElysiumBaked/Materials/decals/MI_n0_Decal", is_decal_surface=True)
    assert decal.slot_asset == "/ElysiumBaked/Materials/decals/MI_n0_Decal"
    assert decal.opaque is False
    assert decal.as_row()["decalAsset"] == "/ElysiumBaked/Materials/decals/MI_n0_Decal"
    assert decal.as_row()["isDecalSurface"] is True
    row = MG.classify_material(decal, {**_BASE, "isDecalSurface": True}, {"decal": True})
    assert (row["legacyClass"], row["v2Class"], row["classChanged"]) == ("opaque", "decal", True)
    assert row["isDecalSurface"] is True
    assert row["decalAsset"] == row["slotAsset"] == "/ElysiumBaked/Materials/decals/MI_n0_Decal"

    # An ordinary surface names no projector and binds its own instance.
    assert binding.as_row()["decalAsset"] is None
    assert binding.slot_asset == binding.asset

    # No legacy record at all (a PAKFILE-only material the legacy corpus never saw) is stated,
    # never guessed.
    row = MG.classify_material(decal, {**_BASE}, None)
    assert row["legacyRecordFound"] is False and row["classChanged"] is False

    report = MG.material_report(
        "sm_test", {"signs/ticker": binding, "decals/n0": decal},
        _sidecars({"signs/ticker": provenance, "decals/n0": {**_BASE, "isDecalSurface": True}}),
        legacy_materials={"signs/ticker": {"additive": True}, "decals/n0": {"decal": True}})
    assert report["counts"]["materials"] == 2
    assert report["counts"]["animatedNow"] == 1 and report["counts"]["classChanged"] == 1
    assert report["counts"]["decalSurfaces"] == report["counts"]["decalProjectorsBound"] == 1
    assert [row["key"] for row in report["animatedNow"]] == ["signs/ticker"]
    assert [row["key"] for row in report["classChanged"]] == ["decals/n0"]


@pytest.mark.parametrize("map_name", WORKING_MAPS)
def test_every_face_group_on_the_working_corpus_resolves_a_staged_and_imported_instance(map_name):
    unit = MG.sidecars.unit_paths(map_name)["root"]
    if not unit.is_file():
        pytest.skip(f"no exported map root unit at {unit}")
    staging = MG.material_staging_root()
    if not staging.is_dir():
        pytest.skip(f"no material staging tree at {staging}")

    geometry = MG.read_geometry(map_name)
    units = geometry.material_units()
    # Every group over every scene names a unit, and a patched face names its map-scoped id.
    assert set(units) == (set(geometry.world.groups) | set(geometry.sky.groups)
                          | {key for scene in geometry.brushes.values() for key in scene.groups})
    assert all(units[key].startswith(f"maps/{map_name}/") for key in units
               if shared_corpus.CUBEMAP_TAG in key)

    table = MG.resolve_material_table(units, MG.sidecar_reader(staging), map_name=map_name)
    assert set(table) == set(units)
    # The `MI_` the editor half will load has to be on disk already: the material lane imports
    # map-scoped, and the map it did not import is exactly the map this would silently unbind.
    content = paths.repo_root() / "Plugins/ElysiumBaked/Content"
    missing = sorted(
        binding.asset for binding in table.values()
        if not (content / (binding.asset[len("/ElysiumBaked/"):] + ".uasset")).is_file())
    assert missing == [], f"{len(missing)} staged instance(s) not imported: {missing[:8]}"

    report = MG.material_report(map_name, table, MG.sidecar_reader(staging))
    assert report["counts"]["materials"] == len(table)
    assert report["counts"]["legacyRecordMissing"] == 0
    assert json.dumps(report)   # serialisable as written beside the staged pair


# ------------------------------------------------------------------ reflection captures (R5.5)


def _fake_units_with_cubemaps(rows, translations):
    from types import SimpleNamespace
    nodes = [{"name": f"cubemap[{i}]", "translation": list(t)} for i, t in enumerate(translations)]
    root = {"cubemaps": [{"index": i, "origin": list(o), "size": 0, "node": i}
                         for i, o in enumerate(rows)]}
    return SimpleNamespace(name="fake", document={"nodes": nodes}, root=root)


def test_cubemap_sample_takes_the_placement_frame_and_the_sky_area_rule():
    # Position is the node translation
    # through the one placement frame, `origin` is the row's Source-inch triple carried verbatim,
    # and sky membership is the same area rule a prop or a light takes -- asked of the SOURCE
    # position, not the glTF one.
    origins = [(-160, 328, 51), (1310, -174, 562)]
    translations = [(x * 0.0254, z * 0.0254, -y * 0.0254) for x, y, z in origins]
    asked = []

    class Sky:
        def is_sky(self, source_position):
            asked.append(tuple(round(v, 6) for v in source_position))
            return source_position[0] > 0

    samples = MG._cubemaps(_fake_units_with_cubemaps(origins, translations), Sky())
    assert [s.index for s in samples] == [0, 1]
    assert [s.origin for s in samples] == origins
    for sample, (x, y, z) in zip(samples, origins):
        assert sample.position == pytest.approx(source_to_unreal(x, y, z), abs=1e-6)
    assert asked == [tuple(float(v) for v in o) for o in origins]
    assert [s.sky for s in samples] == [False, True]
    assert samples[0].as_row() == {
        "index": 0, "origin": [-160, 328, 51],
        "position": list(samples[0].position), "sky": False}
    # The manifest version is restated on either side of the numpy boundary (the editor's embedded
    # Python cannot import this module), so the two constants have to agree -- read, not repeated
    # as a literal that every bump teaches the next reader to edit twice.
    assert MG.MANIFEST_VERSION == _bake_map_v2_manifest_version()


@pytest.mark.parametrize("map_name", WORKING_MAPS)
def test_reader_stands_one_capture_per_lump_42_sample_on_the_working_corpus(map_name):
    unit = MG.sidecars.unit_paths(map_name)["root"]
    if not unit.is_file():
        pytest.skip(f"no exported map root unit at {unit}")
    geometry = MG.read_geometry(map_name)
    rows = MG.sidecars.read_units(map_name).root["cubemaps"]
    assert len(geometry.cubemaps) == len(rows) == geometry.counts["cubemaps"] > 0
    for sample, row in zip(geometry.cubemaps, rows):
        assert sample.index == row["index"]
        assert sample.origin == tuple(row["origin"])
        # The node translation and the row's own Source-inch origin are one point in two frames:
        # the placement frame has to land the translation on `source_to_unreal(origin)`.
        assert sample.position == pytest.approx(
            source_to_unreal(*(float(v) for v in row["origin"])), abs=0.01)
    # No working map authors an env_cubemap inside its 3D-skybox miniature; the rule is kept for
    # the map that does, and this pins the corpus fact the doc states (0 of 57).
    assert not any(sample.sky for sample in geometry.cubemaps)


# ------------------------------------------------------------------------- water volumes (R7.1)

#: The six outward halfspaces of a 1 m glTF cube at the origin (`n . p <= d`), the mould every
#: fake brush below is cut from: `(0, 1, 0)` is glTF up, so it is the water surface.
_UNIT_CUBE_PLANES = (
    ((1.0, 0.0, 0.0), 1.0), ((-1.0, 0.0, 0.0), 0.0),
    ((0.0, 1.0, 0.0), 1.0), ((0.0, -1.0, 0.0), 0.0),
    ((0.0, 0.0, 1.0), 1.0), ((0.0, 0.0, -1.0), 0.0),
)
#: A redundant half-height cap: vbsp carries these on real brushes as bevel sides, and the solver
#: skips them. Up-facing on purpose -- unskipped it would both halve the hull and be read as the
#: brush's water surface.
_BEVEL_PLANE = ((0.0, 1.0, 0.0), 0.5)


def _fake_water_units(brush_specs, textures, rows):
    """The `MapUnits` shape `resolve_water_volumes` reads -- the root extension's `water`,
    `collision`, `planes`, `texinfos` and `textures` tables and nothing else.

    Every brush is the same 1 m cube; a spec is `(contents, material)`, or `(contents, material,
    "bevel")` to append the redundant half-height side. Texinfo `i` names `textures[i]`.
    """

    plane_rows = [{"index": index, "normal": normal, "dist": dist}
                  for index, (normal, dist) in enumerate(_UNIT_CUBE_PLANES + (_BEVEL_PLANE,))]
    keys = list(textures)
    sides: list[dict] = []
    brushes: list[dict] = []
    for index, spec in enumerate(brush_specs):
        contents, material = spec[0], spec[1]
        first = len(sides)
        for plane in list(range(6)) + ([6] if len(spec) > 2 else []):
            sides.append({"index": len(sides), "plane": plane, "texInfo": keys.index(material),
                          "dispInfo": -1, "bevel": 1 if plane == 6 else 0})
        brushes.append({"index": index, "firstSide": first, "numSides": len(sides) - first,
                        "contents": contents})
    root = {
        "planes": plane_rows,
        "collision": {"brushes": brushes, "brushSides": sides},
        "texinfos": [{"texData": index} for index in range(len(keys))],
        "textures": [{"asset": f"vtmb:material:{key}"} for key in keys],
        "water": {"leafData": list(rows)},
    }
    return SimpleNamespace(name="fake", document={"nodes": []}, root=root)


def _water_row(index, tex_info, surface_z=1.0, min_z=0.0):
    """One `LEAFWATERDATA` record as the unit publishes it: heights in glTF metres."""

    return {"index": index, "surfaceZ": surface_z, "minZ": min_z,
            "surfaceTexInfoID": tex_info, "padding": 0}


def _provenance(*pairs, patched=False, base=None):
    """One material unit's staged provenance, as much of it as the water stage reads."""

    return {"patched": patched, "patchBase": base,
            "parameters": [{"block": "", "key": key, "value": value} for key, value in pairs]}


def test_water_volumes_drop_the_sentinel_row_and_keep_the_real_one():
    # `surfaceTexInfoID` -1 is vbsp's own sentinel (two rows on `hw_warrens_2`): it names no
    # material, so it is dropped and named rather than resolved against texinfo -1.
    units = _fake_water_units(
        [(0x10000020, "water/sewer_water")],
        ["water/sewer_water"],
        [_water_row(0, 0), _water_row(1, -1)])
    documents = {"water/sewer_water": _provenance(("%compilewater", "1"))}

    volumes, dropped = MG.resolve_water_volumes(units, documents.get, "fake")
    assert [volume.index for volume in volumes] == [0]
    assert volumes[0].material == "vtmb:material:water/sewer_water"
    assert volumes[0].surface_z_cm == 100.0 and volumes[0].min_z_cm == 0.0
    assert dropped == [{"index": 1, "reason": "sentinel"}]


def test_only_a_compilewater_brush_carrying_the_content_bit_belongs_to_a_volume():
    # The content bit alone is not water: a `0x18000120` shadow caster carries it with nothing but
    # `tools/tools_shadow` sides. `%compilewater` on a side's own material is what vbsp read, and
    # `0x18000020` `func_detail` water passes on exactly the same evidence.
    units = _fake_water_units(
        [(0x10000020, "water/sewer_water"),
         (0x18000120, "tools/tools_shadow"),
         (0x18000020, "water/sewer_water")],
        ["water/sewer_water", "tools/tools_shadow"],
        [_water_row(0, 0)])
    documents = {
        "water/sewer_water": _provenance(("%compilewater", "1")),
        "tools/tools_shadow": _provenance(("$basetexture", "tools/tools_shadow")),
    }

    volumes, dropped = MG.resolve_water_volumes(units, documents.get, "fake")
    assert dropped == []
    assert len(volumes) == 1 and len(volumes[0].brushes) == 2


def test_water_brush_planes_and_bounds_take_the_unreal_frame_and_skip_the_bevel():
    # The unit publishes planes in the glTF frame; the bake's own permutation `(n0, n2, n1)` with
    # the distance in centimetres is orthogonal, so a 1 m cube is a 100 cm cube and `n . p - d <= 0`
    # still names its inside. The bevel side is neither a plane of the hull nor its surface.
    units = _fake_water_units(
        [(0x10000020, "water/sewer_water", "bevel")],
        ["water/sewer_water"],
        [_water_row(0, 0)])
    documents = {"water/sewer_water": _provenance(("%compilewater", "1"))}

    volumes, _dropped = MG.resolve_water_volumes(units, documents.get, "fake")
    brush = volumes[0].brushes[0]
    assert brush.planes == (
        (1.0, 0.0, 0.0, 100.0), (-1.0, 0.0, 0.0, 0.0),
        (0.0, 0.0, 1.0, 100.0), (0.0, 0.0, -1.0, 0.0),
        (0.0, 1.0, 0.0, 100.0), (0.0, -1.0, 0.0, 0.0),
    )
    assert brush.bounds_min == pytest.approx((0.0, 0.0, 0.0), abs=1e-3)
    assert brush.bounds_max == pytest.approx((100.0, 100.0, 100.0), abs=1e-3)

    def outside(point):
        return any(sum(n * p for n, p in zip(plane[:3], point)) - plane[3] > 0.0
                   for plane in brush.planes)

    assert not outside((50.0, 50.0, 50.0))
    assert outside((150.0, 50.0, 50.0)) and outside((50.0, 50.0, -1.0))


def test_water_fog_keys_come_from_the_material_units_own_vmt_provenance():
    # `invisible_water` stages onto `M_V2_Unlit`, which has no fog lane at all, so every fog key is
    # a provenance omission on the instance. The volume reads the VMT the unit was authored from:
    # `{22 20 10}` divided down as authored, the two distances in centimetres.
    units = _fake_water_units(
        [(0x10000020, "water/invisible_water"), (0x10000020, "water/warrenwater2b")],
        ["water/invisible_water", "water/warrenwater2b"],
        [_water_row(0, 0), _water_row(1, 1, surface_z=3.0, min_z=2.0)])
    documents = {
        "water/invisible_water": _provenance(
            ("%compilewater", "1"), ("$fogenable", "1"), ("$fogcolor", "{22 20 10}"),
            ("$fogstart", "1.00"), ("$fogend", "400.00")),
        "water/warrenwater2b": _provenance(("%compilewater", "1"), ("$basetexture", "water/x")),
    }
    units.root["planes"].append({"index": 7, "normal": (0.0, 1.0, 0.0), "dist": 3.0})
    units.root["collision"]["brushSides"][8]["plane"] = 7   # the second cube's top, 3 m up

    volumes, _dropped = MG.resolve_water_volumes(units, documents.get, "fake")
    fogged, clear = volumes
    assert fogged.fog_enable is True
    assert fogged.fog_color == pytest.approx((22 / 255, 20 / 255, 10 / 255), abs=1e-6)
    assert (fogged.fog_start_cm, fogged.fog_end_cm) == (2.54, 1016.0)
    assert fogged.as_row() == {
        "index": 0, "surfaceZCm": 100.0, "minZCm": 0.0,
        "material": "vtmb:material:water/invisible_water",
        "fogEnable": True, "fogColor": list(fogged.fog_color),
        "fogStartCm": 2.54, "fogEndCm": 1016.0,
        "brushes": [fogged.brushes[0].as_row()],
        # R7.4: a unit with no physics block, no leaves and no visibility sub-unit authors none of
        # these, and every one of them says so rather than shipping an empty set as an answer.
        "fluid": None, "pieces": [], "leafBoxesCm": [], "nearBoxesCm": [],
        "materialTableWaterIndex": None,
    }
    # A unit that authors no fog key is clear water -- `SetFogVolumeState`'s own answer for it.
    assert clear.fog_enable is False
    assert (clear.fog_color, clear.fog_start_cm, clear.fog_end_cm) == ((0.0, 0.0, 0.0), 0.0, 0.0)


def test_a_staged_unit_with_no_keys_is_clear_water_and_only_an_unstaged_one_is_an_error():
    """The two cases `import materials` can and cannot fix, told apart.

    A unit the material lane never staged is a real gap and the message names the lane to re-run; a
    unit that staged and authors none of the four keys is `$fogenable` absent, which is
    `SetFogVolumeState`'s clear water. Funnelling both into the error aborted the whole map export
    on a diagnosis that re-running the lane could not change.
    """

    def units_for(row_material):
        units = _fake_water_units(
            [(0x10000020, "water/sewer_water")],
            [row_material, "water/sewer_water"],
            [_water_row(0, 0)])
        return units

    documents = {
        "water/sewer_water": _provenance(("%compilewater", "1")),
        "water/no_keys": _provenance(),
    }

    volumes, dropped = MG.resolve_water_volumes(units_for("water/no_keys"), documents.get, "fake")
    assert dropped == [] and len(volumes) == 1
    assert volumes[0].material == "vtmb:material:water/no_keys"
    assert volumes[0].fog_enable is False

    with pytest.raises(MG.MapGeometryError, match="not staged by the material lane"):
        MG.resolve_water_volumes(units_for("water/unstaged"), documents.get, "fake")


def test_a_row_no_water_brush_stands_at_is_dropped_and_named():
    # `la_bradbury_3`'s row: the only brush at its height is a `tools/tools_shadow` caster, so the
    # record describes a volume the map does not have.
    units = _fake_water_units(
        [(0x18000120, "tools/tools_shadow")],
        ["water/bradbury_blood", "tools/tools_shadow"],
        [_water_row(0, 0)])
    documents = {
        "water/bradbury_blood": _provenance(("%compilewater", "1")),
        "tools/tools_shadow": _provenance(("$basetexture", "tools/tools_shadow")),
    }

    volumes, dropped = MG.resolve_water_volumes(units, documents.get, "fake")
    assert volumes == []
    assert dropped == [{"index": 0, "reason": "no water brush"}]


def test_a_fluid_naming_index_zero_creates_nothing():
    # Verdict B5: VtMB's creation guard is `fluid.index > 0` and nothing else (`vampire.dll
    # FUN_10158600`, `101586ff JLE skip`), so a block naming index 0 gets no controller there. It
    # must get none here either: joined on the surface plane alone it would carve the volume out of
    # `physics.models[0].solids[0]` -- the WORLD's own collision solid -- and those pieces outrank
    # the real water brushes in `ElysiumWater::FindVolumeAt`, so the whole map would read as
    # submerged. No corpus map trips it (both owner maps author `index "5"`).
    units = _fake_water_units(
        [(0x10000020, "water/sewer_water")], ["water/sewer_water"], [_water_row(0, 0)])
    documents = {"water/sewer_water": _provenance(("%compilewater", "1"))}
    units.root["physics"] = {
        "positionAccessor": 0, "indexAccessor": 1,
        "models": [{"index": 0, "solids": [{"ledges": []}],
                    "keyValues": [_key_value(
                        "fluid", index="0", density="1000.000000",
                        surfaceplane="0.000000 0.000000 1.000000 39.370079 ")]}],
    }
    units.accessor = lambda index: np.zeros((0, 3))

    volumes, dropped = MG.resolve_water_volumes(units, documents.get, "fake")
    assert dropped == [] and len(volumes) == 1
    assert volumes[0].fluid is None and volumes[0].pieces == ()
    assert volumes[0].as_row()["fluid"] is None and volumes[0].as_row()["pieces"] == []


def test_a_patched_water_unit_reads_compilewater_and_its_fog_keys_through_its_base():
    # `maps/ch_fulab_1/water/cheap_water_1318_1990_273`'s own provenance is the `patch` delta and
    # nothing else -- `include` plus one `insert` block. Read without the `patchBase` walk the
    # brush would not qualify at all and the volume would ship clear.
    key = "maps/ch_fulab_1/water/cheap_water_1318_1990_273"
    units = _fake_water_units([(0x10000020, key)], [key], [_water_row(0, 0)])
    documents = {
        key: _provenance(("include", "WATER/CHEAP_WATER"),
                         patched=True, base="vtmb:material:water/cheap_water"),
        "water/cheap_water": _provenance(
            ("%compilewater", "1"), ("$forcecheap", "1"), ("$fogenable", "1"),
            ("$fogcolor", "{22 20 10}"), ("$fogstart", "1.00"), ("$fogend", "400.00")),
    }
    documents[key]["parameters"].append(
        {"block": "insert#1", "key": "$envmap", "value": "maps/ch_fulab_1/c1318_1990_273"})

    volumes, dropped = MG.resolve_water_volumes(units, documents.get, "fake")
    assert dropped == [] and len(volumes) == 1
    assert volumes[0].material == f"vtmb:material:{key}"
    assert volumes[0].fog_enable is True and volumes[0].fog_end_cm == 1016.0
    assert len(volumes[0].brushes) == 1


#: `(map, surfaceZCm, material key, brush count)` -- the three exported maps R7.1 stages water on:
#: the sewer the ruling was written against, the pier's invisible ocean volume, and the one
#: PAKFILE-patched `cheap_water` unit in the corpus.
WATER_CORPUS = (
    ("sm_hub_1", -14937.74, "water/sewer_water", 1),
    ("sm_pier_1", -1582.42, "water/invisible_water", 1),
    ("ch_fulab_1", 22.86, "maps/ch_fulab_1/water/cheap_water_1318_1990_273", 1),
)


@pytest.mark.parametrize("map_name,surface_z_cm,material,brush_count", WATER_CORPUS)
def test_water_volumes_on_the_exported_corpus(map_name, surface_z_cm, material, brush_count):
    unit = MG.sidecars.unit_paths(map_name)["root"]
    if not unit.is_file():
        pytest.skip(f"no exported map root unit at {unit}")
    staging = MG.material_staging_root()
    if not staging.is_dir():
        pytest.skip(f"no material staging tree at {staging}")

    units = MG.sidecars.read_units(map_name)
    volumes, dropped = MG.resolve_water_volumes(
        units, MG.sidecar_reader(staging), map_name)
    assert dropped == []
    assert len(volumes) == 1
    volume = volumes[0]
    assert volume.surface_z_cm == pytest.approx(surface_z_cm, abs=0.01)
    assert volume.material == f"vtmb:material:{material}"
    assert volume.fog_enable is True
    assert len(volume.brushes) == brush_count
    for brush in volume.brushes:
        assert len(brush.planes) >= 6
        # A closed hull: its bounds hold the surface the record names. The hull is solved in the
        # collision lane's binary32 Source frame, so its top sits within a rounding step of the
        # plane distance rather than on it (`sm_hub_1`: 2e-4 cm).
        assert brush.bounds_min[2] - 0.01 <= volume.surface_z_cm <= brush.bounds_max[2] + 0.01


# ------------------------------------------------------- water, complete (R7.4)
#
# Rulings K/L/M/N and the water audit's gap list. Four
# facts vbsp published and this stage never read -- the face's own plane (underside), the face's
# lightstyles, the compiler's `fluid { }` / convex pieces / leaf boxes, and the PVS the near-water
# set derives from -- plus the two drops the audit reversed: a `%compilewater` face is never dropped
# for `SURF_NODRAW`, and a 3D-skybox brush entity is never composed into the collision.
#
# The synthetic cases pin the rules; the corpus cases pin them against the three exported water maps
# and skip, loudly, when those are not on this machine.


def _styled(*styles):
    return {"styles": list(styles) + [255] * (4 - len(styles))}


def test_a_faces_lightstyle_is_the_lowest_one_it_names_not_the_first_slot():
    # Measured on `sm_pier_1`'s 34 `objects/surf` foam cards: every one names style 1, but 18 name
    # the switchable style 32 in the earlier slot. First-slot order would flicker one waterline on
    # two patterns; the lowest is the animated one (Quake's inherited 1-11) and the switchable one
    # (32-63, a named `light`'s) defaults to full brightness anyway.
    assert MG.face_light_style(_styled(0, 32, 1)) == 1
    assert MG.face_light_style(_styled(0, 1, 32)) == 1
    assert MG.face_light_style(_styled(0, 1)) == 1
    assert MG.face_light_styles(_styled(0, 32, 1)) == [32, 1]
    # 0 is the always-on base and 255 the empty slot: neither is a style, and a face naming only
    # those animates on none.
    assert MG.face_light_style(_styled(0)) is None
    assert MG.face_light_style({"styles": [0, 255, 255, 255, 3, 4, 5, 6]}) is None
    assert MG.face_light_styles(_styled(0)) == []


def test_underside_is_the_faces_own_plane_normal_and_never_the_side_bit():
    # Ruling N / verdict B2: `Mod_LoadFaces` tests `plane.normal.z < 0.0` (`_DAT_201734e8`) and
    # undefines `$reflecttexture` on it. The unit publishes plane normals in the glTF frame, whose
    # `y` is Unreal's `z`. A vertical face is not an underside -- the pier has 23 of them.
    units = SimpleNamespace(name="fake", root={"planes": [
        {"index": 0, "normal": (0.0, 1.0, 0.0), "dist": 0.0},     # up
        {"index": 1, "normal": (0.0, -1.0, 0.0), "dist": 0.0},    # down
        {"index": 2, "normal": (1.0, 0.0, 0.0), "dist": 0.0},     # vertical
    ]})
    # `side` is the shard's front/back bit and is deliberately not consulted: every one of the
    # corpus's down-facing water faces carries `side 1`, so flipping on it would call all 47 hub
    # water faces up-facing.
    assert MG.face_underside(units, {"plane": 0, "side": 1}) is False
    assert MG.face_underside(units, {"plane": 1, "side": 1}) is True
    assert MG.face_underside(units, {"plane": 2, "side": 0}) is False
    assert MG.face_underside(units, {"plane": 99, "side": 0}) is False


def test_section_keys_round_trip_through_both_suffixes():
    key = MG.section_key("dev/dev_waterbeneath2@cubemapdefault", underside=True, light_style=32)
    assert key == "dev/dev_waterbeneath2@cubemapdefault#underside#style32"
    assert MG.split_section_key(key) == ("dev/dev_waterbeneath2@cubemapdefault", True, 32)
    assert MG.split_section_key("objects/surf#style1") == ("objects/surf", False, 1)
    assert MG.split_section_key("water/invisible_water#underside") == (
        "water/invisible_water", True, None)
    # A material whose own name contains the suffix text is not a section: the style tail has to be
    # a number and the underside tail has to be the whole suffix.
    assert MG.split_section_key("water/x#stylish") == ("water/x#stylish", False, None)
    assert MG.section_key("water/x") == "water/x"


def test_an_underside_group_binds_the_twin_and_states_its_style():
    def binding(key, **overrides):
        _base, underside, style = MG.split_section_key(key)
        fields = dict(
            key=key, unit="vtmb:material:water/sewer_water",
            asset="/ElysiumBaked/Materials/water/MI_sewer_water", master="M_V2_Water",
            blend_mode="Opaque", patched=False, provenance="water/sewer_water",
            underside=underside, light_style=style)
        fields.update(overrides)
        return MG.MaterialBinding(**fields)

    surface = binding("water/sewer_water")
    assert surface.underside_asset is None
    assert surface.slot_asset == "/ElysiumBaked/Materials/water/MI_sewer_water"
    assert surface.as_row()["underside"] is False
    assert surface.as_row()["lightStyle"] is None

    # The twin's name is the surface instance's plus the suffix, in the same folder -- the R7.2
    # decal-twin shape, so the bake needs no second naming rule.
    under = binding("water/sewer_water#underside#style1")
    assert under.underside_asset == "/ElysiumBaked/Materials/water/MI_sewer_water_Underside"
    assert under.slot_asset == under.underside_asset
    assert under.as_row()["undersideAsset"] == under.underside_asset
    assert under.as_row()["lightStyle"] == 1

    # A decal surface still binds its projector: the decal rule is about the whole surface and the
    # underside rule is about which side of a water sheet it is, and only one of them can be true.
    decal = binding("water/sewer_water#underside", is_decal_surface=True,
                    decal_asset="/ElysiumBaked/Materials/water/MI_sewer_water_Decal")
    assert decal.slot_asset == "/ElysiumBaked/Materials/water/MI_sewer_water_Decal"


def test_a_section_key_resolves_the_same_unit_as_its_base_key():
    documents = {"water/sewer_water": {"master": "/x/M_V2_Water", "blendMode": "Opaque"}}
    table = MG.resolve_material_table(
        {"water/sewer_water": "water/sewer_water",
         "water/sewer_water#underside": "water/sewer_water",
         "objects/surf#style1": "water/sewer_water"},
        documents.get, map_name="fake")

    assert set(table) == {"water/sewer_water", "water/sewer_water#underside", "objects/surf#style1"}
    from elysium_pipeline.importers import materials as material_lane
    assert {row.asset for row in table.values()} == {
        material_lane.asset_path_for("water/sewer_water")}
    assert table["water/sewer_water#underside"].underside is True
    assert table["water/sewer_water#underside"].light_style is None
    assert table["objects/surf#style1"].light_style == 1
    assert table["objects/surf#style1"].underside is False


# --- the compiled primitive grid (G13/U2) ---------------------------------------------------------


def _primitive_units(kind, indices, points):
    """A unit whose single face names one primitive: `numPrims`/`firstPrimID` at `dface+100/102`,
    lump 38 positions in Source inches and lump 39's index run."""

    return SimpleNamespace(
        name="fake", document={"nodes": []},
        root={
            "faces": [{
                "index": 0, "plane": 0, "side": 0, "texInfo": 0, "dispInfo": -1,
                "styles": [0, 255, 255, 255], "area": 4096.0, "surfaceFogVolumeID": 0,
                "numPrims": 1, "firstPrimID": 0, "primitive": None,
            }],
            "planes": [{"index": 0, "normal": (0.0, 1.0, 0.0), "dist": 0.0}],
            "texinfos": [{"texData": 0, "textureVecs": [[1.0, 0.0, 0.0, 0.0],
                                                        [0.0, 1.0, 0.0, 0.0]]}],
            "textures": [{"asset": "vtmb:material:water/sewer_water", "width": 64, "height": 64}],
            "primitives": {
                "primitives": [{"index": 0, "type": kind, "firstIndex": 0,
                                "indexCount": len(indices), "firstVert": 0,
                                "vertCount": len(points)}],
                "verts": [{"index": i, "point": point} for i, point in enumerate(points)],
                "indices": {"values": list(indices)},
            },
        })


def _quad_points():
    """A 64-inch quad on the z = 0 plane, in Source inches."""

    return [(0.0, 0.0, 0.0), (64.0, 0.0, 0.0), (0.0, 64.0, 0.0), (64.0, 64.0, 0.0)]


def test_a_face_with_primitives_meshes_the_compiled_grid_with_uvs_from_texinfo():
    # `Shader_DrawSurfaceDynamic` reads `numPrims` first and, when it is non-zero, draws the
    # compiled strip to the exclusion of the surfedge fan -- so the grid IS VtMB's water mesh on the
    # nine maps that carry one. `Mod_LoadPrimVerts` zero-fills the runtime record and copies only
    # the position, so lump 38 has no UVs and they are re-derived from the texinfo vectors here.
    units = _primitive_units(MG.PRIM_TRILIST, [0, 1, 2, 1, 3, 2], _quad_points())
    scene = MG._build_scene(units, None, 0, [0], "fake", lambda key: True, "world")

    assert list(scene.groups) == ["water/sewer_water"]
    assert len(scene.groups["water/sewer_water"]) == 6
    assert len(scene.positions) == 4
    # Source (64, 64, 0) is Unreal (162.56, -162.56, 0), and the UV is the planar projection over
    # the TEXDATA size, exactly what `_planar_uv` gives an ordinary face.
    assert scene.positions[3] == pytest.approx(source_to_unreal(64.0, 64.0, 0.0), abs=1e-4)
    assert scene.uvs[3] == pytest.approx((1.0, 1.0), abs=1e-6)
    assert scene.uvs[0] == pytest.approx((0.0, 0.0), abs=1e-6)
    # Winding is reversed on the way out, like every other face: the frame is a reflection.
    assert scene.groups["water/sewer_water"][:3] == [0, 2, 1]


def test_a_triangle_strip_primitive_unwinds_to_the_same_surface_as_a_list():
    strip = MG._build_scene(
        _primitive_units(MG.PRIM_TRISTRIP, [0, 1, 2, 3], _quad_points()),
        None, 0, [0], "fake", lambda key: True, "world")
    assert len(strip.groups["water/sewer_water"]) == 6

    # A degenerate stitch -- the strip's own way of joining two runs -- states no triangle, so only
    # the one real triangle in this run survives.
    stitched = MG._build_scene(
        _primitive_units(MG.PRIM_TRISTRIP, [0, 1, 2, 2, 2, 3], _quad_points()),
        None, 0, [0], "fake", lambda key: True, "world")
    assert len(stitched.groups["water/sewer_water"]) == 3

    # A type the engine refuses to draw (`else return`) meshes nothing rather than guessing a mode.
    unknown = MG._build_scene(
        _primitive_units(7, [0, 1, 2], _quad_points()), None, 0, [0], "fake", lambda k: True, "w")
    assert unknown.groups == {}


def test_primitive_indices_are_read_local_or_absolute_whichever_the_run_states():
    # `BuildMSurfacePrimIndices` indexes the primitive's own vertex run. A compiler that wrote them
    # absolute into lump 38 would otherwise mesh inside out or drop the face entirely.
    units = _primitive_units(MG.PRIM_TRILIST, [0, 1, 2], _quad_points())
    units.root["primitives"]["primitives"][0]["firstVert"] = 1
    units.root["primitives"]["primitives"][0]["vertCount"] = 3
    units.root["primitives"]["primitives"][0]["indexCount"] = 3
    units.root["primitives"]["indices"]["values"] = [1, 2, 3]
    absolute = MG._build_scene(units, None, 0, [0], "fake", lambda key: True, "world")
    assert len(absolute.groups["water/sewer_water"]) == 3
    assert len(absolute.positions) == 3


def test_a_water_face_row_states_every_field_the_audit_named():
    units = _primitive_units(MG.PRIM_TRILIST, [0, 1, 2, 1, 3, 2], _quad_points())
    units.root["faces"][0]["styles"] = [0, 32, 1, 255]
    units.root["faces"][0]["surfaceFogVolumeID"] = 0
    scene = MG._build_scene(units, None, 0, [0], "fake", lambda key: True, "world")

    assert len(scene.water_faces) == 1
    row = scene.water_faces[0]
    assert row["index"] == 0 and row["scene"] == "world"
    assert row["group"] == "water/sewer_water#style1"
    assert row["unit"] == "vtmb:material:water/sewer_water"
    assert row["underside"] is False and row["normal"] == [0.0, 0.0, 1.0]
    assert (row["lightStyle"], row["lightStyles"]) == (1, [32, 1])
    assert row["surfaceFogVolumeID"] == 0            # G11: the compiler's own volume statement
    assert row["texdata"] == 0 and row["texInfo"] == 0
    assert row["primitive"] == {"first": 0, "count": 1}
    # G26: `faces[].area` is the pin a rebuilt mesh is checked against; `originalFaces[].area` is
    # 0.0 on all 2,182 corpus rows and is never the pin.
    assert row["area"] == 4096.0
    assert row["areaCm2"] == pytest.approx(4096.0 * 2.54 * 2.54, abs=1e-3)
    # ... and `meshedAreaCm2` is the other half of it: what this stage actually produced for the
    # face. The two agreeing is the whole pin -- a mesh path that dropped a shard or double-counted
    # a strip stitch shows up here and nowhere else offline.
    assert row["meshedAreaCm2"] == pytest.approx(row["areaCm2"], rel=1e-6)
    assert row["triangles"] == 2

    # No predicate is "nobody here can answer what a water face is": no rows, and no underside split.
    assert MG._build_scene(units, None, 0, [0], "fake", None, "world").water_faces == []


# --- the fluid block, the convex pieces, the leaf and near boxes ------------------------------------


def _physics_units(key_values, solids=(), positions=(), indices=(), leafs=()):
    return SimpleNamespace(
        name="fake", document={"nodes": []},
        accessor=lambda index: (np.array(positions) if index == 0
                                else np.array(indices).reshape(-1, 1)),
        root={
            "physics": {"positionAccessor": 0, "indexAccessor": 1,
                        "models": [{"index": 0, "keyValues": list(key_values),
                                    "solids": list(solids)}]},
            "bsp": {"leafs": list(leafs)},
        })


def _key_value(kind, **pairs):
    return {"type": kind, "pairs": [{"key": key, "value": value} for key, value in pairs.items()]}


def test_the_fluid_block_lands_in_the_bakes_frame_and_says_what_was_not_authored():
    # `sm_pier_1`'s own block. The plane normal is a direction (Y negated, no scale) and its
    # distance a length (inches to centimetres): `-623 in` is the volume's own `-1582.42 cm`.
    fluid = MG._water_fluid(_physics_units([_key_value(
        "fluid", index="5", density="1000.000000", damping="0.010000",
        surfaceplane="0.000000 0.000000 1.000000 -623.000000 ",
        currentvelocity="0.000000 0.000000 0.000000 ")]))

    assert fluid.index == 5 and fluid.density == 1000.0 and fluid.damping == 0.01
    assert fluid.surface_plane == (0.0, -0.0, 1.0, -1582.42)
    assert fluid.current_velocity_cm == (0.0, -0.0, 0.0)
    # The pier authors neither, and a substituted default would state an authoring that did not
    # happen -- vphysics' own density default is the 1000 the pier does author.
    assert fluid.contents is None and fluid.surface_prop is None
    assert fluid.as_row()["surfacePlane"] == [0.0, -0.0, 1.0, -1582.42]

    # `sm_hub_1`'s: no density, a surfaceprop and the parsed-and-unread contents word.
    hub = MG._water_fluid(_physics_units([_key_value(
        "fluid", index="5", surfaceprop="water", damping="0.010000", contents="268435488",
        surfaceplane="0.000000 0.000000 1.000000 -5881.000000 ",
        currentvelocity="0.000000 0.000000 0.000000 ")]))
    assert hub.density is None and hub.surface_prop == "water"
    assert hub.contents == 268435488
    assert hub.surface_plane[3] == pytest.approx(-14937.74, abs=1e-2)

    # A current nobody authored on any corpus map still has its lane, in the map's own frame.
    flowing = MG._water_fluid(_physics_units([_key_value(
        "fluid", index="5", surfaceplane="0 0 1 0", currentvelocity="10.0 20.0 -30.0 ")]))
    assert flowing.current_velocity_cm == pytest.approx(source_to_unreal(10.0, 20.0, -30.0))

    assert MG._water_fluid(_physics_units([])) is None


def test_the_physics_material_table_states_the_water_index_or_says_it_has_none():
    # G17. `sm_pier_1`'s 18 rows carry `WATER = 17`; `sm_hub_1`'s 16 rows have no water row at all,
    # which is a fact about the map and not a missing read.
    pier = MG._physics_units if False else _physics_units
    assert MG._material_table_water_index(pier([
        _key_value("materialtable", default="1", metal="5", water="17")])) == 17
    assert MG._material_table_water_index(pier([
        _key_value("materialtable", default="1", metal="5")])) is None
    assert MG._material_table_water_index(pier([])) is None


def test_a_solids_ledges_become_convex_plane_sets_with_the_inside_on_the_same_side():
    # G18. A ledge is vbsp's own convex piece -- 12 triangles over 8 vertices for a box -- and the
    # lump states topology, not planes, so the planes are derived and oriented against the piece's
    # own centroid: `n . p - d <= 0` inside, the convention `WaterBrush.planes` already carries.
    corners = [(x, y, z) for x in (0.0, 1.0) for y in (0.0, 1.0) for z in (0.0, 1.0)]
    faces = [
        (0, 1, 3), (0, 3, 2), (4, 7, 5), (4, 6, 7),     # x = 0, x = 1
        (0, 4, 5), (0, 5, 1), (2, 3, 7), (2, 7, 6),     # y = 0, y = 1
        (0, 2, 6), (0, 6, 4), (1, 5, 7), (1, 7, 3),     # z = 0, z = 1
    ]
    units = _physics_units(
        [], solids=[{"index": 0, "ledges": [{
            "index": 0, "firstVertex": 0, "vertexCount": 8, "firstIndex": 0, "indexCount": 36}]}],
        positions=corners, indices=[corner for face in faces for corner in face])

    pieces = MG._solid_pieces(units, 0)

    assert len(pieces) == 1
    piece = pieces[0]
    # The 12 triangles of a box are 6 planes; duplicates of one face collapse.
    assert len(piece.planes) == 6

    def outside(point):
        return any(sum(n * p for n, p in zip(plane[:3], point)) - plane[3] > 1e-6
                   for plane in piece.planes)

    # The glTF-to-Unreal permutation is `(x, z, y) * 100`, so the unit cube is a 100 cm cube.
    assert not outside((50.0, 50.0, 50.0))
    assert outside((150.0, 50.0, 50.0)) and outside((50.0, 50.0, -1.0))
    assert piece.bounds_min == pytest.approx((0.0, 0.0, 0.0), abs=1e-3)
    assert piece.bounds_max == pytest.approx((100.0, 100.0, 100.0), abs=1e-3)
    assert MG._solid_pieces(units, 9) == []


def _leaf(index, cluster, water, mins, maxs):
    return {"index": index, "cluster": cluster, "leafWaterDataID": water,
            "mins": list(mins), "maxs": list(maxs), "contents": 0}


def test_the_leaf_boxes_are_the_records_own_leaves_in_the_bakes_frame():
    # G10: `leafWaterDataID` is the engine's own "the eye is under water" answer in one lookup, and
    # the union of those boxes is a tighter hull than the single brush AABB -- 66-77 inches tighter
    # on `sm_hub_1`. `source_to_unreal` negates Y, so the box is rebuilt from both corners.
    units = _physics_units([], leafs=[
        _leaf(0, 1, 0, (-10, -20, -30), (10, 20, 30)),
        _leaf(1, 2, -1, (0, 0, 0), (1, 1, 1)),
        _leaf(2, 3, 1, (0, 0, 0), (1, 1, 1)),
    ])
    boxes = MG._leaf_water_boxes(units, 0)

    assert len(boxes) == 1
    low, high = boxes[0]
    assert low == pytest.approx(source_to_unreal(-10.0, 20.0, -30.0), abs=1e-4)
    assert high == pytest.approx(source_to_unreal(10.0, -20.0, 30.0), abs=1e-4)
    assert MG._leaf_water_boxes(units, 7) == ()


def test_the_near_water_set_is_derived_from_the_pvs_and_never_from_the_leaf_bit():
    # G9/G23. VtMB's near-water annotation is `union(PVS(water cluster))` -- measured set-equal to
    # the `0x200` leaves on `sm_hub_1` and `sp_soc_3`. It is derived because `sm_pier_1`'s
    # Unofficial-Patch recompile carries the bit on no leaf at all (retail: 702).
    solid = _leaf(3, 4, -1, (0, 0, 0), (0, 0, 0))
    solid["contents"] = 0x1
    units = _physics_units([], leafs=[
        _leaf(0, 1, 0, (0, 0, 0), (1, 1, 1)),        # the water leaf, cluster 1
        _leaf(1, 4, -1, (2, 2, 2), (3, 3, 3)),       # visible from it
        _leaf(2, 9, -1, (4, 4, 4), (5, 5, 5)),       # not visible from it
        solid,                                       # vbsp's dummy solid leaf, in a visible cluster
    ])
    asked: list[list[int]] = []

    def pvs_union(clusters):
        asked.append(list(clusters))
        return {1, 4}

    boxes = MG._near_water_boxes(units, SimpleNamespace(pvs_union=pvs_union), 0)

    assert asked == [[1]]
    # The water leaf itself and the one it can see -- never the solid leaf, which no eye can be in
    # and which VtMB's own annotation never marks (0 of the 97 hub / 126 soc leaves that carry it).
    assert len(boxes) == 2
    # No visibility sub-unit is "this lane could not derive it", never "nothing is near the water".
    assert MG._near_water_boxes(units, None, 0) == ()
    assert MG._near_water_boxes(units, SimpleNamespace(pvs_union=pvs_union), 7) == ()


# --- the three exported water maps ------------------------------------------------------------------

#: What each of the three water maps stages, measured 2026-09-04 off the exported units. Every
#: number here is a fact about the corpus, not a tolerance: the face split is the compiler's, the
#: fluid index selects its own solid, and the near-water set is the PVS union.
WATER_MAP_PINS = {
    "sm_hub_1": {
        "surfaceZCm": -14937.74, "brushes": 1,
        "groups": {"water/sewer_water": 23,
                   "dev/dev_waterbeneath2@cubemapdefault#underside": 24},
        "fluidIndex": 5, "density": None, "pieces": 5, "leafBoxes": 21, "nearBoxes": 97,
        "materialTableWaterIndex": None,
    },
    # 41 `water/invisible_water` faces (18 down-facing, 23 vertical) plus the 9 up-facing faces of
    # the `_depth_33` patch: all 50 are `SURF_NODRAW` and all 50 are kept, which is owner decision 2
    # ("surface on nodraw water") and the whole of this map's swimmable surface.
    # R7.5 look pass: `water/invisible_water` is `%compilenodraw` -- a volume with no drawn surface
    # in VtMB and in the port (owner decision 2 withdrawn against the owner's own frames: the
    # pier's ocean is the `water/blackwater` card below the plane, which a drawn sheet hid). The
    # volume, its fog and its events stay; no face group is meshed.
    "sm_pier_1": {
        "surfaceZCm": -1582.42, "brushes": 1,
        "groups": {},
        "fluidIndex": 5, "density": 1000.0, "pieces": 1, "leafBoxes": 6, "nearBoxes": 569,
        "materialTableWaterIndex": 17,
    },
    # The deep-water witness: 31 `dev_water2_cheap` top and 31 `dev_waterbeneath2` under, 1:1, of
    # which 4 each are the basin's vertical sides and so neither top nor underside.
    "sp_soc_3": {
        "surfaceZCm": -609.6, "brushes": 2,
        "groups": {"dev/dev_water2_cheap@cubemapdefault": 31,
                   "dev/dev_waterbeneath2@cubemapdefault": 4,
                   "dev/dev_waterbeneath2@cubemapdefault#underside": 27},
        "fluidIndex": 15, "density": None, "pieces": 14, "leafBoxes": 40, "nearBoxes": 126,
        "materialTableWaterIndex": None,
    },
}


def _water_corpus(map_name):
    """`(geometry, volumes)` for one exported water map, or a skip when it is not on this machine."""

    unit = MG.sidecars.unit_paths(map_name)["root"]
    if not unit.is_file():
        pytest.skip(f"no exported map root unit at {unit}")
    staging = MG.material_staging_root()
    if not staging.is_dir():
        pytest.skip(f"no material staging tree at {staging}")
    from elysium_pipeline.importers import map_visibility as visibility_lane

    read_sidecar = MG.sidecar_reader(staging)
    geometry = MG.read_geometry(map_name, None, MG.compile_water_predicate(read_sidecar))
    volumes, dropped = MG.resolve_water_volumes(
        geometry.join.units, read_sidecar, map_name, visibility_lane.read_visibility(map_name))
    assert dropped == []
    return geometry, volumes


@pytest.mark.parametrize("map_name", sorted(WATER_MAP_PINS))
def test_the_water_face_split_on_the_exported_corpus(map_name):
    pins = WATER_MAP_PINS[map_name]
    geometry, _volumes = _water_corpus(map_name)
    rows = geometry.water_face_rows()

    assert Counter(row["group"] for row in rows) == Counter(pins["groups"])
    assert len(rows) == sum(pins["groups"].values())
    # `underside` is a fact about the face's own plane and the group key is a function of it, so the
    # two statements have to agree face by face -- a `#underside` group of up-facing faces would
    # bind the reflection-less twin to the surface of the water.
    for row in rows:
        assert row["underside"] is row["group"].endswith(MG.UNDERSIDE_SUFFIX)
        assert row["underside"] == (row["normal"][2] < 0.0)
        assert row["area"] > 0.0 and row["areaCm2"] > 0.0
        # G26/verdict B3, per face: what vbsp computed for the face against what this stage
        # meshed for it. Measured 2026-09-04 across all 159 water faces on the three maps, the
        # worst face is 3e-5 out and every group sums to within 1e-7 -- the 1% here is the
        # contract's tolerance, not the measurement.
        assert row["meshedAreaCm2"] == pytest.approx(row["areaCm2"], rel=0.01)
        assert row["texdata"] >= 0
        # None of the three maps in scope carries a compiled primitive grid; the 512 faces that do
        # are on nine other maps (verdict B4), and the path is pinned on synthetic units above.
        assert row["primitive"] == {"first": 0, "count": 0}
        assert row["triangles"] > 0

    # G26: the area pin, per SECTION -- the unit the bake turns into one mesh section, and the
    # unit `bake_verify` would have had to ask a baked asset for. Every group's staged mesh covers
    # the compiler's own square inches for the faces in it, and the hub's top and underside sheets
    # are the same surface twice.
    by_group: dict[str, float] = {}
    meshed_by_group: dict[str, float] = {}
    for row in rows:
        by_group[row["group"]] = by_group.get(row["group"], 0.0) + row["areaCm2"]
        meshed_by_group[row["group"]] = (
            meshed_by_group.get(row["group"], 0.0) + row["meshedAreaCm2"])
    assert set(by_group) == set(pins["groups"])
    for group, area in by_group.items():
        assert meshed_by_group[group] == pytest.approx(area, rel=0.01), group
    if map_name == "sm_hub_1":
        assert by_group["water/sewer_water"] == pytest.approx(
            by_group["dev/dev_waterbeneath2@cubemapdefault#underside"], rel=1e-9)


@pytest.mark.parametrize("map_name", sorted(WATER_MAP_PINS))
def test_the_water_volume_rows_on_the_exported_corpus(map_name):
    pins = WATER_MAP_PINS[map_name]
    _geometry, volumes = _water_corpus(map_name)

    assert len(volumes) == 1
    volume = volumes[0]
    assert volume.surface_z_cm == pytest.approx(pins["surfaceZCm"], abs=0.01)
    assert len(volume.brushes) == pins["brushes"]

    # G7/verdict B5: the controller's guard is `fluid.index > 0`, so a map that authors one gets
    # entry events -- including the pier, which AUDIT section 12 had guessed did not.
    assert volume.fluid is not None
    assert volume.fluid.index == pins["fluidIndex"] > 0
    assert volume.fluid.density == pins["density"]
    assert volume.fluid.damping == pytest.approx(0.01)
    # The authored plane and the compiler's `surfaceZ` are the same surface: that is the join.
    assert volume.fluid.surface_plane[:3] == (0.0, -0.0, 1.0)
    assert volume.fluid.surface_plane[3] == pytest.approx(volume.surface_z_cm, abs=0.01)
    assert volume.fluid.current_velocity_cm == (0.0, -0.0, 0.0)   # G21: none authored anywhere

    # G18: one piece per ledge of the solid the fluid names, each a closed convex set holding the
    # water it carves.
    assert len(volume.pieces) == pins["pieces"]
    for piece in volume.pieces:
        assert len(piece.planes) >= 4
        assert piece.bounds_min[2] <= piece.bounds_max[2]
    assert any(piece.bounds_max[2] >= volume.surface_z_cm - 1.0 for piece in volume.pieces)

    # G10/G9: the leaf carve and the near-water set.
    assert len(volume.leaf_boxes_cm) == pins["leafBoxes"]
    for low, high in volume.leaf_boxes_cm:
        assert all(a <= b for a, b in zip(low, high))
    assert len(volume.near_boxes_cm) == pins["nearBoxes"] >= len(volume.leaf_boxes_cm)

    # G17: the physics material table's water row, where the map has one.
    assert volume.material_table_water_index == pins["materialTableWaterIndex"]

    row = volume.as_row()
    assert row["fluid"]["index"] == pins["fluidIndex"]
    assert len(row["pieces"]) == pins["pieces"]
    # A piece is a convex the runtime tests bounds-first, exactly like a brush, so the two rows are
    # the same shape and the bake writes them through one struct.
    assert set(row["pieces"][0]) == set(row["brushes"][0]) == {"planes", "boundsCm"}
    assert len(row["leafBoxesCm"]) == pins["leafBoxes"]
    assert len(row["nearBoxesCm"]) == pins["nearBoxes"]


#: `CONTENTS_TESTFOGVOLUME` -- VtMB's own near-water leaf annotation, and the set G9 measured the
#: PVS union against.
_LEAF_NEAR_WATER_BIT = 0x200


@pytest.mark.parametrize("map_name", sorted(WATER_MAP_PINS))
def test_the_pvs_union_reproduces_vtmbs_own_near_water_annotation(map_name):
    """G9/G23. The derivation is the contract because the leaf bit is not always there: it is
    set-equal to the annotation on the two maps that carry it, and it is the only answer on
    `sm_pier_1`, whose Unofficial-Patch recompile carries the bit on no leaf at all."""

    from elysium_pipeline.importers import map_visibility as visibility_lane

    unit = MG.sidecars.unit_paths(map_name)["root"]
    if not unit.is_file() or not visibility_lane.unit_path(map_name).is_file():
        pytest.skip(f"no exported units for {map_name}")

    units = MG.sidecars.read_units(map_name)
    leafs = units.root["bsp"]["leafs"]
    visibility = visibility_lane.read_visibility(map_name)
    assert visibility is not None and visibility.num_clusters > 0

    water_clusters = sorted({int(leaf["cluster"]) for leaf in leafs
                             if int(leaf["leafWaterDataID"]) >= 0 and int(leaf["cluster"]) >= 0})
    near = visibility.pvs_union(water_clusters)
    derived = {int(leaf["index"]) for leaf in leafs
               if int(leaf["cluster"]) in near and not int(leaf["contents"]) & MG.CONTENTS_SOLID}
    annotated = {int(leaf["index"]) for leaf in leafs
                 if int(leaf["contents"]) & _LEAF_NEAR_WATER_BIT}

    assert water_clusters and derived
    if annotated:
        assert derived == annotated                  # sm_hub_1: 97, sp_soc_3: 126
    else:
        # sm_pier_1: retail marks 702 leaves, the UP recompile marks none, and the derivation still
        # answers -- which is why owner decision 1 (stay on the UP build) costs nothing here.
        assert map_name == "sm_pier_1"
        assert len(derived) > len(water_clusters)


# ---------------------------------------------------------- the brush-entity lightstyle carrier

# R7.4 contract 3 reaches a world/sky chunk through the `elysium.style=<n>` tag the bake writes on
# the chunk ACTOR. A brush entity's mesh is never placed, so its style rides the mesh's material
# slot names instead -- and that only works because `safe_name` folds `#style<n>` to `_style<n>`.
# `sm_pier_1`'s 17 `objects/surf` foam bodies, all of G6's motivating geometry, arrive this way.


def test_the_bakes_restated_lightstyle_suffix_is_the_key_grammars_own() -> None:
    # The bake runs in the editor's embedded Python, which has no numpy and therefore cannot import
    # `map_geometry` at all. `asset_names` restates the one suffix it needs; the two are one fact.
    assert asset_names.LIGHTSTYLE_KEY_SUFFIX == MG.LIGHTSTYLE_SUFFIX
    assert asset_names.safe_name(asset_names.LIGHTSTYLE_KEY_SUFFIX + "1") ==         (asset_names.FOLDED_LIGHTSTYLE_MARKER + "1").lstrip("_")


def test_a_styled_group_key_folds_to_the_marker_the_brush_slot_rule_reads() -> None:
    slot = asset_names.safe_name(MG.section_key("objects/surf", light_style=1))
    assert slot == "objects_surf" + asset_names.FOLDED_LIGHTSTYLE_MARKER + "1"
    assert asset_names.brush_slot_style([slot]) == 1


def test_an_unstyled_group_key_folds_to_no_brush_style() -> None:
    key = MG.section_key("objects/surf")
    assert asset_names.brush_slot_style([asset_names.safe_name(key)]) == 0


def test_the_underside_suffix_does_not_read_as_a_style() -> None:
    key = MG.section_key("water/invisible_water", underside=True)
    assert asset_names.brush_slot_style([asset_names.safe_name(key)]) == 0
