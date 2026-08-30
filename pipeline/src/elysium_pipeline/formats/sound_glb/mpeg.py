"""The offset-carrying MPEG audio frame walk, and the tag blocks that bracket it.

MP3 decoding is not bit-exact across decoders, so the authored datum is the bitstream: the walk
locates every frame and the unit copies those frames verbatim into its payload. Everything a
frame header states is decoded beside it, and the ID3, Xing/Info and LAME blocks that surround
the stream are decoded into `tags`.
"""

from __future__ import annotations

from dataclasses import dataclass
import struct
from typing import Any

#: Layer III bitrates in kbit/s, indexed by the header's four-bit field.
BITRATES_V1_L3 = (
    None, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, None,
)
BITRATES_V2_L3 = (
    None, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, None,
)

#: Sample rates by MPEG version id.
SAMPLE_RATES = {
    3: (44100, 48000, 32000, None),         # MPEG-1
    2: (22050, 24000, 16000, None),         # MPEG-2
    0: (11025, 12000, 8000, None),          # MPEG-2.5
}

VERSION_NAMES = {3: "mpeg-1", 2: "mpeg-2", 0: "mpeg-2.5"}
MODE_NAMES = ("stereo", "joint-stereo", "dual-channel", "mono")
EMPHASIS_NAMES = ("none", "50/15-ms", "reserved", "ccit-j.17")

#: Sample frames one Layer III frame carries.
SAMPLES_PER_FRAME = {3: 1152, 2: 576, 0: 576}

ID3V2_HEADER_BYTES = 10
ID3V1_BYTES = 128

#: The genre names ID3v1 indexes; beyond the list the byte is carried as its number alone.
ID3V1_GENRES = (
    "Blues", "Classic Rock", "Country", "Dance", "Disco", "Funk", "Grunge", "Hip-Hop", "Jazz",
    "Metal", "New Age", "Oldies", "Other", "Pop", "R&B", "Rap", "Reggae", "Rock", "Techno",
    "Industrial", "Alternative", "Ska", "Death Metal", "Pranks", "Soundtrack", "Euro-Techno",
    "Ambient", "Trip-Hop", "Vocal", "Jazz+Funk", "Fusion", "Trance", "Classical", "Instrumental",
    "Acid", "House", "Game", "Sound Clip", "Gospel", "Noise", "Alt. Rock", "Bass", "Soul",
    "Punk", "Space", "Meditative", "Instrumental Pop", "Instrumental Rock", "Ethnic", "Gothic",
    "Darkwave", "Techno-Industrial", "Electronic", "Pop-Folk", "Eurodance", "Dream",
    "Southern Rock", "Comedy", "Cult", "Gangsta Rap", "Top 40", "Christian Rap", "Pop/Funk",
    "Jungle", "Native American", "Cabaret", "New Wave", "Psychedelic", "Rave", "Showtunes",
    "Trailer", "Lo-Fi", "Tribal", "Acid Punk", "Acid Jazz", "Polka", "Retro", "Musical",
    "Rock & Roll", "Hard Rock",
)


@dataclass(frozen=True, slots=True)
class RawFrame:
    """One MPEG frame's extent and every field its header states."""

    index: int
    offset: int
    length: int
    version: int
    layer: int
    bitrate: int
    sample_rate: int
    padding: bool
    private: bool
    mode: int
    mode_extension: int
    copyright: bool
    original: bool
    emphasis: int
    crc_present: bool
    crc_declared: int | None
    crc_valid: bool | None
    main_data_begin: int
    side_offset: int
    side_length: int

    @property
    def channels(self) -> int:
        return 1 if self.mode == 3 else 2

    @property
    def samples(self) -> int:
        return SAMPLES_PER_FRAME[self.version]


@dataclass(frozen=True, slots=True)
class MpegWalk:
    """What the stream walk found, in file order."""

    leading: tuple[int, int]
    id3v2: dict[str, Any] | None
    id3v2_range: tuple[int, int] | None
    frames: tuple[RawFrame, ...]
    id3v1: dict[str, Any] | None
    id3v1_offset: int | None
    trailing: tuple[int, int]


def crc16(data: bytes) -> int:
    """The CRC-16 an MPEG frame protects its header and side information with."""

    register = 0xFFFF
    for byte in data:
        register ^= byte << 8
        for _ in range(8):
            register = ((register << 1) ^ 0x8005) & 0xFFFF if register & 0x8000 \
                else (register << 1) & 0xFFFF
    return register


def _side_length(version: int, mode: int) -> int:
    if version == 3:
        return 17 if mode == 3 else 32
    return 9 if mode == 3 else 17


def parse_frame_header(data: bytes, offset: int, index: int) -> RawFrame | None:
    """One frame at `offset`, or None when the bytes there are not a Layer III frame."""

    if offset + 4 > len(data):
        return None
    header = struct.unpack_from(">I", data, offset)[0]
    if (header >> 21) & 0x7FF != 0x7FF:
        return None
    version = (header >> 19) & 3
    layer = (header >> 17) & 3
    if version == 1 or layer != 1:          # reserved version, or not Layer III
        return None
    protection = (header >> 16) & 1
    bitrate_index = (header >> 12) & 0xF
    rate_index = (header >> 10) & 3
    padding = bool((header >> 9) & 1)
    private = bool((header >> 8) & 1)
    mode = (header >> 6) & 3
    mode_extension = (header >> 4) & 3
    copyright_flag = bool((header >> 3) & 1)
    original = bool((header >> 2) & 1)
    emphasis = header & 3
    table = BITRATES_V1_L3 if version == 3 else BITRATES_V2_L3
    bitrate = table[bitrate_index]
    sample_rate = SAMPLE_RATES[version][rate_index]
    if not bitrate or not sample_rate:
        return None
    coefficient = 144 if version == 3 else 72
    length = coefficient * bitrate * 1000 // sample_rate + (1 if padding else 0)
    if length < 4 or offset + length > len(data):
        return None
    side_length = _side_length(version, mode)
    side_offset = offset + 4 + (0 if protection else 2)
    if side_offset + side_length > offset + length:
        return None
    crc_declared: int | None = None
    crc_valid: bool | None = None
    if not protection:
        crc_declared = struct.unpack_from(">H", data, offset + 4)[0]
        computed = crc16(
            bytes(data[offset + 2:offset + 4])
            + bytes(data[side_offset:side_offset + side_length])
        )
        crc_valid = computed == crc_declared
    side = data[side_offset:side_offset + side_length]
    if version == 3:
        main_data_begin = (side[0] << 1) | (side[1] >> 7)
    else:
        main_data_begin = side[0]
    return RawFrame(
        index=index,
        offset=offset,
        length=length,
        version=version,
        layer=3,
        bitrate=bitrate,
        sample_rate=sample_rate,
        padding=padding,
        private=private,
        mode=mode,
        mode_extension=mode_extension,
        copyright=copyright_flag,
        original=original,
        emphasis=emphasis,
        crc_present=not protection,
        crc_declared=crc_declared,
        crc_valid=crc_valid,
        main_data_begin=main_data_begin,
        side_offset=side_offset,
        side_length=side_length,
    )


def _synchsafe(raw: bytes) -> int:
    value = 0
    for byte in raw:
        value = (value << 7) | (byte & 0x7F)
    return value


#: The frame ids whose body this seam decodes into published fields. Every other id keeps its
#: bytes claimed by the frame that owns them and its identity in `coverage.typedUnidentified`.
ID3V2_TEXT_PREFIXES = ("T", "W")
ID3V2_DESCRIBED_FRAMES = ("TXXX", "WXXX", "COMM")


def decodes_id3v2_frame(identifier: str) -> bool:
    """True where `decode_id3v2_frame` publishes the whole body rather than carrying it."""

    return identifier[:1] in ID3V2_TEXT_PREFIXES or identifier == "COMM"


def _id3_take(payload: bytes, encoding: int) -> tuple[str, bytes]:
    """The first NUL-terminated string of a text body, and the bytes after its terminator.

    The terminator is two bytes wide in the two UTF-16 encodings, which is why the split is done
    over bytes rather than over decoded text.
    """

    if encoding in (1, 2):
        codec = "utf-16" if encoding == 1 else "utf-16-be"
        position = 0
        while position + 1 < len(payload):
            if payload[position:position + 2] == b"\0\0":
                return payload[:position].decode(codec, "replace"), payload[position + 2:]
            position += 2
        return payload.decode(codec, "replace"), b""
    codec = "utf-8" if encoding == 3 else "cp1252"
    head, separator, tail = payload.partition(b"\0")
    return head.decode(codec, "replace"), (tail if separator else b"")


def decode_id3v2_frame(identifier: str, payload: bytes) -> dict[str, Any] | None:
    """One ID3v2 frame body as published fields, or None where the seam carries it instead.

    A `TXXX`/`WXXX`/`COMM` body is a description and a value, so both are published; cutting at
    the first NUL would drop the value. A link frame other than `WXXX` carries no encoding byte:
    its body is an ISO-8859-1 URL.
    """

    if not decodes_id3v2_frame(identifier):
        return None
    if not payload:
        return {"text": ""}
    if identifier[:1] == "W" and identifier != "WXXX":
        return {"text": payload.split(b"\0", 1)[0].decode("latin-1", "replace")}
    encoding, rest = payload[0], payload[1:]
    row: dict[str, Any] = {}
    if identifier == "COMM":
        row["language"] = rest[:3].decode("latin-1", "replace")
        rest = rest[3:]
    if identifier in ID3V2_DESCRIBED_FRAMES:
        description, rest = _id3_take(rest, encoding)
        row["description"] = description
        if identifier == "WXXX":
            row["text"] = rest.split(b"\0", 1)[0].decode("latin-1", "replace")
            return row
    row["text"] = _id3_take(rest, encoding)[0]
    return row


def decode_id3v2(data: bytes, offset: int) -> tuple[dict[str, Any], int]:
    """The ID3v2 block at `offset`, and the byte length it occupies."""

    major, revision, flags = data[offset + 3], data[offset + 4], data[offset + 5]
    size = _synchsafe(data[offset + 6:offset + 10])
    total = ID3V2_HEADER_BYTES + size + (ID3V2_HEADER_BYTES if flags & 0x10 else 0)
    total = min(total, len(data) - offset)
    body_end = offset + ID3V2_HEADER_BYTES + size
    position = offset + ID3V2_HEADER_BYTES
    extended: dict[str, Any] | None = None
    if flags & 0x40 and position + 4 <= len(data):
        extended_size = struct.unpack_from(">I", data, position)[0]
        extended = {"sourceOffset": position, "byteLength": extended_size}
        position += 4 + extended_size
    frames: list[dict[str, Any]] = []
    padding_offset = body_end
    while position + ID3V2_HEADER_BYTES <= min(body_end, len(data)):
        identifier = bytes(data[position:position + 4])
        if identifier == b"\0\0\0\0":
            padding_offset = position
            break
        if major >= 4:
            frame_size = _synchsafe(data[position + 4:position + 8])
        else:
            frame_size = struct.unpack_from(">I", data, position + 4)[0]
        frame_flags = struct.unpack_from(">H", data, position + 8)[0]
        start = position + ID3V2_HEADER_BYTES
        if start + frame_size > min(body_end, len(data)):
            break
        row: dict[str, Any] = {
            "id": identifier.decode("latin-1"),
            "sourceOffset": position,
            "byteLength": frame_size,
            "flags": frame_flags,
        }
        if frame_size:
            decoded = decode_id3v2_frame(row["id"],
                                         bytes(data[start:start + frame_size]))
            if decoded is not None:
                row.update(decoded)
        frames.append(row)
        position = start + frame_size
        padding_offset = position
    decoded = {
        "sourceOffset": offset,
        "byteLength": total,
        "version": f"2.{major}.{revision}",
        "flags": flags,
        "declaredSize": size,
        "extendedHeader": extended,
        "frames": frames,
        "padding": {
            "sourceOffset": padding_offset,
            "byteLength": max(0, offset + total - padding_offset),
        },
    }
    return decoded, total


def decode_id3v1(block: bytes, offset: int) -> dict[str, Any]:
    """The trailing 128-byte ID3v1 tag."""

    def text(raw: bytes) -> str:
        return raw.split(b"\0", 1)[0].rstrip(b" ").decode("cp1252", "replace")

    comment_raw = block[97:127]
    track = None
    if comment_raw[28:29] == b"\0" and comment_raw[29:30] != b"\0":
        track = comment_raw[29]
        comment_raw = comment_raw[:28]
    genre = block[127]
    return {
        "sourceOffset": offset,
        "title": text(block[3:33]),
        "artist": text(block[33:63]),
        "album": text(block[63:93]),
        "year": text(block[93:97]),
        "comment": text(comment_raw),
        "track": track,
        "genre": genre,
        "genreName": ID3V1_GENRES[genre] if genre < len(ID3V1_GENRES) else None,
    }


def decode_xing(data: bytes, frame: RawFrame) -> dict[str, Any] | None:
    """The Xing/Info tag the first frame may carry, with the LAME block behind it."""

    candidates = [frame.side_offset + frame.side_length]
    if frame.crc_present:
        candidates.append(frame.offset + 4 + frame.side_length)
    frame_end = frame.offset + frame.length
    for start in candidates:
        if start + 8 > frame_end:
            continue
        marker = bytes(data[start:start + 4])
        if marker not in (b"Xing", b"Info"):
            continue
        flags = struct.unpack_from(">I", data, start + 4)[0]
        position = start + 8
        decoded: dict[str, Any] = {
            "tag": marker.decode("ascii"),
            "sourceOffset": start,
            "flags": flags,
            "frameCount": None,
            "byteCount": None,
            "toc": None,
            "quality": None,
        }
        if flags & 0x1 and position + 4 <= frame_end:
            decoded["frameCount"] = struct.unpack_from(">I", data, position)[0]
            position += 4
        if flags & 0x2 and position + 4 <= frame_end:
            decoded["byteCount"] = struct.unpack_from(">I", data, position)[0]
            position += 4
        if flags & 0x4 and position + 100 <= frame_end:
            decoded["toc"] = list(data[position:position + 100])
            position += 100
        if flags & 0x8 and position + 4 <= frame_end:
            decoded["quality"] = struct.unpack_from(">I", data, position)[0]
            position += 4
        decoded["lame"] = decode_lame(data, position, frame_end)
        return decoded
    return None


def decode_lame(data: bytes, offset: int, limit: int) -> dict[str, Any] | None:
    """The LAME extension behind a Xing/Info tag, where the encoder wrote one."""

    if offset + 20 > limit:
        return None
    encoder = bytes(data[offset:offset + 9])
    if not encoder[:4].isalpha():
        return None
    revision_method = data[offset + 9]
    delay_bytes = data[offset + 21:offset + 24] if offset + 24 <= limit else b"\0\0\0"
    return {
        "sourceOffset": offset,
        "encoder": encoder.decode("latin-1").rstrip("\0 "),
        "tagRevision": revision_method >> 4,
        "vbrMethod": revision_method & 0x0F,
        "lowpassHz": data[offset + 10] * 100,
        "replayGainPeak": struct.unpack_from(">I", data, offset + 11)[0],
        "radioReplayGain": struct.unpack_from(">H", data, offset + 15)[0],
        "audiophileReplayGain": struct.unpack_from(">H", data, offset + 17)[0],
        "encoderDelay": (delay_bytes[0] << 4) | (delay_bytes[1] >> 4),
        "encoderPadding": ((delay_bytes[1] & 0x0F) << 8) | delay_bytes[2],
    }


def walk(data: bytes) -> MpegWalk:
    """Walk one MPEG member: leading bytes, ID3v2, every frame, ID3v1, trailing bytes."""

    position = 0
    id3v2 = None
    id3v2_range = None
    if len(data) >= ID3V2_HEADER_BYTES and data[:3] == b"ID3":
        id3v2, total = decode_id3v2(data, 0)
        id3v2_range = (0, total)
        position = total
    end = len(data)
    id3v1 = None
    id3v1_offset = None
    if end - position >= ID3V1_BYTES and data[end - ID3V1_BYTES:end - ID3V1_BYTES + 3] == b"TAG":
        id3v1_offset = end - ID3V1_BYTES
        id3v1 = decode_id3v1(bytes(data[id3v1_offset:end]), id3v1_offset)
        end = id3v1_offset
    lead_start = position
    frames: list[RawFrame] = []
    while position < end:
        frame = parse_frame_header(data, position, len(frames))
        if frame is None or frame.offset + frame.length > end:
            position += 1
            continue
        break
    first = position
    while position < end:
        frame = parse_frame_header(data, position, len(frames))
        if frame is None or frame.offset + frame.length > end:
            break
        frames.append(frame)
        position += frame.length
    if not frames:
        first = end
        position = end
    return MpegWalk(
        leading=(lead_start, max(0, first - lead_start)),
        id3v2=id3v2,
        id3v2_range=id3v2_range,
        frames=tuple(frames),
        id3v1=id3v1,
        id3v1_offset=id3v1_offset,
        trailing=(position, max(0, end - position)),
    )
