"""The V2 map bake's geometry and placement reader (R5.1).

`docs/architecture/seam_map_map.md` -> "## Import -- geometry and placements" states where a map's
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

import pytest

from elysium_pipeline import map_transport, paths, shared_corpus
from elysium_pipeline.formats.bsp import source_quat_to_unreal, source_to_unreal
from elysium_pipeline.importers import map_geometry as MG


def _bake_map_v2_manifest_version() -> int:
    """`bake_map_v2.MANIFEST_VERSION`, read as text: that module imports `unreal`."""
    source = (Path(__file__).resolve().parents[1] / "unreal" / "bake_map_v2.py").read_text(
        encoding="utf-8")
    found = re.search(r"^MANIFEST_VERSION = (\d+)$", source, re.MULTILINE)
    assert found, "bake_map_v2.py states no MANIFEST_VERSION"
    return int(found.group(1))

#: The three-map working corpus (`seam_migration.md` -> R1); a whole-corpus run is a separate,
#: owner-approved step and this module never asks for one.
WORKING_MAPS = ("sp_tutorial_1", "sm_pawnshop_1", "sm_hub_1")
#: The maps on the V2 model root. R7.1 adds `sm_pier_1` (the water scope) and, on the owner's call
#: of 2026-09-04, `sp_soc_3` -- the deep-water witness (Society of Leopold: a 464-inch
#: `dev_water2_cheap` basin, 40 drip emitters). Both are in `MapsOnV2Models` in
#: `Config/DefaultElysium.ini` and neither is in `WORKING_MAPS`, because the parametrized corpus
#: cases above walk the three-map corpus and neither ruling authorized a wider run of those.
V2_MODEL_MAPS = WORKING_MAPS + ("sm_pier_1", "sp_soc_3")


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
    # VPHYSICS placements of models that ship no `.phy` (the ruling in seam_map_map.md).
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
        assert {key: len(indices) // 3 for key, indices in scene.groups.items()} == groups

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
# `seam_map_map.md` -> "Detail props (R6.3)": every `dprp` record becomes one instance of its
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


def test_the_v2_model_flag_is_its_own_list_and_excludes_sp_theatre():
    # sp_theatre is on the R4.6 entity/collision/environment transport but its models have not been
    # imported, so it must not be carried onto the V2 model root by reusing that list.
    v2 = map_transport.read_array(map_transport.V2_MODELS_KEY)
    transport = map_transport.read_array(map_transport.NEW_TRANSPORT_KEY)
    assert set(v2) == set(V2_MODEL_MAPS)
    assert "sp_theatre" in transport
    assert map_transport.is_map_on_v2_models("sp_theatre") is False
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
# `seam_map_map.md` -> "## Import -- materials (R5.4)": every face group binds the imported `MI_`
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
    # `seam_map_map.md` -> "Import -- reflection captures (R5.5)": position is the node translation
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
