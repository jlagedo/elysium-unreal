"""`hl2.tmp`: the `+header`/`-header` block stream the save system writes.

The save-fragment grammar names only what it reaches: the block
framing, the map/landmark strings that precede a zlib body, and the zlib span itself -- decoded
with `formats/sav.py` where it reaches. Everything else in a block is content this decode cannot
name from `docs/vtmb/savegame_format.md` alone, so it is carried as a `typedUnidentified` range
keyed by its own digest rather than guessed at.
"""

from __future__ import annotations

import hashlib
import re
import struct
import zlib
from typing import Any

from elysium_pipeline.formats import sav
from elysium_pipeline.formats.unit_contract.ledger import ByteLedger

BLOCK_OPEN = b"+header\x00"
BLOCK_CLOSE = b"-header\x00"
#: The 4-byte little-endian sentinel `0xABCDDCBA` that separates one block from the next.
BLOCK_GUARD = b"\xba\xdc\xcd\xab"
#: `"+header\x00"` (8) + one flag byte + a 4-byte float + `"-header\x00"` (8).
HEADER_LENGTH = 8 + 1 + 4 + 8

_ZLIB_SIGNATURES = (0x01, 0x5E, 0x9C, 0xDA)
_STRING_RUN = re.compile(rb"[\x20-\x7e]{3,}\x00")
_MAP_STRING = re.compile(r"^maps/.+\.bsp$", re.IGNORECASE)


class SaveFragmentDecodeError(RuntimeError):
    """`hl2.tmp` does not open with the block framing this decode recognizes."""


def _find_zlib_span(body: bytes) -> tuple[int, int, bytes] | None:
    """The first position in `body` a real zlib stream decompresses from, or `None`.

    Returns `(offset, consumed_length, inflated_bytes)`; `consumed_length` is the compressed
    span's own length, which is what the byte ledger is claimed against.
    """

    for index in range(len(body) - 1):
        if body[index] != 0x78 or body[index + 1] not in _ZLIB_SIGNATURES:
            continue
        try:
            decompressor = zlib.decompressobj()
            inflated = decompressor.decompress(body[index:])
        except zlib.error:
            continue
        if not inflated:
            continue
        consumed = len(body) - index - len(decompressor.unused_data)
        return index, consumed, inflated
    return None


def _walk_fields(inflated: bytes) -> dict[str, Any]:
    """Best-effort `formats/sav.py` field-stream walk: how far the primitive reaches.

    `hl2.tmp` carries no symbol table of its own, so every field decodes with an empty token
    list -- `read_field` then names it `""` -- and this only proves the byte *shape*, not the
    field's meaning, which stays `typedUnidentified`.
    """

    fields = sav.read_fields(inflated, 0, len(inflated), tokens=())
    consumed = sum(4 + field.size for field in fields)
    return {"fieldsWalked": len(fields), "consumedBytes": consumed, "complete": consumed == len(inflated)}


def _digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def decode_save_fragment(member_path: str, data: bytes) -> dict[str, Any]:
    if BLOCK_OPEN not in data:
        raise SaveFragmentDecodeError(f"{member_path}: no {BLOCK_OPEN!r} block marker found")

    starts: list[int] = []
    cursor = 0
    while True:
        found = data.find(BLOCK_OPEN, cursor)
        if found < 0:
            break
        starts.append(found)
        cursor = found + len(BLOCK_OPEN)

    ledger = ByteLedger(member_path, data)
    blocks: list[dict[str, Any]] = []
    dependencies: dict[str, dict[str, Any]] = {}
    typed_unidentified: list[dict[str, Any]] = []
    omissions: list[dict[str, Any]] = []

    def claim_unidentified(offset: int, length: int, reason: str, owner: str) -> None:
        if length <= 0:
            return
        chunk = data[offset:offset + length]
        if not any(chunk):
            ledger.claim(offset, length, "padding-zero", owner)
            return
        ledger.claim(offset, length, "mapped", owner)
        # `hex` carries the value itself, the way `expression_table_glb`/`font_glb` do for their
        # own opaque spans, so the `mapped` claim is honest: the bytes survive the export even
        # though this decode cannot name their meaning.
        typed_unidentified.append(
            {"offset": offset, "length": length, "sha256": _digest(chunk), "hex": chunk.hex(), "reason": reason}
        )

    def claim_trailing_fill(offset: int, length: int, owner: str) -> None:
        """The allocation past the last block's own decoded content: `padding-zero` when zero,
        `omitted-proven` with an `omissions[] trailing-fill` row otherwise."""

        if length <= 0:
            return
        chunk = data[offset:offset + length]
        if not any(chunk):
            ledger.claim(offset, length, "padding-zero", owner)
            return
        ledger.claim(offset, length, "omitted-proven", owner)
        omissions.append(
            {"role": "trailing-fill", "offset": offset, "length": length, "sha256": _digest(chunk)}
        )

    # Bytes before the first block marker are not this decode's to name either; a shipped
    # `hl2.tmp` opens at offset 0 so this is latent today, but a partly recoverable unit publishes
    # what the install holds rather than raising over an unclaimed preamble.
    claim_unidentified(0, starts[0], "preamble-before-first-block", "saveFragment.preamble")

    for index, start in enumerate(starts):
        is_last_block = index == len(starts) - 1
        block_end = starts[index + 1] if index + 1 < len(starts) else len(data)
        if data[start:start + len(BLOCK_OPEN)] != BLOCK_OPEN:
            raise SaveFragmentDecodeError(f"{member_path}: block {index} lost its own marker")
        close_at = start + HEADER_LENGTH - len(BLOCK_CLOSE)
        if data[close_at:close_at + len(BLOCK_CLOSE)] != BLOCK_CLOSE:
            raise SaveFragmentDecodeError(f"{member_path}: block {index} has no {BLOCK_CLOSE!r}")
        header_end = start + HEADER_LENGTH
        flag = data[start + 8]
        (progress,) = struct.unpack_from("<f", data, start + 9)

        guard_present = data[block_end - 4:block_end] == BLOCK_GUARD and block_end - 4 >= header_end
        body_end = block_end - 4 if guard_present else block_end
        # A guard-terminated block closed properly, whatever its index; "the trailing allocation
        # past the last block" only exists once the
        # block stream itself has run out without one.
        is_last_block = is_last_block and not guard_present

        ledger.claim(start, 8, "mapped", f"saveFragment.blocks[{index}].header.open-marker")
        # `flag`/`progress` are decoded and published on `blocks[i].header` below: a value this
        # decode names is `mapped`, not `typedUnidentified` (which is for a value whose meaning
        # is unknown).
        ledger.claim(start + 8, 5, "mapped", f"saveFragment.blocks[{index}].header.value")
        ledger.claim(close_at, 8, "mapped", f"saveFragment.blocks[{index}].header.close-marker")

        body = data[header_end:body_end]
        zlib_span = _find_zlib_span(body)
        strings: list[dict[str, Any]] = []
        zlib_record: dict[str, Any] | None = None

        if zlib_span is not None:
            zoff, zlen, inflated = zlib_span
            preamble = body[:zoff]
            covered: list[tuple[int, int]] = []
            for match in _STRING_RUN.finditer(preamble):
                text = match.group().rstrip(b"\x00").decode("latin-1")
                abs_offset = header_end + match.start()
                length = match.end() - match.start()
                ledger.claim(
                    abs_offset, length, "mapped-string",
                    f"saveFragment.blocks[{index}].strings[{len(strings)}]",
                )
                strings.append({"offset": abs_offset, "length": length, "text": text})
                covered.append((match.start(), match.end()))
                if _MAP_STRING.match(text):
                    dependencies.setdefault(text.lower(), {"text": text, "offset": abs_offset})

            gap_cursor = 0
            for gap_start, gap_end in covered:
                claim_unidentified(
                    header_end + gap_cursor, gap_start - gap_cursor, "block-preamble-content",
                    f"saveFragment.blocks[{index}].preamble[{gap_cursor}]",
                )
                gap_cursor = gap_end
            claim_unidentified(
                header_end + gap_cursor, len(preamble) - gap_cursor, "block-preamble-content",
                f"saveFragment.blocks[{index}].preamble[{gap_cursor}]",
            )

            zlib_abs = header_end + zoff
            walk = _walk_fields(inflated)
            if walk["complete"]:
                # "derived" only where the walk
                # both inflates and walks the stream -- the inflated bytes are then represented by
                # the field count/consumed-length pair below, not merely claimed as present.
                ledger.claim(zlib_abs, zlen, "derived", f"saveFragment.blocks[{index}].zlib")
            else:
                claim_unidentified(
                    zlib_abs, zlen, "block-zlib-span-incomplete-walk",
                    f"saveFragment.blocks[{index}].zlib",
                )
            zlib_record = {
                "offset": zlib_abs,
                "length": zlen,
                "inflatedLength": len(inflated),
                "fieldsWalked": walk["fieldsWalked"],
                "consumedBytes": walk["consumedBytes"],
                "complete": walk["complete"],
            }
            remainder_offset = zlib_abs + zlen
            remainder_owner = f"saveFragment.blocks[{index}].remainder"
            if is_last_block:
                claim_trailing_fill(remainder_offset, body_end - remainder_offset, remainder_owner)
            else:
                claim_unidentified(
                    remainder_offset, body_end - remainder_offset, "block-body-remainder", remainder_owner
                )
        else:
            body_owner = f"saveFragment.blocks[{index}].body"
            if is_last_block:
                claim_trailing_fill(header_end, body_end - header_end, body_owner)
            else:
                claim_unidentified(header_end, body_end - header_end, "block-body-content", body_owner)

        if guard_present:
            # The decoder proved these 4 bytes equal `BLOCK_GUARD`; a value this decode has
            # already verified is `mapped`, not `typedUnidentified`.
            ledger.claim(body_end, 4, "mapped", f"saveFragment.blocks[{index}].guard")

        blocks.append(
            {
                "index": index,
                "offset": start,
                "length": block_end - start,
                "header": {"offset": start, "length": header_end - start, "flag": flag, "progress": progress},
                "body": {"offset": header_end, "length": body_end - header_end},
                "guard": {"offset": body_end, "length": 4} if guard_present else None,
                "strings": strings,
                "zlib": zlib_record,
            }
        )

    ledger_row = ledger.finish()

    dependency_rows = [
        {"role": "map", "sourcePath": record["text"]}
        for _, record in sorted(dependencies.items())
    ]

    return {
        "saveFragment": {"totalBytes": len(data), "blocks": blocks},
        "dependencyCandidates": dependency_rows,
        "typedUnidentified": typed_unidentified,
        "omissions": omissions,
        "ledgerRow": ledger_row,
    }
