"""Tests for the `font_glb` export_v2 seam: `vtmb:font:` and `vtmb:font-list:` units.

Every fixture here is synthetic, built byte-for-byte against the reverse-engineered `.fnt` layout
(`formats/fnt.py`); no test touches the real VtMB install.
"""

from __future__ import annotations

from pathlib import Path
import struct

import pytest

from elysium_pipeline.exporters import font_glb as exporter
from elysium_pipeline.formats.font_glb import (
    FONT_EXTENSION,
    FONT_LIST_EXTENSION,
    FONT_LIST_KEY,
    decode_font,
    decode_font_list,
    parse_registry_rows,
)
from elysium_pipeline.formats.font_glb.decode import GLYPH_ENTRY_SIZE
from elysium_pipeline.formats.font_glb.model import (
    asset_id,
    font_list_asset_id,
    font_path,
    normalize_font_key,
    parse_name,
    registry_key,
)
from elysium_pipeline.formats.font_glb.source import (
    FontListSourceClosure,
    FontSourceClosure,
    PageAvailability,
    font_keys,
    load_font_list_closure,
    load_source_closure,
)
from elysium_pipeline.formats.unit_contract import ByteLedgerError, Origin, SourceMember
from elysium_pipeline.validation import font_glb as validation

GLYPH_TABLE_OFFSET = 292


# --- synthetic byte builders -------------------------------------------------------------------


def _glyph_bytes(
    *,
    advance: int = 0,
    left_bearing: int = 0,
    height: int = 0,
    page: int = 0,
    width: int = 0,
    uv: tuple[float, float, float, float] = (0.0, 0.0, 0.0, 0.0),
    unread: tuple[tuple[int, int, bytes], ...] = (),
) -> bytes:
    buf = bytearray(44)
    buf[0] = advance & 0xFF
    struct.pack_into("<b", buf, 18, left_bearing)
    struct.pack_into("<h", buf, 20, -height)
    buf[24] = page & 0xFF
    struct.pack_into("<h", buf, 26, width)
    struct.pack_into("<4f", buf, 28, *uv)
    for start, length, chunk in unread:
        buf[start:start + length] = chunk.ljust(length, b"\x00")[:length]
    return bytes(buf)


def _fnt_bytes(
    *,
    pages: int = 1,
    line_height: int = 10,
    glyphs: list[bytes],
    charmap: bytes | None = None,
    trailer: bytes = b"",
    word1: int | None = None,
    word2: int = 0,
    word3: int = 0,
    word5: int = 0,
    word6: int = 0,
    word7: int = 0,
) -> bytes:
    count = len(glyphs)
    if charmap is None:
        # Byte 255 carries the top index so `max(charmap) + 1 == count`; every other codepoint
        # maps to glyph 0, so an interior index with nothing pointing to it is a normal shape.
        charmap = bytes([0] * 255 + [count - 1])
    table_end = GLYPH_TABLE_OFFSET + count * 44
    header = struct.pack(
        "<9I",
        pages,
        table_end if word1 is None else word1,
        word2,
        word3,
        line_height,
        word5,
        word6,
        word7,
        GLYPH_TABLE_OFFSET,
    )
    return header + charmap + b"".join(glyphs) + trailer


ORIGIN = Origin(kind="loose", root="Unofficial_Patch")


def _closure(key: str, data: bytes, *, pages=None, font_list_present=True) -> FontSourceClosure:
    member = SourceMember(role="fnt", path=font_path(key), data=data, origin=ORIGIN)
    return FontSourceClosure(
        key=key,
        asset_id=asset_id(key),
        fnt=member,
        available_pages=pages or {},
        font_list_present=font_list_present,
    )


def _page(
    key: str,
    index: int,
    *,
    tth: bool = True,
    vmt: bool = True,
    declared_texture: str | None = None,
) -> PageAvailability:
    """A page as `_scan_pages` would build it: `declared_texture`, when given, stands in for a
    `.vmt`'s own `$basetexture` resolving to a shipped texture the naive `<stem>-page<n>` guess is
    not."""

    tth_path = f"materials/fonts/{key}-page{index}.tth"
    if declared_texture is not None:
        resolved_texture_path = declared_texture
        resolved_texture_present = True
        basetexture_declared = True
    else:
        resolved_texture_path = tth_path
        resolved_texture_present = tth
        basetexture_declared = False
    return PageAvailability(
        index=index,
        vmt_path=f"materials/fonts/{key}-page{index}.vmt",
        vmt_present=vmt,
        resolved_texture_path=resolved_texture_path,
        resolved_texture_present=resolved_texture_present,
        basetexture_declared=basetexture_declared,
    )


# A fully clean, fully resolved font: no anomalies, zero unresolved, exercises both the
# reserved-zero and typedUnidentified header/glyph-unread paths.
CLEAN_KEY = "cleanface_12_400_000"
CLEAN_GLYPH = _glyph_bytes(advance=5, left_bearing=1, height=6, page=0, width=4, uv=(0.0, 0.0, 0.5, 0.5))
CLEAN_DATA = _fnt_bytes(pages=1, line_height=9, glyphs=[CLEAN_GLYPH], word2=2, charmap=bytes(256))


def _clean_closure() -> FontSourceClosure:
    return _closure(CLEAN_KEY, CLEAN_DATA, pages={0: _page(CLEAN_KEY, 0)})


# A quirky font: three glyphs, exercising every anomaly and both unread-span outcomes, still
# fully resolved (pages 0 and 9 both present) so it is not also `unresolved`.
QUIRKY_KEY = "quirkyface_08_000_008"
QUIRKY_G0 = _glyph_bytes(advance=5, left_bearing=2, height=8, page=0, width=6, uv=(0.0, 0.0, 0.5, 0.5))
QUIRKY_G1 = _glyph_bytes(
    advance=7,
    left_bearing=-3,
    height=-4,
    page=0,
    width=-2,
    uv=(0.1, 0.1, 0.9, 0.9),
    unread=((1, 17, bytes([9])), (19, 1, b"\x01"), (22, 2, bytes([2, 0])), (25, 1, b"\x09")),
)
QUIRKY_G2 = _glyph_bytes(advance=1, page=9, width=1, uv=(0.9, 0.9, 0.1, 0.1))
QUIRKY_DATA = _fnt_bytes(
    pages=1,
    line_height=10,
    glyphs=[QUIRKY_G0, QUIRKY_G1, QUIRKY_G2],
    word2=1,
    word6=5,
    trailer=b"\x00\x00\x01\x02\x00",
)


def _quirky_closure() -> FontSourceClosure:
    return _closure(
        QUIRKY_KEY,
        QUIRKY_DATA,
        pages={0: _page(QUIRKY_KEY, 0), 9: _page(QUIRKY_KEY, 9)},
    )


FONT_LIST_TEXT = '"Cleanface" 12 400 0\r\n"Ghost Face" 8 900 2\r\n'
FONT_LIST_DATA = FONT_LIST_TEXT.encode("latin-1")


def _font_list_closure(data: bytes = FONT_LIST_DATA) -> FontListSourceClosure:
    member = SourceMember(role="font-list", path="materials/fonts/fontlist.txt", data=data, origin=ORIGIN)
    return FontListSourceClosure(source=member)


def _model_member(model, data: bytes = CLEAN_DATA) -> SourceMember:
    return SourceMember(role="fnt", path=model.members[0].path, data=data, origin=ORIGIN)


def _index_and_reader(key: str, data: bytes, *, pages=(0,), font_list=True):
    index: dict[str, tuple] = {font_path(key): ("loose", "/x/" + key + ".fnt")}
    blobs = {font_path(key): data}
    for page in pages:
        index[f"materials/fonts/{key}-page{page}.tth"] = ("vpk", ("pack0.vpk", 0, 10))
        index[f"materials/fonts/{key}-page{page}.vmt"] = ("loose", f"/x/{key}-page{page}.vmt")
    if font_list:
        index["materials/fonts/fontlist.txt"] = ("loose", "/x/fontlist.txt")
        blobs["materials/fonts/fontlist.txt"] = FONT_LIST_DATA

    def read_bytes(idx, lookup_key):
        return blobs.get(lookup_key)

    return index, read_bytes


# --- byte ledger: every byte claimed exactly once, zero-state proofs ---------------------------


def test_a_clean_fonts_ledger_claims_every_byte_exactly_once():
    model = decode_font(_clean_closure())
    row = model.byte_ledger[0]
    assert row["byteLength"] == row["accountedBytes"] == len(CLEAN_DATA)
    assert row["coveragePercent"] == 100.0
    covered = sorted(row["ranges"], key=lambda entry: entry["offset"])
    cursor = 0
    for entry in covered:
        assert entry["offset"] == cursor
        cursor += entry["length"]
    assert cursor == len(CLEAN_DATA)


def test_a_quirky_fonts_ledger_is_also_gapless_across_three_glyphs_and_a_mixed_trailer():
    model = decode_font(_quirky_closure())
    row = model.byte_ledger[0]
    assert row["byteLength"] == row["accountedBytes"] == len(QUIRKY_DATA) == 429
    assert row["coveragePercent"] == 100.0


def _fnt_bytes_with_glyph_table_offset(glyph_off: int, pad: bytes) -> bytes:
    """One glyph, header word 8 (`glyphTableOffset`) pointed past the char map's own end (292) by
    `pad` filler bytes -- `_fnt_bytes` cannot build this fixture because it hardcodes 292."""

    count = 1
    charmap = bytes([0] * 255 + [count - 1])
    table_end = glyph_off + count * GLYPH_ENTRY_SIZE
    header = struct.pack("<9I", 1, table_end, 0, 0, 10, 0, 0, 0, glyph_off)
    return header + charmap + pad + CLEAN_GLYPH


def test_a_glyph_table_offset_past_the_char_map_end_claims_the_zero_gap_instead_of_aborting():
    # A `.fnt` whose header points the glyph table past 292 used to leave the bytes between the
    # char map and the table unclaimed, aborting the whole export with a raw `ByteLedgerError`
    # (specDeviation: the seam sweeps this gap the way every other variable-length seam does).
    pad = b"\x00" * 8
    glyph_off = GLYPH_TABLE_OFFSET + len(pad)
    data = _fnt_bytes_with_glyph_table_offset(glyph_off, pad)
    closure = _closure("padface_10_400_000", data, pages={0: _page("padface_10_400_000", 0)})
    model = decode_font(closure, font_list_rows=[])
    row = model.byte_ledger[0]
    assert row["accountedBytes"] == row["byteLength"] == len(data)
    assert not any(entry["role"] == "pad-before-glyph-table" for entry in model.omissions)


def test_a_glyph_table_offset_past_the_char_map_end_with_non_zero_bytes_is_an_omission_not_a_gap():
    pad = bytes([0xAB]) * 8
    glyph_off = GLYPH_TABLE_OFFSET + len(pad)
    data = _fnt_bytes_with_glyph_table_offset(glyph_off, pad)
    closure = _closure("padface2_10_400_000", data, pages={0: _page("padface2_10_400_000", 0)})
    model = decode_font(closure, font_list_rows=[])
    row = model.byte_ledger[0]
    assert row["accountedBytes"] == row["byteLength"] == len(data)
    omission = next(entry for entry in model.omissions if entry["role"] == "pad-before-glyph-table")
    assert omission["offset"] == GLYPH_TABLE_OFFSET
    assert omission["length"] == len(pad)
    assert omission["hex"] == pad.hex()


def test_reserved_zero_header_words_and_unread_spans_are_verified_against_the_bytes():
    model = decode_font(_clean_closure())
    row = model.byte_ledger[0]
    assert row["stateBytes"]["reserved-zero"] > 0
    # header word3, word5, word7 are zero in the fixture -> reserved-zero, verified against CLEAN_DATA.
    from elysium_pipeline.formats.unit_contract import verify_ledger_row

    verify_ledger_row(row, CLEAN_DATA)
    tampered = bytearray(CLEAN_DATA)
    tampered[12] = 0x01  # word3, claimed reserved-zero
    with pytest.raises(ByteLedgerError, match="false reserved-zero claim"):
        verify_ledger_row(row, bytes(tampered))


def test_claiming_reserved_zero_over_a_non_zero_byte_is_refused_at_decode_time():
    # word2 is nonzero in the fixture (typedUnidentified); decoding must not mis-claim it zero.
    model = decode_font(_clean_closure())
    header_word2 = next(word for word in model.header["words"] if word["index"] == 2)
    assert header_word2["role"] == "typedUnidentified"
    assert header_word2["value"] == 2


# --- identity / key rule -----------------------------------------------------------------------


def test_the_font_key_is_tolerant_of_the_root_prefix_extension_and_case():
    variants = [
        "cleanface_12_400_000",
        "CLEANFACE_12_400_000",
        "materials/fonts/cleanface_12_400_000.fnt",
        "materials\\fonts\\cleanface_12_400_000.FNT",
    ]
    normalized = {normalize_font_key(variant) for variant in variants}
    assert normalized == {CLEAN_KEY}
    assert {asset_id(variant) for variant in variants} == {f"vtmb:font:{CLEAN_KEY}"}


def test_a_key_with_a_path_separator_left_over_is_refused():
    with pytest.raises(ValueError, match="invalid font key"):
        normalize_font_key("materials/fonts/sub/deep_12_400_000.fnt")


def test_the_font_list_identity_is_the_one_fixed_key():
    assert font_list_asset_id() == "vtmb:font-list:fontlist"


def test_the_font_list_unit_declares_its_own_extension_and_generator_title():
    # `font-list` is its own `<kind>`, distinct from `font`, so it declares its own extension
    # (every unit declares its own extension `ELYSIUM_vtmb_<kind>`),
    # mirroring the sibling two-identity seam, `shader_program_glb`.
    model = decode_font_list(_font_list_closure(), font_keys={CLEAN_KEY})
    document, binary = exporter.build_font_list_document(model)
    assert binary == b""
    assert document["extensionsUsed"] == [FONT_LIST_EXTENSION]
    assert document["extensionsRequired"] == [FONT_LIST_EXTENSION]
    assert set(document["extensions"]) == {FONT_LIST_EXTENSION}
    assert document["asset"]["generator"] == "Elysium Font-list GLB Exporter"


def test_the_font_units_own_extension_and_generator_title_are_unchanged():
    model = decode_font(_clean_closure())
    document, binary = exporter.build_document(model)
    assert document["extensionsUsed"] == [FONT_EXTENSION]
    assert document["extensionsRequired"] == [FONT_EXTENSION]
    assert set(document["extensions"]) == {FONT_EXTENSION}
    assert document["asset"]["generator"] == "Elysium Font GLB Exporter"


def test_the_registry_key_zero_pads_like_the_shipped_stems():
    assert registry_key("Tahoma", 16, 500, 0) == "tahoma_16_500_000"
    assert registry_key("Trebuchet MS", 24, 900, 0) == "trebuchet_ms_24_900_000"
    assert registry_key("Times New Roman", 122, 900, 0) == "times_new_roman_122_900_000"


def test_the_name_pattern_parses_a_face_that_itself_carries_underscores():
    parts = parse_name("vamp_dialog_base_18_900_000")
    assert parts is not None
    assert (parts.face, parts.size, parts.weight, parts.flags) == ("vamp_dialog_base", 18, 900, 0)
    assert parse_name("not-a-font-name") is None


# --- dependency roles --------------------------------------------------------------------------


def test_every_dependency_role_the_seam_names_appears():
    model = decode_font(_clean_closure())
    roles = {row["role"] for row in model.dependencies}
    assert roles == {"texture", "material", "font-list"}
    texture_row = next(row for row in model.dependencies if row["role"] == "texture")
    assert texture_row["asset"] == "vtmb:texture:fonts/cleanface_12_400_000-page0"
    assert texture_row["resolved"] is True


def test_the_font_list_units_dependency_role_is_font():
    model = decode_font_list(_font_list_closure())
    roles = {row["role"] for row in model.dependencies}
    assert roles == {"font"}


def test_a_page_the_glyph_table_uses_and_the_install_lacks_is_a_page_missing_anomaly_not_unresolved():
    # A missing referenced page counts as the font's own structure and grades it
    # `coverage.unresolved`; the real install ships members that reference a page it never shipped
    # at all (times_new_roman_98/122/147_900_000 -> page 1), which is the
    # non-canonical-storage case instead: a reference to another
    # seam's data that merely fails to resolve, so the unit publishes what the install holds and
    # warns rather than failing (specDeviation).
    closure = _closure(CLEAN_KEY, CLEAN_DATA, pages={}, font_list_present=True)
    model = decode_font(closure, font_list_rows=[])
    assert not model.unresolved
    row = next(row for row in model.anomalies if row["role"] == "page-missing")
    assert row["page"] == 0
    assert row["glyphs"] == [0]
    assert not any(row["role"] == "material" and row["resolved"] for row in model.dependencies)


def test_a_pages_vmt_declared_basetexture_resolves_the_page_even_when_the_naive_stem_guess_is_absent():
    # A page's own `.vmt` frequently names a texture shipped under a *different* stem than
    # `<this font>-page<n>` (real example: `tahoma_28_500_000-page0.vmt` declares
    # `fonts/Tahoma_25_500_000-page0`). The naive `.tth` guess is absent; the declared one resolves.
    page = _page(
        CLEAN_KEY, 0, tth=False, vmt=True, declared_texture="materials/fonts/other_stem-page0.tth"
    )
    closure = _closure(CLEAN_KEY, CLEAN_DATA, pages={0: page}, font_list_present=True)
    model = decode_font(closure, font_list_rows=[])
    assert not model.unresolved
    texture_row = next(row for row in model.dependencies if row["role"] == "texture")
    assert texture_row["asset"] == "vtmb:texture:fonts/other_stem-page0"
    assert texture_row["sourcePath"] == "materials/fonts/other_stem-page0.tth"
    assert texture_row["resolved"] is True


def test_a_pages_vmt_present_without_a_usable_basetexture_is_an_anomaly_and_falls_back_to_the_naive_guess():
    page = _page(CLEAN_KEY, 0, tth=False, vmt=True)  # no declared_texture: the naive guess stands
    closure = _closure(CLEAN_KEY, CLEAN_DATA, pages={0: page}, font_list_present=True)
    model = decode_font(closure, font_list_rows=[])
    assert any(row["role"] == "page-vmt-missing-basetexture" and row["page"] == 0 for row in model.anomalies)
    assert any(row["role"] == "page-missing" and row["page"] == 0 for row in model.anomalies)
    assert not model.unresolved


# --- typedUnidentified / reserved-zero / omission rules -----------------------------------------


def test_header_words_split_between_named_derived_reserved_zero_and_typed_unidentified():
    model = decode_font(_clean_closure())
    roles = {word["index"]: word["role"] for word in model.header["words"]}
    assert roles[0] == "pages"
    assert roles[4] == "lineHeight"
    assert roles[8] == "glyphTableOffset"
    assert roles[1] == "derived"          # word1 == glyphTableOffset + glyphCount * 44 here
    assert roles[2] == "typedUnidentified"
    assert roles[3] == "reserved-zero"
    assert roles[5] == "reserved-zero"
    assert roles[7] == "reserved-zero"


def test_a_header_word_that_disagrees_with_the_derived_formula_falls_back_to_typed_unidentified():
    data = _fnt_bytes(pages=1, glyphs=[CLEAN_GLYPH], word1=999999, charmap=bytes(256))
    model = decode_font(_closure("mismatchface_10_400_000", data, pages={0: _page("x", 0)}))
    word1 = next(word for word in model.header["words"] if word["index"] == 1)
    assert word1["role"] == "typedUnidentified"
    assert word1["value"] == 999999


def test_glyph_unread_spans_are_reserved_zero_when_zero_and_typed_unidentified_otherwise():
    model = decode_font(_quirky_closure())
    clean_glyph = model.glyphs[0]
    quirky_glyph = model.glyphs[1]
    assert clean_glyph["unread"] == []
    assert [record["offset"] for record in quirky_glyph["unread"]] == [337, 355, 358, 361]
    fields = {entry["field"] for entry in model.typed_unidentified}
    assert "glyphs[1].unread[0]" in fields
    assert "glyphs[1].unread[1]" in fields
    assert "glyphs[1].unread[2]" in fields
    assert "glyphs[1].unread[3]" in fields


def test_the_trailer_splits_into_reserved_zero_and_typed_unidentified_runs():
    model = decode_font(_quirky_closure())
    assert model.trailer["byteLength"] == 5
    assert model.trailer["unidentified"] == [{"offset": 426, "length": 2, "hex": "0102"}]
    assert model.trailer["names"] == []


def test_a_null_terminated_printable_run_in_the_trailer_decodes_as_a_named_string():
    # Real members carry a null-terminated name after some numeric bytes (and, on at least one
    # real member, a name naming an entirely different font -- so this is decoded as a plain
    # string, never asserted to be *this* font's own page).
    text_bytes = b"Some_Other_Font-page3"
    table_end = GLYPH_TABLE_OFFSET + 1 * 44  # one glyph, from `charmap=bytes(256)`
    trailer = b"\x01\x02\x03\x04" + text_bytes + b"\x00"
    data = _fnt_bytes(pages=1, glyphs=[CLEAN_GLYPH], charmap=bytes(256), trailer=trailer)
    model = decode_font(_closure("trailerface_10_400_000", data, pages={0: _page("x", 0)}))
    assert model.trailer["names"] == [
        {"offset": table_end + 4, "length": len(text_bytes), "text": text_bytes.decode("latin-1")}
    ]
    assert model.trailer["unidentified"] == [{"offset": table_end, "length": 4, "hex": "01020304"}]
    named_fields = {entry["field"] for entry in model.typed_unidentified if entry["field"].startswith("trailer.names")}
    assert named_fields == {"trailer.names[0]"}


def test_a_printable_run_reaching_eof_with_no_terminator_stays_opaque_hex():
    text_bytes = b"NoTerminatorHere"
    data = _fnt_bytes(pages=1, glyphs=[CLEAN_GLYPH], charmap=bytes(256), trailer=text_bytes)
    model = decode_font(_closure("noterminatorface_10_400_000", data, pages={0: _page("x", 0)}))
    assert model.trailer["names"] == []
    assert model.trailer["unidentified"] == [
        {"offset": GLYPH_TABLE_OFFSET + 44, "length": len(text_bytes), "hex": text_bytes.hex()}
    ]


def test_omitted_unreferenced_glyphs_names_the_indices_no_codepoint_reaches():
    model = decode_font(_quirky_closure())
    row = next(row for row in model.omissions if row["role"] == "unreferenced-glyphs")
    assert row["indices"] == [1]


def test_omitted_unreferenced_page_names_a_shipped_pair_no_glyph_uses():
    closure = _closure(
        CLEAN_KEY,
        CLEAN_DATA,
        pages={0: _page(CLEAN_KEY, 0), 3: _page(CLEAN_KEY, 3)},
    )
    model = decode_font(closure, font_list_rows=[])
    row = next(row for row in model.omissions if row["role"] == "unreferenced-page")
    assert row["page"] == 3


# --- anomalies -----------------------------------------------------------------------------------


def test_glyph_outside_page_negative_width_and_uv_out_of_range_are_named_anomalies():
    model = decode_font(_quirky_closure())
    roles = [(row["role"], row.get("index")) for row in model.anomalies]
    assert ("glyph-outside-page", 2) in roles
    assert ("negative-width", 1) in roles
    assert ("uv-out-of-range", 2) in roles


def test_a_stem_that_does_not_parse_is_a_name_pattern_mismatch():
    data = _fnt_bytes(pages=1, glyphs=[CLEAN_GLYPH], charmap=bytes(256))
    model = decode_font(_closure("not-a-font-stem", data, pages={0: _page("x", 0)}))
    assert any(row["role"] == "name-pattern-mismatch" for row in model.anomalies)
    assert model.name is None


# --- the font-list unit and the join in both directions -----------------------------------------


def test_font_list_rows_carry_their_own_line_span_and_compose_the_font_key():
    rows = parse_registry_rows(FONT_LIST_DATA)
    assert [row["key"] for row in rows] == ["cleanface_12_400_000", "ghost_face_08_900_002"]
    assert rows[0]["offset"] == 0
    assert FONT_LIST_DATA[rows[0]["offset"]:rows[0]["offset"] + rows[0]["length"]] == b'"Cleanface" 12 400 0\r\n'


def test_a_registry_row_naming_a_font_the_install_lacks_is_fontlist_font_missing():
    model = decode_font_list(_font_list_closure(), font_keys={CLEAN_KEY})
    assert any(row["role"] == "fontlist-font-missing" for row in model.anomalies)
    ghost = next(row for row in model.rows if row["face"] == "Ghost Face")
    assert ghost["resolved"] is False


def test_a_font_no_row_names_is_fontlist_row_missing_on_the_fonts_own_side():
    rows = parse_registry_rows(FONT_LIST_DATA)
    closure = _closure(QUIRKY_KEY, QUIRKY_DATA, pages={0: _page(QUIRKY_KEY, 0), 9: _page(QUIRKY_KEY, 9)})
    model = decode_font(closure, font_list_rows=rows)
    assert model.font_list == {"resolved": False, "index": None}
    assert any(row["role"] == "fontlist-row-missing" for row in model.anomalies)


def test_a_font_a_row_names_resolves_the_join():
    # `fontList` publishes the join outcome and the matched row's own ordinal -- the join this
    # unit computed -- but not the row's face/size/weight/flags, which are `fontlist.txt` bytes
    # this unit's `members` never names and so belong to the font-list unit instead.
    rows = parse_registry_rows(FONT_LIST_DATA)
    model = decode_font(_clean_closure(), font_list_rows=rows)
    assert model.font_list == {"resolved": True, "index": 0}


def test_without_font_list_present_the_join_is_a_warning_not_a_failing_unresolved_row():
    # The registry is another seam's data, not this `.fnt`'s own structure -- the glyph table
    # decodes completely without it -- so the non-canonical-storage rule
    # applies (specDeviation): `dependencies` states `resolved: false`, `anomalies[]` names it, and
    # the unit warns instead of entering `coverage.unresolved` and failing.
    closure = _closure(CLEAN_KEY, CLEAN_DATA, pages={0: _page(CLEAN_KEY, 0)}, font_list_present=False)
    model = decode_font(closure)
    assert model.font_list is None
    assert not model.unresolved
    font_list_row = next(row for row in model.dependencies if row["role"] == "font-list")
    assert font_list_row["resolved"] is False
    assert any(row["role"] == "font-list-missing" for row in model.anomalies)


# A registry with a comment, a blank line and a malformed line, exercising every non-`row` kind
# the lexer names (the two-row `FONT_LIST_TEXT` fixture above never touches any of them).
MESSY_FONT_LIST_TEXT = (
    '// a leading comment\r\n'
    '\r\n'
    '"Cleanface" 12 400 0\r\n'
    'not a registry row\r\n'
)
MESSY_FONT_LIST_DATA = MESSY_FONT_LIST_TEXT.encode("latin-1")


def test_a_blank_registry_line_is_claimed_omitted_proven_and_backed_by_a_coverage_row():
    model = decode_font_list(_font_list_closure(MESSY_FONT_LIST_DATA), font_keys={CLEAN_KEY})
    blank_offset = MESSY_FONT_LIST_TEXT.index("\r\n\r\n") + 2
    assert model.omitted_proven == [
        {"offset": blank_offset, "length": 2, "reason": "blank-line"}
    ]
    row = next(row for row in model.byte_ledger[0]["ranges"] if row["offset"] == blank_offset)
    assert row["state"] == "omitted-proven"


def test_a_comment_and_a_malformed_line_are_named_apart_from_the_blank_line():
    model = decode_font_list(_font_list_closure(MESSY_FONT_LIST_DATA), font_keys={CLEAN_KEY})
    assert [comment["text"] for comment in model.comments] == ["// a leading comment"]
    assert any(row["role"] == "row" and row["reason"] == "line-does-not-match-the-registry-row-grammar" for row in model.unresolved)
    assert [row["face"] for row in model.rows] == ["Cleanface"]


def test_a_malformed_lines_own_text_is_published_and_its_ledger_owner_names_the_unresolved_row():
    # The malformed line never lands in `rows[]` -- it composes no font key -- so its bytes must
    # not be a false `mapped-text` claim over a `rows[]` slot that never materializes: the raw
    # text is published on the `coverage.unresolved[]` record that actually carries it, and the
    # ledger owner names that record.
    model = decode_font_list(_font_list_closure(MESSY_FONT_LIST_DATA), font_keys={CLEAN_KEY})
    malformed = next(row for row in model.unresolved if row["role"] == "row")
    assert malformed["raw"] == "not a registry row"
    ledger_row = next(r for r in model.byte_ledger[0]["ranges"] if r["offset"] == malformed["offset"])
    assert ledger_row["state"] == "mapped-text"
    assert ledger_row["owner"] == "coverage.unresolved[0]"


# --- validator rejection of a tampered ledger -----------------------------------------------------


def test_the_validator_rejects_a_document_whose_ledger_was_tampered():
    rows = parse_registry_rows(FONT_LIST_DATA)
    model = decode_font(_clean_closure(), font_list_rows=rows)
    document, binary = exporter.build_document(model)
    root = document["extensions"][FONT_EXTENSION]
    root["coverage"]["byteLedger"][0]["ranges"][0]["length"] = 999
    with pytest.raises(validation.FontGlbValidationError):
        validation.validate_document(document, binary)


def test_the_validator_rejects_a_unit_with_a_nonempty_unresolved_list():
    # A missing referenced page or registry no longer populates `coverage.unresolved` (both are
    # now `anomalies[]` warnings; specDeviation), so this exercises the validator's own
    # completeness check directly rather than relying on a decode path that produces one.
    model = decode_font(_clean_closure())
    document, binary = exporter.build_document(model)
    root = document["extensions"][FONT_EXTENSION]
    root["coverage"]["unresolved"] = [{"role": "synthetic", "reason": "forced for this test"}]
    with pytest.raises(validation.FontGlbValidationError, match="incomplete"):
        validation.validate_document(document, binary)


def test_the_validator_rejects_a_font_list_field_that_contradicts_its_own_anomaly():
    rows = parse_registry_rows(FONT_LIST_DATA)
    model = decode_font(_clean_closure(), font_list_rows=rows)
    document, binary = exporter.build_document(model)
    root = document["extensions"][FONT_EXTENSION]
    root["fontList"] = {"resolved": False, "index": None}  # the fixture actually resolves
    with pytest.raises(validation.FontGlbValidationError, match="fontlist-row-missing"):
        validation.validate_document(document, binary, source_members=(_model_member(model),))


# --- source closures and enumeration --------------------------------------------------------------


def test_load_source_closure_scans_available_pages_independently_of_the_header():
    index, read_bytes = _index_and_reader(CLEAN_KEY, CLEAN_DATA, pages=(0, 3))
    closure = load_source_closure(index, CLEAN_KEY, read_bytes=read_bytes)
    assert set(closure.available_pages) == {0, 3}
    assert closure.font_list_present is True


def test_load_source_closure_reads_the_pages_own_vmt_basetexture_over_the_naive_stem_guess():
    index, read_bytes = _index_and_reader(CLEAN_KEY, CLEAN_DATA, pages=(0,))
    vmt_path = f"materials/fonts/{CLEAN_KEY}-page0.vmt"
    other_tth = "materials/fonts/other_stem-page0.tth"
    index[other_tth] = ("vpk", ("pack0.vpk", 0, 10))
    blobs = {
        font_path(CLEAN_KEY): CLEAN_DATA,
        "materials/fonts/fontlist.txt": FONT_LIST_DATA,
        vmt_path: b'UnlitGeneric\r\n{\r\n\t"$basetexture" "fonts/other_stem-page0"\r\n}\r\n',
    }

    def reader(idx, key):
        return blobs.get(key)

    closure = load_source_closure(index, CLEAN_KEY, read_bytes=reader)
    page = closure.available_pages[0]
    assert page.basetexture_declared is True
    assert page.resolved_texture_path == other_tth
    assert page.resolved_texture_present is True


def test_load_source_closure_raises_when_the_fnt_is_absent():
    index, read_bytes = _index_and_reader(CLEAN_KEY, CLEAN_DATA, pages=())
    index.pop(font_path(CLEAN_KEY))

    from elysium_pipeline.formats.font_glb.source import FontSourceError

    with pytest.raises(FontSourceError):
        load_source_closure(index, "missing_10_400_000", read_bytes=read_bytes)


def test_font_keys_enumerates_every_fnt_stem_sorted():
    index, _ = _index_and_reader(CLEAN_KEY, CLEAN_DATA, pages=(0,), font_list=False)
    index[font_path(QUIRKY_KEY)] = ("loose", "/x/other.fnt")
    assert font_keys(index) == sorted([CLEAN_KEY, QUIRKY_KEY])
    assert exporter.source_keys(index) == font_keys(index)


def test_source_keys_appends_the_font_list_sentinel_when_the_registry_is_present():
    # `fonts-glb` publishes every font unit and the list unit in one pass (the join is checked
    # in both directions); the plural runner only ever calls
    # `source_keys`/`export`, so the sentinel and the dispatch below are what makes that true.
    index, _ = _index_and_reader(CLEAN_KEY, CLEAN_DATA, pages=(0,), font_list=True)
    index[font_path(QUIRKY_KEY)] = ("loose", "/x/other.fnt")
    assert exporter.source_keys(index) == sorted([CLEAN_KEY, QUIRKY_KEY]) + [FONT_LIST_KEY]


def test_the_fonts_glb_corpus_keys_export_every_font_and_the_font_list_unit(tmp_path: Path):
    index, read_bytes = _index_and_reader(CLEAN_KEY, CLEAN_DATA, pages=(0,), font_list=True)
    destinations = [
        exporter.export(index, key, tmp_path, read_bytes=read_bytes)
        for key in exporter.source_keys(index)
    ]
    assert {destination.name for destination in destinations} == {f"{CLEAN_KEY}.glb", "fontlist.glb"}
    for destination in destinations:
        summary = validation.validate(destination)
        assert summary["byteCoveragePercent"] == 100.0


# --- the full file round trip through validation.validate() --------------------------------------


def test_a_clean_font_and_the_font_list_round_trip_through_validate(tmp_path: Path):
    index, read_bytes = _index_and_reader(CLEAN_KEY, CLEAN_DATA, pages=(0,), font_list=True)
    destination = exporter.export(index, CLEAN_KEY, tmp_path, read_bytes=read_bytes)
    summary = validation.validate(destination)
    assert summary["asset"] == f"vtmb:font:{CLEAN_KEY}"
    assert summary["byteCoveragePercent"] == 100.0
    assert summary["accountedBytes"] == summary["sourceBytes"]

    list_destination = exporter.export_font_list(index, tmp_path, read_bytes=read_bytes)
    list_summary = validation.validate(list_destination)
    assert list_summary["asset"] == "vtmb:font-list:fontlist"
    assert list_summary["byteCoveragePercent"] == 100.0


def test_export_tolerates_the_root_prefix_and_extension_on_its_argument(tmp_path: Path):
    index, read_bytes = _index_and_reader(CLEAN_KEY, CLEAN_DATA, pages=(0,), font_list=True)
    destination = exporter.export(
        index, "materials/fonts/" + CLEAN_KEY + ".fnt", tmp_path, read_bytes=read_bytes
    )
    assert destination.name == f"{CLEAN_KEY}.glb"


def test_a_font_with_a_missing_page_and_no_registry_exports_with_warnings_not_a_failure(tmp_path: Path):
    # Both a glyph-referenced page the install never shipped and an absent `fontlist.txt` are
    # non-canonical-storage cases (specDeviation): the unit publishes what the install holds,
    # complete and 100%-covered, and surfaces both gaps as warnings rather than failing export.
    index, read_bytes = _index_and_reader(CLEAN_KEY, CLEAN_DATA, pages=(), font_list=False)
    destination = exporter.export(index, CLEAN_KEY, tmp_path, read_bytes=read_bytes)
    summary = validation.validate(destination)
    assert summary["byteCoveragePercent"] == 100.0
    warnings = validation.warnings_for(summary)
    assert any("page-missing" in warning for warning in warnings)
    assert any("fontlist.txt" in warning for warning in warnings)


def test_export_dispatches_the_font_list_sentinel_to_the_registry_unit(tmp_path: Path):
    index, read_bytes = _index_and_reader(CLEAN_KEY, CLEAN_DATA, pages=(0,), font_list=True)
    destination = exporter.export(index, FONT_LIST_KEY, tmp_path, read_bytes=read_bytes)
    assert destination.name == "fontlist.glb"
    summary = validation.validate(destination)
    assert summary["asset"] == "vtmb:font-list:fontlist"


def test_export_dispatches_the_registrys_own_path_to_the_registry_unit_too(tmp_path: Path):
    index, read_bytes = _index_and_reader(CLEAN_KEY, CLEAN_DATA, pages=(0,), font_list=True)
    destination = exporter.export(
        index, "materials/fonts/fontlist.txt", tmp_path, read_bytes=read_bytes
    )
    assert destination.name == "fontlist.glb"


def test_the_validator_reattaches_a_redirected_texture_row_by_pairing_order_not_by_its_own_path():
    # A page's material row's own `sourcePath` is always this font's own `<stem>-page<n>.vmt`;
    # its texture row may be redirected to a completely different stem/page number by the `.vmt`'s
    # own `$basetexture`. The rebuilt closure must still key that page by the material row's own
    # page number and attach the (differently-numbered-looking) texture row by pairing order.
    page = _page(
        CLEAN_KEY, 0, tth=False, vmt=True, declared_texture="materials/fonts/other_stem-page9.tth"
    )
    closure = _closure(CLEAN_KEY, CLEAN_DATA, pages={0: page}, font_list_present=True)
    rows = parse_registry_rows(FONT_LIST_DATA)
    model = decode_font(closure, font_list_rows=rows)
    document, binary = exporter.build_document(model)
    root = document["extensions"][FONT_EXTENSION]
    texture_row = next(row for row in root["dependencies"] if row["role"] == "texture")
    assert texture_row["sourcePath"] == "materials/fonts/other_stem-page9.tth"
    material_row = next(row for row in root["dependencies"] if row["role"] == "material")
    assert material_row["sourcePath"] == f"materials/fonts/{CLEAN_KEY}-page0.vmt"
    validation.validate_document(document, binary, source_members=(_model_member(model),))
