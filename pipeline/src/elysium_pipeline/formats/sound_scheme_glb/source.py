"""The sound-scheme table as the install resolves it: one file, one unit.

Unlike the surface-property and sound-script seams, a sound scheme is not cut from a shared
table -- `sound/schemes/<stem>.txt` is its own file, so the source closure is the whole member and
carries no `span`.

`install.build_index()`'s default `dirs` does not walk `sound/`; a caller resolving this seam
against the real install rebuilds the index with `sound` added (`build_index(dirs=("sound", ...))`),
or a patch's loose `sound/schemes/` overrides vanish behind the VPK copies.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Callable

from elysium_pipeline.formats.sound_scheme_glb import lexer
from elysium_pipeline.formats.sound_scheme_glb.model import (
    DSP_PRESET_PATH,
    SOURCE_ROOT,
    SOURCE_SUFFIX,
    asset_id,
    normalize_stem,
    sound_source_path,
    source_path,
)
from elysium_pipeline.formats.unit_contract import SourceMember, origin_of, read_member


class SoundSchemeSourceError(RuntimeError):
    """The install carries no sound-scheme member for the requested key."""


@dataclass(frozen=True, slots=True)
class SoundSchemeSourceClosure:
    """The one file the unit owns: no span, no sibling members."""

    stem: str
    asset: str
    member: SourceMember

    def members(self) -> tuple[SourceMember, ...]:
        return (self.member,)


def source_keys(index: dict) -> list[str]:
    """Every sound-scheme stem the UP-first install index resolves, folded and sorted.

    174 schemes resolve in the merged install (147 in the VPKs alone).
    """

    stems: set[str] = set()
    for key in index:
        if key.startswith(SOURCE_ROOT) and key.endswith(SOURCE_SUFFIX):
            stems.add(key[len(SOURCE_ROOT):-len(SOURCE_SUFFIX)])
    return sorted(stems)


def load_source_closure(
    index: dict,
    key: str,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
) -> SoundSchemeSourceClosure:
    stem = normalize_stem(key)
    path = source_path(stem)
    entry = index.get(path)
    if entry is None:
        raise SoundSchemeSourceError(f"missing required sound scheme: {path}")
    data = read_member(index, path, read_bytes=read_bytes)
    if data is None:
        raise SoundSchemeSourceError(f"missing required sound scheme: {path}")
    origin = origin_of(entry)
    member = SourceMember(role="unit-selecting", path=path, data=data, origin=origin)
    return SoundSchemeSourceClosure(stem=stem, asset=asset_id(stem), member=member)


def sound_resolved(index: dict, authored_path: str) -> bool:
    """Whether the UP-first index holds a member for a `Filename`, relative to `sound/`."""

    return sound_source_path(authored_path) in index


def load_dsp_preset_ids(
    index: dict,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
) -> frozenset[str]:
    """The DSP preset ids `scripts/dsp_presets.txt` declares, as authored digit strings.

    A preset is a brace block at the top of the file whose first token is its numeric id
    (`{ <id> <configuration> ... }`); the table has no named blocks the way a sound scheme does,
    so this scans brace depth alone rather than reusing the named-block grammar. Ids that fail to
    resolve stay ordinary strings -- this is a membership check, not the `dsp-preset` seam's own
    decode.
    """

    if DSP_PRESET_PATH not in index:
        return frozenset()
    data = read_member(index, DSP_PRESET_PATH, read_bytes=read_bytes)
    if not data:
        return frozenset()
    tokens = [
        token
        for token in lexer.tokenize(lexer.decode_text(data))
        if token.kind in ("string", "open", "close")
    ]
    ids: set[str] = set()
    depth = 0
    for position, token in enumerate(tokens):
        if token.kind == "open":
            if depth == 0 and position + 1 < len(tokens) and tokens[position + 1].kind == "string":
                candidate = tokens[position + 1].text.strip().strip('"')
                if candidate.lstrip("-").isdigit():
                    ids.add(candidate)
            depth += 1
        elif token.kind == "close":
            depth = max(0, depth - 1)
    return frozenset(ids)
