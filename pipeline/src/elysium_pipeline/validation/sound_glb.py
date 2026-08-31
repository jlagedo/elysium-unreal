"""Independent structural validator for Sound GLB products.

The validator never imports the writer. It re-walks the RIFF chunks or the MPEG frame sequence
from the source members, re-decodes the ADPCM blocks and compares every sample against the
payload, re-checks every field of each frame's header along with its length and CRC, re-parses
the `.lip` and compares every word, phoneme, caption phrase and option, and re-checks the ledger
against the source bytes. With no members present it still verifies the scene-less core, the
accessor's extent and digest, and that `frames[]` is a real MPEG frame stream partitioning the
payload rather than an arithmetic claim over an opaque blob.
"""

from __future__ import annotations

import hashlib
from pathlib import Path
from typing import Any, Mapping

from elysium_pipeline.formats.sound_glb import adpcm, lip, mpeg, riff
from elysium_pipeline.formats.sound_glb.riff import CHUNK_HEADER_BYTES
from elysium_pipeline.formats.sound_glb.model import (
    ANOMALY_ROLES,
    AUDIO_EXTENSIONS,
    CONTAINERS,
    DEPENDENCY_ROLES,
    MEMBER_ROLES,
    OMISSION_ROLES,
    SAMPLE_FORMATS,
    SCHEMA_VERSION,
    SOUND_EXTENSION,
    SOUND_ROOT,
    UNIDENTIFIED_CHUNKS,
)
from elysium_pipeline.formats.unit_contract import container, validate as contract_validate

__all__ = [
    "SoundGlbValidationError",
    "read_glb",
    "validate",
    "validate_document",
    "warnings_for",
]

ASSET_PREFIX = "vtmb:sound:"

#: The component type each sample format publishes through.
COMPONENT_TYPES = {"int16-interleaved": 5122, "mpeg-frames": 5121}


class SoundGlbValidationError(ValueError):
    """A published sound unit contradicts the seam."""


def read_glb(path: Path) -> tuple[dict[str, Any], bytes]:
    try:
        return container.read_glb(Path(path))
    except container.GlbContainerError as error:
        raise SoundGlbValidationError(str(error)) from error


def _fail(message: str) -> None:
    raise SoundGlbValidationError(message)


def _contract(call, *args, **kwargs):
    try:
        return call(*args, **kwargs)
    except contract_validate.UnitValidationError as error:
        raise SoundGlbValidationError(str(error)) from error


def _frames_partition_payload(root: Mapping[str, Any], binary: bytes) -> bool:
    """True only when every declared frame is an MPEG frame the payload bytes actually hold.

    An untagged `.mp3` payload is byte-for-byte its own member, so `frames[]` is the whole of
    what says the BIN chunk is a described frame stream rather than an undescribed blob. It is
    re-parsed out of the chunk here: arithmetic alone would let one fabricated row claim the
    whole payload and carry anything through as "the frame stream".
    """

    declared = int(root.get("payload", {}).get("byteLength", -1))
    if declared < 0 or declared > len(binary):
        return False
    rows = list(root.get("frames") or [])
    if not rows:
        return False
    view = memoryview(binary)[:declared]
    cursor = 0
    for index, row in enumerate(rows):
        if int(row.get("payloadOffset", -1)) != cursor:
            return False
        frame = mpeg.parse_frame_header(view, cursor, index)
        if frame is None or frame.length != int(row.get("length", 0)):
            return False
        cursor += frame.length
    return cursor == declared


def _check_identity(root: Mapping[str, Any]) -> tuple[str, str]:
    identity = root.get("identity") or {}
    asset = str(identity.get("asset", ""))
    key = str(identity.get("key", ""))
    if not key or not key.endswith(AUDIO_EXTENSIONS):
        _fail(f"the unit key {key!r} keeps no audio extension")
    if key != key.lower().replace("\\", "/"):
        _fail(f"the unit key {key!r} is not the joinable spelling")
    if asset != ASSET_PREFIX + key:
        _fail(f"identity {asset!r} disagrees with key {key!r}")
    # The seam publishes the field empty; the corpus index rewrites it in place with one
    # `{from, role}` row per referring unit, and a published unit is read back in both states.
    try:
        contract_validate.validate_index_rows(
            identity.get("referencedBy"), "identity.referencedBy"
        )
    except contract_validate.UnitValidationError as error:
        _fail(str(error))
    return asset, key


def _check_members(root: Mapping[str, Any], key: str) -> list[Mapping[str, Any]]:
    members = list((root.get("sourceResolution") or {}).get("members") or [])
    if not members:
        _fail("a sound unit is decoded from at least its audio member")
    audio = members[0]
    role = str(audio.get("role"))
    if role not in ("wav", "mp3") or not key.endswith("." + role):
        _fail(f"the selecting member's role {role!r} disagrees with the key")
    if str(audio.get("path")) != SOUND_ROOT + key:
        _fail("the selecting member is not this unit's audio member")
    seen = set()
    for member in members:
        member_role = str(member.get("role"))
        if member_role not in MEMBER_ROLES:
            _fail(f"unknown source member role {member_role!r}")
        if member_role in seen:
            _fail(f"two source members fill the {member_role!r} role")
        seen.add(member_role)
    if len(members) > 1 and str(members[1].get("role")) != "lip":
        _fail("the second source member of a sound unit is its .lip companion")
    return members


def _check_payload(root: Mapping[str, Any], document: Mapping[str, Any], binary: bytes) -> None:
    payload = root.get("payload")
    if not isinstance(payload, Mapping):
        _fail("the unit publishes no payload record")
    sample_format = str(payload.get("sampleFormat"))
    if sample_format not in SAMPLE_FORMATS:
        _fail(f"unknown payload sample format {sample_format!r}")
    length = int(payload.get("byteLength", -1))
    if length < 0:
        _fail("the payload declares no byte length")
    accessors = list(document.get("accessors") or [])
    if not length:
        if accessors or payload.get("accessor") is not None:
            _fail("an empty payload declares no accessor")
        if binary:
            _fail("an empty payload publishes no BIN chunk")
        return
    if payload.get("accessor") != 0 or len(accessors) != 1:
        _fail("a sound unit publishes exactly one accessor for its payload")
    accessor = accessors[0]
    if accessor.get("componentType") != COMPONENT_TYPES[sample_format]:
        _fail(f"the accessor does not carry {sample_format}")
    if accessor.get("type") != "SCALAR":
        _fail("the payload accessor is SCALAR")
    width = 2 if sample_format == "int16-interleaved" else 1
    if int(accessor.get("count", -1)) * width != length:
        _fail("the accessor count disagrees with the payload length")
    if length > len(binary):
        _fail("the payload runs past the BIN chunk")
    if hashlib.sha256(binary[:length]).hexdigest() != str(payload.get("sha256")):
        _fail("the payload digest disagrees with the BIN chunk")


def _check_codec(root: Mapping[str, Any], key: str) -> Mapping[str, Any]:
    codec = root.get("codec")
    if not isinstance(codec, Mapping):
        _fail("the unit publishes no codec record")
    if str(codec.get("container")) not in CONTAINERS:
        _fail(f"unknown codec container {codec.get('container')!r}")
    expected = "riff-wave" if key.endswith(".wav") else "mpeg-audio"
    if str(codec.get("container")) != expected:
        _fail(f"a {key[-4:]} member is a {expected} container")
    return codec


def _check_tables(root: Mapping[str, Any], key: str) -> None:
    chunks = root.get("chunks")
    frames = root.get("frames")
    if not isinstance(chunks, list) or not isinstance(frames, list):
        _fail("a sound unit publishes both a chunk and a frame table")
    if key.endswith(".wav") and frames:
        _fail("a RIFF member declares no MPEG frame")
    if key.endswith(".mp3") and chunks:
        _fail("an MPEG member declares no RIFF chunk")
    carried = {
        (str(row.get("id")), int(row.get("sourceOffset", -1)))
        for row in (root.get("coverage") or {}).get("typedUnidentified") or []
    }
    previous = -1
    for index, row in enumerate(chunks):
        if row.get("index") != index:
            _fail(f"chunk {index} is not in source order")
        offset = int(row.get("sourceOffset", -1))
        if offset <= previous:
            _fail(f"chunk {index} does not follow the chunk before it")
        previous = offset
        # a carried chunk is keyed by the offset of the bytes its digest covers: the body
        if row.get("body") is None and \
                (str(row.get("id")), offset + CHUNK_HEADER_BYTES) not in carried:
            _fail(f"chunk {index} ({row.get('id')!r}) is neither decoded nor carried")
        if str(row.get("id")) in UNIDENTIFIED_CHUNKS and row.get("body") is not None:
            _fail(f"chunk {index} ({row.get('id')!r}) is carried, not decoded")
    for index, row in enumerate(frames):
        if row.get("index") != index:
            _fail(f"frame {index} is not in source order")
        if int(row.get("length", 0)) <= 0:
            _fail(f"frame {index} claims no bytes")


def _check_typed_unidentified(root: Mapping[str, Any], members: Mapping[str, bytes]) -> None:
    seen = set()
    for row in (root.get("coverage") or {}).get("typedUnidentified") or []:
        if not isinstance(row, Mapping):
            _fail("a typedUnidentified row is not a record")
        identifier = (str(row.get("id")), int(row.get("sourceOffset", -1)))
        if identifier[1] < 0:
            _fail("a typedUnidentified row keeps no source offset")
        if identifier in seen:
            _fail(f"typedUnidentified row {identifier} is declared twice")
        seen.add(identifier)
        if not str(row.get("owner") or ""):
            _fail(f"typedUnidentified row {identifier} names no ledger owner")
        digest = str(row.get("sha256") or "")
        if not digest:
            _fail(f"typedUnidentified row {identifier} carries no digest of its bytes")
        length = int(row.get("byteLength", -1))
        if length < 0:
            _fail(f"typedUnidentified row {identifier} claims no byte length")
        # the row's own offset and length name a real range, and the digest is that range's
        for data in members.values():
            region = data[identifier[1]:identifier[1] + length]
            if len(region) == length and hashlib.sha256(region).hexdigest() == digest:
                break
        else:
            if members:
                _fail(f"typedUnidentified row {identifier} does not digest the bytes it names")


def _check_tags(root: Mapping[str, Any]) -> None:
    """Every ID3v2 frame body is either published or carried; none is silently dropped."""

    tags = root.get("tags") or {}
    id3v2 = tags.get("id3v2")
    if not isinstance(id3v2, Mapping):
        return
    carried = {
        (str(row.get("id")), int(row.get("sourceOffset", -1)))
        for row in (root.get("coverage") or {}).get("typedUnidentified") or []
    }
    for row in id3v2.get("frames") or []:
        identifier = str(row.get("id"))
        length = int(row.get("byteLength", 0))
        if not length:
            continue
        published = any(field in row for field in ("text", "description", "language"))
        body = int(row.get("sourceOffset", -1)) + mpeg.ID3V2_HEADER_BYTES
        if published:
            continue
        if (identifier, body) not in carried:
            _fail(f"ID3v2 frame {identifier!r} is neither decoded nor carried")


def _check_references(root: Mapping[str, Any], asset: str) -> None:
    dependencies = list(root.get("dependencies") or [])
    resolution = root.get("resolution")
    named = {str(row.get("asset")) for row in dependencies}
    for row in dependencies:
        role = str(row.get("role"))
        if role not in DEPENDENCY_ROLES:
            _fail(f"unknown dependency role {role!r}")
        if str(row.get("asset")) == asset:
            _fail("a sound unit does not depend on itself")
    if resolution is None:
        if dependencies:
            _fail("a dependency names a pair the unit does not record")
        return
    if not isinstance(resolution, Mapping) or resolution.get("rule") != "mp3-first":
        _fail("the only resolution rule a sound unit records is mp3-first")
    partner = str(resolution.get("shadowedBy") or resolution.get("shadows") or "")
    if not partner.startswith(ASSET_PREFIX):
        _fail("the mp3-first pair names no stable sound identity")
    if partner not in named:
        _fail("the mp3-first pair produced no dependency row")
    if len(dependencies) != 1:
        _fail("a sound unit's only reference is its mp3-first pair")


def _check_records(root: Mapping[str, Any]) -> None:
    for row in root.get("anomalies") or []:
        if not isinstance(row, Mapping) or str(row.get("role")) not in ANOMALY_ROLES:
            _fail(f"unknown source anomaly {row!r}")
    for row in root.get("omissions") or []:
        if not isinstance(row, Mapping) or str(row.get("role")) not in OMISSION_ROLES:
            _fail(f"unknown source omission {row!r}")
    coverage = root.get("coverage") or {}
    if coverage.get("unresolved") or coverage.get("unsupported"):
        _fail("the sound unit is incomplete")


# ----------------------------------------------------------------- independent re-decode


def _redecode_wave(root: Mapping[str, Any], binary: bytes, data: bytes) -> None:
    walk = riff.walk(data)
    published = list(root.get("chunks") or [])
    inside = [row for row in published if not row.get("beyondEnvelope")]
    if len(inside) != len(walk.chunks):
        _fail(f"the chunk table states {len(inside)} chunks; the member holds {len(walk.chunks)}")
    for row, raw in zip(inside, walk.chunks):
        if str(row.get("id")) != raw.id.decode("latin-1"):
            _fail(f"chunk at {raw.offset} is {raw.id!r}, not {row.get('id')!r}")
        if int(row.get("sourceOffset")) != raw.offset or int(row.get("length")) != raw.length:
            _fail(f"chunk {row.get('id')!r} is not where the member wrote it")
        if bool(row.get("padded")) != raw.pad:
            _fail(f"chunk {row.get('id')!r} disagrees about its pad byte")
    format_chunk = next((raw for raw in walk.chunks if raw.id == b"fmt "), None)
    data_chunk = next((raw for raw in walk.chunks if raw.id == b"data"), None)
    if format_chunk is None or data_chunk is None:
        _fail("the member declares no fmt or no data chunk")
    format_body = riff.decode_format(
        bytes(data[format_chunk.body_offset:format_chunk.body_offset + format_chunk.length])
    )
    codec = root.get("codec") or {}
    for source_key, published_key in (
        ("formatTag", "tag"), ("channels", "channels"), ("sampleRate", "sampleRate"),
        ("bitsPerSample", "bitsPerSample"), ("blockAlign", "blockAlign"),
    ):
        if format_body[source_key] != codec.get(published_key):
            _fail(f"codec.{published_key} disagrees with the fmt chunk")
    body = bytes(data[data_chunk.body_offset:data_chunk.body_offset + data_chunk.length])
    channels = int(format_body["channels"])
    tag = int(format_body["formatTag"])
    if tag == riff.FORMAT_MS_ADPCM:
        coefficients = format_body.get("coefficients") or [
            list(pair) for pair in adpcm.STANDARD_COEFFICIENTS
        ]
        samples, frames, _blocks, _consumed = adpcm.decode(
            body, channels, int(format_body["blockAlign"]), coefficients
        )
        expected = samples.tobytes()
    elif int(format_body["bitsPerSample"]) == 16:
        usable = len(body) - len(body) % (2 * channels)
        expected = body[:usable]
        frames = usable // (2 * channels)
    else:
        usable = len(body) - len(body) % channels
        expected = b"".join(
            int((byte - 128) << 8).to_bytes(2, "little", signed=True) for byte in body[:usable]
        )
        frames = usable // channels
    length = int(root.get("payload", {}).get("byteLength", -1))
    if expected != binary[:length]:
        _fail("the payload disagrees with the samples the member decodes to")
    if int(codec.get("durationSamples", -1)) != frames:
        _fail("codec.durationSamples disagrees with the decoded frame count")


def _redecode_mpeg(root: Mapping[str, Any], binary: bytes, data: bytes) -> None:
    walk = mpeg.walk(data)
    published = list(root.get("frames") or [])
    if len(published) != len(walk.frames):
        _fail(f"the frame table states {len(published)} frames; the member holds "
              f"{len(walk.frames)}")
    cursor = 0
    for row, raw in zip(published, walk.frames):
        if int(row.get("sourceOffset")) != raw.offset or int(row.get("length")) != raw.length:
            _fail(f"frame {raw.index} is not where the member wrote it")
        expected = {
            "version": mpeg.VERSION_NAMES[raw.version],
            "layer": raw.layer,
            "bitrate": raw.bitrate,
            "sampleRate": raw.sample_rate,
            "padding": raw.padding,
            "private": raw.private,
            "mode": mpeg.MODE_NAMES[raw.mode],
            "modeExtension": raw.mode_extension,
            "copyright": raw.copyright,
            "original": raw.original,
            "emphasis": mpeg.EMPHASIS_NAMES[raw.emphasis],
            "mainDataBegin": raw.main_data_begin,
        }
        for field, value in expected.items():
            if row.get(field) != value:
                _fail(f"frame {raw.index} disagrees with its header at {field}")
        if bool(row.get("crcPresent")) != raw.crc_present or row.get("crcValid") != raw.crc_valid:
            _fail(f"frame {raw.index} disagrees about its CRC")
        if int(row.get("payloadOffset")) != cursor:
            _fail(f"frame {raw.index} is not where the payload holds it")
        if binary[cursor:cursor + raw.length] != data[raw.offset:raw.offset + raw.length]:
            _fail(f"frame {raw.index} is not the member's bytes")
        cursor += raw.length
    if cursor != int(root.get("payload", {}).get("byteLength", -1)):
        _fail("the frame stream does not fill the payload")


def _sections(lines) -> tuple[dict[str, tuple[int, int, int]], str]:
    """The braced sections by name, last one winning, and the version line's token.

    Last-wins is what the decode does: it assigns each section as it walks, so a member that
    writes a name twice publishes the second. Comparing against the same rule keeps the re-parse
    a check of the decode rather than of a different reading of the grammar.
    """

    braced: dict[str, tuple[int, int, int]] = {}
    version = ""
    for name, header, opening, closing in lip.section_extents(lines):
        if opening >= 0:
            braced[name] = (header, opening, closing)
        elif name == "VERSION":
            tokens = lines[header].text.split()
            version = tokens[1] if len(tokens) > 1 else ""
    return braced, version


def _same_number(published: Any, decoded: float) -> bool:
    return isinstance(published, (int, float)) and float(published) == float(decoded)


def _redecode_lip_words(published: Mapping[str, Any], lines, opening: int, closing: int) -> None:
    words, _anomalies, _owners = lip.parse_words(lines, opening, closing)
    rows = list(published.get("words") or [])
    if len(rows) != len(words):
        _fail(f"the lip states {len(rows)} words; the member holds {len(words)}")
    for row, word in zip(rows, words):
        if str(row.get("text")) != word.text:
            _fail(f"word at {word.source_offset} disagrees with the member")
        if int(row.get("sourceOffset", -1)) != word.source_offset:
            _fail(f"word {word.text!r} keeps no source offset")
        if not (_same_number(row.get("start"), word.start)
                and _same_number(row.get("end"), word.end)):
            _fail(f"word {word.text!r} disagrees about its timing")
        if bool(row.get("malformed")) != word.malformed:
            _fail(f"word {word.text!r} disagrees about being malformed")
        phonemes = list(row.get("phonemes") or [])
        if len(phonemes) != len(word.phonemes):
            _fail(f"word {word.text!r} states {len(phonemes)} phonemes")
        for entry, phoneme in zip(phonemes, word.phonemes):
            if int(entry.get("code")) != phoneme.code or str(entry.get("text")) != phoneme.text:
                _fail(f"phoneme at {phoneme.source_offset} disagrees with the member")
            if int(entry.get("sourceOffset", -1)) != phoneme.source_offset:
                _fail(f"phoneme at {phoneme.source_offset} keeps no source offset")
            if not (_same_number(entry.get("start"), phoneme.start)
                    and _same_number(entry.get("end"), phoneme.end)
                    and _same_number(entry.get("volume"), phoneme.volume)):
                _fail(f"phoneme at {phoneme.source_offset} disagrees about its timing")
            if entry.get("flag") != phoneme.flag:
                _fail(f"phoneme at {phoneme.source_offset} disagrees about its flag")


def _redecode_lip_caption(published: Mapping[str, Any], data: bytes, lines,
                          extent: tuple[int, int, int] | None) -> None:
    caption = published.get("closeCaption")
    if extent is None:
        if caption is not None:
            _fail("the unit publishes a caption the member declares no section for")
        return
    if caption is None:
        _fail("the member declares a CLOSECAPTION section the unit does not publish")
    header, opening, closing = extent
    language = ""
    for index in range(opening + 1, closing):
        stripped = lines[index].text.strip()
        if stripped and stripped not in ("{", "}"):
            language = stripped
            break
    if str(caption.get("language")) != language:
        _fail("the caption language disagrees with the member")
    body = data[lines[header].offset:lines[closing].end]
    phrases = lip.parse_phrases(body, lines[header].offset)
    rows = list(caption.get("phrases") or [])
    if len(rows) != len(phrases):
        _fail(f"the caption states {len(rows)} phrases; the member holds {len(phrases)}")
    for row, phrase in zip(rows, phrases):
        if str(row.get("kind")) != phrase.kind or int(row.get("count", -1)) != phrase.count:
            _fail(f"phrase at {phrase.source_offset} disagrees with the member")
        if str(row.get("text")) != phrase.text:
            _fail(f"phrase at {phrase.source_offset} does not carry the member's caption")
        if not (_same_number(row.get("start"), phrase.start)
                and _same_number(row.get("end"), phrase.end)):
            _fail(f"phrase at {phrase.source_offset} disagrees about its timing")
        if int(row.get("sourceOffset", -1)) != phrase.source_offset:
            _fail(f"phrase at {phrase.source_offset} keeps no source offset")


def _redecode_lip(root: Mapping[str, Any], data: bytes) -> None:
    published = root.get("lip")
    if published is None:
        _fail("a unit with a .lip member publishes no lip document")
    lines = lip.lines_of(lip.decode_text(data))
    sections, version = _sections(lines)
    if str(published.get("version")) != version:
        _fail("the lip version disagrees with the member")
    if "WORDS" not in sections:
        _fail("the .lip declares no WORDS section")
    _header, opening, closing = sections["WORDS"]
    _redecode_lip_words(published, lines, opening, closing)

    plaintext = ""
    if "PLAINTEXT" in sections:
        _header, opening, closing = sections["PLAINTEXT"]
        plaintext = "\n".join(lines[index].text for index in range(opening + 1, closing))
    if str(published.get("plaintext")) != plaintext:
        _fail("the lip plaintext disagrees with the member")

    emphasis: list[str] = []
    if "EMPHASIS" in sections:
        _header, opening, closing = sections["EMPHASIS"]
        emphasis = [
            lines[index].text.strip()
            for index in range(opening + 1, closing)
            if lines[index].text.strip()
        ]
    if [str(row) for row in published.get("emphasis") or []] != emphasis:
        _fail("the lip emphasis rows disagree with the member")

    _redecode_lip_caption(published, data, lines, sections.get("CLOSECAPTION"))

    options = {"voiceDuck": None, "speakerName": None}
    if "OPTIONS" in sections:
        _header, opening, closing = sections["OPTIONS"]
        options = lip.parse_options(lines, opening, closing)
    if dict(published.get("options") or {}) != options:
        _fail("the lip options disagree with the member")


def validate_document(document: dict, binary: bytes, *, source_members=None) -> dict[str, Any]:
    root = _contract(
        contract_validate.validate_extension_root,
        document,
        SOUND_EXTENSION,
        asset_prefix=ASSET_PREFIX,
        schema_version=SCHEMA_VERSION,
    )
    _contract(contract_validate.validate_sceneless, document)
    _contract(contract_validate.validate_container, document, binary)
    _contract(contract_validate.validate_accessors, document, binary)
    if document.get("asset", {}).get("generator") != "Elysium Sound GLB Exporter":
        _fail("a sound unit names the Sound GLB exporter as its generator")
    asset, key = _check_identity(root)
    members = _check_members(root, key)
    _check_payload(root, document, binary)
    _check_codec(root, key)
    _check_tables(root, key)
    _check_typed_unidentified(
        root, {member.path: member.data for member in source_members or ()}
    )
    _check_tags(root)
    _check_references(root, asset)
    _check_records(root)
    _contract(contract_validate.validate_ledgers, root, source_members)
    _contract(contract_validate.validate_capsules, document, binary, root, source_members)
    partitions = _frames_partition_payload(root, binary)

    if not partitions and key.endswith(".mp3") and binary:
        _fail("the frame table does not partition the payload")

    if source_members is not None:
        by_path = {member.path: member.data for member in source_members}
        audio = by_path.get(SOUND_ROOT + key, b"")
        if audio:
            if key.endswith(".wav"):
                _redecode_wave(root, binary, audio)
            else:
                _redecode_mpeg(root, binary, audio)
        for member in members[1:]:
            companion = by_path.get(str(member.get("path")), b"")
            if companion:
                _redecode_lip(root, companion)

    coverage = root.get("coverage") or {}
    ledgers = list(coverage.get("byteLedger") or [])
    lip_block = root.get("lip") or {}
    # The contract owns both of these: what a unit could not account for, and how that is phrased
    # for an operator. This seam counts only what is its own on top of them.
    counts = contract_validate.completeness(root)
    return {
        "warnings": contract_validate.warnings_for(root),
        "asset": asset,
        "key": key,
        "container": str((root.get("codec") or {}).get("container")),
        "sampleFormat": str((root.get("payload") or {}).get("sampleFormat")),
        "payloadBytes": int((root.get("payload") or {}).get("byteLength", 0)),
        "durationSamples": int((root.get("codec") or {}).get("durationSamples", 0)),
        "chunks": len(root.get("chunks") or []),
        "frames": len(root.get("frames") or []),
        "words": len(lip_block.get("words") or []) if root.get("lip") else 0,
        "hasLip": root.get("lip") is not None,
        "dependencies": len(root.get("dependencies") or []),
        "anomalies": [str(row.get("role")) for row in root.get("anomalies") or []],
        "omissions": [str(row.get("role")) for row in root.get("omissions") or []],
        "typedUnidentified": [
            f"{row.get('id')}@{row.get('sourceOffset')}"
            for row in coverage.get("typedUnidentified") or []
        ],
        "unresolved": counts["unresolved"],
        "unsupported": counts["unsupported"],
        "sourceBytes": sum(int(row.get("byteLength", 0)) for row in ledgers),
        "accountedBytes": sum(int(row.get("accountedBytes", 0)) for row in ledgers),
        "byteCoveragePercent": [float(row.get("coveragePercent", 0.0)) for row in ledgers],
    }


def validate(path: Path) -> dict[str, Any]:
    document, binary = read_glb(Path(path))
    return validate_document(document, binary)


def warnings_for(summary: dict[str, Any]) -> list[str]:
    """What a published unit could not resolve, phrased for the operator.

    The phrasing is the contract's; `validate_document` records it on the summary as the document
    is checked, so a caller holding only the summary still speaks it in the shared wording.
    """

    return list(summary.get("warnings") or [])
