"""Contract tests for the shared export_v2 unit package.

Every new GLB seam imports `formats.unit_contract`, so what this module pins is not one seam's
behaviour but the shape all of them agree on: the ledger's gaplessness, the container's bytes,
the origin table, the extension root's key order and the kind-independent validation.
"""

from __future__ import annotations

import hashlib
import json
import struct

import pytest

from elysium_pipeline.formats.unit_contract import (
    BIN_CHUNK,
    GLB_MAGIC,
    JSON_CHUNK,
    ROOT_KEYS,
    ByteLedger,
    ByteLedgerError,
    CapsuleError,
    GlbContainerError,
    Origin,
    SourceMember,
    UnitValidationError,
    asset_block,
    asset_id,
    buffer_table,
    completeness,
    coverage_block,
    decode_glb,
    dependency,
    encapsulate,
    encode_glb,
    extension_name,
    extension_root,
    extract_source_member,
    generator,
    identity_block,
    missing_sentinel,
    origin_of,
    pakfile_origin,
    plain,
    ranges_sha256,
    read_glb,
    reference_extension,
    source_capsules,
    source_resolution,
    validate_accessors,
    validate_capsules,
    validate_container,
    validate_extension_root,
    validate_ledgers,
    validate_sceneless,
    verify_ledger_row,
    write_glb,
)
from elysium_pipeline.formats.unit_contract.references import MATERIAL_REFERENCE

MEMBER_PATH = "models/character/npc/lacroix.mdl"
MEMBER = b"MDLZ" + b"\0\0\0\0" + b"lacroix\0" + b"\x01\x02\x03\x04"
EXTENSION = extension_name("demo-unit")
ASSET_PREFIX = "vtmb:demo-unit:"
LOOSE = Origin(kind="loose", root="Unofficial_Patch")


def _ledger(path: str = MEMBER_PATH, data: bytes = MEMBER, **kwargs) -> ByteLedger:
    ledger = ByteLedger(path, data, **kwargs)
    ledger.claim(0, 4, "mapped", "header.magic")
    ledger.claim(4, 4, "reserved-zero", "header.reserved")
    ledger.claim(8, 8, "mapped-string", "header.name")
    ledger.claim(16, 4, "mapped", "body.payload")
    return ledger


def _member(**kwargs) -> SourceMember:
    fields = {"role": "mdl", "path": MEMBER_PATH, "data": MEMBER, "origin": LOOSE}
    fields.update(kwargs)
    return SourceMember(**fields)


def _root(members=None, **overrides):
    members = list(members if members is not None else [_member()])
    rows = [_ledger(member.path, member.data).finish() for member in members]
    fields = {
        "schema_version": "1.0.0",
        "identity": identity_block(ASSET_PREFIX + "lacroix", MEMBER_PATH),
        "source_resolution": source_resolution(members),
        "dependencies": [
            dependency("material", "vtmb:material:skin", "materials/skin.vmt", True)
        ],
        "coverage": coverage_block(mapped=["identity"], byte_ledger=rows),
    }
    fields.update(overrides)
    return extension_root(**fields, demo={"records": 1})


def _document(root=None):
    root = _root() if root is None else root
    return {
        "asset": asset_block("Demo-unit"),
        "extensionsUsed": [EXTENSION],
        "extensionsRequired": [EXTENSION],
        "extensions": {EXTENSION: root},
    }


# --- ledger ----------------------------------------------------------------------------------


def test_a_gapless_ledger_publishes_the_contract_row_shape():
    row = _ledger().finish()
    assert list(row) == [
        "sourcePath",
        "sourceSha256",
        "byteLength",
        "accountedBytes",
        "coveragePercent",
        "stateBytes",
        "rangesSha256",
        "ranges",
    ]
    assert row["byteLength"] == row["accountedBytes"] == len(MEMBER)
    assert row["coveragePercent"] == 100.0
    assert row["sourceSha256"] == hashlib.sha256(MEMBER).hexdigest()
    assert row["stateBytes"] == {"mapped": 8, "mapped-string": 8, "reserved-zero": 4}
    assert [entry["offset"] for entry in row["ranges"]] == [0, 4, 8, 16]
    verify_ledger_row(row, MEMBER)


def test_a_ledger_that_leaves_a_gap_refuses_to_finish():
    ledger = ByteLedger(MEMBER_PATH, MEMBER)
    ledger.claim(0, 4, "mapped", "header.magic")
    ledger.claim(8, 12, "mapped", "body.payload")
    with pytest.raises(ByteLedgerError, match="gap at 4"):
        ledger.finish()


def test_a_ledger_that_leaves_a_byte_unclaimed_refuses_to_finish():
    ledger = ByteLedger(MEMBER_PATH, MEMBER)
    ledger.claim(0, 19, "mapped", "body.payload")
    with pytest.raises(ByteLedgerError, match="ends at 19/20"):
        ledger.finish()


def test_a_second_claim_over_a_claimed_byte_is_refused():
    ledger = ByteLedger(MEMBER_PATH, MEMBER)
    ledger.claim(0, 8, "mapped", "header.magic")
    with pytest.raises(ByteLedgerError, match="overlaps"):
        ledger.claim(4, 8, "mapped", "header.name")


def test_a_zero_state_over_a_non_zero_byte_is_refused():
    ledger = ByteLedger(MEMBER_PATH, MEMBER)
    with pytest.raises(ByteLedgerError, match="non-zero"):
        ledger.claim(0, 4, "padding-zero", "header.magic")
    ledger.claim(4, 4, "reserved-zero", "header.reserved")


def test_a_state_outside_the_vocabulary_is_refused():
    ledger = ByteLedger(MEMBER_PATH, MEMBER)
    with pytest.raises(ByteLedgerError, match="invalid ledger state"):
        ledger.claim(0, 4, "equivalent", "header.magic")


def test_a_claim_beyond_the_member_is_refused():
    ledger = ByteLedger(MEMBER_PATH, MEMBER)
    with pytest.raises(ByteLedgerError, match="exceeds 20"):
        ledger.claim(16, 8, "mapped", "body.payload")


def test_a_span_ledger_hashes_only_the_span():
    whole = b"\xff" * 100 + MEMBER + b"\xee" * 40
    row = _ledger("scripts/table.txt#lacroix", MEMBER, span_offset=100).finish()
    assert row["byteLength"] == len(MEMBER)
    assert row["sourceSha256"] == hashlib.sha256(MEMBER).hexdigest()
    assert row["sourceSha256"] != hashlib.sha256(whole).hexdigest()
    verify_ledger_row(row, whole[100:120])


def test_a_span_ledger_phrases_an_overrun_in_file_coordinates():
    ledger = ByteLedger("scripts/table.txt#lacroix", MEMBER, span_offset=100)
    with pytest.raises(ByteLedgerError, match="file offset 116"):
        ledger.claim(16, 8, "mapped", "body.payload")


def test_the_range_digest_matches_the_contract_formula():
    row = _ledger().finish()
    expected = hashlib.sha256(
        json.dumps(
            {"path": MEMBER_PATH, "byteLength": len(MEMBER), "ranges": row["ranges"]},
            sort_keys=True,
            separators=(",", ":"),
        ).encode("utf-8")
    ).hexdigest()
    assert row["rangesSha256"] == expected
    assert ranges_sha256(MEMBER_PATH, len(MEMBER), row["ranges"]) == expected


def test_a_re_checked_row_notices_a_tampered_state_total():
    row = _ledger().finish()
    row["stateBytes"] = {"mapped": 20}
    with pytest.raises(ByteLedgerError, match="state totals disagree"):
        verify_ledger_row(row)


def test_a_re_checked_row_notices_a_false_zero_claim_only_against_the_bytes():
    ledger = ByteLedger(MEMBER_PATH, b"\0" * 20)
    ledger.claim(0, 20, "padding-zero", "trailer.padding")
    row = ledger.finish()
    verify_ledger_row(row)                       # standalone: nothing to weigh the claim against
    row["sourceSha256"] = hashlib.sha256(MEMBER).hexdigest()
    with pytest.raises(ByteLedgerError, match="false padding-zero claim"):
        verify_ledger_row(row, MEMBER)


def test_an_empty_member_publishes_an_empty_but_complete_ledger():
    row = ByteLedger("scripts/empty.txt", b"").finish()
    assert row["byteLength"] == 0 and row["ranges"] == []
    assert row["sourceSha256"] == hashlib.sha256(b"").hexdigest()
    verify_ledger_row(row, b"")


# --- container -------------------------------------------------------------------------------


def test_a_unit_without_a_bin_chunk_round_trips(tmp_path):
    document = _document()
    destination = tmp_path / "deep" / "lacroix.glb"
    write_glb(document, b"", destination)
    assert not (tmp_path / "deep" / "lacroix.glb.tmp").exists()
    read_document, binary = read_glb(destination)
    assert binary == b""
    assert read_document == json.loads(json.dumps(document))
    assert struct.unpack_from("<III", destination.read_bytes()) == (
        GLB_MAGIC,
        2,
        destination.stat().st_size,
    )


def test_a_unit_with_a_bin_chunk_round_trips(tmp_path):
    document = _document()
    payload = bytes(range(30))
    destination = tmp_path / "lacroix.glb"
    write_glb(document, payload, destination)
    read_document, binary = read_glb(destination)
    assert read_document == json.loads(json.dumps(document))
    assert binary[: len(payload)] == payload
    assert binary[len(payload):] == b"\0\0"


def test_the_json_chunk_is_padded_to_four_bytes_with_spaces():
    encoded = encode_glb({"a": "bcd"}, b"")
    length, kind = struct.unpack_from("<II", encoded, 12)
    assert kind == JSON_CHUNK and length % 4 == 0
    chunk = encoded[20:20 + length]
    assert chunk.rstrip(b" ") == b'{"a":"bcd"}'
    assert chunk[len(b'{"a":"bcd"}'):] == b" "
    assert len(encoded) == 12 + 8 + length


def test_the_bin_chunk_is_padded_to_four_bytes_with_zeros():
    encoded = encode_glb({"a": 1}, b"\x07")
    json_length = struct.unpack_from("<I", encoded, 12)[0]
    bin_length, kind = struct.unpack_from("<II", encoded, 20 + json_length)
    assert kind == BIN_CHUNK and bin_length == 4
    assert encoded[28 + json_length:] == b"\x07\0\0\0"


def test_an_empty_payload_omits_the_bin_chunk():
    encoded = encode_glb({"a": 1})
    json_length = struct.unpack_from("<I", encoded, 12)[0]
    assert len(encoded) == 20 + json_length


def test_the_json_chunk_rejects_nan_and_keeps_writer_key_order():
    with pytest.raises(ValueError):
        encode_glb({"a": float("nan")})
    encoded = encode_glb({"z": 1, "a": 2})
    assert b'{"z":1,"a":2}' in encoded


def test_read_glb_rejects_a_file_that_is_not_a_unit(tmp_path):
    good = encode_glb(_document(), b"")
    path = tmp_path / "unit.glb"

    path.write_bytes(b"XXXX" + good[4:])
    with pytest.raises(GlbContainerError, match="glTF magic"):
        read_glb(path)

    path.write_bytes(good[:4] + struct.pack("<I", 1) + good[8:])
    with pytest.raises(GlbContainerError, match="container version 1"):
        read_glb(path)

    path.write_bytes(good[:8] + struct.pack("<I", len(good) + 4) + good[12:])
    with pytest.raises(GlbContainerError, match="declares"):
        read_glb(path)

    path.write_bytes(good[:12] + struct.pack("<I", len(good)) + good[16:])
    with pytest.raises(GlbContainerError, match="does not fit"):
        read_glb(path)


def test_read_glb_rejects_a_bin_chunk_that_comes_first(tmp_path):
    document = encode_glb({"a": 1})
    json_length = struct.unpack_from("<I", document, 12)[0]
    swapped = (
        document[:12]
        + struct.pack("<II", json_length, BIN_CHUNK)
        + document[20:]
    )
    path = tmp_path / "swapped.glb"
    path.write_bytes(swapped)
    with pytest.raises(GlbContainerError, match="must open with a JSON chunk"):
        read_glb(path)


def test_the_generator_and_asset_block_name_the_kind():
    assert generator("Surface-property") == "Elysium Surface-property GLB Exporter"
    assert asset_block("Texture") == {
        "version": "2.0",
        "generator": "Elysium Texture GLB Exporter",
    }


def test_plain_coerces_records_paths_and_tuples():
    from dataclasses import dataclass
    from enum import Enum
    from pathlib import PurePosixPath

    class Kind(Enum):
        LOOSE = "loose"

    @dataclass
    class Row:
        name: str
        span: tuple

    assert plain(Row("a", (1, 2))) == {"name": "a", "span": [1, 2]}
    assert plain(PurePosixPath("models/a.mdl")) == "models/a.mdl"
    assert plain({"kind": Kind.LOOSE}) == {"kind": "loose"}


# --- origin ----------------------------------------------------------------------------------


def test_a_loose_origin_names_the_install_subdirectory():
    patch = origin_of(("loose", r"C:\games\vtmb\Unofficial_Patch\models\a.mdl"))
    retail = origin_of(("loose", "/mnt/vtmb/Vampire/models/a.mdl"))
    stray = origin_of(("loose", "/tmp/scratch/a.mdl"))
    assert patch.to_json() == {"kind": "loose", "root": "Unofficial_Patch"}
    assert retail.to_json() == {"kind": "loose", "root": "Vampire"}
    assert stray.to_json() == {"kind": "loose", "root": "loose"}


def test_a_vpk_origin_names_the_pack_and_the_extent():
    origin = origin_of(("vpk", (r"C:\games\vtmb\Vampire\pack001.vpk", 4096, 512)))
    assert origin.to_json() == {
        "kind": "vpk",
        "container": "pack001.vpk",
        "offset": 4096,
        "size": 512,
    }


def test_a_pakfile_origin_nests_the_bsp_origin():
    bsp = origin_of(("vpk", ("pack003.vpk", 10, 20)))
    packed = pakfile_origin("sp_tutorial_1", "materials/a.vmt", bsp)
    assert packed.to_json() == {
        "kind": "bsp-pakfile",
        "map": "sp_tutorial_1",
        "member": "materials/a.vmt",
        "origin": {"kind": "vpk", "container": "pack003.vpk", "offset": 10, "size": 20},
    }


def test_an_origin_without_its_kinds_fields_is_refused():
    with pytest.raises(ValueError, match="unknown source origin kind"):
        Origin(kind="steam")
    with pytest.raises(ValueError, match="install subdirectory"):
        Origin(kind="loose")
    with pytest.raises(ValueError, match="container, offset and size"):
        Origin(kind="vpk", container="pack001.vpk")
    with pytest.raises(ValueError, match="nests the BSP"):
        Origin(kind="bsp-pakfile", map="a", member="b")


def test_a_source_member_publishes_its_identity_and_its_span():
    whole = _member().to_json()
    assert whole == {
        "role": "mdl",
        "path": MEMBER_PATH,
        "origin": {"kind": "loose", "root": "Unofficial_Patch"},
        "byteLength": 20,
        "sha256": hashlib.sha256(MEMBER).hexdigest(),
    }
    cut = _member(path="scripts/table.txt#lacroix", span=(100, 20)).to_json()
    assert cut["span"] == {"offset": 100, "length": 20}
    assert cut["byteLength"] == 20


def test_a_span_that_disagrees_with_the_bytes_is_refused():
    with pytest.raises(ValueError, match="span claims 19 bytes"):
        _member(span=(100, 19))


def test_the_member_table_declares_the_up_first_policy():
    table = source_resolution([_member()])
    assert table["policy"] == "up-first"
    assert [row["path"] for row in table["members"]] == [MEMBER_PATH]


def test_read_member_uses_the_injected_reader():
    from elysium_pipeline.formats.unit_contract import read_member

    index = {MEMBER_PATH: ("loose", "C:/x")}
    assert read_member(index, MEMBER_PATH, read_bytes=lambda idx, key: MEMBER) == MEMBER
    assert read_member(index, "absent", read_bytes=lambda idx, key: None) is None


# --- references ------------------------------------------------------------------------------


def test_a_stable_identity_is_folded_and_forward_slashed():
    assert asset_id("material", r"Models\Character\Teeth\UpperTeeth") == (
        "vtmb:material:models/character/teeth/upperteeth"
    )
    assert missing_sentinel("texture", "Models/A.TTH") == "vtmb:missing-texture:models/a.tth"


def test_a_dependency_row_emits_only_the_optional_keys_it_was_given():
    plain_row = dependency("material", "vtmb:material:a", "materials/a.vmt", True)
    assert plain_row == {
        "role": "material",
        "asset": "vtmb:material:a",
        "sourcePath": "materials/a.vmt",
        "resolved": True,
    }
    pinned = dependency(
        "sound", "vtmb:sound:a.mp3", "sound/a.wav", True, resolution="mp3-first", byteLength=12
    )
    assert list(pinned) == ["role", "asset", "sourcePath", "resolved", "resolution", "byteLength"]
    with pytest.raises(ValueError, match="unknown dependency field"):
        dependency("material", "vtmb:material:a", "materials/a.vmt", True, mystery=1)


def test_an_object_local_reference_binds_through_its_extension():
    assert reference_extension(MATERIAL_REFERENCE, "vtmb:material:a") == {
        "extensions": {"ELYSIUM_material_reference": {"asset": "vtmb:material:a"}}
    }
    with pytest.raises(ValueError, match="unknown reference extension"):
        reference_extension("ELYSIUM_mystery_reference", "vtmb:material:a")


# --- coverage --------------------------------------------------------------------------------


def test_the_extension_name_underscores_a_hyphenated_kind():
    assert extension_name("surface-property") == "ELYSIUM_vtmb_surface_property"
    assert extension_name("texture") == "ELYSIUM_vtmb_texture"


def test_the_coverage_block_always_publishes_its_six_lists_in_order():
    block = coverage_block(mapped=["identity"], unresolved=[{"field": "flags"}])
    assert list(block) == [
        "mapped",
        "typedUnidentified",
        "omittedProven",
        "byteLedger",
        "unresolved",
        "unsupported",
    ]
    assert block["typedUnidentified"] == []


def test_an_identity_names_one_source_path_or_several():
    single = identity_block("vtmb:demo:a", "models/a.mdl", name="a")
    assert list(single) == ["asset", "sourcePath", "sourcePolicy", "name"]
    several = identity_block("vtmb:demo:a", ["models/a.mdl", "models/a.vtx"])
    assert several["sourcePaths"] == ["models/a.mdl", "models/a.vtx"]
    assert "sourcePath" not in several


def test_the_extension_root_opens_with_the_contract_keys_then_the_seams_own():
    root = _root()
    assert list(root) == list(ROOT_KEYS) + ["demo"]


def test_a_kind_specific_key_may_not_shadow_a_contract_key():
    with pytest.raises(ValueError, match="are the contract's"):
        extension_root(
            schema_version="1.0.0",
            identity={},
            source_resolution={},
            dependencies=[],
            coverage={},
            **{"schemaVersion": "2.0.0", "sourceResolution": {}},
        )


# --- validation ------------------------------------------------------------------------------


def test_a_well_formed_unit_passes_every_kind_independent_check():
    document = _document()
    root = validate_extension_root(
        document, EXTENSION, asset_prefix=ASSET_PREFIX, schema_version="1.0.0"
    )
    validate_container(document, b"")
    validate_sceneless(document)
    validate_ledgers(root, [_member()])
    validate_capsules(document, b"", root, [_member()])
    assert completeness(root) == {"unresolved": 0, "unsupported": 0, "typedUnidentified": 0}


def test_an_identity_outside_the_kinds_namespace_is_refused():
    document = _document()
    with pytest.raises(UnitValidationError, match="is outside"):
        validate_extension_root(document, EXTENSION, asset_prefix="vtmb:material:")


def test_an_extension_that_is_used_but_not_required_is_refused():
    document = _document()
    document["extensionsRequired"] = []
    with pytest.raises(UnitValidationError, match="extensionsRequired"):
        validate_extension_root(document, EXTENSION, asset_prefix=ASSET_PREFIX)
    document["extensionsRequired"] = [EXTENSION]
    document["extensionsUsed"] = []
    with pytest.raises(UnitValidationError, match="extensionsUsed"):
        validate_extension_root(document, EXTENSION, asset_prefix=ASSET_PREFIX)


def test_a_root_whose_keys_are_out_of_order_is_refused():
    root = _root()
    reordered = {"identity": root["identity"]}
    reordered.update(root)
    document = _document(reordered)
    with pytest.raises(UnitValidationError, match="root opens with"):
        validate_extension_root(document, EXTENSION, asset_prefix=ASSET_PREFIX)


def test_a_declared_schema_version_must_match():
    document = _document()
    with pytest.raises(UnitValidationError, match="declares schema"):
        validate_extension_root(
            document, EXTENSION, asset_prefix=ASSET_PREFIX, schema_version="2.0.0"
        )


def test_a_malformed_dependency_row_is_refused():
    root = _root(dependencies=[{"role": "material", "asset": "materials/a.vmt"}])
    with pytest.raises(UnitValidationError, match="dependency 0 is missing"):
        validate_extension_root(_document(root), EXTENSION, asset_prefix=ASSET_PREFIX)


def test_a_scene_less_unit_may_not_declare_nodes():
    document = _document()
    document["nodes"] = [{"name": "root"}]
    with pytest.raises(UnitValidationError, match="no nodes"):
        validate_sceneless(document)
    document.pop("nodes")
    document["skins"] = [{"joints": [0]}]
    with pytest.raises(UnitValidationError, match="no skins"):
        validate_sceneless(document)


def test_a_buffer_without_a_bin_chunk_is_refused():
    document = _document()
    document["buffers"] = [{"byteLength": 4}]
    with pytest.raises(UnitValidationError, match="has no BIN chunk"):
        validate_container(document, b"")


def test_a_buffer_view_must_sit_on_buffer_zero_and_align():
    document = _document()
    binary = bytes(32)
    document["buffers"] = [{"byteLength": 32}]
    document["bufferViews"] = [{"buffer": 1, "byteOffset": 0, "byteLength": 32}]
    with pytest.raises(UnitValidationError, match="does not use buffer 0"):
        validate_container(document, binary)
    document["bufferViews"] = [{"buffer": 0, "byteOffset": 2, "byteLength": 30}]
    with pytest.raises(UnitValidationError, match="unaligned offset 2"):
        validate_container(document, binary)
    document["bufferViews"] = [{"buffer": 0, "byteOffset": 0, "byteLength": 64}]
    with pytest.raises(UnitValidationError, match="outside the BIN chunk"):
        validate_container(document, binary)
    document["bufferViews"] = [{"buffer": 0, "byteOffset": 0, "byteLength": 32}]
    validate_container(document, binary)


def test_an_accessor_that_overruns_its_view_is_refused():
    document = _document()
    binary = bytes(32)
    document["buffers"] = [{"byteLength": 32}]
    document["bufferViews"] = [{"buffer": 0, "byteOffset": 0, "byteLength": 32}]
    document["accessors"] = [{"bufferView": 0, "componentType": 5126, "type": "VEC3", "count": 2}]
    validate_accessors(document, binary)
    document["accessors"][0]["count"] = 4
    with pytest.raises(UnitValidationError, match="needs 48 bytes"):
        validate_accessors(document, binary)


def test_every_member_owns_exactly_one_ledger_row():
    root = _root()
    root["coverage"]["byteLedger"] = []
    with pytest.raises(UnitValidationError, match="0 byte ledger"):
        validate_ledgers(root)


def test_a_tampered_range_table_is_caught_on_read_back():
    root = _root()
    root["coverage"]["byteLedger"][0]["ranges"][0]["length"] = 5
    with pytest.raises(UnitValidationError, match="byte range 1 starts at 4, not 5"):
        validate_ledgers(root)


def test_a_ledger_that_disagrees_with_its_member_identity_is_caught():
    root = _root()
    root["coverage"]["byteLedger"][0]["sourceSha256"] = hashlib.sha256(b"other").hexdigest()
    with pytest.raises(UnitValidationError, match="ledger digest disagrees"):
        validate_ledgers(root)


def test_export_time_validation_weighs_the_ledger_against_the_member_bytes():
    """The declared identity can agree while the bytes behind it do not."""

    root = _root()
    swapped = MEMBER[:8] + b"beckett\0" + MEMBER[16:]
    with pytest.raises(UnitValidationError, match="source digest disagrees"):
        validate_ledgers(root, [_member(data=swapped)])
    with pytest.raises(UnitValidationError, match="false reserved-zero claim"):
        validate_ledgers(root, [_member(data=MEMBER[:4] + b"\x01\0\0\0" + MEMBER[8:])])


# --- source capsule --------------------------------------------------------------------------


def _capsuled(members=None, **overrides):
    """A document whose members travel in its BIN chunk, plus that chunk and its root.

    The root carries no byte ledger: the capsule is checked on its own, which is what lets a seam
    be judged on carrying the bytes separately from being judged on decoding them.
    """

    members = list(members if members is not None else [_member()])
    resolution, views, binary = encapsulate(members)
    fields = {
        "schema_version": "1.0.0",
        "identity": identity_block(ASSET_PREFIX + "lacroix", MEMBER_PATH),
        "source_resolution": resolution,
        "dependencies": [],
        "coverage": coverage_block(mapped=["identity"]),
    }
    fields.update(overrides)
    root = extension_root(**fields, demo={"records": 1})
    document = _document(root)
    if binary:
        document["buffers"] = buffer_table(binary)
        document["bufferViews"] = views
    return document, binary, root


def test_a_capsule_round_trips_the_member_bytes_exactly():
    tricky = b"\xef\xbb\xbfKey\t\"a b \"\r\n  trailing   \r\n\x93quoted\x94\n"
    document, binary, root = _capsuled([_member(data=tricky)])
    assert document["buffers"] == [{"byteLength": len(tricky)}]
    assert document["bufferViews"] == [
        {"buffer": 0, "byteOffset": 0, "byteLength": len(tricky)}
    ]
    member = root["sourceResolution"]["members"][0]
    assert member["capsule"] == {"bufferView": 0, "byteLength": len(tricky)}
    assert extract_source_member(document, binary, member) == tricky
    assert source_capsules(document, binary, root) == {MEMBER_PATH: tricky}
    validate_capsules(document, binary, root, [_member(data=tricky)])


def test_several_members_are_packed_four_byte_aligned_and_stay_separable():
    first = _member(role="mdl", path="a/one.txt", data=b"one" * 7)      # 21 bytes, unaligned
    second = _member(role="vtx", path="a/two.txt", data=b"two" * 5)
    document, binary, root = _capsuled([first, second])
    views = document["bufferViews"]
    assert [view["byteOffset"] % 4 for view in views] == [0, 0]
    assert views[1]["byteOffset"] == 24                                 # 21 padded up to 24
    assert source_capsules(document, binary, root) == {
        "a/one.txt": first.data, "a/two.txt": second.data
    }
    validate_capsules(document, binary, root, [first, second])


def test_an_empty_member_capsules_to_no_buffer_view_and_no_bin_chunk():
    empty = _member(data=b"")
    document, binary, root = _capsuled([empty])
    assert binary == b"" and "buffers" not in document and "bufferViews" not in document
    assert root["sourceResolution"]["members"][0]["capsule"] == {"byteLength": 0}
    assert source_capsules(document, binary, root) == {MEMBER_PATH: b""}
    validate_container(document, binary)
    validate_capsules(document, binary, root, [empty])


def test_a_capsule_the_bin_chunk_does_not_carry_is_refused():
    document, binary, root = _capsuled()
    document["bufferViews"][0]["byteLength"] -= 4
    with pytest.raises(UnitValidationError, match="declares .* bytes and its view offers"):
        validate_capsules(document, binary, root)


def test_a_member_with_no_capsule_at_all_is_refused_once_the_seam_declares_one():
    document, binary, root = _capsuled()
    root["sourceResolution"]["members"][0].pop("capsule")
    with pytest.raises(UnitValidationError, match="carries no source capsule"):
        validate_capsules(document, binary, root)


def test_a_capsule_whose_bytes_are_not_the_member_it_names_is_refused():
    document, binary, root = _capsuled()
    tampered = b"beckett" + binary[7:]
    with pytest.raises(UnitValidationError, match="not the member it names"):
        validate_capsules(document, tampered, root)


def test_a_capsule_that_disagrees_with_the_bytes_the_exporter_read_is_refused():
    """The unit can be self-consistent and still not be the file the exporter actually read."""

    document, binary, root = _capsuled()
    other = _member(data=b"lacroix" + MEMBER[7:])
    assert len(other.data) == len(MEMBER) and other.data != MEMBER
    validate_capsules(document, binary, root)                    # standalone: self-consistent
    with pytest.raises(UnitValidationError, match="disagrees with the member it was cut from"):
        validate_capsules(document, binary, root, [other])


def test_a_seam_that_declares_no_capsule_still_validates_and_may_not_smuggle_one():
    document = _document()
    root = document["extensions"][EXTENSION]
    validate_capsules(document, b"", root, [_member()])
    root["sourceResolution"]["members"][0]["capsule"] = {"byteLength": 0}
    with pytest.raises(UnitValidationError, match="does not declare"):
        validate_capsules(document, b"", root, [_member()])


def test_an_unknown_capsule_encoding_is_refused():
    document, binary, root = _capsuled()
    root["sourceResolution"]["capsule"] = {"encoding": "zlib"}
    with pytest.raises(UnitValidationError, match="unknown source capsule"):
        validate_capsules(document, binary, root)


def test_extracting_from_a_row_that_is_not_a_capsule_is_an_error_not_an_empty_result():
    document, binary, _root_ = _capsuled()
    with pytest.raises(CapsuleError, match="carries no source capsule"):
        extract_source_member(document, binary, {"path": MEMBER_PATH})
    with pytest.raises(CapsuleError, match="names no bufferView of this unit"):
        extract_source_member(
            document, binary, {"path": MEMBER_PATH, "capsule": {"bufferView": 9, "byteLength": 4}}
        )


def test_a_capsuled_unit_survives_the_container_it_is_published_through():
    document, binary, _root_ = _capsuled()
    validate_container(document, binary)
    read_document, read_binary = decode_glb(encode_glb(document, binary))
    read_root = read_document["extensions"][EXTENSION]
    assert source_capsules(read_document, read_binary, read_root) == {MEMBER_PATH: MEMBER}


def test_completeness_and_warnings_report_every_incomplete_row():
    root = _root(
        coverage=coverage_block(
            mapped=["identity"],
            typed_unidentified=[{"field": "unknown0x24"}],
            byte_ledger=[_ledger().finish()],
            unresolved=[{"field": "flags"}],
            unsupported=[{"field": "eyelidState"}],
        )
    )
    root["omissions"] = [{"reason": "empty-member"}]
    root["anomalies"] = [{"role": "repeated-scalar-key"}]
    assert completeness(root) == {
        "unresolved": 1,
        "unsupported": 1,
        "typedUnidentified": 1,
    }
    from elysium_pipeline.formats.unit_contract import warnings_for

    assert warnings_for(root) == [
        "unresolved: flags",
        "unsupported: eyelidState",
        "typed but unidentified: unknown0x24",
        "omitted: empty-member",
        "anomaly: repeated-scalar-key",
    ]


def test_a_sentinel_reference_in_omitted_proven_becomes_a_warning():
    """A `vtmb:missing-<kind>:` sentinel produces no dependency row by contract and is graded
    `coverage.omittedProven` instead; `warnings_for` is the only reader that speaks it."""

    from elysium_pipeline.formats.unit_contract import warnings_for

    root = _root(
        coverage=coverage_block(
            mapped=["identity"],
            omitted_proven=[
                {
                    "path": "materialBindings.slots[0]",
                    "reason": "studio-texture-name-has-no-vmt",
                    "asset": missing_sentinel("material", "0:body"),
                    "candidates": ["materials/body.vmt"],
                }
            ],
        )
    )
    assert warnings_for(root) == ["sentinel: studio-texture-name-has-no-vmt"]


def test_omitted_proven_rows_with_no_sentinel_asset_stay_silent():
    """Byte-ledger residue is graded `omitted-proven` too, at a volume that would drown the
    operator if every row became a warning; only a sentinel reference is carved out."""

    from elysium_pipeline.formats.unit_contract import warnings_for

    root = _root(
        coverage=coverage_block(
            mapped=["identity"],
            omitted_proven=[
                {"path": "mdl.padding", "reason": "compiler-trailer-residue"},
                {"path": "vtx.comparison.overlap", "reason": "legacy-vtx-equivalent"},
            ],
        )
    )
    assert warnings_for(root) == []


def test_sentinel_warnings_sit_between_coverage_and_root_level_warnings():
    from elysium_pipeline.formats.unit_contract import warnings_for

    root = _root(
        coverage=coverage_block(
            mapped=["identity"],
            unresolved=[{"field": "flags"}],
            omitted_proven=[
                {
                    "path": "materialBindings.slots[0]",
                    "reason": "studio-texture-name-has-no-vmt",
                    "asset": missing_sentinel("material", "0:body"),
                }
            ],
        )
    )
    root["omissions"] = [{"reason": "empty-member"}]
    assert warnings_for(root) == [
        "unresolved: flags",
        "sentinel: studio-texture-name-has-no-vmt",
        "omitted: empty-member",
    ]
