"""The offset-carrying RIFF chunk walk, and the body decode of every id this seam reads.

A WAVE member is a 12-byte envelope header and a sequence of `<id><size><body>[pad]` chunks. The
walk is written against what the bytes support rather than what the envelope claims: 196 shipped
members declare a size that disagrees with their length, so the walk stops at the envelope and the
excess is reported as its own region for the decoder to classify.
"""

from __future__ import annotations

from dataclasses import dataclass
import struct
from typing import Any

RIFF_MAGIC = b"RIFF"
WAVE_TYPE = b"WAVE"

#: The 12 bytes every WAVE member opens with: `RIFF`, the envelope size, `WAVE`.
HEADER_BYTES = 12
CHUNK_HEADER_BYTES = 8

#: The RIFF size field counts every byte after itself, so the envelope ends this far past it.
SIZE_FIELD_END = 8

#: `fmt ` format tags this seam understands. Any other tag is `unsupported` and fails the unit.
#: The `bext` reserved region: one past the UMID, up to the coding history the block ends with.
BEXT_RESERVED_START = 412
BEXT_CODING_HISTORY = 602

FORMAT_PCM = 1
FORMAT_MS_ADPCM = 2
FORMAT_NAMES = {FORMAT_PCM: "pcm", FORMAT_MS_ADPCM: "ms-adpcm"}


class RiffWalkError(ValueError):
    """The member is not the RIFF/WAVE container the seam publishes."""


@dataclass(frozen=True, slots=True)
class RawChunk:
    """One chunk's extent, in the coordinates of the member the walk was handed.

    `offset` is the chunk header; `body_offset` is `offset + 8`. `pad` is the odd-length pad byte
    the format requires, and is False where the member omitted it.
    """

    index: int
    id: bytes
    offset: int
    length: int
    pad: bool
    beyond_envelope: bool

    @property
    def body_offset(self) -> int:
        return self.offset + CHUNK_HEADER_BYTES


@dataclass(frozen=True, slots=True)
class RiffWalk:
    """What the chunk walk found: the envelope it trusted and the region beyond it."""

    declared_size: int
    chunks: tuple[RawChunk, ...]
    excess_offset: int
    unpadded: tuple[int, ...]


def walk(data: bytes) -> RiffWalk:
    """Walk one WAVE member's chunks, stopping at the RIFF envelope's declared end."""

    if len(data) < HEADER_BYTES:
        raise RiffWalkError(f"a WAVE member is at least {HEADER_BYTES} bytes, not {len(data)}")
    if data[:4] != RIFF_MAGIC or data[8:12] != WAVE_TYPE:
        raise RiffWalkError("the member does not open with a RIFF/WAVE header")
    declared = struct.unpack_from("<I", data, 4)[0]
    # The envelope is believed only as far as the bytes go: a declared end past the member is
    # `stale-riff-length`, never a read past the end of the buffer.
    envelope_end = min(SIZE_FIELD_END + declared, len(data))
    chunks: list[RawChunk] = []
    unpadded: list[int] = []
    position = HEADER_BYTES
    while position + CHUNK_HEADER_BYTES <= envelope_end:
        identifier = bytes(data[position:position + 4])
        length = struct.unpack_from("<I", data, position + 4)[0]
        body_end = position + CHUNK_HEADER_BYTES + length
        if body_end > len(data):
            break                       # the chunk overruns the member; the rest is excess
        pad = False
        if length % 2:
            if body_end < envelope_end and data[body_end] == 0:
                pad = True
            else:
                unpadded.append(position)
        chunks.append(RawChunk(len(chunks), identifier, position, length, pad, False))
        position = body_end + (1 if pad else 0)
    return RiffWalk(
        declared_size=declared,
        chunks=tuple(chunks),
        excess_offset=position,
        unpadded=tuple(unpadded),
    )


def walk_excess(data: bytes, offset: int) -> tuple[RawChunk, ...]:
    """Chunks written past the RIFF envelope, or nothing when the excess is not chunk-shaped.

    Seven members carry a `smpl` chunk beyond the declared length and 189 carry an 18-byte Sound
    Forge trailer whose first four bytes are not a chunk id; only the first case parses.
    """

    chunks: list[RawChunk] = []
    position = offset
    while position + CHUNK_HEADER_BYTES <= len(data):
        identifier = bytes(data[position:position + 4])
        if not all(0x20 <= byte <= 0x7E for byte in identifier):
            return ()
        length = struct.unpack_from("<I", data, position + 4)[0]
        body_end = position + CHUNK_HEADER_BYTES + length
        if body_end > len(data):
            return ()
        pad = False
        if length % 2 and body_end < len(data) and data[body_end] == 0:
            pad = True
        chunks.append(RawChunk(len(chunks), identifier, position, length, pad, True))
        position = body_end + (1 if pad else 0)
    return tuple(chunks) if position == len(data) and chunks else ()


def _text(raw: bytes) -> str:
    """A RIFF text field: CP-1252, trimmed at the first NUL, trailing NULs discarded."""

    cut = raw.split(b"\0", 1)[0]
    return cut.decode("cp1252", "replace")


def decode_format(body: bytes) -> dict[str, Any]:
    """`fmt `: every field including the extension bytes."""

    if len(body) < 16:
        raise RiffWalkError(f"a fmt chunk is at least 16 bytes, not {len(body)}")
    tag, channels, rate, byte_rate, block_align, bits = struct.unpack_from("<HHIIHH", body, 0)
    decoded: dict[str, Any] = {
        "formatTag": tag,
        "formatName": FORMAT_NAMES.get(tag, "unknown"),
        "channels": channels,
        "sampleRate": rate,
        "byteRate": byte_rate,
        "blockAlign": block_align,
        "bitsPerSample": bits,
    }
    if len(body) >= 18:
        extension_size = struct.unpack_from("<H", body, 16)[0]
        decoded["extensionSize"] = extension_size
        if tag == FORMAT_MS_ADPCM and len(body) >= 22:
            samples_per_block, coefficient_count = struct.unpack_from("<HH", body, 18)
            decoded["samplesPerBlock"] = samples_per_block
            decoded["coefficientCount"] = coefficient_count
            needed = 22 + 4 * coefficient_count
            if len(body) < needed:
                raise RiffWalkError(
                    f"a fmt chunk declaring {coefficient_count} coefficient pairs is "
                    f"{needed} bytes, not {len(body)}"
                )
            decoded["coefficients"] = [
                list(struct.unpack_from("<hh", body, 22 + 4 * index))
                for index in range(coefficient_count)
            ]
    return decoded


def decode_fact(body: bytes) -> dict[str, Any]:
    """`fact`: the sample count the encoder claimed. Carried and compared, never trusted."""

    if len(body) < 4:
        raise RiffWalkError(f"a fact chunk is at least 4 bytes, not {len(body)}")
    return {"sampleLength": struct.unpack_from("<I", body, 0)[0]}


def decode_cue(body: bytes) -> dict[str, Any]:
    """`cue `: the cue points an authoring tool left in the member."""

    if len(body) < 4:
        raise RiffWalkError(f"a cue chunk is at least 4 bytes, not {len(body)}")
    count = struct.unpack_from("<I", body, 0)[0]
    points = []
    for index in range(count):
        start = 4 + 24 * index
        if start + 24 > len(body):
            break
        identifier, position, chunk_id, chunk_start, block_start, offset = struct.unpack_from(
            "<II4sIII", body, start
        )
        points.append({
            "id": identifier,
            "position": position,
            "chunkId": chunk_id.decode("latin-1"),
            "chunkStart": chunk_start,
            "blockStart": block_start,
            "sampleOffset": offset,
        })
    return {"cuePointCount": count, "points": points}


def decode_smpl(body: bytes) -> dict[str, Any]:
    """`smpl`: the sampler record, with the loops it declares."""

    if len(body) < 36:
        raise RiffWalkError(f"a smpl chunk is at least 36 bytes, not {len(body)}")
    (manufacturer, product, period, note, fraction, smpte_format, smpte_offset,
     loop_count, sampler_bytes) = struct.unpack_from("<IIIIIIIII", body, 0)
    loops = []
    for index in range(loop_count):
        start = 36 + 24 * index
        if start + 24 > len(body):
            break
        identifier, kind, begin, end, loop_fraction, plays = struct.unpack_from(
            "<IIIIII", body, start
        )
        loops.append({
            "id": identifier,
            "type": kind,
            "start": begin,
            "end": end,
            "fraction": loop_fraction,
            "playCount": plays,
        })
    return {
        "manufacturer": manufacturer,
        "product": product,
        "samplePeriod": period,
        "midiUnityNote": note,
        "midiPitchFraction": fraction,
        "smpteFormat": smpte_format,
        "smpteOffset": smpte_offset,
        "loopCount": loop_count,
        "samplerDataBytes": sampler_bytes,
        "loops": loops,
    }


def decode_list(body: bytes, offset: int) -> dict[str, Any]:
    """`LIST`: the list type and its sub-chunks; `INFO` sub-chunks decoded as text."""

    if len(body) < 4:
        raise RiffWalkError(f"a LIST chunk is at least 4 bytes, not {len(body)}")
    list_type = body[:4].decode("latin-1")
    entries: list[dict[str, Any]] = []
    position = 4
    while position + CHUNK_HEADER_BYTES <= len(body):
        identifier = body[position:position + 4].decode("latin-1")
        length = struct.unpack_from("<I", body, position + 4)[0]
        start = position + CHUNK_HEADER_BYTES
        if start + length > len(body):
            break
        payload = body[start:start + length]
        entry: dict[str, Any] = {
            "id": identifier,
            "sourceOffset": offset + start,
            "byteLength": length,
        }
        if list_type == "INFO":
            entry["text"] = _text(payload)
        elif identifier in ("labl", "note") and length >= 4:
            entry["cuePointId"] = struct.unpack_from("<I", payload, 0)[0]
            entry["text"] = _text(payload[4:])
        elif identifier == "ltxt" and length >= 20:
            (cue_id, sample_length, purpose, country, language, dialect,
             code_page) = struct.unpack_from("<II4sHHHH", payload, 0)
            entry.update({
                "cuePointId": cue_id,
                "sampleLength": sample_length,
                "purpose": purpose.decode("latin-1"),
                "country": country,
                "language": language,
                "dialect": dialect,
                "codePage": code_page,
            })
            if length > 20:
                entry["text"] = _text(payload[20:])
        entries.append(entry)
        position = start + length + (length % 2)
    return {"listType": list_type, "subChunks": entries}


def decode_bext(body: bytes) -> dict[str, Any]:
    """`bext`: the Broadcast Wave description block.

    The 190 bytes between the UMID and the coding history are the specification's reserved
    region; no field represents them, so the decoder stops at `BEXT_RESERVED_START` and the
    ledger claims that run as `reserved-zero` rather than folding it into the decoded body.
    """

    if len(body) < BEXT_CODING_HISTORY:
        raise RiffWalkError(
            f"a bext chunk is at least {BEXT_CODING_HISTORY} bytes, not {len(body)}"
        )
    return {
        "description": _text(body[0:256]),
        "originator": _text(body[256:288]),
        "originatorReference": _text(body[288:320]),
        "originationDate": _text(body[320:330]),
        "originationTime": _text(body[330:338]),
        "timeReferenceLow": struct.unpack_from("<I", body, 338)[0],
        "timeReferenceHigh": struct.unpack_from("<I", body, 342)[0],
        "version": struct.unpack_from("<H", body, 346)[0],
        "umid": body[348:BEXT_RESERVED_START].hex(),
        "codingHistory": _text(body[BEXT_CODING_HISTORY:]),
    }
