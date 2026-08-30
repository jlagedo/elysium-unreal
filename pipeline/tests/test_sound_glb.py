"""Synthetic contract tests for the isolated Sound GLB exporter.

Every member here is built in the test: the real install is never read, so a machine without
VtMB runs the same proofs as one with it.
"""

from __future__ import annotations

import hashlib
import struct

import pytest

from elysium_pipeline.exporters import sound_glb
from elysium_pipeline.formats.sound_glb import (
    SCHEMA_VERSION,
    SOUND_EXTENSION,
    decode_sound,
    load_source_closure,
    output_relative_path,
    source_keys,
)
from elysium_pipeline.formats.sound_glb import adpcm, mpeg
from elysium_pipeline.formats.unit_contract.coverage import ROOT_KEYS
from elysium_pipeline.validation import sound_glb as validation


# --------------------------------------------------------------------------- builders


def _chunk(identifier: bytes, body: bytes) -> bytes:
    return identifier + struct.pack("<I", len(body)) + body + (b"\0" if len(body) % 2 else b"")


def _riff(*chunks: bytes, size: int | None = None, trailer: bytes = b"") -> bytes:
    body = b"WAVE" + b"".join(chunks)
    declared = len(body) if size is None else size
    return b"RIFF" + struct.pack("<I", declared) + body + trailer


COEFFICIENTS = adpcm.STANDARD_COEFFICIENTS


def _adpcm_format(channels: int = 1, block_align: int = 32) -> bytes:
    extension = struct.pack("<HH", adpcm.samples_per_block(block_align, channels), 7)
    extension += b"".join(struct.pack("<hh", *pair) for pair in COEFFICIENTS)
    return struct.pack(
        "<HHIIHHH", 2, channels, 22050, 22050 * block_align // 244, block_align, 4,
        len(extension),
    ) + extension


def _adpcm_block(block_align: int = 32) -> bytes:
    head = bytes([0]) + struct.pack("<hhh", 16, 1000, 900)
    return head + bytes(range(block_align - len(head)))


def _pcm_format(bits: int, channels: int = 1) -> bytes:
    align = channels * bits // 8
    return struct.pack("<HHIIHH", 1, channels, 22050, 22050 * align, align, bits)


ADPCM_WAV = _riff(
    _chunk(b"fmt ", _adpcm_format()),
    _chunk(b"fact", struct.pack("<I", 52)),
    _chunk(b"data", _adpcm_block()),
)

#: A member whose `fact` disagrees with the blocks `data` actually holds.
STALE_FACT_WAV = _riff(
    _chunk(b"fmt ", _adpcm_format()),
    _chunk(b"fact", struct.pack("<I", 999)),
    _chunk(b"data", _adpcm_block()),
)

PCM16_WAV = _riff(
    _chunk(b"fmt ", _pcm_format(16)),
    _chunk(b"data", struct.pack("<8h", 0, 1, -1, 32767, -32768, 7, 8, 9)),
)

PCM8_WAV = _riff(
    _chunk(b"fmt ", _pcm_format(8)),
    _chunk(b"data", bytes([0, 128, 255, 64])),
)

#: An odd-length `data` chunk followed by its zero pad byte; the pad is the seam's only
#: `padding-zero` claim over a WAVE member.
ODD_PCM8_WAV = _riff(
    _chunk(b"fmt ", _pcm_format(8)),
    _chunk(b"data", bytes([1, 2, 3])),
)

#: The Sound Forge trailer: ten zero bytes, a length and `W3DI`, written past the envelope.
W3DI_TRAILER = b"\0" * 10 + struct.pack("<I", 18) + b"W3DI"
TRAILER_WAV = _riff(
    _chunk(b"fmt ", _pcm_format(16)),
    _chunk(b"data", struct.pack("<2h", 5, 6)),
    trailer=W3DI_TRAILER,
)

#: Sound Forge session chunks the seam carries rather than decodes.
UNKNOWN_CHUNK_WAV = _riff(
    _chunk(b"fmt ", _pcm_format(16)),
    _chunk(b"minf", bytes(16)),
    _chunk(b"data", struct.pack("<2h", 5, 6)),
    _chunk(b"ovwf", bytes(24)),
)

#: The metadata chunks the corpus writes beside the waveform: an `INFO` list, an `adtl` list
#: keyed to a cue point, the cue point itself, a sampler record and a Broadcast Wave block.
INFO_LIST = b"INFO" + _chunk(b"ISFT", b"Lavf58.34.101\0")
ADTL_LIST = (
    b"adtl"
    + _chunk(b"labl", struct.pack("<I", 1) + b"marker\0")
    + _chunk(b"ltxt", struct.pack("<II4sHHHH", 1, 100, b"rgn ", 0, 0, 0, 0) + b"region\0")
)
CUE_POINTS = struct.pack("<I", 1) + struct.pack("<II4sIII", 1, 64, b"data", 0, 0, 64)
SAMPLER = (
    struct.pack("<IIIIIIIII", 0, 0, 22675, 60, 0, 0, 0, 1, 0)
    + struct.pack("<IIIIII", 0, 0, 0, 7, 0, 0)
)
BEXT_UMID = bytes(range(64))
BEXT_CODING_HISTORY = b"A=PCM,F=22050,W=16,M=mono"


def _bext(umid: bytes = BEXT_UMID) -> bytes:
    """A 602-byte Broadcast Wave block; bytes 412..602 are the reserved region."""

    block = bytearray(602)
    block[0:5] = b"take1"
    block[256:262] = b"studio"
    block[320:330] = b"2004-11-16"
    block[330:338] = b"09:00:00"
    struct.pack_into("<I", block, 338, 44100)
    struct.pack_into("<H", block, 346, 1)
    block[348:412] = umid
    return bytes(block) + BEXT_CODING_HISTORY


METADATA_WAV = _riff(
    _chunk(b"fmt ", _pcm_format(16)),
    _chunk(b"LIST", INFO_LIST),
    _chunk(b"LIST", ADTL_LIST),
    _chunk(b"cue ", CUE_POINTS),
    _chunk(b"smpl", SAMPLER),
    _chunk(b"bext", _bext()),
    _chunk(b"data", struct.pack("<4h", 1, 2, 3, 4)),
)

UNSUPPORTED_WAV = _riff(
    _chunk(b"fmt ", struct.pack("<HHIIHH", 0x11, 1, 22050, 11025, 256, 4)),
    _chunk(b"data", bytes(16)),
)


def _mp3_frame(*, bitrate_index: int = 9, crc: bool = True, mode: int = 3,
               corrupt_crc: bool = False) -> bytes:
    header = bytearray(4)
    header[0] = 0xFF
    header[1] = 0b11111010 if crc else 0b11111011
    header[2] = (bitrate_index << 4) | 0x00
    header[3] = (mode << 6)
    bitrate = mpeg.BITRATES_V1_L3[bitrate_index]
    length = 144 * bitrate * 1000 // 44100
    side = 17 if mode == 3 else 32
    side_bytes = bytes(range(1, side + 1))
    if crc:
        digest = mpeg.crc16(bytes(header[2:4]) + side_bytes)
        if corrupt_crc:
            digest ^= 0xFFFF
        body = struct.pack(">H", digest) + side_bytes
    else:
        body = side_bytes
    frame = bytes(header) + body
    return frame + bytes(length - len(frame))


PLAIN_MP3 = _mp3_frame() + _mp3_frame() + _mp3_frame()


def _id3v2(*frames: tuple[bytes, bytes], padding: int = 8) -> bytes:
    body = b"".join(
        identifier + struct.pack(">I", len(payload)) + b"\0\0" + payload
        for identifier, payload in frames
    ) + bytes(padding)
    size = len(body)
    synchsafe = bytes([
        (size >> 21) & 0x7F, (size >> 14) & 0x7F, (size >> 7) & 0x7F, size & 0x7F,
    ])
    return b"ID3" + b"\x03\x00" + b"\0" + synchsafe + body


def _id3v1(title: bytes = b"line") -> bytes:
    return b"TAG" + title.ljust(30, b"\0") + bytes(128 - 33)


TAGGED_MP3 = (
    _id3v2((b"TIT2", b"\0line191"))
    + _mp3_frame()
    + _mp3_frame()
    + _id3v1()
)

#: The frame shapes the 116 tagged members write: a plain text frame, a `TXXX` whose value
#: sits behind a NUL separator, and a `PRIV` whose body no field of this seam represents.
NUL = bytes(1)
PRIV_BODY = b"XMP" + NUL + b"<?xpacket?>"
RICH_TAGGED_MP3 = (
    _id3v2(
        (b"TIT2", NUL + b"line191"),
        (b"TXXX", NUL + b"creation_time" + NUL + b"2022-10-25" + NUL),
        (b"PRIV", PRIV_BODY),
    )
    + _mp3_frame()
    + _mp3_frame()
)

LEADING_ZERO_MP3 = bytes(16) + _mp3_frame() + _mp3_frame()

CRC_BROKEN_MP3 = _mp3_frame(corrupt_crc=True) + _mp3_frame()

VBR_MP3 = _mp3_frame(bitrate_index=5) + _mp3_frame(bitrate_index=9)

#: Bytes after the last frame that are neither a frame nor a recognised tag.
TRAILING_MP3 = _mp3_frame() + b"\x01\x02\x03"

LIP = (
    b"VERSION 1.2\r\n"
    b"PLAINTEXT\r\n{\r\nHello there\r\n}\r\n"
    b"WORDS\r\n{\r\n"
    b"WORD Hello 0.000 0.320\r\n{\r\n"
    b"104 hh 0.000 0.100 1.000 0\r\n"
    b"603 eh 0.100 0.320 1.000\r\n"
    b"}\r\n"
    b"WORD Come on 0.320 0.400\r\n{\r\n}\r\n"
    b"}\r\n"
    b"EMPHASIS\r\n{\r\n}\r\n"
    b"CLOSECAPTION\r\n{\r\nenglish\r\n{\r\n"
    b'PHRASE char 14 "Hello\r\nthere" 0.000 0.400\r\n'
    b"}\r\n}\r\n"
    b"OPTIONS\r\n{\r\nvoice_duck 1\r\nspeaker_name Jack\r\n}\r\n"
)

#: Seven shipped members end with a stray NUL after the last line terminator.
LIP_WITH_NUL = LIP + b"\0"

#: Shapes the `.lip` grammar does not name: a section this seam has no parser for, and a stray
#: brace no section opened. Neither occurs in the shipped corpus; both are the fallback a patched
#: member would land in, and neither may cost a byte its claim.
LIP_UNKNOWN = LIP + b"NOISES\r\n{\r\nbreath 0.100\r\n}\r\n}\r\n"

#: A `unicode` caption: the counted region is UTF-16LE and the row wrote no quote around it.
_WIDE = "Ja bin".encode("utf-16-le")
LIP_UNICODE = (
    b"VERSION 1.1\n"
    b"PLAINTEXT\n{\n}\n"
    b"WORDS\n{\n}\n"
    b"EMPHASIS\n{\n}\n"
    b"CLOSECAPTION\n{\nenglish\n{\n"
    + b"PHRASE unicode " + str(len(_WIDE)).encode() + b" " + _WIDE + b" 0.000 1.500\n"
    + b"}\n}\n"
    b"OPTIONS\n{\nvoice_duck 0\n}\n"
)


def _index(members: dict[str, bytes], root: str = "Unofficial_Patch") -> dict:
    return {
        key: ("loose", f"C:/game/{root}/{key}")
        for key in members
    }


def _reader(members: dict[str, bytes]):
    return lambda index, key: members.get(key)


def _publish(members: dict[str, bytes], key: str):
    index = _index(members)
    closure = load_source_closure(index, key, read_bytes=_reader(members))
    model = decode_sound(closure)
    document, binary = sound_glb.build_document(model)
    return closure, model, document, binary


def _root(document: dict) -> dict:
    return document["extensions"][SOUND_EXTENSION]


def _ledger(document: dict, path: str) -> dict:
    for row in _root(document)["coverage"]["byteLedger"]:
        if row["sourcePath"] == path:
            return row
    raise AssertionError(f"no ledger row for {path}")


def _assert_partition(row: dict, data: bytes) -> None:
    """Every byte of the member is claimed by exactly one range, in order, once."""

    seen = bytearray(len(data))
    cursor = 0
    for entry in row["ranges"]:
        assert entry["offset"] == cursor, row["sourcePath"]
        assert entry["length"] > 0
        assert entry["owner"]
        for position in range(entry["offset"], entry["offset"] + entry["length"]):
            assert seen[position] == 0, "a byte is claimed twice"
            seen[position] = 1
        cursor += entry["length"]
    assert cursor == len(data)
    assert all(seen), "a byte is claimed by nothing"
    assert row["accountedBytes"] == len(data)
    assert row["coveragePercent"] == 100.0
    assert row["sourceSha256"] == hashlib.sha256(data).hexdigest()


# --------------------------------------------------------------------------- identity


def test_the_key_keeps_the_extension_that_distinguishes_the_two_spellings():
    members = {"sound/music/theme.wav": PCM16_WAV, "sound/music/theme.mp3": PLAIN_MP3}
    _, _, wav_document, _ = _publish(members, "music/theme.wav")
    _, _, mp3_document, _ = _publish(members, "sound/music/theme.mp3")
    assert _root(wav_document)["identity"]["asset"] == "vtmb:sound:music/theme.wav"
    assert _root(mp3_document)["identity"]["asset"] == "vtmb:sound:music/theme.mp3"
    assert str(output_relative_path("music/theme.wav")) == "music/theme.wav.glb"
    assert str(output_relative_path("MUSIC/Theme.MP3")) == "music/theme.mp3.glb"


def test_the_identity_publishes_an_empty_referenced_by_for_the_corpus_index():
    _, _, document, _ = _publish({"sound/a.wav": PCM16_WAV}, "a.wav")
    assert _root(document)["identity"]["referencedBy"] == []
    assert _root(document)["identity"]["sourcePolicy"] == "up-first"


def test_source_keys_selects_only_the_two_audio_spellings():
    index = _index({
        "sound/a.wav": b"", "sound/a.mp3": b"", "sound/a.lip": b"", "sound/a.sfk": b"",
        "sound/a.pk": b"", "sound/a.vcd": b"", "materials/x.vmt": b"",
    })
    assert source_keys(index) == ["a.mp3", "a.wav"]


# --------------------------------------------------------------------------- container


def test_the_unit_is_scene_less_and_requires_its_own_extension():
    _, _, document, _ = _publish({"sound/a.wav": ADPCM_WAV}, "a.wav")
    assert document["asset"]["generator"] == "Elysium Sound GLB Exporter"
    assert document["extensionsUsed"] == [SOUND_EXTENSION]
    assert document["extensionsRequired"] == [SOUND_EXTENSION]
    for forbidden in ("scenes", "nodes", "meshes", "images", "textures", "samplers"):
        assert forbidden not in document


def test_the_extension_root_opens_with_the_contract_keys_in_order():
    _, _, document, _ = _publish({"sound/a.wav": ADPCM_WAV}, "a.wav")
    root = _root(document)
    assert tuple(list(root)[:len(ROOT_KEYS)]) == ROOT_KEYS
    assert root["schemaVersion"] == SCHEMA_VERSION


def test_one_closure_yields_one_byte_identical_product():
    members = {"sound/a.mp3": TAGGED_MP3, "sound/a.lip": LIP}
    from elysium_pipeline.formats.unit_contract.container import encode_glb

    first = encode_glb(*_publish(members, "a.mp3")[2:])
    second = encode_glb(*_publish(members, "a.mp3")[2:])
    assert first == second


# --------------------------------------------------------------------------- WAVE decode


def test_every_wave_byte_is_claimed_exactly_once():
    for member in (ADPCM_WAV, PCM16_WAV, PCM8_WAV, ODD_PCM8_WAV, TRAILER_WAV,
                   UNKNOWN_CHUNK_WAV):
        _, _, document, _ = _publish({"sound/a.wav": member}, "a.wav")
        _assert_partition(_ledger(document, "sound/a.wav"), member)


def test_adpcm_blocks_are_graded_derived_and_pcm16_is_graded_mapped():
    _, _, adpcm_document, _ = _publish({"sound/a.wav": ADPCM_WAV}, "a.wav")
    states = {
        entry["owner"]: entry["state"]
        for entry in _ledger(adpcm_document, "sound/a.wav")["ranges"]
    }
    assert states["riff.chunks[2].body"] == "derived"
    _, _, pcm_document, _ = _publish({"sound/a.wav": PCM16_WAV}, "a.wav")
    states = {
        entry["owner"]: entry["state"]
        for entry in _ledger(pcm_document, "sound/a.wav")["ranges"]
    }
    assert states["riff.chunks[1].body"] == "mapped"


def test_eight_bit_pcm_is_widened_into_the_int16_payload():
    _, _, document, binary = _publish({"sound/a.wav": PCM8_WAV}, "a.wav")
    assert list(struct.unpack("<4h", binary)) == [-32768, 0, 32512, -16384]
    assert _root(document)["payload"]["sampleFormat"] == "int16-interleaved"
    states = {
        entry["owner"]: entry["state"]
        for entry in _ledger(document, "sound/a.wav")["ranges"]
    }
    assert states["riff.chunks[1].body"] == "derived"


def test_the_adpcm_payload_is_the_samples_the_block_decodes_to():
    _, model, _, binary = _publish({"sound/a.wav": ADPCM_WAV}, "a.wav")
    samples, frames, blocks, consumed = adpcm.decode(
        _adpcm_block(), 1, 32, [list(pair) for pair in COEFFICIENTS]
    )
    assert binary == samples.tobytes()
    assert blocks == 1 and consumed == 32
    assert model.codec["durationSamples"] == frames == 52


def test_the_adpcm_predictor_divides_toward_zero_not_toward_minus_infinity():
    """The format's `/ 256` truncates; flooring puts every negative predictor one LSB low."""

    assert adpcm._truncate(-513) == -2
    assert adpcm._truncate(513) == 2
    negative = bytes([3]) + struct.pack("<hhh", 16, -1001, -900) + bytes([0x11] * 25)
    samples, _frames, _blocks, _consumed = adpcm.decode(
        negative, 1, 32, [list(pair) for pair in COEFFICIENTS]
    )
    first, second = COEFFICIENTS[3]
    numerator = -1001 * first + -900 * second
    assert numerator < 0 and numerator % 256, "the vector must divide inexactly to prove anything"
    truncated = -((-numerator) // 256) + 16          # the first nibble is +1 at delta 16
    assert list(samples)[:3] == [-900, -1001, truncated]
    assert truncated != numerator // 256 + 16        # a floored decode lands one LSB below


def test_the_odd_chunk_pad_byte_is_the_only_padding_zero_claim():
    _, _, document, _ = _publish({"sound/a.wav": ODD_PCM8_WAV}, "a.wav")
    padding = [
        entry for entry in _ledger(document, "sound/a.wav")["ranges"]
        if entry["state"] == "padding-zero"
    ]
    assert len(padding) == 1 and padding[0]["length"] == 1
    assert padding[0]["owner"].endswith(".pad")
    assert ODD_PCM8_WAV[padding[0]["offset"]] == 0


def test_a_pad_claim_over_a_non_zero_byte_is_refused_at_export_time():
    members = {"sound/a.wav": ODD_PCM8_WAV}
    closure, _, document, binary = _publish(members, "a.wav")
    validation.validate_document(document, binary, source_members=closure.members())
    row = _ledger(document, "sound/a.wav")
    pad = next(entry for entry in row["ranges"] if entry["state"] == "padding-zero")
    tampered = bytearray(ODD_PCM8_WAV)
    tampered[pad["offset"]] = 0x7F
    forged = closure.audio.__class__(
        role=closure.audio.role, path=closure.audio.path, data=bytes(tampered),
        origin=closure.audio.origin,
    )
    with pytest.raises(validation.SoundGlbValidationError):
        validation.validate_document(document, binary, source_members=(forged,))


def test_the_sound_forge_trailer_is_omitted_with_its_digest():
    _, model, document, _ = _publish({"sound/a.wav": TRAILER_WAV}, "a.wav")
    assert [row["role"] for row in model.omissions] == ["sound-forge-trailer"]
    assert model.omissions[0]["sha256"] == hashlib.sha256(W3DI_TRAILER).hexdigest()
    assert "riff-envelope-excess" in [row["role"] for row in model.anomalies]
    excess = [
        entry for entry in _ledger(document, "sound/a.wav")["ranges"]
        if entry["owner"] == "riff.excess"
    ]
    assert len(excess) == 1 and excess[0]["state"] == "omitted-proven"
    assert {"owner": "riff.excess", "reason": "sound-forge-trailer"} in \
        _root(document)["coverage"]["omittedProven"]


def test_a_sound_forge_chunk_is_carried_typed_by_id_and_offset():
    _, _, document, _ = _publish({"sound/a.wav": UNKNOWN_CHUNK_WAV}, "a.wav")
    root = _root(document)
    typed = root["coverage"]["typedUnidentified"]
    assert sorted(row["id"] for row in typed) == ["minf", "ovwf"]
    owners = {
        entry["owner"]: (entry["offset"], entry["length"])
        for entry in _ledger(document, "sound/a.wav")["ranges"]
    }
    by_id = {chunk["id"]: chunk for chunk in root["chunks"]}
    for row in typed:
        # the row's offset and length name the range its digest covers -- the chunk body --
        # and that range is exactly the one its ledger owner claimed
        region = UNKNOWN_CHUNK_WAV[row["sourceOffset"]:row["sourceOffset"] + row["byteLength"]]
        assert hashlib.sha256(region).hexdigest() == row["sha256"]
        assert owners[row["owner"]] == (row["sourceOffset"], row["byteLength"])
        chunk = by_id[row["id"]]
        assert row["sourceOffset"] == chunk["sourceOffset"] + 8
        assert chunk["body"] is None


def test_every_decoded_chunk_body_carries_the_records_the_member_wrote():
    _, _, document, _ = _publish({"sound/a.wav": METADATA_WAV}, "a.wav")
    root = _root(document)
    lists = [row["body"] for row in root["chunks"] if row["id"] == "LIST"]

    assert lists[0]["listType"] == "INFO"
    software = lists[0]["subChunks"][0]
    assert (software["id"], software["text"]) == ("ISFT", "Lavf58.34.101")
    # the sub-chunk's offset names the place its bytes are, not merely a number
    start, length = software["sourceOffset"], software["byteLength"]
    assert METADATA_WAV[start:start + length] == b"Lavf58.34.101\0"

    assert lists[1]["listType"] == "adtl"
    label, text = lists[1]["subChunks"]
    assert (label["id"], label["cuePointId"], label["text"]) == ("labl", 1, "marker")
    assert (text["id"], text["purpose"], text["text"]) == ("ltxt", "rgn ", "region")
    assert text["sampleLength"] == 100

    cue = next(row["body"] for row in root["chunks"] if row["id"] == "cue ")
    assert cue["cuePointCount"] == 1
    assert (cue["points"][0]["chunkId"], cue["points"][0]["sampleOffset"]) == ("data", 64)

    sampler = next(row["body"] for row in root["chunks"] if row["id"] == "smpl")
    assert (sampler["samplePeriod"], sampler["midiUnityNote"]) == (22675, 60)
    assert sampler["loopCount"] == 1 and sampler["loops"][0]["end"] == 7

    broadcast = next(row["body"] for row in root["chunks"] if row["id"] == "bext")
    assert broadcast["description"] == "take1" and broadcast["originator"] == "studio"
    assert broadcast["originationDate"] == "2004-11-16"
    assert broadcast["timeReferenceLow"] == 44100 and broadcast["version"] == 1
    assert broadcast["umid"] == BEXT_UMID.hex()
    assert broadcast["codingHistory"] == BEXT_CODING_HISTORY.decode()

    # nothing here was carried instead of decoded
    assert root["coverage"]["typedUnidentified"] == []
    _assert_partition(_ledger(document, "sound/a.wav"), METADATA_WAV)


def test_the_bext_reserved_region_is_proven_zero_rather_than_claimed_decoded():
    closure, _, document, binary = _publish({"sound/a.wav": METADATA_WAV}, "a.wav")
    ranges = _ledger(document, "sound/a.wav")["ranges"]
    reserved = [entry for entry in ranges if entry["owner"].endswith(".body.reserved")]
    assert len(reserved) == 1
    entry = reserved[0]
    assert entry["state"] == "reserved-zero" and entry["length"] == 190
    assert METADATA_WAV[entry["offset"]:entry["offset"] + entry["length"]] == bytes(190)
    # the block is claimed in the parts it has, not as one undifferentiated mapped body
    assert any(row["owner"].endswith(".body.codingHistory") for row in ranges)
    validation.validate_document(document, binary, source_members=closure.members())


def test_a_bext_that_writes_into_its_reserved_region_omits_those_bytes_with_evidence():
    written = bytearray(_bext())
    written[500] = 0x7F
    member = _riff(
        _chunk(b"fmt ", _pcm_format(16)),
        _chunk(b"bext", bytes(written)),
        _chunk(b"data", struct.pack("<2h", 1, 2)),
    )
    closure, model, document, binary = _publish({"sound/a.wav": member}, "a.wav")
    assert [row["role"] for row in model.omissions] == ["bext-reserved"]
    entry = next(
        row for row in _ledger(document, "sound/a.wav")["ranges"]
        if row["owner"].endswith(".body.reserved")
    )
    assert entry["state"] == "omitted-proven"
    assert model.omissions[0]["sha256"] == hashlib.sha256(bytes(written[412:602])).hexdigest()
    validation.validate_document(document, binary, source_members=closure.members())
    _assert_partition(_ledger(document, "sound/a.wav"), member)


def test_a_chunk_written_past_the_envelope_pays_under_the_index_it_publishes_at():
    """The excess of seven shipped members is a well-formed `smpl`, so it publishes as a chunk."""

    inside = _chunk(b"fmt ", _pcm_format(16)) + _chunk(b"data", struct.pack("<2h", 5, 6))
    member = _riff(inside, size=len(b"WAVE") + len(inside), trailer=_chunk(b"smpl", SAMPLER))
    _, _, document, _ = _publish({"sound/a.wav": member}, "a.wav")
    root = _root(document)
    beyond = [row for row in root["chunks"] if row["beyondEnvelope"]]
    assert len(beyond) == 1 and beyond[0]["id"] == "smpl"
    index = beyond[0]["index"]
    owners = {entry["owner"] for entry in _ledger(document, "sound/a.wav")["ranges"]}
    # the owner resolves in the table the reader actually holds
    assert f"riff.chunks[{index}].header" in owners
    assert f"riff.chunks[{index}].body" in owners
    assert not any(owner.startswith("riff.excess.chunks[") for owner in owners)
    _assert_partition(_ledger(document, "sound/a.wav"), member)


def test_a_stale_fact_count_is_an_anomaly_rather_than_a_trusted_frame_count():
    _, model, _, _ = _publish({"sound/a.wav": STALE_FACT_WAV}, "a.wav")
    row = next(entry for entry in model.anomalies if entry["role"] == "fact-samples-mismatch")
    assert row["factSampleLength"] == 999
    assert row["decodedSampleFrames"] == model.codec["durationSamples"] == 52


def test_an_unknown_format_tag_fails_the_unit():
    from elysium_pipeline.formats.sound_glb import SoundDecodeError

    with pytest.raises(SoundDecodeError):
        _publish({"sound/a.wav": UNSUPPORTED_WAV}, "a.wav")


def test_an_empty_member_publishes_a_warned_unit_with_no_payload():
    closure, model, document, binary = _publish({"sound/a.wav": b""}, "a.wav")
    assert binary == b"" and "accessors" not in document
    assert [row["role"] for row in model.omissions] == ["empty-member"]
    summary = validation.validate_document(document, binary,
                                           source_members=closure.members())
    assert summary["payloadBytes"] == 0
    assert validation.warnings_for(summary) == ["omitted: empty-member"]


# --------------------------------------------------------------------------- MPEG decode


def test_every_mpeg_byte_is_claimed_exactly_once():
    for member in (PLAIN_MP3, TAGGED_MP3, LEADING_ZERO_MP3, VBR_MP3):
        _, _, document, _ = _publish({"sound/a.mp3": member}, "a.mp3")
        _assert_partition(_ledger(document, "sound/a.mp3"), member)


def test_the_payload_is_the_frame_stream_and_each_frame_locates_itself_in_both():
    _, model, document, binary = _publish({"sound/a.mp3": TAGGED_MP3}, "a.mp3")
    rows = _root(document)["frames"]
    assert len(rows) == 2
    assert _root(document)["payload"]["sampleFormat"] == "mpeg-frames"
    for row in rows:
        source = TAGGED_MP3[row["sourceOffset"]:row["sourceOffset"] + row["length"]]
        payload = binary[row["payloadOffset"]:row["payloadOffset"] + row["length"]]
        assert source == payload
    assert sum(row["length"] for row in rows) == len(binary)
    assert model.codec["durationSamples"] == 2 * 1152


def test_id3_and_frame_headers_are_decoded_into_tags():
    _, model, _, _ = _publish({"sound/a.mp3": TAGGED_MP3}, "a.mp3")
    assert model.tags["id3v2"]["version"] == "2.3.0"
    assert model.tags["id3v2"]["frames"][0]["id"] == "TIT2"
    assert model.tags["id3v2"]["frames"][0]["text"] == "line191"
    assert model.tags["id3v1"]["title"] == "line"


def test_the_id3v2_padding_is_claimed_padding_zero_and_id3v1_is_mapped():
    _, _, document, _ = _publish({"sound/a.mp3": TAGGED_MP3}, "a.mp3")
    states = {
        entry["owner"]: (entry["state"], entry["length"])
        for entry in _ledger(document, "sound/a.mp3")["ranges"]
    }
    assert states["mp3.tags.id3v2.padding"][0] == "padding-zero"
    assert states["mp3.tags.id3v1"] == ("mapped", 128)


def test_an_id3v2_frame_body_this_seam_does_not_decode_is_carried_with_its_digest():
    """A `mapped` claim over the whole tag would state a decode of every frame that never ran."""

    _, _, document, _ = _publish({"sound/a.mp3": RICH_TAGGED_MP3}, "a.mp3")
    root = _root(document)
    frames = root["tags"]["id3v2"]["frames"]
    assert [row["id"] for row in frames] == ["TIT2", "TXXX", "PRIV"]

    # the described frame publishes both of its strings; cutting at the first NUL drops the value
    assert frames[0]["text"] == "line191"
    assert frames[1]["description"] == "creation_time" and frames[1]["text"] == "2022-10-25"
    assert "text" not in frames[2] and "description" not in frames[2]

    carried = [
        row for row in root["coverage"]["typedUnidentified"] if row["kind"] == "id3v2-frame"
    ]
    assert [row["id"] for row in carried] == ["PRIV"]
    row = carried[0]
    assert row["sourceOffset"] == frames[2]["sourceOffset"] + 10
    region = RICH_TAGGED_MP3[row["sourceOffset"]:row["sourceOffset"] + row["byteLength"]]
    assert region == PRIV_BODY
    assert hashlib.sha256(region).hexdigest() == row["sha256"]

    # and the range the row names is the one its owner claimed
    owners = {
        entry["owner"]: (entry["offset"], entry["length"])
        for entry in _ledger(document, "sound/a.mp3")["ranges"]
    }
    assert owners[row["owner"]] == (row["sourceOffset"], row["byteLength"])
    assert owners["mp3.tags.id3v2.frames[2]"][1] == 10
    _assert_partition(_ledger(document, "sound/a.mp3"), RICH_TAGGED_MP3)


def test_the_validator_refuses_an_id3v2_frame_that_is_neither_decoded_nor_carried():
    closure, _, document, binary = _publish({"sound/a.mp3": RICH_TAGGED_MP3}, "a.mp3")
    root = _root(document)
    root["coverage"]["typedUnidentified"] = [
        row for row in root["coverage"]["typedUnidentified"] if row["kind"] != "id3v2-frame"
    ]
    with pytest.raises(validation.SoundGlbValidationError):
        validation.validate_document(document, binary, source_members=closure.members())


def test_the_validator_refuses_a_typed_row_that_does_not_digest_the_bytes_it_names():
    closure, _, document, binary = _publish({"sound/a.wav": UNKNOWN_CHUNK_WAV}, "a.wav")
    root = _root(document)
    root["coverage"]["typedUnidentified"][0]["sha256"] = hashlib.sha256(b"other").hexdigest()
    with pytest.raises(validation.SoundGlbValidationError):
        validation.validate_document(document, binary, source_members=closure.members())


def test_leading_zero_bytes_before_the_first_sync_are_a_named_anomaly():
    _, model, document, _ = _publish({"sound/a.mp3": LEADING_ZERO_MP3}, "a.mp3")
    row = next(entry for entry in model.anomalies if entry["role"] == "leading-zero-padding")
    assert row["byteLength"] == 16
    leading = next(
        entry for entry in _ledger(document, "sound/a.mp3")["ranges"]
        if entry["owner"] == "mp3.leading"
    )
    assert leading["state"] == "padding-zero"


def test_a_frame_whose_crc_does_not_verify_is_recorded_and_still_carried():
    _, model, document, binary = _publish({"sound/a.mp3": CRC_BROKEN_MP3}, "a.mp3")
    assert [row["role"] for row in model.anomalies] == ["crc-mismatch"]
    assert _root(document)["frames"][0]["crcValid"] is False
    assert _root(document)["frames"][1]["crcValid"] is True
    assert len(binary) == len(CRC_BROKEN_MP3)


def test_bytes_that_are_neither_a_frame_nor_a_tag_are_omitted_with_their_digest():
    _, model, document, binary = _publish({"sound/a.mp3": TRAILING_MP3}, "a.mp3")
    row = next(entry for entry in model.omissions if entry["role"] == "non-frame-bytes")
    assert row["byteLength"] == 3
    assert row["sha256"] == hashlib.sha256(b"\x01\x02\x03").hexdigest()
    trailing = next(
        entry for entry in _ledger(document, "sound/a.mp3")["ranges"]
        if entry["owner"] == "mp3.trailing"
    )
    assert trailing["state"] == "omitted-proven"
    assert {"owner": "mp3.trailing", "reason": "non-frame-bytes"} in \
        _root(document)["coverage"]["omittedProven"]
    assert len(binary) == len(TRAILING_MP3) - 3
    _assert_partition(_ledger(document, "sound/a.mp3"), TRAILING_MP3)


def test_the_verbatim_frame_stream_is_admitted_only_because_frames_describe_it():
    closure, _, document, binary = _publish({"sound/a.mp3": PLAIN_MP3}, "a.mp3")
    # An untagged member is exactly its own frame sequence, so the payload is the member's bytes.
    assert binary == PLAIN_MP3
    validation.validate_document(document, binary, source_members=closure.members())
    _root(document)["payload"]["sampleFormat"] = "int16-interleaved"
    with pytest.raises(validation.SoundGlbValidationError):
        validation.validate_document(document, binary, source_members=closure.members())


def test_a_bitrate_change_within_one_stream_is_recorded():
    _, model, _, _ = _publish({"sound/a.mp3": VBR_MP3}, "a.mp3")
    row = next(entry for entry in model.anomalies if entry["role"] == "bitrate-change")
    assert row["bitrates"] == [64, 128]
    assert model.codec["variableBitrate"] is True


# --------------------------------------------------------------------------- companion


def test_every_lip_byte_is_claimed_exactly_once_including_a_trailing_nul():
    for companion in (LIP, LIP_WITH_NUL):
        _, _, document, _ = _publish(
            {"sound/a.wav": PCM16_WAV, "sound/a.lip": companion}, "a.wav"
        )
        _assert_partition(_ledger(document, "sound/a.lip"), companion)
    _, model, document, _ = _publish(
        {"sound/a.wav": PCM16_WAV, "sound/a.lip": LIP_WITH_NUL}, "a.wav"
    )
    nul = next(
        entry for entry in _ledger(document, "sound/a.lip")["ranges"]
        if entry["owner"] == "lip.trailing"
    )
    assert nul["state"] == "padding-zero"
    assert [row["role"] for row in model.omissions] == ["lip-trailing-nul"]


def test_the_lip_words_phonemes_and_caption_are_decoded():
    _, model, document, _ = _publish(
        {"sound/a.wav": PCM16_WAV, "sound/a.lip": LIP}, "a.wav"
    )
    lip = _root(document)["lip"]
    assert lip["version"] == "1.2"
    assert lip["plaintext"] == "Hello there"
    assert [word["text"] for word in lip["words"]] == ["Hello", "Come on"]
    phonemes = lip["words"][0]["phonemes"]
    assert [row["code"] for row in phonemes] == [104, 603]
    assert phonemes[0]["flag"] == 0 and phonemes[1]["flag"] is None
    assert lip["closeCaption"]["language"] == "english"
    assert lip["closeCaption"]["phrases"][0]["text"] == "Hello\r\nthere"
    assert lip["options"] == {"voiceDuck": 1, "speakerName": "Jack"}
    assert model.lip.words[0].source_offset > 0


def test_a_unicode_caption_is_read_as_utf16_through_its_byte_count():
    _, _, document, _ = _publish(
        {"sound/a.wav": PCM16_WAV, "sound/a.lip": LIP_UNICODE}, "a.wav"
    )
    lip = _root(document)["lip"]
    assert lip["version"] == "1.1"
    phrase = lip["closeCaption"]["phrases"][0]
    assert phrase["kind"] == "unicode"
    assert phrase["count"] == len(_WIDE)
    assert phrase["text"] == "Ja bin"
    assert (phrase["start"], phrase["end"]) == (0.0, 1.5)
    _assert_partition(_ledger(document, "sound/a.lip"), LIP_UNICODE)


def test_an_unrecognised_lip_region_is_carried_with_the_bytes_it_covers():
    members = {"sound/a.wav": PCM16_WAV, "sound/a.lip": LIP_UNKNOWN}
    closure, _, document, binary = _publish(members, "a.wav")
    typed = {row["id"]: row for row in _root(document)["coverage"]["typedUnidentified"]}

    section = typed["NOISES"]
    region = LIP_UNKNOWN[section["sourceOffset"]:section["sourceOffset"] + section["byteLength"]]
    assert region.startswith(b"NOISES") and b"breath 0.100" in region
    assert hashlib.sha256(region).hexdigest() == section["sha256"]

    stray = typed["}"]
    assert stray["kind"] == "lip-line" and stray["text"] == "}"
    assert stray["owner"] == "lip.unclaimed"
    assert hashlib.sha256(b"}").hexdigest() == stray["sha256"]

    _assert_partition(_ledger(document, "sound/a.lip"), LIP_UNKNOWN)
    validation.validate_document(document, binary, source_members=closure.members())


def test_a_word_row_carrying_a_real_space_is_a_named_anomaly():
    _, model, _, _ = _publish({"sound/a.wav": PCM16_WAV, "sound/a.lip": LIP}, "a.wav")
    row = next(entry for entry in model.anomalies if entry["role"] == "malformed-word-row")
    assert row["line"] == "WORD Come on 0.320 0.400"
    assert row["sourceOffset"] == LIP.index(b"WORD Come on")


def test_a_lip_beside_a_dual_spelling_stem_is_a_member_of_both_units():
    members = {"sound/a.wav": PCM16_WAV, "sound/a.mp3": PLAIN_MP3, "sound/a.lip": LIP}
    for key in ("a.wav", "a.mp3"):
        closure, _, document, _ = _publish(members, key)
        assert [member.role for member in closure.members()][1] == "lip"
        roles = [row["role"] for row in _root(document)["sourceResolution"]["members"]]
        assert roles[1] == "lip"


# --------------------------------------------------------------------------- references


def test_a_wav_shadowed_by_an_mp3_declares_the_pair_it_loses_to():
    members = {"sound/a.wav": PCM16_WAV, "sound/a.mp3": PLAIN_MP3}
    _, _, document, _ = _publish(members, "a.wav")
    root = _root(document)
    assert root["resolution"] == {"rule": "mp3-first", "shadowedBy": "vtmb:sound:a.mp3"}
    assert root["dependencies"] == [{
        "role": "shadowing-sound", "asset": "vtmb:sound:a.mp3",
        "sourcePath": "sound/a.mp3", "resolved": True, "resolution": "mp3-first",
    }]


def test_an_mp3_that_shadows_a_wav_declares_the_pair_it_hides():
    members = {"sound/a.wav": PCM16_WAV, "sound/a.mp3": PLAIN_MP3}
    _, _, document, _ = _publish(members, "a.mp3")
    root = _root(document)
    assert root["resolution"] == {"rule": "mp3-first", "shadows": "vtmb:sound:a.wav"}
    assert [row["role"] for row in root["dependencies"]] == ["shadowed-sound"]


def test_a_lone_member_declares_no_pair_and_no_dependency():
    _, _, document, _ = _publish({"sound/a.wav": PCM16_WAV}, "a.wav")
    assert _root(document)["resolution"] is None
    assert _root(document)["dependencies"] == []


# --------------------------------------------------------------------------- validation


def test_the_validator_refuses_a_tampered_ledger():
    closure, _, document, binary = _publish({"sound/a.wav": ADPCM_WAV}, "a.wav")
    row = _ledger(document, "sound/a.wav")
    row["ranges"][1]["length"] += 1
    with pytest.raises(validation.SoundGlbValidationError):
        validation.validate_document(document, binary, source_members=closure.members())


def test_the_validator_refuses_a_frame_table_that_does_not_partition_the_payload():
    closure, _, document, binary = _publish({"sound/a.mp3": PLAIN_MP3}, "a.mp3")
    _root(document)["frames"][1]["payloadOffset"] += 1
    with pytest.raises(validation.SoundGlbValidationError):
        validation.validate_document(document, binary, source_members=closure.members())


def test_the_validator_refuses_a_declared_scene():
    closure, _, document, binary = _publish({"sound/a.wav": PCM16_WAV}, "a.wav")
    document["scenes"] = [{"nodes": []}]
    with pytest.raises(validation.SoundGlbValidationError):
        validation.validate_document(document, binary, source_members=closure.members())


def test_the_validator_refuses_a_payload_that_disagrees_with_the_member():
    closure, _, document, binary = _publish({"sound/a.wav": ADPCM_WAV}, "a.wav")
    forged = bytearray(binary)
    forged[0] ^= 0xFF
    _root(document)["payload"]["sha256"] = hashlib.sha256(bytes(forged)).hexdigest()
    with pytest.raises(validation.SoundGlbValidationError):
        validation.validate_document(document, bytes(forged),
                                     source_members=closure.members())


def test_an_opaque_blob_with_one_fabricated_frame_row_is_not_admitted_as_a_frame_stream():
    """The mp3 exemption from the no-opaque-mirror rule is earned by the bytes, not by sums."""

    _, _, document, _ = _publish({"sound/a.mp3": PLAIN_MP3}, "a.mp3")
    root = _root(document)
    blob = bytes(range(256)) * 4
    root["payload"]["byteLength"] = len(blob)
    root["payload"]["sha256"] = hashlib.sha256(blob).hexdigest()
    document["accessors"][0]["count"] = len(blob)
    document["bufferViews"][0]["byteLength"] = len(blob)
    document["buffers"][0]["byteLength"] = len(blob)
    # one row whose arithmetic partitions the payload perfectly and whose bytes are not a frame
    root["frames"] = [dict(root["frames"][0], index=0, sourceOffset=0, length=len(blob),
                           payloadOffset=0)]
    with pytest.raises(validation.SoundGlbValidationError):
        validation.validate_document(document, blob)


def test_the_validator_refuses_a_caption_that_disagrees_with_the_lip():
    members = {"sound/a.wav": PCM16_WAV, "sound/a.lip": LIP}
    closure, _, document, binary = _publish(members, "a.wav")
    _root(document)["lip"]["closeCaption"]["phrases"][0]["text"] = "Goodbye"
    with pytest.raises(validation.SoundGlbValidationError):
        validation.validate_document(document, binary, source_members=closure.members())


def test_the_validator_refuses_a_phoneme_timing_that_disagrees_with_the_lip():
    members = {"sound/a.wav": PCM16_WAV, "sound/a.lip": LIP}
    closure, _, document, binary = _publish(members, "a.wav")
    _root(document)["lip"]["words"][0]["phonemes"][0]["end"] = 0.25
    with pytest.raises(validation.SoundGlbValidationError):
        validation.validate_document(document, binary, source_members=closure.members())


def test_the_validator_refuses_a_frame_header_field_that_disagrees_with_the_member():
    closure, _, document, binary = _publish({"sound/a.mp3": PLAIN_MP3}, "a.mp3")
    _root(document)["frames"][1]["mode"] = "stereo"
    with pytest.raises(validation.SoundGlbValidationError):
        validation.validate_document(document, binary, source_members=closure.members())


def test_export_time_validation_re_reads_the_members_rather_than_trusting_the_decode(tmp_path):
    members = {"sound/a.wav": PCM16_WAV}
    reads: list[str] = []

    def reader(_index, key):
        reads.append(key)
        return PCM8_WAV if len(reads) > 1 else members[key]

    with pytest.raises(validation.SoundGlbValidationError):
        sound_glb.export(_index(members), "a.wav", tmp_path, read_bytes=reader)
    assert len(reads) > 1, "the export never re-read the member it validated"
    assert not list(tmp_path.rglob("*.glb")), "a failing unit was written anyway"


def test_a_wave_unit_never_mirrors_its_member_into_the_product():
    closure, _, document, binary = _publish({"sound/a.wav": ADPCM_WAV}, "a.wav")
    assert ADPCM_WAV not in binary
    validation.validate_document(document, binary, source_members=closure.members())


def test_a_unit_the_corpus_index_back_filled_still_validates(tmp_path):
    """`identity.referencedBy` is empty at export and written in place by the corpus index;
    validation reads a published unit in both states."""

    from elysium_pipeline.formats.corpus_index_glb import backfill

    members = {"sound/a.wav": PCM16_WAV}
    destination = sound_glb.export(_index(members), "a.wav", tmp_path,
                                   read_bytes=_reader(members))
    rows = [{"from": "vtmb:scene:talk", "role": "sound"}]
    assert backfill.rewrite(destination, "vtmb:sound:a.wav", rows) is not None
    validation.validate(destination)
    with pytest.raises(validation.SoundGlbValidationError):
        backfill.rewrite(destination, "vtmb:sound:a.wav", [{"from": "vtmb:scene:talk"}])
        validation.validate(destination)


@pytest.mark.parametrize("key,members", [
    ("a.wav", {"sound/a.wav": ADPCM_WAV, "sound/a.lip": LIP}),
    ("a.wav", {"sound/a.wav": TRAILER_WAV}),
    ("a.wav", {"sound/a.wav": UNKNOWN_CHUNK_WAV}),
    ("a.wav", {"sound/a.wav": METADATA_WAV}),
    ("a.wav", {"sound/a.wav": b""}),
    ("a.mp3", {"sound/a.mp3": TAGGED_MP3, "sound/a.lip": LIP_WITH_NUL}),
    ("a.mp3", {"sound/a.mp3": RICH_TAGGED_MP3}),
    ("a.mp3", {"sound/a.mp3": PLAIN_MP3, "sound/a.wav": PCM16_WAV}),
])
def test_a_written_unit_round_trips_through_standalone_validation(tmp_path, key, members):
    index = _index(members)
    destination = sound_glb.export(index, key, tmp_path, read_bytes=_reader(members))
    assert destination == tmp_path / (key + ".glb")
    summary = validation.validate(destination)
    assert summary["asset"] == "vtmb:sound:" + key
    assert summary["unresolved"] == 0 and summary["unsupported"] == 0
    assert summary["sourceBytes"] == summary["accountedBytes"]
    assert all(percent == 100.0 for percent in summary["byteCoveragePercent"])
