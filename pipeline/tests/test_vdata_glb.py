"""Synthetic contract tests for the isolated vdata GLB exporter.

Every fixture below is hand-built bytes plus a fake install index and a `read_bytes` lambda that
serves them -- never the real VtMB install, per the seam's own test-authoring rule.
"""

from __future__ import annotations

from pathlib import PurePosixPath

import pytest

from elysium_pipeline.exporters import vdata_glb as exporter
from elysium_pipeline.formats.unit_contract import ByteLedgerError, UnitValidationError, verify_ledger_row
from elysium_pipeline.formats.vdata_glb import lexer, model as model_module, projection as projection_module
from elysium_pipeline.formats.vdata_glb.decode import VdataDecodeError, decode_vdata
from elysium_pipeline.formats.vdata_glb.source import VdataSourceError, load_source_closure, source_keys
from elysium_pipeline.validation import vdata_glb as validation


def _index(*keys: str) -> dict:
    return {key: ("loose", f"C:/game/Vampire/{key}") for key in keys}


def _closure(key: str, body: bytes, *, index_key: str | None = None):
    path = index_key or f"vdata/{key}.txt"
    index = _index(path)
    return load_source_closure(index, key, read_bytes=lambda idx, k: body if k == path else None)


def _decode(key: str, body: bytes, **kwargs):
    return decode_vdata(_closure(key, body), **kwargs)


# --- tokenizer / tree parser -------------------------------------------------------------------


def test_the_tokenizer_claims_every_byte_of_a_keyvalues_file_exactly_once():
    body = b'Foo\r\n{\r\n\t"a"\t"1" // trailing\r\n}\r\n'
    text = lexer.decode_text(body)
    tokens = lexer.tokenize(text)
    covered = sum(token.length for token in tokens)
    assert covered == len(body)
    cursor = 0
    for token in tokens:
        assert token.offset == cursor
        cursor += token.length
    assert cursor == len(body)


def test_a_scalar_pair_and_a_nested_block_decode_with_offsets():
    body = b'WeaponData\n{\n\t"printname" "Katana"\n\tActivation\n\t{\n\t\t"Tag" "Primary"\n\t}\n}\n'
    model = _decode("items/synthetic", body)
    assert model.grammar == "keyvalues"
    assert model.root_key == "WeaponData"
    root = model.tree["children"][0]
    assert root["kind"] == "block" and root["key"] == "weapondata"
    printname, activation = root["children"]
    assert printname == {
        "index": 0, "kind": "scalar", "key": "printname", "sourceKey": "printname",
        "quotedKey": True, "value": "Katana", "quotedValue": True, "escapes": [],
        "offset": printname["offset"], "length": printname["length"],
    }
    assert activation["kind"] == "block" and activation["key"] == "activation"
    tag = activation["children"][0]
    assert tag["key"] == "tag" and tag["value"] == "Primary"
    # Every claimed range reconstructs the source exactly.
    verify_ledger_row(model.ledger_row, model.member.data)
    assert model.ledger_row["coveragePercent"] == 100.0


def test_a_repeated_scalar_key_is_flagged_and_resolves_last_wins():
    body = b'Foo\n{\n\t"a" "1"\n\t"a" "2"\n}\n'
    model = _decode("system/synthetic", body)
    root = model.tree["children"][0]
    assert [child["value"] for child in root["children"]] == ["1", "2"]
    roles = [row["role"] for row in model.anomalies]
    assert "repeated-scalar-key" in roles
    reshaped = model.projection["sections"]
    assert reshaped["a"] == ["1", "2"]  # the tree keeps both; the open reshape lists them


def test_a_valueless_key_is_recorded_as_present_and_empty():
    body = b'Foo\n{\n\t"a"\n}\n'
    model = _decode("system/synthetic", body)
    root = model.tree["children"][0]
    pair = root["children"][0]
    assert pair["value"] == "" and pair["quotedValue"] is False
    assert any(row["role"] == "valueless-key" for row in model.anomalies)


def test_an_orphan_open_brace_degrades_to_an_unparsed_tail():
    body = b'Foo\n{\n\t"a" "1"\n\t{\n\t\t"orphan" "1"\n\t}\n}\n'
    model = _decode("system/synthetic", body)
    assert model.tree["unparsedOffset"] is not None
    assert any(row["role"] == "unbalanced-braces" for row in model.anomalies)
    verify_ledger_row(model.ledger_row, model.member.data)


def test_an_unclosed_block_is_flagged_unbalanced_braces():
    body = b'Foo\n{\n\tActivation\n\t{\n\t\t"Tag" "Primary"\n'
    model = _decode("system/synthetic", body)
    assert model.tree["unparsedOffset"] is not None
    assert any(row["role"] == "unbalanced-braces" for row in model.anomalies)
    verify_ledger_row(model.ledger_row, model.member.data)


def test_a_stray_top_level_close_is_skipped_and_claimed():
    body = b'}\nFoo\n{\n\t"a" "1"\n}\n'
    model = _decode("system/synthetic", body)
    assert model.tree["unparsedOffset"] is None
    assert model.root_key == "Foo"
    assert any(row["role"] == "unbalanced-braces" for row in model.anomalies)
    verify_ledger_row(model.ledger_row, model.member.data)


def test_a_bare_continuation_token_extends_the_preceding_unquoted_value():
    body = b'Foo\n{\n\tkey unquotedvalue extra\n}\n'
    model = _decode("system/synthetic", body)
    root = model.tree["children"][0]
    pair = root["children"][0]
    assert pair["value"] == "unquotedvalue extra"
    assert any(row["role"] == "unquoted-token-with-space" for row in model.anomalies)
    verify_ledger_row(model.ledger_row, model.member.data)


def test_a_quoted_value_escape_round_trips_through_raw_and_decoded():
    body = b'Foo\n{\n\t"a" "she said \\"hi\\""\n}\n'
    model = _decode("system/synthetic", body)
    pair = model.tree["children"][0]["children"][0]
    assert pair["value"] == 'she said \\"hi\\"'          # as spelled, escapes intact
    assert lexer.decode_escapes(pair["value"], pair["escapes"]) == 'she said "hi"'
    verify_ledger_row(model.ledger_row, model.member.data)


def test_a_directive_key_is_carried_as_typed_unidentified():
    body = b'#include "shared.txt"\nFoo\n{\n\t"a" "1"\n}\n'
    model = _decode("system/synthetic", body)
    directive = model.tree["children"][0]
    assert directive == {
        "index": 0, "kind": "directive", "name": "include", "sourceKey": "#include",
        "argument": "shared.txt", "offset": 0, "length": directive["length"],
    }
    assert any(row.get("reason") == "directive" for row in model.typed_unidentified)
    verify_ledger_row(model.ledger_row, model.member.data)


def test_non_ascii_bytes_are_recorded_with_their_offsets():
    body = 'Foo\n{\n\t"a" "h\u00e9ng"\n}\n'.encode("latin-1")
    model = _decode("system/synthetic", body)
    rows = [row for row in model.anomalies if row["role"] == "non-ascii-byte"]
    assert rows and all(body[row["offset"]] > 0x7F for row in rows)


def test_non_ascii_bytes_are_recorded_for_the_freeform_grammar_too():
    body = "plain prose with h\u00e9ng and no braces\n".encode("latin-1")
    model = _decode("system/credits", body)
    assert model.grammar == "freeform"
    rows = [row for row in model.anomalies if row["role"] == "non-ascii-byte"]
    assert rows and all(body[row["offset"]] > 0x7F for row in rows)


def test_non_ascii_bytes_are_recorded_for_the_delimited_grammar_too():
    body = "Title\u00e901  | NONE | 51\r\n".encode("latin-1")
    model = _decode("system/experience_table", body)
    assert model.grammar == "delimited"
    rows = [row for row in model.anomalies if row["role"] == "non-ascii-byte"]
    assert rows and all(body[row["offset"]] > 0x7F for row in rows)


def test_a_trailing_comment_is_claimed_as_mapped_text_not_a_binary_record():
    body = b'Foo\n{\n\t"a" "1" // note\n}\n'
    model = _decode("system/synthetic", body)
    comment_ranges = [row for row in model.ledger_row["ranges"] if row["owner"].startswith("comments[")]
    assert comment_ranges and all(row["state"] == "mapped-text" for row in comment_ranges)


def test_insignificant_whitespace_is_recorded_as_an_evidence_backed_omission():
    model = _decode("system/synthetic", b'Foo\n{\n\t"a" "1"\n}\n')
    omitted = {row["role"]: row for row in model.omissions}
    assert omitted["whitespace"]["reason"] == "insignificant-keyvalues-separator-bytes"
    assert omitted["whitespace"]["byteLength"] > 0


def test_a_leading_bom_is_recorded_as_an_evidence_backed_omission():
    body = b'\xef\xbb\xbfFoo\n{\n\t"a" "1"\n}\n'
    model = _decode("system/synthetic", body)
    omitted = {row["role"]: row for row in model.omissions}
    assert omitted["bom"] == {
        "role": "bom", "reason": "utf-8-byte-order-mark-prefix", "byteLength": 3,
    }


def test_a_stalled_lexer_raises_a_decode_error_not_a_bare_lex_error(monkeypatch):
    def _stall(text):
        raise lexer.VdataLexError("boom")

    monkeypatch.setattr(lexer, "tokenize", _stall)
    with pytest.raises(VdataDecodeError):
        _decode("system/synthetic", b'Foo\n{\n\t"a" "1"\n}\n')


def test_a_trailing_ws_fix_comment_is_recorded_as_a_commented_key_anomaly():
    body = b'SignData\n{\n\t"XPos"\t""\t//1, ws-fix\n}\n'
    model = _decode("signs/synthetic", body)
    rows = [row for row in model.anomalies if row["role"] == "commented-key"]
    assert rows == [{"role": "commented-key", "path": "0.0", "value": "1", "offset": rows[0]["offset"]}]


def test_a_file_with_no_block_structure_falls_back_to_freeform_rows():
    body = b"# a header directive\nplain prose with no braces at all\n"
    model = _decode("system/credits", body)
    assert model.grammar == "freeform"
    assert model.tree is None
    assert [row["kind"] for row in model.rows] == ["line", "line"]
    assert model.rows[0]["text"] == "# a header directive"
    verify_ledger_row(model.ledger_row, model.member.data)


def test_a_file_of_only_comments_decodes_as_an_empty_keyvalues_document():
    body = b"// nothing but commentary\n// twice\n"
    model = _decode("camerashots/synthetic", body)
    assert model.grammar == "keyvalues"
    assert model.tree == {"children": [], "unparsedOffset": None}
    assert model.root_key is None
    assert model.projection["kind"] == "open"
    verify_ledger_row(model.ledger_row, model.member.data)


def test_the_delimited_grammar_classifies_every_line_kind():
    body = (
        b"> Comments must begin the line with \">\"\r\n"
        b">\r\n"
        b"Title01  | NONE | 51\r\n"
        b"\r\n"
        b"> Total Experience Value: 2\r\n"
        b"Tutor01 | Escaped Sabbat assault | 201\r\n"
    )
    model = _decode("system/experience_table", body)
    assert model.grammar == "delimited"
    kinds = [row["kind"] for row in model.rows]
    assert kinds == ["comment", "comment", "data", "skipped", "header", "data"]
    data_rows = [row for row in model.rows if row["kind"] == "data"]
    assert data_rows[0]["key"] == "Title01" and data_rows[0]["value"] == "51"
    assert data_rows[1]["description"] == "Escaped Sabbat assault"
    verify_ledger_row(model.ledger_row, model.member.data)


def test_an_empty_member_is_recorded_as_an_omitted_proven_row():
    model = _decode("system/synthetic", b"")
    assert model.omissions == [{"reason": "empty-member"}]
    assert model.ledger_row["byteLength"] == 0
    verify_ledger_row(model.ledger_row, b"")


# --- identity / key rules ----------------------------------------------------------------------


def test_the_key_and_asset_identity_rules():
    assert model_module.normalize_key("VDATA/Items/Item_W_Katana.TXT") == "items/item_w_katana"
    assert model_module.normalize_key("items/item_w_katana") == "items/item_w_katana"
    assert model_module.normalize_key(r"Items\Item_W_Katana") == "items/item_w_katana"
    assert model_module.asset_id("Items/Item_W_Katana") == "vtmb:vdata:items/item_w_katana"
    assert model_module.subtree_of("items/item_w_katana") == "items"
    assert model_module.variant_of("system/stats - hunter") == "hunter"
    assert model_module.variant_of("system/stats - vampire") == "vampire"
    assert model_module.variant_of("system/stats") == "base"
    assert model_module.output_relative_path("items/item_w_katana") == PurePosixPath(
        "items/item_w_katana.glb"
    )
    with pytest.raises(model_module.VdataModelError):
        model_module.normalize_key("just-a-name")


def test_source_keys_and_load_source_closure_tolerate_prefixes_and_extensions():
    index = _index("vdata/items/item_w_katana.txt")
    reader = lambda idx, key: b"WeaponData\n{\n}\n"
    assert source_keys(index) == ["items/item_w_katana"]
    for argument in ("items/item_w_katana", "vdata/items/item_w_katana.txt", "ITEMS/ITEM_W_KATANA"):
        closure = load_source_closure(index, argument, read_bytes=reader)
        assert closure.key == "items/item_w_katana"
        assert closure.subtree == "items"
        assert closure.variant == "base"
    with pytest.raises(VdataSourceError):
        load_source_closure(index, "items/missing", read_bytes=reader)


# --- WeaponData ----------------------------------------------------------------------------------

_KATANA = b"""WeaponData
{
\t"viewmodel"\t"models/weapons/w_null.mdl"
\t"playermodel"\t"models/weapons/katana/world/g_katana.mdl"
\t"wieldmodel_f"\t"models/weapons/katana/wield/w_f_katana.mdl"
\t"wieldmodel_m"\t"models/weapons/katana/wield/w_m_katana.mdl"
\t"infomodel"\t"models/weapons/katana/info/i_katana.mdl"
\t"activation0"\t""
\t"sound_group"\t"Katana"
\t"camera_class"\t"melee"
\t"item_flags"\t"0"
\t"item_worth"\t"215"
\t"BitFlag_CantBeLast"\t"1"
}
"""


def test_weapon_data_publishes_the_four_model_roles_with_null_and_absent_and_present_cases():
    model = _decode(
        "items/item_w_katana", _KATANA,
        resolve_model=lambda path: path == "models/weapons/katana/world/g_katana.mdl",
    )
    fields = model.projection["fields"]
    assert model.projection["kind"] == "closed"
    view = fields["models"]["viewmodel"]
    assert view == {
        "raw": "models/weapons/w_null.mdl", "path": "models/weapons/w_null.mdl",
        "asset": "vtmb:model:weapons/w_null", "authored": True, "present": True,
        "resolved": False,
    }
    ground = fields["models"]["playermodel"]
    assert ground["present"] is True and ground["resolved"] is True
    assert ground["asset"] == "vtmb:model:weapons/katana/world/g_katana"
    wieldmodel_absent = fields["models"]
    assert wieldmodel_absent["wieldmodel_f"]["authored"] is True  # authored in this fixture
    dep_roles = {(row["role"], row["asset"]) for row in model.dependencies}
    assert ("model", "vtmb:model:weapons/w_null") in dep_roles


def test_weapon_data_applies_the_loaders_stated_defaults():
    body = b'WeaponData\n{\n\t"item_type" "weapon_melee"\n}\n'
    model = _decode("items/synthetic", body)
    fields = model.projection["fields"]
    assert fields["itemFlags"] == {"value": 8, "authored": False, "default": 8}
    assert fields["isVisibleInHud"] == {"value": True, "authored": False, "default": True}
    assert fields["hidesHandsModel"] == {"value": False, "authored": False, "default": False}
    assert fields["zoomSwayDeltaMagnitudeMin"]["value"] == 0.6
    # An absent model key still publishes present:false, not an omission.
    assert fields["models"]["viewmodel"] == {
        "raw": None, "path": None, "asset": None, "authored": False, "present": False,
        "resolved": False,
    }


def test_weapon_data_camera_class_enum_mapping():
    for spelling, enum in (("melee", "0x10"), ("force_3rd", "0x10"), ("ranged", "0x2"),
                            ("thrown", "0x4"), ("force_1st", "0x8"), ("noswitch", "0x0")):
        body = f'WeaponData\n{{\n\t"camera_class" "{spelling}"\n}}\n'.encode()
        model = _decode("items/synthetic", body)
        assert model.projection["fields"]["cameraClass"]["enum"] == enum
    absent = _decode("items/synthetic", b"WeaponData\n{\n}\n")
    assert absent.projection["fields"]["cameraClass"]["enum"] == "0x0"
    assert absent.projection["fields"]["cameraClass"]["authored"] is False


def test_weapon_data_sound_group_dependency_resolves_through_the_index():
    members = ["sound/weapons/melee/katana/swish_1.wav", "sound/weapons/melee/katana/deploy.wav"]
    model = _decode(
        "items/item_w_katana", _KATANA,
        resolve_sound_group=lambda folded: members if folded == "katana" else [],
    )
    sound_group = model.projection["fields"]["soundGroup"]
    assert sound_group["resolved"] is True
    assert {row["path"] for row in sound_group["members"]} == set(members)
    assert {row["asset"] for row in sound_group["members"]} == {
        "vtmb:sound:weapons/melee/katana/swish_1.wav",
        "vtmb:sound:weapons/melee/katana/deploy.wav",
    }
    sound_group_deps = [row for row in model.dependencies if row["role"] == "sound-group"]
    assert len(sound_group_deps) == 2
    # `sourcePath` is the install-relative path the member resolved to, not the bare group name.
    assert {row["sourcePath"] for row in sound_group_deps} == set(members)
    assert {row["asset"] for row in sound_group_deps} == {
        "vtmb:sound:weapons/melee/katana/swish_1.wav",
        "vtmb:sound:weapons/melee/katana/deploy.wav",
    }


def test_weapon_data_sound_group_with_no_directory_match_is_typed_unidentified():
    model = _decode("items/item_w_katana", _KATANA, resolve_sound_group=lambda folded: [])
    assert model.projection["fields"]["soundGroup"]["resolved"] is False
    unidentified = [row for row in model.typed_unidentified if row.get("field") == "sound_group"]
    assert unidentified and isinstance(unidentified[0]["offset"], int)


def test_weapon_data_publishes_an_unknown_key_as_typed_unidentified():
    body = b'WeaponData\n{\n\t"is_wieldable" "1"\n}\n'
    model = _decode("items/synthetic", body)
    assert any(row["field"] == "is_wieldable" for row in model.typed_unidentified)


def test_weapon_data_publishes_sound_dependencies_from_nested_magazine_and_activation_sounddata():
    body = (
        b'WeaponData\n{\n'
        b'\tMagazine\n\t{\n\t\tSoundData\n\t\t{\n\t\t\t"reload"\n\t\t\t{\n'
        b'\t\t\t\t"sound1"\t"weapons/mac_10/reload.wav"\n\t\t\t}\n\t\t}\n\t}\n'
        b'\tActivation\n\t{\n\t\tSoundData\n\t\t{\n\t\t\t"attack"\n\t\t\t{\n'
        b'\t\t\t\t"sound1"\t"weapons/mac_10/shoot1.wav"\n\t\t\t}\n\t\t}\n\t}\n'
        b'}\n'
    )
    model = _decode(
        "items/synthetic", body,
        resolve_asset=lambda path: path == "sound/weapons/mac_10/reload.wav",
    )
    sound_deps = {
        (row["sourcePath"], row["asset"], row["resolved"])
        for row in model.dependencies if row["role"] == "sound"
    }
    assert ("weapons/mac_10/reload.wav", "vtmb:sound:weapons/mac_10/reload.wav", True) in sound_deps
    assert ("weapons/mac_10/shoot1.wav", "vtmb:sound:weapons/mac_10/shoot1.wav", False) in sound_deps


def test_weapon_data_deduplicates_the_same_model_reference_bound_to_several_roles():
    body = (
        b'WeaponData\n{\n'
        b'\t"viewmodel"\t"models/weapons/w_null.mdl"\n'
        b'\t"playermodel"\t"models/weapons/w_null.mdl"\n'
        b'\t"wieldmodel_f"\t"models/weapons/w_null.mdl"\n'
        b'\t"wieldmodel_m"\t"models/weapons/w_null.mdl"\n'
        b'}\n'
    )
    model = _decode("items/synthetic", body)
    null_model_deps = [row for row in model.dependencies if row["asset"] == "vtmb:model:weapons/w_null"]
    assert len(null_model_deps) == 1


# --- PreCacheData --------------------------------------------------------------------------------


def test_precache_data_publishes_an_asset_dependency_per_entry():
    body = b'PreCacheData\n{\n\tPreCacheData\n\t{\n\t\t"env_phosphoroushit"\t"sprites/ember.spr"\n\t}\n}\n'
    model = _decode(
        "precache/entities", body, resolve_asset=lambda path: path == "sprites/ember.spr"
    )
    assert model.projection["kind"] == "closed"
    entries = model.projection["entries"]
    assert entries == [{
        "classname": "env_phosphoroushit", "path": "sprites/ember.spr",
        "asset": "vtmb:asset:sprites/ember.spr", "resolved": True,
    }]
    assert model.dependencies == [
        {"role": "asset", "asset": "vtmb:asset:sprites/ember.spr",
         "sourcePath": "sprites/ember.spr", "resolved": True}
    ]


# --- TerminalDefinition / keypad_strings ----------------------------------------------------------

_TERMINAL = b"""TerminalDefinition
{
\t"screen saver"\t\t"Abrams Jewelry, Inc."
\t"brackets"\t\t""
\t"email_password"\t"letmein"
\t"email_username"\t"Abrams Jewelry"

\tLogonScreen
\t{
\t\t"line0"\t\t"  Abrams Jewelry, Inc., L.L.C."
\t}

\tSubDir
\t{
\t\t"name"\t\t\t"Safe"
\t\t"password"\t\t"griff"
\t\t"description"\t\t"Safe Security Controls"

\t\tFunction
\t\t{
\t\t\t"name"\t\t"Unlock"
\t\t\t"description"\t"Unlock Safe"
\t\t\t"runtext"\t"Safe doors unlocked."
\t\t\t"trigger"\t"0"
\t\t\t"runscript"\t"G.Safe_Locked = 0"
\t\t}
\t}

\tEmail
\t{
\t\t"subject"\t"A reminder"
\t\t"sender"\t"LaCroix"
\t\t"body"\t"Mercurio will contact you."
\t\t"autodelete"\t"1"
\t}
}
"""


def test_terminal_definition_publishes_subdirs_functions_and_emails():
    model = _decode("hackterminals/synthetic", _TERMINAL)
    fields = model.projection["fields"]
    assert fields["screenSaver"]["value"] == "Abrams Jewelry, Inc."
    assert fields["logonScreen"] == [{"key": "line0", "value": "  Abrams Jewelry, Inc., L.L.C."}]
    assert len(fields["subDirs"]) == 1
    subdir = fields["subDirs"][0]
    assert subdir["name"] == "Safe" and len(subdir["functions"]) == 1
    assert subdir["functions"][0]["runscript"] == "G.Safe_Locked = 0"
    assert len(fields["emails"]) == 1


def test_terminal_definition_marks_autodelete_not_read_by_loader():
    model = _decode("hackterminals/synthetic", _TERMINAL)
    autodelete = model.projection["fields"]["emails"][0]["autodelete"]
    assert autodelete == {"value": "1", "readByLoader": False}


_KEYPAD = b"""keypad_strings
{
\tkeypad
\t{
\t\t"TextID"\t\t"keypad example"
\t\t"TitleText"\t\t"##ERROR##"
\t}
\tkeypad
\t{
\t\t"TextID"\t\t"bloodbank"
\t\t"TitleText"\t\t"Santa Monica Blood Bank"
\t}
}
"""


def test_keypad_strings_tolerates_a_directive_alongside_its_keypad_blocks():
    body = (
        b'keypad_strings\n{\n\t#include "shared.txt"\n\tkeypad\n\t{\n\t\t"TextID" "a"\n'
        b'\t\t"TitleText" "b"\n\t}\n}\n'
    )
    model = _decode("hackterminals/synthetic", body)
    assert model.projection["kind"] == "closed"
    assert model.projection["keypads"] == [{"textId": "a", "titleText": "b"}]


def test_keypad_strings_publishes_every_keypad_block():
    model = _decode("hackterminals/prop_keypad", _KEYPAD)
    assert model.projection["kind"] == "closed"
    assert model.projection["keypads"] == [
        {"textId": "keypad example", "titleText": "##ERROR##"},
        {"textId": "bloodbank", "titleText": "Santa Monica Blood Bank"},
    ]


# --- open projection -------------------------------------------------------------------------


def test_clan_projection_preserves_rows_and_types_only_general_body_fields():
    model = _decode("system/clandoc000", b'''ClanDataTables {
        ClanData { General {
            M_Body0 "models/character/pc/old.mdl"
            M_Body0 "models/character/pc/male.mdl"
            F_Body3 "models/character/pc/female.mdl"
            DeathGib "models/gibs/hgibs.mdl"
        } Unknown { Keep "everything" } }
        ClanData { General { M_Body "models/character/npc/hunter.mdl" F_Body "" } }
    }''', resolve_model=lambda path: path.endswith("male.mdl"))
    rows = model.projection["clans"]
    assert len(rows) == 2
    assert list(rows[0]["bodies"]) == ["m_body0", "f_body3"]
    assert rows[0]["bodies"]["m_body0"]["asset"] == "vtmb:model:character/pc/male"
    assert rows[0]["bodies"]["m_body0"]["resolved"]
    assert not rows[1]["bodies"]["f_body"]["present"]
    assert "unknown" in model.projection["sections"]["clandata"][0]
    assert any(row["asset"] == "vtmb:model:gibs/hgibs" for row in model.dependencies)
    verify_ledger_row(model.ledger_row, model.member.data)


def test_an_open_root_key_reshapes_the_tree_without_a_completeness_claim():
    body = b'CameraShotTable\n{\n\tShot\n\t{\n\t\t"Position" "Named"\n\t}\n}\n'
    model = _decode("camerashots/synthetic", body)
    assert model.projection["kind"] == "open"
    assert model.projection["vocabulary"] == "open"
    assert model.projection["rootKey"] == "CameraShotTable"
    assert model.projection["sections"] == {"shot": {"position": "Named"}}


def test_clandatatables_body_and_hand_models_publish_model_dependencies_from_the_open_root():
    body = (
        b'ClanDataTables\n{\n\tClanData\n\t{\n\t\tGeneral\n\t\t{\n'
        b'\t\t\t"M_Body"\t"models/character/npc/x.mdl"\n'
        b'\t\t\t"F_Hands"\t"models/hands/female/x.mdl"\n'
        b'\t\t\t"DeathGib"\t"models/gibs/hgibs.mdl"\n'
        b'\t\t}\n\t}\n}\n'
    )
    model = _decode(
        "system/clandoc000", body,
        resolve_model=lambda path: path == "models/gibs/hgibs.mdl",
    )
    assert model.projection["kind"] == "open"
    dep_rows = {(row["role"], row["asset"], row["resolved"]) for row in model.dependencies}
    assert ("model", "vtmb:model:character/npc/x", False) in dep_rows
    assert ("model", "vtmb:model:hands/female/x", False) in dep_rows
    assert ("model", "vtmb:model:gibs/hgibs", True) in dep_rows


def test_chareditor_music_publishes_a_sound_dependency_joined_under_sound():
    body = b'CharEditor\n{\n\tMusic\t\t"music/Vampire_Theme.mp3"\n}\n'
    model = _decode(
        "system/chareditor", body,
        resolve_asset=lambda path: path == "sound/music/vampire_theme.mp3",
    )
    assert model.dependencies == [{
        "role": "sound", "asset": "vtmb:sound:music/vampire_theme.mp3",
        "sourcePath": "music/Vampire_Theme.mp3", "resolved": True,
    }]


def test_open_root_model_dependencies_are_matched_by_value_shape_not_a_key_allowlist():
    body = (
        b'ClanDataTables\n{\n\tClanData\n\t{\n\t\tGeneral\n\t\t{\n'
        b'\t\t\t"M_Body0"\t"models/character/pc/male/brujah/armor0/brujah_male_armor_0.mdl"\n'
        b'\t\t\t"InfoModel"\t"models/weapons/disciplines/animalism/info/i_animalism.mdl"\n'
        b'\t\t\t"SpawnModel"\t"models/character/npc/unique/girl.mdl"\n'
        b'\t\t}\n\t}\n}\n'
    )
    model = _decode("system/clandoc000", body, resolve_model=lambda path: False)
    dep_rows = {(row["role"], row["asset"]) for row in model.dependencies}
    assert ("model", "vtmb:model:character/pc/male/brujah/armor0/brujah_male_armor_0") in dep_rows
    assert ("model", "vtmb:model:weapons/disciplines/animalism/info/i_animalism") in dep_rows
    assert ("model", "vtmb:model:character/npc/unique/girl") in dep_rows


def test_an_open_root_scalar_ending_in_mp3_publishes_a_sound_dependency_with_no_key_allowlist():
    body = b'RadioData\n{\n\tShow\n\t{\n\t\t"filename"\t"radio/radio_loop_1.mp3"\n\t}\n}\n'
    model = _decode(
        "system/radio_data", body,
        resolve_asset=lambda path: path == "sound/radio/radio_loop_1.mp3",
    )
    assert model.dependencies == [{
        "role": "sound", "asset": "vtmb:sound:radio/radio_loop_1.mp3",
        "sourcePath": "radio/radio_loop_1.mp3", "resolved": True,
    }]


def test_signdata_backgroundimage_name_publishes_a_material_dependency():
    body = b'SignData\n{\n\tBackgroundImage\n\t{\n\t\t"Name"\t"hud/signs/PortraitCrayon"\n\t}\n}\n'
    model = _decode(
        "signs/childs_drawing", body,
        resolve_asset=lambda path: path == "materials/hud/signs/portraitcrayon.vmt",
    )
    assert model.dependencies == [{
        "role": "material", "asset": "vtmb:material:hud/signs/portraitcrayon",
        "sourcePath": "hud/signs/PortraitCrayon", "resolved": True,
    }]


# --- validation --------------------------------------------------------------------------------


def test_coverage_mapped_reflects_the_grammar_and_content_actually_published():
    from elysium_pipeline.formats.vdata_glb import coverage as coverage_module

    plain = _decode("system/synthetic", b'Foo\n{\n\t"a" "1"\n}\n')
    plain_mapped = coverage_module.mapped_keys(plain)
    assert "tree" in plain_mapped and "rows" not in plain_mapped
    assert "comments" not in plain_mapped  # no comment authored

    commented = _decode("system/synthetic", b'Foo\n{\n\t"a" "1" // note\n}\n')
    assert "comments" in coverage_module.mapped_keys(commented)

    freeform = _decode("system/credits", b"plain prose with no braces at all\n")
    freeform_mapped = coverage_module.mapped_keys(freeform)
    assert "rows" in freeform_mapped and "tree" not in freeform_mapped

    delimited = _decode("system/experience_table", b"Title01  | NONE | 51\r\n")
    delimited_mapped = coverage_module.mapped_keys(delimited)
    assert "rows" in delimited_mapped and "tree" not in delimited_mapped


def test_an_unresolved_dependency_warns_instead_of_failing_the_unit(tmp_path):
    index = _index("vdata/items/item_w_katana.txt")
    reader = lambda idx, key: _KATANA if key == "vdata/items/item_w_katana.txt" else None
    destination = exporter.export(index, "items/item_w_katana", tmp_path, read_bytes=reader)
    summary = validation.validate(destination)
    assert summary["unresolved"] == 0 and summary["unsupported"] == 0  # completeness unaffected
    assert "vtmb:model:weapons/w_null" in summary["unresolvedDependencies"]
    warnings = validation.warnings_for(summary)
    assert any("no target for" in warning for warning in warnings)


def test_a_full_export_round_trips_through_standalone_validation(tmp_path):
    index = _index("vdata/items/item_w_katana.txt")
    reader = lambda idx, key: _KATANA if key == "vdata/items/item_w_katana.txt" else None
    destination = exporter.export(index, "items/item_w_katana", tmp_path, read_bytes=reader)
    assert destination == tmp_path / "items" / "item_w_katana.glb"
    summary = validation.validate(destination)
    assert summary["byteCoveragePercent"] == 100.0
    assert summary["unresolved"] == 0 and summary["unsupported"] == 0
    assert summary["subtree"] == "items" and summary["variant"] == "base"
    assert isinstance(validation.warnings_for(summary), list)


def test_the_validator_rejects_a_tampered_byte_ledger():
    model = _decode("system/synthetic", b'Foo\n{\n\t"a" "1"\n}\n')
    document, binary = exporter.build_document(model)
    ledger = document["extensions"][model_module.VDATA_EXTENSION]["coverage"]["byteLedger"][0]
    ledger["ranges"][0]["length"] += 1
    with pytest.raises((UnitValidationError, ByteLedgerError)):
        validation.validate_document(document, binary, source_members=model.member and [model.member])


def test_the_validator_independently_reparses_the_source_and_catches_a_disagreement():
    model = _decode("system/synthetic", b'Foo\n{\n\t"a" "1"\n}\n')
    document, binary = exporter.build_document(model)
    tree = document["extensions"][model_module.VDATA_EXTENSION]["tree"]
    tree["children"][0]["children"][0]["value"] = "2"
    with pytest.raises(UnitValidationError):
        validation.validate_document(document, binary, source_members=[model.member])


def test_the_validator_accepts_a_well_formed_document_with_source_members():
    model = _decode("items/item_w_katana", _KATANA)
    document, binary = exporter.build_document(model)
    summary = validation.validate_document(document, binary, source_members=[model.member])
    assert summary["grammar"] == "keyvalues"
    assert summary["projectionKind"] == "closed"


def test_the_validator_catches_a_tampered_closed_projection_default():
    model = _decode("items/synthetic", b'WeaponData\n{\n\t"item_type" "weapon_melee"\n}\n')
    document, binary = exporter.build_document(model)
    root = document["extensions"][model_module.VDATA_EXTENSION]
    root["projection"]["fields"]["itemFlags"]["value"] = 99
    with pytest.raises(UnitValidationError):
        validation.validate_document(document, binary, source_members=[model.member])


def test_the_validator_catches_a_tampered_freeform_row():
    body = b"plain prose with no braces at all\n"
    model = _decode("system/credits", body)
    document, binary = exporter.build_document(model)
    root = document["extensions"][model_module.VDATA_EXTENSION]
    root["rows"][0]["text"] = "tampered"
    with pytest.raises(UnitValidationError):
        validation.validate_document(document, binary, source_members=[model.member])


def test_the_validator_catches_a_tampered_delimited_row():
    body = b"Title01  | NONE | 51\r\n"
    model = _decode("system/experience_table", body)
    document, binary = exporter.build_document(model)
    root = document["extensions"][model_module.VDATA_EXTENSION]
    root["rows"][0]["value"] = "999"
    with pytest.raises(UnitValidationError):
        validation.validate_document(document, binary, source_members=[model.member])


def test_export_wraps_a_missing_member_as_a_vdata_glb_error(tmp_path):
    index = _index("vdata/items/item_w_katana.txt")
    with pytest.raises(exporter.VdataGlbError):
        exporter.export(
            index, "items/does-not-exist", tmp_path, read_bytes=lambda idx, key: None
        )


def test_ws_fix_value_extracts_the_residual_number():
    assert projection_module.ws_fix_value("//1, ws-fix") == "1"
    assert projection_module.ws_fix_value("// added by wesp") is None


# --- source capsule ----------------------------------------------------------------------------


#: Bytes a vdata file actually carries that a re-encode would quietly change: DOS terminators,
#: tab-separated pairs, trailing spaces before the newline, cp1252 punctuation and accents above
#: 0x7f, a UTF-8 BOM the engine reads straight past, and a quoted value spanning two lines
#: (`clandoc000.txt` opens one that way). The capsule is the unaltered file, so each of these
#: must come back out of the GLB byte for byte.
_TRICKY = {
    "crlf-tabs-and-trailing-space": b'Foo\r\n{\r\n\t"a"\t"1"   \r\n}\r\n',
    "cp1252-high-bytes": b'Foo\n{\n\t"name" "Caf\xe9 \x93Nosferatu\x94 \x96 Santa Monica"\n}\n',
    "utf8-bom": b'\xef\xbb\xbfFoo\n{\n\t"a" "1"\n}\n',
    "multi-line-quoted-value": b'Foo\n{\n\t"desc" "line one\r\n\r\nline two"\n}\n',
    "no-terminator-at-eof": b'Foo\n{\n\t"a" "1"\n}',
    "empty-file": b"",
}


def _capsule_of(destination):
    from elysium_pipeline.formats.unit_contract import read_glb, source_capsules

    document, binary = read_glb(destination)
    root = document["extensions"][model_module.VDATA_EXTENSION]
    return document, binary, root, source_capsules(document, binary, root)


@pytest.mark.parametrize("name", sorted(_TRICKY))
def test_the_capsule_returns_the_source_file_byte_for_byte(tmp_path, name):
    body = _TRICKY[name]
    path = "vdata/system/synthetic.txt"
    index = _index(path)
    destination = exporter.export(
        index, "system/synthetic", tmp_path, read_bytes=lambda idx, key: body
    )
    _document, _binary, root, capsules = _capsule_of(destination)
    assert capsules == {path: body}
    assert root["schemaVersion"] == "1.1.0"
    assert validation.validate(destination)["byteCoveragePercent"] == 100.0


def test_a_published_unit_declares_its_capsule_and_addresses_it_through_buffer_zero(tmp_path):
    body = _KATANA
    index = _index("vdata/items/item_w_katana.txt")
    destination = exporter.export(
        index, "items/item_w_katana", tmp_path, read_bytes=lambda idx, key: body
    )
    document, binary, root, _capsules = _capsule_of(destination)
    assert root["sourceResolution"]["capsule"] == {"encoding": "raw"}
    member = root["sourceResolution"]["members"][0]
    assert member["capsule"] == {"bufferView": 0, "byteLength": len(body)}
    assert document["buffers"] == [{"byteLength": len(body)}]
    assert document["bufferViews"][0]["buffer"] == 0
    # The BIN chunk is the payload padded to four bytes and nothing else.
    assert len(binary) - len(body) == -len(body) % 4
    assert binary[: len(body)] == body


def test_an_empty_vdata_file_publishes_no_bin_chunk_and_an_empty_capsule(tmp_path):
    index = _index("vdata/system/blank.txt")
    destination = exporter.export(
        index, "system/blank", tmp_path, read_bytes=lambda idx, key: b""
    )
    document, binary, root, capsules = _capsule_of(destination)
    assert binary == b"" and "buffers" not in document
    assert root["sourceResolution"]["members"][0]["capsule"] == {"byteLength": 0}
    assert capsules == {"vdata/system/blank.txt": b""}


def test_the_validator_refuses_a_vdata_unit_that_dropped_its_capsule():
    model = _decode("system/synthetic", b'Foo\n{\n\t"a" "1"\n}\n')
    document, binary = exporter.build_document(model)
    document["extensions"][model_module.VDATA_EXTENSION]["sourceResolution"]["members"][0].pop(
        "capsule"
    )
    with pytest.raises(UnitValidationError, match="carries no source capsule"):
        validation.validate_document(document, binary, source_members=[model.member])


def test_the_validator_refuses_a_vdata_unit_that_never_declared_a_capsule():
    # Capsule is REQUIRED for vdata (schema 1.1.0), unlike a seam that has not adopted it --
    # `validate_capsules` alone returns early on a missing `sourceResolution.capsule`, so this must
    # be caught by the vdata seam's own explicit check, not the generic one.
    model = _decode("system/synthetic", b'Foo\n{\n\t"a" "1"\n}\n')
    document, binary = exporter.build_document(model)
    root = document["extensions"][model_module.VDATA_EXTENSION]
    root["sourceResolution"].pop("capsule")
    for member in root["sourceResolution"]["members"]:
        member.pop("capsule", None)
    document.pop("buffers", None)
    document.pop("bufferViews", None)
    with pytest.raises(UnitValidationError, match="declares no source capsule"):
        validation.validate_document(document, b"", source_members=[model.member])


def test_the_validator_refuses_a_vdata_capsule_whose_bytes_were_tampered_with():
    model = _decode("system/synthetic", b'Foo\n{\n\t"a" "1"\n}\n')
    document, binary = exporter.build_document(model)
    tampered = b"B" + binary[1:]
    with pytest.raises(UnitValidationError, match="not the member it names"):
        validation.validate_document(document, tampered, source_members=[model.member])
