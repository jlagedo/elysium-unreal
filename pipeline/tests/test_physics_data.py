"""Pure source preservation and worker contracts; never launch/import Unreal."""
from copy import deepcopy
import importlib.util
import json
import os
import re
from pathlib import Path
import struct
from types import SimpleNamespace

import pytest

from elysium_pipeline.formats.unit_contract.container import encode_glb
from elysium_pipeline.importers.physics_data import (
    EXTENSION, GEOMETRY_FRAME, PRODUCER, PhysicsDataError, json_text,
    physics_projection, project_selected_entry, sha256, stage_entry,
)


def sample():
    binary = struct.pack("<9f3H", 0, 0, 0, 1, 2, 3, -4, 5, 6, 0, 2, 1)
    bones = [{"index": 0, "parent": -1, "name": "Root*", "poseToBone": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0], "unknownBoneFlag": 71}]
    hull = {"sourceOffset": 64, "positions": 0, "indices": 1,
            "sourcePoints": [{"ivpW": 99}], "futureLedge": {"x": "retain"}}
    physics = {"coordinateSystem": GEOMETRY_FRAME, "header": {"solidCount": 2, "future": [3, 2, 1]},
        "solids": [{"index": 0, "sourceOffset": 16, "massCenter": [1, 2, 3], "rotationInertia": [4, 5, 6],
                    "properties": {"index": 7, "name": "Root*", "mass": 3.123456789012345,
                                   "massbias": 2., "origin": [8, 9, 10], "surfaceprop": "flesh", "future": "value"},
                    "hulls": [hull, dict(hull, sourceOffset=128)]},
                   {"index": 1, "sourceOffset": 256, "massCenter": [0, 0, 0], "rotationInertia": [1, 1, 1],
                    "properties": {"index": 3, "name": "Missing Calf", "parent": "Root*", "mass": 4}, "hulls": [dict(hull, sourceOffset=300)]}],
        "constraints": [{"parent": 7, "child": 3, "xmin": -25, "xmax": 20, "xfriction": 0, "futureJoint": [1, 2]}],
        "editParams": [{"totalmass": 7, "jointmerge": "a,b", "unknown": [1]}],
        "keyValues": [{"type": "future-block", "pairs": [{"key": "same", "value": "first"}, {"key": "same", "value": "second"}]}],
        "breaks": [{"model": "gib", "health": 11}], "unknown": {"untouched": [False, None, "x"]}}
    document = {"asset": {"version": "2.0"}, "buffers": [{"byteLength": len(binary)}],
        "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 36}, {"buffer": 0, "byteOffset": 36, "byteLength": 6}],
        "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
                      {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"}],
        "nodes": [{"translation": [0, 1, 0]}],
        "extensions": {EXTENSION: {"identity": {"asset": "vtmb:model:test/rig"}, "mdl": {"bones": bones},
                                   "physics": physics, "sourceResolution": {"source": "GLB only"}, "anomalies": [{"unknown": True}]}}}
    body = {"assetId": "vtmb:model:test/rig", "sourceSemantics": {"mdl": {"bones": deepcopy(bones)}, "physics": deepcopy(physics)}}
    return document, binary, body


def project(document, binary, body):
    return physics_projection(document, binary, body, source_glb_sha256="a"*64, staged_body_sha256="b"*64)


def test_preserves_every_physics_field_and_order_with_owned_hull_geometry():
    doc, binary, body = sample()
    before = deepcopy((doc, body))
    result = project(doc, binary, body)
    assert (doc, body) == before
    assert result["assetPath"] == "/ElysiumBaked/Models/test/DA_rig_physics"
    assert json.loads(result["sourceEvidenceJson"])["physics"] == body["sourceSemantics"]["physics"]
    assert [r["binaryIndex"] for r in result["solids"]] == [0, 1]
    assert [r["authoredIndex"]["value"] for r in result["solids"]] == [7, 3]
    assert result["constraints"][0]["parentSolidOrdinal"] == 0
    assert result["constraints"][0]["childSolidOrdinal"] == 1
    assert result["keyValues"][0]["pairs"] == [{"key": "same", "value": "first"}, {"key": "same", "value": "second"}]
    assert result["solids"][0]["nativeBoneName"] == "Root_"
    assert result["solids"][1]["sourceBoneIndex"] == -1
    assert result["gaps"][0]["sourceName"] == "Missing Calf"
    assert result["solids"][0]["hulls"][1]["ledgeOrdinal"] == 1
    assert result["solids"][1]["hulls"][0]["solidOrdinal"] == 1
    assert result["solids"][0]["hulls"][0]["vertices"][2] == {"x": -4., "y": 5., "z": 6.}
    assert result["solids"][0]["hulls"][0]["indices"] == [0, 2, 1]
    assert result["constraints"][0]["axes"][0]["friction"] == {"bPresent": True, "value": 0}
    assert result["constraints"][0]["axes"][1]["friction"] == {"bPresent": False, "value": 0}


def test_null_physics_is_explicit_cookable_absence():
    doc, binary, body = sample()
    doc["extensions"][EXTENSION]["physics"] = body["sourceSemantics"]["physics"] = None
    result = project(doc, binary, body)
    assert result["bHasPhysics"] is False
    assert result["solids"] == result["constraints"] == result["gaps"] == []
    assert json.loads(result["sourceEvidenceJson"])["physics"] is None


def test_missing_or_ambiguous_solid_endpoints_are_retained_as_gaps():
    doc, binary, body = sample()
    doc["extensions"][EXTENSION]["physics"]["solids"][1]["properties"]["index"] = 7
    body["sourceSemantics"]["physics"] = deepcopy(doc["extensions"][EXTENSION]["physics"])
    result = project(doc, binary, body)
    assert len(result["solids"]) == 2 and len(result["constraints"]) == 1
    assert result["constraints"][0]["parentSolidOrdinal"] == -1
    assert result["constraints"][0]["childSolidOrdinal"] == -1
    assert sum(g["kind"] == "solid-reference" for g in result["gaps"]) == 2


@pytest.mark.parametrize("mutation", ["sparse", "normalized", "bounds", "nan", "indices", "body"])
def test_bad_accessors_or_stale_semantics_refuse_without_silent_geometry_loss(mutation):
    doc, binary, body = sample()
    if mutation == "sparse": doc["accessors"][0]["sparse"] = {}
    elif mutation == "normalized": doc["accessors"][0]["normalized"] = True
    elif mutation == "bounds": doc["bufferViews"][0]["byteLength"] = 12
    elif mutation == "nan": binary = struct.pack("<f", float("nan")) + binary[4:]
    elif mutation == "indices": binary = binary[:-2] + struct.pack("<H", 99)
    else: body["sourceSemantics"]["physics"]["unknown"] = {}
    with pytest.raises(PhysicsDataError): project(doc, binary, body)


def test_strided_accessor_retains_exact_point_order():
    doc, binary, body = sample()
    points = struct.unpack("<9f", binary[:36])
    packed = b"".join(struct.pack("<4f", *points[i:i+3], 123) for i in (0, 3, 6)) + binary[36:]
    doc["buffers"][0]["byteLength"] = len(packed)
    doc["bufferViews"][0].update(byteLength=48, byteStride=16)
    doc["bufferViews"][1]["byteOffset"] = 48
    assert project(doc, packed, body)["solids"][0]["hulls"][0]["vertices"][1] == {"x": 1., "y": 2., "z": 3.}


def test_selected_entry_hashes_and_disjoint_stage_output(tmp_path):
    doc, binary, body = sample()
    exports, characters, output = (tmp_path / n for n in ("exports", "characters", "physics"))
    exports.mkdir(); characters.mkdir()
    glb = encode_glb(doc, binary); encoded = json_text(body).encode()
    (exports / "unit.glb").write_bytes(glb); (characters / "body.json").write_bytes(encoded)
    entry = {"assetId": body["assetId"], "meshAsset": "/SK_rig", "unitGlb": "unit.glb", "body": "body.json",
             "recipe": {"unitSha256": sha256(glb), "bodySha256": sha256(encoded)}}
    assert project_selected_entry(entry, [entry["assetId"]], exports, characters)["assetId"] == entry["assetId"]
    row = stage_entry(entry, [entry["assetId"]], exports, characters, output)
    assert sha256((output / row["projectionFile"]).read_bytes()) == row["projectionSha256"]
    with pytest.raises(PhysicsDataError, match="disjoint"):
        stage_entry(entry, [entry["assetId"]], exports, characters, characters / "physics")
    with pytest.raises(PhysicsDataError, match="selected"):
        project_selected_entry(entry, [], exports, characters)
    (characters / "body.json").write_bytes(encoded+b" ")
    with pytest.raises(PhysicsDataError, match="changed"):
        project_selected_entry(entry, [entry["assetId"]], exports, characters)


@pytest.fixture
def worker():
    path = Path(__file__).parents[1] / "unreal" / "import_physics_data.py"
    spec = importlib.util.spec_from_file_location("physics_worker_test", path)
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    return module


def test_worker_checks_native_fields_on_reuse_and_refuses_foreign_owner(worker):
    calls = []
    class Data:
        owner = PRODUCER
        @staticmethod
        def verify(asset, text): calls.append("verify"); return ""
    data = Data()
    unreal = SimpleNamespace(load_asset=lambda _: data, EditorAssetLibrary=SimpleNamespace(get_metadata_tag=lambda *args: data.owner))
    bl = SimpleNamespace(PRODUCER_TAG="owner", stored_recipe=lambda *args, **kwargs: "recipe")
    asset, status = worker._publish(unreal, bl, Data, {"assetPath": "/unit"}, "recipe")
    assert asset is data and status == "reused" and calls == ["verify"]
    data.owner = "another-lane"
    with pytest.raises(RuntimeError, match="foreign"):
        worker._publish(unreal, bl, Data, {"assetPath": "/unit"}, "recipe", True)


def test_python_projection_matches_every_reflected_native_property():
    # Cross-language schema check only, not a substitute for UHT/build/cook acceptance.
    header = (Path(__file__).parents[2] / "Source/ElysiumUE/Public/ElysiumPhysicsData.h").read_text(encoding="utf-8")
    structs = {}
    for name, body in re.findall(r"USTRUCT\(\)\s*struct (\w+)\s*\{(.*?)\n\};", header, re.S):
        structs[name] = re.findall(r'UPROPERTY\([^\n]*\)\s+([\w<>]+)\s+(\w+)', body)

    def verify(name, value):
        fields = structs[name]
        assert {key.casefold() for key in value} == {key.casefold() for _, key in fields}, name
        values = {key.casefold(): v for key, v in value.items()}
        for kind, key in fields:
            item = values[key.casefold()]
            if kind in structs:
                verify(kind, item)
            elif kind.startswith("TArray<") and kind[7:-1] in structs:
                for row in item:
                    verify(kind[7:-1], row)
    verify("FElysiumPhysicsSourceData", project(*sample()))
    assert "WITH_EDITORONLY_DATA" not in header


def test_worker_repair_and_save_failure_are_reported(worker):
    calls = []
    class Data:
        payload = "changed"
        @staticmethod
        def verify(asset, text): calls.append("verify"); return "" if asset.payload == text else "changed"
        @staticmethod
        def apply_json(asset, text): calls.append("apply"); asset.payload = text; return asset, ""
    data = Data()
    unreal = SimpleNamespace(load_asset=lambda _: data,
        EditorAssetLibrary=SimpleNamespace(get_metadata_tag=lambda *args: PRODUCER),
        log_warning=lambda _: calls.append("warning"))
    bl = SimpleNamespace(PRODUCER_TAG="owner", stored_recipe=lambda *args, **kwargs: "recipe",
        stamp_recipe=lambda *args, **kwargs: calls.append("stamp"), save=lambda _: False)
    with pytest.raises(RuntimeError, match="save failed"):
        worker._publish(unreal, bl, Data, {"assetPath": "/unit"}, "recipe")
    assert calls == ["verify", "warning", "apply", "verify", "stamp"]


@pytest.mark.skipif(not os.environ.get("ELYSIUM_PHYSICS_DATA_CASES"), reason="explicit bounded read-only GLB/staged-body cases")
def test_bounded_real_physics_projection():
    exports, stage = Path(r"E:\elysium-work\exports_v2"), Path(r"E:\elysium-work\import\characters")
    manifest = json.loads((stage / "manifest.json").read_text(encoding="utf-8"))
    keys = ["character/npc/unique/downtown/lacroix/lacroix",
            "character/npc/common/blueblood/male/newscaster_male",
            "character/npc/common/security_guard/security_guard",
            "character/pc/male/malkavian/armor3/malkavian_male_armor_3"]
    entries = {e["key"]: e for e in manifest["assets"]}
    report = []
    for key in keys:
        projection = project_selected_entry(entries[key], manifest["selectedUnits"], exports, stage)
        evidence = json.loads(projection["sourceEvidenceJson"])
        assert len(projection["solids"]) == len(evidence["physics"]["solids"])
        assert len(projection["constraints"]) == len(evidence["physics"]["constraints"])
        report.append({"assetId": projection["assetId"], "assetPath": projection["assetPath"],
                       "sourceGlbSha256": projection["sourceGlbSha256"], "stagedBodySha256": projection["stagedBodySha256"],
                       "solids": len(projection["solids"]), "constraints": len(projection["constraints"]),
                       "hulls": sum(len(s["hulls"]) for s in projection["solids"]),
                       "vertices": sum(len(h["vertices"]) for s in projection["solids"] for h in s["hulls"]),
                       "indices": sum(len(h["indices"]) for s in projection["solids"] for h in s["hulls"]),
                       "gaps": projection["gaps"], "cookedEvidenceBytes": len(projection["sourceEvidenceJson"].encode())})
    assert len(report[1]["gaps"]) >= 4
    output = Path(r"E:\elysium-work\_r8_explore\agents\physics\physics_data_cases.json")
    output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))
