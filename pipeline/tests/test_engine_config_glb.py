"""Synthetic contract tests for the isolated engine-config GLB exporter.

Every test builds its own fake install index and `read_bytes` closure -- never the real one --
so the suite is a pin on this seam's own decode/export/validate contract, independent of the
corpus.
"""

from __future__ import annotations

import hashlib
import struct
import zlib
from pathlib import PurePosixPath

import pytest

from elysium_pipeline.exporters import engine_config_glb as exporter
from elysium_pipeline.formats.engine_config_glb import (
    ENGINE_CONFIG_EXTENSION,
    decode_engine_config,
    load_source_closure,
    member_resolved,
    normalize_key,
    output_relative_path,
    source_keys,
)
from elysium_pipeline.formats.engine_config_glb.decode import EngineConfigDecodeError
from elysium_pipeline.formats.engine_config_glb.model import (
    EngineConfigModelError,
    asset_id,
    dependency_asset_id,
    grammar_for,
)
from elysium_pipeline.formats.engine_config_glb.source import EngineConfigSourceError
from elysium_pipeline.formats.unit_contract import (
    ByteLedger,
    ByteLedgerError,
    UnitValidationError,
    write_glb,
)
from elysium_pipeline.validation import engine_config_glb as validation
from elysium_pipeline.validation.engine_config_glb import EngineConfigGlbValidationError

# --- fixtures ----------------------------------------------------------------------------------


def _fake_install(files: dict[str, bytes]) -> tuple[dict, callable]:
    index = {path: ("loose", f"/fake/{path}") for path in files}

    def read_bytes(idx: dict, key: str) -> bytes | None:
        return files.get(key)

    return index, read_bytes


def _export_and_validate(files: dict[str, bytes], key: str, tmp_path):
    index, read_bytes = _fake_install(files)
    destination = exporter.export(index, key, tmp_path, read_bytes=read_bytes)
    summary = validation.validate(destination)
    return destination, summary


def _decode(files: dict[str, bytes], key: str):
    index, read_bytes = _fake_install(files)
    closure = load_source_closure(index, key, read_bytes=read_bytes)
    model = decode_engine_config(closure, resolver=lambda path: member_resolved(index, path))
    return index, read_bytes, closure, model


# =================================================================================================
# console-script
# =================================================================================================

USER_CFG = (
    b'// user config\r\n'
    b'unbindall\r\n'
    b'bind "TAB" "+wpn_secondaryatk"\r\n'
    b'bind SEMICOLON "+mlook"\r\n'
    b'bind "TAB" "+duplicate"\r\n'
    b'alias run "-speed;"\r\n'
    b'alias chain "run;autospeed()"\r\n'
    b'alias run "-speed2;"\r\n'
    b'fps_max "65"\r\n'
    b'exec autoexec.cfg\r\n'
    b'exec joystick.cfg\r\n'
    b'"__main__.checkTutorial()"\r\n'
)
USER_CFG_PATH = "cfg/user.cfg"


def test_the_key_keeps_its_own_extension_below_the_family_root():
    assert normalize_key("engine-config/cfg/user.cfg") == "cfg/user.cfg"
    assert normalize_key("CFG/USER.CFG") == "cfg/user.cfg"
    assert asset_id("cfg/user.cfg") == "vtmb:engine-config:cfg/user.cfg"
    assert output_relative_path("cfg/user.cfg") == PurePosixPath("cfg/user.cfg.glb")
    assert output_relative_path("lights.rad") == PurePosixPath("lights.rad.glb")


def test_a_key_outside_the_closed_vocabulary_is_refused():
    with pytest.raises(EngineConfigModelError):
        normalize_key("cfg/joystick.cfg")
    with pytest.raises(EngineConfigModelError):
        grammar_for("cfg/elysium_capture.cfg")


def test_dependency_asset_id_names_a_key_this_seam_never_decodes():
    """`exec joystick.cfg` has this identity even though the install ships no such member."""

    assert dependency_asset_id("cfg/joystick.cfg") == "vtmb:engine-config:cfg/joystick.cfg"


def test_source_keys_enumerates_only_the_known_vocabulary_present_in_the_index():
    index, _ = _fake_install(
        {
            "cfg/user.cfg": b"x", "cfg/elysium_capture.cfg": b"x", "not_a_member.txt": b"x",
            "lights.rad": b"x",
        }
    )
    assert source_keys(index) == ["cfg/user.cfg", "lights.rad"]


def test_every_byte_of_a_console_script_is_claimed_exactly_once():
    _, _, _, model = _decode({USER_CFG_PATH: USER_CFG}, USER_CFG_PATH)
    row = model.ledger_row
    assert row["byteLength"] == len(USER_CFG) == row["accountedBytes"]
    assert row["coveragePercent"] == 100.0
    cursor = 0
    for entry in row["ranges"]:
        assert entry["offset"] == cursor
        cursor += entry["length"]
    assert cursor == len(USER_CFG)


def test_commands_are_the_complete_ordered_list_with_their_own_spans():
    _, _, _, model = _decode({USER_CFG_PATH: USER_CFG}, USER_CFG_PATH)
    names = [c["name"] for c in model.commands]
    assert names[0] == "unbindall"
    for index, command in enumerate(model.commands):
        assert command["index"] == index
        raw = USER_CFG[command["offset"]: command["offset"] + command["length"]].decode("latin-1")
        assert command["name"] in raw


def test_the_semicolon_alias_translates_to_the_literal_character():
    _, _, _, model = _decode({USER_CFG_PATH: USER_CFG}, USER_CFG_PATH)
    binding = next(b for b in model.bindings if b["sourceKey"] == "SEMICOLON")
    assert binding["key"] == ";"
    assert binding["command"] == "+mlook"


def test_a_repeated_bind_resolves_last_and_records_an_anomaly():
    _, _, _, model = _decode({USER_CFG_PATH: USER_CFG}, USER_CFG_PATH)
    tab_bindings = [b for b in model.bindings if b["key"] == "TAB"]
    assert len(tab_bindings) == 1
    assert tab_bindings[0]["command"] == "+duplicate"
    assert any(row["role"] == "repeated-bind" and row["key"] == "TAB" for row in model.anomalies)


def test_a_repeated_alias_resolves_last_and_records_an_anomaly():
    _, _, _, model = _decode({USER_CFG_PATH: USER_CFG}, USER_CFG_PATH)
    run_aliases = [a for a in model.aliases if a["name"] == "run"]
    assert len(run_aliases) == 1
    assert run_aliases[0]["body"] == "-speed2;"
    assert any(row["role"] == "repeated-alias" and row["name"] == "run" for row in model.anomalies)


def test_an_alias_body_naming_another_alias_is_a_resolved_use():
    _, _, _, model = _decode({USER_CFG_PATH: USER_CFG}, USER_CFG_PATH)
    chain = next(a for a in model.aliases if a["name"] == "chain")
    assert chain["uses"] == [{"name": "run", "resolved": True}]


def test_script_expressions_are_found_in_alias_bodies_and_bare_commands():
    _, _, _, model = _decode({USER_CFG_PATH: USER_CFG}, USER_CFG_PATH)
    texts = {row["text"] for row in model.script_expressions}
    assert "autospeed()" in texts
    assert "__main__.checkTutorial()" in texts


def test_a_two_token_line_that_is_not_a_command_or_alias_is_an_archived_cvar():
    _, _, _, model = _decode({USER_CFG_PATH: USER_CFG}, USER_CFG_PATH)
    cvar = next(c for c in model.cvars if c["name"] == "fps_max")
    assert cvar["value"] == "65"
    assert cvar["archived"] is True


def test_exec_produces_an_engine_config_dependency_per_target_resolution():
    _, _, _, model = _decode(
        {USER_CFG_PATH: USER_CFG, "cfg/autoexec.cfg": b"x"}, USER_CFG_PATH
    )
    by_asset = {row["asset"]: row for row in model.dependencies}
    resolved = by_asset["vtmb:engine-config:cfg/autoexec.cfg"]
    assert resolved["role"] == "engine-config"
    assert resolved["resolved"] is True
    unresolved = by_asset["vtmb:engine-config:cfg/joystick.cfg"]
    assert unresolved["resolved"] is False


def test_an_unterminated_quote_is_an_anomaly_and_still_fully_ledgered():
    text = b'bind "TAB" "+jump\r\nnextcommand\r\n'
    _, _, _, model = _decode({USER_CFG_PATH: text}, USER_CFG_PATH)
    assert any(row["role"] == "unterminated-quote" for row in model.anomalies)
    assert model.ledger_row["coveragePercent"] == 100.0


def test_mixed_line_endings_is_a_file_level_anomaly():
    text = b'cmd1\r\ncmd2\n'
    _, _, _, model = _decode({USER_CFG_PATH: text}, USER_CFG_PATH)
    assert {"role": "mixed-line-endings"} in model.anomalies


def test_a_command_followed_by_an_inline_comment_is_still_fully_ledgered():
    """The whitespace between a command's last argument and a trailing `//` comment used to be
    left unclaimed, aborting the export with a byte-ledger gap."""

    text = b'bind "w" "+forward" // move forward\r\nalias foo bar // note\r\n'
    _, _, _, model = _decode({USER_CFG_PATH: text}, USER_CFG_PATH)
    assert model.ledger_row["coveragePercent"] == 100.0
    names = [c["name"] for c in model.commands]
    assert names == ["bind", "alias"]


def test_the_omitted_proven_whitespace_claim_is_backed_by_an_omissions_row():
    _, _, _, model = _decode({USER_CFG_PATH: USER_CFG}, USER_CFG_PATH)
    assert model.ledger_row["stateBytes"].get("omitted-proven", 0) > 0
    row = next(o for o in model.omissions if o["role"] == "insignificant-whitespace")
    assert row["reason"] == "separator-bytes-carry-no-grammar-meaning"
    assert row["length"] == model.ledger_row["stateBytes"]["omitted-proven"]
    assert row["count"] > 0
    assert isinstance(row["sha256"], str) and len(row["sha256"]) == 64


def test_an_essentially_empty_script_decodes_with_zero_commands():
    _, _, _, model = _decode({"cfg/dummy.txt": b" "}, "cfg/dummy.txt")
    assert model.commands == []
    assert model.ledger_row["coveragePercent"] == 100.0


def test_a_truly_empty_console_script_is_an_empty_member_omission():
    _, _, _, model = _decode({"cfg/dummy.txt": b""}, "cfg/dummy.txt")
    assert model.ledger_row["byteLength"] == 0
    assert {"role": "empty-member"} in model.omissions


# =================================================================================================
# rad-rows
# =================================================================================================

LIGHTS_RAD = (
    b"lights/fluorescentcool001a\t189 231 232 350\n"
    b"// a comment row\n"
    b"\n"
    b"dev/DEV_INTERIORLIGHT02B\t151 176 204\n"
)


def test_texture_lights_read_texture_rgb_and_intensity_as_authored():
    _, _, _, model = _decode({"lights.rad": LIGHTS_RAD}, "lights.rad")
    row = model.texture_lights[0]
    assert row["texture"] == "lights/fluorescentcool001a"
    assert (row["r"], row["g"], row["b"], row["intensity"]) == (189, 231, 232, 350)
    assert row["material"] == "vtmb:material:lights/fluorescentcool001a"


def test_a_five_field_row_produces_one_material_dependency():
    _, _, _, model = _decode({"lights.rad": LIGHTS_RAD}, "lights.rad")
    row = next(d for d in model.dependencies if d["asset"] == "vtmb:material:lights/fluorescentcool001a")
    assert row["role"] == "material"
    assert row["sourcePath"] == "materials/lights/fluorescentcool001a.vmt"


def test_a_row_with_other_than_five_fields_is_malformed_but_still_carried():
    _, _, _, model = _decode({"lights.rad": LIGHTS_RAD}, "lights.rad")
    malformed = next(r for r in model.texture_lights if "DEV_INTERIORLIGHT" in r["texture"])
    assert malformed["intensity"] is None
    assert malformed["r"] == 151
    assert any(row["role"] == "malformed-rad-row" for row in model.anomalies)


def test_every_byte_of_lights_rad_is_claimed_exactly_once():
    _, _, _, model = _decode({"lights.rad": LIGHTS_RAD}, "lights.rad")
    row = model.ledger_row
    assert row["accountedBytes"] == row["byteLength"] == len(LIGHTS_RAD)
    assert row["coveragePercent"] == 100.0


def test_mixed_line_endings_is_reported_outside_the_console_script_grammar_too():
    """`mixed-line-endings` is scoped to no grammar in particular."""

    text = b"lights/a\t1 2 3 4\r\nlights/b\t1 2 3 4\n"
    _, _, _, model = _decode({"lights.rad": text}, "lights.rad")
    assert {"role": "mixed-line-endings"} in model.anomalies


# =================================================================================================
# keyvalues (detail.vbsp)
# =================================================================================================

DETAIL_VBSP_WELL_FORMED = (
    b'detail\n'
    b'{\n'
    b'\tweeds\n'
    b'\t{\n'
    b'\t\t"density" "1000.0"\n'
    b'\t\tGroup1\n'
    b'\t\t{\n'
    b'\t\t\t"alpha" "1.0"\n'
    b'\t\t\tModel1\n'
    b'\t\t\t{\n'
    b'\t\t\t\t"model" "models\\scenery\\plants\\weeda\\weeda.mdl"\n'
    b'\t\t\t\t"amount" ".05"\n'
    b'\t\t\t}\n'
    b'\t\t}\n'
    b'\t}\n'
    b'}\n'
)

#: Reproduces the retail anomaly this seam's own `specDeviations` names: an extra, unnamed `{`
#: with no name to attach to, wrapping the block that follows it.
DETAIL_VBSP_ANOMALOUS = (
    b'detail\n'
    b'{\n'
    b'\tbranches\n'
    b'\t{\n'
    b'\t\tGroup1\n'
    b'\t\t{\n'
    b'\t\t\t"alpha" "1.0"\n'
    b'\t\t}\n'
    b'\t{\n'
    b'\t\tGroup2\n'
    b'\t\t{\n'
    b'\t\t\t"alpha" "0.0"\n'
    b'\t\t\tModel1\n'
    b'\t\t\t{\n'
    b'\t\t\t\t"model" "models\\a\\a.mdl"\n'
    b'\t\t\t\t"amount" "0.5"\n'
    b'\t\t\t}\n'
    b'\t\t}\n'
    b'\t}\n'
    b'\t}\n'
    b'}\n'
)


def test_detail_types_are_typed_three_levels_deep():
    _, _, _, model = _decode({"detail.vbsp": DETAIL_VBSP_WELL_FORMED}, "detail.vbsp")
    detail_type = model.detail_types[0]
    assert detail_type["name"] == "weeds"
    assert detail_type["density"] == "1000.0"
    group = detail_type["groups"][0]
    assert group["name"] == "Group1"
    assert group["alpha"] == "1.0"
    model_entry = group["models"][0]
    assert model_entry["model"] == r"models\scenery\plants\weeda\weeda.mdl"
    assert model_entry["modelNormalized"] == "models/scenery/plants/weeda/weeda.mdl"
    assert model_entry["amount"] == ".05"


def test_a_model_path_produces_one_model_dependency():
    _, _, _, model = _decode({"detail.vbsp": DETAIL_VBSP_WELL_FORMED}, "detail.vbsp")
    row = next(d for d in model.dependencies if d["role"] == "model")
    assert row["asset"] == "vtmb:model:scenery/plants/weeda/weeda"
    assert row["sourcePath"] == "models/scenery/plants/weeda/weeda.mdl"
    assert row["resolved"] is False


def test_an_unnamed_brace_is_an_anonymous_block_transparently_unwrapped():
    _, _, _, model = _decode({"detail.vbsp": DETAIL_VBSP_ANOMALOUS}, "detail.vbsp")
    assert any(row["role"] == "anonymous-block" for row in model.anomalies)
    detail_type = model.detail_types[0]
    group_names = [group["name"] for group in detail_type["groups"]]
    # Group2 is unwrapped from the anonymous block and reads as a direct sibling of Group1.
    assert group_names == ["Group1", "Group2"]
    assert model.ledger_row["coveragePercent"] == 100.0


def test_an_unclosed_block_at_end_of_file_is_an_anomaly_not_a_crash():
    text = b'detail\n{\n\tweeds\n\t{\n\t\t"density" "1.0"\n'
    _, _, _, model = _decode({"detail.vbsp": text}, "detail.vbsp")
    assert model.ledger_row["coveragePercent"] == 100.0
    roles = [row["role"] for row in model.anomalies]
    assert roles.count("unclosed-block-at-end-of-file") == 2


def test_the_root_must_be_a_single_detail_block():
    with pytest.raises(EngineConfigDecodeError):
        _decode({"detail.vbsp": b'notdetail\n{\n}\n'}, "detail.vbsp")


# =================================================================================================
# line-list (maps/loadorder.txt)
# =================================================================================================

LOADORDER = (
    b"// not included: sp_genesisdevice_1\n"
    b"sp_theatre\n"
    b"// a plain comment\n"
    b"sm_hub_1\n"
)


def test_maps_are_one_row_per_line_with_a_stable_map_id():
    _, _, _, model = _decode({"maps/loadorder.txt": LOADORDER}, "maps/loadorder.txt")
    included = [row for row in model.maps if row["included"]]
    assert [row["name"] for row in included] == ["sp_theatre", "sm_hub_1"]
    assert included[0]["asset"] == "vtmb:map:sp_theatre"


def test_a_not_included_comment_is_both_a_comment_and_an_excluded_map_row():
    _, _, _, model = _decode({"maps/loadorder.txt": LOADORDER}, "maps/loadorder.txt")
    excluded = next(row for row in model.maps if not row["included"])
    assert excluded["name"] == "sp_genesisdevice_1"
    assert excluded["asset"] == "vtmb:map:sp_genesisdevice_1"
    assert any("not included" in comment["text"] for comment in model.comments)


def test_map_resolution_is_checked_against_the_install():
    index, read_bytes = _fake_install({"maps/loadorder.txt": LOADORDER, "maps/sp_theatre.bsp": b"x"})
    closure = load_source_closure(index, "maps/loadorder.txt", read_bytes=read_bytes)
    model = decode_engine_config(closure, resolver=lambda path: member_resolved(index, path))
    resolved = {row["name"]: row["resolved"] for row in model.maps}
    assert resolved["sp_theatre"] is True
    assert resolved["sm_hub_1"] is False
    dep = next(d for d in model.dependencies if d["asset"] == "vtmb:map:sp_theatre")
    assert dep["resolved"] is True


def test_every_byte_of_loadorder_is_claimed_exactly_once():
    _, _, _, model = _decode({"maps/loadorder.txt": LOADORDER}, "maps/loadorder.txt")
    row = model.ledger_row
    assert row["accountedBytes"] == row["byteLength"] == len(LOADORDER)
    assert row["coveragePercent"] == 100.0


# =================================================================================================
# key-equals-value (pack_values.txt)
# =================================================================================================

PACK_VALUES = (
    b"// comment\n"
    b"pack_folder = pack\n"
    b"max_size = 200\n"
    b"exclude = bsp dll py mp3 bik\n"
    b"skip = scc vpk map\n"
    b"separate = wav\n"
    b"localized_file = localized_list.txt\n"
)


def test_packer_carries_every_named_key():
    _, _, _, model = _decode({"pack_values.txt": PACK_VALUES}, "pack_values.txt")
    assert model.packer == {
        "pack_folder": "pack",
        "max_size": "200",
        "exclude": ["bsp", "dll", "py", "mp3", "bik"],
        "skip": ["scc", "vpk", "map"],
        "separate": ["wav"],
        "localized_file": "localized_list.txt",
    }


def test_every_byte_of_pack_values_is_claimed_exactly_once():
    _, _, _, model = _decode({"pack_values.txt": PACK_VALUES}, "pack_values.txt")
    row = model.ledger_row
    assert row["accountedBytes"] == row["byteLength"] == len(PACK_VALUES)
    assert row["coveragePercent"] == 100.0


def test_a_row_with_no_equals_sign_is_malformed_but_still_carried():
    text = PACK_VALUES + b"not_a_key_value_row\n"
    _, _, _, model = _decode({"pack_values.txt": text}, "pack_values.txt")
    assert any(row["role"] == "malformed-key-equals-value-row" for row in model.anomalies)
    assert model.ledger_row["coveragePercent"] == 100.0


def test_localized_file_names_another_engine_config_unit_as_a_dependency():
    """`localized_file` names `localized_list.txt`, another unit of this same seam; the contract's
    reference rule applies whether or not the seam doc's own Dependencies table names a role."""

    _, _, _, model = _decode(
        {"pack_values.txt": PACK_VALUES, "localized_list.txt": b"vdata\r\n"}, "pack_values.txt",
    )
    row = next(d for d in model.dependencies if d["role"] == "engine-config")
    assert row["asset"] == dependency_asset_id("localized_list.txt")
    assert row["sourcePath"] == "localized_list.txt"
    assert row["resolved"] is True


def test_localized_file_dependency_is_unresolved_when_the_install_has_no_such_member():
    _, _, _, model = _decode({"pack_values.txt": PACK_VALUES}, "pack_values.txt")
    row = next(d for d in model.dependencies if d["role"] == "engine-config")
    assert row["resolved"] is False


# =================================================================================================
# localized-list
# =================================================================================================

LOCALIZED_LIST = (
    b"DLG\r\n"
    b"materials\\Interface\\Pop_Ups\\dialog_dementation.vmt\r\n"
    b"materials\\Interface\\Pop_Ups\\dialog_dementation.tth\r\n"
    b"Sound\\Character\\dlg\\line1.lip\r\n"
    b"sound\\cutscenes\\intro.vcd\r\n"
    b"models\\props\\barrel.mdl\r\n"
    b"models\\props\\barrel.dx80.vtx\r\n"
    b"resource\\gameui_english.txt\r\n"
    b"scripts\\kb_act.lst\r\n"
    b"resource\\vampirescheme.res\r\n"
    b"some\\unknown\\thing.xyz\r\n"
    b"vdata\r\n"
)


def test_categories_are_one_row_per_header_with_typed_path_roles():
    _, _, _, model = _decode({"localized_list.txt": LOCALIZED_LIST}, "localized_list.txt")
    dlg = model.categories[0]
    assert dlg["name"] == "DLG"
    roles = {row["path"]: row.get("role") for row in dlg["paths"]}
    assert roles["materials\\Interface\\Pop_Ups\\dialog_dementation.vmt"] == "material"
    assert roles["materials\\Interface\\Pop_Ups\\dialog_dementation.tth"] == "texture"
    assert roles["Sound\\Character\\dlg\\line1.lip"] == "sound"
    assert roles["sound\\cutscenes\\intro.vcd"] == "scene"
    assert roles["models\\props\\barrel.mdl"] == "model"
    assert roles["models\\props\\barrel.dx80.vtx"] == "model"
    assert roles["resource\\gameui_english.txt"] == "ui-resource"
    assert roles["scripts\\kb_act.lst"] == "ui-resource"
    assert roles["resource\\vampirescheme.res"] == "ui-resource"
    assert roles["some\\unknown\\thing.xyz"] is None


def test_an_empty_category_still_produces_a_header_row():
    _, _, _, model = _decode({"localized_list.txt": LOCALIZED_LIST}, "localized_list.txt")
    vdata = model.categories[-1]
    assert vdata["name"] == "vdata"
    assert vdata["paths"] == []


def test_a_typed_extension_produces_one_dependency_per_role():
    _, _, _, model = _decode({"localized_list.txt": LOCALIZED_LIST}, "localized_list.txt")
    roles = {row["role"] for row in model.dependencies}
    assert roles == {"material", "texture", "sound", "scene", "model", "ui-resource"}


def test_a_lip_reference_resolves_the_paired_mp3_first():
    _, _, _, model = _decode(
        {
            "localized_list.txt": LOCALIZED_LIST,
            "sound/character/dlg/line1.mp3": b"x",
            "sound/character/dlg/line1.wav": b"x",
        },
        "localized_list.txt",
    )
    row = next(d for d in model.dependencies if d["role"] == "sound")
    assert row["asset"] == "vtmb:sound:character/dlg/line1.mp3"
    assert row["sourcePath"] == "sound/character/dlg/line1.mp3"
    assert row["resolved"] is True


def test_a_lip_reference_with_neither_companion_is_an_ordinary_unresolved_dependency():
    """A reference whose target is
    another seam's data and merely fails to resolve is an ordinary `dependencies` row, `resolved:
    false` -- not the `vtmb:missing-<kind>:` sentinel, which is reserved for a reference the
    *referenced kind's own rules* make unreachable to the engine. Neither `.mp3` nor `.wav`
    existing in the install is the former, not the latter."""

    _, _, _, model = _decode({"localized_list.txt": LOCALIZED_LIST}, "localized_list.txt")
    assert not any(o["role"] == "missing-sound-reference" for o in model.omissions)
    row = next(d for d in model.dependencies if d["role"] == "sound")
    assert row["asset"] == "vtmb:sound:character/dlg/line1.wav"
    assert row["sourcePath"] == "sound/character/dlg/line1.wav"
    assert row["resolved"] is False


def test_the_validator_rejects_the_retired_missing_sound_reference_omission_role():
    """`missing-sound-reference` is a role the decode no longer emits, so the validator's
    vocabulary no longer names it: a document carrying one is rejected rather than accepted as
    an omission this seam can prove."""

    assert "missing-sound-reference" not in validation._OMISSION_ROLES
    _, _, _, model = _decode({"localized_list.txt": LOCALIZED_LIST}, "localized_list.txt")
    document, binary = exporter.build_document(model)
    root = document["extensions"][ENGINE_CONFIG_EXTENSION]
    root["omissions"] = list(root["omissions"]) + [
        {"role": "missing-sound-reference", "path": "sound/character/dlg/line1.lip"}
    ]
    with pytest.raises(
        validation.EngineConfigGlbValidationError, match="unknown source omission"
    ):
        validation.validate_document(document, binary)


def test_a_vcd_reference_produces_a_scene_dependency():
    _, _, _, model = _decode({"localized_list.txt": LOCALIZED_LIST}, "localized_list.txt")
    row = next(d for d in model.dependencies if d["role"] == "scene")
    assert row["asset"] == "vtmb:scene:cutscenes/intro"
    assert row["sourcePath"] == "sound/cutscenes/intro.vcd"


def test_mdl_and_vtx_references_share_one_model_dependency():
    _, _, _, model = _decode({"localized_list.txt": LOCALIZED_LIST}, "localized_list.txt")
    model_rows = [d for d in model.dependencies if d["role"] == "model"]
    assert len(model_rows) == 1
    assert model_rows[0]["asset"] == "vtmb:model:props/barrel"
    assert model_rows[0]["sourcePath"] == "models/props/barrel.mdl"


def test_a_ui_resource_reference_keeps_its_extension_in_the_key():
    _, _, _, model = _decode({"localized_list.txt": LOCALIZED_LIST}, "localized_list.txt")
    assets = {d["asset"] for d in model.dependencies if d["role"] == "ui-resource"}
    assert assets == {
        "vtmb:ui-resource:resource/gameui_english.txt",
        "vtmb:ui-resource:scripts/kb_act.lst",
        "vtmb:ui-resource:resource/vampirescheme.res",
    }


def test_an_unclassified_extension_is_an_anomaly_not_a_silent_reference():
    _, _, _, model = _decode({"localized_list.txt": LOCALIZED_LIST}, "localized_list.txt")
    assert any(
        row["role"] == "unclassified-localized-path" and row["path"] == "some\\unknown\\thing.xyz"
        for row in model.anomalies
    )
    assert not any(d["sourcePath"].endswith("thing.xyz") for d in model.dependencies)


def test_every_byte_of_localized_list_is_claimed_exactly_once():
    _, _, _, model = _decode({"localized_list.txt": LOCALIZED_LIST}, "localized_list.txt")
    row = model.ledger_row
    assert row["accountedBytes"] == row["byteLength"] == len(LOCALIZED_LIST)
    assert row["coveragePercent"] == 100.0


# =================================================================================================
# binary: vidcfg.bin / voice_ban.dt
# =================================================================================================

VIDCFG_BIN = bytes.fromhex("e21eb7d7080dcf11fe6a0820a1c2cb35") + struct.pack("<I", 180092928)


def test_vidcfg_bin_carries_its_bytes_and_a_typed_unidentified_candidate_reading():
    _, _, _, model = _decode({"vidcfg.bin": VIDCFG_BIN}, "vidcfg.bin")
    assert model.binary["byteLength"] == 20
    assert model.binary["bytes"] == VIDCFG_BIN.hex()
    assert model.binary["candidateReading"]["guid"] == VIDCFG_BIN[:16].hex()
    assert len(model.typed_unidentified) == 1
    assert model.unresolved == [] and model.unsupported == []
    assert model.ledger_row["coveragePercent"] == 100.0


def test_voice_ban_dt_reads_a_count_with_zero_following_entries():
    data = struct.pack("<i", 1)
    _, _, _, model = _decode({"voice_ban.dt": data}, "voice_ban.dt")
    assert model.binary["candidateReading"] == {"count": 1}
    assert len(model.typed_unidentified) == 1
    assert model.omissions == []


def test_voice_ban_dt_trailing_zero_bytes_are_a_verified_padding_zero_claim():
    data = struct.pack("<i", 1) + b"\x00\x00\x00\x00"
    _, _, _, model = _decode({"voice_ban.dt": data}, "voice_ban.dt")
    row = model.ledger_row
    assert row["stateBytes"].get("padding-zero") == 4
    assert model.omissions == []


def test_voice_ban_dt_trailing_nonzero_bytes_are_trailing_fill():
    data = struct.pack("<i", 1) + b"\x01\x02\x03\x04"
    _, _, _, model = _decode({"voice_ban.dt": data}, "voice_ban.dt")
    trailing = data[4:]
    row = next(o for o in model.omissions if o["role"] == "trailing-fill")
    assert row["offset"] == 4
    assert row["length"] == len(trailing)
    assert row["sha256"] == hashlib.sha256(trailing).hexdigest()
    assert model.ledger_row["stateBytes"].get("omitted-proven") == len(trailing)
    # The count field is the candidate-count row only; the trailing bytes are now `omitted-proven`.
    assert len(model.typed_unidentified) == 1


def test_a_false_padding_zero_claim_over_non_zero_bytes_is_refused():
    """Pins that the shared `ByteLedger` this seam is built on would catch a bad claim."""

    ledger = ByteLedger("voice_ban.dt", b"\x01\x00\x00\x00")
    with pytest.raises(ByteLedgerError, match="non-zero"):
        ledger.claim(0, 4, "padding-zero", "binary.bytes")


# =================================================================================================
# save-fragment (hl2.tmp)
# =================================================================================================

def _block(progress: float, body: bytes, *, guard: bool = True) -> bytes:
    header = b"+header\x00" + bytes([1]) + struct.pack("<f", progress) + b"-header\x00"
    return header + body + (b"\xba\xdc\xcd\xab" if guard else b"")


#: A field-stream record that walks completely: `short size=3; short token=0; b"abc"`.
_COMPLETE_FIELD_STREAM = struct.pack("<HH", 3, 0) + b"abc"


def _build_hl2_tmp() -> bytes:
    preamble = b"vampire\x00maps/sp_taxiride.bsp\x00pier\x00"
    block_a = _block(0.0, preamble + zlib.compress(_COMPLETE_FIELD_STREAM))
    block_b = _block(0.5, b"\x00" * 16)                          # all-zero body: padding-zero
    block_c = _block(0.75, b"\x01\x02\x03garbage!!")              # mid-file, no zlib: block-body-content
    block_d = _block(1.0, b"\x04\x05\x06trailing!!", guard=False)  # last block, no guard: trailing-fill
    return block_a + block_b + block_c + block_d


HL2_TMP = _build_hl2_tmp()


def test_hl2_tmp_declares_identity_residue():
    _, _, closure, model = _decode({"hl2.tmp": HL2_TMP}, "hl2.tmp")
    document, binary = exporter.build_document(model)
    identity = document["extensions"][ENGINE_CONFIG_EXTENSION]["identity"]
    assert identity["residue"] is True


def test_the_preamble_strings_are_read_before_the_zlib_body():
    _, _, _, model = _decode({"hl2.tmp": HL2_TMP}, "hl2.tmp")
    block_a = model.save_fragment["blocks"][0]
    texts = [row["text"] for row in block_a["strings"]]
    assert texts == ["vampire", "maps/sp_taxiride.bsp", "pier"]


def test_the_zlib_span_is_derived_and_walked_with_formats_sav():
    _, _, _, model = _decode({"hl2.tmp": HL2_TMP}, "hl2.tmp")
    block_a = model.save_fragment["blocks"][0]
    assert block_a["zlib"]["complete"] is True
    assert block_a["zlib"]["fieldsWalked"] == 1
    assert block_a["zlib"]["consumedBytes"] == len(_COMPLETE_FIELD_STREAM)
    assert model.ledger_row["stateBytes"]["derived"] == block_a["zlib"]["length"]


#: One legitimate field record (`short size=3; short token=0; b"abc"`) followed by four bytes
#: that do not form another complete record: `sav.read_fields` stops there, so the walk never
#: reaches the end of the inflated stream.
_INCOMPLETE_FIELD_STREAM = struct.pack("<HH", 3, 0) + b"abc" + b"\xff\xff\xff\xff"


def test_a_zlib_span_whose_walk_does_not_complete_is_typed_unidentified_not_derived():
    """`derived` applies only where the save decoder both
    inflates *and walks* the span; an incomplete walk represents none of the inflated bytes, so
    the compressed span itself is carried as `typedUnidentified` instead."""

    preamble = b"vampire\x00maps/sp_taxiride.bsp\x00pier\x00"
    body = preamble + zlib.compress(_INCOMPLETE_FIELD_STREAM)
    data = _block(0.0, body)
    _, _, _, model = _decode({"hl2.tmp": data}, "hl2.tmp")
    block = model.save_fragment["blocks"][0]
    assert block["zlib"]["complete"] is False
    assert block["zlib"]["consumedBytes"] < block["zlib"]["inflatedLength"]
    row = next(r for r in model.typed_unidentified if r["reason"] == "block-zlib-span-incomplete-walk")
    assert row["offset"] == block["zlib"]["offset"]
    assert row["length"] == block["zlib"]["length"]
    assert "hex" in row and "sha256" in row
    assert model.ledger_row["stateBytes"].get("derived", 0) == 0
    assert model.ledger_row["coveragePercent"] == 100.0


def test_the_map_string_is_a_map_dependency():
    _, _, _, model = _decode({"hl2.tmp": HL2_TMP}, "hl2.tmp")
    dep = next(d for d in model.dependencies if d["role"] == "map")
    assert dep["asset"] == "vtmb:map:sp_taxiride"
    assert dep["sourcePath"] == "maps/sp_taxiride.bsp"
    assert dep["resolved"] is False


def test_an_all_zero_block_body_is_a_verified_padding_zero_claim():
    _, _, _, model = _decode({"hl2.tmp": HL2_TMP}, "hl2.tmp")
    assert model.ledger_row["stateBytes"]["padding-zero"] >= 16


def test_a_non_zero_block_body_with_no_zlib_is_typed_unidentified_with_its_digest():
    _, _, _, model = _decode({"hl2.tmp": HL2_TMP}, "hl2.tmp")
    reasons = {row["reason"] for row in model.typed_unidentified}
    assert "block-body-content" in reasons
    for row in model.typed_unidentified:
        assert set(row) == {"offset", "length", "sha256", "hex", "reason"}
        chunk = HL2_TMP[row["offset"]:row["offset"] + row["length"]]
        assert row["hex"] == chunk.hex()


def test_the_trailing_allocation_past_the_last_unguarded_block_is_omitted_proven():
    _, _, _, model = _decode({"hl2.tmp": HL2_TMP}, "hl2.tmp")
    assert any(
        row["role"] == "trailing-fill" and row.get("length") == len(b"\x04\x05\x06trailing!!")
        for row in model.omissions
    )
    assert model.ledger_row["stateBytes"].get("omitted-proven", 0) >= len(b"\x04\x05\x06trailing!!")


def test_every_byte_of_hl2_tmp_is_claimed_exactly_once():
    _, _, _, model = _decode({"hl2.tmp": HL2_TMP}, "hl2.tmp")
    row = model.ledger_row
    assert row["byteLength"] == len(HL2_TMP) == row["accountedBytes"]
    assert row["coveragePercent"] == 100.0
    cursor = 0
    for entry in row["ranges"]:
        assert entry["offset"] == cursor
        cursor += entry["length"]
    assert cursor == len(HL2_TMP)
    assert model.unresolved == [] and model.unsupported == []


def test_a_corrupt_zlib_signature_falls_back_to_typed_unidentified():
    """A middle block's corrupt/no-zlib body is `block-body-content`, not `trailing-fill` --
    that grading is reserved for the last, unguarded block (see the save-fragment tests above)."""

    preamble = b"nomapname\x00"
    corrupt = preamble + b"\x78\x9c" + b"not actually zlib data at all"
    block = _block(0.0, corrupt)
    trailing = _block(1.0, b"")
    _, _, _, model = _decode({"hl2.tmp": block + trailing}, "hl2.tmp")
    assert model.save_fragment["blocks"][0]["zlib"] is None
    assert model.ledger_row["coveragePercent"] == 100.0
    assert any(row["reason"] == "block-body-content" for row in model.typed_unidentified)


# =================================================================================================
# build_document / validation
# =================================================================================================

def test_build_document_opens_the_extension_root_with_the_contract_key_order():
    _, _, _, model = _decode({USER_CFG_PATH: USER_CFG}, USER_CFG_PATH)
    document, binary = exporter.build_document(model)
    assert binary == b""
    root = document["extensions"][ENGINE_CONFIG_EXTENSION]
    assert list(root)[:5] == ["schemaVersion", "identity", "sourceResolution", "dependencies", "coverage"]
    assert document["extensionsUsed"] == document["extensionsRequired"] == [ENGINE_CONFIG_EXTENSION]
    assert document["asset"]["generator"] == "Elysium Engine-config GLB Exporter"


def test_every_grammar_key_is_always_present_and_exactly_one_is_non_null():
    _, _, _, model = _decode({"lights.rad": LIGHTS_RAD}, "lights.rad")
    document, binary = exporter.build_document(model)
    root = document["extensions"][ENGINE_CONFIG_EXTENSION]
    grammar_keys = (
        "commands", "bindings", "aliases", "cvars", "scriptExpressions", "textureLights",
        "detailTypes", "maps", "packer", "packerKeys", "categories", "binary", "saveFragment",
    )
    non_null = [key for key in grammar_keys if root[key] is not None]
    assert non_null == ["textureLights"]


def test_packer_keys_are_published_and_the_ledger_owner_they_are_claimed_under_resolves():
    _, _, _, model = _decode({"pack_values.txt": PACK_VALUES}, "pack_values.txt")
    document, binary = exporter.build_document(model)
    root = document["extensions"][ENGINE_CONFIG_EXTENSION]
    assert root["packerKeys"] == model.packer_keys
    assert {entry["owner"] for entry in root["coverage"]["byteLedger"][0]["ranges"]} >= {
        f"packerKeys[{row['index']}]" for row in model.packer_keys
    }


def test_coverage_mapped_names_the_grammars_own_populated_tables():
    _, _, _, lights_model = _decode({"lights.rad": LIGHTS_RAD}, "lights.rad")
    lights_document, _ = exporter.build_document(lights_model)
    lights_mapped = lights_document["extensions"][ENGINE_CONFIG_EXTENSION]["coverage"]["mapped"]
    assert "textureLights" in lights_mapped
    assert "commands" not in lights_mapped

    _, _, _, cfg_model = _decode({USER_CFG_PATH: USER_CFG}, USER_CFG_PATH)
    cfg_document, _ = exporter.build_document(cfg_model)
    cfg_mapped = cfg_document["extensions"][ENGINE_CONFIG_EXTENSION]["coverage"]["mapped"]
    assert "commands" in cfg_mapped and "bindings" in cfg_mapped
    assert "textureLights" not in cfg_mapped


def test_a_well_formed_console_script_passes_validation(tmp_path):
    destination, summary = _export_and_validate({USER_CFG_PATH: USER_CFG, "cfg/autoexec.cfg": b"x"}, USER_CFG_PATH, tmp_path)
    assert summary["asset"] == "vtmb:engine-config:cfg/user.cfg"
    assert summary["grammar"] == "console-script"
    assert summary["byteCoveragePercent"] == 100.0


def test_validation_rejects_a_tampered_byte_ledger_range():
    _, _, closure, model = _decode({"pack_values.txt": PACK_VALUES}, "pack_values.txt")
    document, binary = exporter.build_document(model)
    ranges = document["extensions"][ENGINE_CONFIG_EXTENSION]["coverage"]["byteLedger"][0]["ranges"]
    ranges[0]["length"] += 1
    with pytest.raises(UnitValidationError):
        validation.validate_document(document, binary, source_members=closure.members())


def test_validation_rejects_a_table_row_that_disagrees_with_an_independent_redecode():
    """A tampered `cvars[]` row is invisible to the byte ledger (no bytes moved) but must still
    fail the independent re-decode the contract requires."""

    _, _, closure, model = _decode({USER_CFG_PATH: USER_CFG}, USER_CFG_PATH)
    document, binary = exporter.build_document(model)
    root = document["extensions"][ENGINE_CONFIG_EXTENSION]
    root["cvars"][0]["value"] = "tampered"
    with pytest.raises(EngineConfigGlbValidationError):
        validation.validate_document(document, binary, source_members=closure.members())


def test_validation_rejects_a_grammar_that_declares_the_wrong_key_non_null():
    _, _, closure, model = _decode({"lights.rad": LIGHTS_RAD}, "lights.rad")
    document, binary = exporter.build_document(model)
    root = document["extensions"][ENGINE_CONFIG_EXTENSION]
    root["maps"] = []
    with pytest.raises(EngineConfigGlbValidationError):
        validation.validate_document(document, binary, source_members=closure.members())


def test_validation_rejects_an_unknown_dependency_role():
    _, _, closure, model = _decode({"lights.rad": LIGHTS_RAD}, "lights.rad")
    document, binary = exporter.build_document(model)
    document["extensions"][ENGINE_CONFIG_EXTENSION]["dependencies"].append(
        {"role": "not-a-real-role", "asset": "vtmb:material:x", "sourcePath": "x", "resolved": False}
    )
    with pytest.raises(EngineConfigGlbValidationError):
        validation.validate_document(document, binary, source_members=closure.members())


def test_a_missing_member_is_refused():
    index, read_bytes = _fake_install({})
    with pytest.raises(EngineConfigSourceError):
        load_source_closure(index, "cfg/user.cfg", read_bytes=read_bytes)


@pytest.mark.parametrize(
    "path,text",
    [
        (USER_CFG_PATH, USER_CFG),
        ("lights.rad", LIGHTS_RAD),
        ("detail.vbsp", DETAIL_VBSP_WELL_FORMED),
        ("maps/loadorder.txt", LOADORDER),
        ("pack_values.txt", PACK_VALUES),
        ("localized_list.txt", LOCALIZED_LIST),
        ("vidcfg.bin", VIDCFG_BIN),
        ("voice_ban.dt", struct.pack("<i", 1)),
        ("hl2.tmp", HL2_TMP),
    ],
)
def test_every_grammar_round_trips_through_export_and_standalone_validation(path, text, tmp_path):
    destination, summary = _export_and_validate({path: text}, path, tmp_path / path.replace("/", "_"))
    assert summary["byteCoveragePercent"] == 100.0
    assert destination.exists()
    # Re-reading and re-validating from the file alone (no install) agrees with the export-time
    # summary -- the standalone half of the export/validate split.
    again = validation.validate(destination)
    assert again == summary


def test_write_glb_then_validate_agree_on_a_hand_built_document(tmp_path):
    _, _, closure, model = _decode({"maps/loadorder.txt": LOADORDER}, "maps/loadorder.txt")
    document, binary = exporter.build_document(model)
    destination = tmp_path / "loadorder.txt.glb"
    write_glb(document, binary, destination)
    summary = validation.validate(destination)
    assert summary["grammar"] == "line-list"


def test_export_is_deterministic_for_the_same_source_bytes(tmp_path):
    index, read_bytes = _fake_install({USER_CFG_PATH: USER_CFG, "cfg/autoexec.cfg": b"x"})
    first = exporter.export(index, USER_CFG_PATH, tmp_path / "a", read_bytes=read_bytes)
    second = exporter.export(index, USER_CFG_PATH, tmp_path / "b", read_bytes=read_bytes)
    assert first.read_bytes() == second.read_bytes()
