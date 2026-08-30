"""Complete decode of one `.fnt` glyph table and of the `fontlist.txt` registry.

Layout constants are the ones `formats/fnt.py` already reverse-engineered: a 36-byte `u32[9]`
header, a 256-byte char-to-glyph map at 36, and `glyphCount` 44-byte glyph entries at the header's
own `glyphTableOffset` (292 on every shipped member). This module additionally answers what every
byte of that layout is for, which `fnt.py`'s narrower "read enough to render" decode does not need.
"""

from __future__ import annotations

import re
import struct
from typing import Any

from elysium_pipeline.formats.font_glb import lexer
from elysium_pipeline.formats.font_glb.model import (
    FONT_LIST_PATH,
    FontListModel,
    FontModel,
    asset_id,
    font_list_asset_id,
    font_path,
    page_material_path,
    page_texture_path,
    parse_name,
    registry_key,
)
from elysium_pipeline.formats.material_glb.model import asset_id as material_asset_id
from elysium_pipeline.formats.texture_glb.model import asset_id as texture_asset_id
from elysium_pipeline.formats.unit_contract import ByteLedger, ByteLedgerError, dependency


class FontDecodeError(RuntimeError):
    """The selected `.fnt` bytes cannot be decoded against the reverse-engineered layout."""


HEADER_WORD_COUNT = 9
HEADER_SIZE = HEADER_WORD_COUNT * 4
CHARMAP_OFFSET = 36
CHARMAP_LENGTH = 256
GLYPH_ENTRY_SIZE = 44

#: Which header word names which field; the rest are the "other six" the seam note describes.
NAMED_HEADER_WORDS = {0: "pages", 4: "lineHeight", 8: "glyphTableOffset"}
#: Word 1 always equals `glyphTableOffset + glyphCount * 44` (the trailer's start offset) across
#: every shipped member, so it is `derived` rather than `typedUnidentified` when the file agrees.
DERIVED_HEADER_WORD = 1

#: Bytes of one 44-byte glyph entry this decode assigns no field to. The seam note reads
#: "1-17, 19, 21-23, 25", but byte 21 is the high byte of the s16 `height` field at offset 20
#: (`-struct.unpack_from("<h", entry, 20)`); the true unread pair is 22-23. See `specDeviations`.
GLYPH_UNREAD_SPANS = ((1, 17), (19, 1), (22, 2), (25, 1))

_UV_EPSILON = 1e-4

#: At least 4 printable-ASCII bytes ending a trailer non-zero run; the NUL that terminates such a
#: string is the first byte of the reserved-zero run the loop below claims right after, since zero
#: bytes are what split runs in the first place. Real members carry a page name here (e.g.
#: `Marlett_08_000_008-page0\0`), but also carry names naming a page of a *different* font
#: entirely (observed: `times_new_roman_86_900_000`'s trailer ends with
#: `Vamp_MainFont_111_1000_000-page8\0`, a font this file never references) -- this is decoded as
#: a plain string with no claim about which font, if any, it belongs to.
TRAILER_STRING_RE = re.compile(rb"[\x20-\x7e]{4,}\Z")


def _header_words(data: bytes) -> tuple[int, ...]:
    return struct.unpack_from(f"<{HEADER_WORD_COUNT}I", data, 0)


def _decode_header(
    data: bytes, ledger: ByteLedger, glyph_table_end: int
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    words = _header_words(data)
    entries: list[dict[str, Any]] = []
    typed_unidentified: list[dict[str, Any]] = []
    named: dict[str, int] = {}
    for index, value in enumerate(words):
        offset = index * 4
        if index in NAMED_HEADER_WORDS:
            role = NAMED_HEADER_WORDS[index]
            named[role] = value
            ledger.claim(offset, 4, "mapped", f"header.words[{index}]")
        elif index == DERIVED_HEADER_WORD and value == glyph_table_end:
            role = "derived"
            ledger.claim(offset, 4, "mapped", f"header.words[{index}]")
        elif value == 0:
            role = "reserved-zero"
            ledger.claim(offset, 4, "reserved-zero", f"header.words[{index}]")
        else:
            role = "typedUnidentified"
            typed_unidentified.append({"field": f"header.words[{index}]", "offset": offset, "value": value})
            ledger.claim(offset, 4, "mapped", f"header.words[{index}]")
        entries.append({"index": index, "offset": offset, "value": value, "role": role})
    header = {
        "words": entries,
        "pages": named["pages"],
        "lineHeight": named["lineHeight"],
        "glyphTableOffset": named["glyphTableOffset"],
    }
    return header, typed_unidentified


def _decode_glyph(
    data: bytes, glyph_off: int, index: int, pages_declared: int, ledger: ByteLedger
) -> tuple[dict[str, Any], list[dict[str, Any]], list[dict[str, Any]]]:
    start = glyph_off + index * GLYPH_ENTRY_SIZE
    entry = data[start:start + GLYPH_ENTRY_SIZE]
    advance = entry[0]
    left_bearing = struct.unpack_from("<b", entry, 18)[0]
    height = -struct.unpack_from("<h", entry, 20)[0]
    page = entry[24]
    width = struct.unpack_from("<h", entry, 26)[0]
    u0, v0, u1, v1 = struct.unpack_from("<4f", entry, 28)

    ledger.claim(start + 0, 1, "mapped", f"glyphs[{index}].advance")
    ledger.claim(start + 18, 1, "mapped", f"glyphs[{index}].leftBearing")
    ledger.claim(start + 20, 2, "mapped", f"glyphs[{index}].height")
    ledger.claim(start + 24, 1, "mapped", f"glyphs[{index}].page")
    ledger.claim(start + 26, 2, "mapped", f"glyphs[{index}].width")
    ledger.claim(start + 28, 16, "mapped", f"glyphs[{index}].uv")

    typed_unidentified: list[dict[str, Any]] = []
    unread: list[dict[str, Any]] = []
    for span_index, (span_start, span_length) in enumerate(GLYPH_UNREAD_SPANS):
        absolute = start + span_start
        chunk = entry[span_start:span_start + span_length]
        owner = f"glyphs[{index}].unread[{span_index}]"
        if any(chunk):
            ledger.claim(absolute, span_length, "mapped", owner)
            record = {"offset": absolute, "length": span_length, "hex": chunk.hex()}
            unread.append(record)
            typed_unidentified.append({"field": owner, **record})
        else:
            ledger.claim(absolute, span_length, "reserved-zero", owner)

    anomalies: list[dict[str, Any]] = []
    if page >= pages_declared:
        anomalies.append({"role": "glyph-outside-page", "index": index, "page": page})
    if width < 0:
        anomalies.append({"role": "negative-width", "index": index, "width": width})
    coords = (u0, v0, u1, v1)
    if any(c < -_UV_EPSILON or c > 1 + _UV_EPSILON for c in coords) or u1 < u0 or v1 < v0:
        anomalies.append({"role": "uv-out-of-range", "index": index, "uv": list(coords)})

    record = {
        "index": index,
        "advance": advance,
        "leftBearing": left_bearing,
        "height": height,
        "page": page,
        "width": width,
        "uv": {"u0": u0, "v0": v0, "u1": u1, "v1": v1},
        "sourceOffset": start,
        "unread": unread,
    }
    return record, typed_unidentified, anomalies


def _claim_trailer_hex(
    ledger: ByteLedger,
    typed_unidentified: list[dict[str, Any]],
    unidentified: list[dict[str, Any]],
    absolute: int,
    chunk: bytes,
    run_index: int,
) -> None:
    if not chunk:
        return
    ledger.claim(absolute, len(chunk), "mapped", f"trailer.unidentified[{run_index}]")
    record = {"offset": absolute, "length": len(chunk), "hex": chunk.hex()}
    unidentified.append(record)
    typed_unidentified.append({"field": f"trailer.unidentified[{run_index}]", **record})


def _decode_trailer(
    data: bytes, table_end: int, ledger: ByteLedger
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    """Everything after the glyph table, to end of file.

    The seam note describes a fixed 96-byte trailer plus an `excess` remainder; real members do
    not carry a fixed-length trailer (see `specDeviations`), so this claims the whole remainder as
    one variable-length region, run-length split into zero and non-zero spans. A non-zero span
    that contains a null-terminated printable-ASCII run (`TRAILER_STRING_RE`) decodes that run as
    a named string; everything else stays opaque hex, both still `typedUnidentified` because this
    decode does not know -- and cannot always trust -- whose data it is (see `specDeviations`).
    """

    trailer_bytes = data[table_end:]
    typed_unidentified: list[dict[str, Any]] = []
    unidentified: list[dict[str, Any]] = []
    names: list[dict[str, Any]] = []
    position, total, run_index = 0, len(trailer_bytes), 0
    while position < total:
        start = position
        zero = trailer_bytes[position] == 0
        while position < total and (trailer_bytes[position] == 0) == zero:
            position += 1
        length = position - start
        absolute = table_end + start
        if zero:
            # The trailer is not a declared structure (see the module docstring): a zero run here
            # is unreferenced storage the source happens to hold zero, not a declared field the
            # contract's `reserved-zero` grade is for -- that grade is reserved for the header
            # words and glyph-entry unread spans, which sit inside declared records.
            ledger.claim(absolute, length, "padding-zero", f"trailer.padding[{run_index}]")
        else:
            chunk = trailer_bytes[start:position]
            # Only a run with a following zero byte (`position < total`) is genuinely
            # null-terminated; a printable tail running to EOF has no terminator to speak of.
            match = TRAILER_STRING_RE.search(chunk) if position < total else None
            if match is None:
                _claim_trailer_hex(ledger, typed_unidentified, unidentified, absolute, chunk, run_index)
            else:
                str_start = match.start()
                _claim_trailer_hex(
                    ledger, typed_unidentified, unidentified, absolute, chunk[:str_start], run_index
                )
                name_bytes = chunk[str_start:]
                name_offset = absolute + str_start
                owner = f"trailer.names[{len(names)}]"
                ledger.claim(name_offset, len(name_bytes), "mapped-string", owner)
                text = name_bytes.decode("latin-1")
                name_record = {"offset": name_offset, "length": len(name_bytes), "text": text}
                names.append(name_record)
                typed_unidentified.append(
                    {"field": owner, "offset": name_offset, "length": len(name_bytes), "hex": name_bytes.hex()}
                )
        run_index += 1
    return {"byteLength": total, "unidentified": unidentified, "names": names}, typed_unidentified


def _claim_pretable_gap(data: bytes, glyph_off: int, ledger: ByteLedger) -> list[dict[str, Any]]:
    """The bytes, if any, between the fixed char map (ending at 292) and a glyph table the header
    points further out.

    Every shipped member has `glyphTableOffset == 292` (the seam note), so this region is normally
    empty; the guard in `decode_font` only rejects `glyph_off` short of the char map, so a member
    whose header names a later offset is admitted, and this claims the gap so the ledger stays
    gapless instead of aborting with a raw `ByteLedgerError` (see `specDeviations`).
    """

    start = CHARMAP_OFFSET + CHARMAP_LENGTH
    length = glyph_off - start
    if length <= 0:
        return []
    chunk = data[start:glyph_off]
    if any(chunk):
        ledger.claim(start, length, "omitted-proven", "pad")
        return [
            {
                "role": "pad-before-glyph-table",
                "offset": start,
                "length": length,
                "hex": chunk.hex(),
            }
        ]
    ledger.claim(start, length, "padding-zero", "pad")
    return []


def decode_font(closure, *, font_list_rows: list[dict[str, Any]] | None = None) -> FontModel:
    """The complete unit for one font source closure.

    `font_list_rows` is the parsed `fontlist.txt` registry (`parse_registry_rows`), or `None` when
    the caller never loaded it (the join then publishes `unresolved` rather than asserting a
    target this decode never looked for).
    """

    member = closure.fnt
    data, path = member.data, member.path
    if len(data) < HEADER_SIZE + CHARMAP_LENGTH:
        raise FontDecodeError(f"{path}: file is shorter than the fixed header and char map")
    words = _header_words(data)
    glyph_off = words[8]
    charmap = list(data[CHARMAP_OFFSET:CHARMAP_OFFSET + CHARMAP_LENGTH])
    if len(charmap) != CHARMAP_LENGTH:
        raise FontDecodeError(f"{path}: char map is truncated")
    count = max(charmap) + 1
    table_end = glyph_off + count * GLYPH_ENTRY_SIZE
    if glyph_off < HEADER_SIZE + CHARMAP_LENGTH or table_end > len(data):
        raise FontDecodeError(
            f"{path}: glyph table {glyph_off}+{count}*44 does not fit {len(data)} bytes"
        )

    ledger = ByteLedger(path, data)
    typed_unidentified: list[dict[str, Any]] = []
    anomalies: list[dict[str, Any]] = []
    omissions: list[dict[str, Any]] = []

    try:
        header, header_typed = _decode_header(data, ledger, table_end)
        typed_unidentified.extend(header_typed)
        ledger.claim(CHARMAP_OFFSET, CHARMAP_LENGTH, "mapped", "charMap")
        omissions.extend(_claim_pretable_gap(data, glyph_off, ledger))

        pages_declared = header["pages"]
        glyphs: list[dict[str, Any]] = []
        used_pages: set[int] = set()
        for index in range(count):
            record, record_typed, record_anomalies = _decode_glyph(
                data, glyph_off, index, pages_declared, ledger
            )
            glyphs.append(record)
            typed_unidentified.extend(record_typed)
            anomalies.extend(record_anomalies)
            used_pages.add(record["page"])

        # The seam note's `anomalies[] charmap-index-out-of-range` (a char-map entry >= the glyph
        # count) is unreachable: `count` is definitionally `max(charmap) + 1` (the format carries no
        # separate glyph-count field; see `formats/fnt.py`), so no entry can ever be >= `count`. Not
        # implemented; see `specDeviations`.

        # The seam note's `omissions[] unreferenced-glyphs` describes a table *longer* than a
        # separately-stored glyph count, with the excess "still decoded into glyphs[]". That scenario
        # is equally unreachable under the same `count` derivation, so this instead names the interior
        # indices below `count` no code point reaches -- the achievable reading; see `specDeviations`.
        unreferenced = sorted(set(range(count)) - set(charmap))
        if unreferenced:
            omissions.append(
                {"role": "unreferenced-glyphs", "count": len(unreferenced), "indices": unreferenced}
            )

        trailer, trailer_typed = _decode_trailer(data, table_end, ledger)
        typed_unidentified.extend(trailer_typed)

        byte_ledger_row = ledger.finish()
    except ByteLedgerError as error:
        raise FontDecodeError(f"{path}: {error}") from error

    dependencies: list[dict[str, Any]] = []
    unresolved: list[dict[str, Any]] = []
    pages_out: list[dict[str, Any]] = []
    all_page_indices = sorted(set(range(pages_declared)) | used_pages | set(closure.available_pages))
    for page_index in all_page_indices:
        available = closure.available_pages.get(page_index)
        naive_tth_path = page_texture_path(closure.key, page_index)
        vmt_path = page_material_path(closure.key, page_index)
        material_present = bool(available and available.vmt_present)
        # The page's own `.vmt` frequently names a different real texture than the naive
        # `<stem>-page<n>` guess (`$basetexture`); that declared target is authoritative when the
        # `.vmt` states one, so a page is only `unresolved` when *that* target is absent.
        if available is not None:
            texture_path = available.resolved_texture_path
            texture_present = available.resolved_texture_present
        else:
            texture_path = naive_tth_path
            texture_present = False
        texture_id = texture_asset_id(texture_path)
        material_id = material_asset_id(vmt_path)
        dependencies.append(dependency("texture", texture_id, texture_path, texture_present))
        dependencies.append(dependency("material", material_id, vmt_path, material_present))
        if available is not None and available.vmt_present and not available.basetexture_declared:
            anomalies.append(
                {"role": "page-vmt-missing-basetexture", "page": page_index, "path": available.vmt_path}
            )
        if page_index in used_pages and not texture_present:
            # `seam_map_font.md` reads this as the font's own structure and grades it
            # `coverage.unresolved`; the real install ships members that reference a page it never
            # shipped (neither the `.tth` nor its `.vmt`), which is exactly
            # `seam_map_unit_contract.md`'s non-canonical-storage case instead -- a reference to
            # another seam's data (the page's texture and material units) that merely fails to
            # resolve, so the unit publishes what the install holds and warns. See `specDeviations`.
            using_glyphs = sorted(g["index"] for g in glyphs if g["page"] == page_index)
            anomalies.append({
                "role": "page-missing",
                "page": page_index,
                "path": texture_path,
                "glyphs": using_glyphs,
            })
        if available is not None and page_index not in used_pages:
            omissions.append(
                {"role": "unreferenced-page", "page": page_index, "path": naive_tth_path}
            )
        pages_out.append({
            "index": page_index,
            "texture": texture_id,
            "material": material_id,
            "resolved": texture_present and material_present,
            "coverageChannel": "alpha",
        })

    # `fontList` publishes the join outcome and the matched row's own ordinal -- the join this
    # unit itself computed, so a consumer can check it from the font side -- but not the row's
    # other fields (face, size, weight, flags, offset): those are `fontlist.txt` bytes, and
    # `members` here names only this `.fnt` member, so nothing in this unit's own byte ledger
    # backs them. A font's own name is already published as `name` (parsed from the filename, a
    # different source than the registry); the font-list unit is the one that carries the row's
    # content.
    font_list_entry: dict[str, Any] | None = None
    if closure.font_list_present:
        match = None
        if font_list_rows is not None:
            match = next((row for row in font_list_rows if row["key"] == closure.key), None)
        font_list_entry = {
            "resolved": match is not None,
            "index": match["index"] if match is not None else None,
        }
        if match is None:
            anomalies.append(
                {"role": "fontlist-row-missing", "font": closure.key, "path": closure.fnt.path}
            )
        dependencies.append(dependency("font-list", font_list_asset_id(), FONT_LIST_PATH, True))
    else:
        dependencies.append(dependency("font-list", font_list_asset_id(), FONT_LIST_PATH, False))
        # The registry is another seam's data, not this `.fnt`'s own structure -- the glyph table
        # decodes completely without it -- so its absence is graded like the page case above:
        # `anomalies[]` and a warning, not `coverage.unresolved`. See `specDeviations`.
        anomalies.append(
            {"role": "font-list-missing", "reason": "the install carries no fontlist.txt registry"}
        )

    name = parse_name(closure.key)
    if name is None:
        anomalies.append({"role": "name-pattern-mismatch", "key": closure.key})

    return FontModel(
        key=closure.key,
        asset_id=closure.asset_id,
        members=(member,),
        header=header,
        char_map=charmap,
        glyphs=glyphs,
        trailer=trailer,
        pages=pages_out,
        font_list=font_list_entry,
        dependencies=dependencies,
        anomalies=anomalies,
        omissions=omissions,
        unresolved=unresolved,
        unsupported=[],
        typed_unidentified=typed_unidentified,
        byte_ledger=[byte_ledger_row],
        name=name,
    )


def parse_registry_rows(data: bytes) -> list[dict[str, Any]]:
    """Every well-formed `"Face" size weight flags` row, in source order."""

    rows: list[dict[str, Any]] = []
    ordinal = 0
    for line in lexer.tokenize(lexer.decode_text(data)):
        if line.kind != "row":
            continue
        key = registry_key(line.face, line.size, line.weight, line.flags)
        rows.append({
            "index": ordinal,
            "offset": line.offset,
            "length": line.length,
            "face": line.face,
            "size": line.size,
            "weight": line.weight,
            "flags": line.flags,
            "key": key,
            "asset": asset_id(key),
        })
        ordinal += 1
    return rows


def decode_font_list(closure, *, font_keys: set[str] | frozenset[str] | None = None) -> FontListModel:
    """The complete font-list unit.

    `font_keys` is every `.fnt` stem the install resolves (`source.font_keys`); without it a row's
    join publishes `resolved: true` unconditionally rather than asserting an install this decode
    never looked at.
    """

    member = closure.source
    data = member.data
    text = lexer.decode_text(data)
    lines = lexer.tokenize(text)
    ledger = ByteLedger(member.path, data)

    rows: list[dict[str, Any]] = []
    comments: list[dict[str, Any]] = []
    anomalies: list[dict[str, Any]] = []
    unresolved: list[dict[str, Any]] = []
    dependencies: list[dict[str, Any]] = []
    omitted_proven: list[dict[str, Any]] = []
    seen_assets: set[str] = set()
    ordinal = 0

    for line in lines:
        if line.kind == "blank":
            ledger.claim(line.offset, line.length, "omitted-proven", f"whitespace[{line.index}]")
            omitted_proven.append(
                {"offset": line.offset, "length": line.length, "reason": "blank-line"}
            )
            continue
        if line.kind == "comment":
            ledger.claim(line.offset, line.length, "mapped", f"comments[{len(comments)}]")
            comments.append({"offset": line.offset, "text": line.text})
            continue
        if line.kind == "malformed":
            # A malformed line is never added to `rows[]` -- it composes no font key -- so the
            # ledger owner names the `coverage.unresolved[]` record that actually carries these
            # bytes, not a `rows[]` slot that never materializes. The line's own text is
            # published on that record (`raw`) so the `mapped-text` claim is true: the text is
            # decoded into a structured record, just not the row table.
            owner = f"coverage.unresolved[{len(unresolved)}]"
            ledger.claim(line.offset, line.length, "mapped-text", owner)
            unresolved.append({
                "role": "row",
                "index": ordinal,
                "offset": line.offset,
                "raw": line.text,
                "reason": "line-does-not-match-the-registry-row-grammar",
            })
            ordinal += 1
            continue

        key = registry_key(line.face, line.size, line.weight, line.flags)
        font_asset = asset_id(key)
        resolved = font_keys is None or key in font_keys
        ledger.claim(line.offset, line.length, "mapped-text", f"rows[{ordinal}]")
        rows.append({
            "index": ordinal,
            "face": line.face,
            "size": line.size,
            "weight": line.weight,
            "flags": line.flags,
            "offset": line.offset,
            "asset": font_asset,
            "resolved": resolved,
        })
        if not resolved:
            anomalies.append({
                "role": "fontlist-font-missing",
                "index": ordinal,
                "face": line.face,
                "path": font_path(key),
            })
        if font_asset not in seen_assets:
            seen_assets.add(font_asset)
            dependencies.append(dependency("font", font_asset, font_path(key), resolved))
        ordinal += 1

    byte_ledger_row = ledger.finish()
    return FontListModel(
        members=(member,),
        rows=rows,
        comments=comments,
        dependencies=dependencies,
        anomalies=anomalies,
        omissions=[],
        unresolved=unresolved,
        unsupported=[],
        byte_ledger=[byte_ledger_row],
        omitted_proven=omitted_proven,
    )
