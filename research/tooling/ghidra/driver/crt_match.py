# -*- coding: utf-8 -*-
"""Name statically linked C-runtime bodies by exact byte match against the archive the game links.

`crt_fid` names the CRT through Ghidra's Function ID, a hash of each library function's
instructions. FID missed bodies the linker emitted verbatim -- `memcpy` (285 callers), `rand`,
`strchr` -- because the archive object and the linked image analyse to different function
extents, and a hash of two extents never agrees. This reads the archive itself: every COFF
member's code section, every symbol's extent inside it, the relocation sites masked, and asks
whether those bytes stand at a function entry of the image. A match is the archive's own
statement -- the linker copied these bytes from this member -- so it carries the `binary` tier.

What is read: the `!<arch>` members of `LIBC.LIB` and `LIBCMT.LIB` staged (sha256-verified) by
`crt_fid stage`, and the image's code section. Nothing here reads a body's meaning.

Refused: an extent with fewer than `MIN_FIXED` unmasked bytes (too generic to be one function's);
a pattern found at two function entries (the match is not identity); a decorated C++ symbol
(the overlay names C identifiers only; those are reported); an address the two archives name
differently.
"""

from __future__ import annotations

import collections
import re
import struct
from dataclasses import dataclass
from pathlib import Path

MIN_FIXED = 8               # unmasked bytes a pattern must carry to be one function's
ARCHIVES = ("LIBC.LIB", "LIBCMT.LIB")

IMAGE_SCN_CNT_CODE = 0x20
IMAGE_SYM_CLASS_EXTERNAL = 2
IMAGE_SYM_CLASS_STATIC = 3
# relocation type -> bytes the linker rewrites at the site
RELOC_WIDTH = {0x06: 4, 0x07: 4, 0x0A: 2, 0x0B: 4, 0x14: 4}


@dataclass
class Extent:
    archive: str
    member: str
    symbol: str
    storage: str            # external / static
    pattern: bytes          # literal bytes with relocation sites as b"?"
    mask: bytes             # one byte per position: 1 = fixed, 0 = relocated
    size: int
    value: int = 0          # the symbol's offset in its section: aliases share it

    @property
    def unit(self) -> str:
        return self.member.replace("\\", "/").rsplit("/", 1)[-1]

    @property
    def fixed(self) -> int:
        return sum(self.mask)


@dataclass
class Match:
    va: int
    extent: Extent


def crt_root(research: Path) -> Path | None:
    staged = research / "ghidra" / "crt" / "vc6sp5"
    if any((staged / name).is_file() for name in ARCHIVES):
        return staged
    return None


# -- the archive -----------------------------------------------------------------------------------

def archive_members(path: Path) -> list[tuple[str, bytes]]:
    """Every `(name, payload)` of a `!<arch>` library, long names resolved."""
    data = path.read_bytes()
    if data[:8] != b"!<arch>\n":
        raise ValueError(f"{path} is not an ar archive")
    longnames = b""
    members: list[tuple[str, bytes]] = []
    at = 8
    while at + 60 <= len(data):
        header = data[at:at + 60]
        name = header[:16].decode("latin-1").rstrip()
        size = int(header[48:58].decode("ascii").strip() or "0")
        payload = data[at + 60:at + 60 + size]
        at += 60 + size + (size & 1)
        if name == "/":
            continue                          # the linker member(s)
        if name == "//":
            longnames = payload
            continue
        if name.startswith("/") and name[1:].isdigit():
            start = int(name[1:])
            end = longnames.find(b"\x00", start)
            slash = longnames.find(b"/", start)
            stop = min(x for x in (end, slash) if x >= 0) if max(end, slash) >= 0 else len(longnames)
            name = longnames[start:stop].decode("latin-1")
        else:
            name = name.rstrip("/")
        members.append((name, payload))
    return members


def coff_extents(archive: str, member: str, obj: bytes) -> list[Extent]:
    """The function extents of one i386 COFF object: each named symbol in a code section, from
    its value to the next symbol's, relocation sites masked, trailing alignment padding cut."""
    if len(obj) < 20:
        return []
    machine, nsect, _stamp, symtab, nsyms, optsize, _chars = struct.unpack_from("<HHIIIHH", obj, 0)
    if machine != 0x14C or symtab == 0 or symtab + nsyms * 18 > len(obj):
        return []
    strings_at = symtab + nsyms * 18
    strtab = obj[strings_at:]

    sections = []
    base = 20 + optsize
    for i in range(nsect):
        raw = obj[base + i * 40:base + (i + 1) * 40]
        if len(raw) < 40:
            return []
        name, _vsize, _va, rawsize, rawptr, relptr, _lineptr, nrel, _nline, chars = \
            struct.unpack_from("<8sIIIIIIHHI", raw, 0)
        sections.append((name.rstrip(b"\x00").decode("latin-1"), rawsize, rawptr, relptr, nrel, chars))

    def symbol_name(raw: bytes) -> str:
        if raw[:4] == b"\x00\x00\x00\x00":
            offset = struct.unpack_from("<I", raw, 4)[0]
            end = strtab.find(b"\x00", offset)
            return strtab[offset:end if end >= 0 else None].decode("latin-1")
        return raw.rstrip(b"\x00").decode("latin-1")

    # symbols per section: (value, name, storage)
    per_section: dict[int, list[tuple[int, str, int]]] = collections.defaultdict(list)
    i = 0
    while i < nsyms:
        raw = obj[symtab + i * 18:symtab + (i + 1) * 18]
        value, section, _type, storage, aux = struct.unpack_from("<IhHBB", raw, 8)
        if section > 0 and storage in (IMAGE_SYM_CLASS_EXTERNAL, IMAGE_SYM_CLASS_STATIC):
            per_section[section].append((value, symbol_name(raw[:8]), storage))
        i += 1 + aux

    out: list[Extent] = []
    for number, (sname, rawsize, rawptr, relptr, nrel, chars) in enumerate(sections, start=1):
        if not chars & IMAGE_SCN_CNT_CODE or rawsize == 0 or rawptr == 0:
            continue
        code = obj[rawptr:rawptr + rawsize]
        mask = bytearray(b"\x01" * len(code))
        for r in range(nrel):
            site, _symidx, rtype = struct.unpack_from("<IIH", obj, relptr + r * 10)
            width = RELOC_WIDTH.get(rtype, 4)
            for k in range(site, min(site + width, len(mask))):
                mask[k] = 0
        symbols = sorted(per_section.get(number, []))
        # the section's own name symbol (`.text`, storage static, value 0) is a boundary, not a body
        named = [(v, n, s) for v, n, s in symbols if n != sname and not n.startswith((".", "$"))]
        boundaries = sorted({v for v, _, _ in symbols} | {rawsize})
        for value, name, storage in named:
            stop = next((b for b in boundaries if b > value), rawsize)
            chunk = code[value:stop]
            cmask = mask[value:stop]
            # alignment padding after the last instruction: INT3 or NOP runs the linker may not keep
            while len(chunk) > 1 and chunk[-1] in (0xCC, 0x90) and cmask[-1]:
                chunk, cmask = chunk[:-1], cmask[:-1]
            if len(chunk) < MIN_FIXED:
                continue
            pattern = bytes(b if m else 0x3F for b, m in zip(chunk, cmask))
            out.append(Extent(archive, member, name,
                              "external" if storage == IMAGE_SYM_CLASS_EXTERNAL else "static",
                              pattern, bytes(cmask), len(chunk), value))
    return out


def archive_extents(path: Path) -> list[Extent]:
    out: list[Extent] = []
    for member, payload in archive_members(path):
        if payload[:2] == b"\x4c\x01":            # i386 COFF object; import descriptors are not
            out += coff_extents(path.name, member, payload)
    return out


# -- the image -------------------------------------------------------------------------------------

class Image:
    """The code sections of a PE image, by virtual address."""

    def __init__(self, path: Path):
        data = path.read_bytes()
        e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
        nsect = struct.unpack_from("<H", data, e_lfanew + 6)[0]
        optsize = struct.unpack_from("<H", data, e_lfanew + 20)[0]
        self.base = struct.unpack_from("<I", data, e_lfanew + 24 + 28)[0]
        first = e_lfanew + 24 + optsize
        self.code: list[tuple[int, bytes]] = []
        for i in range(nsect):
            header = first + i * 40
            vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", data, header + 8)
            chars = struct.unpack_from("<I", data, header + 36)[0]
            if chars & IMAGE_SCN_CNT_CODE:
                self.code.append((self.base + va, data[rawptr:rawptr + min(rawsize, vsize or rawsize)]))


def _regex(extent: Extent) -> re.Pattern:
    parts = []
    for byte, fixed in zip(extent.pattern, extent.mask):
        parts.append(re.escape(bytes([byte])) if fixed else b".")
    return re.compile(b"".join(parts), re.S)


def find(image: Image, extents: list[Extent]) -> dict[str, list[Match]]:
    """Every virtual address each extent's bytes stand at, keyed by symbol."""
    found: dict[str, list[Match]] = collections.defaultdict(list)
    for extent in extents:
        if extent.fixed < MIN_FIXED:
            continue
        pattern = _regex(extent)
        for start, code in image.code:
            for m in pattern.finditer(code):
                found[extent.symbol].append(Match(start + m.start(), extent))
    return found


# -- the pass --------------------------------------------------------------------------------------

C_SYMBOL = re.compile(r"^_[A-Za-z_]\w*$")


def crt_pass(corpus, binary: Path, root: Path):
    """Proposals for the module's unnamed function entries whose bytes are an archive symbol's,
    and a report: matches at entries already named (agreement is the pass's own verification),
    ambiguous patterns, decorated symbols, and which archive answered."""
    from name_passes import Proposal, is_placeholder  # noqa: WPS433 -- beside this file

    image = Image(binary)
    per_archive: dict[str, dict[str, list[Match]]] = {}
    for name in ARCHIVES:
        path = root / name
        if path.is_file():
            per_archive[name] = find(image, archive_extents(path))

    # va -> every extent whose bytes stand there, across archives. A symbol found at two entries
    # is not identity (VC6 links `memcpy` and `memmove` from one body): those entries are
    # recorded as unsettled rather than named.
    by_va: dict[int, list[Extent]] = collections.defaultdict(list)
    shared: dict[int, set[str]] = collections.defaultdict(set)
    ambiguous: dict[str, int] = {}
    for archive, found in per_archive.items():
        for symbol, matches in found.items():
            entries = {m.va for m in matches if f"{m.va:08x}" in corpus.name}
            if len(entries) > 1:
                ambiguous[symbol] = len(entries)
                for va in entries:
                    shared[va].add(symbol)
                continue
            for m in matches:
                if m.va in entries:
                    by_va[m.va].append(m.extent)

    report = collections.Counter()
    agreements: list[str] = []
    disagreements: list[str] = []
    decorated: list[str] = []
    proposals: list[Proposal] = []
    for va, extents in sorted(by_va.items()):
        addr = f"{va:08x}"
        symbols = sorted({e.symbol for e in extents})
        # Aliases are one body under two names in one member (`_tolower` / `__tolower_lk`);
        # the same bytes in two members (`_strtol` / `_wcstol`) are two bodies the bytes cannot
        # tell apart.
        units = {(e.unit, e.value) for e in extents}
        if len(units) > 1:
            report["two symbols"] += 1
            disagreements.append(f"{addr}: {', '.join(symbols)}")
            if all(C_SYMBOL.match(s) for s in symbols) and is_placeholder(corpus.name.get(addr, "")) \
                    and addr not in corpus.thunk:
                proposals.append(Proposal(
                    corpus.module, addr, corpus.name[addr], "unsettled",
                    f"CRT: byte-identical to {len(units)} archive bodies ({', '.join(symbols)}); "
                    "the bytes cannot tell which the linker copied", "unsettled"))
            continue
        external = sorted((e for e in extents if e.storage == "external"), key=lambda e: (len(e.symbol), e.symbol))
        chosen = external[0] if external else min(extents, key=lambda e: (len(e.symbol), e.symbol))
        symbol = chosen.symbol
        aliases = [s for s in symbols if s != symbol]
        archives = sorted({e.archive for e in extents})
        current = corpus.name.get(addr, "")
        if not is_placeholder(current):
            if current.lstrip("_") in {s.lstrip("_") for s in symbols}:
                report["agree"] += 1
                agreements.append(f"{addr} {current}")
            elif symbol.startswith("?") or current.startswith(("operator", "FID_", "staticinit_", "$")):
                report["decorated"] += 1
            else:
                report["disagree"] += 1
                disagreements.append(f"{addr}: image {current}, archive {symbol}")
            continue
        if addr in corpus.thunk:
            continue
        if not C_SYMBOL.match(symbol):
            report["decorated"] += 1
            decorated.append(f"{addr} {symbol}")
            continue
        report["proposed"] += 1
        relocated = chosen.size - chosen.fixed
        proposals.append(Proposal(
            corpus.module, addr, symbol, "binary",
            f"CRT: byte-identical to {archives[0]} member {chosen.member} symbol {symbol} "
            f"({chosen.storage}, {chosen.size} bytes, {relocated} relocated byte(s) masked"
            + (f"; aliases {', '.join(aliases)}" if aliases else "")
            + (f"; also in {', '.join(archives[1:])}" if len(archives) > 1 else "")
            + "); VC6 SP5 archive staged and sha256-verified by `crt_fid stage`", "crt"))
    for va, symbols in sorted(shared.items()):
        addr = f"{va:08x}"
        if va in by_va or addr in corpus.thunk or not is_placeholder(corpus.name.get(addr, "")):
            continue
        if not all(C_SYMBOL.match(s) for s in symbols):
            continue
        others = sorted(f"{v:08x}" for v, s in shared.items() if v != va and s & symbols)
        report["shared body"] += 1
        proposals.append(Proposal(
            corpus.module, addr, corpus.name[addr], "unsettled",
            f"CRT: the bytes of {', '.join(sorted(symbols))} stand here and at {', '.join(others)}; "
            "VC6 links one body under both names and the bytes cannot tell which entry is which",
            "unsettled"))
    out = dict(report)
    out["archives"] = {a: sum(len(v) for v in f.values()) for a, f in per_archive.items()}
    out["ambiguous"] = ambiguous
    out["agreements"] = agreements
    out["disagreements"] = disagreements
    out["decorated_symbols"] = decorated
    return proposals, out
