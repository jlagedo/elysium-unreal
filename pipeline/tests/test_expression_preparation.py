"""Cooked model expression join and source-gender projection; no generated corpus writes."""
from copy import deepcopy
import json
from pathlib import Path

import pytest

from elysium_pipeline.formats.model_glb.expressions import selected_tables
from elysium_pipeline.importers.character_data import mesh_projection
from elysium_pipeline.importers.expression_tables import expression_model_projection
from test_character_data import document, material


@pytest.mark.parametrize("flags, male", [(0, True), (256, False), (0xffffffff, False), (0xfffffeff, True)])
def test_model_gender_uses_only_source_flag_and_preserves_all_flags(flags, male):
    doc = document()
    source = doc["extensions"]["ELYSIUM_vtmb_model"]
    source["identity"]["asset"] = "vtmb:model:fixture/male_female_ambiguous_name"
    source["mdl"]["header"]["flags"]["value"] = flags
    source["facial"]["selectedTables"] = selected_tables("missing", {
        "expressions/phonemes.vfe": object(), "expressions/phonemes_male.vfe": object()})
    untouched = deepcopy(source)
    result = mesh_projection(doc, material)
    assert source == untouched
    assert result["expressionData"] == expression_model_projection(source)
    assert result["expressionData"]["modelFlags"] == flags
    assert result["expressionData"]["modelIsMale"] is male
    for original, projected in zip(result["expressionTables"], result["expressionData"]["selections"]):
        assert json.loads(projected["sourceSelectionJson"]) == original
        assert projected["fallbackAssetIds"] == ["vtmb:expression-table:phonemes", "vtmb:expression-table:phonemes_male"]


@pytest.mark.parametrize("flags", [True, -1, 2**32, 1.25, "256"])
def test_invalid_gender_source_is_not_defaulted(flags):
    source = document()["extensions"]["ELYSIUM_vtmb_model"]
    source["mdl"]["header"]["flags"]["value"] = flags
    with pytest.raises(ValueError, match="uint32"):
        expression_model_projection(source)


def test_absent_facial_domain_retains_explicit_empty_selection_and_model_flags():
    source = document()["extensions"]["ELYSIUM_vtmb_model"]
    source["facial"] = None
    assert expression_model_projection(source) == {
        "schemaVersion": "1.0.0", "modelFlags": 0, "modelIsMale": True, "selections": []}
    del source["mdl"]["header"]["flags"]
    with pytest.raises(KeyError):
        expression_model_projection(source)


def test_native_preparation_and_consumers_have_no_expression_disk_fallback():
    repository = Path(__file__).parents[2]
    preparation = (repository / "Source/ElysiumUE/Private/Visual/ElysiumExpressionPreparation.cpp").read_text(encoding="utf-8")
    for forbidden in ("LoadObject<", "LoadSynchronous(", "LoadFileToString(", "ParseText(", "ElysiumExpressions::Load("):
        assert forbidden not in preparation
    for name in ("ElysiumChoreoScene.cpp", "ElysiumEntityWorldDialogue.cpp"):
        consumer = (repository / "Source/ElysiumUE/Private/Substrate" / name).read_text(encoding="utf-8")
        assert "ElysiumExpressions::Load(" not in consumer
        assert "ElysiumExpressions::LoadPreparedPhonemes(" in consumer
