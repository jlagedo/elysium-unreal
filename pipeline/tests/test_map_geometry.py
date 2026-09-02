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

import json

import pytest

from elysium_pipeline import map_transport, paths, shared_corpus
from elysium_pipeline.formats.bsp import source_quat_to_unreal, source_to_unreal
from elysium_pipeline.importers import map_geometry as MG

#: The three-map working corpus (`seam_migration.md` -> R1); a whole-corpus run is a separate,
#: owner-approved step and this module never asks for one.
WORKING_MAPS = ("sp_tutorial_1", "sm_pawnshop_1", "sm_hub_1")


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


def test_the_v2_model_flag_is_its_own_list_and_excludes_sp_theatre():
    # sp_theatre is on the R4.6 entity/collision/environment transport but its models have not been
    # imported, so it must not be carried onto the V2 model root by reusing that list.
    v2 = map_transport.read_array(map_transport.V2_MODELS_KEY)
    transport = map_transport.read_array(map_transport.NEW_TRANSPORT_KEY)
    assert set(v2) == set(WORKING_MAPS)
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

    # A `$decal` world face the legacy lane bound to the deferred-decal master is a class change
    # on the rebind (it renders as an ordinary translucent surface now).
    decal = MG.MaterialBinding(
        key="decals/n0", unit="vtmb:material:decals/n0", asset="/ElysiumBaked/Materials/decals/MI_n0",
        master="M_V2_LitTranslucent", blend_mode="Translucent", patched=False, provenance="decals/n0")
    row = MG.classify_material(decal, {**_BASE, "isDecalSurface": True}, {"decal": True})
    assert (row["legacyClass"], row["v2Class"], row["classChanged"]) == ("decal", "translucent", True)
    assert row["isDecalSurface"] is True

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
    # The manifest the editor half reads bumped for the new table; the two constants are restated
    # on either side of the numpy boundary and have to agree.
    assert MG.MANIFEST_VERSION == 3


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
