from copy import deepcopy

import pytest

from elysium_pipeline.importers.dynamics_data import dynamics_projection


def source(first=1, terminal=-1):
    return {"sourceOffset": 500, "firstBone": first, "terminalBone": terminal,
            "unusedAuthoredPreset": 4., "gravity": .75, "damping": .2,
            "springExponent": 2., "maxAngleDegrees": 120.}


def semantics(records=None):
    return {"mdl": {"bones": [{"index": 0, "parent": -1, "name": "Bip01 Head"},
                               {"index": 1, "parent": 0, "name": "hair.a"},
                               {"index": 2, "parent": 1, "name": "hair.b"}]},
            "secondaryMotion": [source()] if records is None else records}


def test_all_source_records_survive_in_order_without_widening_install_policy():
    data = semantics([source(), source(), source(2)])
    before = deepcopy(data)
    result = dynamics_projection("vtmb:model:example/hair", data)
    assert data == before
    assert result["assetPath"] == "/ElysiumBaked/Models/example/DYN_hair"
    assert result["sourceRecordCount"] == 3
    assert len(result["chains"]) == 2
    for native, original in zip(result["records"], data["secondaryMotion"]):
        assert all(native[field] == value for field, value in original.items())
    assert result["records"][0]["boneIndices"] == [1, 2]
    assert result["records"][0]["recipeIndex"] == 0
    assert result["records"][1]["recipeIndex"] == 1
    assert result["records"][2]["projection"] == "source-only"
    assert result["chains"][0]["damping"] == .7
    assert result["records"][0]["damping"] == .2
    assert result["chains"][0]["angularSpring"] == .04


def test_breast_body_is_cooked_beside_original_record():
    data = semantics([source(1, 1)])
    data["mdl"]["bones"][0]["name"] = "Bip01 Spine1"
    data["mdl"]["bones"][1]["name"] = "Left Breast"
    result = dynamics_projection("vtmb:model:example/body", data)
    assert len(result["bodies"]) == 1
    assert not result["chains"]
    assert result["bodies"][0]["coneAngleDegrees"] == 90
    assert result["records"][0]["maxAngleDegrees"] == 120
    assert result["records"][0]["projection"] == "breast-body"


def test_unknown_source_fields_and_bad_walks_refuse_projection():
    data = semantics()
    data["secondaryMotion"][0]["newSemantic"] = 7
    with pytest.raises(ValueError, match="unhandled field"):
        dynamics_projection("vtmb:model:test", data)
    with pytest.raises(ValueError, match="outside"):
        dynamics_projection("vtmb:model:test", semantics([source(99)]))


def test_empty_declaration_is_explicit():
    result = dynamics_projection("vtmb:model:test", semantics([]))
    assert result["sourceRecordCount"] == 0
    assert result["records"] == result["chains"] == result["bodies"] == []
