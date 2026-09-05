from copy import deepcopy

import pytest

from elysium_pipeline.importers.character_data import mesh_projection, MaterialFacts


def document():
    eye = {"index": 0, "bone": 0, "up": [1., 0., 0.], "forward": [0., 1., 0.],
           "zoffset": .1, "radius": .5, "iris_scale": 2., "upperflexdesc": [0, 1, 2],
           "lowerflexdesc": [-1, -1, -1], "uppertarget": [-.1, .2, .3], "lowertarget": [0., 0., 0.],
           "upperlidflexdesc": 0, "lowerlidflexdesc": -1}
    return {"nodes": [{"children": [1]}, {"translation": [.1, .2, .3],
            "extensions": {"ELYSIUM_vtmb_model": {"eyeballIndex": 0, "bone": 0}}}],
            "extensions": {"ELYSIUM_vtmb_model": {
                "identity": {"asset": "vtmb:model:character/body"},
                "mdl": {"bones": [{"name": "head", "flags": 2}], "header": {"phonemeFilter": [.08, .1]},
                        "textures": [{"name": "Eye_Left"}], "skinTable": [[0], [0]],
                        "bodyParts": [{"index": 0, "models": [{"index": 0, "eyeballs": [eye],
                                        "meshes": [{"materialType": 1, "materialParam": 0, "material": 0}]}]}]},
                "facial": {"flexDescriptions": ["morph"], "controllers": [{"name": "jaw", "type": "phoneme", "min": -1., "max": 1.}],
                           "rules": [{"flexDescription": 0, "operations": [{"operation": "FETCH1", "index": 0},
                                     {"operation": "CONST", "value": .5}, {"operation": "DIV", "rawOperandBits": 123}]}],
                           "morphTargets": [{"name": "morph", "flexDescription": 0, "targets": [0., 1., 2., 3.]}],
                           "mouths": [{"bone": 0, "forward": [0., 1., 0.], "flexDescription": 0}], "selectedTables": []},
                "procedural": {"axisInterpolation": [{"bone": "head", "boneIndex": 0, "control": "head", "controlIndex": 0,
                    "axisIndex": 0, "pos": [[1., 2., 3.]] * 6, "quat": [[0., 0., 0., 1.]] * 6}]},
                "materialBindings": {"slots": [{"slot": 0, "sourceName": "Eye_Left", "material": "vtmb:material:eye"}],
                                     "skinFamilies": [["vtmb:material:eye"], ["vtmb:material:alternate"]]},
            }}}


def material(id):
    return {"assetPath": "/ElysiumBaked/Materials/MI_" + id.rsplit(":", 1)[-1],
            "textures": {"Iris": "/ElysiumBaked/Textures/T_iris"}, "scalars": {"Vampire": 1.}}


def test_projection_preserves_rpn_and_converts_eye_origins_from_core_nodes():
    source = document()
    untouched = deepcopy(source)
    result = mesh_projection(source, material)
    assert source == untouched
    assert result["facial"]["rules"] == [{"flexdesc": 0, "ops": [["FETCH1", 0], ["CONST", .5], ["DIV"]]}]
    assert result["facial"]["controllers"][0]["min"] == -1.
    assert result["facial"]["mouths"][0]["forward"] == [0., -1., 0.]
    assert result["facial"]["phoneme_filter"] == [.08, .1]
    eye = result["eyes"]["eyeballs"][0]
    assert eye["org"] == pytest.approx([10., 30., 20.])
    assert eye["forward"] == [0., -1., 0.]
    assert eye["radius"] == .5 and eye["uppertarget"] == [-.1, .2, .3]
    assert eye["material"] == "Eye_Left"
    assert eye["iris_asset"] == "/ElysiumBaked/Textures/T_iris.T_iris"
    assert eye["vampire"]
    assert result["skinFamilies"][1] == ["/ElysiumBaked/Materials/MI_alternate.MI_alternate"]
    assert result["splitBones"] == ["head"]
    assert result["composition"]["rules"][0]["pos"][0] == pytest.approx([2.54, -5.08, 7.62])


def test_unmatched_eye_node_is_a_projection_failure():
    source = document()
    source["nodes"][0]["children"] = []
    with pytest.raises(ValueError, match="not local to its declared bone"):
        mesh_projection(source, material)


def test_unknown_facial_opcode_cannot_be_silently_removed():
    source = document()
    source["extensions"]["ELYSIUM_vtmb_model"]["facial"]["rules"][0]["operations"].append({"operation": "UNKNOWN"})
    with pytest.raises(ValueError, match="unhandled facial operation"):
        mesh_projection(source, material)


def test_eye_without_a_material_mesh_retains_its_record_and_reports_the_source_gap():
    source = document()
    source["extensions"]["ELYSIUM_vtmb_model"]["mdl"]["bodyParts"][0]["models"][0]["meshes"] = []
    result = mesh_projection(source, material)
    assert len(result["eyes"]["eyeballs"]) == 1
    assert result["eyes"]["eyeballs"][0]["material"] == ""
    assert result["anomalies"][0]["code"] == "eyeHasNoMaterialMesh"


def test_eye_material_facts_follow_patch_parent_and_skinned_route():
    base = {"assetPath": "/ElysiumBaked/Materials/MI_eye_Skinned", "patched": False,
            "textures": {"Iris": "/ElysiumBaked/Textures/T_iris"}, "scalars": {"Vampire": 1}}
    route = {"assetPath": "/ElysiumBaked/Materials/MI_eye", "skinnedAsset": base["assetPath"],
             "patched": False, "textures": {}, "scalars": {}}
    patch = {"assetPath": "/ElysiumBaked/Materials/MI_patch", "skinnedAsset": "/ElysiumBaked/Materials/MI_patch",
             "parent": base["assetPath"], "patched": True, "textures": {}, "scalars": {"Vampire": 0}}
    facts = MaterialFacts({"assets": [base, route, patch]})
    assert facts("vtmb:material:eye")["assetPath"] == base["assetPath"]
    value = facts("vtmb:material:patch")
    assert value["textures"]["Iris"] == base["textures"]["Iris"]
    assert value["scalars"]["Vampire"] == 0
