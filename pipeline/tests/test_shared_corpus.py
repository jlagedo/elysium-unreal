import tempfile
from pathlib import Path

from elysium_pipeline import shared_corpus as SC
from elysium_pipeline.formats import mdl


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


SEARCH = ["models/scenery/furniture/MilkCrate/"]

KEY = "models/scenery/furniture/milkcrate/milkcrate"

VMT = b'"VertexLitGeneric"\n{\n"$basetexture" "models/scenery/milkcrate"\n}\n'


# PropMaterialKeyNormalizationTests
# One material key, one fold, on both sides of the join.
#
# A model header states its texture search paths and its material names in the install's own
# mixed case; `shared/materials.json` is keyed by `material_key`, which is lower case. Every
# lookup against that document -- `bake_lib.read_mtl`'s above all -- is case-sensitive, so a
# `.mtl` naming the header's spelling joins nothing and the mesh bakes with a null material.

def _read(key):
    # The install index is case-folded (`install.read` lowers), so a mixed-case candidate
    # reads the same file a lower-case one would.
    return VMT if key.lower() == "materials/%s.vmt" % KEY else None


def test_the_recorded_key_is_the_one_the_corpus_document_carries():
    channels = mdl.material_channels("MilkCrate", SEARCH, _read)
    assert channels["vmt"] == KEY
    assert channels["vmt"] == SC.material_key(channels["vmt"])


def test_a_prop_mtl_names_that_key():
    mesh = mdl.Mesh("MilkCrate")
    mesh.verts = [(1.0, 2.0, 3.0, 0.0, 0.0),
                  (2.0, 2.0, 3.0, 1.0, 0.0),
                  (1.0, 3.0, 3.0, 0.0, 1.0)]
    mesh.tris = [(0, 1, 2)]
    with tempfile.TemporaryDirectory() as out:
        mdl.write_obj_scene([mesh], "crate", out, SEARCH, _read, {})
        mtl = (Path(out) / "crate.mtl").read_text(encoding="utf-8")
    assert "mat %s\n" % KEY in mtl


HIT = b'"VertexLitGeneric"\n{\n"$basetexture" "models/spike"\n}\n'


# MaterialResolutionTests
# `resolve_vmt` is the engine's own walk and only that walk (research case
# `material-resolution`): the model header's search paths, in header order, each composed into
# `materials/<path><name>.vmt`, with nothing after the last one.

def test_the_header_search_paths_are_tried_in_header_order():
    tried = []

    def read(key):
        tried.append(key)
        return HIT if key == "materials/models/props/second/spike.vmt" else None

    path, info = mdl.resolve_vmt(
        "spike", ["models/props/first/", "models/props/second/"], read)
    assert info is not None
    assert path == "models/props/second/spike"
    assert tried == ["materials/models/props/first/spike.vmt",
        "materials/models/props/second/spike.vmt"]


def test_a_flat_material_is_not_a_last_resort():
    # The engine composes one path per search path and stops; there is no global
    # `materials/<name>.vmt` step, so a name that only exists flat misses.
    tried = []

    def read(key):
        tried.append(key)
        return HIT if key == "materials/spike.vmt" else None

    assert mdl.resolve_vmt("spike", ["models/props/"], read) == (None, None)
    assert tried == ["materials/models/props/spike.vmt"]


def test_a_model_with_no_search_path_resolves_nothing():
    assert mdl.resolve_vmt("spike", [], lambda key: HIT) == (None, None)


def test_a_world_name_resolves_against_the_materials_root():
    # A world or decal material's authored name is already its path, and the engine's brush
    # path composes exactly `materials/<name>.vmt` for it.
    tried = []

    def read(key):
        tried.append(key)
        return HIT if key == "materials/brick/brickwall001a.vmt" else None

    path, info = mdl.resolve_vmt("brick/brickwall001a", mdl.WORLD_SEARCH, read)
    assert path == "brick/brickwall001a"
    assert tried == ["materials/brick/brickwall001a.vmt"]


def test_a_total_miss_answers_no_material():
    assert mdl.material_channels("spike", ["models/props/"], lambda key: None) is None


def test_a_missed_slot_writes_newmtl_with_no_mat_line():
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


# RigBoneNameTests
# `rig_bone_name` must be a fixed point of Control Rig's own sanitizer: every character it
# emits is one `URigHierarchy::SanitizeName` keeps, or the track and the bone diverge again.

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
