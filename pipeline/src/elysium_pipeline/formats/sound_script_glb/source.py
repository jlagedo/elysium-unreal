"""The six `scripts/*.txt` tables as the install resolves them, cut into per-entry units.

Each table proves it partitions into entry spans, comments and insignificant whitespace, with
nothing left over, before any unit is cut from it -- the same proof `surface_property_glb/source.py`
runs over `surfaceproperties.txt`, run here once per table and once per grammar (the four
KeyValues tables, the positional DSP table, the line-oriented sentence table).
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Callable

from elysium_pipeline.formats.sound_script_glb import dsp_tree, kv_tree, lexer
from elysium_pipeline.formats.sound_script_glb.model import (
    MANIFEST_KEY,
    TABLE_PATHS,
    dsp_preset_asset_id,
    game_sound_asset_id,
    manifest_asset_id,
    normalize_name,
    sentence_asset_id,
    soundscape_asset_id,
)
from elysium_pipeline.formats.unit_contract import Origin, SourceMember, origin_of


class SoundScriptSourceError(RuntimeError):
    """A table is absent, incoherent, or does not partition into entries."""


def _read(index: dict, key: str, read_bytes) -> tuple[bytes, Origin]:
    entry = index.get(key)
    data = read_bytes(index, key) if entry else None
    if data is None:
        raise SoundScriptSourceError(f"missing required table: {key}")
    return data, origin_of(entry)


def _default_read_bytes():
    from elysium_pipeline.formats import install

    return install.read


def _normalize_install_key(path: str) -> str:
    key = str(path).replace("\\", "/").strip().strip('"').lower().lstrip("/")
    while "//" in key:
        key = key.replace("//", "/")
    return key


def sound_reference_exists(index: dict, path: str) -> bool:
    """Whether the install holds a member for a wave path a `wave`/`rndwave` value names.

    A `sound`-role dependency's target lives in the sibling `sound_glb` seam's corpus, below
    `sound/`, not in one of this seam's own tables; this checks the same merged, UP-first index
    every seam reads through (`install.build_index`), keyed the way `install.read` normalizes a
    key, without reading the member's bytes -- existence is all a dependency row needs.
    """

    return index.get("sound/" + _normalize_install_key(path)) is not None


def table_reference_exists(index: dict, path: str) -> bool:
    """Whether the install holds the `scripts/*.txt` table a manifest `precache_file` names."""

    return index.get(_normalize_install_key(path)) is not None


# --- the four KeyValues tables -----------------------------------------------------------------


@dataclass(frozen=True, slots=True)
class KvTable:
    """One parsed KeyValues table: every named block, and the proof its bytes are accounted for."""

    path: str
    data: bytes
    origin: Origin
    spans: dict[str, tuple[str, int, int]]   # folded name -> (source name, offset, end)

    @property
    def names(self) -> tuple[str, ...]:
        return tuple(sorted(self.spans))

    def cut(self, name: str) -> tuple[str, str, bytes, tuple[int, int]]:
        folded = normalize_name(name)
        if folded not in self.spans:
            raise SoundScriptSourceError(f"{self.path} declares no entry {folded!r}")
        source_name, offset, end = self.spans[folded]
        return folded, source_name, self.data[offset:end], (offset, end - offset)


def load_kv_table(
    index: dict, path: str, *, read_bytes: Callable[[dict, str], bytes | None] | None = None
) -> KvTable:
    read_bytes = read_bytes or _default_read_bytes()
    data, origin = _read(index, path, read_bytes)
    text = lexer.decode_text(data)
    try:
        blocks = kv_tree.parse(lexer.tokenize(text))
    except lexer.SoundScriptLexError as error:
        raise SoundScriptSourceError(f"{path}: {error}") from error
    if not blocks:
        raise SoundScriptSourceError(f"{path} declares no entries")

    from elysium_pipeline.formats.unit_contract import ByteLedger

    spans: dict[str, tuple[str, int, int]] = {}
    ledger = ByteLedger(path, data)
    for block in blocks:
        folded = normalize_name(block.name.text)
        if folded in spans:
            raise SoundScriptSourceError(f"{path}: entry {folded!r} is declared twice")
        spans[folded] = (block.name.text, block.offset, block.end)
        ledger.claim(block.offset, block.end - block.offset, "mapped", f"table.entry[{folded}]")
    covered = [(offset, end) for _, offset, end in spans.values()]
    for token in lexer.tokenize(text):
        if any(offset <= token.offset < end for offset, end in covered):
            continue
        if token.kind == "whitespace":
            ledger.claim(token.offset, token.length, "omitted-proven", "table.insignificant-whitespace")
        elif token.kind in ("comment", "bom"):
            ledger.claim(token.offset, token.length, "mapped", f"table.{token.kind}")
        else:
            raise SoundScriptSourceError(f"{path}: byte {token.offset} belongs to no entry")
    ledger.finish()
    return KvTable(path, data, origin, spans)


# --- the DSP preset table ------------------------------------------------------------------------


@dataclass(frozen=True, slots=True)
class DspTable:
    path: str
    data: bytes
    origin: Origin
    spans: dict[int, tuple[int, int]]   # preset id -> (offset, end)

    @property
    def ids(self) -> tuple[int, ...]:
        return tuple(sorted(self.spans))

    def cut(self, preset_id: int) -> tuple[bytes, tuple[int, int]]:
        if preset_id not in self.spans:
            raise SoundScriptSourceError(f"{self.path} declares no preset {preset_id!r}")
        offset, end = self.spans[preset_id]
        return self.data[offset:end], (offset, end - offset)


def load_dsp_table(
    index: dict, *, read_bytes: Callable[[dict, str], bytes | None] | None = None
) -> DspTable:
    read_bytes = read_bytes or _default_read_bytes()
    path = TABLE_PATHS["dsp-preset"]
    data, origin = _read(index, path, read_bytes)
    text = lexer.decode_text(data)
    try:
        blocks, stray = dsp_tree.parse(lexer.tokenize(text))
    except lexer.SoundScriptLexError as error:
        raise SoundScriptSourceError(f"{path}: {error}") from error
    if not blocks:
        raise SoundScriptSourceError(f"{path} declares no presets")

    from elysium_pipeline.formats.unit_contract import ByteLedger

    spans: dict[int, tuple[int, int]] = {}
    ledger = ByteLedger(path, data)
    stray_ranges: list[tuple[int, int]] = []
    for stray_ordinal, token in enumerate(stray):
        ledger.claim(token.offset, token.length, "mapped", f"table.stray-text[{stray_ordinal}]")
        stray_ranges.append((token.offset, token.end))
    for block in blocks:
        tokens = block.tokens()
        if not tokens:
            raise SoundScriptSourceError(f"{path}: byte {block.offset} preset names no id")
        try:
            preset_id = int(tokens[0].text.strip())
        except ValueError as error:
            raise SoundScriptSourceError(
                f"{path}: byte {block.offset} preset id {tokens[0].text!r} is not an integer"
            ) from error
        if preset_id in spans:
            raise SoundScriptSourceError(f"{path}: preset {preset_id} is declared twice")
        spans[preset_id] = (block.offset, block.end)
        ledger.claim(block.offset, block.end - block.offset, "mapped", f"table.preset[{preset_id}]")
    covered = [(offset, end) for offset, end in spans.values()] + stray_ranges
    for token in lexer.tokenize(text):
        if any(offset <= token.offset < end for offset, end in covered):
            continue
        if token.kind == "whitespace":
            ledger.claim(token.offset, token.length, "omitted-proven", "table.insignificant-whitespace")
        elif token.kind in ("comment", "bom"):
            ledger.claim(token.offset, token.length, "mapped", f"table.{token.kind}")
        else:
            raise SoundScriptSourceError(f"{path}: byte {token.offset} belongs to no preset")
    ledger.finish()
    return DspTable(path, data, origin, spans)


# --- the sentence table ---------------------------------------------------------------------------


@dataclass(frozen=True, slots=True)
class SentenceTable:
    path: str
    data: bytes
    origin: Origin
    spans: dict[str, tuple[str, int, int]]   # folded name -> (source name, offset, end)

    @property
    def names(self) -> tuple[str, ...]:
        return tuple(sorted(self.spans))

    def cut(self, name: str) -> tuple[str, str, bytes, tuple[int, int]]:
        folded = normalize_name(name)
        if folded not in self.spans:
            raise SoundScriptSourceError(f"{self.path} declares no sentence {folded!r}")
        source_name, offset, end = self.spans[folded]
        return folded, source_name, self.data[offset:end], (offset, end - offset)


def _line_spans(text: str) -> list[tuple[int, int]]:
    """`(start, end)` of every line's content, `end` excluding the line terminator."""

    spans = []
    start = 0
    for index, char in enumerate(text):
        if char == "\n":
            stop = index
            if stop > start and text[stop - 1] == "\r":
                stop -= 1
            spans.append((start, stop))
            start = index + 1
    if start < len(text):
        spans.append((start, len(text)))
    return spans


def load_sentence_table(
    index: dict, *, read_bytes: Callable[[dict, str], bytes | None] | None = None
) -> SentenceTable:
    read_bytes = read_bytes or _default_read_bytes()
    path = TABLE_PATHS["sentence"]
    data, origin = _read(index, path, read_bytes)
    text = lexer.decode_text(data)

    from elysium_pipeline.formats.unit_contract import ByteLedger

    spans: dict[str, tuple[str, int, int]] = {}
    #: `sentences.txt`'s banner block is the table's comments, the same as the KeyValues and DSP
    #: loaders' own header comments -- claimed `mapped`/`table.comment[i]`, not folded into the
    #: blank-line/terminator whitespace that is genuinely insignificant.
    comment_spans: list[tuple[int, int]] = []
    ledger = ByteLedger(path, data)
    for start, end in _line_spans(text):
        line = text[start:end]
        stripped = line.strip()
        if not stripped:
            continue                                   # blank line: insignificant whitespace
        if stripped.startswith("//"):
            comment_spans.append((start, end))
            continue
        name_end = start
        while name_end < end and text[name_end] not in " \t":
            name_end += 1
        source_name = text[start:name_end]
        folded = normalize_name(source_name)
        if folded in spans:
            raise SoundScriptSourceError(f"{path}: sentence {folded!r} is declared twice")
        spans[folded] = (source_name, start, end)
        ledger.claim(start, end - start, "mapped", f"table.entry[{folded}]")
    for ordinal, (start, end) in enumerate(comment_spans):
        ledger.claim(start, end - start, "mapped", f"table.comment[{ordinal}]")
    covered = [(offset, end) for _, offset, end in spans.values()] + comment_spans
    cursor = 0
    for start, end in sorted(covered):
        if start > cursor:
            ledger.claim(cursor, start - cursor, "omitted-proven", "table.insignificant-whitespace")
        cursor = end
    if cursor < len(data):
        ledger.claim(cursor, len(data) - cursor, "omitted-proven", "table.insignificant-whitespace")
    if not spans:
        raise SoundScriptSourceError(f"{path} declares no sentences")
    ledger.finish()
    return SentenceTable(path, data, origin, spans)


# --- the merged game-sound directory ---------------------------------------------------------------


@dataclass(frozen=True, slots=True)
class GameSoundDirectory:
    """The union of `game_sounds_surfaceproperties.txt` (live) and `sounds.txt` (dormant).

    `game_sounds_manifest.txt` precaches only the live table, so a name the dormant table repeats
    is dead data shadowed by the live entry of the same name: the collision is a real property of
    the shipped corpus (16 names, `Metal_Barrel.Impact` and `Test.Sound` among them), not
    something `seam_map_sound_script.md` resolves, and this seam keeps the live entry's identity
    and records the dormant duplicate as shadowed rather than publishing two units for one asset
    id. See `specDeviations`.
    """

    live: KvTable
    dormant: KvTable
    shadowed: frozenset[str]

    def owner(self, name: str) -> str:
        folded = normalize_name(name)
        if folded in self.live.spans:
            return "live"
        if folded in self.dormant.spans:
            return "dormant"
        raise SoundScriptSourceError(f"no game sound named {folded!r}")

    @property
    def names(self) -> tuple[str, ...]:
        return tuple(sorted(set(self.live.spans) | set(self.dormant.spans)))


def load_game_sound_directory(
    index: dict, *, read_bytes: Callable[[dict, str], bytes | None] | None = None
) -> GameSoundDirectory:
    live = load_kv_table(index, TABLE_PATHS["game-sound-live"], read_bytes=read_bytes)
    dormant = load_kv_table(index, TABLE_PATHS["game-sound-dormant"], read_bytes=read_bytes)
    shadowed = frozenset(set(dormant.spans) & set(live.spans))
    return GameSoundDirectory(live, dormant, shadowed)


# --- closures ---------------------------------------------------------------------------------


@dataclass(frozen=True, slots=True)
class GameSoundClosure:
    name: str
    source_name: str
    asset_id: str
    dormant: bool
    entry: SourceMember
    #: Evidence for the dormant `sounds.txt` entry of the same name, when one exists and this
    #: closure is the live entry that took the shared identity -- `None` otherwise (including
    #: for a dormant-owned closure, which shadows nothing). See `GameSoundDirectory`.
    shadowed_dormant: dict[str, Any] | None = None

    def members(self) -> tuple[SourceMember, ...]:
        return (self.entry,)


@dataclass(frozen=True, slots=True)
class ManifestClosure:
    asset_id: str
    entry: SourceMember

    def members(self) -> tuple[SourceMember, ...]:
        return (self.entry,)


@dataclass(frozen=True, slots=True)
class SoundscapeClosure:
    name: str
    source_name: str
    asset_id: str
    entry: SourceMember

    def members(self) -> tuple[SourceMember, ...]:
        return (self.entry,)


@dataclass(frozen=True, slots=True)
class SentenceClosure:
    name: str
    source_name: str
    asset_id: str
    entry: SourceMember

    def members(self) -> tuple[SourceMember, ...]:
        return (self.entry,)


@dataclass(frozen=True, slots=True)
class DspPresetClosure:
    preset_id: int
    asset_id: str
    entry: SourceMember

    def members(self) -> tuple[SourceMember, ...]:
        return (self.entry,)


def game_sound_closure(directory: GameSoundDirectory, name: str) -> GameSoundClosure:
    owner = directory.owner(name)
    table = directory.live if owner == "live" else directory.dormant
    folded, source_name, data, span = table.cut(name)
    member = SourceMember(role="entry", path=f"{table.path}#{folded}", data=data, origin=table.origin, span=span)
    shadowed_dormant: dict[str, Any] | None = None
    if owner == "live" and folded in directory.shadowed:
        dormant_source_name, dormant_offset, dormant_end = directory.dormant.spans[folded]
        shadowed_dormant = {
            "table": directory.dormant.path,
            "sourceName": dormant_source_name,
            "offset": dormant_offset,
            "length": dormant_end - dormant_offset,
        }
    return GameSoundClosure(
        folded, source_name, game_sound_asset_id(folded), owner == "dormant", member, shadowed_dormant
    )


def manifest_closure(
    index: dict, *, read_bytes: Callable[[dict, str], bytes | None] | None = None
) -> ManifestClosure:
    read_bytes = read_bytes or _default_read_bytes()
    path = TABLE_PATHS["manifest"]
    data, origin = _read(index, path, read_bytes)
    member = SourceMember(role="table", path=path, data=data, origin=origin)
    return ManifestClosure(manifest_asset_id(), member)


def soundscape_closure(table: KvTable, name: str) -> SoundscapeClosure:
    folded, source_name, data, span = table.cut(name)
    member = SourceMember(role="entry", path=f"{table.path}#{folded}", data=data, origin=table.origin, span=span)
    return SoundscapeClosure(folded, source_name, soundscape_asset_id(folded), member)


def sentence_closure(table: SentenceTable, name: str) -> SentenceClosure:
    folded, source_name, data, span = table.cut(name)
    member = SourceMember(role="entry", path=f"{table.path}#{folded}", data=data, origin=table.origin, span=span)
    return SentenceClosure(folded, source_name, sentence_asset_id(folded), member)


def dsp_preset_closure(table: DspTable, preset_id: int) -> DspPresetClosure:
    data, span = table.cut(preset_id)
    member = SourceMember(
        role="entry", path=f"{table.path}#{preset_id}", data=data, origin=table.origin, span=span
    )
    return DspPresetClosure(preset_id, dsp_preset_asset_id(preset_id), member)


def load_source_closure(
    index: dict,
    key: str,
    *,
    kind: str = "game-sound",
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
    directory: GameSoundDirectory | None = None,
    table: KvTable | SentenceTable | DspTable | None = None,
) -> object:
    """The one entry the unit owns, for whichever of the five kinds `kind` names.

    `directory`/`table` let a corpus run parse each shared source once; on their own the call
    reads the table itself, so one unit still costs one command.
    """

    if kind == "game-sound":
        directory = directory or load_game_sound_directory(index, read_bytes=read_bytes)
        return game_sound_closure(directory, key)
    if kind == "manifest":
        return manifest_closure(index, read_bytes=read_bytes)
    if kind == "soundscape":
        table = table or load_kv_table(index, TABLE_PATHS["soundscape"], read_bytes=read_bytes)
        return soundscape_closure(table, key)
    if kind == "sentence":
        table = table or load_sentence_table(index, read_bytes=read_bytes)
        return sentence_closure(table, key)
    if kind == "dsp-preset":
        table = table or load_dsp_table(index, read_bytes=read_bytes)
        return dsp_preset_closure(table, int(key))
    raise SoundScriptSourceError(f"unknown sound-script kind {kind!r}")
