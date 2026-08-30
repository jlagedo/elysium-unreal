"""The shader-program seam: two unit kinds, one corpus, synthetic bytes only.

Every fixture here is built in the test, and the install is a dictionary with a `read_bytes`
lambda beside it, so nothing in this module depends on a VtMB installation being present.
"""

from __future__ import annotations

import hashlib
import json
import struct

import pytest

from elysium_pipeline.exporters import shader_program_glb as exporter
from elysium_pipeline.formats.shader_program_glb import (
    ShaderKeyError,
    decode_shader_program,
    decode_shader_source,
    load_shader_source_closure,
    load_source_closure,
    normalize_program_key,
    normalize_source_key,
    program_output_relative_path,
    source_output_relative_path,
)
from elysium_pipeline.formats.shader_program_glb import ctab as ctab_module
from elysium_pipeline.formats.shader_program_glb import tokens as tokens_module
from elysium_pipeline.formats.shader_program_glb.decode import (
    ShaderSourceDecodeError,
    ShaderProgramDecodeError,
)
from elysium_pipeline.validation import shader_program_glb as validation

PSH_KEY = "unlitgeneric"
PSH_PATH = f"materials/dxshaders/{PSH_KEY}.psh"
VCS_PATH = f"shaders/psh/{PSH_KEY}.vcs"

PS_1_1 = 0xFFFF0101
TEX = 0x00000042
MUL = 0x00000005
MOV = 0x00000001
END = 0x0000FFFF

DEST_T0 = 0x80000000 | (3 << 28) | (0xF << 16)          # t0, full write mask
DEST_R0_RGB = 0x80000000 | (0 << 28) | (0x7 << 16)      # r0.rgb
DEST_R0_A = 0x80000000 | (0 << 28) | (0x8 << 16)        # r0.a
SRC_T0 = 0x80000000 | (3 << 28) | (0xE4 << 16)          # t0
SRC_C0 = 0x80000000 | (2 << 28) | (0xE4 << 16)          # c0
SRC_T0_A = 0x80000000 | (3 << 28) | (0xFF << 16)        # t0.a

PS_1_4 = 0xFFFF0104
PS_2_0 = 0xFFFF0200
VS_2_0 = 0xFFFE0200
PHASE = 0x0000FFFD

TWIN_ASSET = f"vtmb:shader-program:psh/{PSH_KEY}"


def _reg(kind: int, index: int) -> int:
    """One parameter token's register field: type in bits 28-30 and 11-12, index in bits 0-10."""

    return 0x80000000 | ((kind & 7) << 28) | (((kind >> 3) & 3) << 11) | (index & 0x7FF)


def _dest(kind: int, index: int, mask: int = 0xF) -> int:
    return _reg(kind, index) | (mask << 16)


def _src(kind: int, index: int, swizzle: int = 0xE4) -> int:
    return _reg(kind, index) | (swizzle << 16)


def _op(opcode: int, operand_tokens: int = 0) -> int:
    """An instruction token that declares its operand length, as shader model 2 requires."""

    return opcode | (operand_tokens << 24)


SOURCE_TEXT = (
    "ps.1.1\r\n"
    "\r\n"
    "; draw a texture\r\n"
    "def c1, 1.0f, 0.5f, 0.25f, 0.0f\r\n"
    "tex t0\r\n"
    "mul r0.rgb, c0, t0\t; scale by the constant\r\n"
    "+ mov r0.a, t0.a\r\n"
    "mov r0.a, t0.a"
)

#: The tokens `SOURCE_TEXT` assembles to, written out by hand rather than by the assembler.
SOURCE_TOKENS = (
    PS_1_1,
    0x00000051, 0x80000000 | (2 << 28) | (0xF << 16) | 1,
    struct.unpack("<I", struct.pack("<f", 1.0))[0],
    struct.unpack("<I", struct.pack("<f", 0.5))[0],
    struct.unpack("<I", struct.pack("<f", 0.25))[0],
    struct.unpack("<I", struct.pack("<f", 0.0))[0],
    TEX, DEST_T0,
    MUL, DEST_R0_RGB, SRC_C0, SRC_T0,
    MOV | 0x40000000, DEST_R0_A, SRC_T0_A,
    MOV, DEST_R0_A, SRC_T0_A,
    END,
)


def _words(*values: int) -> bytes:
    return struct.pack(f"<{len(values)}I", *values)


def _vcs(
    streams: list[bytes],
    *,
    version: int = 0,
    total: int | None = None,
    dynamic: int = 1,
    flags: int = 64,
    centroid: int = 0,
    entries: list[tuple[int, int]] | None = None,
    gap: bytes = b"",
    trailing: bytes = b"",
) -> bytes:
    """A synthetic combo bundle: header, offset/size table, streams, optional fill."""

    count = len(streams) if total is None else total
    table_end = 20 + 8 * (len(entries) if entries is not None else count)
    computed: list[tuple[int, int]] = []
    body = bytearray()
    cursor = table_end
    for index, stream in enumerate(streams):
        if index:
            body.extend(gap)
            cursor += len(gap)
        computed.append((cursor, len(stream)))
        body.extend(stream)
        cursor += len(stream)
    body.extend(trailing)
    table = entries if entries is not None else computed
    out = bytearray(struct.pack("<5i", version, count, dynamic, flags, centroid))
    for offset, size in table:
        out.extend(struct.pack("<2i", offset, size))
    out.extend(body)
    return bytes(out)


def _ctab_payload() -> bytes:
    """A `CTAB` comment payload with one float4 constant, laid out by hand."""

    creator, target, name = b"unit test\0", b"ps_2_0\0", b"g_Colour\0"
    header_at, info_at, type_at = 0, 28, 48
    creator_at = 64
    target_at = creator_at + len(creator)
    name_at = target_at + len(target)
    body = bytearray(name_at + len(name))
    struct.pack_into("<7I", body, header_at, 28, creator_at, 0xFFFF0200, 1, info_at, 0, target_at)
    struct.pack_into("<IHHHHII", body, info_at, name_at, 2, 3, 1, 0, type_at, 0)
    struct.pack_into("<6HI", body, type_at, 1, 3, 1, 4, 1, 0, 0)
    body[creator_at:creator_at + len(creator)] = creator
    body[target_at:target_at + len(target)] = target
    body[name_at:name_at + len(name)] = name
    payload = b"CTAB" + bytes(body)
    return payload + b"\xab" * (-len(payload) % 4)


def _ctab_struct_payload() -> bytes:
    """A `CTAB` whose one constant is a struct, so its member table has to be followed."""

    creator, target = b"unit test\0", b"ps_2_0\0"
    name, member_name = b"g_Light\0", b"colour\0"
    info_at, struct_type_at, member_type_at = 28, 48, 64
    member_info_at = 80
    creator_at = member_info_at + 8
    target_at = creator_at + len(creator)
    name_at = target_at + len(target)
    member_name_at = name_at + len(name)
    body = bytearray(member_name_at + len(member_name))
    struct.pack_into("<7I", body, 0, 28, creator_at, 0xFFFF0200, 1, info_at, 0, target_at)
    struct.pack_into("<IHHHHII", body, info_at, name_at, 2, 0, 1, 0, struct_type_at, 0)
    struct.pack_into("<6HI", body, struct_type_at, 5, 0, 1, 4, 1, 1, member_info_at)
    struct.pack_into("<6HI", body, member_type_at, 1, 3, 1, 4, 1, 0, 0)
    struct.pack_into("<2I", body, member_info_at, member_name_at, member_type_at)
    body[creator_at:creator_at + len(creator)] = creator
    body[target_at:target_at + len(target)] = target
    body[name_at:name_at + len(name)] = name
    body[member_name_at:member_name_at + len(member_name)] = member_name
    payload = b"CTAB" + bytes(body)
    return payload + b"\xab" * (-len(payload) % 4)


def _ctab_stream(payload: bytes | None = None) -> bytes:
    payload = _ctab_payload() if payload is None else payload
    comment = 0x0000FFFE | ((len(payload) // 4) << 16)
    return _words(0xFFFF0200, comment) + payload + _words(END)


def _index(**members: bytes) -> tuple[dict, object]:
    """A fake install index and the reader that serves it."""

    index = {
        path: ("loose", f"C:/game/Unofficial_Patch/{path}") for path in members
    }
    return index, lambda _index, key: members.get(key)


def _install(source: str = SOURCE_TEXT, compiled: bytes | None = None) -> tuple[dict, object]:
    members: dict[str, bytes] = {}
    if source is not None:
        members[PSH_PATH] = source.encode("ascii")
    if compiled is not None:
        members[VCS_PATH] = compiled
    return _index(**members)


def _source_model(source: str = SOURCE_TEXT, compiled: bytes | None = None):
    index, reader = _install(source, compiled)
    closure = load_shader_source_closure(index, PSH_KEY, read_bytes=reader)
    return closure, decode_shader_source(closure)


def _program_model(compiled: bytes, *, key: str = "psh/" + PSH_KEY, source: str | None = None):
    members: dict[str, bytes] = {f"shaders/{key}.vcs": compiled}
    if source is not None:
        members[PSH_PATH] = source.encode("ascii")
    index, reader = _index(**members)
    closure = load_source_closure(index, key, read_bytes=reader)
    return closure, decode_shader_program(closure)


def _ledger_ranges(model) -> list[dict]:
    return model.byte_ledger[0]["ranges"]


# ------------------------------------------------------------------------------- identity rules


def test_a_source_key_tolerates_the_root_prefix_and_the_source_extension():
    assert normalize_source_key("materials/dxshaders/Eyes.psh") == "eyes"
    assert normalize_source_key("dxshaders/eyes") == "eyes"
    assert normalize_source_key("eyes") == "eyes"


def test_a_program_key_keeps_its_subdirectory_and_drops_the_vcs_extension():
    assert normalize_program_key("shaders/psh/LightmappedGeneric.vcs") == "psh/lightmappedgeneric"
    assert normalize_program_key("fxc/refract_ps20") == "fxc/refract_ps20"


def test_a_program_key_outside_the_three_shipped_subdirectories_is_refused():
    with pytest.raises(ShaderKeyError):
        normalize_program_key("source/eyes")
    with pytest.raises(ShaderKeyError):
        normalize_program_key("eyes")


def test_each_kind_publishes_into_its_own_family_directory():
    assert source_output_relative_path("eyes").as_posix() == "source/eyes.glb"
    assert program_output_relative_path("psh/eyes").as_posix() == "psh/eyes.glb"


def test_one_file_carries_one_identity():
    _, source = _source_model()
    _, program = _program_model(_vcs([_words(*SOURCE_TOKENS)]))
    assert source.asset_id == "vtmb:shader-source:unlitgeneric"
    assert program.asset_id == "vtmb:shader-program:psh/unlitgeneric"
    document, _ = exporter.build_source_document(source)
    root = document["extensions"]["ELYSIUM_vtmb_shader_source"]
    assert root["identity"]["asset"] == source.asset_id
    assert list(root)[:5] == [
        "schemaVersion", "identity", "sourceResolution", "dependencies", "coverage"
    ]


def test_both_kinds_declare_their_extension_used_and_required_and_stay_scene_less():
    _, source = _source_model()
    _, program = _program_model(_vcs([_words(*SOURCE_TOKENS)]))
    for document, extension, title in (
        (exporter.build_source_document(source)[0], "ELYSIUM_vtmb_shader_source", "Shader-source"),
        (exporter.build_document(program)[0], "ELYSIUM_vtmb_shader_program", "Shader-program"),
    ):
        assert document["extensionsUsed"] == [extension]
        assert document["extensionsRequired"] == [extension]
        assert document["asset"]["generator"] == f"Elysium {title} GLB Exporter"
        for forbidden in ("scenes", "nodes", "meshes", "images", "textures", "samplers",
                          "buffers", "accessors"):
            assert forbidden not in document


def test_every_published_record_is_json_plain():
    _, program = _program_model(_vcs([_ctab_stream()]))
    document, binary = exporter.build_document(program)
    assert binary == b""
    assert json.loads(json.dumps(document, allow_nan=False)) == document


# -------------------------------------------------------------------------------- source bytes


def test_a_shader_source_claims_every_byte_of_its_member_exactly_once():
    closure, model = _source_model()
    row = model.byte_ledger[0]
    assert row["coveragePercent"] == 100.0
    assert row["accountedBytes"] == row["byteLength"] == len(closure.psh.data)
    assert row["sourceSha256"] == hashlib.sha256(closure.psh.data).hexdigest()
    cursor = 0
    for span in row["ranges"]:
        assert span["offset"] == cursor
        cursor += span["length"]
    assert cursor == len(closure.psh.data)
    assert set(row["stateBytes"]) == {"mapped-text"}


def test_a_line_with_a_comment_splits_into_a_code_range_and_a_comment_range():
    _, model = _source_model()
    owners = [span["owner"] for span in _ledger_ranges(model)]
    assert "source.lines[5]" in owners
    assert "source.lines[5].comment" in owners
    comment_line = model.lines[5]
    assert comment_line.marker == ";"
    assert model.comments[1]["sourceOffset"] == comment_line.comment_offset


def test_a_comment_only_line_is_claimed_by_its_comment_alone():
    _, model = _source_model()
    owners = [span["owner"] for span in _ledger_ranges(model)]
    assert "source.lines[2].comment" in owners
    assert "source.lines[2]" not in owners        # the line has no code part to pay for


def test_the_parser_refuses_a_line_it_cannot_classify():
    with pytest.raises(ShaderSourceDecodeError):
        _source_model("ps.1.1\r\nquux r0, t0\r\n")
    with pytest.raises(ShaderSourceDecodeError):
        _source_model("ps.1.1\r\nmov r0, q9\r\n")
    with pytest.raises(ShaderSourceDecodeError):
        _source_model("mov r0, t0\r\n")


def test_a_def_line_becomes_a_define_row_with_four_floats():
    _, model = _source_model()
    define = model.defines[0]
    assert (define.register.register_class, define.register.index) == ("const", 1)
    assert define.values == (1.0, 0.5, 0.25, 0.0)
    assert define.source_offset == model.lines[define.line].offset


def test_a_leading_plus_and_a_trailing_plus_both_co_issue_the_instruction_they_mark():
    _, model = _source_model()
    leading = next(row for row in model.instructions if row.co_issue_marker == "leading")
    assert leading.co_issued is True
    _, trailing = _source_model("ps.1.1\r\nmul r0.rgb, c0, t0 +\r\nmov r0.a, t0.a\r\n")
    assert [row.co_issued for row in trailing.instructions] == [False, True]
    assert trailing.instructions[1].co_issue_marker == "trailing-on-previous"


def test_a_source_records_the_instruction_modifiers_and_operand_modifiers_it_spells():
    _, model = _source_model("ps.1.1\r\nmul_x2_sat r0.rgb, 1-c0, -t0_bx2\r\n")
    instruction = model.instructions[0]
    assert instruction.opcode == "mul"
    assert set(instruction.modifiers) == {"_x2", "_sat"}
    assert instruction.destination.write_mask == "rgb"
    assert instruction.sources[0].complement is True
    assert (instruction.sources[1].negate, instruction.sources[1].modifier) == (True, "_bx2")


def test_a_cpp_comment_marker_is_carried_as_an_anomaly_rather_than_dropped():
    _, model = _source_model("ps.1.1\r\nmov r0, t0\t// squared\r\n")
    assert [row["role"] for row in model.anomalies] == ["cpp-comment-marker"]
    assert model.comments[0]["marker"] == "//"


def test_a_source_names_the_compiled_twin_by_stable_id_and_declares_it_once():
    _, without = _source_model()
    assert without.compiled_twin == {
        "present": False,
        "asset": f"vtmb:missing-shader-program:psh/{PSH_KEY}",
        "sourcePath": VCS_PATH,
    }
    assert without.dependencies == []
    _, with_twin = _source_model(compiled=_vcs([_words(*SOURCE_TOKENS)]))
    assert with_twin.compiled_twin == {
        "present": True,
        "asset": TWIN_ASSET,
        "sourcePath": VCS_PATH,
    }
    assert with_twin.dependencies == [
        {
            "role": "compiled-twin",
            "asset": TWIN_ASSET,
            "sourcePath": VCS_PATH,
            "resolved": True,
        }
    ]


# ------------------------------------------------------------------------------ compiled bytes


def test_a_compiled_bundle_claims_header_table_and_streams_exactly_once():
    closure, model = _program_model(_vcs([_words(*SOURCE_TOKENS), _words(PS_1_1, TEX, DEST_T0, END)]))
    row = model.byte_ledger[0]
    assert row["coveragePercent"] == 100.0
    assert row["accountedBytes"] == row["byteLength"] == len(closure.vcs.data)
    owners = [span["owner"] for span in row["ranges"]]
    assert owners[:2] == ["vcs.header", "vcs.comboTable"]
    assert owners[2:] == ["vcs.combos[0].tokens", "vcs.combos[1].tokens"]
    assert row["ranges"][0] == {"offset": 0, "length": 20, "state": "mapped", "owner": "vcs.header"}
    assert set(row["stateBytes"]) == {"mapped"}


def test_zero_fill_between_combos_is_padding_and_non_zero_fill_is_a_proven_omission():
    _, padded = _program_model(
        _vcs([_words(PS_1_1, TEX, DEST_T0, END), _words(PS_1_1, TEX, DEST_T0, END)],
             gap=b"\0\0\0\0", trailing=b"\0\0\0\0")
    )
    padding = [span for span in _ledger_ranges(padded) if span["state"] == "padding-zero"]
    assert [span["length"] for span in padding] == [4, 4]
    assert [row["role"] for row in padded.omissions] == ["absent-readable-source"]
    _, filled = _program_model(
        _vcs([_words(PS_1_1, TEX, DEST_T0, END), _words(PS_1_1, TEX, DEST_T0, END)],
             gap=b"junk", trailing=b"tail")
    )
    omitted = [span for span in _ledger_ranges(filled) if span["state"] == "omitted-proven"]
    assert [span["owner"] for span in omitted] == ["vcs.interComboFill", "vcs.trailingFill"]
    assert [row["role"] for row in filled.omissions] == [
        "inter-combo-fill", "trailing-fill", "absent-readable-source",
    ]
    assert filled.omissions[0]["sha256"] == hashlib.sha256(b"junk").hexdigest()


def test_a_padding_claim_is_never_made_over_a_non_zero_byte():
    _, filled = _program_model(
        _vcs([_words(PS_1_1, TEX, DEST_T0, END)], trailing=b"\0\0\1\0")
    )
    states = {span["owner"]: span["state"] for span in _ledger_ranges(filled)}
    assert states["vcs.trailingFill"] == "omitted-proven"


def test_the_three_header_words_of_unverified_meaning_are_carried_as_typed_unidentified():
    _, model = _program_model(_vcs([_words(PS_1_1, TEX, DEST_T0, END)], dynamic=3, flags=52,
                                   centroid=7))
    rows = {row["field"]: row for row in model.typed_unidentified}
    assert set(rows) == {"header.dynamicCombos", "header.flags", "header.centroidMask"}
    assert (rows["header.dynamicCombos"]["sourceOffset"], rows["header.dynamicCombos"]["value"]) == (8, 3)
    assert (rows["header.flags"]["sourceOffset"], rows["header.flags"]["value"]) == (12, 52)
    assert (rows["header.centroidMask"]["sourceOffset"], rows["header.centroidMask"]["value"]) == (16, 7)
    assert model.unresolved == [] and model.unsupported == []


def test_absent_combo_entries_collapse_into_runs_and_are_recorded_as_an_omission():
    stream = _words(PS_1_1, TEX, DEST_T0, END)
    compiled = _vcs([stream], total=4, entries=[(-1, 0), (-1, 0), (52, len(stream)), (0, 0)])
    _, model = _program_model(compiled)
    assert model.combo_table["presentEntries"] == 1
    assert model.combo_table["absentEntries"] == 3
    assert model.combo_table["absentRuns"] == [
        {"start": 0, "count": 2, "sourceOffset": 20, "offset": -1, "size": 0,
         "reason": "absent-no-stream"},
        {"start": 3, "count": 1, "sourceOffset": 44, "offset": 0, "size": 0,
         "reason": "absent-no-stream"},
    ]
    assert [combo["index"] for combo in model.combos] == [2]
    assert [row["role"] for row in model.omissions] == [
        "absent-combo-entries", "absent-readable-source",
    ]
    assert model.omissions[0]["reason"] == "absent-no-stream"
    assert model.byte_ledger[0]["coveragePercent"] == 100.0


def test_every_combo_reassembles_to_the_exact_bytes_of_its_stream():
    stream = _words(*SOURCE_TOKENS)
    closure, model = _program_model(_vcs([stream]))
    combo = model.combos[0]
    assert tokens_module.reassemble(combo["tokens"]) == stream
    assert combo["sha256"] == hashlib.sha256(stream).hexdigest()
    assert combo["model"] == "ps_1_1"
    assert [row["opcode"] for row in combo["instructions"]] == ["def", "tex", "mul", "mov", "mov"]
    assert combo["instructions"][3]["coIssued"] is True
    assert combo["tokens"][1]["sourceOffset"] == combo["offset"] + 4


def test_a_constant_table_is_decoded_into_sections_that_re_encode_byte_for_byte():
    payload = _ctab_payload()
    _, model = _program_model(_vcs([_ctab_stream()]))
    table = model.combos[0]["constantTable"]
    assert table["creator"] == "unit test"
    assert table["target"] == "ps_2_0"
    assert table["unaccountedBytes"] == 0
    assert [row["name"] for row in table["constants"]] == ["g_Colour"]
    assert table["constants"][0]["registerSet"] == "float4"
    assert table["constants"][0]["registerIndex"] == 3
    assert ctab_module.encode(table) == payload
    assert {section["kind"] for section in table["sections"]} == {
        "four-cc", "header", "constant-info", "type-info", "string", "fill"
    }
    owners = [span["owner"] for span in _ledger_ranges(model)]
    assert "vcs.combos[0].constantTable" in owners
    assert owners.count("vcs.combos[0].tokens") == 2


def test_a_constant_table_with_struct_members_follows_and_accounts_for_its_member_table():
    payload = _ctab_struct_payload()
    _, model = _program_model(_vcs([_ctab_stream(payload)]))
    table = model.combos[0]["constantTable"]
    assert table["unaccountedBytes"] == 0
    assert table["constants"][0]["class"] == "struct"
    assert [member["name"] for member in table["constants"][0]["members"]] == ["colour"]
    assert table["constants"][0]["members"][0]["type"] == "float"
    assert any(section["kind"] == "struct-member-info" for section in table["sections"])
    assert ctab_module.encode(table) == payload


def test_a_record_read_from_an_offset_keeps_the_offset_it_was_read_from():
    _, source = _source_model()
    assert [row.source_offset for row in source.instructions] == [
        source.lines[row.line].offset + len(source.lines[row.line].code)
        - len(source.lines[row.line].code.lstrip())
        for row in source.instructions
    ]
    _, program = _program_model(_vcs([_words(*SOURCE_TOKENS)]))
    combo = program.combos[0]
    assert program.header["sourceOffset"] == 0
    assert program.combo_table["sourceOffset"] == 20
    assert combo["sourceOffset"] == 20        # the table entry the combo was read from
    assert [row["sourceOffset"] for row in combo["tokens"][:3]] == [
        combo["offset"], combo["offset"] + 4, combo["offset"] + 8
    ]
    assert combo["instructions"][0]["sourceOffset"] == combo["tokens"][1]["sourceOffset"]


def test_a_combo_entry_that_leaves_the_member_is_an_anomaly_that_claims_no_bytes():
    stream = _words(PS_1_1, TEX, DEST_T0, END)
    compiled = _vcs([stream], total=2, entries=[(36, len(stream)), (36, 4096)])
    _, model = _program_model(compiled)
    assert [row["role"] for row in model.anomalies] == ["combo-out-of-bounds"]
    assert [combo["index"] for combo in model.combos] == [0]
    assert model.byte_ledger[0]["coveragePercent"] == 100.0


def test_a_stream_that_does_not_close_with_the_end_token_is_recorded_and_still_accounted():
    stream = _words(PS_1_1, TEX, DEST_T0, MOV)
    _, model = _program_model(_vcs([stream]))
    roles = [row["role"] for row in model.anomalies]
    assert "stream-without-end-token" in roles
    assert "stream-not-tokenizable" in roles
    assert [row["role"] for row in model.omissions] == [
        "undecoded-combo-stream", "absent-readable-source",
    ]
    assert model.byte_ledger[0]["coveragePercent"] == 100.0
    assert model.unresolved == []


def test_two_entries_that_share_one_stream_claim_its_bytes_once():
    stream = _words(PS_1_1, TEX, DEST_T0, END)
    compiled = _vcs([stream], total=2, entries=[(36, len(stream)), (36, len(stream))])
    _, model = _program_model(compiled)
    assert model.combos[1]["aliasOf"] == 0
    assert "tokens" not in model.combos[1]
    assert model.byte_ledger[0]["coveragePercent"] == 100.0


def test_an_entry_that_starts_inside_another_stream_claims_no_bytes_of_its_own(tmp_path):
    stream = _words(PS_1_1, TEX, DEST_T0, END)
    compiled = _vcs([stream], total=2, entries=[(36, len(stream)), (40, len(stream) - 4)])
    index, reader = _index(**{VCS_PATH: compiled})
    _, model = _program_model(compiled)
    assert [row["role"] for row in model.anomalies] == ["combo-table-overlap"]
    assert model.combos[1]["overlapping"] is True
    assert "tokens" not in model.combos[1]
    assert model.byte_ledger[0]["coveragePercent"] == 100.0
    summary = validation.validate(
        exporter.export(index, "psh/" + PSH_KEY, tmp_path, read_bytes=reader)
    )
    assert summary["presentCombos"] == 2
    assert any("combo-table-overlap" in warning for warning in validation.warnings_for(summary))


def test_a_bundle_too_short_for_its_header_is_refused():
    with pytest.raises(ShaderProgramDecodeError):
        _program_model(b"\0" * 12)


# ------------------------------------------------------------------------- the source join


def test_a_same_stem_readable_source_produces_one_shader_source_dependency_row():
    _, model = _program_model(_vcs([_words(*SOURCE_TOKENS)]), source=SOURCE_TEXT)
    assert len(model.dependencies) == 1
    row = model.dependencies[0]
    assert row["role"] == "shader-source"
    assert row["asset"] == "vtmb:shader-source:unlitgeneric"
    assert row["sourcePath"] == PSH_PATH
    assert row["resolved"] is True
    assert row["byteLength"] == len(SOURCE_TEXT.encode("ascii"))
    assert row["sha256"] == hashlib.sha256(SOURCE_TEXT.encode("ascii")).hexdigest()
    assert model.source_comparison["state"] == "equivalent"
    assert model.source_comparison["combosMatched"] == [0]


def test_a_source_that_differs_from_the_compiled_combo_is_recorded_as_drift_not_corrected():
    drifted = list(SOURCE_TOKENS)
    drifted[drifted.index(DEST_R0_RGB)] = DEST_R0_RGB | (1 << 20)      # the binary saturates
    _, model = _program_model(_vcs([_words(*drifted)]), source=SOURCE_TEXT)
    comparison = model.source_comparison
    assert comparison["state"] == "source-binary-drift"
    assert comparison["combosMatched"] == []
    assert comparison["combosDiffering"][0]["combo"] == 0
    assert comparison["combosDiffering"][0]["assembledValue"] == DEST_R0_RGB
    assert comparison["combosDiffering"][0]["comboValue"] == DEST_R0_RGB | (1 << 20)
    assert [row["role"] for row in model.anomalies] == ["source-binary-drift"]


def test_a_program_outside_psh_has_no_readable_twin_and_no_source_comparison():
    stream = _words(0xFFFE0101, MOV, 0x80000000 | (0 << 28) | (0xF << 16),
                    0x80000000 | (1 << 28) | (0xE4 << 16), END)
    _, model = _program_model(_vcs([stream]), key="vsh/" + PSH_KEY, source=SOURCE_TEXT)
    assert model.dependencies == []
    assert model.source_comparison is None
    assert model.readable_source["present"] is True          # the install does hold the stem
    assert model.readable_source["comparable"] is False
    assert model.readable_source["asset"] is None
    assert model.readable_source["sourcePath"] == PSH_PATH
    assert "pixel-shader assembly" in model.readable_source["reason"]
    assert model.omissions == []
    assert model.combos[0]["model"] == "vs_1_1"


# ---------------------------------------------------------------------------------- validation


def test_the_validator_rejects_a_tampered_ledger_row():
    closure, model = _program_model(_vcs([_words(*SOURCE_TOKENS)]))
    document, binary = exporter.build_document(model)
    root = document["extensions"]["ELYSIUM_vtmb_shader_program"]
    root["coverage"]["byteLedger"][0]["ranges"][0]["length"] += 4
    with pytest.raises(validation.ShaderProgramGlbValidationError):
        validation.validate_document(document, binary, source_members=closure.members())


def test_the_validator_rejects_a_padding_claim_over_a_non_zero_byte():
    closure, model = _program_model(_vcs([_words(*SOURCE_TOKENS)]))
    document, binary = exporter.build_document(model)
    root = document["extensions"]["ELYSIUM_vtmb_shader_program"]
    root["coverage"]["byteLedger"][0]["ranges"][0]["state"] = "padding-zero"
    with pytest.raises(validation.ShaderProgramGlbValidationError):
        validation.validate_document(document, binary, source_members=closure.members())


def test_the_validator_rejects_a_combo_whose_tokens_disown_its_digest():
    closure, model = _program_model(_vcs([_words(*SOURCE_TOKENS)]))
    document, binary = exporter.build_document(model)
    root = document["extensions"]["ELYSIUM_vtmb_shader_program"]
    root["combos"][0]["tokens"][2]["value"] ^= 0xF
    with pytest.raises(validation.ShaderProgramGlbValidationError):
        validation.validate_document(document, binary, source_members=closure.members())


def test_the_validator_rejects_a_unit_that_declares_a_scene_or_a_bin_chunk():
    _, model = _source_model()
    document, binary = exporter.build_source_document(model)
    with pytest.raises(validation.ShaderProgramGlbValidationError):
        validation.validate_document(dict(document, scenes=[{"nodes": []}]), binary)
    with pytest.raises(validation.ShaderProgramGlbValidationError):
        validation.validate_document(document, b"\0\0\0\0")


def test_the_validator_rejects_a_drift_row_the_anomalies_do_not_carry():
    drifted = list(SOURCE_TOKENS)
    drifted[drifted.index(DEST_R0_RGB)] = DEST_R0_RGB | (1 << 20)
    closure, model = _program_model(_vcs([_words(*drifted)]), source=SOURCE_TEXT)
    document, binary = exporter.build_document(model)
    root = document["extensions"]["ELYSIUM_vtmb_shader_program"]
    root["anomalies"] = []
    with pytest.raises(validation.ShaderProgramGlbValidationError):
        validation.validate_document(document, binary, source_members=closure.members())


def test_a_published_source_unit_round_trips_through_validation(tmp_path):
    index, reader = _install(SOURCE_TEXT, _vcs([_words(*SOURCE_TOKENS)]))
    destination = exporter.export_shader_source(index, PSH_KEY, tmp_path, read_bytes=reader)
    assert destination == tmp_path / "source" / f"{PSH_KEY}.glb"
    summary = validation.validate(destination)
    assert summary["kind"] == "shader-source"
    assert summary["asset"] == "vtmb:shader-source:unlitgeneric"
    assert summary["byteCoveragePercent"] == 100.0
    assert summary["accountedBytes"] == summary["sourceBytes"]
    assert summary["shaderModel"] == "ps_1_1"
    assert validation.warnings_for(summary) == []


def test_a_program_the_corpus_index_back_filled_still_validates(tmp_path):
    """`selectedBy[]` is empty at export and written in place by the corpus index, which reads
    the material units' `shaderResolution.programs`; validation reads a published program in
    both states and in no other."""

    from elysium_pipeline.formats.corpus_index_glb import backfill
    from elysium_pipeline.formats.unit_contract import read_glb

    index, reader = _install(SOURCE_TEXT, _vcs([_words(*SOURCE_TOKENS), _ctab_stream()]))
    destination = exporter.export(index, "psh/" + PSH_KEY, tmp_path, read_bytes=reader)
    asset = "vtmb:shader-program:psh/" + PSH_KEY
    document, _ = read_glb(destination)
    assert document["extensions"]["ELYSIUM_vtmb_shader_program"]["selectedBy"] == []

    rows = [{"from": "vtmb:material:wall", "role": "pixelShader"}]
    assert backfill.rewrite(destination, asset, rows) is not None
    validation.validate(destination)
    document, _ = read_glb(destination)
    assert document["extensions"]["ELYSIUM_vtmb_shader_program"]["selectedBy"] == rows

    backfill.rewrite(destination, asset, ["vtmb:material:wall"])
    with pytest.raises(validation.ShaderProgramGlbValidationError):
        validation.validate(destination)


def test_a_published_program_unit_round_trips_through_validation(tmp_path):
    index, reader = _install(SOURCE_TEXT, _vcs([_words(*SOURCE_TOKENS), _ctab_stream()]))
    destination = exporter.export(index, "psh/" + PSH_KEY, tmp_path, read_bytes=reader)
    assert destination == tmp_path / "psh" / f"{PSH_KEY}.glb"
    summary = validation.validate(destination)
    assert summary["kind"] == "shader-program"
    assert summary["asset"] == "vtmb:shader-program:psh/unlitgeneric"
    assert summary["byteCoveragePercent"] == 100.0
    assert summary["presentCombos"] == 2
    assert summary["comparisonState"] == "source-binary-drift"     # the second combo is not the source
    assert any("typed but unidentified" in warning for warning in validation.warnings_for(summary))


def test_one_source_closure_yields_one_byte_identical_product(tmp_path):
    index, reader = _install(SOURCE_TEXT, _vcs([_words(*SOURCE_TOKENS)]))
    first = exporter.export(index, "psh/" + PSH_KEY, tmp_path / "a", read_bytes=reader)
    second = exporter.export(index, "psh/" + PSH_KEY, tmp_path / "b", read_bytes=reader)
    assert first.read_bytes() == second.read_bytes()


def test_the_corpus_commands_enumerate_only_this_seam_s_members():
    index, _ = _index(
        **{
            PSH_PATH: b"ps.1.1\r\n",
            VCS_PATH: b"",
            "shaders/vsh/other.vcs": b"",
            "shaders/fxc/third_ps20.vcs": b"",
            "shaders/psh/nested/deep.vcs": b"",
            "materials/dxshaders/sub/deep.psh": b"",
            "materials/models/unrelated.vmt": b"",
        }
    )
    assert exporter.shader_source_source_keys(index) == [PSH_KEY]
    assert exporter.source_keys(index) == [
        "fxc/third_ps20", "psh/" + PSH_KEY, "vsh/other"
    ]


# ------------------------------------------------------------ model-dependent token shapes


def _ps_1_4_stream() -> bytes:
    """`texcoord` and `tex` are respelled with a source of their own from ps.1.4 on."""

    return _words(
        PS_1_4,
        64, _dest(0, 0), _src(3, 0),                       # texcrd r0, t0
        PHASE,
        66, _dest(0, 1), _src(3, 1),                       # texld r1, t1
        END,
    )


def _ps_2_0_stream() -> bytes:
    """A declaration prologue and the three-operand `texld` shader model 2 spells."""

    return _words(
        PS_2_0,
        _op(31, 2), 0x80000000 | (2 << 27), _dest(10, 0),  # dcl_2d s0
        _op(66, 3), _dest(0, 0), _src(3, 0), _src(10, 0),  # texld r0, t0, s0
        _op(1, 2), _dest(0, 1) | (1 << 20), _src(0, 0),    # mov_sat r1, r0
        END,
    )


def _vs_2_0_stream() -> bytes:
    """Integer and boolean declarations, and a source that indexes through `a0`."""

    return _words(
        VS_2_0,
        _op(48, 5), _dest(7, 0), 1, 2, 3, 4,               # defi i0, 1, 2, 3, 4
        _op(47, 2), _dest(14, 0), 1,                       # defb b0, true
        _op(9, 4), _dest(0, 0, 0x1), _src(1, 0),           # dp4 r0.x, v0, c48[a0]
        _src(2, 48) | 0x2000, _src(3, 0, 0xFF),
        END,
    )


def test_a_ps_1_4_stream_tokenizes_the_texture_opcodes_that_model_respelled():
    _, model = _program_model(_vcs([_ps_1_4_stream()]), key="fxc/water_ps14")
    combo = model.combos[0]
    assert model.anomalies == []
    assert combo["model"] == "ps_1_4"
    assert [row["opcode"] for row in combo["instructions"]] == ["texcrd", "phase", "texld"]
    assert [len(row["sources"]) for row in combo["instructions"]] == [1, 0, 1]
    assert combo["instructions"][0]["text"] == "texcrd r0, t0"
    assert tokens_module.reassemble(combo["tokens"]) == _ps_1_4_stream()


def test_a_ps_2_0_stream_tokenizes_a_sampler_declaration_and_a_three_operand_texld():
    _, model = _program_model(_vcs([_ps_2_0_stream()]), key="fxc/lit_ps20")
    combo = model.combos[0]
    assert model.anomalies == []
    assert combo["model"] == "ps_2_0"
    assert [row["opcode"] for row in combo["instructions"]] == ["dcl", "texld", "mov"]
    assert [len(row["sources"]) for row in combo["instructions"]] == [0, 2, 1]
    assert combo["instructions"][0]["declaration"]["samplerType"] is not None
    assert combo["instructions"][1]["text"] == "texld r0, t0, s0"
    assert combo["instructions"][2]["modifiers"] == ["_sat"]
    assert tokens_module.reassemble(combo["tokens"]) == _ps_2_0_stream()


def test_a_relative_address_token_belongs_to_the_source_it_indexes_and_is_no_operand_of_its_own():
    _, model = _program_model(_vcs([_vs_2_0_stream()]), key="vsh/indexed")
    combo = model.combos[0]
    assert model.anomalies == []
    assert combo["model"] == "vs_2_0"
    assert [row["opcode"] for row in combo["instructions"]] == ["defi", "defb", "dp4"]
    assert [row["literals"] for row in combo["instructions"][:2]] == [["1", "2", "3", "4"], ["true"]]
    indexed = combo["instructions"][2]
    assert indexed["text"] == "dp4 r0.x, v0, c48[a0.w]"
    assert len(indexed["sources"]) == 2
    assert indexed["sources"][0]["relativeAddress"] is False
    assert "addressRegister" not in indexed["sources"][0]
    assert indexed["sources"][1]["relativeAddress"] is True
    assert indexed["sources"][1]["addressRegister"]["registerClass"] == "address"
    assert indexed["sources"][1]["addressRegister"]["text"] == "a0.w"
    assert tokens_module.reassemble(combo["tokens"]) == _vs_2_0_stream()


def test_an_entry_that_leaves_the_member_is_absent_for_a_reason_of_its_own():
    stream = _words(PS_1_1, TEX, DEST_T0, END)
    compiled = _vcs([stream], total=3, entries=[(44, len(stream)), (-1, 0), (9000, 8)])
    _, model = _program_model(compiled)
    assert [row["role"] for row in model.anomalies] == ["combo-out-of-bounds"]
    assert [(run["start"], run["sourceOffset"], run["reason"])
            for run in model.combo_table["absentRuns"]] == [
        (1, 28, "absent-no-stream"),
        (2, 36, "absent-out-of-bounds"),
    ]
    rows = [row for row in model.omissions if row["role"] == "absent-combo-entries"]
    assert [(row["reason"], row["entries"], row["sourceOffset"]) for row in rows] == [
        ("absent-no-stream", 1, 28),
        ("absent-out-of-bounds", 1, 36),
    ]
    assert "outside the member" in rows[1]["evidence"]
    assert model.byte_ledger[0]["coveragePercent"] == 100.0


def test_a_header_that_states_a_negative_combo_count_publishes_with_the_anomaly(tmp_path):
    index, reader = _index(**{VCS_PATH: _vcs([], total=-3, entries=[])})
    closure = load_source_closure(index, "psh/" + PSH_KEY, read_bytes=reader)
    model = decode_shader_program(closure)
    assert [row["role"] for row in model.anomalies] == ["negative-combo-count"]
    assert model.combo_table["entries"] == 0
    assert model.combo_table["declaredEntries"] == 0
    assert model.byte_ledger[0]["coveragePercent"] == 100.0
    destination = exporter.export(index, "psh/" + PSH_KEY, tmp_path, read_bytes=reader)
    summary = validation.validate(destination)
    assert summary["totalCombos"] == -3
    assert summary["presentCombos"] == 0
    assert summary["byteCoveragePercent"] == 100.0
    assert any("negative-combo-count" in warning
               for warning in validation.warnings_for(summary))


def test_the_validator_rejects_an_instruction_that_states_an_operand_its_tokens_do_not_carry():
    _, model = _program_model(_vcs([_vs_2_0_stream()]), key="vsh/indexed")
    document, binary = exporter.build_document(model)
    root = document["extensions"]["ELYSIUM_vtmb_shader_program"]
    indexed = root["combos"][0]["instructions"][2]
    indexed["sources"].append(indexed["sources"][1]["addressRegister"])
    with pytest.raises(validation.ShaderProgramGlbValidationError):
        validation.validate_document(document, binary)


def test_the_validator_rejects_a_source_that_names_a_compiled_twin_it_does_not_declare():
    _, model = _source_model(compiled=_vcs([_words(*SOURCE_TOKENS)]))
    document, binary = exporter.build_source_document(model)
    root = document["extensions"]["ELYSIUM_vtmb_shader_source"]
    assert [row["role"] for row in root["dependencies"]] == ["compiled-twin"]
    root["dependencies"] = []
    with pytest.raises(validation.ShaderProgramGlbValidationError):
        validation.validate_document(document, binary)


# ------------------------------------------------------ empty members and unresolved references


def test_a_source_the_install_holds_as_zero_bytes_publishes_instead_of_refusing(tmp_path):
    index, reader = _install(source="", compiled=_vcs([_words(*SOURCE_TOKENS)]))
    closure = load_shader_source_closure(index, PSH_KEY, read_bytes=reader)
    model = decode_shader_source(closure)
    assert model.version is None
    assert model.shader_model is None
    assert model.lines == []
    assert [row["role"] for row in model.omissions] == ["empty-member"]
    assert model.byte_ledger[0]["byteLength"] == 0
    assert model.byte_ledger[0]["coveragePercent"] == 100.0
    assert model.byte_ledger[0]["ranges"] == []
    destination = exporter.export_shader_source(index, PSH_KEY, tmp_path, read_bytes=reader)
    summary = validation.validate(destination)
    assert summary["shaderModel"] is None
    assert summary["sourceBytes"] == 0
    assert summary["byteCoveragePercent"] == 100.0
    assert any("empty-member" in warning for warning in validation.warnings_for(summary))


def test_a_bundle_the_install_holds_as_zero_bytes_publishes_instead_of_refusing(tmp_path):
    index, reader = _index(**{VCS_PATH: b"", PSH_PATH: SOURCE_TEXT.encode("ascii")})
    closure = load_source_closure(index, "psh/" + PSH_KEY, read_bytes=reader)
    model = decode_shader_program(closure)
    assert model.header is None
    assert model.combo_table is None
    assert model.combos == []
    assert model.source_comparison is None
    assert model.typed_unidentified == []
    assert [row["role"] for row in model.omissions] == ["empty-member"]
    assert model.byte_ledger[0]["byteLength"] == 0
    destination = exporter.export(index, "psh/" + PSH_KEY, tmp_path, read_bytes=reader)
    summary = validation.validate(destination)
    assert summary["totalCombos"] is None
    assert summary["presentCombos"] == 0
    assert summary["byteCoveragePercent"] == 100.0
    assert any("empty-member" in warning for warning in validation.warnings_for(summary))


def test_a_bundle_that_holds_bytes_but_no_header_is_still_refused():
    index, reader = _index(**{VCS_PATH: b"\0\0\0\0"})
    closure = load_source_closure(index, "psh/" + PSH_KEY, read_bytes=reader)
    with pytest.raises(ShaderProgramDecodeError):
        decode_shader_program(closure)


def test_an_unresolved_compiled_twin_keeps_the_missing_sentinel_and_an_omission(tmp_path):
    index, reader = _install(SOURCE_TEXT, compiled=None)
    closure = load_shader_source_closure(index, PSH_KEY, read_bytes=reader)
    model = decode_shader_source(closure)
    sentinel = f"vtmb:missing-shader-program:psh/{PSH_KEY}"
    assert model.compiled_twin["asset"] == sentinel
    assert model.dependencies == []
    assert [(row["role"], row["asset"]) for row in model.omissions] == [
        ("absent-compiled-twin", sentinel)
    ]
    destination = exporter.export_shader_source(index, PSH_KEY, tmp_path, read_bytes=reader)
    summary = validation.validate(destination)
    assert summary["dependencies"] == 0
    assert "omitted: the install resolves no compiled bundle of this stem" in (
        validation.warnings_for(summary)
    )


def test_an_unresolved_readable_source_keeps_the_missing_sentinel_and_an_omission(tmp_path):
    index, reader = _index(**{VCS_PATH: _vcs([_words(*SOURCE_TOKENS)])})
    closure = load_source_closure(index, "psh/" + PSH_KEY, read_bytes=reader)
    model = decode_shader_program(closure)
    sentinel = f"vtmb:missing-shader-source:{PSH_KEY}"
    assert model.readable_source == {
        "present": False,
        "comparable": True,
        "sourcePath": PSH_PATH,
        "asset": sentinel,
        "reason": "the install resolves no readable source of this stem",
    }
    assert model.dependencies == []
    assert [(row["role"], row["asset"]) for row in model.omissions] == [
        ("absent-readable-source", sentinel)
    ]
    destination = exporter.export(index, "psh/" + PSH_KEY, tmp_path, read_bytes=reader)
    assert validation.validate(destination)["dependencies"] == 0


def test_the_validator_rejects_a_unit_that_calls_a_member_it_accounted_for_empty():
    closure, model = _program_model(_vcs([_words(*SOURCE_TOKENS)]))
    document, binary = exporter.build_document(model)
    root = document["extensions"]["ELYSIUM_vtmb_shader_program"]
    root["omissions"].append({"role": "empty-member", "sourcePath": VCS_PATH, "byteLength": 0})
    with pytest.raises(validation.ShaderProgramGlbValidationError):
        validation.validate_document(document, binary, source_members=closure.members())


def test_the_validator_rejects_an_unresolved_reference_that_names_no_sentinel():
    _, model = _source_model(compiled=None)
    document, binary = exporter.build_source_document(model)
    root = document["extensions"]["ELYSIUM_vtmb_shader_source"]
    root["identity"]["compiledTwin"]["asset"] = None
    with pytest.raises(validation.ShaderProgramGlbValidationError):
        validation.validate_document(document, binary)


def test_the_validator_rejects_a_bundle_outside_psh_that_names_a_readable_source():
    stream = _words(0xFFFE0101, MOV, _dest(0, 0), _src(1, 0), END)
    _, model = _program_model(_vcs([stream]), key="vsh/" + PSH_KEY, source=SOURCE_TEXT)
    document, binary = exporter.build_document(model)
    root = document["extensions"]["ELYSIUM_vtmb_shader_program"]
    root["identity"]["readableSource"]["asset"] = f"vtmb:shader-source:{PSH_KEY}"
    with pytest.raises(validation.ShaderProgramGlbValidationError):
        validation.validate_document(document, binary)


def test_a_predicated_instruction_publishes_its_predicate_and_spells_it_in_the_text():
    stream = _words(
        VS_2_0,
        _op(1, 3) | 0x10000000, _reg(19, 0) | (0xE4 << 16),      # (p0) mov r0, r1
        _dest(0, 0), _src(0, 1),
        END,
    )
    _, model = _program_model(_vcs([stream]), key="vsh/predicated")
    combo = model.combos[0]
    assert model.anomalies == []
    instruction = combo["instructions"][0]
    assert instruction["predicated"] is True
    assert instruction["predicate"]["registerClass"] == "predicate"
    assert instruction["text"].startswith("(p0) mov ")
    assert len(instruction["sources"]) == 1
    assert tokens_module.reassemble(combo["tokens"]) == stream
