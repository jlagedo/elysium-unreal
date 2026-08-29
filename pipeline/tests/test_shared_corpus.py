import tempfile
import unittest
from pathlib import Path

from elysium_pipeline import shared_corpus as SC
from elysium_pipeline.formats import mdl
import pytest


def test_texture_key_accepts_every_authored_spelling():
    for spelling in (
        "metal/metalox",
        "Metal\\MetalOx",
        "/metal//metalox",
        "materials/metal/metalox",
        "materials/metal/metalox.tth",
        "metal/metalox.vtf",
    ):
        assert SC.texture_key(spelling) == "metal/metalox", spelling


def test_material_key_drops_the_prefix_and_the_extension():
    assert SC.material_key("materials\\Models/Scenery/spike.vmt") == "models/scenery/spike"


def test_static_stem_folds_the_whole_path_including_models():
    assert SC.static_stem("models/items/Rings/Ground/Ring03.mdl") == "models_items_rings_ground_ring03"


def test_static_stem_keeps_the_characters_the_c_twin_keeps():
    # FElysiumContentPaths::PropModelStem replaces one-for-one and keeps '.', '_' and '-'.
    assert SC.static_stem("models/scenery/a-b/c.d e.mdl") == "models_scenery_a-b_c.d_e"


class PropMaterialKeyNormalizationTests(unittest.TestCase):
    """One material key, one fold, on both sides of the join.

    A model header states its texture search paths and its material names in the install's own
    mixed case; `shared/materials.json` is keyed by `material_key`, which is lower case. Every
    lookup against that document -- `bake_lib.read_mtl`'s above all -- is case-sensitive, so a
    `.mtl` naming the header's spelling joins nothing and the mesh bakes with a null material.
    """

    SEARCH = ["models/scenery/furniture/MilkCrate/"]
    KEY = "models/scenery/furniture/milkcrate/milkcrate"
    VMT = b'"VertexLitGeneric"\n{\n"$basetexture" "models/scenery/milkcrate"\n}\n'

    def _read(self, key):
        # The install index is case-folded (`install.read` lowers), so a mixed-case candidate
        # reads the same file a lower-case one would.
        return self.VMT if key.lower() == "materials/%s.vmt" % self.KEY else None

    def test_the_recorded_key_is_the_one_the_corpus_document_carries(self):
        channels = mdl.material_channels("MilkCrate", self.SEARCH, self._read)
        assert channels["vmt"] == self.KEY
        assert channels["vmt"] == SC.material_key(channels["vmt"])

    def test_a_prop_mtl_names_that_key(self):
        mesh = mdl.Mesh("MilkCrate")
        mesh.verts = [(1.0, 2.0, 3.0, 0.0, 0.0),
                      (2.0, 2.0, 3.0, 1.0, 0.0),
                      (1.0, 3.0, 3.0, 0.0, 1.0)]
        mesh.tris = [(0, 1, 2)]
        with tempfile.TemporaryDirectory() as out:
            mdl.write_obj_scene([mesh], "crate", out, self.SEARCH, self._read, {})
            mtl = (Path(out) / "crate.mtl").read_text(encoding="utf-8")
        assert "mat %s\n" % self.KEY in mtl


class MaterialResolutionTests(unittest.TestCase):
    """`resolve_vmt` is the engine's own walk and only that walk (research case
    `material-resolution`): the model header's search paths, in header order, each composed into
    `materials/<path><name>.vmt`, with nothing after the last one."""

    HIT = b'"VertexLitGeneric"\n{\n"$basetexture" "models/spike"\n}\n'

    def test_the_header_search_paths_are_tried_in_header_order(self):
        tried = []

        def read(key):
            tried.append(key)
            return self.HIT if key == "materials/models/props/second/spike.vmt" else None

        path, info = mdl.resolve_vmt(
            "spike", ["models/props/first/", "models/props/second/"], read)
        assert info is not None
        assert path == "models/props/second/spike"
        assert tried == ["materials/models/props/first/spike.vmt",
                                 "materials/models/props/second/spike.vmt"]

    def test_a_flat_material_is_not_a_last_resort(self):
        # The engine composes one path per search path and stops; there is no global
        # `materials/<name>.vmt` step, so a name that only exists flat misses.
        tried = []

        def read(key):
            tried.append(key)
            return self.HIT if key == "materials/spike.vmt" else None

        assert mdl.resolve_vmt("spike", ["models/props/"], read) == (None, None)
        assert tried == ["materials/models/props/spike.vmt"]

    def test_a_model_with_no_search_path_resolves_nothing(self):
        assert mdl.resolve_vmt("spike", [], lambda key: self.HIT) == (None, None)

    def test_a_world_name_resolves_against_the_materials_root(self):
        # A world or decal material's authored name is already its path, and the engine's brush
        # path composes exactly `materials/<name>.vmt` for it.
        tried = []

        def read(key):
            tried.append(key)
            return self.HIT if key == "materials/brick/brickwall001a.vmt" else None

        path, info = mdl.resolve_vmt("brick/brickwall001a", mdl.WORLD_SEARCH, read)
        assert path == "brick/brickwall001a"
        assert tried == ["materials/brick/brickwall001a.vmt"]

    def test_a_total_miss_answers_no_material(self):
        assert mdl.material_channels("spike", ["models/props/"], lambda key: None) is None

    def test_a_missed_slot_writes_newmtl_with_no_mat_line(self):
        """The bake's signal for a miss is the absence of the `mat` line, which is what lets it
        bind the error material on exactly that slot."""
        mesh = mdl.Mesh("spike")
        mesh.verts = [(1.0, 2.0, 3.0, 0.0, 0.0),
                      (2.0, 2.0, 3.0, 1.0, 0.0),
                      (1.0, 3.0, 3.0, 0.0, 1.0)]
        mesh.tris = [(0, 1, 2)]
        with tempfile.TemporaryDirectory() as out:
            mdl.write_obj_scene([mesh], "spike", out, ["models/props/"], lambda key: None, {})
            mtl = (Path(out) / "spike.mtl").read_text(encoding="utf-8")
        assert "newmtl spike\n" in mtl
        assert "mat " not in mtl


def test_an_unpatched_face_keys_by_its_authored_material():
    assert SC.world_material_key("art/bdiorama1", "sm_hub_1") == "art/bdiorama1"


def test_a_map_wide_cubemap_face_carries_the_default_tag():
    assert SC.world_material_key("maps/sm_hub_1/asphalt/asphaltasan", "sm_hub_1") == "asphalt/asphaltasan@cubemapdefault"


def test_a_positioned_cubemap_face_carries_its_own_cube():
    assert SC.world_material_key("maps/sm_hub_1/glass/glass01_660_671_73", "sm_hub_1") == "glass/glass01@c660_671_73"


def test_another_maps_patch_is_not_read_as_this_maps_cubemap():
    # base_material strips any map's prefix, but only the owning map's patch names a cube.
    assert SC.cubemap_of("maps/la_hub_1/glass/glass01", "sm_hub_1") is None


def test_two_maps_naming_one_material_resolve_to_one_key():
    # The regression this corpus exists for: the same authored surface in two maps was two
    # assets, free to disagree.
    first = SC.world_material_key("brick/brickwall001a", "sm_hub_1")
    second = SC.world_material_key("brick/brickwall001a", "sm_junkyard_1")
    assert first == second


def test_a_plain_material_is_shared():
    assert not SC.is_map_scoped_material("brick/brickwall001a")


def test_a_cubemap_patched_material_stays_with_its_map():
    assert SC.is_map_scoped_material("asphalt/asphaltasan@cubemapdefault")


def test_a_decal_stays_with_its_map():
    assert SC.is_map_scoped_material("decals/stains/blooda", decal=True)


def test_a_wetness_driven_surface_stays_with_its_map():
    assert SC.is_map_scoped_material("concrete/wet", wetness_driven=True)


def test_a_material_only_this_maps_pakfile_carries_stays_with_its_map():
    """The corpus reads the install, so it never holds a VBSP-written map-local material.

    Called shared, no package would author it and the surface would bind the master's own
    placeholder rather than failing.
    """
    assert SC.is_map_scoped_material(
        "maps/sm_pier_1/water/invisible_water_depth_33", local=True)


def test_control_rig_illegal_characters_fold_to_underscores():
    from elysium_pipeline.asset_names import rig_bone_name
    assert rig_bone_name("[2]upper_teeth") == "_2_upper_teeth"
    assert rig_bone_name("[2]GeoSphere02") == "_2_GeoSphere02"


def test_legal_names_pass_verbatim():
    from elysium_pipeline.asset_names import rig_bone_name
    for name in ("Bip01 L Finger0", "lower_teeth", "bush hook", "a-b.c|d"):
        assert rig_bone_name(name) == name


def test_a_leading_space_folds_and_a_later_space_does_not():
    from elysium_pipeline.asset_names import rig_bone_name
    assert rig_bone_name(" tail bone") == "_tail bone"


def test_every_product_of_one_key_is_a_distinct_file():
    key = "models/scenery/structural/doorknoba/doorknob1"
    names = {SC.texture_file(key, suffix) for suffix in SC.ROLES}
    assert len(names) == len(SC.ROLES)
    assert "models_scenery_structural_doorknoba_doorknob1.png" in names
    assert "models_scenery_structural_doorknoba_doorknob1_ke.png" in names


def test_the_corpus_relative_path_is_what_materials_json_records():
    assert SC.texture_rel("metal/metalox", SC.NORMAL) == "tex/metal_metalox_n.png"


def test_a_sky_face_is_addressed_by_the_sky_not_by_the_map():
    # The map export writes these as the map-local alias `sky_<face>`, which is why two maps
    # sharing one sky held two copies under one name.
    assert SC.sky_face_file("nightsky1", "bk") == "skybox_nightsky1bk.png"
    assert SC.sky_face_file("nightsky1", "bk") == SC.sky_face_file("NightSky1", "bk")


def test_asset_names_follow_the_established_folds():
    assert SC.texture_asset("metal_metalox_n.png") == "T_metal_metalox_n"
    assert SC.material_asset("models/scenery/spike") == "MI_models_scenery_spike"
    assert SC.mesh_asset("models_scenery_doorknoba") == "SM_models_scenery_doorknoba"


def _textures():
    return {
        "metal/metalox": {
            "files": {"metal_metalox.png": "albedo"}, "alpha": False},
        "glass/glass01": {
            "files": {"glass_glass01.png": "albedo",
                      "glass_glass01_n.png": "normal"}, "alpha": True},
    }


def test_two_keys_folding_to_one_file_name_is_a_named_failure():
    with pytest.raises(ValueError) as caught:
        SC.build_manifest(
            textures={"a/b c": {"files": {"a_b_c.png": "albedo"}},
                      "a/b_c": {"files": {"a_b_c.png": "albedo"}}},
            materials={}, models={})
    assert "collision" in str(caught.value)


def test_the_fingerprint_ignores_discovery_order():
    first = SC.corpus_fingerprint(["b", "a"], ["y", "x"], ["q", "p"])
    second = SC.corpus_fingerprint(["a", "b"], ["x", "y"], ["p", "q"])
    assert first == second
    assert first != SC.corpus_fingerprint(["a"], ["x", "y"], ["p", "q"])


def test_a_stale_version_names_the_command_that_regenerates_it():
    document = SC.build_manifest(textures={}, materials={}, models={})
    document["version"] = SC.VERSION + 1
    with pytest.raises(ValueError) as caught:
        SC.check_manifest(document)
    assert "export bundle corpus" in str(caught.value)


def test_a_placed_stem_the_corpus_never_decoded_is_named():
    document = SC.build_manifest(
        textures={}, materials={},
        models={"models_scenery_doorknoba": {"model": "models/scenery/doorknoba.mdl"}})
    placed = ["models_scenery_doorknoba", "models_scenery_ashtray",
              "models_scenery_ashtray", ""]
    assert SC.missing_models(document["models"], placed) == ["models_scenery_ashtray"]


def test_every_texture_bearing_field_is_declared_a_channel():
    """`CHANNEL_FIELDS` is how a texture is traced back to the materials that draw it.

    A field that states a decoded file but is left out of the list makes that texture
    untraceable, so `export texture` would re-decode nothing and report success.
    """
    record = SC.material_record({
        "albedo": "metal/metalox", "selfillum": True, "bump": "metal/metalox_bump",
        "refract_map": "glass/warp", "envmap": "env_cubemap", "envmask": "metal/mask",
        "base_tex2": "ground/dirt", "water": True, "water_normal": "water/ripples",
    })
    stated = {name for name, value in record.items()
              if isinstance(value, str) and value.startswith(SC.TEX + "/")}
    assert set(SC.CHANNEL_FIELDS) == stated


class MaterialRecordTests(unittest.TestCase):
    def _channels(self, **overrides):
        base = {
            "material": "spike", "vmt": "models/scenery/spike", "albedo": "models/scenery/spike",
            "selfillum": False, "additive": False, "translucent": False,
            "alphatest": False, "glass": False, "envmap": "", "envmap_path": "", "envmask": "",
            "envmask_from_alpha": False, "envtint": None, "globalwetness": None, "bump": "",
            "base_tex2": "", "refract": False, "refract_amount": 0.0, "refract_map": "",
            "refract_is_dudv": False, "iris": "", "vampire": False, "water": False,
            "water_normal": "", "water_fog_color": None, "water_fog_start": None,
            "water_fog_end": None, "water_reflect_tint": None, "decal_scale": 0.0, "unlit": False,
        }
        base.update(overrides)
        return base

    def test_a_channel_whose_file_the_corpus_lacks_is_stated_empty(self):
        # A record that pointed at a file the decode never wrote would bake as the master's own
        # placeholder, which is exactly the silent grey surface the guard exists to prevent.
        record = SC.material_record(self._channels(), files=set())
        assert record["albedo"] == ""
        assert record["albedo_key"] == "models/scenery/spike"

    def test_a_present_channel_is_stated_corpus_relative(self):
        record = SC.material_record(
            self._channels(), files={"models_scenery_spike.png"})
        assert record["albedo"] == "tex/models_scenery_spike.png"

    def test_without_a_file_set_every_named_channel_is_stated(self):
        # `files=None` is "state what the VMT names": the caller has no decoded set to check
        # against. Every caller that does have one passes it, including the map exporter's
        # PAKFILE-local fallback -- a local material can name a texture the corpus never decoded.
        record = SC.material_record(self._channels())
        assert record["albedo"] == "tex/models_scenery_spike.png"

    def test_an_authored_bumpmap_outranks_the_derived_glass_normal(self):
        record = SC.material_record(
            self._channels(glass=True, bump="models/scenery/authored"))
        assert record["bump"] == "tex/models_scenery_authored_n.png"

    def test_semantic_glass_falls_back_to_the_normal_derived_from_its_albedo(self):
        record = SC.material_record(self._channels(glass=True))
        assert record["bump"] == "tex/models_scenery_spike_glass_n.png"

    def test_a_water_material_carries_its_own_normal_and_fog(self):
        # The Water shader names no $basetexture, so a record built only from albedo would drop
        # every canal and sewer in the game.
        record = SC.material_record(self._channels(
            albedo="", water=True, water_normal="dev/water_normal",
            water_fog_end=1024.0, water_reflect_tint=[0.5, 0.6, 0.7]))
        assert record["water"]
        assert record["water_normal"] == "tex/dev_water_normal_n.png"
        assert record["water_fog_end"] == 1024.0
        assert record["water_reflect_tint"] == [0.5, 0.6, 0.7]

    def test_the_base_alpha_env_mask_reads_the_albedo_key(self):
        record = SC.material_record(
            self._channels(envmap="env_cubemap", envmask_from_alpha=True))
        assert record["env_mask"] == "tex/models_scenery_spike_envmask.png"
