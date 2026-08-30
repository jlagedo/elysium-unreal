"""Contract tests for the Map-entities GLB seam.

Every fixture here is synthetic: a BSP built byte by byte in this module, handed to the seam
through a fake index and an injected reader, so nothing in this file depends on an installed copy
of the game. What the tests pin is the seam's own promises -- the identity rule, the ledger's
gaplessness over the lump span, each dependency role, each anomaly and omission the specification
names, and the validator's refusal of a unit that has been tampered with.
"""

from __future__ import annotations

import hashlib
import struct

import pytest

import elysium_pipeline.formats.map_glb.partition as map_partition
from elysium_pipeline.exporters import map_entities_glb as exporter
from elysium_pipeline.formats import map_entities_glb as seam
from elysium_pipeline.formats.map_entities_glb import decode as decoder
from elysium_pipeline.formats.map_entities_glb import lexer, model as entity_model
from elysium_pipeline.formats.unit_contract import (
    ROOT_KEYS,
    encode_glb,
    ranges_sha256,
    read_glb,
)
from elysium_pipeline.validation import map_entities_glb as validation

MAP_NAME = "testmap"
MAP_MEMBER = f"maps/{MAP_NAME}.bsp"


# --------------------------------------------------------------------------------------------
# One synthetic BSP: a header, the entity lump, and a MODELS lump that sizes the `*N` space.
# --------------------------------------------------------------------------------------------


def _bsp(entities: bytes, *, models: int = 3, revision: int = 7) -> bytes:
    body = bytearray()
    rows: dict[int, tuple[int, int]] = {}
    base = map_partition.HEADER_BYTES

    def add(index: int, payload: bytes) -> None:
        rows[index] = (base + len(body), len(payload))
        body.extend(payload)

    add(0, entities)
    add(14, bytes(models * entity_model.MODEL_STRIDE))
    header = bytearray(struct.pack("<4si", b"VBSP", 17))
    for index in range(map_partition.LUMP_COUNT):
        offset, length = rows.get(index, (0, 0))
        header.extend(struct.pack("<iii4s", offset, length, 0, b"\0\0\0\0"))
    header.extend(struct.pack("<i", revision))
    data = bytes(header) + bytes(body)
    return data + struct.pack("<I", rows[0][0]) + map_partition.TRAILER_SIGNATURE


def _block(*pairs: tuple[str, str]) -> str:
    lines = "".join(f'"{key}" "{value}"\n' for key, value in pairs)
    return "{\n" + lines + "}\n"


def _lump(*blocks: str, terminator: bytes = b"\0") -> bytes:
    return "".join(blocks).encode("latin-1") + terminator


WORLDSPAWN = _block(("classname", "worldspawn"), ("skyname", "santamonica"))


def _install(entities: bytes, *members: str, models: int = 3):
    """A fake install: one BSP, plus any member paths the references should resolve against."""

    data = _bsp(entities, models=models)
    index: dict[str, tuple] = {MAP_MEMBER: ("loose", f"/fake/Unofficial_Patch/{MAP_MEMBER}")}
    payloads = {MAP_MEMBER: data}
    for member in members:
        index[member] = ("loose", f"/fake/Vampire/{member}")
        payloads[member] = b"member"

    def read_bytes(_index: dict, key: str) -> bytes | None:
        return payloads.get(key)

    return index, read_bytes


def _export(tmp_path, entities: bytes, *members: str, models: int = 3):
    index, read_bytes = _install(entities, *members, models=models)
    path = exporter.export(index, MAP_NAME, tmp_path, read_bytes=read_bytes)
    document, binary = read_glb(path)
    return path, document, document["extensions"][seam.MAP_ENTITIES_EXTENSION], binary


def _decode(entities: bytes, *members: str, models: int = 3):
    index, read_bytes = _install(entities, *members, models=models)
    closure = seam.load_source_closure(index, MAP_NAME, read_bytes=read_bytes)
    return seam.decode_map_entities(closure, member_exists=lambda path: path.lower() in index)


def _dependency(root, role: str):
    return [row for row in root["dependencies"] if row["role"] == role]


# --------------------------------------------------------------------------------------------
# Identity and the family the unit is published into
# --------------------------------------------------------------------------------------------


def test_the_key_folds_case_and_tolerates_the_root_prefix_and_extension():
    for spelling in ("SP_Tutorial_1", "maps/sp_tutorial_1.bsp", "sp_tutorial_1.bsp"):
        assert seam.normalize_key(spelling) == "sp_tutorial_1"
    assert seam.asset_id("SP_Tutorial_1") == "vtmb:map-entities:sp_tutorial_1"
    assert seam.map_asset_id("sp_tutorial_1") == "vtmb:map:sp_tutorial_1"


def test_the_unit_is_written_beside_the_root_in_the_maps_family():
    assert seam.output_relative_path("sp_tutorial_1").as_posix() == "maps/sp_tutorial_1.entities.glb"


def test_the_span_member_names_the_lump_it_was_cut_from():
    assert seam.member_path("sp_tutorial_1") == "maps/sp_tutorial_1.bsp#lump0"


def test_a_missing_map_is_refused_before_any_decode():
    with pytest.raises(seam.MapEntitiesSourceError):
        seam.load_source_closure({}, MAP_NAME, read_bytes=lambda index, key: None)


def test_source_keys_lists_every_map_the_install_resolves():
    index, _ = _install(_lump(WORLDSPAWN))
    index["maps/graphs/testmap.ain"] = ("loose", "/fake/x")
    assert exporter.source_keys(index) == [MAP_NAME]


# --------------------------------------------------------------------------------------------
# Container, identity block and the contract's key order
# --------------------------------------------------------------------------------------------


def test_the_unit_is_scene_less_with_no_bin_chunk_and_the_contract_key_order(tmp_path):
    path, document, root, binary = _export(tmp_path, _lump(WORLDSPAWN))
    assert binary == b""
    assert document["asset"]["generator"] == "Elysium Map-entities GLB Exporter"
    assert document["extensionsUsed"] == [seam.MAP_ENTITIES_EXTENSION]
    assert document["extensionsRequired"] == [seam.MAP_ENTITIES_EXTENSION]
    for forbidden in ("scenes", "nodes", "meshes", "images", "textures", "samplers", "buffers"):
        assert forbidden not in document
    assert tuple(list(root)[: len(ROOT_KEYS)]) == ROOT_KEYS
    assert root["identity"]["asset"] == f"vtmb:map-entities:{MAP_NAME}"
    assert root["identity"]["sourcePath"] == MAP_MEMBER
    assert root["identity"]["map"] == MAP_NAME
    assert root["schemaVersion"] == seam.SCHEMA_VERSION


def test_the_member_carries_the_lump_span_and_the_map_block_shares_the_root_facts(tmp_path):
    _, _, root, _ = _export(tmp_path, _lump(WORLDSPAWN))
    member = root["sourceResolution"]["members"][0]
    assert member["path"] == f"{MAP_MEMBER}#lump0"
    assert member["span"]["length"] == member["byteLength"]
    assert root["map"]["offset"] == member["span"]["offset"]
    assert root["map"]["mapRevision"] == 7
    assert root["map"]["file"]["path"] == MAP_MEMBER
    assert root["map"]["brushModels"]["count"] == 3


def test_two_exports_of_one_source_are_byte_identical(tmp_path):
    entities = _lump(WORLDSPAWN, _block(("classname", "light"), ("origin", "1 2 3")))
    first, _, _, _ = _export(tmp_path / "a", entities)
    second, _, _, _ = _export(tmp_path / "b", entities)
    assert first.read_bytes() == second.read_bytes()


# --------------------------------------------------------------------------------------------
# The byte ledger over the lump span
# --------------------------------------------------------------------------------------------


def test_every_byte_of_the_lump_span_is_claimed_exactly_once(tmp_path):
    entities = _lump(
        WORLDSPAWN,
        _block(("classname", "light"), ("origin", "1 2 3")),
        _block(("classname", "prop_dynamic"), ("model", "models/props/box.mdl")),
    )
    _, _, root, _ = _export(tmp_path, entities, "models/props/box.mdl")
    ledger = root["coverage"]["byteLedger"][0]
    assert ledger["byteLength"] == len(entities)
    assert ledger["accountedBytes"] == len(entities)
    assert ledger["coveragePercent"] == 100.0
    cursor = 0
    for row in ledger["ranges"]:
        assert row["offset"] == cursor
        cursor += row["length"]
    assert cursor == len(entities)
    assert sum(ledger["stateBytes"].values()) == len(entities)


def test_the_terminator_is_the_only_reserved_zero_and_the_text_is_mapped(tmp_path):
    _, _, root, _ = _export(tmp_path, _lump(WORLDSPAWN))
    ledger = root["coverage"]["byteLedger"][0]
    assert ledger["stateBytes"]["reserved-zero"] == 1
    assert set(ledger["stateBytes"]) == {"mapped-text", "reserved-zero"}
    terminator = [row for row in ledger["ranges"] if row["state"] == "reserved-zero"]
    assert terminator == [
        {
            "offset": ledger["byteLength"] - 1,
            "length": 1,
            "state": "reserved-zero",
            "owner": "trailing-null",
        }
    ]


def test_braces_pairs_and_whitespace_each_own_their_own_bytes(tmp_path):
    _, _, root, _ = _export(tmp_path, _lump(WORLDSPAWN))
    owners = [row["owner"] for row in root["coverage"]["byteLedger"][0]["ranges"]]
    assert "entities[0].braces.open" in owners
    assert "entities[0].braces.close" in owners
    assert "entities[0].keyValues[0]" in owners
    assert "whitespace" in owners


def test_bytes_after_the_terminator_are_omitted_proven_with_a_digest(tmp_path):
    entities = _lump(WORLDSPAWN, terminator=b"\0residue")
    _, _, root, _ = _export(tmp_path, entities)
    omission = root["omissions"][0]
    assert omission["role"] == "bytes-after-terminator"
    assert omission["byteLength"] == len(b"residue")
    assert omission["sha256"]
    assert root["coverage"]["omittedProven"] == root["omissions"]
    states = root["coverage"]["byteLedger"][0]["stateBytes"]
    assert states["omitted-proven"] == len(b"residue")
    assert [row["role"] for row in root["anomalies"] if row["role"] == "terminator-count"]


def test_a_lump_with_no_terminator_is_reported_rather_than_repaired(tmp_path):
    _, _, root, _ = _export(tmp_path, _lump(WORLDSPAWN, terminator=b""))
    roles = [row["role"] for row in root["anomalies"]]
    assert "terminator-count" in roles
    assert "reserved-zero" not in root["coverage"]["byteLedger"][0]["stateBytes"]


def test_an_empty_lump_publishes_with_an_omission_and_a_warning(tmp_path):
    path, _, root, _ = _export(tmp_path, b"")
    assert root["entities"] == []
    assert root["worldspawn"] is None
    assert root["omissions"] == [
        {
            "role": "empty-member",
            "reason": "the map's ENTITIES lump is empty",
            "sourceOffset": 0,
            "byteLength": 0,
        }
    ]
    ledger = root["coverage"]["byteLedger"][0]
    assert ledger["byteLength"] == 0 and ledger["coveragePercent"] == 100.0
    summary = validation.validate(path)
    assert "omitted: the map's ENTITIES lump is empty" in validation.warnings_for(summary)


def test_a_carriage_return_and_a_high_byte_are_named_anomalies(tmp_path):
    entities = _lump("{\r\n" + '"classname" "worldspawn"\r\n' + '"targetname" "caf\xe9"\r\n}\r\n')
    _, _, root, _ = _export(tmp_path, entities)
    roles = {row["role"] for row in root["anomalies"]}
    assert {"line-end-cr", "non-ascii-byte"} <= roles


# --------------------------------------------------------------------------------------------
# The entity table
# --------------------------------------------------------------------------------------------


def test_every_pair_is_published_in_authored_order_with_its_span(tmp_path):
    entities = _lump(WORLDSPAWN, _block(("classname", "light"), ("_light", "255 255 255 200")))
    _, _, root, _ = _export(tmp_path, entities)
    light = root["entities"][1]
    assert [pair["key"] for pair in light["keyValues"]] == ["classname", "_light"]
    text = entities.decode("latin-1")
    for pair in light["keyValues"]:
        span = text[pair["sourceOffset"]:pair["sourceOffset"] + pair["byteLength"]]
        assert span.startswith(f'"{pair["sourceKey"]}"')
        assert span.endswith(f'"{pair["value"]}"')
        assert pair["quotedKey"] and pair["quotedValue"]


def test_a_key_is_folded_and_its_authored_spelling_is_kept(tmp_path):
    entities = _lump(WORLDSPAWN, _block(("classname", "prop_dynamic"), ("SceneFile", "x")))
    _, _, root, _ = _export(tmp_path, entities)
    pair = root["entities"][1]["keyValues"][1]
    assert pair["key"] == "scenefile"
    assert pair["sourceKey"] == "SceneFile"


def test_worldspawn_is_restated_at_root_level_and_names_its_block(tmp_path):
    _, _, root, _ = _export(tmp_path, _lump(WORLDSPAWN, _block(("classname", "light"))))
    assert root["worldspawn"]["index"] == 0
    assert root["worldspawn"] == root["entities"][0]
    assert root["worldspawn"]["classname"] == "worldspawn"


def test_the_class_census_counts_every_block(tmp_path):
    entities = _lump(
        WORLDSPAWN, _block(("classname", "light")), _block(("classname", "light"))
    )
    _, _, root, _ = _export(tmp_path, entities)
    assert root["classCensus"] == [
        {"classname": "light", "count": 2},
        {"classname": "worldspawn", "count": 1},
    ]


def test_a_repeated_scalar_key_is_an_anomaly_and_every_occurrence_is_kept(tmp_path):
    entities = _lump(
        WORLDSPAWN,
        _block(("classname", "light"), ("style", "1"), ("style", "2")),
    )
    _, _, root, _ = _export(tmp_path, entities)
    duplicate = [row for row in root["anomalies"] if row["role"] == "duplicate-scalar-key"]
    assert duplicate[0]["key"] == "style"
    assert duplicate[0]["occurrences"] == [1, 2]
    assert [pair["value"] for pair in root["entities"][1]["keyValues"][1:]] == ["1", "2"]


def test_a_repeated_output_key_is_kept_in_lump_order_without_a_duplicate_anomaly(tmp_path):
    entities = _lump(
        WORLDSPAWN,
        _block(
            ("classname", "logic_relay"),
            ("OnTrigger", "a,Trigger,,0,-1,,"),
            ("OnTrigger", "b,Trigger,,0,-1,,"),
        ),
    )
    _, _, root, _ = _export(tmp_path, entities)
    assert [row["target"] for row in root["entities"][1]["outputs"]] == ["a", "b"]
    assert not [row for row in root["anomalies"] if row["role"] == "duplicate-scalar-key"]


# --------------------------------------------------------------------------------------------
# Numbers: C atof semantics
# --------------------------------------------------------------------------------------------


def test_atof_reads_the_longest_numeric_prefix_and_nothing_else():
    assert entity_model.atof("-3496,92") == (-3496.0, "-3496")
    assert entity_model.atof("  12.5e2rest") == (1250.0, "  12.5e2")
    assert entity_model.atof("none") == (0.0, "")
    assert entity_model.atoi("0") == 0
    assert entity_model.atoi("-1abc") == -1


def test_a_comma_decimal_origin_reads_as_the_engine_reads_it_and_names_the_dropped_suffix(tmp_path):
    entities = _lump(
        WORLDSPAWN, _block(("classname", "light"), ("origin", "-3496,92 -3147,1 140"))
    )
    _, _, root, _ = _export(tmp_path, entities)
    origin = root["entities"][1]["origin"]
    assert origin["raw"] == "-3496,92 -3147,1 140"
    assert origin["source"] == [-3496.0, -3147.0, 140.0]
    assert origin["gltf"] == pytest.approx([-3496 * 0.0254, 140 * 0.0254, 3147 * 0.0254])
    dropped = [row for row in root["anomalies"] if row["role"] == "atof-truncated-number"]
    assert [row["dropped"] for row in dropped] == [",92", ",1"]


def test_angles_are_restated_as_the_gltf_quaternion_of_the_same_placement(tmp_path):
    entities = _lump(WORLDSPAWN, _block(("classname", "light"), ("angles", "0 90 0")))
    _, _, root, _ = _export(tmp_path, entities)
    angles = root["entities"][1]["angles"]
    assert angles["source"] == [0.0, 90.0, 0.0]
    assert angles["gltf"] == pytest.approx([0.0, 0.7071067811865476, 0.0, 0.7071067811865476])


def test_a_vector_that_is_not_three_components_is_carried_with_an_anomaly(tmp_path):
    entities = _lump(WORLDSPAWN, _block(("classname", "light"), ("origin", "697 878 -5 3")))
    _, _, root, _ = _export(tmp_path, entities)
    assert root["entities"][1]["origin"]["source"] == [697.0, 878.0, -5.0]
    assert [row["role"] for row in root["anomalies"] if row["role"] == "vector-field-count"]


# --------------------------------------------------------------------------------------------
# Outputs
# --------------------------------------------------------------------------------------------


def test_a_seven_field_output_publishes_the_six_fields_retail_consumes_and_the_residue(tmp_path):
    entities = _lump(
        WORLDSPAWN,
        _block(
            ("classname", "logic_relay"),
            ("OnTrigger", "door,Open,param,1.5,3,openDoor(),residue"),
        ),
    )
    _, _, root, _ = _export(tmp_path, entities)
    output = root["entities"][1]["outputs"][0]
    assert output["target"] == "door"
    assert output["input"] == "Open"
    assert output["parameter"] == "param"
    assert output["delay"] == {"raw": "1.5", "value": 1.5}
    assert output["times"] == {"raw": "3", "value": 3, "unlimited": False}
    assert output["python"] == "openDoor()"
    assert output["extra"] == "residue"
    assert output["fieldCount"] == 7
    assert output["keyValue"] == 1
    assert not [row for row in root["anomalies"] if row["role"] == "output-field-count"]


def test_an_authored_zero_times_means_unlimited_exactly_as_minus_one_does(tmp_path):
    entities = _lump(
        WORLDSPAWN,
        _block(
            ("classname", "logic_relay"),
            ("OnTrigger", "a,Trigger,,0,0,,"),
            ("OnTrigger", "b,Trigger,,0,-1,,"),
        ),
    )
    _, _, root, _ = _export(tmp_path, entities)
    first, second = root["entities"][1]["outputs"]
    assert first["times"] == {"raw": "0", "value": -1, "unlimited": True}
    assert second["times"] == {"raw": "-1", "value": -1, "unlimited": True}


def test_a_negative_delay_is_carried_as_authored(tmp_path):
    entities = _lump(
        WORLDSPAWN,
        _block(("classname", "logic_relay"), ("OnTrigger", "a,PlaySound,,-1.0,-1,,")),
    )
    _, _, root, _ = _export(tmp_path, entities)
    assert root["entities"][1]["outputs"][0]["delay"] == {"raw": "-1.0", "value": -1.0}


def test_a_field_count_other_than_seven_is_an_anomaly_and_not_a_failure(tmp_path):
    entities = _lump(
        WORLDSPAWN,
        _block(("classname", "logic_relay"), ("OnTrigger", "a,Trigger,,0,-1")),
    )
    _, _, root, _ = _export(tmp_path, entities)
    row = [item for item in root["anomalies"] if item["role"] == "output-field-count"][0]
    assert row["fieldCount"] == 5 and row["expected"] == 7
    output = root["entities"][1]["outputs"][0]
    assert output["python"] == "" and "extra" not in output


def test_an_output_looking_key_the_datamap_does_not_declare_stays_a_plain_keyvalue(tmp_path):
    entities = _lump(
        WORLDSPAWN,
        _block(
            ("classname", "trigger_player_activity_level"),
            ("OnTrigger", "a,Trigger,,0,-1,,"),
        ),
        _block(("classname", "logic_relay"), ("OnTrigger-wesp", "b,Trigger,,0,-1,,")),
    )
    _, _, root, _ = _export(tmp_path, entities)
    for index in (1, 2):
        assert root["entities"][index]["outputs"] == []
        assert root["entities"][index]["keyValues"][1]["outputLike"] is True


def test_every_python_call_string_is_carried_verbatim_with_its_back_links(tmp_path):
    entities = _lump(
        WORLDSPAWN,
        _block(("classname", "logic_relay"), ("OnTrigger", ",,,0,-1,setArea(),")),
        _block(("classname", "logic_pythoncheck"), ("python_script", "G.Flag == 1")),
    )
    _, _, root, _ = _export(tmp_path, entities)
    assert root["scriptExpressions"] == [
        {
            "index": 0,
            "entity": 1,
            "output": 0,
            "keyValue": 1,
            "role": "output-python",
            "expression": "setArea()",
        },
        {
            "index": 1,
            "entity": 2,
            "output": None,
            "keyValue": 1,
            "role": "python-script",
            "expression": "G.Flag == 1",
        },
    ]


# --------------------------------------------------------------------------------------------
# References: one row on the entity, one row in dependencies
# --------------------------------------------------------------------------------------------


def test_a_studio_model_names_the_model_unit(tmp_path):
    entities = _lump(
        WORLDSPAWN, _block(("classname", "prop_dynamic"), ("model", "models/props/box.mdl"))
    )
    _, _, root, _ = _export(tmp_path, entities, "models/props/box.mdl")
    assert root["entities"][1]["model"] == {
        "kind": "model",
        "asset": "vtmb:model:props/box",
        "path": "models/props/box.mdl",
    }
    assert _dependency(root, "model") == [
        {
            "role": "model",
            "asset": "vtmb:model:props/box",
            "sourcePath": "models/props/box.mdl",
            "resolved": True,
        }
    ]


def test_a_brush_model_names_the_root_unit_and_is_proven_against_the_models_lump(tmp_path):
    entities = _lump(WORLDSPAWN, _block(("classname", "func_brush"), ("model", "*2")))
    _, _, root, _ = _export(tmp_path, entities)
    assert root["entities"][1]["model"] == {
        "kind": "brush",
        "index": 2,
        "withinModelCount": True,
    }
    row = _dependency(root, "brush-model")[0]
    assert row["asset"] == f"vtmb:map:{MAP_NAME}"
    # The pin states the bytes the row's own `sourcePath` names -- the whole BSP.
    bsp = _bsp(entities)
    assert row["sourcePath"] == MAP_MEMBER
    assert row["byteLength"] == len(bsp)
    assert row["sha256"] == hashlib.sha256(bsp).hexdigest()
    # Lump 14's own length and digest are `map.brushModels`, against an explicit lump number.
    assert root["map"]["brushModels"]["lump"] == entity_model.MODELS_LUMP
    assert root["map"]["brushModels"]["length"] == 3 * entity_model.MODEL_STRIDE
    assert root["map"]["brushModels"]["count"] == 3
    assert root["entities"][1]["references"][0]["index"] == 2


def test_a_brush_model_outside_the_models_lump_is_an_anomaly(tmp_path):
    entities = _lump(WORLDSPAWN, _block(("classname", "func_brush"), ("model", "*9")))
    _, _, root, _ = _export(tmp_path, entities, models=3)
    assert root["entities"][1]["model"]["withinModelCount"] is False
    assert root["entities"][1]["references"][0]["resolved"] is False
    assert [row for row in root["anomalies"] if row["role"] == "brush-model-index"]


def test_a_sprite_model_and_a_decal_texture_name_material_units(tmp_path):
    entities = _lump(
        WORLDSPAWN,
        _block(("classname", "env_sprite"), ("model", "sprites/glowa.vmt")),
        _block(("classname", "infodecal"), ("texture", "decals/stains/rustb")),
    )
    _, _, root, _ = _export(tmp_path, entities, "materials/sprites/glowa.vmt")
    assert root["entities"][1]["model"] == {"kind": "sprite", "path": "sprites/glowa.vmt"}
    assets = {row["asset"] for row in _dependency(root, "material")}
    assert "vtmb:material:sprites/glowa" in assets
    assert "vtmb:material:decals/stains/rustb" in assets
    unresolved = [row for row in _dependency(root, "material") if not row["resolved"]]
    assert [row["sourcePath"] for row in unresolved] == [
        "materials/decals/stains/rustb.vmt",
        "materials/skybox/santamonicabk.vmt",
        "materials/skybox/santamonicadn.vmt",
        "materials/skybox/santamonicaft.vmt",
        "materials/skybox/santamonicalf.vmt",
        "materials/skybox/santamonicart.vmt",
        "materials/skybox/santamonicaup.vmt",
    ]


def test_the_skyname_names_the_six_skybox_faces(tmp_path):
    faces = [f"materials/skybox/santamonica{face}.vmt" for face in seam.SKYBOX_FACES]
    _, _, root, _ = _export(tmp_path, _lump(WORLDSPAWN), *faces)
    assert [row["sourcePath"] for row in _dependency(root, "material")] == sorted(faces)
    assert len(root["entities"][0]["references"]) == 6
    assert {row["keyValue"] for row in root["entities"][0]["references"]} == {1}


def test_a_sound_scheme_a_sound_and_a_scene_each_name_their_own_unit(tmp_path):
    entities = _lump(
        WORLDSPAWN,
        _block(
            ("classname", "ambient_soundscheme"),
            ("scheme_file", "sound/schemes/cops_outside.txt"),
        ),
        _block(("classname", "ambient_generic"), ("message", "environmental/fire/fire.wav")),
        _block(("classname", "logic_choreo"), ("SceneFile", "sound/cinematic/x.vcd")),
    )
    members = (
        "sound/schemes/cops_outside.txt",
        "sound/environmental/fire/fire.wav",
        "sound/cinematic/x.vcd",
    )
    _, _, root, _ = _export(tmp_path, entities, *members)
    assert _dependency(root, "sound-scheme")[0]["asset"] == "vtmb:sound-scheme:cops_outside"
    assert _dependency(root, "sound")[0]["asset"] == (
        "vtmb:sound:environmental/fire/fire.wav"
    )
    assert _dependency(root, "scene")[0]["asset"] == "vtmb:scene:cinematic/x"
    assert all(row["resolved"] for row in root["dependencies"] if row["role"] != "material")


def test_a_sign_and_a_hack_terminal_name_vdata_units(tmp_path):
    entities = _lump(
        WORLDSPAWN,
        _block(("classname", "prop_sign"), ("definition_file", "vdata/signs/newspaper.txt")),
        _block(("classname", "prop_hacking"), ("hackterminal", "haven_pc")),
    )
    _, _, root, _ = _export(tmp_path, entities, "vdata/signs/newspaper.txt")
    rows = {row["asset"]: row for row in _dependency(root, "vdata")}
    assert rows["vtmb:vdata:signs/newspaper"]["resolved"] is True
    assert rows["vtmb:vdata:hackterminals/haven_pc"]["sourcePath"] == (
        "vdata/hackterminals/haven_pc.txt"
    )


def test_a_particle_definition_names_a_particle_unit_by_name_or_by_path(tmp_path):
    entities = _lump(
        WORLDSPAWN,
        _block(("classname", "env_particle"), ("particle_definition", "fire2_emitter")),
        _block(
            ("classname", "env_particle"),
            ("particle_definition", "particles/explosion2_emitter.txt"),
        ),
    )
    _, _, root, _ = _export(tmp_path, entities, "particles/fire2_emitter.txt")
    rows = {row["asset"]: row for row in _dependency(root, "particle")}
    assert rows["vtmb:particle:fire2_emitter"]["resolved"] is True
    assert rows["vtmb:particle:explosion2_emitter"]["resolved"] is False


def test_a_dialogue_file_names_a_dialogue_unit(tmp_path):
    entities = _lump(
        WORLDSPAWN,
        _block(("classname", "npc_VHumanCombatant"), ("dialogname", "dlg/generic/cabbie.dlg")),
    )
    _, _, root, _ = _export(tmp_path, entities, "dlg/generic/cabbie.dlg")
    assert _dependency(root, "dialogue") == [
        {
            "role": "dialogue",
            "asset": "vtmb:dialogue:generic/cabbie",
            "sourcePath": "dlg/generic/cabbie.dlg",
            "resolved": True,
        }
    ]


def test_a_file_named_under_a_key_outside_the_vocabulary_is_an_untyped_reference(tmp_path):
    entities = _lump(
        WORLDSPAWN, _block(("classname", "prop_door"), ("locksnd", "world/door_lock.wav"))
    )
    _, _, root, _ = _export(tmp_path, entities)
    row = [item for item in root["anomalies"] if item["role"] == "untyped-file-reference"][0]
    assert row["key"] == "locksnd" and row["extension"] == ".wav"
    assert root["entities"][1]["references"] == []


def test_two_entities_naming_one_unit_share_one_dependency_row(tmp_path):
    entities = _lump(
        WORLDSPAWN,
        _block(("classname", "prop_dynamic"), ("model", "models/props/box.mdl")),
        _block(("classname", "prop_dynamic"), ("model", "models/props/box.mdl")),
    )
    _, _, root, _ = _export(tmp_path, entities, "models/props/box.mdl")
    assert len(_dependency(root, "model")) == 1
    assert len(root["entities"][1]["references"]) == 1
    assert len(root["entities"][2]["references"]) == 1


# --------------------------------------------------------------------------------------------
# Departures from the grammar
# --------------------------------------------------------------------------------------------


def test_an_escaped_quote_does_not_close_the_value(tmp_path):
    entities = _lump(
        WORLDSPAWN,
        '{\n"classname" "logic_auto"\n"OnMapLoad" ",,,0,-1,setArea(\\"downtown\\"),"\n}\n',
    )
    _, _, root, _ = _export(tmp_path, entities)
    assert root["entities"][1]["outputs"][0]["python"] == 'setArea("downtown")'


def test_a_comment_line_is_carried_and_claimed(tmp_path):
    entities = _lump("{\n// Can't get this to work\n" + '"classname" "light"\n}\n')
    _, _, root, _ = _export(tmp_path, entities)
    assert root["comments"][0]["text"] == "// Can't get this to work"
    assert [row["role"] for row in root["anomalies"] if row["role"] == "comment-line"]
    owners = [row["owner"] for row in root["coverage"]["byteLedger"][0]["ranges"]]
    assert "comments[0]" in owners


def test_a_stray_token_shifts_the_pairs_the_way_the_engine_shifts_them(tmp_path):
    entities = _lump(
        WORLDSPAWN,
        '{\n"classname" "prop_dynamic"\n"OnUseBegin" ",,,0,-1,x(),"mop\n"OnUseEnd" "a,b,,0,-1,,"\n}\n',
    )
    _, _, root, _ = _export(tmp_path, entities)
    pairs = root["entities"][1]["keyValues"]
    assert [pair["sourceKey"] for pair in pairs] == [
        "classname", "OnUseBegin", "mop", "a,b,,0,-1,,",
    ]
    assert pairs[2]["value"] == "OnUseEnd"
    assert pairs[2]["quotedKey"] is False
    assert pairs[3]["value"] is None
    roles = {row["role"] for row in root["anomalies"]}
    assert {"unquoted-token", "closing-brace-without-data"} <= roles


def test_a_block_the_tokenizer_cannot_close_is_unresolved_and_fails_the_unit(tmp_path):
    entities = _lump(WORLDSPAWN, '{\n"classname" "light"\n')
    index, read_bytes = _install(entities)
    with pytest.raises(validation.MapEntitiesGlbValidationError):
        exporter.export(index, MAP_NAME, tmp_path, read_bytes=read_bytes)
    model = _decode(entities)
    assert model.unresolved[0]["role"] == "unterminated-block"


def test_a_block_without_a_classname_is_named(tmp_path):
    _, _, root, _ = _export(tmp_path, _lump(WORLDSPAWN, _block(("targetname", "orphan"))))
    assert [row for row in root["anomalies"] if row["role"] == "missing-classname"]
    assert root["entities"][1]["classname"] is None


def test_the_tokenizer_tiles_the_text_it_reads():
    text = '{\n"a" "b" // trailing\n(x)\n}\n'
    tokens = lexer.tokenize(text)
    assert "".join(token.text if token.kind != "string" else "" for token in tokens)
    cursor = 0
    for token in tokens:
        assert token.offset == cursor
        cursor += token.length
    assert cursor == len(text)


# --------------------------------------------------------------------------------------------
# Validation
# --------------------------------------------------------------------------------------------


def test_a_published_unit_round_trips_through_the_standalone_validator(tmp_path):
    entities = _lump(
        WORLDSPAWN,
        _block(
            ("classname", "logic_relay"),
            ("targetname", "relay"),
            ("origin", "1 2 3"),
            ("OnTrigger", "door,Open,,0,-1,call(),"),
        ),
        _block(("classname", "func_brush"), ("model", "*1")),
    )
    path, _, root, _ = _export(tmp_path, entities)
    summary = validation.validate(path)
    assert summary["asset"] == f"vtmb:map-entities:{MAP_NAME}"
    assert summary["entities"] == 3
    assert summary["outputs"] == 1
    assert summary["byteCoveragePercent"] == 100.0
    assert summary["accountedBytes"] == summary["sourceBytes"] == len(entities)
    assert root["coverage"]["unresolved"] == []
    assert root["coverage"]["unsupported"] == []
    assert root["coverage"]["typedUnidentified"] == []
    assert validation.warnings_for(summary) == [
        "the install carries no member for materials/skybox/santamonicabk.vmt, "
        "materials/skybox/santamonicadn.vmt, materials/skybox/santamonicaft.vmt, "
        "materials/skybox/santamonicalf.vmt and 2 more"
    ]


def test_a_tampered_ledger_range_is_refused(tmp_path):
    path, document, root, _ = _export(tmp_path, _lump(WORLDSPAWN))
    ledger = root["coverage"]["byteLedger"][0]
    ledger["ranges"][0]["length"] += 1
    path.write_bytes(encode_glb(document, b""))
    with pytest.raises(validation.MapEntitiesGlbValidationError):
        validation.validate(path)


def test_a_ledger_that_claims_the_terminator_is_not_zero_is_refused(tmp_path):
    entities = _lump(WORLDSPAWN)
    index, read_bytes = _install(entities)
    closure = seam.load_source_closure(index, MAP_NAME, read_bytes=read_bytes)
    model = seam.decode_map_entities(closure, member_exists=lambda path: False)
    model.claims[-1] = entity_model.Claim(0, 1, "reserved-zero", "trailing-null")
    with pytest.raises(Exception):
        seam.build_coverage(model)


def test_a_unit_whose_output_back_link_is_broken_is_refused(tmp_path):
    entities = _lump(
        WORLDSPAWN,
        _block(("classname", "logic_relay"), ("OnTrigger", "a,Trigger,,0,-1,,")),
    )
    path, document, root, _ = _export(tmp_path, entities)
    root["entities"][1]["outputs"][0]["keyValue"] = 7
    path.write_bytes(encode_glb(document, b""))
    with pytest.raises(validation.MapEntitiesGlbValidationError):
        validation.validate(path)


def test_a_reference_with_no_dependency_row_is_refused(tmp_path):
    entities = _lump(
        WORLDSPAWN, _block(("classname", "prop_dynamic"), ("model", "models/props/box.mdl"))
    )
    path, document, root, _ = _export(tmp_path, entities)
    root["dependencies"] = [
        row for row in root["dependencies"] if row["role"] != "model"
    ]
    path.write_bytes(encode_glb(document, b""))
    with pytest.raises(validation.MapEntitiesGlbValidationError):
        validation.validate(path)


def test_a_unit_that_declares_a_scene_is_refused(tmp_path):
    path, document, _, _ = _export(tmp_path, _lump(WORLDSPAWN))
    document["scenes"] = [{"nodes": []}]
    document["nodes"] = [{}]
    path.write_bytes(encode_glb(document, b""))
    with pytest.raises(validation.MapEntitiesGlbValidationError):
        validation.validate(path)


def test_export_time_validation_compares_the_document_against_a_fresh_walk(tmp_path, monkeypatch):
    entities = _lump(WORLDSPAWN, _block(("classname", "light"), ("origin", "1 2 3")))
    index, read_bytes = _install(entities)
    closure = seam.load_source_closure(index, MAP_NAME, read_bytes=read_bytes)
    model = seam.decode_map_entities(closure, member_exists=lambda path: False)
    document, binary = exporter.build_document(model)
    root = document["extensions"][seam.MAP_ENTITIES_EXTENSION]
    root["entities"][1]["keyValues"][1]["value"] = "9 9 9"
    with pytest.raises(validation.MapEntitiesGlbValidationError):
        validation.validate_document(document, binary, source_members=closure.members())


def test_the_decoder_refuses_a_lump_whose_bytes_belong_to_no_record():
    entities = _lump(WORLDSPAWN)
    index, read_bytes = _install(entities)
    closure = seam.load_source_closure(index, MAP_NAME, read_bytes=read_bytes)
    original = decoder.lexer.tokenize

    def truncated(text: str):
        return [token for token in original(text) if token.offset > 3]

    decoder.lexer.tokenize = truncated
    try:
        with pytest.raises(decoder.MapEntitiesDecodeError):
            seam.decode_map_entities(closure)
    finally:
        decoder.lexer.tokenize = original


def test_a_dependency_row_keeps_the_spelling_the_map_authored(tmp_path):
    entities = _lump(
        WORLDSPAWN,
        _block(("classname", "ambient_soundscheme"), ("scheme_file", "sound/Schemes/CH_Cloud.txt")),
        _block(("classname", "prop_dynamic"), ("model", "Models/Props/Box.MDL")),
    )
    members = ("sound/schemes/ch_cloud.txt", "models/props/box.mdl")
    _, _, root, _ = _export(tmp_path, entities, *members)
    scheme = _dependency(root, "sound-scheme")[0]
    assert scheme["sourcePath"] == "sound/Schemes/CH_Cloud.txt"
    assert scheme["asset"] == "vtmb:sound-scheme:ch_cloud"
    assert scheme["resolved"] is True
    model = _dependency(root, "model")[0]
    assert model["sourcePath"] == "Models/Props/Box.MDL"
    assert model["asset"] == "vtmb:model:props/box"
    assert model["resolved"] is True


def test_two_spellings_of_one_unit_share_a_single_dependency_row(tmp_path):
    entities = _lump(
        WORLDSPAWN,
        _block(("classname", "prop_dynamic"), ("model", "models/Props/box.mdl")),
        _block(("classname", "prop_dynamic"), ("model", "models/props/BOX.mdl")),
    )
    _, _, root, _ = _export(tmp_path, entities, "models/props/box.mdl")
    assert [row["sourcePath"] for row in _dependency(root, "model")] == ["models/Props/box.mdl"]


def test_an_output_the_class_declares_under_another_name_is_an_output(tmp_path):
    entities = _lump(
        WORLDSPAWN,
        _block(
            ("classname", "game_ui"),
            ("PressedAttack", "pc_control,Deactivate,,0.5,-1,,"),
            ("PressedAttack", "popup,CloseWindow,,0,-1,,"),
            ("PlayerOn", "popup,OpenWindow,,0,-1,,"),
        ),
    )
    _, _, root, _ = _export(tmp_path, entities)
    outputs = root["entities"][1]["outputs"]
    assert [row["key"] for row in outputs] == ["PressedAttack", "PressedAttack", "PlayerOn"]
    assert outputs[0]["target"] == "pc_control" and outputs[0]["delay"]["value"] == 0.5
    assert not [row for row in root["anomalies"] if row["role"] == "duplicate-scalar-key"]
    assert not any(pair.get("outputLike") for pair in root["entities"][1]["keyValues"])


def test_a_truncated_output_delay_names_the_suffix_atof_dropped(tmp_path):
    entities = _lump(
        WORLDSPAWN,
        _block(("classname", "logic_relay"), ("OnTrigger", "door,Open,,1,5,-1,,")),
    )
    _, _, root, _ = _export(tmp_path, entities)
    assert root["entities"][1]["outputs"][0]["delay"] == {"raw": "1", "value": 1.0}
    entities = _lump(
        WORLDSPAWN,
        _block(("classname", "logic_relay"), ("OnTrigger", "door,Open,,1.5s,-1,,")),
    )
    _, _, root, _ = _export(tmp_path / "second", entities)
    dropped = [row for row in root["anomalies"] if row["role"] == "atof-truncated-number"]
    assert dropped[0]["field"] == "delay" and dropped[0]["dropped"] == "s"
    assert root["entities"][1]["outputs"][0]["delay"] == {"raw": "1.5s", "value": 1.5}


def test_a_model_naming_a_file_the_vocabulary_does_not_type_is_an_untyped_reference(tmp_path):
    entities = _lump(
        WORLDSPAWN, _block(("classname", "env_sprite"), ("model", "sprites/glow.spr"))
    )
    _, _, root, _ = _export(tmp_path, entities)
    assert root["entities"][1]["model"] == {"kind": "sprite", "path": "sprites/glow.spr"}
    assert root["entities"][1]["references"] == []
    row = [item for item in root["anomalies"] if item["role"] == "untyped-file-reference"][0]
    assert row["key"] == "model" and row["extension"] == ".spr"


def test_an_unterminated_quoted_string_is_a_role_the_vocabulary_carries():
    entities = _lump(WORLDSPAWN, '{\n"classname" "light"\n"targetname" "half\n')
    model = _decode(entities)
    roles = [row["role"] for row in model.anomalies]
    assert "unterminated-quoted-string" in roles
    assert "unterminated-quoted-string" in entity_model.ANOMALY_ROLES
    assert model.unresolved[0]["role"] == "unterminated-block"


def test_a_tampered_coordinate_transform_is_refused(tmp_path):
    entities = _lump(
        WORLDSPAWN,
        _block(("classname", "light"), ("origin", "1 2 3"), ("angles", "0 90 0")),
    )
    index, read_bytes = _install(entities)
    closure = seam.load_source_closure(index, MAP_NAME, read_bytes=read_bytes)
    for field in ("origin", "angles"):
        model = seam.decode_map_entities(closure, member_exists=lambda path: False)
        document, binary = exporter.build_document(model)
        root = document["extensions"][seam.MAP_ENTITIES_EXTENSION]
        root["entities"][1][field]["gltf"][0] += 0.5
        with pytest.raises(validation.MapEntitiesGlbValidationError):
            validation.validate_document(document, binary, source_members=closure.members())


def test_a_unit_that_drops_a_declared_output_is_refused(tmp_path):
    entities = _lump(
        WORLDSPAWN,
        _block(("classname", "logic_relay"), ("OnTrigger", "a,Trigger,,0,-1,,")),
    )
    index, read_bytes = _install(entities)
    closure = seam.load_source_closure(index, MAP_NAME, read_bytes=read_bytes)
    model = seam.decode_map_entities(closure, member_exists=lambda path: False)
    document, binary = exporter.build_document(model)
    root = document["extensions"][seam.MAP_ENTITIES_EXTENSION]
    root["entities"][1]["outputs"] = []
    with pytest.raises(validation.MapEntitiesGlbValidationError):
        validation.validate_document(document, binary, source_members=closure.members())


def test_the_summary_publishes_the_warnings_the_operator_is_shown(tmp_path):
    entities = _lump(WORLDSPAWN, _block(("classname", "light"), ("origin", "-3496,92 0 0")))
    path, _, _, _ = _export(tmp_path, entities)
    summary = validation.validate(path)
    assert summary["warnings"] == validation.warnings_for(summary)
    assert "anomaly: atof-truncated-number" in summary["warnings"]
    assert [line for line in summary["warnings"] if line.startswith("the source departs")]


def test_a_span_member_that_names_another_lump_is_refused(tmp_path):
    path, document, root, _ = _export(tmp_path, _lump(WORLDSPAWN))
    member = root["sourceResolution"]["members"][0]
    assert member["path"] == f"{MAP_MEMBER}#lump{entity_model.ENTITIES_LUMP}"
    member["path"] = f"{MAP_MEMBER}#lump4"
    ledger = root["coverage"]["byteLedger"][0]
    ledger["sourcePath"] = member["path"]
    ledger["rangesSha256"] = ranges_sha256(
        member["path"], ledger["byteLength"], ledger["ranges"]
    )
    path.write_bytes(encode_glb(document, b""))
    with pytest.raises(validation.MapEntitiesGlbValidationError):
        validation.validate(path)


def test_a_map_block_that_contradicts_the_span_it_was_cut_from_is_refused(tmp_path):
    entities = _lump(WORLDSPAWN, _block(("classname", "func_brush"), ("model", "*1")))
    for field, value in (
        ("offset", 999999),
        ("length", 12345),
        ("entityCount", 4242),
        ("offsetBase", "file"),
    ):
        path, document, root, _ = _export(tmp_path / field, entities)
        root["map"][field] = value
        path.write_bytes(encode_glb(document, b""))
        with pytest.raises(validation.MapEntitiesGlbValidationError):
            validation.validate(path)


def test_a_brush_model_bound_that_contradicts_the_models_lump_is_refused(tmp_path):
    entities = _lump(WORLDSPAWN, _block(("classname", "func_brush"), ("model", "*1")))
    path, document, root, _ = _export(tmp_path, entities)
    root["map"]["brushModels"]["count"] = 99999
    path.write_bytes(encode_glb(document, b""))
    with pytest.raises(validation.MapEntitiesGlbValidationError):
        validation.validate(path)


def test_a_unit_that_drops_a_reference_and_its_dependency_row_is_refused(tmp_path):
    entities = _lump(
        WORLDSPAWN, _block(("classname", "prop_dynamic"), ("model", "models/props/box.mdl"))
    )
    index, read_bytes = _install(entities, "models/props/box.mdl")
    closure = seam.load_source_closure(index, MAP_NAME, read_bytes=read_bytes)
    model = seam.decode_map_entities(closure, member_exists=lambda path: path.lower() in index)
    document, binary = exporter.build_document(model)
    root = document["extensions"][seam.MAP_ENTITIES_EXTENSION]
    root["entities"][1]["references"] = []
    root["dependencies"] = [row for row in root["dependencies"] if row["role"] != "model"]
    with pytest.raises(validation.MapEntitiesGlbValidationError):
        validation.validate_document(document, binary, source_members=closure.members())


def test_a_unit_that_drops_an_anomaly_a_fresh_walk_derives_is_refused(tmp_path):
    entities = _lump(
        WORLDSPAWN,
        _block(("classname", "logic_relay"), ("OnTrigger", "a,Trigger,,0,-1")),
        _block(("classname", "env_sprite"), ("locksnd", "sound/a.wav")),
        _block(("classname", "light"), ("origin", "-3496,92 0 0")),
    )
    index, read_bytes = _install(entities)
    closure = seam.load_source_closure(index, MAP_NAME, read_bytes=read_bytes)
    for role in ("output-field-count", "untyped-file-reference", "atof-truncated-number"):
        model = seam.decode_map_entities(closure, member_exists=lambda path: False)
        document, binary = exporter.build_document(model)
        root = document["extensions"][seam.MAP_ENTITIES_EXTENSION]
        assert [row for row in root["anomalies"] if row["role"] == role]
        root["anomalies"] = [row for row in root["anomalies"] if row["role"] != role]
        with pytest.raises(validation.MapEntitiesGlbValidationError):
            validation.validate_document(document, binary, source_members=closure.members())


def test_a_dropped_comment_row_is_refused(tmp_path):
    entities = _lump(WORLDSPAWN, '{\n// authored note\n"classname" "light"\n}\n')
    index, read_bytes = _install(entities)
    closure = seam.load_source_closure(index, MAP_NAME, read_bytes=read_bytes)
    model = seam.decode_map_entities(closure, member_exists=lambda path: False)
    document, binary = exporter.build_document(model)
    root = document["extensions"][seam.MAP_ENTITIES_EXTENSION]
    assert len(root["comments"]) == 1
    root["comments"] = []
    with pytest.raises(validation.MapEntitiesGlbValidationError):
        validation.validate_document(document, binary, source_members=closure.members())


def test_the_mapped_field_list_grades_published_fields_not_ledger_owners(tmp_path):
    path, _, root, _ = _export(tmp_path, _lump(WORLDSPAWN))
    mapped = root["coverage"]["mapped"]
    assert "whitespace" not in mapped
    assert "entities[].keyValues" in mapped
    owners = {row["owner"] for row in root["coverage"]["byteLedger"][0]["ranges"]}
    assert "whitespace" in owners
