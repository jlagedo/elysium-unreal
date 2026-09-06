"""Ornament (`prop_dynamic_ornament`) demand from animation events 4100/4102.

Retail formatting is `CBaseCombatCharacter::HandleAnimEvent`, vampire.dll `0x1032e330`:
`"%s.mdl"` for 4100 and `"%s_%s.mdl"` with "male"/"female" for 4102, over the event's own
options string, verbatim. Synthetic units only; no install, editor or shared output roots.
"""
import pytest

from elysium_pipeline import ornament_models as ornaments
from elysium_pipeline.asset_paths import baked_unit
from elysium_pipeline.formats import mdl_skel
from elysium_pipeline.formats.unit_contract.container import encode_glb
from elysium_pipeline.importers.ornament_catalogue import project_ornament_catalogue


# The retail sprintf

def test_4100_appends_the_extension_to_the_options_string_alone():
    assert ornaments.event_model_paths(4100, "models/items/cards/cards_left_hold") == [
        "models/items/cards/cards_left_hold.mdl"]


def test_4102_formats_one_path_per_gender_word_in_the_branch_order():
    assert ornaments.event_model_paths(4102, "models/items/Cigarette/Cigarette") == [
        "models/items/cigarette/cigarette_male.mdl", "models/items/cigarette/cigarette_female.mdl"]


def test_an_option_that_already_carries_mdl_keeps_it():
    # The shipped wineglass option is "models/scenery/misc/wineglass/wineglass.mdl"; retail
    # concatenates rather than replaces, and the Unofficial Patch ships the resulting literal.
    assert ornaments.event_model_paths(4102, "models/scenery/misc/wineglass/wineglass.mdl") == [
        "models/scenery/misc/wineglass/wineglass.mdl_male.mdl",
        "models/scenery/misc/wineglass/wineglass.mdl_female.mdl"]


def test_keys_fold_case_and_separators_and_nothing_else():
    assert ornaments.event_model_paths(4100, r"models\Items\Can\Drink_Can") == [
        "models/items/can/drink_can.mdl"]


def test_an_empty_option_asks_for_nothing_and_4101_is_not_a_spawn():
    assert ornaments.event_model_paths(4100, "  ") == []
    with pytest.raises(ornaments.OrnamentDemandError):
        ornaments.event_model_paths(ornaments.DETACH_EVENT, "models/items/cigarette/cigarette")


# Collection from a model's own sequence descriptors

def _seq(label, events):
    return mdl_skel.Seq(label=label, base=0, frames=1, fps=30., activity="", actweight=0,
                        flags=0, events=events)


def test_only_the_two_spawning_events_are_collected_in_descriptor_order():
    clips = (_seq("idle", (mdl_skel.Event(.1, 2050, 0, "left foot"),)),
             _seq("cigarette_Into", (mdl_skel.Event(.5, 4102, 0, "models/items/Cigarette/Cigarette"),
                                     mdl_skel.Event(.9, 4101, 0, ""))),
             _seq("Graffiti_Into", (mdl_skel.Event(.2, 4100, 0, "models/scenery/misc/SprayCan/SprayCan"),)))
    rows = ornaments.collect_requests(clips, "vtmb:model:character/shared/male/misc")
    assert [r["sequence"] for r in rows] == ["cigarette_Into", "Graffiti_Into"]
    assert rows[0]["paths"] == ["models/items/cigarette/cigarette_male.mdl",
                                "models/items/cigarette/cigarette_female.mdl"]
    assert rows[1]["event"] == 4100 and rows[1]["cycle"] == pytest.approx(.2)


# Whole-corpus discovery over published units

def _unit(root, key, sequences):
    path = root / "models" / (key + ".glb")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(encode_glb({"asset": {"version": "2.0"}, "extensions": {"ELYSIUM_vtmb_model": {
        "identity": {"asset": "vtmb:model:" + key}, "mdl": {"sequences": sequences}}}}))
    return path


def _event(cycle, event, options):
    return {"cycle": cycle, "event": event, "type": 0, "options": options}


@pytest.fixture
def corpus(tmp_path):
    _unit(tmp_path, "character/shared/male/misc", [
        {"label": "cigarette_Idle", "events": [_event(.5, 4102, "models/items/Cigarette/Cigarette")]},
        {"label": "Graffiti_Into", "events": [_event(.2, 4100, "models/scenery/misc/SprayCan/SprayCan")]},
        {"label": "walkie", "events": [_event(.3, 4100, "models/items/walkie_talkie")]},
        {"label": "plain", "events": []}])
    for key in ("items/cigarette/cigarette_male", "items/cigarette/cigarette_female",
                "scenery/misc/spraycan/spraycan"):
        _unit(tmp_path, key, [])
    return tmp_path


def test_discovery_expands_every_record_and_resolves_published_units(corpus):
    result = ornaments.discover(corpus)
    assert [r["path"] for r in result["models"]] == [
        "models/items/cigarette/cigarette_female.mdl",
        "models/items/cigarette/cigarette_male.mdl",
        "models/items/walkie_talkie.mdl",
        "models/scenery/misc/spraycan/spraycan.mdl"]
    assert result["modelIds"] == ["vtmb:model:items/cigarette/cigarette_female",
                                  "vtmb:model:items/cigarette/cigarette_male",
                                  "vtmb:model:scenery/misc/spraycan/spraycan"]
    assert result["requestCount"] == 4
    row = next(r for r in result["models"] if r["path"].endswith("cigarette_male.mdl"))
    assert row["events"] == [4102] and row["genders"] == ["male"]
    assert row["requests"][0]["owner"] == "vtmb:model:character/shared/male/misc"
    assert row["requests"][0]["sequence"] == "cigarette_Idle"


def test_a_named_model_with_no_published_unit_is_a_source_gap_not_a_failure(corpus):
    result = ornaments.discover(corpus)
    gap = next(r for r in result["models"] if r["sourceAbsent"])
    assert gap["path"] == "models/items/walkie_talkie.mdl"
    assert gap["assetId"] == "vtmb:model:items/walkie_talkie"
    assert [r["path"] for r in result["sourceGaps"]] == ["models/items/walkie_talkie.mdl"]
    assert result["sourceGaps"][0]["owners"] == ["vtmb:model:character/shared/male/misc"]
    # An explicit published-id set answers the same way as the on-disk lookup.
    requests = [r for owner, seqs in ornaments.walk_units(corpus)
                for r in ornaments.collect_requests(seqs, owner)]
    assert ornaments.resolve(requests, published_ids=set(result["modelIds"])) == result


# The cooked projection

def _entries(ids):
    return {id: {"assetId": id, "meshAsset": baked_unit(id, "SK"),
                 "skeletonAsset": baked_unit(id, "SKEL")} for id in ids}


def test_projection_keys_by_retail_path_and_carries_mesh_skeleton_and_bones(corpus):
    demand = ornaments.discover(corpus)
    bones = ["Bip01", "Bip01 R Hand", "Cylinder40"]
    projection = project_ornament_catalogue(demand, _entries(demand["modelIds"]), lambda id: bones)
    assert projection["catalogueKind"] == "OrnamentModels"
    assert projection["assetPath"] == "/ElysiumBaked/Models/_Corpus/DA_OrnamentModels"
    models = projection["data"]["models"]
    assert set(models) == {r["path"] for r in demand["models"]}
    row = models["models/items/cigarette/cigarette_male.mdl"]
    assert row == {"assetId": "vtmb:model:items/cigarette/cigarette_male",
                   "mesh": "/ElysiumBaked/Models/items/cigarette/SK_cigarette_male.SK_cigarette_male",
                   "skeleton": "/ElysiumBaked/Models/items/cigarette/SKEL_cigarette_male.SKEL_cigarette_male",
                   "sourceAbsent": False, "bones": bones, "events": [4102], "genders": ["male"],
                   "options": ["models/items/Cigarette/Cigarette"], "requestCount": 1,
                   "sourceEvidence": row["sourceEvidence"]}
    assert projection["data"]["requestCount"] == 4


def test_a_source_gap_row_is_published_with_no_native_product(corpus):
    demand = ornaments.discover(corpus)
    projection = project_ornament_catalogue(demand, _entries(demand["modelIds"]), lambda id: ["Bip01"])
    gap = projection["data"]["models"]["models/items/walkie_talkie.mdl"]
    assert gap["sourceAbsent"] and gap["mesh"] == "" and gap["skeleton"] == "" and gap["bones"] == []
    assert gap["assetId"] == "vtmb:model:items/walkie_talkie"
    assert "walkie_talkie" in projection["data"]["sourceGaps"]


def test_a_resolved_ornament_absent_from_the_character_stage_refuses_projection(corpus):
    demand = ornaments.discover(corpus)
    entries = _entries(demand["modelIds"])
    entries.pop("vtmb:model:scenery/misc/spraycan/spraycan")
    with pytest.raises(ValueError, match="not in the character stage"):
        project_ornament_catalogue(demand, entries, lambda id: ["Bip01"])


def test_an_ornament_staged_without_its_own_mesh_refuses_projection(corpus):
    demand = ornaments.discover(corpus)
    entries = _entries(demand["modelIds"])
    entries["vtmb:model:scenery/misc/spraycan/spraycan"]["meshAsset"] = None
    with pytest.raises(ValueError, match="without its own mesh"):
        project_ornament_catalogue(demand, entries, lambda id: ["Bip01"])


# The character stage joins the demand into its default selection

def test_registered_as_a_cooked_catalogue_kind_and_a_packaging_root():
    from elysium_pipeline.asset_paths import corpus_path
    from elysium_pipeline.cook_roots import GLOBALS
    from elysium_pipeline.importers.model_catalogues import KINDS

    assert KINDS["OrnamentModels"] == "ElysiumOrnamentCatalogue"
    assert GLOBALS[corpus_path("model", "DA", "OrnamentModels")] == "ElysiumOrnamentCatalogue"


def test_the_character_stage_selects_an_ornament_no_other_lane_reaches(tmp_path):
    """A 4102 option is the ONLY edge to this model: it is not a skeletal candidate, has no
    role, no placement, no item and no include edge, so an unjoined stage would leave it out."""
    from copy import deepcopy

    from elysium_pipeline.formats.unit_contract.container import decode_glb
    from elysium_pipeline.importers import characters
    from pipeline.tests.test_model_glb import _export

    template = _export(tmp_path / "template")
    document, binary = decode_glb(template.read_bytes(), str(template))
    exports = tmp_path / "exports"
    host, ornament = "vtmb:model:character/host/host", "vtmb:model:items/cigarette/cigarette_male"
    for id, shape, family, sequences in (
            (host, "skeletal", "character", [{
                "index": 0, "label": "cigarette_Into", "grid": {
                    "numblends": 0, "groupsize": [1, 1], "paramindex": [-1, -1],
                    "paramstart": [0., 0.], "paramend": [0., 0.], "cells": []},
                "events": [_event(.5, 4102, "models/items/Cigarette/Cigarette")]}]),
            (ornament, "static", "items", []),
            ("vtmb:model:items/cigarette/cigarette_female", "static", "items", [])):
        doc = deepcopy(document)
        unit = doc["extensions"]["ELYSIUM_vtmb_model"]
        unit["identity"].update(asset=id, modelPath="models/" + id[11:] + ".mdl",
                                family=family, shape=shape, roles=[])
        unit["mdl"]["sequences"] = sequences
        path = exports / ("models/" + id[11:] + ".glb")
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(encode_glb(doc, binary))

    manifest = characters.stage_characters(exports, tmp_path / "stage", log=lambda *a: None)
    assert manifest["stageFailures"] == []
    assert manifest["ornamentDemand"]["modelIds"] == [
        "vtmb:model:items/cigarette/cigarette_female", ornament]
    assert ornament in manifest["selectedUnits"]
    entry = next(r for r in manifest["assets"] if r["assetId"] == ornament)
    assert entry["meshAsset"] == baked_unit(ornament, "SK")
    assert entry["skeletonAsset"] == baked_unit(ornament, "SKEL")
