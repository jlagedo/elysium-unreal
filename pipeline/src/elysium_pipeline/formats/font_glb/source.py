"""The install closures for one font identity and for the font-list registry.

A font unit's only true member is its own `.fnt` file; the atlas pages it names are another
seam's units (`texture_glb`, `material_glb`) and are joined by asset ID, never by embedding their
bytes here. `available_pages` is what the install actually holds for this stem's `-page<n>`
triples, gathered independently of anything the `.fnt` header declares, so the decoder can name a
page the install holds and no glyph uses.
"""

from __future__ import annotations

from dataclasses import dataclass
import re
from typing import Callable

from elysium_pipeline.formats.font_glb.model import (
    FONT_LIST_PATH,
    FONTS_DIR,
    asset_id,
    font_path,
    normalize_font_key,
)
from elysium_pipeline.formats.unit_contract import SourceMember, origin_of


class FontSourceError(RuntimeError):
    """The selected font or the font-list registry is absent or incoherent."""


#: Only `.tth` and `.vmt` are recognized page members: the install ships one `.ttz` for every
#: `.tth` it ships (242/242 on the real corpus, zero `.ttz`-only stems), so a `.ttz` sibling names
#: no page this scan would otherwise miss, and nothing downstream ever reads a `.ttz` flag.
_PAGE_RE = re.compile(r"^(?P<stem>.+)-page(?P<index>\d+)\.(?P<ext>tth|vmt)$")

#: A page's own `.vmt` names its real texture with `"$basetexture" "<value>"`; the install ships
#: many pages under a stem the `.fnt` itself does not repeat (e.g. `tahoma_28_500_000-page0.vmt`
#: points at `fonts/Tahoma_25_500_000-page0`), so this is read instead of guessing `<stem>-page<n>`.
_BASETEXTURE_RE = re.compile(r'"\$basetexture"\s*"([^"]*)"', re.IGNORECASE)


def _normalize_basetexture(value: str) -> str | None:
    text = value.strip()
    if not text:
        return None
    normalized = text.replace("\\", "/").strip("/").lower()
    while "//" in normalized:
        normalized = normalized.replace("//", "/")
    for suffix in (".tth", ".ttz", ".vtf"):
        if normalized.endswith(suffix):
            normalized = normalized[: -len(suffix)]
            break
    if normalized.startswith("materials/"):
        normalized = normalized[len("materials/"):]
    return normalized or None


def _declared_texture_path(reader, index: dict, vmt_path: str) -> str | None:
    """The page's own `.vmt`-declared texture path (`materials/...tth`), or `None` unread/absent."""

    data = reader(index, vmt_path)
    if data is None:
        return None
    match = _BASETEXTURE_RE.search(data.decode("latin-1"))
    if match is None:
        return None
    normalized = _normalize_basetexture(match.group(1))
    if normalized is None:
        return None
    return f"materials/{normalized}.tth"


@dataclass(frozen=True, slots=True)
class PageAvailability:
    index: int
    vmt_path: str
    vmt_present: bool
    #: The texture identity to actually use: the page's own `.vmt`-declared target when the `.vmt`
    #: names one, else the naive `<stem>-page<n>.tth` guess.
    resolved_texture_path: str
    resolved_texture_present: bool
    #: Whether `resolved_texture_path` came from a `.vmt`'s own `$basetexture` (`True`) or is the
    #: naive stem guess because the `.vmt` is absent or declares nothing usable (`False`).
    basetexture_declared: bool


@dataclass(frozen=True, slots=True)
class FontSourceClosure:
    key: str
    asset_id: str
    fnt: SourceMember
    available_pages: dict[int, PageAvailability]
    font_list_present: bool

    def members(self) -> tuple[SourceMember, ...]:
        return (self.fnt,)


@dataclass(frozen=True, slots=True)
class FontListSourceClosure:
    source: SourceMember

    def members(self) -> tuple[SourceMember, ...]:
        return (self.source,)


def _reader(read_bytes: Callable[[dict, str], bytes | None] | None):
    if read_bytes is not None:
        return read_bytes
    from elysium_pipeline.formats import install

    return install.read


def font_keys(index: dict) -> list[str]:
    """Every `.fnt` stem the install resolves, folded and sorted."""

    prefix, suffix = FONTS_DIR + "/", ".fnt"
    return sorted(
        path[len(prefix):-len(suffix)]
        for path in index
        if path.startswith(prefix) and path.endswith(suffix)
    )


def _scan_pages(index: dict, key: str, reader: Callable[[dict, str], bytes | None]) -> dict[int, PageAvailability]:
    prefix = f"{FONTS_DIR}/{key}-page"
    rows: dict[int, dict[str, str | bool]] = {}
    for candidate in index:
        if not candidate.startswith(prefix):
            continue
        match = _PAGE_RE.match(candidate[len(FONTS_DIR) + 1:])
        if match is None or match.group("stem") != key:
            continue
        page_index = int(match.group("index"))
        row = rows.setdefault(page_index, {})
        row[match.group("ext")] = True
    pages: dict[int, PageAvailability] = {}
    for page_index, flags in rows.items():
        tth_path = f"{FONTS_DIR}/{key}-page{page_index}.tth"
        vmt_path = f"{FONTS_DIR}/{key}-page{page_index}.vmt"
        tth_present = bool(flags.get("tth"))
        vmt_present = bool(flags.get("vmt"))
        declared = _declared_texture_path(reader, index, vmt_path) if vmt_present else None
        if declared is not None:
            resolved_texture_path = declared
            resolved_texture_present = declared in index
            basetexture_declared = True
        else:
            resolved_texture_path = tth_path
            resolved_texture_present = tth_present
            basetexture_declared = False
        pages[page_index] = PageAvailability(
            index=page_index,
            vmt_path=vmt_path,
            vmt_present=vmt_present,
            resolved_texture_path=resolved_texture_path,
            resolved_texture_present=resolved_texture_present,
            basetexture_declared=basetexture_declared,
        )
    return pages


def load_source_closure(
    index: dict,
    key: str,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
) -> FontSourceClosure:
    reader = _reader(read_bytes)
    normalized = normalize_font_key(key)
    path = font_path(normalized)
    entry = index.get(path)
    data = reader(index, path) if entry else None
    if data is None:
        raise FontSourceError(f"missing required font table: {path}")
    fnt = SourceMember(role="fnt", path=path, data=data, origin=origin_of(entry))
    pages = _scan_pages(index, normalized, reader)
    font_list_present = FONT_LIST_PATH in index
    return FontSourceClosure(
        key=normalized,
        asset_id=asset_id(normalized),
        fnt=fnt,
        available_pages=pages,
        font_list_present=font_list_present,
    )


def load_font_list_closure(
    index: dict,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
) -> FontListSourceClosure:
    reader = _reader(read_bytes)
    entry = index.get(FONT_LIST_PATH)
    data = reader(index, FONT_LIST_PATH) if entry else None
    if data is None:
        raise FontSourceError(f"missing required font registry: {FONT_LIST_PATH}")
    member = SourceMember(role="font-list", path=FONT_LIST_PATH, data=data, origin=origin_of(entry))
    return FontListSourceClosure(source=member)
