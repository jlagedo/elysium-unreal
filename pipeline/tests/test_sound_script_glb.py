"""Synthetic contract tests for the isolated Sound-script GLB exporter, all five unit kinds."""

from __future__ import annotations

from pathlib import Path
import tempfile

import pytest

from elysium_pipeline.exporters import sound_script_glb as exporter
from elysium_pipeline.formats.sound_script_glb import decode, source, symbols
from elysium_pipeline.formats.sound_script_glb.model import (
    SOUND_SCRIPT_EXTENSION,
    TABLE_PATHS,
)
from elysium_pipeline.formats.unit_contract import ByteLedgerError, UnitValidationError
from elysium_pipeline.validation import sound_script_glb as validation

# --- synthetic tables ---------------------------------------------------------------------------

GSP_TABLE = (
    b'// live table header\r\n'
    b'"Metal_Barrel.Impact"\r\n{\r\n'
    b'\t"soundlevel"\t"SNDLVL_75dB"\r\n'
    b'\t"volume"\t"0.6"\r\n'
    b'\t"pitch"\t"98,100"\r\n'
    b'\t"channel"\t"CHAN_BODY"\r\n'
    b'\t"rndwave"\r\n\t{\r\n'
    b'\t\t"wave"\t"Surfaces/Metal_Barrel/Impact1.wav"\r\n'
    b'\t\t"wave"\t"Surfaces/Metal_Barrel/Impact2.wav"\r\n'
    b'\t}\r\n'
    b'}\r\n'
    b'"Sentence.Cue"\r\n{\r\n'
    b'\t"wave"\t"!SPI_DIES0"\r\n'
    b'\t"unknownkey"\t"x"\r\n'
    b'}\r\n'
)

SOUNDS_TABLE = (
    b'"Metal_Barrel.Impact"\r\n{\r\n'
    b'\t"wave"\t"stock/different.wav"\r\n'
    b'}\r\n'
    b'"Dormant.Only"\r\n{\r\n'
    b'\t"volume"\t"VOL_NORM"\r\n'
    b'\t"pitch"\t"PITCH_NORM"\r\n'
    b'\t"wave"\t"ambient/dormant.wav"\r\n'
    b'}\r\n'
)

MANIFEST_TABLE = (
    b'game_sounds_manifest\r\n{\r\n'
    b'\t"precache_file"\t\t"scripts/game_sounds_surfaceproperties.txt"\r\n'
    b'\t// commented out\r\n'
    b'\t//"precache_file"\t\t"scripts/sounds.txt"\r\n'
    b'}\r\n'
)

SOUNDSCAPE_TABLE = (
    b'"cabin"\r\n{\r\n'
    b'\t"dsp"\t"17"\r\n'
    b'\t"playlooping"\r\n\t{\r\n'
    b'\t\t"volume"\t"0.5"\r\n'
    b'\t\t"pitch"\t"100"\r\n'
    b'\t\t"attenuation"\t"2"\r\n'
    b'\t\t"wave"\t"temp/cabin_ambience.wav"\r\n'
    b'\t}\r\n'
    b'\t"playrandom"\r\n\t{\r\n'
    b'\t\t"time"\t"0.1, 0.1"\r\n'
    b'\t\t"rndwave"\r\n\t\t{\r\n'
    b'\t\t\t"wave"\t"temp/wall1.wav"\r\n'
    b'\t\t\t"wave"\t"!SPI_DIES1"\r\n'
    b'\t\t}\r\n'
    b'\t}\r\n'
    b'}\r\n'
)

SENTENCE_TABLE = (
    b'// SPEECH SYSTEM SENTENCES.\r\n'
    b'// banner\r\n'
    b'\r\n'
    b'SPI_AGGRO0 character/monster/spiderchick/Scream.wav {Len 0.00}\r\n'
    b'SPI_DIES0 character/monster/spiderchick/Death1.wav {Len 1.50}\r\n'
    b'SPI_DIES1 character/monster/spiderchick/Death1.wav {Len 1.50}\r\n'
    b'MX_SPACED0 character/monster/ming xiao/exert_heavy_1.wav {Len 0.00}\r\n'
    b'C1A0_HL1 barney/,scientists have finally(p95),perhaps unwittingly.,unlocked it\r\n'
)

DSP_TABLE = (
    b'// header\r\n'
    b'// RVA size min #dly feedbk gain cutoff fpar fmod rate w d h fw fd fh ftap\r\n'
    b'{\t0\tLINEAR\t0.2 0.8\t0.0 0.0 70 0.75\r\n'
    b'\t{  0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 }\r\n'
    b'}\r\n'
    b'{\t4\tLINEAR\t0.2 0.8\t0.0 0.0 70 0.75\r\n'
    b'\t// DFR size #dly feedbk\r\n'
    b'\t{  DFR  1.0 3  0.1483 }\r\n'
    b'\t// RVA size min #dly feedbk gain cutoff fpar fmod rate w d h fw fd fh ftap\r\n'
    b'\t{  RVA 100.0 30.0 4  0.95 1.8 4000 1  0 0 0 0 0 0 0 0 0}\r\n'
    b'}\r\n'
    b'{\t5\tLINEAR\t0.2 0.8\t0.0 0.0 70 0.75\r\n'
    b'\t{  DFR  1.0 3  0.1483  9.0 }\r\n'
    b'}\r\n'
    b'{\t6\tLINEAR\t0.2 0.8\t0.0 0.0 70 0.75\r\n'
    b'\t{  ZZZ  1.0 2.0 }\r\n'
    b'}\r\n'
    b'{\t7\tLINEAR\t0.2 0.8\t0.0 0.0 70 0.75\r\n'
    b'\t{  FLT  LP  200  0.5  LO  0.8 }\r\n'
    b'}\r\n'
    b'{\t8\tLINEAR\t0.2 0.8\t0.0 0.0 70 0.75\r\n'
    b'\t{  FLT  XXX  200  0.5  LO  0.8 }\r\n'
    b'}\r\n'
)


def _index() -> dict:
    return {
        TABLE_PATHS["game-sound-live"]: ("loose", "C:/game/Unofficial_Patch/scripts/game_sounds_surfaceproperties.txt"),
        TABLE_PATHS["game-sound-dormant"]: ("vpk", ("C:/game/Vampire/pack008.vpk", 0, len(SOUNDS_TABLE))),
        TABLE_PATHS["manifest"]: ("vpk", ("C:/game/Vampire/pack001.vpk", 0, len(MANIFEST_TABLE))),
        TABLE_PATHS["soundscape"]: ("vpk", ("C:/game/Vampire/pack001.vpk", 100, len(SOUNDSCAPE_TABLE))),
        TABLE_PATHS["sentence"]: ("vpk", ("C:/game/Vampire/pack001.vpk", 200, len(SENTENCE_TABLE))),
        TABLE_PATHS["dsp-preset"]: ("vpk", ("C:/game/Vampire/pack001.vpk", 300, len(DSP_TABLE))),
    }


_BODIES = {
    TABLE_PATHS["game-sound-live"]: GSP_TABLE,
    TABLE_PATHS["game-sound-dormant"]: SOUNDS_TABLE,
    TABLE_PATHS["manifest"]: MANIFEST_TABLE,
    TABLE_PATHS["soundscape"]: SOUNDSCAPE_TABLE,
    TABLE_PATHS["sentence"]: SENTENCE_TABLE,
    TABLE_PATHS["dsp-preset"]: DSP_TABLE,
}


def _read_bytes(index: dict, key: str) -> bytes | None:
    return _BODIES.get(key)


def _game_sound_directory():
    return source.load_game_sound_directory(_index(), read_bytes=_read_bytes)


def _decode_game_sound(name: str, **kwargs):
    closure = source.game_sound_closure(_game_sound_directory(), name)
    return decode.decode_game_sound(closure, **kwargs)


def _decode_soundscape(name: str, **kwargs):
    table = source.load_kv_table(_index(), TABLE_PATHS["soundscape"], read_bytes=_read_bytes)
    closure = source.soundscape_closure(table, name)
    return decode.decode_soundscape(closure, **kwargs)


def _decode_sentence(name: str, **kwargs):
    table = source.load_sentence_table(_index(), read_bytes=_read_bytes)
    closure = source.sentence_closure(table, name)
    return decode.decode_sentence(closure, **kwargs)


def _decode_dsp(preset_id: int):
    table = source.load_dsp_table(_index(), read_bytes=_read_bytes)
    closure = source.dsp_preset_closure(table, preset_id)
    return decode.decode_dsp_preset(closure)


def _publish(model):
    document, binary = exporter.build_document(model)
    return document, binary


# --- table partition proofs ----------------------------------------------------------------------


def test_the_live_game_sound_table_partitions_into_entries_comments_and_whitespace():
    table = source.load_kv_table(_index(), TABLE_PATHS["game-sound-live"], read_bytes=_read_bytes)
    assert table.names == ("metal_barrel.impact", "sentence.cue")


def test_a_name_declared_twice_refuses_the_table():
    with pytest.raises(source.SoundScriptSourceError):
        source.load_kv_table(
            _index(), TABLE_PATHS["game-sound-live"],
            read_bytes=lambda index, key: GSP_TABLE + b'"Metal_Barrel.Impact"\r\n{\r\n}\r\n',
        )


def test_a_token_outside_every_block_refuses_the_table():
    with pytest.raises(source.SoundScriptSourceError):
        source.load_kv_table(
            _index(), TABLE_PATHS["game-sound-live"],
            read_bytes=lambda index, key: GSP_TABLE + b'"stray"\r\n',
        )


def test_the_dormant_table_shadows_a_name_the_live_table_already_owns():
    directory = _game_sound_directory()
    assert directory.shadowed == frozenset({"metal_barrel.impact"})
    assert directory.owner("metal_barrel.impact") == "live"
    assert directory.owner("dormant.only") == "dormant"
    assert set(directory.names) == {"metal_barrel.impact", "sentence.cue", "dormant.only"}


def test_a_shadowed_dormant_entry_is_recorded_as_an_anomaly_on_the_live_unit():
    model = _decode_game_sound("metal_barrel.impact")
    shadow_rows = [row for row in model.anomalies if row["role"] == "shadowed-dormant-entry"]
    assert len(shadow_rows) == 1
    assert shadow_rows[0]["table"] == TABLE_PATHS["game-sound-dormant"]
    assert shadow_rows[0]["sourceName"] == "Metal_Barrel.Impact"
    assert isinstance(shadow_rows[0]["offset"], int) and shadow_rows[0]["length"] > 0
    # a name the dormant table owns outright (no live duplicate) shadows nothing
    assert _decode_game_sound("dormant.only").anomalies == []


def test_the_sentence_table_partitions_by_line():
    table = source.load_sentence_table(_index(), read_bytes=_read_bytes)
    assert set(table.names) == {"spi_aggro0", "spi_dies0", "spi_dies1", "mx_spaced0", "c1a0_hl1"}


def test_the_dsp_table_partitions_by_preset_id():
    table = source.load_dsp_table(_index(), read_bytes=_read_bytes)
    assert table.ids == (0, 4, 5, 6, 7, 8)


def test_a_dsp_id_declared_twice_refuses_the_table():
    with pytest.raises(source.SoundScriptSourceError):
        source.load_dsp_table(
            _index(), read_bytes=lambda index, key: DSP_TABLE + b'{ 4 LINEAR 0.2 0.8 0.0 0.0 0 0 }\r\n'
        )


def test_sound_reference_exists_checks_the_sound_root_of_the_merged_index():
    index = {"sound/impact/metal1.wav": ("loose", "C:/game/Unofficial_Patch/sound/impact/metal1.wav")}
    assert source.sound_reference_exists(index, "Impact/Metal1.wav") is True
    assert source.sound_reference_exists(index, "impact\\metal1.wav") is True
    assert source.sound_reference_exists(index, "impact/metal2.wav") is False


def test_table_reference_exists_checks_the_merged_index_directly():
    index = {"scripts/dsp_presets.txt": ("vpk", ("C:/game/Vampire/pack001.vpk", 0, 10))}
    assert source.table_reference_exists(index, "scripts/dsp_presets.txt") is True
    assert source.table_reference_exists(index, "scripts/missing.txt") is False


def test_resolve_symbol_key_returns_the_tables_own_spelling():
    from elysium_pipeline.formats.sound_script_glb import symbols

    assert symbols.resolve_symbol_key("sndlvl_75db", symbols.SOUND_LEVELS) == "SNDLVL_75dB"
    assert symbols.resolve_symbol_key("SNDLVL_60DB", symbols.SOUND_LEVELS) == "SNDLVL_60dB"
    assert symbols.resolve_symbol_key("nope", symbols.SOUND_LEVELS) is None


# --- byte ledger: every byte claimed exactly once -------------------------------------------------


def _assert_gapless(ledger_row: dict) -> None:
    assert ledger_row["coveragePercent"] == 100.0
    cursor = 0
    for row in ledger_row["ranges"]:
        assert row["offset"] == cursor
        cursor += row["length"]
    assert cursor == ledger_row["byteLength"]


def test_every_game_sound_byte_is_claimed_exactly_once():
    model = _decode_game_sound("metal_barrel.impact")
    _assert_gapless(model.byte_coverage[0])
    assert model.byte_coverage[0]["byteLength"] == model.sources[0].byte_length


def test_every_soundscape_byte_is_claimed_exactly_once():
    model = _decode_soundscape("cabin")
    _assert_gapless(model.byte_coverage[0])


def test_every_sentence_byte_is_claimed_exactly_once():
    for name in ("spi_aggro0", "mx_spaced0", "c1a0_hl1"):
        model = _decode_sentence(name)
        _assert_gapless(model.byte_coverage[0])


def test_every_dsp_preset_byte_is_claimed_exactly_once():
    for preset_id in (0, 4, 5, 6):
        model = _decode_dsp(preset_id)
        _assert_gapless(model.byte_coverage[0])


def test_every_manifest_byte_is_claimed_exactly_once():
    closure = source.manifest_closure(_index(), read_bytes=_read_bytes)
    model = decode.decode_manifest(closure)
    _assert_gapless(model.byte_coverage[0])


def test_sentence_and_dsp_preset_text_ranges_are_graded_mapped_text_not_mapped():
    """The ledger state `mapped` is reserved for a binary record or
    payload; a sentence's name/path/length tokens and a DSP preset's header/processor tokens are
    all text decoded into a structured record, so they must be graded `mapped-text`, never the
    bare `mapped` this seam once used for them."""

    sentence_model = _decode_sentence("spi_aggro0")
    assert "mapped" not in sentence_model.byte_coverage[0]["stateBytes"]
    assert sentence_model.byte_coverage[0]["stateBytes"]["mapped-text"] > 0

    dsp_model = _decode_dsp(4)
    assert "mapped" not in dsp_model.byte_coverage[0]["stateBytes"]
    assert dsp_model.byte_coverage[0]["stateBytes"]["mapped-text"] > 0


def test_whitespace_is_the_only_omission_in_a_keyvalues_table():
    model = _decode_game_sound("metal_barrel.impact")
    assert set(model.byte_coverage[0]["stateBytes"]) <= {"mapped-text", "omitted-proven"}
    assert "omitted-proven" in model.byte_coverage[0]["stateBytes"]


def test_a_ledger_that_claims_omitted_proven_bytes_publishes_the_omission_to_match():
    closure = source.manifest_closure(_index(), read_bytes=_read_bytes)
    manifest_model = decode.decode_manifest(closure)
    dsp_model = _decode_dsp(4)
    sentence_model = _decode_sentence("spi_aggro0")
    for model in (manifest_model, dsp_model, sentence_model):
        assert "omitted-proven" in model.byte_coverage[0]["stateBytes"]
        assert model.omissions
        document, binary = _publish(model)
        coverage = document["extensions"][SOUND_SCRIPT_EXTENSION]["coverage"]
        assert coverage["omittedProven"] == model.omissions


# --- identity / key rule --------------------------------------------------------------------------


def test_the_unit_key_folds_case_and_keeps_the_source_spelling():
    model = _decode_game_sound("METAL_BARREL.IMPACT")
    assert model.name == "metal_barrel.impact"
    assert model.source_name == "Metal_Barrel.Impact"
    assert model.asset_id == "vtmb:sound-script:metal_barrel.impact"


def test_a_slash_in_a_name_is_rejected():
    from elysium_pipeline.formats.sound_script_glb.model import SoundScriptModelError, normalize_name

    with pytest.raises(SoundScriptModelError):
        normalize_name("a/b")


def test_the_manifest_identity_is_the_fixed_asset():
    closure = source.manifest_closure(_index(), read_bytes=_read_bytes)
    assert closure.asset_id == "vtmb:sound-script-manifest:game_sounds_manifest"


def test_the_dsp_preset_identity_is_keyed_by_id():
    model = _decode_dsp(4)
    assert model.asset_id == "vtmb:dsp-preset:4"


# --- dependency roles -------------------------------------------------------------------------


def test_a_direct_wave_produces_a_sound_dependency():
    model = _decode_game_sound("metal_barrel.impact")
    assert {"role": "sound", "asset": "vtmb:sound:surfaces/metal_barrel/impact1.wav",
            "sourcePath": "sound/Surfaces/Metal_Barrel/Impact1.wav", "resolved": True} in model.dependencies


def test_a_sound_reference_the_install_does_not_resolve_is_reported_but_does_not_fail_the_unit():
    model = _decode_game_sound("metal_barrel.impact", sound_exists=lambda path: False)
    assert {"role": "sound", "asset": "vtmb:sound:surfaces/metal_barrel/impact1.wav",
            "sourcePath": "sound/Surfaces/Metal_Barrel/Impact1.wav", "resolved": False} in model.dependencies
    #: A miss in the sibling `sound_glb` seam's corpus is not this entry's defect, so it is never
    #: folded into `unresolved` (which would fail `completeness()` at validation time).
    assert model.unresolved == []
    document, binary = _publish(model)
    summary = validation.validate_document(document, binary)
    assert "vtmb:sound:surfaces/metal_barrel/impact1.wav" in summary["unresolvedDependencies"]
    assert validation.warnings_for(summary)


def test_a_bang_prefixed_wave_produces_a_sentence_dependency():
    model = _decode_game_sound("sentence.cue", sentence_exists=lambda name: name.lower() == "spi_dies0")
    assert {"role": "sentence", "asset": "vtmb:sentence:spi_dies0", "sourcePath": "SPI_DIES0", "resolved": True} in model.dependencies
    assert model.unresolved == []


def test_an_unresolvable_sentence_reference_is_reported_unresolved():
    model = _decode_game_sound("sentence.cue", sentence_exists=lambda name: False)
    assert [row["reason"] for row in model.unresolved] == ["wave-names-no-defined-sentence"]


def test_a_soundscape_dsp_key_produces_a_dsp_preset_dependency():
    model = _decode_soundscape("cabin", dsp_exists=lambda i: i == 17)
    assert {
        "role": "dsp-preset", "asset": "vtmb:dsp-preset:17",
        "sourcePath": TABLE_PATHS["dsp-preset"], "resolved": True,
    } in model.dependencies
    assert model.unresolved == []


def test_an_unresolvable_dsp_reference_is_reported_unresolved():
    model = _decode_soundscape("cabin", dsp_exists=lambda i: False)
    assert [row["reason"] for row in model.unresolved] == ["dsp-key-names-no-defined-preset"]


def test_a_manifest_precache_file_produces_a_sound_script_table_dependency():
    closure = source.manifest_closure(_index(), read_bytes=_read_bytes)
    model = decode.decode_manifest(closure)
    assert model.dependencies == [{
        "role": "sound-script-table",
        "asset": "vtmb:sound-script-table:scripts/game_sounds_surfaceproperties.txt",
        "sourcePath": "scripts/game_sounds_surfaceproperties.txt",
        "resolved": True,
    }]


# --- vocabulary / anomaly / typed rules ------------------------------------------------------------


def test_a_key_outside_the_game_sound_vocabulary_is_reported_rather_than_dropped():
    model = _decode_game_sound("sentence.cue")
    assert [row["key"] for row in model.unsupported if row["reason"].startswith("key-outside")] == ["unknownkey"]


def test_an_attenuation_key_is_supported_in_a_soundscape_block():
    model = _decode_soundscape("cabin")
    playlooping = model.record["blocks"][0]
    attenuation = playlooping["parameters"]["attenuation"]
    assert attenuation["value"] == 2.0
    assert model.parameters[attenuation["parameter"]].key == "attenuation"
    assert model.unsupported == []


def test_a_manifest_precache_file_the_install_does_not_resolve_is_reported_but_does_not_fail_the_unit():
    closure = source.manifest_closure(_index(), read_bytes=_read_bytes)
    model = decode.decode_manifest(closure, table_exists=lambda path: False)
    assert model.dependencies[0]["resolved"] is False
    assert model.unresolved == []


def test_a_dormant_only_entry_publishes_dormant_true_with_evidence():
    model = _decode_game_sound("dormant.only")
    assert model.dormant is True
    assert model.dormant_evidence


def test_a_symbol_the_header_does_not_document_still_resolves_from_the_sdk_reference():
    model = _decode_game_sound("dormant.only")
    assert model.record["pitch"]["resolved"] == {"value": 100.0}
    assert model.record["volume"]["resolved"] == {"value": 1.0}


def test_the_published_symbol_keeps_the_tables_own_casing_not_an_upper_cased_copy():
    model = _decode_game_sound("metal_barrel.impact")
    assert model.record["soundLevel"]["symbol"] == "SNDLVL_75dB"
    assert model.record["channel"]["symbol"] == "CHAN_BODY"


def test_every_resolved_scalar_field_names_the_parameter_it_came_from():
    model = _decode_game_sound("metal_barrel.impact")
    for field in ("channel", "volume", "pitch", "soundLevel"):
        record = model.record[field]
        assert model.parameters[record["parameter"]].key == field.lower()


def test_a_dsp_parameter_count_mismatch_is_an_anomaly_with_the_raw_tokens_kept():
    model = _decode_dsp(5)
    mismatches = [row for row in model.anomalies if row["role"] == "parameter-count-mismatch"]
    assert mismatches and mismatches[0]["declaredCount"] == 3 and mismatches[0]["actualCount"] == 4
    processor = model.record["processors"][0]
    assert [p["sourceToken"] for p in processor["parameters"]] == ["1.0", "3", "0.1483", "9.0"]


def test_every_dsp_preset_record_field_names_the_parameter_it_came_from():
    model = _decode_dsp(4)
    record = model.record
    assert model.parameters[record["id"]["parameter"]].key == "id"
    assert model.parameters[record["configuration"]["parameter"]].key == "configuration"
    assert model.parameters[record["mix"]["min"]["parameter"]].key == "mixMin"
    assert model.parameters[record["parameters"]["duration"]["parameter"]].key == "duration"
    processor = record["processors"][0]
    assert model.parameters[processor["parameter"]].key == "processorType"
    for param in processor["parameters"]:
        assert model.parameters[param["parameter"]].value == param["sourceToken"]


def test_an_unrecognized_processor_type_is_unsupported_not_invented():
    model = _decode_dsp(6)
    assert [row["reason"] for row in model.unsupported] == ["unrecognized-processor-type"]


def test_a_null_processor_is_a_documented_pass_through():
    model = _decode_dsp(0)
    assert model.record["processors"][0]["type"] == "pass-through"
    assert model.unsupported == []


def test_a_dsp_processor_symbol_valued_field_resolves_against_its_own_table():
    model = _decode_dsp(7)
    processor = model.record["processors"][0]
    ftype, cutoff, qwidth, quality, gain = processor["parameters"]
    assert ftype["symbol"] == "LP" and ftype["value"] == symbols.FILTER_TYPES["LP"]
    assert quality["symbol"] == "LO" and quality["value"] == symbols.FILTER_QUALITY["LO"]
    assert cutoff["symbol"] is None and cutoff["value"] == 200.0
    assert model.typed_unidentified == []
    assert model.unsupported == []


def test_a_dsp_processor_token_that_is_neither_a_number_nor_a_symbol_is_typed_unidentified():
    model = _decode_dsp(8)
    assert len(model.typed_unidentified) == 1
    row = model.typed_unidentified[0]
    assert row["sourceToken"] == "XXX"
    assert isinstance(row["offset"], int)
    ftype = model.record["processors"][0]["parameters"][0]
    assert ftype["symbol"] is None and ftype["value"] is None
    # a typed-unidentified value is carried, not dropped, and does not fail the unit
    document, binary = _publish(model)
    assert validation.validate_document(document, binary)["typedUnidentified"] == 1


def test_the_hl1_sentence_grammar_publishes_directory_words_and_modifiers():
    model = _decode_sentence("c1a0_hl1")
    assert model.record["grammar"] == "hl1"
    assert model.record["directory"] == "barney/"
    words = [t for t in model.record["tokens"] if t["type"] == "word"]
    assert words[0]["value"] == "scientists have finally"
    assert words[0]["modifiers"] == [{"code": "p", "name": "pitch", "value": 95}]
    pauses = [t for t in model.record["tokens"] if t["type"] == "pause"]
    assert pauses


def test_the_modern_sentence_grammar_publishes_a_plain_path_and_length():
    model = _decode_sentence("spi_dies0")
    assert model.record["path"] == "character/monster/spiderchick/Death1.wav"
    assert model.record["length"] == 1.5
    assert model.record["grammar"] == "modern"


def test_a_modern_sentence_path_produces_a_sound_dependency():
    model = _decode_sentence("spi_dies0")
    assert model.dependencies == [{
        "role": "sound",
        "asset": "vtmb:sound:character/monster/spiderchick/death1.wav",
        "sourcePath": "sound/character/monster/spiderchick/Death1.wav",
        "resolved": True,
    }]
    assert [parameter.key for parameter in model.parameters] == ["name", "path", "length"]
    document, binary = _publish(model)
    assert validation.validate_document(document, binary)["byteCoveragePercent"] == 100.0


def test_a_sentence_path_the_install_does_not_resolve_is_reported_but_does_not_fail_the_unit():
    model = _decode_sentence("spi_dies0", sound_exists=lambda path: False)
    assert model.dependencies[0]["resolved"] is False
    assert model.unresolved == []


def test_the_hl1_sentence_grammar_produces_no_sound_dependency():
    model = _decode_sentence("c1a0_hl1")
    assert model.dependencies == []


def test_a_path_with_an_embedded_space_is_kept_whole():
    model = _decode_sentence("mx_spaced0")
    assert model.record["path"] == "character/monster/ming xiao/exert_heavy_1.wav"


def test_a_repeated_numbered_sentence_name_is_two_units():
    dies0 = _decode_sentence("spi_dies0")
    dies1 = _decode_sentence("spi_dies1")
    assert dies0.asset_id == "vtmb:sentence:spi_dies0"
    assert dies1.asset_id == "vtmb:sentence:spi_dies1"


# --- GLB structure ----------------------------------------------------------------------------


def test_the_published_document_carries_no_scene_and_no_binary():
    model = _decode_game_sound("metal_barrel.impact")
    document, binary = _publish(model)
    assert binary == b""
    assert "scenes" not in document and "meshes" not in document
    assert document["extensionsUsed"] == [SOUND_SCRIPT_EXTENSION]
    assert document["extensionsRequired"] == [SOUND_SCRIPT_EXTENSION]
    assert document["asset"]["generator"] == "Elysium Sound-script GLB Exporter"


def test_the_extension_root_opens_with_the_contract_key_order():
    model = _decode_game_sound("metal_barrel.impact")
    document, _ = _publish(model)
    root = document["extensions"][SOUND_SCRIPT_EXTENSION]
    assert list(root)[:5] == ["schemaVersion", "identity", "sourceResolution", "dependencies", "coverage"]


def test_every_shipped_shape_validates():
    for name in ("metal_barrel.impact", "dormant.only"):
        document, binary = _publish(_decode_game_sound(
            name, sentence_exists=lambda n: n.lower() == "spi_dies0"
        ))
        summary = validation.validate_document(document, binary)
        assert summary["byteCoveragePercent"] == 100.0
    document, binary = _publish(_decode_soundscape("cabin", dsp_exists=lambda i: True))
    assert validation.validate_document(document, binary)["byteCoveragePercent"] == 100.0
    for name in ("spi_aggro0", "c1a0_hl1"):
        document, binary = _publish(_decode_sentence(name))
        assert validation.validate_document(document, binary)["byteCoveragePercent"] == 100.0
    for preset_id in (0, 4):
        document, binary = _publish(_decode_dsp(preset_id))
        assert validation.validate_document(document, binary)["byteCoveragePercent"] == 100.0
    closure = source.manifest_closure(_index(), read_bytes=_read_bytes)
    document, binary = _publish(decode.decode_manifest(closure))
    assert validation.validate_document(document, binary)["byteCoveragePercent"] == 100.0


def test_an_unsupported_key_fails_validation():
    document, binary = _publish(_decode_game_sound("sentence.cue"))
    with pytest.raises(validation.SoundScriptGlbValidationError):
        validation.validate_document(document, binary)


def test_a_dependency_no_reference_produced_fails_validation():
    document, binary = _publish(_decode_game_sound("metal_barrel.impact"))
    extension = document["extensions"][SOUND_SCRIPT_EXTENSION]
    extension["dependencies"].append(
        {"role": "sound", "asset": "vtmb:sound:invented.wav", "sourcePath": "invented.wav", "resolved": False}
    )
    with pytest.raises(validation.SoundScriptGlbValidationError):
        validation.validate_document(document, binary)


def test_a_tampered_ledger_range_table_fails_validation():
    document, binary = _publish(_decode_dsp(0))
    extension = document["extensions"][SOUND_SCRIPT_EXTENSION]
    ledger = extension["coverage"]["byteLedger"][0]
    ledger["ranges"][0]["length"] += 1     # opens a gap / overlap in the range table
    with pytest.raises((validation.SoundScriptGlbValidationError, ByteLedgerError, UnitValidationError)):
        validation.validate_document(document, binary)


def test_a_tampered_record_field_fails_export_time_validation_via_independent_redecode():
    closure = source.game_sound_closure(_game_sound_directory(), "metal_barrel.impact")
    model = decode.decode_game_sound(closure)
    document, binary = _publish(model)
    extension = document["extensions"][SOUND_SCRIPT_EXTENSION]

    # unmodified, the document validates against a fresh re-decode of the same member
    assert validation.validate_document(document, binary, source_members=closure.members())

    extension["record"]["waves"][0]["path"] = "totally/invented.wav"
    with pytest.raises(validation.SoundScriptGlbValidationError):
        validation.validate_document(document, binary, source_members=closure.members())


def test_a_tampered_resolved_symbol_fails_export_time_validation_via_independent_redecode():
    closure = source.game_sound_closure(_game_sound_directory(), "metal_barrel.impact")
    model = decode.decode_game_sound(closure)
    document, binary = _publish(model)
    extension = document["extensions"][SOUND_SCRIPT_EXTENSION]
    extension["record"]["soundLevel"]["resolved"] = 999
    with pytest.raises(validation.SoundScriptGlbValidationError):
        validation.validate_document(document, binary, source_members=closure.members())


def test_a_tampered_dependency_row_fails_export_time_validation_via_independent_redecode():
    closure = source.game_sound_closure(_game_sound_directory(), "metal_barrel.impact")
    resolvers = {
        "sentence_exists": lambda n: False,
        "sound_exists": lambda path: False,
    }
    model = decode.decode_game_sound(closure, **resolvers)
    document, binary = _publish(model)
    extension = document["extensions"][SOUND_SCRIPT_EXTENSION]

    # unmodified, the document validates against a fresh re-decode using the same resolvers
    assert validation.validate_document(
        document, binary, source_members=closure.members(), resolvers=resolvers
    )

    extension["dependencies"][0]["resolved"] = True
    extension["dependencies"][0]["sourcePath"] = "sound/completely/invented/path.wav"
    with pytest.raises(validation.SoundScriptGlbValidationError):
        validation.validate_document(
            document, binary, source_members=closure.members(), resolvers=resolvers
        )


def test_a_relabeled_ledger_range_fails_export_time_validation_via_independent_redecode():
    closure = source.game_sound_closure(_game_sound_directory(), "metal_barrel.impact")
    model = decode.decode_game_sound(closure)
    document, binary = _publish(model)
    extension = document["extensions"][SOUND_SCRIPT_EXTENSION]
    ledger = extension["coverage"]["byteLedger"][0]

    # relabel one live-text `mapped-text` range as a false `omitted-proven` claim, keeping the
    # range table internally self-consistent (contiguous, correct stateBytes/rangesSha256) so only
    # the independent re-decode -- not `verify_ledger_row`'s self-check -- can catch it
    victim = next(row for row in ledger["ranges"] if row["state"] == "mapped-text" and row["length"] > 0)
    victim["state"] = "omitted-proven"
    victim["owner"] = "entry.dead-storage"
    from collections import Counter

    from elysium_pipeline.formats.unit_contract import ranges_sha256

    totals = Counter()
    for row in ledger["ranges"]:
        totals[row["state"]] += row["length"]
    ledger["stateBytes"] = dict(sorted(totals.items()))
    ledger["rangesSha256"] = ranges_sha256(ledger["sourcePath"], ledger["byteLength"], ledger["ranges"])

    with pytest.raises(validation.SoundScriptGlbValidationError):
        validation.validate_document(document, binary, source_members=closure.members())


def test_member_bytes_re_read_before_export_disagree_with_the_ledger():
    import dataclasses

    closure = source.game_sound_closure(_game_sound_directory(), "metal_barrel.impact")
    model = decode.decode_game_sound(closure)
    document, binary = _publish(model)
    tampered = dataclasses.replace(closure.entry, data=closure.entry.data[:-1] + b"!")
    with pytest.raises((validation.SoundScriptGlbValidationError, ByteLedgerError, UnitValidationError)):
        validation.validate_document(document, binary, source_members=(tampered,))


def test_the_same_source_exports_byte_identical_bytes_twice():
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        first = exporter.export_dsp_preset(_index(), 4, root / "a", read_bytes=_read_bytes)
        second = exporter.export_dsp_preset(_index(), 4, root / "b", read_bytes=_read_bytes)
        assert first.read_bytes() == second.read_bytes()


def test_a_false_zero_claim_is_rejected_at_export_time():
    from elysium_pipeline.formats.unit_contract import ByteLedger

    ledger = ByteLedger("scripts/fake.txt", b"\x01\x00")
    with pytest.raises(ByteLedgerError):
        ledger.claim(0, 1, "reserved-zero", "entry.reserved")


# --- final round trip through the file validator ------------------------------------------------


def test_the_written_units_read_back_through_the_file_validator():
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        destination = exporter.export_game_sound(_index(), "Metal_Barrel.Impact", root, read_bytes=_read_bytes)
        assert destination == root / "sound-scripts" / "metal_barrel.impact.glb"
        summary = validation.validate(destination)
        assert summary["asset"] == "vtmb:sound-script:metal_barrel.impact"

        destination = exporter.export_manifest(_index(), root, read_bytes=_read_bytes)
        summary = validation.validate(destination)
        assert summary["asset"] == "vtmb:sound-script-manifest:game_sounds_manifest"

        destination = exporter.export_soundscape(
            _index(), "cabin", root, read_bytes=_read_bytes, dsp_ids=frozenset({17})
        )
        summary = validation.validate(destination)
        assert summary["asset"] == "vtmb:soundscape:cabin"

        destination = exporter.export_sentence(_index(), "SPI_AGGRO0", root, read_bytes=_read_bytes)
        summary = validation.validate(destination)
        assert summary["asset"] == "vtmb:sentence:spi_aggro0"

        destination = exporter.export_dsp_preset(_index(), 4, root, read_bytes=_read_bytes)
        summary = validation.validate(destination)
        assert summary["asset"] == "vtmb:dsp-preset:4"


def test_source_keys_enumerates_every_kind():
    keys = exporter.source_keys(_index(), read_bytes=_read_bytes)
    assert "metal_barrel.impact" in keys
    assert "game_sounds_manifest" in keys
    assert "cabin" in keys
    assert "spi_dies0" in keys
    assert "4" in keys


def test_export_is_the_game_sound_alias():
    assert exporter.export is exporter.export_game_sound
