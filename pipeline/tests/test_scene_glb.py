"""Synthetic contract tests for the isolated Scene GLB exporter.

Every `.vcd` here is hand-built, never read from a real install: `INDEX`/`read_bytes` stand up a
fake install the way `formats.install.build_index`/`read` would answer it.
"""

from __future__ import annotations

import re
from pathlib import Path

import pytest

from elysium_pipeline.exporters import scene_glb as exporter
from elysium_pipeline.formats.scene_glb import decode, lexer, model, source
from elysium_pipeline.formats.unit_contract import ROOT_KEYS, container
from elysium_pipeline.validation import scene_glb as validation


def _crlf(text: bytes) -> bytes:
    """Normalize to CRLF regardless of whether `text` already uses it, so a literal written
    either way is safe to pass here."""

    return text.replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")


def _install(key: str, data: bytes | None, **extra: bytes) -> tuple[dict, callable]:
    """One fake install: `key` (a scene key, with or without `sound/`/`.vcd`) plus any extra
    install-relative members `read_bytes` should also answer."""

    source_path = "sound/" + model.normalize_scene_key(key) + ".vcd"
    index: dict[str, tuple] = {}
    members: dict[str, bytes] = {}
    if data is not None:
        index[source_path] = ("loose", "/fake/" + source_path)
        members[source_path] = data
    for extra_path, extra_data in extra.items():
        index[extra_path] = ("loose", "/fake/" + extra_path)
        members[extra_path] = extra_data

    def read_bytes(idx: dict, k: str) -> bytes | None:
        return members.get(k)

    return index, read_bytes


BASIC_VCD = _crlf(b"""// Choreo version 1
actor "Jack"
{
  channel "Speech"
  {
    event speak "NPC Line"
    {
      time 0.000000 4.870386
      param "character/dlg/main characters/jack_tutorial/line191_col_e.wav"
      param2 "70dB"
      fixedlength
    }
    event silence "sil1"
    {
      time 1.110000 1.700000
      param "0.590"
    }
  }
  channel "Scripts"
  {
    event python "Sabbat"
    {
      time 0.680000 -1.000000
      param "OnPreRaidSounds()"
      param2 "OnPreRaidSounds()"
    }
  }
  channel "snarl 1"
  {
    event expression "Snarl 01"
    {
      time 1.286666 3.693333
      param "smiling_jack_expressions"
      param2 "Snarl"
      event_ramp
      {
        0.6862 0.4192
        1.1448 1.0000
      }
    }
  }
  bonerename "Bip01" "Bip01"
}

fps 60
snap off
""")

BASIC_KEY = "character/dlg/main characters/jack_tutorial/line191_col_e"


def _export(key: str, data: bytes, output_root: Path, **extra: bytes) -> Path:
    index, read_bytes = _install(key, data, **extra)
    return exporter.export(index, key, output_root, read_bytes=read_bytes)


# --- identity and key normalization -------------------------------------------------------------


def test_the_key_folds_case_and_tolerates_the_root_prefix_and_extension():
    assert model.normalize_scene_key(BASIC_KEY) == BASIC_KEY
    assert model.normalize_scene_key("SOUND/" + BASIC_KEY.upper() + ".VCD") == BASIC_KEY
    assert model.normalize_scene_key(BASIC_KEY.replace("/", "\\") + ".vcd") == BASIC_KEY
    assert model.asset_id(BASIC_KEY) == "vtmb:scene:" + BASIC_KEY
    assert model.output_relative_path(BASIC_KEY).as_posix() == BASIC_KEY + ".glb"


def test_an_invalid_key_is_refused():
    with pytest.raises(ValueError):
        model.normalize_scene_key("../escape")


def test_source_keys_lists_every_vcd_below_sound_as_an_export_ready_key():
    index = {
        "sound/a/b.vcd": ("loose", "/x"),
        "sound/a/b.wav": ("loose", "/y"),
        "sound/c.vcd": ("loose", "/z"),
        "expressions/a.vfe": ("loose", "/w"),
    }
    assert exporter.source_keys(index) == ["a/b", "c"]


def test_a_missing_member_is_refused_before_any_decode():
    index, read_bytes = _install(BASIC_KEY, None)
    with pytest.raises(source.SceneSourceError):
        source.load_source_closure(index, BASIC_KEY, read_bytes=read_bytes)


# --- container / extension-root shape -----------------------------------------------------------


def test_a_scene_unit_is_scene_less_and_its_bin_chunk_is_the_source_capsule(tmp_path):
    dest = _export(BASIC_KEY, BASIC_VCD, tmp_path)
    document, binary = container.read_glb(dest)
    # Schema 1.1.0: no accessor, and the whole BIN chunk is the `.vcd` member's own bytes
    # (`read_glb` hands back the chunk as stored, so up to three bytes of container padding
    # follow the capsule).
    assert binary[:len(BASIC_VCD)] == BASIC_VCD
    assert len(binary) - len(BASIC_VCD) < 4
    assert "accessors" not in document
    assert document["buffers"] == [{"byteLength": len(BASIC_VCD)}]
    assert document["bufferViews"] == [
        {"buffer": 0, "byteOffset": 0, "byteLength": len(BASIC_VCD)}
    ]
    resolution = document["extensions"]["ELYSIUM_vtmb_scene"]["sourceResolution"]
    assert resolution["capsule"] == {"encoding": "raw"}
    assert resolution["members"][0]["capsule"] == {
        "bufferView": 0, "byteLength": len(BASIC_VCD)
    }
    for forbidden in ("scenes", "nodes", "meshes", "images", "textures", "samplers", "animations", "skins"):
        assert forbidden not in document
    assert document["asset"]["generator"] == "Elysium Scene GLB Exporter"
    assert document["extensionsUsed"] == ["ELYSIUM_vtmb_scene"]
    assert document["extensionsRequired"] == ["ELYSIUM_vtmb_scene"]
    root = document["extensions"]["ELYSIUM_vtmb_scene"]
    assert tuple(list(root)[: len(ROOT_KEYS)]) == ROOT_KEYS
    assert list(root)[len(ROOT_KEYS):] == [
        "version", "fps", "snap", "actors", "scriptExpressions", "comments", "anomalies", "omissions",
    ]


def test_the_output_path_sits_below_the_family_root_with_no_extra_prefix(tmp_path):
    dest = _export(BASIC_KEY, BASIC_VCD, tmp_path)
    assert dest == tmp_path / Path(BASIC_KEY + ".glb")


# --- byte ledger: every byte claimed exactly once, and only in the states this seam uses --------


def test_every_byte_of_the_member_is_claimed_exactly_once(tmp_path):
    dest = _export(BASIC_KEY, BASIC_VCD, tmp_path)
    document, _ = container.read_glb(dest)
    root = document["extensions"]["ELYSIUM_vtmb_scene"]
    row = root["coverage"]["byteLedger"][0]
    assert row["byteLength"] == len(BASIC_VCD) == row["accountedBytes"]
    assert row["coveragePercent"] == 100.0
    ranges = row["ranges"]
    cursor = 0
    for entry in ranges:
        assert entry["offset"] == cursor
        cursor += entry["length"]
    assert cursor == len(BASIC_VCD)


def test_the_scene_ledger_never_claims_a_zero_state():
    """A `.vcd` is text with no alignment fill and no declared-zero field, so nothing here should
    ever need `reserved-zero` / `padding-zero` -- unlike a binary seam, this decode has no claim
    that a zero-state check could falsely justify."""

    index, read_bytes = _install(BASIC_KEY, BASIC_VCD)
    closure = source.load_source_closure(index, BASIC_KEY, read_bytes=read_bytes)
    model_ = decode.decode_scene(closure)
    states = set(model_.byte_ledger[0]["stateBytes"])
    assert states <= {"mapped-text", "omitted-proven"}


def test_whitespace_is_claimed_as_omitted_proven_and_words_as_mapped_text():
    index, read_bytes = _install(BASIC_KEY, BASIC_VCD)
    closure = source.load_source_closure(index, BASIC_KEY, read_bytes=read_bytes)
    model_ = decode.decode_scene(closure)
    ranges = model_.byte_ledger[0]["ranges"]
    owners_by_state = {}
    for entry in ranges:
        owners_by_state.setdefault(entry["state"], set()).add(entry["owner"])
    assert owners_by_state["omitted-proven"] == {"whitespace"}
    assert "header.version" in owners_by_state["mapped-text"]


def test_insignificant_whitespace_is_recorded_as_omitted_proven_evidence(tmp_path):
    """The `omitted-proven` byte state is not just a ledger label: the contract requires it be
    backed by a row a reader can find, in `omissions[]` and in `coverage.omittedProven` alike."""

    dest = _export(BASIC_KEY, BASIC_VCD, tmp_path)
    document, _ = container.read_glb(dest)
    root = document["extensions"]["ELYSIUM_vtmb_scene"]
    row = root["coverage"]["byteLedger"][0]
    whitespace_bytes = sum(
        entry["length"] for entry in row["ranges"] if entry["state"] == "omitted-proven"
    )
    assert whitespace_bytes > 0
    match = next(r for r in root["omissions"] if r["role"] == "insignificant-whitespace")
    assert match["bytes"] == whitespace_bytes
    assert match in root["coverage"]["omittedProven"]


# --- version, fps, snap ---------------------------------------------------------------------------


def test_the_version_header_is_read_and_excluded_from_the_generic_comment_list(tmp_path):
    dest = _export(BASIC_KEY, BASIC_VCD, tmp_path)
    summary = validation.validate(dest)
    assert summary["version"] == 1
    assert summary["comments"] == 0


def test_a_file_with_no_version_line_publishes_null_and_an_anomaly(tmp_path):
    vcd = _crlf(b'actor "A"\r\n{\r\n}\r\nfps 60\r\nsnap off\r\n')
    dest = _export("test/no-version", vcd, tmp_path)
    summary = validation.validate(dest)
    assert summary["version"] is None
    assert "missing-version-line" in summary["anomalies"]


def test_snap_on_is_a_named_anomaly(tmp_path):
    vcd = _crlf(b'// Choreo version 1\r\nactor "A"\r\n{\r\n}\r\nfps 60\r\nsnap on\r\n')
    dest = _export("test/snap-on", vcd, tmp_path)
    summary = validation.validate(dest)
    assert "snap-on" in summary["anomalies"]


def test_an_ordinary_comment_is_carried_and_counted():
    vcd = _crlf(
        b'// Choreo version 1\r\nactor "A"\r\n{\r\n  // a note\r\n  channel "C"\r\n  {\r\n  }\r\n}\r\n'
        b'fps 60\r\nsnap off\r\n'
    )
    index, read_bytes = _install("test/comment", vcd)
    closure = source.load_source_closure(index, "test/comment", read_bytes=read_bytes)
    model_ = decode.decode_scene(closure)
    assert len(model_.comments) == 1
    assert model_.comments[0]["text"] == "// a note"


# --- actor / channel structure -------------------------------------------------------------------


def test_inactive_actor_and_channel_blocks_are_named_anomalies(tmp_path):
    vcd = _crlf(
        b'// Choreo version 1\r\n'
        b'actor "A"\r\n{\r\n  active 0\r\n  channel "C"\r\n  {\r\n    active 0\r\n  }\r\n}\r\n'
        b'fps 60\r\nsnap off\r\n'
    )
    dest = _export("test/inactive", vcd, tmp_path)
    summary = validation.validate(dest)
    assert summary["anomalies"].count("inactive-block") == 2


def test_bonerename_and_faceposermodel_round_trip(tmp_path):
    vcd = _crlf(
        b'// Choreo version 1\r\n'
        b'actor "A"\r\n{\r\n'
        b'  faceposermodel "models/a.mdl"\r\n'
        b'  bonerename "Bip01" "Bip02"\r\n'
        b'  channel "C"\r\n  {\r\n  }\r\n'
        b'}\r\nfps 60\r\nsnap off\r\n'
    )
    dest = _export("test/bones", vcd, tmp_path)
    document, _ = container.read_glb(dest)
    actor = document["extensions"]["ELYSIUM_vtmb_scene"]["actors"][0]
    assert actor["facePoserModel"] == "models/a.mdl"
    assert actor["boneRenames"] == [{"from": "Bip01", "to": "Bip02", "offset": actor["boneRenames"][0]["offset"]}]


def test_a_word_outside_both_vocabularies_is_unsupported_and_fails_the_unit(tmp_path):
    vcd = _crlf(
        b'// Choreo version 1\r\n'
        b'actor "A"\r\n{\r\n  mystery "x"\r\n  channel "C"\r\n  {\r\n  }\r\n}\r\n'
        b'fps 60\r\nsnap off\r\n'
    )
    with pytest.raises(validation.SceneGlbValidationError, match="incomplete"):
        _export("test/unsupported-actor-word", vcd, tmp_path)


def test_a_recognised_but_unused_token_is_carried_verbatim_as_extras(tmp_path):
    vcd = _crlf(
        b'// Choreo version 1\r\n'
        b'actor "A"\r\n{\r\n  channel "C"\r\n  {\r\n'
        b'    event speak "s"\r\n    {\r\n      time 0 1\r\n      param "a/b.wav"\r\n'
        b'      tags "one" "two"\r\n'
        b'    }\r\n  }\r\n}\r\nfps 60\r\nsnap off\r\n'
    )
    dest = _export("test/extras", vcd, tmp_path)
    document, _ = container.read_glb(dest)
    event = document["extensions"]["ELYSIUM_vtmb_scene"]["actors"][0]["channels"][0]["events"][0]
    assert event["extras"] == [{"token": "tags", "words": ["one", "two"], "offset": event["extras"][0]["offset"], "block": None}]
    # the event itself is still complete: an unresolved sound target warns, it does not fail
    summary = validation.validate(dest)
    assert summary["anomalies"] == []


# --- event per-type fields -------------------------------------------------------------------------


def test_speak_records_mp3_first_resolution_as_a_sound_dependency(tmp_path):
    dest = _export(BASIC_KEY, BASIC_VCD, tmp_path)
    document, _ = container.read_glb(dest)
    root = document["extensions"]["ELYSIUM_vtmb_scene"]
    event = root["actors"][0]["channels"][0]["events"][0]
    assert event["type"] == "speak" and event["typeId"] == 5
    dep = root["dependencies"][event["sound"]]
    assert dep == {
        "role": "sound",
        "asset": "vtmb:sound:character/dlg/main characters/jack_tutorial/line191_col_e.mp3",
        "sourcePath": "sound/character/dlg/main characters/jack_tutorial/line191_col_e.wav",
        "resolved": False,
        "resolution": "mp3-first",
    }
    assert event["level"] == 70.0


def test_speak_resolves_the_mp3_when_the_install_holds_it(tmp_path):
    dest = _export(
        BASIC_KEY, BASIC_VCD, tmp_path,
        **{"sound/character/dlg/main characters/jack_tutorial/line191_col_e.mp3": b"id3"},
    )
    document, _ = container.read_glb(dest)
    root = document["extensions"]["ELYSIUM_vtmb_scene"]
    event = root["actors"][0]["channels"][0]["events"][0]
    dep = root["dependencies"][event["sound"]]
    assert dep["resolved"] is True
    summary = validation.validate(dest)
    assert dep["sourcePath"] not in summary["unresolvedDependencies"]


def test_two_speak_events_naming_the_same_path_share_one_dependency_row(tmp_path):
    vcd = _crlf(
        b'// Choreo version 1\r\n'
        b'actor "A"\r\n{\r\n  channel "C"\r\n  {\r\n'
        b'    event speak "s1"\r\n    {\r\n      time 0 1\r\n      param "a/b.wav"\r\n      param2 "70dB"\r\n    }\r\n'
        b'    event speak "s2"\r\n    {\r\n      time 1 2\r\n      param "a/b.wav"\r\n      param2 "70dB"\r\n    }\r\n'
        b'  }\r\n}\r\nfps 60\r\nsnap off\r\n'
    )
    dest = _export("test/dedup", vcd, tmp_path)
    document, _ = container.read_glb(dest)
    root = document["extensions"]["ELYSIUM_vtmb_scene"]
    events = root["actors"][0]["channels"][0]["events"]
    assert len(root["dependencies"]) == 1
    assert events[0]["sound"] == events[1]["sound"] == 0


def test_bodysound_defaults_and_floors_its_level(tmp_path):
    vcd = _crlf(
        b'// Choreo version 1\r\n'
        b'actor "A"\r\n{\r\n  channel "C"\r\n  {\r\n'
        b'    event bodysound "h1"\r\n    {\r\n      time 0 1\r\n      param "amb/horn.wav"\r\n    }\r\n'
        b'    event bodysound "h2"\r\n    {\r\n      time 1 2\r\n      param "amb/horn2.wav"\r\n      param2 "10"\r\n    }\r\n'
        b'  }\r\n}\r\nfps 60\r\nsnap off\r\n'
    )
    dest = _export("test/bodysound", vcd, tmp_path)
    document, _ = container.read_glb(dest)
    events = document["extensions"]["ELYSIUM_vtmb_scene"]["actors"][0]["channels"][0]["events"]
    assert events[0]["level"] == 80.0          # no param2 -> default
    assert events[1]["level"] == 75.0          # "10" floored at 75


def test_expression_and_flexanimation_carry_the_stem_and_declare_a_table_dependency(tmp_path):
    dest = _export(BASIC_KEY, BASIC_VCD, tmp_path)
    document, _ = container.read_glb(dest)
    root = document["extensions"]["ELYSIUM_vtmb_scene"]
    event = root["actors"][0]["channels"][2]["events"][0]
    assert event["type"] == "expression"
    assert event["expressionTable"] == "smiling_jack_expressions"
    assert event["expressionName"] == "Snarl"
    assert {"role": "expression-table", "asset": "vtmb:expression-table:smiling_jack_expressions",
            "sourcePath": "expressions/smiling_jack_expressions.vfe",
            "resolved": False} in root["dependencies"]
    assert event["ramp"] == [
        {"time": 0.6862, "value": 0.4192, "offset": event["ramp"][0]["offset"]},
        {"time": 1.1448, "value": 1.0, "offset": event["ramp"][1]["offset"]},
    ]


def test_expression_table_resolves_against_either_vfe_or_txt(tmp_path):
    dest = _export(
        BASIC_KEY, BASIC_VCD, tmp_path,
        **{"expressions/smiling_jack_expressions.txt": b"$keys"},
    )
    document, _ = container.read_glb(dest)
    root = document["extensions"]["ELYSIUM_vtmb_scene"]
    row = next(r for r in root["dependencies"] if r["role"] == "expression-table")
    assert row["resolved"] is True


def test_gesture_and_sequence_carry_the_clip_label_with_no_dependency(tmp_path):
    vcd = _crlf(
        b'// Choreo version 1\r\n'
        b'actor "A"\r\n{\r\n  channel "C"\r\n  {\r\n'
        b'    event gesture "g"\r\n    {\r\n      time 0 1\r\n      param "wave_seq"\r\n      sequenceduration 2.5\r\n    }\r\n'
        b'    event sequence "seq"\r\n    {\r\n      time 0 10\r\n      param "entire_scene"\r\n    }\r\n'
        b'  }\r\n}\r\nfps 60\r\nsnap off\r\n'
    )
    dest = _export("test/clips", vcd, tmp_path)
    document, _ = container.read_glb(dest)
    root = document["extensions"]["ELYSIUM_vtmb_scene"]
    events = root["actors"][0]["channels"][0]["events"]
    assert events[0]["clipLabel"] == "wave_seq" and events[0]["sequenceDuration"] == 2.5
    assert events[1]["clipLabel"] == "entire_scene"
    assert root["dependencies"] == []


def test_firetrigger_parses_atoi_style(tmp_path):
    vcd = _crlf(
        b'// Choreo version 1\r\n'
        b'actor "A"\r\n{\r\n  channel "C"\r\n  {\r\n'
        b'    event firetrigger "t"\r\n    {\r\n      time 0 -1\r\n      param "3garbage"\r\n    }\r\n'
        b'  }\r\n}\r\nfps 60\r\nsnap off\r\n'
    )
    dest = _export("test/trigger", vcd, tmp_path)
    document, _ = container.read_glb(dest)
    event = document["extensions"]["ELYSIUM_vtmb_scene"]["actors"][0]["channels"][0]["events"][0]
    assert event["trigger"] == 3


def test_python_events_are_carried_verbatim_and_indexed(tmp_path):
    dest = _export(BASIC_KEY, BASIC_VCD, tmp_path)
    document, _ = container.read_glb(dest)
    root = document["extensions"]["ELYSIUM_vtmb_scene"]
    event = root["actors"][0]["channels"][1]["events"][0]
    assert event["type"] == "python"
    row = root["scriptExpressions"][event["expression"]]
    assert row["param"] == row["param2"] == "OnPreRaidSounds()"


def test_cameramove_lookat_and_face_carry_both_params_as_entity_names(tmp_path):
    vcd = _crlf(
        b'// Choreo version 1\r\n'
        b'actor "A"\r\n{\r\n  channel "C"\r\n  {\r\n'
        b'    event cameramove "m"\r\n    {\r\n      time 0 -1\r\n      param "cam1"\r\n      param2 "targ1"\r\n    }\r\n'
        b'  }\r\n}\r\nfps 60\r\nsnap off\r\n'
    )
    dest = _export("test/cameramove", vcd, tmp_path)
    document, _ = container.read_glb(dest)
    event = document["extensions"]["ELYSIUM_vtmb_scene"]["actors"][0]["channels"][0]["events"][0]
    assert event["entityNames"] == ["cam1", "targ1"]


def test_camerashot_is_decoded_like_any_event_and_carries_unhandled(tmp_path):
    vcd = _crlf(
        b'// Choreo version 1\r\n'
        b'actor "A"\r\n{\r\n  channel "C"\r\n  {\r\n'
        b'    event camerashot "s"\r\n    {\r\n      time 0 -1\r\n      param "shot1"\r\n    }\r\n'
        b'  }\r\n}\r\nfps 60\r\nsnap off\r\n'
    )
    dest = _export("test/camerashot", vcd, tmp_path)
    document, _ = container.read_glb(dest)
    event = document["extensions"]["ELYSIUM_vtmb_scene"]["actors"][0]["channels"][0]["events"][0]
    assert event["typeId"] == 17 and event["unhandled"] is True


def test_silence_and_loud_record_marker_duration_and_flag_a_mismatch(tmp_path):
    vcd = _crlf(
        b'// Choreo version 1\r\n'
        b'actor "A"\r\n{\r\n  channel "C"\r\n  {\r\n'
        b'    event silence "ok"\r\n    {\r\n      time 1.000000 1.500000\r\n      param "0.500"\r\n    }\r\n'
        b'    event loud "bad"\r\n    {\r\n      time 2.000000 2.200000\r\n      param "0.999"\r\n    }\r\n'
        b'  }\r\n}\r\nfps 60\r\nsnap off\r\n'
    )
    dest = _export("test/markers", vcd, tmp_path)
    summary = validation.validate(dest)
    assert summary["anomalies"] == ["marker-duration-mismatch"]


def test_a_degenerate_time_range_is_flagged(tmp_path):
    vcd = _crlf(
        b'// Choreo version 1\r\n'
        b'actor "A"\r\n{\r\n  channel "C"\r\n  {\r\n'
        b'    event section "x"\r\n    {\r\n      time 5.000000 1.000000\r\n    }\r\n'
        b'  }\r\n}\r\nfps 60\r\nsnap off\r\n'
    )
    dest = _export("test/degenerate", vcd, tmp_path)
    summary = validation.validate(dest)
    assert summary["anomalies"] == ["degenerate-time"]


def test_an_instantaneous_event_with_end_negative_one_is_never_degenerate(tmp_path):
    vcd = _crlf(
        b'// Choreo version 1\r\n'
        b'actor "A"\r\n{\r\n  channel "C"\r\n  {\r\n'
        b'    event section "x"\r\n    {\r\n      time 5.000000 -1.000000\r\n    }\r\n'
        b'  }\r\n}\r\nfps 60\r\nsnap off\r\n'
    )
    dest = _export("test/instant", vcd, tmp_path)
    summary = validation.validate(dest)
    assert summary["anomalies"] == []


def test_a_missing_closing_brace_bubbles_misnested_content_to_its_true_owner_instead_of_dropping_it(tmp_path):
    """One shipped `.vcd` is short exactly one closing brace on a channel, which folds the next
    sibling channel, a bonerename and the file's own footer into it under the grammar's single
    word-list/brace rule. Every one of those is still a live token and still belongs somewhere
    real; the decode must route it there (with a named anomaly) rather than call it unsupported."""

    vcd = _crlf(
        b'// Choreo version 1\r\n'
        b'actor "A"\r\n{\r\n'
        b'  channel "Gestures"\r\n  {\r\n'
        b'    event gesture "g"\r\n    {\r\n      time 0 1\r\n      param "clip"\r\n    }\r\n'
        b'  channel "Expressions"\r\n  {\r\n  }\r\n'
        b'  bonerename "Bip01" "Bip01"\r\n'
        b'}\r\n'
        b'fps 60\r\nsnap off\r\n'
    )
    dest = _export("test/missing-brace", vcd, tmp_path)
    document, _ = container.read_glb(dest)
    root = document["extensions"]["ELYSIUM_vtmb_scene"]
    actor = root["actors"][0]
    assert [channel["name"] for channel in actor["channels"]] == ["Gestures", "Expressions"]
    assert actor["boneRenames"] == [
        {"from": "Bip01", "to": "Bip01", "offset": actor["boneRenames"][0]["offset"]}
    ]
    assert root["fps"] == 60 and root["snap"] is False
    summary = validation.validate(dest)
    assert set(summary["anomalies"]) >= {
        "unclosed-block-at-end-of-file", "misnested-channel", "misnested-bonerename",
        "misnested-footer-token",
    }
    assert summary["byteCoveragePercent"] == 100.0


def test_a_misnested_channels_ledger_owner_names_the_slot_it_actually_publishes_at(tmp_path):
    """`decode_channel` reserves a channel's slot before recursing into the content a missing
    brace folded under it, so the record's own `channels[N].line` owner and its published index
    `N` never disagree -- and no two distinct channels ever publish under the same owner path."""

    vcd = _crlf(
        b'// Choreo version 1\r\n'
        b'actor "A"\r\n{\r\n'
        b'  channel "Gestures"\r\n  {\r\n'
        b'    event gesture "g"\r\n    {\r\n      time 0 1\r\n      param "clip"\r\n    }\r\n'
        b'  channel "Expressions"\r\n  {\r\n  }\r\n'
        b'  bonerename "Bip01" "Bip01"\r\n'
        b'}\r\n'
        b'fps 60\r\nsnap off\r\n'
    )
    dest = _export("test/misnested-owner", vcd, tmp_path)
    document, _ = container.read_glb(dest)
    root = document["extensions"]["ELYSIUM_vtmb_scene"]
    ranges = root["coverage"]["byteLedger"][0]["ranges"]
    channel_line_re = re.compile(r"^actors\[0\]\.channels\[\d+\]\.line$")
    line_owners = {
        entry["owner"] for entry in ranges if channel_line_re.match(entry["owner"])
    }
    # Exactly one owner per published channel index, and each is distinct.
    assert line_owners == {"actors[0].channels[0].line", "actors[0].channels[1].line"}


def test_a_footer_folded_under_a_channel_by_a_missing_brace_is_still_recovered(tmp_path):
    """`decode_actor`'s missing-brace recovery for a mis-parented `fps`/`snap` footer has a
    sibling case: the unclosed block can just as well be a channel's own, folding the footer in
    one level deeper. `decode_channel` carries the same recovery branch."""

    vcd = _crlf(
        b'// Choreo version 1\r\n'
        b'actor "A"\r\n{\r\n'
        b'  channel "C"\r\n  {\r\n'
        b'    event gesture "g"\r\n    {\r\n      time 0 1\r\n      param "clip"\r\n    }\r\n'
        b'fps 60\r\nsnap off\r\n'
    )
    dest = _export("test/channel-footer", vcd, tmp_path)
    document, _ = container.read_glb(dest)
    root = document["extensions"]["ELYSIUM_vtmb_scene"]
    assert root["fps"] == 60 and root["snap"] is False
    summary = validation.validate(dest)
    assert "misnested-footer-token" in summary["anomalies"]
    assert summary["byteCoveragePercent"] == 100.0


# --- empty member ---------------------------------------------------------------------------------


def test_an_empty_member_publishes_a_warning_and_an_omission(tmp_path):
    dest = _export("test/empty", b"", tmp_path)
    document, binary = container.read_glb(dest)
    assert binary == b""
    root = document["extensions"]["ELYSIUM_vtmb_scene"]
    assert root["actors"] == [] and root["version"] is None
    assert root["omissions"] == [{"role": "empty-member", "reason": "the member holds zero bytes"}]
    summary = validation.validate(dest)
    assert summary["sourceBytes"] == 0 and summary["byteCoveragePercent"] == 100.0


# --- validation: tampering and independent re-derivation -------------------------------------------


def test_a_tampered_ledger_range_is_rejected_on_read_back(tmp_path):
    dest = _export(BASIC_KEY, BASIC_VCD, tmp_path)
    document, binary = container.read_glb(dest)
    root = document["extensions"]["ELYSIUM_vtmb_scene"]
    root["coverage"]["byteLedger"][0]["ranges"][0]["length"] += 1
    container.write_glb(document, binary, dest)
    with pytest.raises(Exception):
        validation.validate(dest)


def test_a_published_field_that_disagrees_with_the_source_is_rejected_at_export_time():
    index, read_bytes = _install(BASIC_KEY, BASIC_VCD)
    closure = source.load_source_closure(index, BASIC_KEY, read_bytes=read_bytes)
    model_ = decode.decode_scene(closure)
    document, binary = __import__(
        "elysium_pipeline.exporters.scene_glb", fromlist=["build_document"]
    ).build_document(model_)
    document["extensions"]["ELYSIUM_vtmb_scene"]["actors"][0]["name"] = "Tampered"
    with pytest.raises(validation.SceneGlbValidationError, match="disagrees with the source"):
        validation.validate_document(document, binary, source_members=closure.members())


def _basic_document():
    index, read_bytes = _install(BASIC_KEY, BASIC_VCD)
    closure = source.load_source_closure(index, BASIC_KEY, read_bytes=read_bytes)
    model_ = decode.decode_scene(closure)
    document, binary = __import__(
        "elysium_pipeline.exporters.scene_glb", fromlist=["build_document"]
    ).build_document(model_)
    return document, binary, closure


def test_a_tampered_speak_level_is_rejected_at_export_time():
    document, binary, closure = _basic_document()
    event = document["extensions"]["ELYSIUM_vtmb_scene"]["actors"][0]["channels"][0]["events"][0]
    event["level"] = 999.0
    with pytest.raises(validation.SceneGlbValidationError, match="level disagrees"):
        validation.validate_document(document, binary, source_members=closure.members())


def test_a_forged_mp3_first_dependency_asset_is_rejected_at_export_time():
    document, binary, closure = _basic_document()
    root = document["extensions"]["ELYSIUM_vtmb_scene"]
    event = root["actors"][0]["channels"][0]["events"][0]
    dep = root["dependencies"][event["sound"]]
    dep["asset"] = "vtmb:sound:totally/other.wav"
    dep["sourcePath"] = "totally/other.wav"
    with pytest.raises(validation.SceneGlbValidationError, match="sourcePath disagrees|not one of the mp3-first candidates"):
        validation.validate_document(document, binary, source_members=closure.members())


def test_a_tampered_expression_name_is_rejected_at_export_time(tmp_path):
    vcd = _crlf(
        b'// Choreo version 1\r\n'
        b'actor "A"\r\n{\r\n  channel "C"\r\n  {\r\n'
        b'    event expression "e"\r\n    {\r\n      time 0 1\r\n'
        b'      param "smiling_jack_expressions"\r\n      param2 "Snarl"\r\n    }\r\n'
        b'  }\r\n}\r\nfps 60\r\nsnap off\r\n'
    )
    index, read_bytes = _install("test/expr-name", vcd)
    closure = source.load_source_closure(index, "test/expr-name", read_bytes=read_bytes)
    model_ = decode.decode_scene(closure)
    document, binary = __import__(
        "elysium_pipeline.exporters.scene_glb", fromlist=["build_document"]
    ).build_document(model_)
    event = document["extensions"]["ELYSIUM_vtmb_scene"]["actors"][0]["channels"][0]["events"][0]
    event["expressionName"] = "WRONG"
    with pytest.raises(validation.SceneGlbValidationError, match="expressionName disagrees"):
        validation.validate_document(document, binary, source_members=closure.members())


def test_a_tampered_comment_text_is_rejected_at_export_time():
    vcd = _crlf(
        b'// Choreo version 1\r\nactor "A"\r\n{\r\n  // a note\r\n  channel "C"\r\n  {\r\n  }\r\n}\r\n'
        b'fps 60\r\nsnap off\r\n'
    )
    index, read_bytes = _install("test/comment-tamper", vcd)
    closure = source.load_source_closure(index, "test/comment-tamper", read_bytes=read_bytes)
    model_ = decode.decode_scene(closure)
    document, binary = __import__(
        "elysium_pipeline.exporters.scene_glb", fromlist=["build_document"]
    ).build_document(model_)
    root = document["extensions"]["ELYSIUM_vtmb_scene"]
    assert root["comments"] == [{"offset": root["comments"][0]["offset"], "text": "// a note"}]
    root["comments"][0]["text"] = "// FORGED"
    with pytest.raises(validation.SceneGlbValidationError, match="comments disagree"):
        validation.validate_document(document, binary, source_members=closure.members())


def test_an_unknown_anomaly_role_is_rejected():
    index, read_bytes = _install(BASIC_KEY, BASIC_VCD)
    closure = source.load_source_closure(index, BASIC_KEY, read_bytes=read_bytes)
    model_ = decode.decode_scene(closure)
    document, binary = __import__(
        "elysium_pipeline.exporters.scene_glb", fromlist=["build_document"]
    ).build_document(model_)
    document["extensions"]["ELYSIUM_vtmb_scene"]["anomalies"].append({"role": "made-up"})
    with pytest.raises(validation.SceneGlbValidationError, match="unknown source anomaly"):
        validation.validate_document(document, binary)


def test_a_dependency_row_with_no_producing_event_is_rejected():
    index, read_bytes = _install(BASIC_KEY, BASIC_VCD)
    closure = source.load_source_closure(index, BASIC_KEY, read_bytes=read_bytes)
    model_ = decode.decode_scene(closure)
    document, binary = __import__(
        "elysium_pipeline.exporters.scene_glb", fromlist=["build_document"]
    ).build_document(model_)
    document["extensions"]["ELYSIUM_vtmb_scene"]["dependencies"].append(
        {"role": "sound", "asset": "vtmb:sound:orphan.wav", "sourcePath": "orphan.wav", "resolved": False}
    )
    with pytest.raises(validation.SceneGlbValidationError, match="dependency set disagrees"):
        validation.validate_document(document, binary)


# --- full round trip --------------------------------------------------------------------------------


def test_a_published_unit_round_trips_through_standalone_validation(tmp_path):
    dest = _export(BASIC_KEY, BASIC_VCD, tmp_path)
    summary = validation.validate(dest)
    assert summary["asset"] == model.asset_id(BASIC_KEY)
    assert summary["byteCoveragePercent"] == 100.0
    assert summary["accountedBytes"] == summary["sourceBytes"] == len(BASIC_VCD)
    warnings = validation.warnings_for(summary)
    assert any("no member for" in warning for warning in warnings)


def test_export_is_deterministic_for_the_same_source(tmp_path):
    first = _export(BASIC_KEY, BASIC_VCD, tmp_path / "a")
    second = _export(BASIC_KEY, BASIC_VCD, tmp_path / "b")
    assert first.read_bytes() == second.read_bytes()


# --- lexer-level proofs -------------------------------------------------------------------------


def test_the_lexer_parses_a_bare_two_word_ramp_row_with_no_keyword():
    text = "event_ramp\r\n{\r\n  0.5 1.0\r\n}\r\n"
    tokens = [t for t in lexer.tokenize(text) if t.kind in ("word", "open", "close")]
    records, anomalies = lexer.parse_records(text, tokens)
    assert anomalies == []
    assert len(records) == 1
    ramp = records[0]
    assert ramp.keyword == "event_ramp"
    assert len(ramp.children) == 1
    assert [w.text for w in ramp.children[0].words] == ["0.5", "1.0"]


def test_an_unclosed_block_is_a_structural_anomaly_not_a_crash():
    text = 'actor "A"\r\n{\r\n  channel "C"\r\n'
    tokens = [t for t in lexer.tokenize(text) if t.kind in ("word", "open", "close")]
    records, anomalies = lexer.parse_records(text, tokens)
    assert len(records) == 1
    assert anomalies and anomalies[0]["role"] == "unclosed-block-at-end-of-file"
