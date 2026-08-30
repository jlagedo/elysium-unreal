"""Synthetic contract tests for the isolated Expression-table GLB exporter."""

from __future__ import annotations

import struct

import pytest

from elysium_pipeline.exporters import expression_table_glb as exporter
from elysium_pipeline.formats.expression_table_glb import (
    ExpressionTableSourceError,
    compare_tables,
    decode_expression_table,
    decode_txt,
    decode_vfe,
    identity_class,
    load_source_closure,
    normalize_stem,
    output_relative_path,
    source_keys,
    stem_asset_id,
)
from elysium_pipeline.formats.expression_table_glb.decode import ExpressionTableDecodeError
from elysium_pipeline.formats.unit_contract import (
    ByteLedgerError,
    Origin,
    SourceMember,
    container,
)
from elysium_pipeline.validation import expression_table_glb as validation

KEYS = ["right_cheek_raiser", "lower_lip"]
STEM = "test_phonemes"
VFE_PATH = f"expressions/{STEM}.vfe"
TXT_PATH = f"expressions/{STEM}.txt"

TXT = (
    b"$keys right_cheek_raiser lower_lip\r\n"
    b"$hasweighting\r\n"
    b'// two rows, matching the synthetic VFE below\r\n'
    b'"b" "b" 0.350 1.000 0.000 0.000 "Big : voiced alveolar stop"\r\n'
    b'"m" "m" 0.000 0.000 0.590 1.000 "Mat : voiced bilabial nasal"\r\n'
)


def _build_vfe(
    *,
    path_name: str = VFE_PATH,
    keys: list[str] = KEYS,
    settings: list[dict] | None = None,
    trailing_padding: int = 0,
    trailing_bytes: bytes | None = None,
    declared_length: int | None = None,
    key_mapping_gap: bytes | None = None,
) -> bytes:
    if settings is None:
        settings = [
            {"name": "b", "class_index": ord("b"), "values": [(0, 0.35, 1.0), (1, 0.0, 0.0)]},
            {"name": "m", "class_index": ord("m"), "values": [(0, 0.0, 0.0), (1, 0.59, 1.0)]},
        ]

    header_size = 172
    cursor = header_size
    setting_offset = cursor
    cursor += len(settings) * 24

    name_offsets, name_blobs = [], []
    for setting in settings:
        name_offsets.append(cursor)
        blob = setting["name"].encode("ascii") + b"\0"
        name_blobs.append(blob)
        cursor += len(blob)

    value_offsets, value_blobs = [], []
    for setting in settings:
        value_offsets.append(cursor)
        if setting.get("kind", 0) == 0:
            blob = b"".join(struct.pack("<iff", k, v, w) for k, v, w in setting["values"])
        else:
            # A markov (`kind != 0`) record stores opaque 8-byte slots this seam never decodes;
            # the content is arbitrary for a test that only checks the ledger claims it made.
            blob = setting.get("raw_values", b"\xab" * (8 * setting.get("count", 0)))
        value_blobs.append(blob)
        cursor += len(blob)

    gap = key_mapping_gap or b""
    key_name_offset = cursor
    cursor += len(keys) * 4
    cursor += len(gap)
    key_mapping_offset = cursor
    cursor += len(keys) * 4

    key_string_offsets, key_blobs = [], []
    for key in keys:
        key_string_offsets.append(cursor)
        blob = key.encode("ascii") + b"\0"
        key_blobs.append(blob)
        cursor += len(blob)

    total_length = cursor + trailing_padding + len(trailing_bytes or b"")

    header = bytearray(header_size)
    header[0:4] = b"EFV\0"
    struct.pack_into("<i", header, 4, 0)
    header[8:136] = path_name.encode("ascii")[:127].ljust(128, b"\0")
    struct.pack_into("<i", header, 136, declared_length if declared_length is not None else total_length)
    struct.pack_into("<i", header, 140, len(settings))
    struct.pack_into(
        "<7i", header, 144, setting_offset, 0, 0, 0, len(keys), key_name_offset, key_mapping_offset
    )

    settings_bytes = bytearray()
    for index, setting in enumerate(settings):
        record = setting_offset + index * 24
        settings_bytes += struct.pack(
            "<6i",
            name_offsets[index] - record,
            setting.get("kind", 0),
            setting.get("count", len(setting.get("values", ()))),
            setting["class_index"],
            0,
            value_offsets[index] - record,
        )

    data = bytearray(total_length)
    data[0:header_size] = header
    data[setting_offset:setting_offset + len(settings_bytes)] = settings_bytes
    position = setting_offset + len(settings_bytes)
    for blob in name_blobs + value_blobs:
        data[position:position + len(blob)] = blob
        position += len(blob)
    key_offsets_table = b"".join(struct.pack("<i", offset) for offset in key_string_offsets)
    data[position:position + len(key_offsets_table)] = key_offsets_table
    position += len(key_offsets_table)
    if gap:
        data[position:position + len(gap)] = gap
        position += len(gap)
    key_mapping_table = b"".join(struct.pack("<i", index) for index in range(len(keys)))
    data[position:position + len(key_mapping_table)] = key_mapping_table
    position += len(key_mapping_table)
    for blob in key_blobs:
        data[position:position + len(blob)] = blob
        position += len(blob)
    if trailing_bytes:
        data[position:position + len(trailing_bytes)] = trailing_bytes
    return bytes(data)


VFE = _build_vfe()


def _index(vfe: bytes | None = VFE, txt: bytes | None = TXT):
    entries = {}
    payloads = {}
    if vfe is not None:
        entries[VFE_PATH] = ("loose", "C:/game/Vampire/" + VFE_PATH)
        payloads[VFE_PATH] = vfe
    if txt is not None:
        entries[TXT_PATH] = ("loose", "C:/game/Vampire/" + TXT_PATH)
        payloads[TXT_PATH] = txt
    return entries, (lambda index, key: payloads.get(key))


def _closure(vfe: bytes | None = VFE, txt: bytes | None = TXT):
    index, read_bytes = _index(vfe, txt)
    return load_source_closure(index, STEM, read_bytes=read_bytes)


def _member(role: str, path: str, data: bytes) -> SourceMember:
    return SourceMember(role=role, path=path, data=data, origin=Origin(kind="loose", root="Vampire"))


# --- identity / key rule -----------------------------------------------------------------------


def test_the_stem_is_folded_and_the_extension_and_directory_are_dropped():
    assert normalize_stem(r"Expressions\LaCroix_Phonemes.VFE") == "lacroix_phonemes"
    assert normalize_stem("lacroix_phonemes.txt") == "lacroix_phonemes"


def test_the_asset_id_and_output_path_follow_the_kind_and_family():
    assert stem_asset_id("Lacroix_Phonemes") == "vtmb:expression-table:lacroix_phonemes"
    assert output_relative_path("Lacroix_Phonemes").as_posix() == (
        "expression-tables/lacroix_phonemes.glb"
    )


def test_an_install_relative_path_is_reduced_to_its_own_basename():
    assert normalize_stem("expressions/lacroix_phonemes.vfe") == "lacroix_phonemes"


def test_an_empty_stem_is_refused():
    with pytest.raises(ValueError):
        normalize_stem("")


def test_the_identity_class_comes_from_the_stems_own_suffix():
    assert identity_class("crooked_cop_expressions") == "expressions"
    assert identity_class("lacroix_phonemes") == "phonemes"
    assert identity_class("phonemes") == "none"
    assert identity_class("phonemes_male") == "none"


# --- source resolution -------------------------------------------------------------------------


def test_source_keys_merges_vfe_and_txt_stems_without_duplicates():
    index, _ = _index()
    index["expressions/demal_expressions.vfe"] = ("loose", "x")
    index["expressions/scrubs_female_phonemes.txt"] = ("loose", "x")
    assert source_keys(index) == [
        "demal_expressions",
        "scrubs_female_phonemes",
        "test_phonemes",
    ]


def test_a_stem_with_neither_member_is_refused():
    index, read_bytes = _index()
    with pytest.raises(ExpressionTableSourceError):
        load_source_closure(index, "no_such_stem", read_bytes=read_bytes)


def test_a_txt_only_closure_carries_no_vfe_member():
    closure = _closure(vfe=None)
    assert closure.vfe is None and closure.txt is not None
    assert [member.role for member in closure.members()] == ["txt"]


# --- VFE decode ----------------------------------------------------------------------------


def test_every_vfe_byte_is_claimed_exactly_once():
    model = decode_expression_table(_closure())
    row = next(r for r in model.byte_ledger if r["sourcePath"] == VFE_PATH)
    assert row["coveragePercent"] == 100.0
    cursor = 0
    for entry in row["ranges"]:
        assert entry["offset"] == cursor
        cursor += entry["length"]
    assert cursor == row["byteLength"] == len(VFE)


def test_the_directory_words_are_carried_as_typed_unidentified():
    vfe, table, claims, typed, anomalies, omissions, unsupported = decode_vfe(
        _member("vfe", VFE_PATH, VFE)
    )
    assert [entry["field"] for entry in typed] == [
        "settingOffset",
        "tableNameOffset",
        "indexCount",
        "indexOffset",
        "keyCount",
        "keyNameOffset",
        "keyMappingOffset",
    ]
    assert all(entry["sourceOffset"] >= 144 for entry in typed)
    assert table["keys"] == KEYS
    assert not anomalies and not omissions and not unsupported


def test_trailing_zero_bytes_are_padding_not_an_omission():
    data = _build_vfe(trailing_padding=4)
    vfe, table, claims, typed, anomalies, omissions, unsupported = decode_vfe(
        _member("vfe", VFE_PATH, data)
    )
    assert omissions == []
    trailing = [c for c in claims if c[3] == "vfe.trailing"]
    assert trailing and trailing[0][2] == "padding-zero"


def test_non_zero_trailing_bytes_are_an_omission_not_padding():
    data = _build_vfe(trailing_bytes=b"\x01\x02\x03")
    vfe, table, claims, typed, anomalies, omissions, unsupported = decode_vfe(
        _member("vfe", VFE_PATH, data)
    )
    assert omissions == [
        {"role": "trailing-fill", "sourceOffset": omissions[0]["sourceOffset"], "length": 3}
    ]
    trailing = [c for c in claims if c[3] == "vfe.trailing"]
    assert trailing and trailing[0][2] == "omitted-proven"


def test_a_declared_length_mismatch_is_a_named_anomaly():
    data = _build_vfe(declared_length=99999)
    _, _, _, _, anomalies, _, _ = decode_vfe(_member("vfe", VFE_PATH, data))
    assert any(row["role"] == "declared-length-mismatch" for row in anomalies)


def test_an_internal_name_mismatch_is_a_named_anomaly():
    data = _build_vfe(path_name="expressions/some_other_file.vfe")
    _, _, _, _, anomalies, _, _ = decode_vfe(_member("vfe", VFE_PATH, data))
    assert any(row["role"] == "internal-name-mismatch" for row in anomalies)


def test_a_zero_key_table_decodes_to_empty_keys_and_values():
    data = _build_vfe(
        keys=[],
        settings=[
            {"name": "row0", "class_index": ord("_"), "values": []},
            {"name": "row1", "class_index": ord("_"), "values": []},
        ],
    )
    vfe, table, *_ = decode_vfe(_member("vfe", VFE_PATH, data))
    assert table["keys"] == []
    assert all(row["values"] == [] and row["weights"] == [] for row in table["rows"])


def test_a_markov_settings_value_bytes_are_carried_as_typed_unidentified_not_a_bare_mapped_claim():
    """A `kind != 0` record's value bytes are unsupported and represent no table row, so the
    ledger's `mapped` claim over them is only true because they are also published verbatim."""

    data = _build_vfe(
        settings=[
            {"name": "b", "class_index": ord("b"), "values": [(0, 0.35, 1.0), (1, 0.0, 0.0)]},
            {"name": "markov1", "class_index": 0, "kind": 1, "count": 2},
        ]
    )
    vfe, table, claims, typed, anomalies, omissions, unsupported = decode_vfe(
        _member("vfe", VFE_PATH, data)
    )
    assert any(row["type"] == "markov" and row["values"] == [] for row in vfe["settings"])
    assert len(table["rows"]) == 1  # only the kind==0 setting produces a table row
    markov_unsupported = next(row for row in unsupported if row["reason"] == "markov-setting-is-not-a-table-row")
    markov_typed = next(row for row in typed if row["field"] == "settings[1].values")
    assert markov_typed["length"] == 16
    assert markov_typed["hex"] == (b"\xab" * 16).hex()
    value_claims = [c for c in claims if c[3] == "vfe.values[1]"]
    assert value_claims == [(markov_typed["sourceOffset"], 16, "mapped", "vfe.values[1]")]
    cursor = 0
    for offset, length, state, owner in sorted(claims):
        assert offset == cursor
        cursor += length
    assert cursor == len(data)


def test_a_zero_byte_interior_gap_is_alignment_padding_not_an_omission():
    data = _build_vfe(key_mapping_gap=b"\x00\x00")
    vfe, table, claims, typed, anomalies, omissions, unsupported = decode_vfe(
        _member("vfe", VFE_PATH, data)
    )
    assert omissions == []
    alignment = [c for c in claims if c[3] == "vfe.alignment"]
    assert alignment and all(c[2] == "padding-zero" for c in alignment)
    assert table["keys"] == KEYS
    cursor = 0
    for offset, length, state, owner in sorted(claims):
        assert offset == cursor
        cursor += length
    assert cursor == len(data)


def test_a_non_zero_interior_gap_is_an_omission_not_padding():
    data = _build_vfe(key_mapping_gap=b"\x01\x02")
    vfe, table, claims, typed, anomalies, omissions, unsupported = decode_vfe(
        _member("vfe", VFE_PATH, data)
    )
    assert any(row["role"] == "interior-fill" for row in omissions)
    alignment = [c for c in claims if c[3] == "vfe.alignment"]
    assert alignment and any(c[2] == "omitted-proven" for c in alignment)
    assert table["keys"] == KEYS


def test_vfe_indexes_and_key_mappings_carry_their_own_source_offset():
    vfe, table, claims, typed, anomalies, omissions, unsupported = decode_vfe(
        _member("vfe", VFE_PATH, VFE)
    )
    assert vfe["keyMappings"]["sourceOffset"] is not None
    assert vfe["keyMappings"]["values"] == [0, 1]
    assert vfe["indexes"] == {"sourceOffset": None, "values": []}


def test_the_alternate_1260_byte_layout_carries_the_body_as_typed_unidentified():
    body = bytearray(1260)
    body[0:4] = b"EFV\0"
    struct.pack_into("<i", body, 4, 0)
    body[8:8 + len(b"phonemes_strong")] = b"phonemes_strong"
    body[136:140] = struct.pack("<f", 1.0)
    body[140:144] = struct.pack("<f", 1.0)
    vfe, table, claims, typed, anomalies, omissions, unsupported = decode_vfe(
        _member("vfe", "expressions/phonemes_strong.vfe", bytes(body))
    )
    name_end = 8 + len(b"phonemes_strong") + 1
    assert table is None
    assert vfe["length"] is None and vfe["numFlexSettings"] is None
    assert len(typed) == 1
    assert typed[0]["sourceOffset"] == name_end
    assert typed[0]["length"] == 1260 - name_end
    assert typed[0]["hex"] == bytes(body)[name_end:].hex()
    assert any(row["role"] == "alternate-layout" for row in anomalies)
    # Only `id`/`version`/`name` are ever published for this layout; nothing past the name's own
    # terminator is claimed under `vfe.header` -- the whole 136-byte widened-name offset is
    # meaningless for these two files, per the blocking-review fix.
    assert claims[0] == (0, name_end, "mapped", "vfe.header")
    assert claims[1] == (name_end, 1260 - name_end, "mapped", "vfe.body")
    cursor = 0
    for offset, length, state, owner in claims:
        assert offset == cursor
        cursor += length
    assert cursor == 1260


def test_a_bad_magic_refuses_the_vfe():
    with pytest.raises(ExpressionTableDecodeError):
        decode_vfe(_member("vfe", VFE_PATH, b"NOPE" + bytes(200)))


# --- TXT decode ------------------------------------------------------------------------------


def test_every_txt_byte_is_claimed_exactly_once():
    _, _, claims, _, _ = decode_txt(_member("txt", TXT_PATH, TXT))
    cursor = 0
    for offset, length, state, owner in sorted(claims):
        assert offset == cursor
        cursor += length
    assert cursor == len(TXT)


def test_the_txt_authoring_table_matches_the_synthetic_rows():
    _, authoring, _, unsupported, _ = decode_txt(_member("txt", TXT_PATH, TXT))
    assert authoring["keys"] == KEYS
    assert authoring["hasWeighting"] is True
    assert [row["name"] for row in authoring["rows"]] == ["b", "m"]
    assert authoring["rows"][0]["values"] == [0.35, 0.0]
    assert authoring["rows"][0]["weights"] == [1.0, 0.0]
    assert authoring["rows"][0]["phonemeCode"] == ord("b")
    assert not unsupported


def test_a_row_before_keys_refuses_the_txt():
    body = b'"b" "b" 0.0 0.0 "d"\r\n$keys a\r\n'
    with pytest.raises(ExpressionTableDecodeError):
        decode_txt(_member("txt", TXT_PATH, body))


def test_an_unterminated_quote_raises_the_shared_decode_error_not_the_lexers_own():
    body = b'$keys a\r\n"unterminated row\r\n'
    with pytest.raises(ExpressionTableDecodeError):
        decode_txt(_member("txt", TXT_PATH, body))


def test_an_unknown_directive_is_unsupported_but_still_claimed():
    body = b"$keys a\r\n$unknowndirective\r\n"
    _, _, claims, unsupported, _ = decode_txt(_member("txt", TXT_PATH, body))
    assert unsupported and unsupported[0]["field"] == "$unknowndirective"
    cursor = 0
    for offset, length, state, owner in sorted(claims):
        assert offset == cursor
        cursor += length
    assert cursor == len(body)


def test_a_zero_key_txt_table_permits_a_row_with_no_numbers():
    body = b"$keys\r\n$hasweighting\r\n" b'"row0" "_" "a label with no numbers"\r\n'
    _, authoring, _, _, _ = decode_txt(_member("txt", TXT_PATH, body))
    assert authoring["keys"] == []
    assert authoring["rows"][0]["values"] == [] and authoring["rows"][0]["weights"] == []
    assert authoring["rows"][0]["phonemeCode"] is None


def test_a_blank_line_is_an_omission_not_a_silent_drop():
    body = b"$keys a\r\n\r\n" b'"x" "_" 1.0 "d"\r\n'
    txt, authoring, claims, unsupported, omissions = decode_txt(_member("txt", TXT_PATH, body))
    assert [row for row in omissions if row["role"] == "whitespace"]
    blank_claims = [c for c in claims if c[3] == "txt.whitespace"]
    assert blank_claims and blank_claims[0][2] == "omitted-proven"


def test_a_comment_line_is_mapped_text_and_carried_in_txt_comments():
    prefix = b"$keys a\r\n"
    body = prefix + b"// a note\r\n" + b'"x" "_" 1.0 "d"\r\n'
    txt, authoring, claims, unsupported, omissions = decode_txt(_member("txt", TXT_PATH, body))
    assert txt["comments"] == [{"sourceOffset": len(prefix), "text": "// a note"}]
    comment_claims = [c for c in claims if c[3] == "txt.comments"]
    assert comment_claims and comment_claims[0][2] == "mapped-text"


def test_a_bom_is_an_omission_not_mapped_content():
    body = b"\xef\xbb\xbf$keys a\r\n" b'"x" "_" 1.0 "d"\r\n'
    txt, authoring, claims, unsupported, omissions = decode_txt(_member("txt", TXT_PATH, body))
    assert [row for row in omissions if row["role"] == "bom" and row["sourceOffset"] == 0]
    bom_claims = [c for c in claims if c[3] == "txt.bom"]
    assert bom_claims and bom_claims[0][2] == "omitted-proven"
    cursor = 0
    for offset, length, state, owner in sorted(claims):
        assert offset == cursor
        cursor += length
    assert cursor == len(body)


# --- comparison --------------------------------------------------------------------------------


def _table(keys, rows):
    return {"keys": keys, "hasWeighting": True, "rows": rows}


def _row(index, name, values, weights):
    return {
        "index": index, "name": name, "class": "_", "phonemeCode": None,
        "values": values, "weights": weights, "description": "",
    }


def test_identical_tables_compare_equivalent():
    table = _table(["a", "b"], [_row(0, "x", [1.0, 2.0], [1.0, 1.0])])
    comparison, anomalies, unresolved, omissions = compare_tables(table, table)
    assert comparison == {"state": "equivalent"}
    assert not anomalies and not unresolved and not omissions


def test_a_missing_twin_compares_no_twin_and_is_an_omission():
    table = _table(["a"], [_row(0, "x", [1.0], [1.0])])
    comparison, anomalies, unresolved, omissions = compare_tables(
        table, None, vfe_present=True, txt_present=False
    )
    assert comparison == {"state": "no-twin"}
    assert omissions == [{"role": "no-twin", "missingMember": "txt"}]
    comparison, _, _, omissions = compare_tables(None, table, vfe_present=False, txt_present=True)
    assert comparison == {"state": "no-twin"}
    assert omissions == [{"role": "no-twin", "missingMember": "vfe"}]


def test_an_alternate_layout_vfe_with_a_txt_twin_is_not_comparable_not_no_twin():
    """Both members exist, but the VFE decoded no `table` -- distinct from a missing twin."""

    table = _table(["a"], [_row(0, "x", [1.0], [1.0])])
    comparison, anomalies, unresolved, omissions = compare_tables(
        None, table, vfe_present=True, txt_present=True
    )
    assert comparison == {"state": "not-comparable"}
    assert anomalies == [{"role": "not-comparable"}]
    assert not unresolved and not omissions


def test_a_small_rounding_only_delta_is_within_tolerance():
    table = _table(["a"], [_row(0, "x", [0.1234567], [1.0])])
    authoring = _table(["a"], [_row(0, "x", [0.123], [1.0])])
    comparison, anomalies, unresolved, omissions = compare_tables(table, authoring)
    assert comparison["state"] == "rounding-only"
    assert comparison["maxDelta"] == pytest.approx(0.0004567, abs=1e-6)
    assert anomalies == [{"role": "rounding-only", "maxDelta": comparison["maxDelta"]}]
    assert not unresolved


def test_extra_keys_on_the_vfe_side_are_named():
    table = _table(["a", "b"], [_row(0, "x", [1.0, 2.0], [1.0, 1.0])])
    authoring = _table(["a"], [_row(0, "x", [1.0], [1.0])])
    comparison, anomalies, unresolved, omissions = compare_tables(table, authoring)
    assert comparison["state"] == "extra-keys"
    assert comparison["extraKeys"] == ["b"]
    assert anomalies == [{"role": "extra-keys", "keys": ["b"]}]


def test_extra_keys_with_a_shared_key_value_disagreement_is_also_unresolved():
    """A structural difference (`extra-keys`) never excuses dropping a values fact."""

    table = _table(["a", "b"], [_row(0, "x", [1.0, 2.0], [1.0, 1.0])])
    authoring = _table(["a"], [_row(0, "x", [0.0], [1.0])])
    comparison, anomalies, unresolved, omissions = compare_tables(table, authoring)
    assert comparison["state"] == "extra-keys"
    assert comparison["maxDelta"] == pytest.approx(1.0)
    assert any(
        row["reason"] == "value-delta-exceeds-rounding-tolerance" and row["maxDelta"] == pytest.approx(1.0)
        for row in unresolved
    )
    assert any(row["role"] == "value-delta-exceeds-rounding-tolerance" for row in anomalies)
    assert any(row["role"] == "extra-keys" for row in anomalies)


def test_a_weight_only_disagreement_beyond_tolerance_is_unresolved():
    table = _table(["a"], [_row(0, "x", [1.0], [1.0])])
    authoring = _table(["a"], [_row(0, "x", [1.0], [0.0])])
    comparison, anomalies, unresolved, omissions = compare_tables(table, authoring)
    assert comparison["state"] == "unresolved"
    assert comparison["maxWeightDelta"] == pytest.approx(1.0)
    assert any(row["reason"] == "value-delta-exceeds-rounding-tolerance" for row in unresolved)
    assert anomalies == [{"role": "value-delta-exceeds-rounding-tolerance", "maxDelta": 0.0, "maxWeightDelta": pytest.approx(1.0)}]


def test_disagreeing_key_sets_that_are_not_extra_keys_are_unresolved_and_anomalous():
    table = _table(["a"], [_row(0, "x", [1.0], [1.0])])
    authoring = _table(["c"], [_row(0, "x", [1.0], [1.0])])
    comparison, anomalies, unresolved, omissions = compare_tables(table, authoring)
    assert comparison["state"] == "unresolved"
    assert any(row["reason"] == "table-and-authoring-key-sets-disagree" for row in unresolved)
    assert anomalies == [{"role": "unresolved"}]


def test_extra_keys_with_disagreeing_row_counts_is_still_a_named_anomaly():
    table = _table(
        ["a", "b"],
        [_row(0, "x", [1.0, 2.0], [1.0, 1.0]), _row(1, "y", [1.0, 2.0], [1.0, 1.0])],
    )
    authoring = _table(["a"], [_row(0, "x", [1.0], [1.0])])
    comparison, anomalies, unresolved, omissions = compare_tables(table, authoring)
    assert comparison["state"] == "extra-keys"
    assert anomalies == [{"role": "extra-keys", "keys": ["b"]}]
    assert any(row["reason"] == "row-count-disagrees" for row in unresolved)


def test_two_swapped_rows_compare_row_order():
    table = _table(["a"], [_row(0, "p", [1.0], [1.0]), _row(1, "b", [2.0], [1.0])])
    authoring = _table(["a"], [_row(0, "b", [2.0], [1.0]), _row(1, "p", [1.0], [1.0])])
    comparison, anomalies, unresolved, omissions = compare_tables(table, authoring)
    assert comparison["state"] == "row-order"
    assert comparison["indices"] == [0, 1]


def test_one_renamed_row_compares_row_renamed():
    table = _table(["a"], [_row(0, "bliss", [1.0], [1.0])])
    authoring = _table(["a"], [_row(0, "eyes closed", [1.0], [1.0])])
    comparison, anomalies, unresolved, omissions = compare_tables(table, authoring)
    assert comparison["state"] == "row-renamed"
    assert comparison["index"] == 0
    assert anomalies[0]["tableName"] == "bliss" and anomalies[0]["authoringName"] == "eyes closed"


# --- full model / dependencies / selectedBy -----------------------------------------------------


def test_the_unit_resolves_no_dependency():
    model = decode_expression_table(_closure())
    document, binary = exporter.build_document(model)
    root = document["extensions"][list(document["extensions"])[0]]
    assert root["dependencies"] == []
    assert root["selectedBy"] == []
    assert binary == b""


def test_a_twin_pair_decodes_to_the_expected_source_kind_and_identity():
    model = decode_expression_table(_closure())
    assert model.source_kind == "vfe+txt"
    assert model.runtime_loadable is True
    assert model.identity_class_ == "phonemes"
    assert model.comparison["state"] == "equivalent"


def test_a_txt_only_unit_is_not_runtime_loadable():
    model = decode_expression_table(_closure(vfe=None))
    assert model.source_kind == "txt-only"
    assert model.runtime_loadable is False
    assert model.comparison == {"state": "no-twin"}


# --- validation --------------------------------------------------------------------------------


def test_a_unit_the_corpus_index_back_filled_still_validates(tmp_path):
    """`selectedBy[]` is empty at export and written in place by the corpus index; validation
    reads a published unit in both states."""

    from elysium_pipeline.formats.corpus_index_glb import backfill

    index, read_bytes = _index()
    destination = exporter.export(index, STEM, tmp_path, read_bytes=read_bytes)
    asset = "vtmb:expression-table:" + STEM
    rows = [{"from": "vtmb:scene:talk", "role": "expression-table"}]
    assert backfill.rewrite(destination, asset, rows) is not None
    validation.validate(destination)
    document, _ = container.read_glb(destination)
    assert document["extensions"]["ELYSIUM_vtmb_expression_table"]["selectedBy"] == rows
    with pytest.raises(validation.ExpressionTableGlbValidationError):
        backfill.rewrite(destination, asset, ["vtmb:scene:talk"])
        validation.validate(destination)


def test_a_published_unit_round_trips_through_validation(tmp_path):
    index, read_bytes = _index()
    destination = exporter.export(index, STEM, tmp_path, read_bytes=read_bytes)
    summary = validation.validate(destination)
    assert summary["asset"] == "vtmb:expression-table:test_phonemes"
    assert summary["sourceKind"] == "vfe+txt"
    assert summary["comparisonState"] == "equivalent"
    assert summary["byteCoveragePercent"] == 100.0
    # The directory words are always carried as typedUnidentified until the widened-name layout
    # is verified against the loader, so even a clean twin publishes with a warning per word.
    assert summary["typedUnidentified"] == 7
    assert len(validation.warnings_for(summary)) == 7


def test_a_tampered_ledger_is_refused_at_export_time():
    model = decode_expression_table(_closure())
    document, binary = exporter.build_document(model)
    root = document["extensions"][list(document["extensions"])[0]]
    root["coverage"]["byteLedger"][0]["ranges"][0]["length"] += 1
    with pytest.raises(Exception):
        validation.validate_document(document, binary, source_members=model.members)


def test_standalone_validation_catches_a_row_whose_values_disagree_with_keys(tmp_path):
    index, read_bytes = _index()
    destination = exporter.export(index, STEM, tmp_path, read_bytes=read_bytes)
    document, binary = validation.read_glb(destination)
    root = document["extensions"][list(document["extensions"])[0]]
    root["table"]["rows"][0]["values"] = root["table"]["rows"][0]["values"][:1]
    with pytest.raises(validation.ExpressionTableGlbValidationError):
        validation.validate_document(document, binary)


def test_standalone_validation_catches_an_undisclosed_row_count_mismatch(tmp_path):
    index, read_bytes = _index()
    destination = exporter.export(index, STEM, tmp_path, read_bytes=read_bytes)
    document, binary = validation.read_glb(destination)
    root = document["extensions"][list(document["extensions"])[0]]
    root["authoring"]["rows"] = root["authoring"]["rows"][:1]
    with pytest.raises(validation.ExpressionTableGlbValidationError):
        validation.validate_document(document, binary)


def test_export_fails_closed_when_a_source_byte_is_swapped_after_decode():
    closure = _closure()
    model = decode_expression_table(closure)
    document, binary = exporter.build_document(model)
    tampered_vfe = _member("vfe", VFE_PATH, b"\xffXXX" + VFE[4:])
    with pytest.raises(Exception):
        validation.validate_document(document, binary, source_members=(tampered_vfe, closure.txt))
