"""Map-driven skeletal closure without blanket static-prop expansion or native runs."""
from copy import deepcopy

import pytest

from elysium_pipeline.formats.unit_contract.container import encode_glb
from elysium_pipeline.importers import material_consumers
from elysium_pipeline.model_usage import discover_placed_animation_models, placed_animation_models, skeletal_candidate


def model(root, name, labels=("idle",), bones=1):
    id = "vtmb:model:scenery/" + name
    material = "vtmb:material:scenery/" + name
    unit = {"identity": {"asset": id, "shape": "static", "family": "scenery", "roles": ["placed-prop"]},
            "mdl": {"bones": [{}] * bones, "sequences": [{"label": label} for label in labels], "includeModels": []},
            "materialBindings": {"slots": [{"material": material}], "skinFamilies": [[material]]}}
    path = root / "models/scenery" / (name + ".glb")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(encode_glb({"extensions": {"ELYSIUM_vtmb_model": unit}}))
    return id, unit


def entity(id, *, classname="prop_dynamic", keys=None, resolved=True, outputs=None):
    return {"index": 1, "classname": classname,
            "model": {"kind": "model", "asset": id, "path": "models/" + id[11:] + ".mdl"},
            "keyValues": [{"key": key, "value": value} for key, value in (keys or {}).items()],
            "references": [{"role": "model", "asset": id, "resolved": resolved}], "outputs": outputs or []}


def entities(root, name, rows):
    path = root / "maps" / (name + ".entities.glb")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(encode_glb({"extensions": {"ELYSIUM_vtmb_map_entities": {
        "identity": {"asset": "vtmb:map-entities:" + name}, "entities": rows}}}))


@pytest.mark.parametrize("key", ["LoopSequence", "demo_sequence"])
def test_explicit_animation_selects_rigid_model_and_preserves_static_material_twin(tmp_path, key):
    requested, unit = model(tmp_path, "requested")
    scenery, _ = model(tmp_path, "unanimated")
    entities(tmp_path, "test", [entity(requested, keys={key: "idle"}), entity(scenery)])
    assert not skeletal_candidate(unit["identity"], 1)
    assert discover_placed_animation_models(tmp_path, [requested, scenery]) == {requested}
    usage = material_consumers.collect(tmp_path)
    assert usage["vtmb:material:scenery/requested"] == {"skeletal": [requested], "static": [requested], "map": []}
    assert usage["vtmb:material:scenery/unanimated"]["skeletal"] == []


def test_setanimation_wildcard_is_map_scoped_and_excludes_npcs(tmp_path):
    selected, _ = model(tmp_path, "wired")
    elsewhere, _ = model(tmp_path, "elsewhere")
    npc, _ = model(tmp_path, "npc")
    entities(tmp_path, "a", [entity(selected, keys={"targetname": "switch01"},
        outputs=[{"input": "SetAnimation", "target": "switch*"}]),
        entity(npc, classname="npc_test", keys={"LoopSequence": "idle"})])
    entities(tmp_path, "b", [entity(elsewhere, keys={"targetname": "switch01"})])
    assert discover_placed_animation_models(tmp_path, [selected, elsewhere, npc]) == {selected}


def test_intrinsic_requires_a_source_label_and_absent_source_does_not_open_a_model(tmp_path):
    present, _ = model(tmp_path, "switch", labels=("IDLE_ON",))
    absent_labels, _ = model(tmp_path, "missing_labels", labels=("unrelated",))
    absent_model = "vtmb:model:scenery/not_published"
    entities(tmp_path, "test", [entity(present, classname="prop_switch"),
        entity(absent_labels, classname="prop_switch"),
        entity(absent_model, keys={"LoopSequence": "idle"}, resolved=False)])
    assert discover_placed_animation_models(tmp_path, [present, absent_labels]) == {present}


@pytest.mark.parametrize("value", ["", "0", "none", "NULL"])
def test_empty_animation_requests_do_not_expand_selection(tmp_path, value):
    id, _ = model(tmp_path, "static")
    entities(tmp_path, "test", [entity(id, keys={"LoopSequence": value})])
    assert discover_placed_animation_models(tmp_path, [id]) == set()


def test_missing_resolved_source_refuses_closure(tmp_path):
    entities(tmp_path, "test", [entity("vtmb:model:scenery/missing", keys={"LoopSequence": "idle"})])
    with pytest.raises(ValueError, match="no published model"):
        discover_placed_animation_models(tmp_path, [])


def test_full_vocabulary_keeps_one_frame_label_demand_without_mutating_evidence():
    id = "vtmb:model:scenery/oneframe"
    requests = {id: {"sourceAbsent": False, "fullClips": True, "requiredClips": []}}
    original = deepcopy(requests)
    assert placed_animation_models(requests, lambda _: ["only_sequence"]) == {id}
    assert requests == original
    assert placed_animation_models(requests, lambda _: []) == set()


def test_default_character_stage_joins_23_requested_owners_and_targeted_stage_stays_selective(tmp_path, monkeypatch):
    from elysium_pipeline.asset_paths import baked_unit
    from elysium_pipeline.formats.unit_contract.container import read_document
    from elysium_pipeline.importers import characters

    root = tmp_path / "exports"
    requested = {model(root, f"requested_{i}")[0] for i in range(23)}
    scenery, _ = model(root, "unanimated")
    entities(root, "test", [entity(id, keys={"LoopSequence": "idle"}) for id in sorted(requested)] + [entity(scenery)])
    # Selection/closure test: keep unrelated payload building out of this fixture.
    monkeypatch.setattr(characters.cinematics, "discover_sets", lambda _: {})
    monkeypatch.setattr(characters.wield, "discover", lambda *_: {"modelIds": [], "sourceGaps": [], "failures": []})
    monkeypatch.setattr(characters.families, "build", lambda *_: ({"owners": [], "families": []}, {}))

    def stage_unit(path, destination, **kwargs):
        id = read_document(path)["extensions"]["ELYSIUM_vtmb_model"]["identity"]["asset"]
        return {"assetId": id, "skeletonAsset": baked_unit(id, "SKEL"),
                "meshAsset": baked_unit(id, "SK"), "animationAssets": [baked_unit(id, "A", label="idle")]}

    monkeypatch.setattr(characters, "stage_unit", stage_unit)
    default = characters.stage_characters(root, tmp_path / "default", log=lambda _: None)
    assert set(default["selectedUnits"]) == requested
    assert {row["assetId"] for row in default["assets"]} == requested
    assert not default["stageFailures"]
    usage = material_consumers.collect(root)
    assert {owner for row in usage.values() for owner in row["skeletal"]} == requested
    assert {owner for row in usage.values() for owner in row["static"]} == requested | {scenery}
    targeted = characters.stage_characters(root, tmp_path / "targeted", bodies=[scenery], log=lambda _: None)
    assert targeted["selectedUnits"] == [scenery]
