import pytest

from elysium_pipeline.importers.tuning_keys import model_key_plan


def cast():
    return {"models": {"vtmb:model:a/body": {}, "vtmb:model:b/body": {}},
            "aliases": {"hero": "vtmb:model:a/body", "models/a/body.mdl": "vtmb:model:a/body"},
            "ambiguousAliases": {"body": ["vtmb:model:a/body", "vtmb:model:b/body"]}}


def test_tuning_keys_resolve_to_ids_and_remain_stable_after_migration():
    assert model_key_plan(["HERO"], cast()) == {"HERO": "vtmb:model:a/body"}
    assert model_key_plan(["models\\a\\body.mdl"], cast()) == {"models\\a\\body.mdl": "vtmb:model:a/body"}
    assert model_key_plan(["vtmb:model:a/body"], cast()) == {"vtmb:model:a/body": "vtmb:model:a/body"}


def test_ambiguous_missing_and_colliding_tuning_keys_fail_before_any_value_can_be_lost():
    for keys in (["body"], ["missing"], ["hero", "vtmb:model:a/body"]):
        with pytest.raises(ValueError):
            model_key_plan(keys, cast())
