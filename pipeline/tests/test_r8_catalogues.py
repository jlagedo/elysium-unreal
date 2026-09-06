"""R8 catalogue semantic contracts; no install, Unreal, or generated-stage writes."""
from copy import deepcopy
import hashlib
import json
from types import SimpleNamespace as NS

import numpy as np
import pytest

from elysium_pipeline.asset_paths import baked_unit
from elysium_pipeline.formats.mdl_skel import Seq
from elysium_pipeline.importers.catalogue_common import staged_json, object_path
from elysium_pipeline.importers.wield_catalogue import project_wield_catalogue
from elysium_pipeline.importers.prop_skin_catalogue import project_skin_model, project_skin_catalogue, family_materials
from elysium_pipeline.importers.placed_catalogue import (
    POLICY, absent_model, collect_placed_references, measure_static_equivalence, project_placed_model, project_placed_catalogue, select_rest,
)
from elysium_pipeline.placed_models import select_rest_sequence

ID = "vtmb:model:scenery/test"


def wield_fixture():
    def item(key, kind, id):
        ref = {"assetId": id, "source": "models/test.mdl" if id else "", "kind": kind,
               "meshAsset": baked_unit(id, "SK") if kind == "real" else None}
        return {"assetId": "vtmb:vdata:items/" + key, "classname": key, "showsViewModel": True,
                "animPrefix": "knife", "models": {"f": ref, "m": deepcopy(ref)}}
    items = [item("item_real", "real", ID), item("item_empty", "empty", None),
             item("item_null", "null", "vtmb:model:weapons/w_null"),
             item("item_absent", "absent", "vtmb:model:error")]
    entry = {"assetId": ID, "meshAsset": baked_unit(ID, "SK"), "animationSkeletonAsset": baked_unit(ID, "SKEL"),
             "animationAssets": [baked_unit(ID, "A", label="idle")], "wieldBinding": "socket_prop",
             "wieldSkeletonSource": "test.wield.skel", "recipe": {"wieldRigSha256": "abc"}}
    decision = {"binding": "socket_prop", "referencePoseSource": "clip", "referenceClip": "idle", "grip": "knife",
                "referencePose": [{"name": "hand", "parent": -1, "position": [1, 2, 3], "rotation": [0, 0, 0, 1]},
                                  {"name": "prop", "parent": 0, "position": [0, 1, 0], "rotation": [0, 0, 0, 1]}],
                "mountBone": "prop", "handBone": "hand", "collapseBone": "prop",
                "trailTip": {"bone": "prop", "position": [0, 0, 7], "rotation": [0, 0, 0, 1]},
                "onBody": {"bodies": 1}, "checks": {"motion": {"ok": False, "detail": [["idle", "prop", 7.]]}}}
    gaps = [{"assetId": items[-1]["assetId"], "sex": sex, "model": "vtmb:model:error", "reason": "source absent"} for sex in ("f", "m")]
    manifest = {"assets": [entry], "stageFailures": [], "wieldCatalogue": {
        "items": items, "modelIds": [ID], "sourceGaps": gaps, "failures": []}}
    trees = {"vtmb:model:character/body": {"hand": "", "prop": "hand"},
             "vtmb:model:character/other": {"hand": ""}}
    entry["recipe"]["wieldBodyTrees"] = hashlib.sha256(json.dumps(trees, ensure_ascii=False,
        allow_nan=False, separators=(",", ":")).encode()).hexdigest()
    return manifest, {"assetId": ID, "wield": decision}, trees


def test_wield_full_join_retains_absences_decisions_and_reference_ownership():
    m, body, trees = wield_fixture()
    result = project_wield_catalogue(m, lambda _: body, trees)["data"]
    assert len(result["items"]) == 4 and len(result["models"]) == 1
    assert [r["female"]["state"] for r in result["items"].values()] == ["Real", "Empty", "Null", "Absent"]
    model = result["models"][ID]
    assert json.loads(model["decisionEvidence"]) == body["wield"]
    assert model["binding"] == "SocketProp"  # even a check finding is not permission to guess a binding
    assert model["referenceOwner"] == ID and model["referenceClip"].endswith("/A_idle.A_idle")
    assert model["trailTipBone"] == "prop"
    assert model["bodies"]["vtmb:model:character/other"]["unmatchedBones"] == ["prop"]
    assert json.loads(result["sourceGaps"]) == m["wieldCatalogue"]["sourceGaps"]


@pytest.mark.parametrize("mutation", ["gap", "duplicate", "missing-model", "missing-clip", "trail", "wrong-pose", "old-path", "body-scope"])
def test_wield_refuses_incomplete_or_stale_joins(mutation):
    m, body, trees = wield_fixture()
    if mutation == "gap": m["wieldCatalogue"]["sourceGaps"].pop()
    if mutation == "duplicate": m["wieldCatalogue"]["items"].append(m["wieldCatalogue"]["items"][0])
    if mutation == "missing-model": m["wieldCatalogue"]["modelIds"] = []
    if mutation == "missing-clip": m["assets"][0]["animationAssets"] = []
    if mutation == "trail": body["wield"]["trailTip"] = None
    if mutation == "wrong-pose": body["wield"]["referencePose"][0]["parent"] = 0
    if mutation == "old-path": m["assets"][0]["meshAsset"] = "/ElysiumBaked/Items/SK_test"
    if mutation == "body-scope": trees = {"vtmb:model:character/other": {"hand": ""}}
    with pytest.raises((ValueError, KeyError)):
        project_wield_catalogue(m, lambda _: body, trees)


def skin_fixture():
    a, b, c = ["vtmb:material:" + n for n in ("a", "b", "c")]
    families = [[b, a, c], [c, a, c], [b, a, c]]
    unit = {"identity": {"asset": ID}, "mdl": {"skinTable": [[1, 0, 2], [2, 0, 2], [1, 0, 2]]},
            "materialBindings": {"slots": [{"material": x} for x in (a, b, c)], "skinFamilies": families}}
    reps = [{"kind": kind, "mesh": baked_unit(ID, prefix),
             "slots": [{"index": 0, "slotName": "drawn", "skinReferences": [0]}]}
            for kind, prefix in [("skeletal", "SK"), ("static", "SM")]]
    route = lambda id, kind: baked_unit(id, "MI", role="Skinned" if kind == "skeletal" else None)
    return unit, reps, route


def test_skin_keeps_undrawn_columns_nonidentity_indices_and_trailing_base_families():
    u, reps, route = skin_fixture()
    model = project_skin_model(u, reps, route)
    assert model["familyCount"] == 3 and model["skinReferenceCount"] == 3
    assert [c["sourceSlot"] for c in model["representations"][0]["families"][0]["cells"]] == [1, 0, 2]
    assert family_materials(model, "skeletal", 1)[0][2].endswith("MI_c_Skinned.MI_c_Skinned")
    assert family_materials(model, "static", 1)[0][2].endswith("MI_c.MI_c")
    for family in (0, 2, 99, -1):
        assert family_materials(model, "skeletal", family)[0][2].endswith("MI_b_Skinned.MI_b_Skinned")
    assert json.loads(model["sourceEvidence"])["materialBindings"] == u["materialBindings"]
    assert len(project_skin_catalogue([model], expected_ids=[ID])["data"]["models"]) == 1
    with pytest.raises(ValueError, match="coverage"):
        project_skin_catalogue([model], expected_ids=[ID, "vtmb:model:another"])


@pytest.mark.parametrize("mutation", ["ragged", "indices", "fold", "route", "old-path"])
def test_skin_refuses_loss_or_missing_material_routes(mutation):
    u, reps, route = skin_fixture()
    if mutation == "ragged": u["materialBindings"]["skinFamilies"][1].pop()
    if mutation == "indices": u["mdl"]["skinTable"][0][0] = 0
    if mutation == "fold": reps[0]["slots"][0]["skinReferences"] = [0, 1]
    if mutation == "route": route = lambda *_: None
    if mutation == "old-path": reps[1]["mesh"] = "/ElysiumBaked/Meshes/SM_test"
    with pytest.raises(ValueError): project_skin_model(u, reps, route)


def test_geometryless_empty_source_cell_survives_without_invented_material():
    u = {"identity": {"asset": ID}, "mdl": {"skinTable": [[0]]},
         "materialBindings": {"slots": [], "skinFamilies": [[""]]}}
    model = project_skin_model(u, [], lambda *_: pytest.fail("must not invent material for geometryless source"))
    assert model["familyCount"] == model["skinReferenceCount"] == 1
    assert model["representations"] == [] and model["sourceOnlyReason"]
    assert json.loads(model["sourceEvidence"])["skinTable"] == [[0]]
    assert json.loads(model["sourceEvidence"])["materialBindings"]["skinFamilies"] == [[""]]


def placed_unit(*, cloth=False, displacement=0., second_model=False):
    seq = [Seq("idle_a", 0, 2, 30., "ACT_IDLE", 0, 0), Seq("idle_b", 1, 2, 30., "ACT_IDLE", 3, 1)]
    bone = NS(index=0, parent=-1, name="root", flags=0, pos=(0, 0, 0), quat=(0, 0, 0, 1),
              pose_to_bone=(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0))
    data = {0: np.array([0, 1, 2]), 1: np.zeros((3, 1), dtype=int), 2: np.ones((3, 1))}
    ext = {"skinReference": 0, "model": int(second_model), "sourceVertices": [0, 1, 2],
           "sourcePositions": [[0, 0, 0], [1, 0, 0], [0, 1, 0]], "sourceNormals": [[0, 0, 1]] * 3}
    primitive = {"extensions": {"ELYSIUM_vtmb_model": ext}, "indices": 0, "attributes": {"JOINTS_0": 1, "WEIGHTS_0": 2}}
    unit = NS(id=ID, sequences=seq, bones=[bone], mdl={"sequences": [{"label": r.label} for r in seq]},
              dropped_sequences=[{"label": "discarded", "reason": "duplicate"}],
              extension={"identity": {"asset": ID, "modelPath": "models/scenery/test.mdl", "roles": ["placed-prop"]},
                         "cloth": {"garments": [1] if cloth else []}, "vtx": {"lods": [{"index": 0, "mesh": 0}]}},
              document={"meshes": [{"primitives": [primitive]}]}, precise=lambda a, s: np.array(a, dtype=float),
              accessor=lambda i: data[i],
              animation_frames=lambda base, frames: [[((displacement if base else 0., 0., 0.), (0., 0., 0., 1.))]] * frames)
    return unit


def test_static_equivalence_measures_all_candidates_and_bind():
    unit = placed_unit()
    result = measure_static_equivalence(unit)
    assert result["proven"] and result["equivalent"]
    assert result["poses"] == ["bind", "idle_a", "idle_b"]
    result = measure_static_equivalence(placed_unit(displacement=.1))
    assert result["proven"] and not result["equivalent"]
    assert result["maxPositionCm"] == pytest.approx(.254)


def test_undefined_source_normals_keep_static_equivalence_unproved():
    unit = placed_unit()
    ext = unit.document['meshes'][0]['primitives'][0]['extensions']['ELYSIUM_vtmb_model']
    ext['sourcePositions'] = [[0.,0.,0.]] * 3
    ext['sourceNormals'] = [[0.,0.,0.]] * 3
    result = measure_static_equivalence(unit)
    assert not result['proven'] and not result['equivalent']
    assert result['undefinedNormalVertices'] == [0,1,2]
    assert result['sourcePrimitive'] == 0
    projected = project_placed_model(unit, static_mesh=baked_unit(ID, 'SM'), proof=result)
    assert not projected['staticRestSuffices']


@pytest.mark.parametrize("cloth,second_model", [(False, False), (True, False), (False, True)])
def test_static_rest_requires_geometry_equivalence_and_no_cloth_or_topology_loss(cloth, second_model):
    unit = placed_unit(cloth=cloth, second_model=second_model)
    row = project_placed_model(unit, static_mesh=baked_unit(ID, "SM"), proof=measure_static_equivalence(unit))
    assert row["staticRestSuffices"] == (not cloth and not second_model)
    if cloth or second_model:
        with pytest.raises(ValueError): project_placed_catalogue([row], expected_ids=[ID])
    else:
        for token in [-1, 0, 1, 40, 2**31-1]:
            assert select_rest(row, token)["label"] == select_rest_sequence(row["modelPath"], unit.sequences, token).label


def test_placed_pending_and_absent_are_distinct_and_do_not_authorize_static():
    row = project_placed_model(placed_unit(), static_mesh=baked_unit(ID, "SM"))
    assert not row["staticRestSuffices"] and row["acceptanceIssues"]
    with pytest.raises(ValueError): project_placed_catalogue([row], expected_ids=[ID])
    absent = absent_model("vtmb:model:error", "models/error.mdl", "authored reference absent from source")
    assert project_placed_catalogue([absent], expected_ids=["vtmb:model:error"])["data"]["models"]["vtmb:model:error"]["sourceAbsent"]
    assert select_rest(absent, 0) is None


def test_nonrest_clip_is_not_erased_by_static_rest_equivalence():
    unit = placed_unit()
    unit.sequences.append(Seq("open", 2, 3, 30., "", 1, 0))
    row = project_placed_model(unit, static_mesh=baked_unit(ID, "SM"), proof=measure_static_equivalence(unit))
    assert row["clips"][-1]["state"] == "unavailable"
    with pytest.raises(ValueError): project_placed_catalogue([row], expected_ids=[ID])


def test_static_rest_cannot_erase_an_animated_placement_requirement():
    unit = placed_unit()
    usage = {"assetId": ID, "fullClips": True, "requiredClips": ["open"], "uses": [{"sourceRecordIndex": 7}]}
    row = project_placed_model(unit, static_mesh=baked_unit(ID, "SM"), proof=measure_static_equivalence(unit), usage=usage)
    # An intrinsic label the source vocabulary never had is an explicit source gap, recorded in
    # the evidence rather than claimed as a requirement the runtime could satisfy.
    assert row["fullClipsRequired"] and row["requiredClips"] == []
    assert json.loads(row["sourceEvidence"])["absentIntrinsicClips"] == [
        {"label": "open", "state": "source-absent",
         "reason": "intrinsic label is absent from source sequence vocabulary"}]
    assert json.loads(row["placementEvidence"]) == usage
    with pytest.raises(ValueError): project_placed_catalogue([row], expected_ids=[ID])


def test_placed_native_grids_keep_base_cell_and_all_derived_products():
    unit = placed_unit()
    a = object_path(baked_unit(ID, "A", label="idle_a"))
    extra = object_path(baked_unit(ID, "A", label="cell@host"))
    blend = object_path(baked_unit(ID, "BS", label="idle_b"))
    body = {"assetId": ID, "nativeSequences": {"idle_a": a, "cell@host": extra},
            "nativeBlendSpaces": {"idle_b": blend}, "sequences": [
                {"owner": ID, "label": "idle_a", "assets": {"sequence": a}},
                {"owner": ID, "label": "idle_b", "assets": {"blendSpace": blend, "baseCell": extra}}]}
    staged = {"assetId": ID, "meshAsset": baked_unit(ID, "SK")}
    row = project_placed_model(unit, staged=staged, native_body=body)
    assert not row["acceptanceIssues"]
    assert row["nativeSequences"]["cell@host"] == extra
    assert row["clips"][1]["blendSpace"] == blend and row["clips"][1]["baseCell"] == extra
    assert row["bodyData"] == object_path(baked_unit(ID, "DA"))
    body["sequences"][1]["assets"]["baseCell"] = object_path(baked_unit(ID, "A", label="absent"))
    with pytest.raises(ValueError, match="undeclared"):
        project_placed_model(unit, staged=staged, native_body=body)


def test_stage_reader_checks_digest_and_cannot_escape(tmp_path):
    raw = json.dumps({"assetId": ID}).encode()
    (tmp_path / "body.json").write_bytes(raw)
    entry = {"assetId": ID, "body": "body.json", "recipe": {"bodySha256": hashlib.sha256(raw).hexdigest()}}
    assert staged_json(tmp_path, entry)["assetId"] == ID
    (tmp_path / "body.json").write_text("{}")
    with pytest.raises(ValueError, match="stale"): staged_json(tmp_path, entry)
    entry["body"] = "../outside.json"
    with pytest.raises(ValueError, match="escapes"): staged_json(tmp_path, entry)


def test_placed_collection_keeps_absent_refs_intrinsic_clips_and_source_record_indices():
    def entity(index, name, id=ID, resolved=True):
        return {"index": index, "classname": name, "model": {"kind": "model", "asset": id, "path": "models/" + id.split(':',2)[2] + ".mdl"},
                "keyValues": [{"key": "targetname", "value": "switch01"}], "outputs": [],
                "references": [{"role": "model", "asset": id, "resolved": resolved}]}
    a = entity(7, "prop_switch")
    a["outputs"] = [{"input": "SetAnimation", "target": "switch*"}]
    units = [{"identity": {"asset": "vtmb:map-entities:test"}, "entities": [a, entity(8, "npc_test"),
              entity(9, "prop_dynamic", "vtmb:model:error", False)]}]
    result = collect_placed_references(units, [], published_ids=[ID])
    assert len(result) == 2
    assert result[ID]["fullClips"]
    assert result[ID]["requiredClips"] == ["activate", "deactivate", "idle_off", "idle_on"]
    assert result[ID]["uses"][0]["sourceRecordIndex"] == 7
    assert result["vtmb:model:error"]["sourceAbsent"]
    with pytest.raises(ValueError, match="no published"):
        collect_placed_references(units, [], published_ids=[])
