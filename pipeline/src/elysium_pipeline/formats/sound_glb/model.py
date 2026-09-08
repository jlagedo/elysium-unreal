"""Semantic records for the isolated Sound GLB exporter.

One VtMB audio member below `sound/` -- a `.wav` or an `.mp3` -- is one unit, together with the
same-stem `.lip` phoneme document when the install ships one. The key keeps the extension,
because seven stems ship as both spellings and the two are distinct members with distinct bytes.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import PurePosixPath
from typing import Any

from elysium_pipeline.formats.unit_contract import coverage as contract_coverage
from elysium_pipeline.formats.unit_contract import references as contract_references

#: The kind this seam publishes, and the extension every unit of it declares.
KIND = "sound"
SOUND_EXTENSION = contract_coverage.extension_name(KIND)
#: 1.1.0 added the source capsule: the audio member's exact bytes and, when the install ships
#: one, the `.lip` companion's, travel in the unit's BIN chunk after the decoded payload,
#: hash-checked against `sourceResolution`.
#: The payload is a *decode* -- interleaved PCM for a `.wav`, the bare frame stream for an
#: `.mp3` -- so before 1.1.0 no sound unit carried the file the install actually holds.
SCHEMA_VERSION = "1.1.0"

#: The install directory the key is relative to.
SOUND_ROOT = "sound/"

#: The two spellings that select a unit. The key keeps whichever one it was resolved as.
AUDIO_EXTENSIONS = (".wav", ".mp3")

#: The companion the audio stem may ship beside it.
COMPANION_EXTENSION = ".lip"

#: What `payload.sampleFormat` says the BIN chunk holds.
SAMPLE_FORMATS = ("int16-interleaved", "mpeg-frames")

#: The containers a unit is decoded from.
CONTAINERS = ("riff-wave", "mpeg-audio")

#: The roles a source member fills. The audio member selects the unit; the `.lip` is optional and
#: resolves UP-first on its own, so a patched caption can sit beside a retail waveform.
MEMBER_ROLES = ("wav", "mp3", "lip")

#: The dependency roles a sound unit can produce. Both come from the engine's mp3-first join:
#: `speak` and `PlayDialogFile` play `<stem>.mp3` when it exists, so a `.wav` whose stem also
#: resolves as `.mp3` names the member retail plays instead, and the `.mp3` names the one it hides.
DEPENDENCY_ROLES = ("shadowing-sound", "shadowed-sound")

#: The RIFF chunk ids this seam decodes into `chunks[].body`.
DECODED_CHUNKS = ("fmt ", "data", "fact", "cue ", "smpl", "LIST", "bext")

#: Sound Forge session, region, overview and metadata chunks, and the Windows clipboard `DISP`.
#: Their bytes are claimed by the chunk that owns them and their identity is carried in
#: `coverage.typedUnidentified`; decoding one later is a schema addition, not a ledger change.
UNIDENTIFIED_CHUNKS = ("ovwf", "regn", "minf", "umid", "elm1", "elmo", "_PMX", "DISP")

#: The named departures from the format this seam is allowed to record.
ANOMALY_ROLES = (
    "stale-riff-length",
    "riff-envelope-excess",
    "odd-chunk-unpadded",
    "fact-samples-mismatch",
    "leading-zero-padding",
    "crc-mismatch",
    "bitrate-change",
    "stream-parameter-change",
    "malformed-word-row",
)

#: The evidence-backed omissions a unit can carry.
OMISSION_ROLES = (
    "empty-member",
    "sound-forge-trailer",
    "riff-envelope-excess",
    "undecodable-block-tail",
    "non-frame-bytes",
    "id3v2-padding",
    "bext-reserved",
    "lip-trailing-nul",
)


class SoundKeyError(ValueError):
    """A key does not name an audio member below `sound/`."""


def normalize_key(key: str) -> str:
    """The unit key: the path below `sound/`, folded, forward-slashed, extension kept.

    The command surface tolerates the root prefix, so `sound/x.wav` and `x.wav` name one unit.
    """

    normalized = str(key).replace("\\", "/").strip().strip('"').lower().lstrip("/")
    while "//" in normalized:
        normalized = normalized.replace("//", "/")
    if normalized.startswith(SOUND_ROOT):
        normalized = normalized[len(SOUND_ROOT):]
    if not normalized or normalized.startswith("../") or "/../" in normalized:
        raise SoundKeyError(f"invalid sound key {key!r}")
    if not normalized.endswith(AUDIO_EXTENSIONS):
        raise SoundKeyError(f"sound key {key!r} names no {' or '.join(AUDIO_EXTENSIONS)} member")
    return normalized


def key_extension(key: str) -> str:
    """`.wav` or `.mp3` -- the spelling the key kept."""

    return normalize_key(key)[-4:]


def key_stem(key: str) -> str:
    """The key without its audio extension; what the `.lip` companion is named after."""

    return normalize_key(key)[:-4]


def counterpart_key(key: str) -> str:
    """The other spelling of one stem, which the mp3-first rule joins this unit to."""

    normalized = normalize_key(key)
    return normalized[:-4] + (".mp3" if normalized.endswith(".wav") else ".wav")


def asset_id(key: str) -> str:
    return contract_references.asset_id(KIND, normalize_key(key))


def source_path(key: str) -> str:
    """The install-relative path of the audio member."""

    return SOUND_ROOT + normalize_key(key)


def companion_path(key: str) -> str:
    """The install-relative path of the same-stem `.lip`."""

    return SOUND_ROOT + key_stem(key) + COMPANION_EXTENSION


def output_relative_path(key: str) -> PurePosixPath:
    """Where the unit publishes below the seam's family root (`sounds/`)."""

    return PurePosixPath(normalize_key(key) + ".glb")


@dataclass(frozen=True, slots=True)
class RiffChunk:
    """One RIFF chunk exactly where the member wrote it.

    `offset` is the chunk header's own file offset -- published as `sourceOffset` -- so a ledger
    range and the record it pays for name the same place. `body` is None where the id is one this
    seam carries rather than decodes; the carried body's own offset is what keys its
    `coverage.typedUnidentified` row.
    """

    index: int
    id: str
    offset: int
    length: int
    padded: bool
    beyond_envelope: bool
    body: dict[str, Any] | None


@dataclass(frozen=True, slots=True)
class MpegFrame:
    """One MPEG-1 Layer III frame: where it sits in the member, and where in the payload."""

    index: int
    offset: int
    length: int
    payload_offset: int
    version: str
    layer: int
    bitrate: int
    sample_rate: int
    padding: bool
    private: bool
    mode: str
    mode_extension: int
    copyright: bool
    original: bool
    emphasis: str
    crc_present: bool
    crc_valid: bool | None
    main_data_begin: int


@dataclass(frozen=True, slots=True)
class LipPhoneme:
    """One phoneme row.

    `code` is the key -- the code point the expression table's class column carries -- and `text`
    is authoring residue that names a different row under the same code across the corpus.
    """

    code: int
    text: str
    start: float
    end: float
    volume: float
    flag: int | None
    source_offset: int


@dataclass(frozen=True, slots=True)
class LipWord:
    text: str
    start: float
    end: float
    phonemes: tuple[LipPhoneme, ...]
    source_offset: int
    malformed: bool


@dataclass(frozen=True, slots=True)
class LipPhrase:
    kind: str
    count: int
    text: str
    start: float
    end: float
    source_offset: int


@dataclass(frozen=True, slots=True)
class LipDocument:
    version: str
    plaintext: str
    words: tuple[LipWord, ...]
    emphasis: tuple[str, ...]
    close_caption: dict[str, Any] | None
    options: dict[str, Any]


@dataclass(slots=True)
class SoundModel:
    """Everything one sound unit publishes, decoded from the members the closure resolved.

    `claims` is the decoder's byte accountability, one `(role, offset, length, state, owner)` per
    range; `coverage.build` turns it into the published ledger through the shared `ByteLedger`.
    """

    key: str
    asset_id: str
    members: list[Any]
    payload: bytes
    sample_format: str
    codec: dict[str, Any]
    chunks: list[RiffChunk]
    frames: list[MpegFrame]
    tags: dict[str, Any]
    lip: LipDocument | None
    resolution: dict[str, Any] | None
    dependencies: list[dict[str, Any]]
    anomalies: list[dict[str, Any]]
    omissions: list[dict[str, Any]]
    typed_unidentified: list[dict[str, Any]]
    omitted_proven: list[dict[str, Any]]
    claims: list[tuple[str, int, int, str, str]] = field(default_factory=list)
    unresolved: list[dict[str, Any]] = field(default_factory=list)
    unsupported: list[dict[str, Any]] = field(default_factory=list)
