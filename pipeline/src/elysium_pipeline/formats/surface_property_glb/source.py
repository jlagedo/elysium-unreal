"""The surface-property table as the install resolves it, cut into per-entry units."""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import os
from pathlib import Path
from typing import Callable

from elysium_pipeline.formats.surface_property_glb import lexer
from elysium_pipeline.formats.surface_property_glb.coverage import ByteLedger
from elysium_pipeline.formats.surface_property_glb.model import (
    TABLE_PATH,
    SourceIdentity,
    asset_id,
    normalize_name,
)

#: The sound-script table the physics `impact` and `scrape` keys name into.
SOUND_SCRIPT_PATH = "scripts/game_sounds_surfaceproperties.txt"


class SurfacePropertySourceError(RuntimeError):
    """The surface-property table is absent or incoherent."""


@dataclass(frozen=True, slots=True)
class SourceMember:
    role: str
    path: str
    data: bytes
    origin: dict[str, object]
    table_offset: int

    def identity(self) -> SourceIdentity:
        return SourceIdentity(
            role=self.role,
            path=self.path,
            origin=self.origin,
            byte_length=len(self.data),
            sha256=hashlib.sha256(self.data).hexdigest(),
            table_offset=self.table_offset,
        )


@dataclass(frozen=True, slots=True)
class SurfacePropertyClosure:
    """The one table entry the unit owns.

    A named `base` stays a hashed dependency rather than a member: its bytes belong to its own
    surface-property product, and duplicating them here would make two units authoritative for
    one entry.
    """

    name: str
    source_name: str
    asset_id: str
    entry: SourceMember

    def members(self) -> tuple[SourceMember, ...]:
        return (self.entry,)


@dataclass(frozen=True, slots=True)
class SurfacePropertyTable:
    """Every entry of one parsed table, with the proof that its bytes are fully accounted."""

    path: str
    data: bytes
    origin: dict[str, object]
    spans: dict[str, tuple[str, int, int]]   # folded name -> (source name, offset, end)

    @property
    def sha256(self) -> str:
        return hashlib.sha256(self.data).hexdigest()

    @property
    def names(self) -> tuple[str, ...]:
        return tuple(sorted(self.spans))

    def summary(self, name: str) -> dict[str, object]:
        """What the unit publishes about the table it was cut from."""

        _, offset, end = self.spans[name]
        return {
            "path": self.path,
            "byteLength": len(self.data),
            "sha256": self.sha256,
            "entryCount": len(self.spans),
            "entryOffset": offset,
            "entryLength": end - offset,
        }

    def closure(self, name: str) -> SurfacePropertyClosure:
        folded = normalize_name(name)
        if folded not in self.spans:
            raise SurfacePropertySourceError(f"{self.path} declares no surface {folded!r}")
        source_name, offset, end = self.spans[folded]
        member = SourceMember(
            role="entry",
            path=f"{self.path}#{folded}",
            data=self.data[offset:end],
            origin=dict(self.origin, table=self.summary(folded)),
            table_offset=offset,
        )
        return SurfacePropertyClosure(folded, source_name, asset_id(folded), member)


def origin_of(entry) -> dict[str, object]:
    kind, value = entry
    if kind == "loose":
        path = Path(value)
        lowered = [part.lower() for part in path.parts]
        root = "loose"
        for candidate in ("unofficial_patch", "vampire"):
            if candidate in lowered:
                root = path.parts[lowered.index(candidate)]
                break
        return {"kind": "loose", "root": root}
    pack, offset, size = value
    return {
        "kind": "vpk",
        "container": os.path.basename(pack),
        "offset": int(offset),
        "size": int(size),
    }


def _read(index: dict, key: str, read_bytes) -> tuple[bytes, dict[str, object]]:
    entry = index.get(key)
    data = read_bytes(index, key) if entry else None
    if data is None:
        raise SurfacePropertySourceError(f"missing required table: {key}")
    return data, origin_of(entry)


def load_table(
    index: dict,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
    path: str = TABLE_PATH,
) -> SurfacePropertyTable:
    """Parse the whole table and prove every one of its bytes is accounted for.

    The proof is what lets a unit publish a ledger over its own entry alone: if the table
    partitions into entries, comments and whitespace with nothing left over, then the bytes no
    unit claims are bytes no unit was ever authoritative for.
    """

    if read_bytes is None:
        from elysium_pipeline.formats import install

        read_bytes = install.read
    data, origin = _read(index, path, read_bytes)
    text = lexer.decode_text(data)
    try:
        entries = lexer.parse(lexer.tokenize(text))
    except lexer.SurfacePropertyLexError as error:
        raise SurfacePropertySourceError(f"{path}: {error}") from error
    if not entries:
        raise SurfacePropertySourceError(f"{path} declares no surface entries")

    spans: dict[str, tuple[str, int, int]] = {}
    ledger = ByteLedger(path, data)
    for entry in entries:
        folded = normalize_name(entry.name.text)
        if folded in spans:
            # Two entries under one name would leave the unit's identity ambiguous, and the
            # engine's own merge order is not recoverable from the file.
            raise SurfacePropertySourceError(f"{path}: surface {folded!r} is declared twice")
        spans[folded] = (entry.name.text, entry.offset, entry.end)
        ledger.claim(entry.offset, entry.end - entry.offset, "mapped", f"table.entry[{folded}]")
    covered = [(offset, end) for _, offset, end in spans.values()]
    for token in lexer.tokenize(text):
        if any(offset <= token.offset < end for offset, end in covered):
            continue                            # the entry's own ledger claims these bytes
        if token.kind == "whitespace":
            ledger.claim(
                token.offset, token.length, "omitted-proven", "table.insignificant-whitespace"
            )
        elif token.kind in ("comment", "bom"):
            ledger.claim(token.offset, token.length, "mapped", f"table.{token.kind}")
        else:
            raise SurfacePropertySourceError(
                f"{path}: byte {token.offset} belongs to no surface entry"
            )
    ledger.finish()
    return SurfacePropertyTable(path, data, origin, spans)


def load_sound_script_names(
    index: dict,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
) -> frozenset[str]:
    """The sound-script names `impact` and `scrape` can resolve against, folded to lower case.

    A sound script nests (`rndwave` holds a list of waves), so the file is scanned for the names
    it declares at depth zero rather than parsed into a value tree this seam has no use for.
    """

    if read_bytes is None:
        from elysium_pipeline.formats import install

        read_bytes = install.read
    if SOUND_SCRIPT_PATH not in index:
        return frozenset()
    data = read_bytes(index, SOUND_SCRIPT_PATH)
    if data is None:
        return frozenset()
    tokens = [
        token
        for token in lexer.tokenize(lexer.decode_text(data))
        if token.kind in ("string", "open", "close")
    ]
    names: set[str] = set()
    depth = 0
    for position, token in enumerate(tokens):
        if token.kind == "open":
            depth += 1
        elif token.kind == "close":
            depth = max(0, depth - 1)
        elif depth == 0 and tokens[position + 1:position + 2]:
            if tokens[position + 1].kind == "open":
                names.add(token.text.strip().lower())
    return frozenset(names)


def load_source_closure(
    index: dict,
    name: str,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
) -> SurfacePropertyClosure:
    return load_table(index, read_bytes=read_bytes).closure(name)
