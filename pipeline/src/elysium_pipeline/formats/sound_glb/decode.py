"""One sound identity, decoded from the members its closure resolved.

A `.wav` decodes to interleaved 16-bit PCM: the MS-ADPCM blocks are `derived`, 16-bit PCM is
copied and 8-bit PCM is widened. An `.mp3` decodes to its own frame stream, because MP3 decoding
is not bit-exact across decoders and the bitstream is the authored datum. Every byte either member
holds is claimed exactly once, and every range the source stores as zero is proven to be zero
before it is labelled one.
"""

from __future__ import annotations

from array import array
import hashlib
import sys
from typing import Any

from elysium_pipeline.formats.sound_glb import adpcm, lip as lip_lexer, mpeg, riff
from elysium_pipeline.formats.sound_glb.model import (
    DECODED_CHUNKS,
    LipDocument,
    MpegFrame,
    RiffChunk,
    SoundModel,
    asset_id,
    key_extension,
    source_path,
)
from elysium_pipeline.formats.unit_contract.references import dependency


class SoundDecodeError(ValueError):
    """The member does not decode into the unit this seam publishes."""


def _digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _seconds(samples: int, rate: int) -> float:
    return round(samples / rate, 6) if rate else 0.0


#: The `chunks[].body` decoder for each RIFF id this seam reads. `data` is decoded in the walk
#: itself, because it also produces the payload and its own ledger claims.
CHUNK_DECODERS = {
    "fmt ": lambda body, raw: riff.decode_format(body),
    "fact": lambda body, raw: riff.decode_fact(body),
    "cue ": lambda body, raw: riff.decode_cue(body),
    "smpl": lambda body, raw: riff.decode_smpl(body),
    "LIST": lambda body, raw: riff.decode_list(body, raw.body_offset),
    "bext": lambda body, raw: riff.decode_bext(body),
}
assert set(CHUNK_DECODERS) | {"data"} == set(DECODED_CHUNKS)


def _pcm_bytes(samples: array) -> bytes:
    """Little-endian int16, whatever the host's byte order is."""

    if sys.byteorder == "big":
        samples = array("h", samples)
        samples.byteswap()
    return samples.tobytes()


# --------------------------------------------------------------------------- WAVE


def _decode_wave(model_path: str, data: bytes, claims: list, anomalies: list,
                 omissions: list, typed: list) -> tuple[bytes, str, dict[str, Any],
                                                        list[RiffChunk]]:
    walk = riff.walk(data)
    claims.append((model_path, 0, riff.HEADER_BYTES, "mapped", "riff.header"))
    if riff.SIZE_FIELD_END + walk.declared_size != len(data):
        anomalies.append({
            "role": "stale-riff-length",
            "declaredSize": walk.declared_size,
            "envelopeEnd": riff.SIZE_FIELD_END + walk.declared_size,
            "memberLength": len(data),
        })
    for offset in walk.unpadded:
        anomalies.append({
            "role": "odd-chunk-unpadded",
            "sourceOffset": offset,
            "id": bytes(data[offset:offset + 4]).decode("latin-1"),
        })

    raw_chunks = list(walk.chunks)
    excess_chunks = riff.walk_excess(data, walk.excess_offset) if \
        walk.excess_offset < len(data) else ()
    tail_length = len(data) - walk.excess_offset
    if tail_length:
        tail = bytes(data[walk.excess_offset:])
        anomalies.append({
            "role": "riff-envelope-excess",
            "sourceOffset": walk.excess_offset,
            "byteLength": tail_length,
            "sha256": _digest(tail),
        })
        if not excess_chunks:
            claims.append(
                (model_path, walk.excess_offset, tail_length, "omitted-proven", "riff.excess")
            )
            omissions.append({
                "role": "sound-forge-trailer" if tail.endswith(b"W3DI")
                else "riff-envelope-excess",
                "sourceOffset": walk.excess_offset,
                "byteLength": tail_length,
                "sha256": _digest(tail),
            })

    format_body: dict[str, Any] | None = None
    for raw in raw_chunks:
        if raw.id == b"fmt ":
            format_body = riff.decode_format(bytes(data[raw.body_offset:
                                                        raw.body_offset + raw.length]))
            break
    if format_body is None:
        raise SoundDecodeError("the member declares no fmt chunk")
    tag = int(format_body["formatTag"])
    channels = int(format_body["channels"])
    if tag not in (riff.FORMAT_PCM, riff.FORMAT_MS_ADPCM):
        raise SoundDecodeError(f"unsupported WAVE format tag {tag}")
    if channels not in (1, 2):
        raise SoundDecodeError(f"a WAVE member carries 1 or 2 channels, not {channels}")

    payload = b""
    frames = 0
    blocks = 0
    fact_samples: int | None = None
    chunks: list[RiffChunk] = []
    for raw in list(raw_chunks) + list(excess_chunks):
        # The owner is the path to the record the range paid for, and every chunk -- inside the
        # envelope or past it -- is published in one flat `chunks[]`, so the owner names the
        # index the reader will find it at. `beyondEnvelope` is what distinguishes the two.
        prefix = f"riff.chunks[{len(chunks)}]"
        identifier = raw.id.decode("latin-1")
        claims.append((model_path, raw.offset, riff.CHUNK_HEADER_BYTES, "mapped",
                       prefix + ".header"))
        body = bytes(data[raw.body_offset:raw.body_offset + raw.length])
        decoded: dict[str, Any] | None = None
        state = "mapped"
        if identifier == "data" and not raw.beyond_envelope and not payload:
            samples, frames, blocks, consumed = _decode_samples(format_body, body)
            payload = _pcm_bytes(samples)
            state = "mapped" if (tag == riff.FORMAT_PCM
                                 and int(format_body["bitsPerSample"]) == 16) else "derived"
            if consumed < raw.length:
                # A tail too short to carry a block preamble decodes to no sample; it is claimed
                # as its own range with the evidence rather than folded into the decoded body.
                claims.append((model_path, raw.body_offset, consumed, state, prefix + ".body"))
                claims.append((model_path, raw.body_offset + consumed, raw.length - consumed,
                               "omitted-proven", prefix + ".body.tail"))
                omissions.append({
                    "role": "undecodable-block-tail",
                    "sourceOffset": raw.body_offset + consumed,
                    "byteLength": raw.length - consumed,
                    "sha256": _digest(body[consumed:]),
                })
            else:
                claims.append((model_path, raw.body_offset, raw.length, state, prefix + ".body"))
            decoded = {
                "sampleFrames": frames,
                "blocks": blocks,
                "payloadRange": {"offset": 0, "length": len(payload)},
            }
            chunks.append(RiffChunk(len(chunks), identifier, raw.offset, raw.length,
                                    raw.pad, raw.beyond_envelope, decoded))
            if raw.pad:
                claims.append((model_path, raw.body_offset + raw.length, 1, "padding-zero",
                               prefix + ".pad"))
            continue
        decoder = CHUNK_DECODERS.get(identifier)
        if decoder is not None:
            decoded = decoder(body, raw)
            if identifier == "fact":
                fact_samples = int(decoded["sampleLength"])
        else:
            # A chunk this seam does not decode is carried, not dropped: its bytes stay claimed
            # by the range below and its identity survives in `coverage.typedUnidentified`, so
            # decoding it later is a schema addition rather than a ledger change.
            typed.append({
                "kind": "riff-chunk",
                "id": identifier,
                "owner": prefix + ".body",
                # the body's own offset, so `[sourceOffset, sourceOffset + byteLength)` is
                # exactly the range the digest covers and the range the owner claimed
                "sourceOffset": raw.body_offset,
                "byteLength": raw.length,
                "sha256": _digest(body),
            })
        if identifier == "bext" and decoded is not None:
            _claim_bext_body(model_path, data, raw, prefix, claims, omissions)
        else:
            claims.append((model_path, raw.body_offset, raw.length, state, prefix + ".body"))
        if raw.pad:
            claims.append((model_path, raw.body_offset + raw.length, 1, "padding-zero",
                           prefix + ".pad"))
        chunks.append(RiffChunk(len(chunks), identifier, raw.offset, raw.length, raw.pad,
                                raw.beyond_envelope, decoded))

    if not payload and not any(chunk.id == "data" for chunk in chunks):
        raise SoundDecodeError("the member declares no data chunk")
    if fact_samples is not None and fact_samples != frames:
        anomalies.append({
            "role": "fact-samples-mismatch",
            "factSampleLength": fact_samples,
            "decodedSampleFrames": frames,
        })
    rate = int(format_body["sampleRate"])
    codec: dict[str, Any] = {
        "container": "riff-wave",
        "tag": tag,
        "formatName": format_body["formatName"],
        "channels": channels,
        "sampleRate": rate,
        "bitsPerSample": int(format_body["bitsPerSample"]),
        "byteRate": int(format_body["byteRate"]),
        "blockAlign": int(format_body["blockAlign"]),
        "samplesPerBlock": format_body.get("samplesPerBlock"),
        "coefficients": format_body.get("coefficients"),
        "durationSamples": frames,
        "durationSeconds": _seconds(frames, rate),
        "variableBitrate": False,
    }
    return payload, "int16-interleaved", codec, chunks


def _claim_bext_body(model_path: str, data: bytes, raw, prefix: str, claims: list,
                     omissions: list) -> None:
    """Claim a `bext` body in the three parts the block actually has.

    No field represents the 190 bytes between the UMID and the coding history, so folding them
    into one `mapped` body range would assert a decode that did not happen. The region is the
    specification's reserved run and the source stores it as zero; a member that writes into it
    publishes those bytes as an evidence-backed omission instead.
    """

    start = raw.body_offset
    reserved = start + riff.BEXT_RESERVED_START
    history = start + riff.BEXT_CODING_HISTORY
    claims.append((model_path, start, riff.BEXT_RESERVED_START, "mapped", prefix + ".body"))
    region = bytes(data[reserved:history])
    if any(region):
        claims.append((model_path, reserved, len(region), "omitted-proven",
                       prefix + ".body.reserved"))
        omissions.append({
            "role": "bext-reserved",
            "sourceOffset": reserved,
            "byteLength": len(region),
            "sha256": _digest(region),
        })
    else:
        claims.append((model_path, reserved, len(region), "reserved-zero",
                       prefix + ".body.reserved"))
    tail = raw.length - riff.BEXT_CODING_HISTORY
    if tail:
        claims.append((model_path, history, tail, "mapped", prefix + ".body.codingHistory"))


def _decode_samples(format_body: dict[str, Any], body: bytes) -> tuple[array, int, int, int]:
    """The `data` body as interleaved int16, with the frames, blocks and bytes it accounts for."""

    tag = int(format_body["formatTag"])
    channels = int(format_body["channels"])
    bits = int(format_body["bitsPerSample"])
    if tag == riff.FORMAT_MS_ADPCM:
        coefficients = format_body.get("coefficients") or [
            list(pair) for pair in adpcm.STANDARD_COEFFICIENTS
        ]
        return adpcm.decode(body, channels, int(format_body["blockAlign"]), coefficients)
    if bits == 16:
        usable = len(body) - len(body) % (2 * channels)
        samples = array("h")
        samples.frombytes(body[:usable])
        if sys.byteorder == "big":
            samples.byteswap()
        return samples, usable // (2 * channels), 0, usable
    if bits == 8:
        usable = len(body) - len(body) % channels
        samples = array("h", ((byte - 128) << 8 for byte in body[:usable]))
        return samples, usable // channels, 0, usable
    raise SoundDecodeError(f"unsupported PCM sample width {bits}")


# --------------------------------------------------------------------------- MPEG


def _claim_id3v2(model_path: str, data: bytes, tag: dict[str, Any],
                 extent: tuple[int, int], claims: list, omissions: list, typed: list) -> None:
    """Claim an ID3v2 block one frame at a time, and carry the bodies this seam does not decode.

    One `mapped` claim over the whole block would state that every frame's content reached the
    unit; the corpus writes `PRIV` and `GEOB` frames whose bodies no published field represents.
    Their bytes stay claimed by the frame that owns them and their identity survives in
    `coverage.typedUnidentified`, keyed by frame id and by the offset the digest covers.
    """

    offset, total = extent
    end = offset + total
    padding_offset = min(int(tag["padding"]["sourceOffset"]), end)
    rows = list(tag["frames"])
    # the tag header and, where the member wrote one, the extended header
    head_end = int(rows[0]["sourceOffset"]) if rows else padding_offset
    claims.append((model_path, offset, head_end - offset, "mapped", "mp3.tags.id3v2"))
    for index, row in enumerate(rows):
        start = int(row["sourceOffset"])
        length = int(row["byteLength"])
        owner = f"mp3.tags.id3v2.frames[{index}]"
        claims.append((model_path, start, mpeg.ID3V2_HEADER_BYTES, "mapped", owner))
        if not length:
            continue
        body_offset = start + mpeg.ID3V2_HEADER_BYTES
        claims.append((model_path, body_offset, length, "mapped", owner + ".body"))
        if mpeg.decodes_id3v2_frame(str(row["id"])):
            continue
        typed.append({
            "kind": "id3v2-frame",
            "id": str(row["id"]),
            "owner": owner + ".body",
            "sourceOffset": body_offset,
            "byteLength": length,
            "sha256": _digest(bytes(data[body_offset:body_offset + length])),
        })
    padding_length = end - padding_offset
    if not padding_length:
        return
    region = bytes(data[padding_offset:padding_offset + padding_length])
    if any(region):
        claims.append((model_path, padding_offset, padding_length, "omitted-proven",
                       "mp3.tags.id3v2.padding"))
        omissions.append({
            "role": "id3v2-padding",
            "sourceOffset": padding_offset,
            "byteLength": padding_length,
            "sha256": _digest(region),
        })
    else:
        claims.append((model_path, padding_offset, padding_length, "padding-zero",
                       "mp3.tags.id3v2.padding"))


def _decode_mpeg(model_path: str, data: bytes, claims: list, anomalies: list,
                 omissions: list, typed: list) -> tuple[bytes, str, dict[str, Any],
                                                        list[MpegFrame], dict[str, Any]]:
    walk = mpeg.walk(data)
    if not walk.frames:
        raise SoundDecodeError("the member carries no MPEG audio frame")

    lead_offset, lead_length = walk.leading
    if lead_length:
        region = bytes(data[lead_offset:lead_offset + lead_length])
        if any(region):
            claims.append((model_path, lead_offset, lead_length, "omitted-proven", "mp3.leading"))
            omissions.append({
                "role": "non-frame-bytes",
                "sourceOffset": lead_offset,
                "byteLength": lead_length,
                "sha256": _digest(region),
            })
        else:
            claims.append((model_path, lead_offset, lead_length, "padding-zero", "mp3.leading"))
            anomalies.append({
                "role": "leading-zero-padding",
                "sourceOffset": lead_offset,
                "byteLength": lead_length,
            })

    if walk.id3v2_range is not None and walk.id3v2 is not None:
        _claim_id3v2(model_path, data, walk.id3v2, walk.id3v2_range, claims, omissions, typed)

    pieces: list[bytes] = []
    frames: list[MpegFrame] = []
    payload_offset = 0
    for raw in walk.frames:
        claims.append((model_path, raw.offset, raw.length, "mapped", f"mp3.frames[{raw.index}]"))
        pieces.append(bytes(data[raw.offset:raw.offset + raw.length]))
        frames.append(MpegFrame(
            index=raw.index,
            offset=raw.offset,
            length=raw.length,
            payload_offset=payload_offset,
            version=mpeg.VERSION_NAMES[raw.version],
            layer=raw.layer,
            bitrate=raw.bitrate,
            sample_rate=raw.sample_rate,
            padding=raw.padding,
            private=raw.private,
            mode=mpeg.MODE_NAMES[raw.mode],
            mode_extension=raw.mode_extension,
            copyright=raw.copyright,
            original=raw.original,
            emphasis=mpeg.EMPHASIS_NAMES[raw.emphasis],
            crc_present=raw.crc_present,
            crc_valid=raw.crc_valid,
            main_data_begin=raw.main_data_begin,
        ))
        payload_offset += raw.length
        if raw.crc_present and raw.crc_valid is False:
            anomalies.append({
                "role": "crc-mismatch",
                "frame": raw.index,
                "sourceOffset": raw.offset,
                "declared": raw.crc_declared,
            })

    if walk.id3v1_offset is not None:
        claims.append((model_path, walk.id3v1_offset, mpeg.ID3V1_BYTES, "mapped",
                       "mp3.tags.id3v1"))
    trail_offset, trail_length = walk.trailing
    if trail_length:
        region = bytes(data[trail_offset:trail_offset + trail_length])
        claims.append((model_path, trail_offset, trail_length, "omitted-proven", "mp3.trailing"))
        omissions.append({
            "role": "non-frame-bytes",
            "sourceOffset": trail_offset,
            "byteLength": trail_length,
            "sha256": _digest(region),
        })

    bitrates = sorted({frame.bitrate for frame in frames})
    if len(bitrates) > 1:
        anomalies.append({"role": "bitrate-change", "bitrates": bitrates})
    parameters = {(frame.sample_rate, frame.mode) for frame in frames}
    if len(parameters) > 1:
        anomalies.append({
            "role": "stream-parameter-change",
            "parameters": sorted(f"{rate}/{mode}" for rate, mode in parameters),
        })

    first = walk.frames[0]
    xing = mpeg.decode_xing(data, first)
    lame = xing.pop("lame", None) if xing else None
    tags = {
        "id3v2": walk.id3v2,
        "id3v1": walk.id3v1,
        "xing": xing if xing and xing["tag"] == "Xing" else None,
        "info": xing if xing and xing["tag"] == "Info" else None,
        "lame": lame,
    }
    samples = sum(frame.samples for frame in walk.frames)
    codec = {
        "container": "mpeg-audio",
        "tag": f"{mpeg.VERSION_NAMES[first.version]}-layer-3",
        "channels": first.channels,
        "sampleRate": first.sample_rate,
        "bitrate": first.bitrate,
        "mode": mpeg.MODE_NAMES[first.mode],
        "durationSamples": samples,
        "durationSeconds": _seconds(samples, first.sample_rate),
        "variableBitrate": len(bitrates) > 1,
    }
    return b"".join(pieces), "mpeg-frames", codec, frames, tags


# --------------------------------------------------------------------------- LIP


def _typed_lip_region(data: bytes, identifier: str, owner: str, offset: int,
                      length: int) -> dict[str, Any]:
    """A `.lip` region this seam recognises by name but does not decode, carried with its bytes."""

    return {
        "kind": "lip-section",
        "id": identifier,
        "owner": owner,
        "sourceOffset": offset,
        "byteLength": length,
        "sha256": _digest(bytes(data[offset:offset + length])),
    }


def _decode_lip(member, claims: list, anomalies: list, omissions: list,
                typed: list) -> LipDocument:
    data = member.data
    text = lip_lexer.decode_text(data)
    lines = lip_lexer.lines_of(text)
    covered_content = [False] * len(lines)
    covered_terminator = [False] * len(lines)

    version = ""
    plaintext = ""
    words: list = []
    emphasis: tuple[str, ...] = ()
    close_caption: dict[str, Any] | None = None
    options: dict[str, Any] = {"voiceDuck": None, "speakerName": None}

    def claim_span(start: int, end: int, owner: str) -> None:
        first, last = lines[start], lines[end]
        claims.append((member.path, first.offset, last.end - first.offset, "mapped-text", owner))
        for index in range(start, end + 1):
            covered_content[index] = True
            if index < end:
                covered_terminator[index] = True

    def claim_line(index: int, owner: str) -> None:
        line = lines[index]
        if line.length:
            claims.append((member.path, line.offset, line.length, "mapped-text", owner))
        covered_content[index] = True

    for name, header, opening, closing in lip_lexer.section_extents(lines):
        if opening < 0:
            if name == "VERSION":
                claim_line(header, "lip.version")
                tokens = lines[header].text.split()
                version = tokens[1] if len(tokens) > 1 else ""
            elif not lines[header].text.strip("\0"):
                # Seven members end with a stray NUL past the last line terminator.
                line = lines[header]
                claims.append((member.path, line.offset, line.length, "padding-zero",
                               "lip.trailing"))
                covered_content[header] = True
                omissions.append({
                    "role": "lip-trailing-nul",
                    "sourceOffset": line.offset,
                    "byteLength": line.length,
                })
            else:
                claim_line(header, f"lip.sections[{name}]")
                typed.append(_typed_lip_region(data, name, f"lip.sections[{name}]",
                                               lines[header].offset, lines[header].length))
            continue
        if name == "PLAINTEXT":
            claim_span(header, closing, "lip.plaintext")
            plaintext = "\n".join(lines[index].text for index in range(opening + 1, closing))
        elif name == "WORDS":
            claim_line(header, "lip.words")
            claim_line(opening, "lip.words")
            claim_line(closing, "lip.words")
            words, word_anomalies, owners = lip_lexer.parse_words(lines, opening, closing)
            anomalies.extend(word_anomalies)
            for index, owner in owners:
                claim_line(index, owner)
        elif name == "EMPHASIS":
            claim_span(header, closing, "lip.emphasis")
            emphasis = tuple(
                lines[index].text.strip()
                for index in range(opening + 1, closing)
                if lines[index].text.strip()
            )
        elif name == "CLOSECAPTION":
            claim_span(header, closing, "lip.closeCaption")
            language = ""
            for index in range(opening + 1, closing):
                stripped = lines[index].text.strip()
                if stripped and stripped not in ("{", "}"):
                    language = stripped
                    break
            body = data[lines[header].offset:lines[closing].end]
            close_caption = {
                "language": language,
                "phrases": lip_lexer.parse_phrases(body, lines[header].offset),
            }
        elif name == "OPTIONS":
            claim_span(header, closing, "lip.options")
            options = lip_lexer.parse_options(lines, opening, closing)
        else:
            claim_span(header, closing, f"lip.sections[{name}]")
            typed.append(_typed_lip_region(
                data, name, f"lip.sections[{name}]", lines[header].offset,
                lines[closing].end - lines[header].offset,
            ))

    for index, line in enumerate(lines):
        if not covered_content[index] and line.length:
            content = line.text
            if not content.strip():
                claims.append((member.path, line.offset, line.length, "mapped-text",
                               "lip.whitespace"))
            elif not content.strip("\0"):
                claims.append((member.path, line.offset, line.length, "padding-zero",
                               "lip.trailing"))
                omissions.append({
                    "role": "lip-trailing-nul",
                    "sourceOffset": line.offset,
                    "byteLength": line.length,
                })
            else:
                claims.append((member.path, line.offset, line.length, "mapped-text",
                               "lip.unclaimed"))
                row = _typed_lip_region(data, content.strip().split()[0], "lip.unclaimed",
                                        line.offset, line.length)
                row["kind"] = "lip-line"
                # the line's own text, so a `mapped-text` claim over it is a claim over
                # something the unit actually carries rather than over a name and a length
                row["text"] = content
                typed.append(row)
        if not covered_terminator[index] and line.terminator:
            claims.append((member.path, line.end, line.terminator, "mapped-text",
                           "lip.whitespace"))

    return LipDocument(
        version=version,
        plaintext=plaintext,
        words=tuple(words),
        emphasis=emphasis,
        close_caption=close_caption,
        options=options,
    )


# --------------------------------------------------------------------------- unit


def decode_sound(closure) -> SoundModel:
    """Decode one sound unit from its resolved members."""

    key = closure.key
    audio = closure.audio
    claims: list[tuple[str, int, int, str, str]] = []
    anomalies: list[dict[str, Any]] = []
    omissions: list[dict[str, Any]] = []
    typed: list[dict[str, Any]] = []

    payload = b""
    chunks: list[RiffChunk] = []
    frames: list[MpegFrame] = []
    tags: dict[str, Any] = {"id3v2": None, "id3v1": None, "xing": None, "info": None,
                            "lame": None}
    extension = key_extension(key)
    container = "riff-wave" if extension == ".wav" else "mpeg-audio"
    sample_format = "int16-interleaved" if extension == ".wav" else "mpeg-frames"
    codec: dict[str, Any] = {"container": container}

    if not audio.data:
        # An empty member publishes with a warning, a zero-length payload and no chunk or frame
        # table; there is nothing to walk and nothing to claim.
        omissions.append({"role": "empty-member", "sourcePath": audio.path, "byteLength": 0})
    elif extension == ".wav":
        payload, sample_format, codec, chunks = _decode_wave(
            audio.path, audio.data, claims, anomalies, omissions, typed
        )
    else:
        payload, sample_format, codec, frames, tags = _decode_mpeg(
            audio.path, audio.data, claims, anomalies, omissions, typed
        )

    lip_document = None
    if closure.lip is not None:
        if closure.lip.data:
            lip_document = _decode_lip(closure.lip, claims, anomalies, omissions, typed)
        else:
            omissions.append({
                "role": "empty-member", "sourcePath": closure.lip.path, "byteLength": 0,
            })

    resolution = None
    dependencies: list[dict[str, Any]] = []
    if closure.counterpart:
        other = asset_id(closure.counterpart)
        shadowed = extension == ".wav"
        resolution = {
            "rule": "mp3-first",
            ("shadowedBy" if shadowed else "shadows"): other,
        }
        dependencies.append(dependency(
            "shadowing-sound" if shadowed else "shadowed-sound",
            other,
            source_path(closure.counterpart),
            True,
            resolution="mp3-first",
        ))

    omitted_proven = [
        {"owner": owner, "reason": role}
        for owner, role in _omission_owners(claims, omissions)
    ]
    return SoundModel(
        key=key,
        asset_id=closure.asset_id,
        members=list(closure.members()),
        payload=payload,
        sample_format=sample_format,
        codec=codec,
        chunks=chunks,
        frames=frames,
        tags=tags,
        lip=lip_document,
        resolution=resolution,
        dependencies=dependencies,
        anomalies=anomalies,
        omissions=omissions,
        typed_unidentified=typed,
        omitted_proven=omitted_proven,
        claims=claims,
    )


def _omission_owners(claims: list, omissions: list) -> list[tuple[str, str]]:
    """Pair every `omitted-proven` range with the omission row that carries its evidence.

    The evidence itself lives once, in `omissions[]`; `coverage.omittedProven` names the owner and
    the reason so a reader can walk from a graded byte range to the proof without restating it.
    """

    by_offset = {int(row.get("sourceOffset", -1)): str(row.get("role", "")) for row in omissions}
    rows: list[tuple[str, str]] = []
    for _path, offset, _length, state, owner in claims:
        if state == "omitted-proven":
            rows.append((owner, by_offset.get(offset, "omitted")))
    for row in omissions:
        if row.get("role") == "empty-member":
            rows.append(("member", "empty-member"))
    return rows
