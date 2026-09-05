from elysium_pipeline.asset_paths import baked_unit
from elysium_pipeline.importers import materials, material_consumers
from elysium_pipeline.formats.unit_contract.container import encode_glb
import pytest


def entry(key, parent=None):
    id = "vtmb:material:" + key
    return {"unit": id, "assetPath": baked_unit(id, "MI"),
            "parent": parent or materials.MASTER_ROOT + "/M_V2_Lit", "patched": bool(parent),
            "decalAsset": None, "undersideAsset": None, "textures": {"NormalMap": "T_normal", "EnvMap": "TC_probe"},
            "scalars": {"Alpha": .5}, "vectors": {}, "switches": {}, "allSwitches": [],
            "basePropertyOverrides": {"blendMode": "Opaque", "twoSided": False},
            "sourceAlphaTest": True, "recipe": {}}


def provenance(entries):
    return {e["assetPath"]: {"master": materials.MASTER_ROOT + "/M_V2_Lit"} for e in entries}


def test_map_face_consumer_keeps_world_material_and_routes_skeletal_twin():
    e = entry("metal/walkwayb")
    p = provenance([e])
    rows, report = material_consumers.route([e], p,
        {e["unit"]: {"skeletal": ["vent"], "static": [], "map": ["tutorial"]}},
        materials.MASTER_ROOT, materials.EXPOSED_PARAMS)
    assert report["skinnedTwins"] == 1
    world, skinned = rows
    assert world["parent"].endswith("/M_V2_Lit")
    assert skinned["assetPath"] == e["assetPath"] + "_Skinned"
    assert skinned["parent"].endswith("/M_V2_LitSkinned")
    assert skinned["textures"] == world["textures"]
    assert skinned["basePropertyOverrides"]["blendMode"] == "Masked"
    assert skinned["switches"]["UseAlphaTest"]
    assert skinned["scalars"]["ModelAlpha"] == 1.
    assert p[e["assetPath"]]["skinnedAsset"] == skinned["assetPath"]


def test_patch_consumers_propagate_before_parent_routing():
    base = entry("base")
    world = entry("world_patch", base["assetPath"])
    skin = entry("skin_patch", base["assetPath"])
    p = provenance([base, world, skin])
    rows, _ = material_consumers.route([base, world, skin], p,
        {world["unit"]: {"map": ["map"]}, skin["unit"]: {"skeletal": ["body"]}},
        materials.MASTER_ROOT, materials.EXPOSED_PARAMS)
    by = {e["assetPath"]: e for e in rows}
    assert world["parent"] == base["assetPath"]
    assert skin["parent"] == base["assetPath"] + "_Skinned"
    assert by[skin["parent"]]["parent"].endswith("/M_V2_LitSkinned")


def test_only_drawn_map_faces_add_static_usage(tmp_path):
    id = "vtmb:material:metal/walkwayb"
    model = {"identity": {"asset": "vtmb:model:vent", "shape": "skeletal"},
             "mdl": {"bones": [{}]},
             "materialBindings": {"slots": [{"material": id}], "skinFamilies": [[id]]}}
    path = tmp_path / "models/vent.glb"
    path.parent.mkdir()
    path.write_bytes(encode_glb({"extensions": {"ELYSIUM_vtmb_model": model}}))
    world = {"identity": {"asset": "vtmb:map:map"},
             "faces": [{"tool": False, "noDraw": False, "texInfo": 0},
                       {"tool": False, "noDraw": True, "texInfo": 1}],
             "texinfos": [{"texData": 0}, {"texData": 1}],
             "textures": [{"asset": id}, {"asset": "vtmb:material:invisible"}]}
    path = tmp_path / "maps/map.glb"
    path.parent.mkdir()
    path.write_bytes(encode_glb({"extensions": {"ELYSIUM_vtmb_map": world}}))
    result = material_consumers.collect(tmp_path)
    assert result[id] == {"skeletal": ["vtmb:model:vent"], "static": [], "map": ["vtmb:map:map"]}
    assert "vtmb:material:invisible" not in result


def test_rigid_cloth_model_keeps_both_material_consumers(tmp_path):
    material = "vtmb:material:cloth/table"
    model = {"identity": {"asset": "vtmb:model:scenery/table", "family": "scenery", "shape": "static"},
             "mdl": {"bones": [{}]}, "cloth": {"garments": [{}]},
             "materialBindings": {"slots": [{"material": material}], "skinFamilies": [[material]]}}
    path = tmp_path / "models/table.glb"
    path.parent.mkdir()
    path.write_bytes(encode_glb({"extensions": {"ELYSIUM_vtmb_model": model}}))
    result = material_consumers.collect(tmp_path)[material]
    assert result["skeletal"] == result["static"] == ["vtmb:model:scenery/table"]


def test_skinned_twin_cannot_alias_an_authored_material_name():
    base, conflicting = entry("body"), entry("body_Skinned")
    with pytest.raises(ValueError, match="collision"):
        material_consumers.route([base, conflicting], provenance([base, conflicting]),
            {base["unit"]: {"skeletal": ["body"], "map": ["map"]}},
            materials.MASTER_ROOT, materials.EXPOSED_PARAMS)


def test_rigid_character_still_requests_skeletal_materials(tmp_path):
    id = "vtmb:material:models/character/gibs/eyeball"
    unit = {"identity": {"asset": "vtmb:model:character/gibs/left_eye", "shape": "static", "family": "character"},
            "mdl": {"bones": [{"name": "eye"}]},
            "materialBindings": {"slots": [{"material": id}], "skinFamilies": [[id]]}}
    path = tmp_path / "models/eye.glb"
    path.parent.mkdir()
    path.write_bytes(encode_glb({"extensions": {"ELYSIUM_vtmb_model": unit}}))
    usage = material_consumers.collect(tmp_path)[id]
    assert usage["skeletal"] == [unit["identity"]["asset"]]
    assert usage["static"] == usage["skeletal"]
