"""Synthetic contract tests for the isolated sound-scheme GLB exporter.

Every test builds its own fake install index and `read_bytes` closure -- never the real one -- so
the suite is a pin on this seam's own decode/export/validate contract, independent of the corpus.
"""

from __future__ import annotations

from pathlib import PurePosixPath

import pytest

from elysium_pipeline.exporters import sound_scheme_glb as exporter
from elysium_pipeline.formats.sound_scheme_glb import (
    SOUND_SCHEME_EXTENSION,
    decode_sound_scheme,
    load_dsp_preset_ids,
    load_source_closure,
    normalize_stem,
    output_relative_path,
    source_keys,
)
from elysium_pipeline.formats.sound_scheme_glb.decode import SoundSchemeDecodeError
from elysium_pipeline.formats.sound_scheme_glb.source import SoundSchemeSourceError
from elysium_pipeline.formats.unit_contract import (
    ByteLedger,
    ByteLedgerError,
    UnitValidationError,
    write_glb,
)
from elysium_pipeline.validation import sound_scheme_glb as validation
from elysium_pipeline.validation.sound_scheme_glb import SoundSchemeGlbValidationError

# --- fixtures ----------------------------------------------------------------------------------


def _fake_install(files: dict[str, bytes]) -> tuple[dict, callable]:
    """A minimal `install.build_index`-shaped index plus its `read_bytes` closure."""

    index = {path: ("loose", f"/fake/{path}") for path in files}

    def read_bytes(idx: dict, key: str) -> bytes | None:
        return files.get(key)

    return index, read_bytes


#: Exercises: SchemeParams (both keys), a repeated Music block (last-wins, with the discarded
#: first occurrence flagged both `repeated-block` and `volume-out-of-range`), Combat's `Dry`,
#: Ambient's `NoPause` (outside `seam_map_sound_scheme.md`'s own Ambient column -- accepted by
#: `MUSIC_LIKE_KEYS`), an out-of-vocabulary block, two `RandomSound` occurrences (one with a
#: repeated `Volume`, an inverted `Pitch` range, a wrapping `Angle` range that is *not* flagged,
#: an out-of-vocabulary key and an unresolved `Filename`; the other with a second unresolved
#: `Filename` sharing no asset with the first).
SAMPLE = (
    b'SoundScheme\r\n'
    b'{\r\n'
    b'\tSchemeParams\r\n'
    b'\t{\r\n'
    b'\t\t"RandomSoundCount"\t"3"\r\n'
    b'\t\t"RoomDSP"\t"7"\r\n'
    b'\t}\r\n'
    b'\tMusic // first, discarded\r\n'
    b'\t{\r\n'
    b'\t\t"Filename"\t"music/theme.mp3"\r\n'
    b'\t\t"Volume"\t"200"\r\n'
    b'\t\t"NoPause"\t"1"\r\n'
    b'\t}\r\n'
    b'\tMusic\r\n'
    b'\t{\r\n'
    b'\t\t"Filename"\t"music/theme2.mp3"\r\n'
    b'\t\t"Volume"\t"50"\r\n'
    b'\t}\r\n'
    b'\tCombat\r\n'
    b'\t{\r\n'
    b'\t\t"Filename"\t"combat/fight.mp3"\r\n'
    b'\t\t"Volume"\t"60"\r\n'
    b'\t\t"Dry"\t"1"\r\n'
    b'\t}\r\n'
    b'\tAmbient\r\n'
    b'\t{\r\n'
    b'\t\t"Filename"\t"ambient/room.wav"\r\n'
    b'\t\t"Volume"\t"40"\r\n'
    b'\t\t"NoPause"\t"0"\r\n'
    b'\t}\r\n'
    b'\tWeather\r\n'
    b'\t{\r\n'
    b'\t\t"Intensity"\t"5"\r\n'
    b'\t}\r\n'
    b'\tRandomSound\r\n'
    b'\t{\r\n'
    b'\t\t"Filename"\t"FX/Drip.wav"\r\n'
    b'\t\t"PitchMin"\t"110"\r\n'
    b'\t\t"PitchMax"\t"90"\r\n'
    b'\t\t"Volume"\t"50"\r\n'
    b'\t\t"Volume"\t"55"\r\n'
    b'\t\t"AngleMin"\t"350"\r\n'
    b'\t\t"AngleMax"\t"10"\r\n'
    b'\t\t"Mystery"\t"1"\r\n'
    b'\t}\r\n'
    b'\tRandomSound\r\n'
    b'\t{\r\n'
    b'\t\t"Filename"\t"fx/drip.wav"\r\n'
    b'\t\t"Volume"\t"30"\r\n'
    b'\t}\r\n'
    b'}\r\n'
)

#: `SAMPLE` with the out-of-vocabulary block and key removed, so a decode of it has zero
#: `unsupported` rows and can pass `validate_document` outright -- the way every one of the 174
#: shipped schemes does.
VALID_SAMPLE = (
    SAMPLE
    .replace(b'\tWeather\r\n\t{\r\n\t\t"Intensity"\t"5"\r\n\t}\r\n', b'')
    .replace(b'\t\t"Mystery"\t"1"\r\n', b'')
)

SCHEME_PATH = "sound/schemes/testscheme.txt"
KNOWN_SOUNDS = {"music/theme.mp3", "music/theme2.mp3", "combat/fight.mp3", "ambient/room.wav"}
KNOWN_DSP_IDS = frozenset({"7"})


def _decode(text: bytes = SAMPLE):
    index, read_bytes = _fake_install({SCHEME_PATH: text})
    closure = load_source_closure(index, "testscheme", read_bytes=read_bytes)
    model = decode_sound_scheme(
        closure,
        sound_exists=lambda path: path.replace("\\", "/").lower() in KNOWN_SOUNDS,
        dsp_preset_exists=lambda preset_id: preset_id in KNOWN_DSP_IDS,
    )
    return index, read_bytes, closure, model


def _parameter(model, block: str, key: str):
    return next(p for p in model.parameters if p.block == block and p.key == key)


# --- byte ledger -----------------------------------------------------------------------------


def test_every_byte_of_the_source_is_claimed_exactly_once():
    _, _, _, model = _decode()
    row = model.ledger_row
    assert row["byteLength"] == len(SAMPLE)
    assert row["accountedBytes"] == len(SAMPLE)
    assert row["coveragePercent"] == 100.0
    ranges = row["ranges"]
    cursor = 0
    for entry in ranges:
        assert entry["offset"] == cursor
        cursor += entry["length"]
    assert cursor == len(SAMPLE)


def test_the_ledger_has_no_span_and_hashes_the_whole_file():
    import hashlib

    _, _, _, model = _decode()
    assert model.ledger_row["sourcePath"] == SCHEME_PATH
    assert model.ledger_row["sourceSha256"] == hashlib.sha256(SAMPLE).hexdigest()
    assert model.member.path == SCHEME_PATH


def test_text_ranges_are_graded_mapped_text_not_mapped():
    """`seam_map_unit_contract.md` reserves the ledger state `mapped` for a binary record or
    payload; every range this seam claims is a text token (`root.name`, `root.braces`, a block's
    name/braces, a key/value pair, a comment) decoded into a structured table, so it must be
    graded `mapped-text`. Only a byte-order-mark claim -- which this sample carries none of -- may
    still be `mapped`."""

    _, _, _, model = _decode()
    state_bytes = model.ledger_row["stateBytes"]
    assert "mapped" not in state_bytes
    assert state_bytes["mapped-text"] > 0
    owners_by_state: dict[str, set[str]] = {}
    for row in model.ledger_row["ranges"]:
        owners_by_state.setdefault(row["state"], set()).add(row["owner"])
    assert "root.name" in owners_by_state["mapped-text"]
    assert any(owner.startswith("comments[") for owner in owners_by_state["mapped-text"])


def test_a_false_zero_claim_over_the_files_own_text_is_refused():
    """The seam never claims `reserved-zero`/`padding-zero` itself; this pins that the shared
    `ByteLedger` it is built on would still catch the seam if it ever tried to."""

    ledger = ByteLedger(SCHEME_PATH, SAMPLE)
    with pytest.raises(ByteLedgerError, match="non-zero"):
        ledger.claim(0, 4, "padding-zero", "root.name")


# --- identity and key rule ---------------------------------------------------------------------


@pytest.mark.parametrize(
    "raw",
    ["testscheme", "TestScheme", "sound/schemes/testscheme.txt", "Sound/Schemes/TESTSCHEME.TXT",
     r"sound\schemes\testscheme.txt"],
)
def test_the_key_folds_case_slashes_the_root_prefix_and_the_extension(raw):
    assert normalize_stem(raw) == "testscheme"


def test_one_identity_names_one_file_below_the_family_directory():
    _, _, closure, model = _decode()
    assert closure.asset == model.asset_id == "vtmb:sound-scheme:testscheme"
    assert output_relative_path("testscheme") == PurePosixPath("testscheme.glb")


def test_load_source_closure_tolerates_the_root_prefix_and_source_extension():
    index, read_bytes = _fake_install({SCHEME_PATH: SAMPLE})
    for argument in ("testscheme", "sound/schemes/testscheme.txt", "TestScheme"):
        closure = load_source_closure(index, argument, read_bytes=read_bytes)
        assert closure.stem == "testscheme"
        assert closure.asset == "vtmb:sound-scheme:testscheme"


def test_a_missing_scheme_file_is_refused():
    index, read_bytes = _fake_install({})
    with pytest.raises(SoundSchemeSourceError):
        load_source_closure(index, "nosuchscheme", read_bytes=read_bytes)


def test_source_keys_enumerates_every_scheme_stem_folded_and_sorted():
    index, _ = _fake_install(
        {
            "sound/schemes/zeta.txt": b"x",
            "sound/schemes/alpha.txt": b"x",
            "sound/other/not_a_scheme.txt": b"x",
            "sound/schemes/notes.dat": b"x",
        }
    )
    assert source_keys(index) == ["alpha", "zeta"]


# --- dependency roles ------------------------------------------------------------------------


def test_the_sound_role_names_every_filename_the_published_scheme_still_makes():
    _, _, _, model = _decode()
    sound_rows = [row for row in model.dependencies if row["role"] == "sound"]
    assets = {row["asset"] for row in sound_rows}
    # "music/theme.mp3" is the discarded first `Music` block's `Filename`; the second `Music`
    # block's `repeated-block` overwrite means it is no longer a reference `scheme` makes, so it
    # owns no dependency row even though it is still in `parameters[]` (see `_collect_dependencies`).
    assert assets == {
        "vtmb:sound:music/theme2.mp3",
        "vtmb:sound:combat/fight.mp3",
        "vtmb:sound:ambient/room.wav",
        "vtmb:sound:fx/drip.wav",
    }
    # "FX/Drip.wav" and "fx/drip.wav" normalize to one asset and dedupe to one row, keeping the
    # first authored spelling.
    drip = next(row for row in sound_rows if row["asset"] == "vtmb:sound:fx/drip.wav")
    # Install-relative, spelling preserved (`seam_map_unit_contract.md`, References between
    # units) -- not the bare authored token: the `dsp-preset` row two lines below is already
    # install-relative (`scripts/dsp_presets.txt#<id>`), and `sound` now agrees with it.
    assert drip["sourcePath"] == "sound/FX/Drip.wav"
    assert len(sound_rows) == len(assets)


def test_the_dsp_preset_role_names_roomdsp_and_states_its_resolution():
    _, _, _, model = _decode()
    preset_rows = [row for row in model.dependencies if row["role"] == "dsp-preset"]
    assert len(preset_rows) == 1
    assert preset_rows[0]["asset"] == "vtmb:dsp-preset:7"
    assert preset_rows[0]["resolved"] is True
    assert model.params["roomDsp"]["resolved"] is True


def test_an_unknown_dsp_preset_id_is_a_dependency_that_does_not_resolve():
    text = SAMPLE.replace(b'"RoomDSP"\t"7"', b'"RoomDSP"\t"999"')
    _, _, _, model = _decode(text)
    row = next(r for r in model.dependencies if r["role"] == "dsp-preset")
    assert row["resolved"] is False
    assert model.params["roomDsp"]["resolved"] is False


# --- grammar and coverage vocabulary -----------------------------------------------------------


def test_a_key_outside_a_blocks_vocabulary_is_unsupported():
    _, _, _, model = _decode()
    assert {"key": "mystery", "block": "RandomSound[0]", "offset": _parameter(model, "RandomSound[0]", "mystery").offset,
            "reason": "key-outside-the-block-vocabulary"} in model.unsupported


def test_a_block_outside_the_vocabulary_is_unsupported_but_still_ledgered():
    _, _, _, model = _decode()
    rows = [row for row in model.unsupported if row.get("block") == "Weather[0]"]
    assert rows == [
        {"block": "Weather[0]", "offset": rows[0]["offset"], "reason": "block-outside-the-scheme-vocabulary"}
    ]
    # Its pairs are still in `parameters[]` (and therefore ledgered), just outside `scheme`.
    assert _parameter(model, "Weather[0]", "intensity").value == "5"


def test_a_repeated_scalar_key_resolves_last_and_records_an_anomaly():
    _, _, _, model = _decode()
    anomalies = [a for a in model.anomalies if a.get("role") == "repeated-scalar-key" and a.get("key") == "volume"]
    assert len(anomalies) == 1
    random_sound = model.random_sounds[0]
    assert random_sound["volume"]["value"] == 55.0


def test_a_repeated_singleton_block_resolves_last_and_records_an_anomaly():
    _, _, _, model = _decode()
    anomalies = [a for a in model.anomalies if a.get("role") == "repeated-block" and a.get("block") == "Music"]
    assert len(anomalies) == 1
    assert model.music["file"] == "music/theme2.mp3"
    assert model.music["volume"]["value"] == 50.0
    # The discarded first occurrence is still fully represented in `parameters[]`...
    assert _parameter(model, "Music[0]", "filename").value == "music/theme.mp3"
    # ...and its own departure (out-of-range volume) is still recorded.
    assert any(
        a.get("role") == "volume-out-of-range" and a.get("value") == 200.0 for a in model.anomalies
    )


def test_range_inverted_flags_pitch_but_angle_wraps_without_one():
    _, _, _, model = _decode()
    random_sound = model.random_sounds[0]
    assert random_sound["pitch"]["min"]["value"] == 110.0
    assert random_sound["pitch"]["max"]["value"] == 90.0
    assert random_sound["angle"] == {"min": {"value": 350.0, "raw": "350", "parameter": random_sound["angle"]["min"]["parameter"]},
                                      "max": {"value": 10.0, "raw": "10", "parameter": random_sound["angle"]["max"]["parameter"]}}
    roles = [a["role"] for a in model.anomalies]
    assert roles.count("range-inverted") == 1
    inverted = next(a for a in model.anomalies if a["role"] == "range-inverted")
    assert inverted["pair"] == "pitch"


def test_volume_out_of_range_is_flagged_for_any_sound_emitting_block():
    _, _, _, model = _decode()
    values = sorted(a["value"] for a in model.anomalies if a["role"] == "volume-out-of-range")
    assert values == [200.0]


def test_an_unresolved_filename_is_an_anomaly_and_an_unresolved_dependency():
    _, _, _, model = _decode()
    unresolved_paths = {a["path"] for a in model.anomalies if a["role"] == "unresolved-file"}
    assert unresolved_paths == {"FX/Drip.wav", "fx/drip.wav"}
    assert all(
        row["resolved"] is False
        for row in model.dependencies
        if row["asset"] == "vtmb:sound:fx/drip.wav"
    )


def test_a_discarded_singleton_blocks_unresolved_file_raises_no_anomaly():
    """`music/theme.mp3` (the discarded first `Music` occurrence's `Filename` in `SAMPLE`) is a
    known sound, so it never exercised this path; here the discarded occurrence's file is itself
    unresolved. Since `dependencies` is built from the *final* `scheme` only (see
    `_collect_dependencies`), an `unresolved-file` anomaly for a reference the published scheme no
    longer makes would name something `dependencies` carries no row for -- so it must not be
    raised."""

    text = SAMPLE.replace(b'"Filename"\t"music/theme.mp3"', b'"Filename"\t"music/gone.mp3"')
    _, _, _, model = _decode(text)
    unresolved_paths = {a["path"] for a in model.anomalies if a["role"] == "unresolved-file"}
    assert "music/gone.mp3" not in unresolved_paths
    assert not any(row["asset"] == "vtmb:sound:music/gone.mp3" for row in model.dependencies)
    # The final Music occurrence is still a known sound, so no unresolved-file fires for it.
    assert model.music["file"] == "music/theme2.mp3"


def test_ambient_accepts_nopause_beyond_its_own_grammar_column():
    """`seam_map_sound_scheme.md`'s own `Ambient` column is `Filename`, `Volume` alone; real data
    (`la_abandoned_building_1.txt`, `sm_junkyard_1.txt`, `test2.txt`) authors `NoPause` (and, in
    `test2.txt`, `Dry`) there too -- accepted by `MUSIC_LIKE_KEYS`."""

    _, _, _, model = _decode()
    assert model.ambient["noPause"] == {"value": False, "raw": "0", "parameter": model.ambient["noPause"]["parameter"]}
    assert model.unsupported == [
        row for row in model.unsupported if row.get("reason") != "key-outside-the-block-vocabulary"
        or row.get("block") != "Ambient[0]"
    ]


def test_an_empty_or_valueless_value_is_not_unsupported():
    """One shipped scheme (`epicdemo.txt`) authors `"Filename" ""`; the meaning -- no sound plays
    -- is understood, so it is not `unsupported` the way a genuinely unparsable value is."""

    text = (
        b'SoundScheme\r\n{\r\n'
        b'\tAmbient\r\n\t{\r\n\t\t"Filename"\t""\r\n\t\t"Volume"\t"50"\r\n\t}\r\n'
        b'\tRandomSound\r\n\t{\r\n\t\t"AngleMax"\r\n\t}\r\n'
        b'}\r\n'
    )
    _, _, _, model = _decode(text)
    assert model.unsupported == []
    assert model.ambient["file"] is None and model.ambient["asset"] is None
    assert model.random_sounds[0]["angle"]["max"] is None
    assert any(a["role"] == "valueless-key" for a in model.anomalies)


def test_a_value_that_is_present_but_not_a_number_is_unsupported():
    text = SAMPLE.replace(b'"RandomSoundCount"\t"3"', b'"RandomSoundCount"\t"lots"')
    _, _, _, model = _decode(text)
    assert {"key": "randomsoundcount", "offset": _parameter(model, "SchemeParams[0]", "randomsoundcount").offset,
            "reason": "value-is-not-a-number"} in model.unsupported
    assert model.params["randomSoundCount"] is None


def test_a_nonfinite_number_is_unsupported_not_a_write_time_crash():
    """`float("nan")`/`float("inf")` both parse under bare `float()`; the container's JSON chunk
    rejects them (`seam_map_unit_contract.md`, Container), so this is graded at decode time rather
    than surfacing as a late `ValueError` out of the GLB writer."""

    text = SAMPLE.replace(b'"RandomSoundCount"\t"3"', b'"RandomSoundCount"\t"nan"')
    _, _, _, model = _decode(text)
    assert {"key": "randomsoundcount", "offset": _parameter(model, "SchemeParams[0]", "randomsoundcount").offset,
            "reason": "value-is-not-a-number"} in model.unsupported
    assert model.params["randomSoundCount"] is None


def test_the_root_literal_must_be_soundscheme():
    index, read_bytes = _fake_install({SCHEME_PATH: b'NotAScheme\r\n{\r\n}\r\n'})
    closure = load_source_closure(index, "testscheme", read_bytes=read_bytes)
    with pytest.raises(SoundSchemeDecodeError):
        decode_sound_scheme(closure)


def test_an_unclosed_root_is_an_anomaly_not_a_crash():
    """One shipped scheme (`epicdemo.txt`) never closes its own root block."""

    text = (
        b'SoundScheme\r\n{\r\n'
        b'\tMusic\r\n\t{\r\n\t\t"Filename"\t"a.mp3"\r\n\t\t"Volume"\t"10"\r\n\t}\r\n'
    )
    _, _, _, model = _decode(text)
    assert any(a["role"] == "unclosed-block-at-end-of-file" for a in model.anomalies)
    assert model.ledger_row["accountedBytes"] == len(text)


def test_a_significant_token_after_the_root_is_an_anomaly_not_a_ledger_gap():
    """A bare token after `SoundScheme`'s own closing brace names nothing this grammar has a
    field for; it must still be claimed (as `omitted-proven`), not left as an unaccounted-for
    byte-ledger gap that aborts the export."""

    text = (
        b'SoundScheme\r\n{\r\n'
        b'\tMusic\r\n\t{\r\n\t\t"Filename"\t"a.mp3"\r\n\t}\r\n'
        b'}\r\nstray\r\n'
    )
    _, _, _, model = _decode(text)
    assert any(a["role"] == "trailing-content-after-root" for a in model.anomalies)
    assert {"role": "trailing-content-after-root",
            "reason": "content-outside-the-root-block-has-no-keyvalues-meaning"} in model.omissions
    assert model.ledger_row["accountedBytes"] == len(text)
    assert model.ledger_row["coveragePercent"] == 100.0


# --- document shape and export ------------------------------------------------------------------


def test_build_document_opens_the_extension_root_with_the_contract_key_order():
    _, _, _, model = _decode()
    document, binary = exporter.build_document(model)
    assert binary == b""
    root = document["extensions"][SOUND_SCHEME_EXTENSION]
    assert list(root)[:5] == ["schemaVersion", "identity", "sourceResolution", "dependencies", "coverage"]
    assert document["extensionsUsed"] == document["extensionsRequired"] == [SOUND_SCHEME_EXTENSION]
    assert document["asset"]["generator"] == "Elysium Sound-scheme GLB Exporter"


def test_a_well_formed_document_passes_validation():
    _, _, closure, model = _decode(VALID_SAMPLE)
    document, binary = exporter.build_document(model)
    summary = validation.validate_document(document, binary, source_members=closure.members())
    assert summary["asset"] == "vtmb:sound-scheme:testscheme"
    assert summary["dependencies"] == len(model.dependencies)
    assert summary["randomSounds"] == 2


def test_validation_rejects_a_scheme_value_that_disagrees_with_its_own_raw_token():
    """A decode bug that writes the wrong number into `scheme` while leaving `raw` (and
    `parameters[]`) alone must not pass -- this needs no source at all: `scheme`'s own `raw` and
    `parameters[]`'s own `value` are supposed to be the same string either way."""

    _, _, closure, model = _decode(VALID_SAMPLE)
    document, binary = exporter.build_document(model)
    document["extensions"][SOUND_SCHEME_EXTENSION]["scheme"]["music"]["volume"]["value"] = 999.0
    with pytest.raises(SoundSchemeGlbValidationError):
        validation.validate_document(document, binary)
    with pytest.raises(SoundSchemeGlbValidationError):
        validation.validate_document(document, binary, source_members=closure.members())


def test_validation_rejects_a_scheme_file_whose_spelling_disagrees_with_its_own_parameter():
    """A decode bug that re-spells a `Filename` between `scheme` and `parameters[]` must not pass,
    again with no source needed: the two are supposed to agree on the raw source token."""

    _, _, closure, model = _decode(VALID_SAMPLE)
    document, binary = exporter.build_document(model)
    document["extensions"][SOUND_SCHEME_EXTENSION]["scheme"]["music"]["file"] = "MUSIC/THEME2.MP3"
    with pytest.raises(SoundSchemeGlbValidationError):
        validation.validate_document(document, binary)


def test_validation_rejects_a_parameter_table_that_disagrees_with_an_independent_reparse():
    """`music/theme.mp3` is the discarded first `Music` occurrence's `Filename`: no `scheme` field
    names its parameter slot, so only an independent re-parse of the source -- not any
    scheme-vs-parameters cross-check -- can catch it being tampered with, and only runs when a
    source member is actually available to re-parse."""

    _, _, closure, model = _decode(VALID_SAMPLE)
    document, binary = exporter.build_document(model)
    parameters = document["extensions"][SOUND_SCHEME_EXTENSION]["parameters"]
    slot = _parameter(model, "Music[0]", "filename").index
    assert parameters[slot]["value"] == "music/theme.mp3"
    parameters[slot]["value"] = "music/tampered.mp3"
    # Standalone (no source to re-parse against) has nothing to catch this with.
    validation.validate_document(document, binary)
    with pytest.raises(SoundSchemeGlbValidationError):
        validation.validate_document(document, binary, source_members=closure.members())


def test_validation_rejects_a_comment_dropped_from_the_published_table():
    _, _, closure, model = _decode(VALID_SAMPLE)
    assert model.comments  # the sample's own "// first, discarded" comment
    document, binary = exporter.build_document(model)
    document["extensions"][SOUND_SCHEME_EXTENSION]["comments"].pop()
    with pytest.raises(SoundSchemeGlbValidationError):
        validation.validate_document(document, binary, source_members=closure.members())


def test_validation_rejects_an_omissions_row_the_byte_ledger_does_not_justify():
    _, _, closure, model = _decode(VALID_SAMPLE)
    document, binary = exporter.build_document(model)
    document["extensions"][SOUND_SCHEME_EXTENSION]["omissions"].append(
        {"role": "trailing-content-after-root",
         "reason": "content-outside-the-root-block-has-no-keyvalues-meaning"}
    )
    with pytest.raises(SoundSchemeGlbValidationError):
        validation.validate_document(document, binary, source_members=closure.members())


def test_validation_rejects_a_tampered_byte_ledger_range():
    _, _, closure, model = _decode(VALID_SAMPLE)
    document, binary = exporter.build_document(model)
    ranges = document["extensions"][SOUND_SCHEME_EXTENSION]["coverage"]["byteLedger"][0]["ranges"]
    ranges[0]["length"] += 1
    with pytest.raises(UnitValidationError):
        validation.validate_document(document, binary, source_members=closure.members())


def test_validation_rejects_a_dependency_set_that_disagrees_with_the_scheme():
    _, _, closure, model = _decode(VALID_SAMPLE)
    document, binary = exporter.build_document(model)
    document["extensions"][SOUND_SCHEME_EXTENSION]["dependencies"].pop()
    with pytest.raises(SoundSchemeGlbValidationError):
        validation.validate_document(document, binary, source_members=closure.members())


# --- source -> published reverse checks (block-drop and field-drop regressions) ----------------


def test_validation_rejects_a_music_block_dropped_from_scheme_entirely():
    """A decode bug that nulls `scheme.music` for a block the source still authors -- carrying its
    dependency row away with it -- has nothing in `scheme` left to disagree with itself, so only a
    re-parse of the source (not any scheme-internal cross-check) can catch it."""

    _, _, closure, model = _decode(VALID_SAMPLE)
    document, binary = exporter.build_document(model)
    extension = document["extensions"][SOUND_SCHEME_EXTENSION]
    extension["scheme"]["music"] = None
    extension["dependencies"] = [
        row for row in extension["dependencies"] if row["asset"] != "vtmb:sound:music/theme2.mp3"
    ]
    # Standalone has no source to re-parse against, so this specific drop is invisible to it...
    validation.validate_document(document, binary)
    # ...but export-time validation, which does have the source, must not let it through.
    with pytest.raises(SoundSchemeGlbValidationError):
        validation.validate_document(document, binary, source_members=closure.members())


def test_validation_rejects_a_music_record_missing_its_own_authored_file():
    """`scheme.music` survives but its own `file`/`asset`/`parameter` are nulled -- the source
    still authors a non-empty `Filename` for the block, so this is the same regression as dropping
    the whole record, one field narrower."""

    _, _, closure, model = _decode(VALID_SAMPLE)
    document, binary = exporter.build_document(model)
    extension = document["extensions"][SOUND_SCHEME_EXTENSION]
    music = extension["scheme"]["music"]
    music["file"] = music["asset"] = music["parameter"] = None
    extension["dependencies"] = [
        row for row in extension["dependencies"] if row["asset"] != "vtmb:sound:music/theme2.mp3"
    ]
    with pytest.raises(SoundSchemeGlbValidationError):
        validation.validate_document(document, binary, source_members=closure.members())


def test_validation_rejects_scheme_params_dropped_though_the_source_still_authors_it():
    _, _, closure, model = _decode(VALID_SAMPLE)
    document, binary = exporter.build_document(model)
    extension = document["extensions"][SOUND_SCHEME_EXTENSION]
    extension["scheme"]["params"] = None
    extension["dependencies"] = [
        row for row in extension["dependencies"] if row["role"] != "dsp-preset"
    ]
    with pytest.raises(SoundSchemeGlbValidationError):
        validation.validate_document(document, binary, source_members=closure.members())


def test_validation_rejects_a_field_fabricated_from_the_wrong_parameter_key():
    """A field read off a source row that does not carry its own key -- e.g. `frequency` sourced
    from a `Volume` row -- must not pass even though the fabricated `raw`/`value` agree with each
    other, because the row it names is simply the wrong key."""

    _, _, closure, model = _decode(VALID_SAMPLE)
    document, binary = exporter.build_document(model)
    extension = document["extensions"][SOUND_SCHEME_EXTENSION]
    volume = extension["scheme"]["randomSounds"][0]["volume"]
    extension["scheme"]["randomSounds"][0]["frequency"] = dict(volume)
    with pytest.raises(SoundSchemeGlbValidationError):
        validation.validate_document(document, binary)


def test_validation_rejects_a_scheme_field_naming_a_repeated_keys_earlier_row():
    """`RandomSound[0]`'s own `Volume` repeats (`50` then `55`, last-wins); a `scheme` field
    naming the discarded first row -- even with an internally-consistent `raw`/`value` for that
    row -- is the same class of bug as naming the wrong key outright."""

    _, _, closure, model = _decode(VALID_SAMPLE)
    document, binary = exporter.build_document(model)
    extension = document["extensions"][SOUND_SCHEME_EXTENSION]
    early = _parameter(model, "RandomSound[0]", "volume")
    assert early.value == "50"
    extension["scheme"]["randomSounds"][0]["volume"] = {
        "value": 50.0, "raw": "50", "parameter": early.index,
    }
    with pytest.raises(SoundSchemeGlbValidationError):
        validation.validate_document(document, binary)


# --- dependency sourcePath / resolved guards ----------------------------------------------------


def test_validation_rejects_a_dependency_with_the_wrong_source_path():
    _, _, closure, model = _decode(VALID_SAMPLE)
    document, binary = exporter.build_document(model)
    extension = document["extensions"][SOUND_SCHEME_EXTENSION]
    row = next(r for r in extension["dependencies"] if r["role"] == "sound")
    row["sourcePath"] = "totally/wrong/path.wav"
    with pytest.raises(SoundSchemeGlbValidationError):
        validation.validate_document(document, binary)


def test_validation_rejects_a_dependency_with_a_non_boolean_resolution():
    """The shared contract's own `validate_extension_root` already refuses a non-boolean
    `resolved` before this seam's own `_check_dependencies` runs; this pins that this seam's own
    guard agrees (see `UnitValidationError`, the shared base `SoundSchemeGlbValidationError`
    itself derives from)."""

    _, _, closure, model = _decode(VALID_SAMPLE)
    document, binary = exporter.build_document(model)
    extension = document["extensions"][SOUND_SCHEME_EXTENSION]
    extension["dependencies"][0]["resolved"] = "yes"
    with pytest.raises(UnitValidationError):
        validation.validate_document(document, binary)


# --- parameter offset vs byte-ledger cross-check ------------------------------------------------


def test_validation_rejects_a_parameter_offset_that_disagrees_with_the_byte_ledger():
    _, _, closure, model = _decode(VALID_SAMPLE)
    document, binary = exporter.build_document(model)
    document["extensions"][SOUND_SCHEME_EXTENSION]["parameters"][-1]["offset"] += 100000
    with pytest.raises(SoundSchemeGlbValidationError):
        validation.validate_document(document, binary)


def test_export_writes_a_unit_that_round_trips_through_standalone_validation(tmp_path):
    index, read_bytes = _fake_install(
        {SCHEME_PATH: VALID_SAMPLE, "scripts/dsp_presets.txt": b"{ 7 LINEAR 0.2 0.8 0.0 0.0 70 0.75 { 0 0 } }"}
    )
    dsp_ids = load_dsp_preset_ids(index, read_bytes=read_bytes)
    assert dsp_ids == frozenset({"7"})
    destination = exporter.export(index, "testscheme", tmp_path, read_bytes=read_bytes, dsp_preset_ids=dsp_ids)
    assert destination == tmp_path / "testscheme.glb"
    summary = validation.validate(destination)
    assert summary["byteCoveragePercent"] == 100.0
    assert summary["asset"] == "vtmb:sound-scheme:testscheme"
    warnings = validation.warnings_for(summary)
    assert any("fx/drip.wav" in warning for warning in warnings)


def test_write_glb_then_validate_agree_on_a_hand_built_document(tmp_path):
    _, _, closure, model = _decode(VALID_SAMPLE)
    document, binary = exporter.build_document(model)
    destination = tmp_path / "testscheme.glb"
    write_glb(document, binary, destination)
    summary = validation.validate(destination)
    assert summary["music"] is True
    assert summary["combat"] is True
    assert summary["alert"] is False
    assert summary["ambient"] is True
