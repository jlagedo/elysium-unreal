"""`crt_match.py` on a hand-built COFF object and archive.

The pass reads a Microsoft archive and the user's image, neither of which can be fixtured, so
the object reader, the relocation mask, the alias rule and the two refusals (a pattern at two
entries, two members with one body) run here on bytes assembled in the test.
"""

from __future__ import annotations

import os
import struct
import sys
import tempfile
from pathlib import Path

os.environ.setdefault("ELYSIUM_WORK_ROOT", tempfile.gettempdir())

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "research" / "tooling" / "ghidra" / "driver"))

import crt_match as cm  # noqa: E402


def _coff(symbols: list[tuple[str, int, int]], code: bytes, relocs: list[tuple[int, int]]) -> bytes:
    """One i386 COFF object with a single `.text` section.

    `symbols` are (name, value, storage class); `relocs` are (site, type)."""
    nsect = 1
    header_size = 20 + nsect * 40
    code_at = header_size
    reloc_at = code_at + len(code)
    symtab_at = reloc_at + len(relocs) * 10
    names = [b".text"] + [s[0].encode() for s in symbols]
    strtab = b""
    entries = []
    for name in names:
        if len(name) <= 8:
            entries.append(name.ljust(8, b"\x00"))
        else:
            entries.append(struct.pack("<II", 0, 4 + len(strtab)))
            strtab += name + b"\x00"
    records = [entries[0] + struct.pack("<IhHBB", 0, 1, 0, cm.IMAGE_SYM_CLASS_STATIC, 0)]
    for raw, (_, value, storage) in zip(entries[1:], symbols):
        records.append(raw + struct.pack("<IhHBB", value, 1, 0x20, storage, 0))
    symtab = b"".join(records)
    strings = struct.pack("<I", 4 + len(strtab)) + strtab
    head = struct.pack("<HHIIIHH", 0x14C, nsect, 0, symtab_at, len(records), 0, 0)
    section = struct.pack("<8sIIIIIIHHI", b".text", 0, 0, len(code), code_at, reloc_at, 0,
                          len(relocs), 0, cm.IMAGE_SCN_CNT_CODE | 0x60000000)
    relocations = b"".join(struct.pack("<IIH", site, 0, rtype) for site, rtype in relocs)
    return head + section + code + relocations + symtab + strings


def _archive(members: list[tuple[str, bytes]]) -> bytes:
    out = b"!<arch>\n"
    for name, payload in members:
        header = (name + "/").ljust(16).encode() + b"0".ljust(12) + b"0".ljust(6) + b"0".ljust(6) \
            + b"0".ljust(8) + str(len(payload)).ljust(10).encode() + b"`\n"
        out += header + payload + (b"\n" if len(payload) & 1 else b"")
    return out


class _Corpus:
    module = "vampire.dll"

    def __init__(self, names: dict[str, str], thunk: set[str] = frozenset()):
        self.name = names
        self.thunk = set(thunk)


class _Image:
    def __init__(self, base: int, code: bytes):
        self.code = [(base, code)]


RAND = bytes.fromhex("a1" + "00" * 4 + "69c0fd430300" + "05c39e2600" + "a3" + "00" * 4 + "c1f81025ff7f0000c3")


def test_extents_mask_relocations_and_cut_padding():
    obj = _coff([("_rand", 0, cm.IMAGE_SYM_CLASS_EXTERNAL)], RAND + b"\xcc\xcc\xcc",
                [(1, 0x06), (0x11, 0x06)])
    extents = cm.coff_extents("LIBC.LIB", "rand.obj", obj)
    assert [e.symbol for e in extents] == ["_rand"]
    rand = extents[0]
    assert rand.size == len(RAND)                     # the INT3 padding is not the body's
    assert rand.fixed == len(RAND) - 8                # two DIR32 sites masked
    assert rand.mask[1:5] == b"\x00" * 4 and rand.mask[0] == 1


def test_find_matches_at_the_image_offset_through_masked_sites(monkeypatch, tmp_path):
    obj = _coff([("_rand", 0, cm.IMAGE_SYM_CLASS_EXTERNAL)], RAND, [(1, 0x06), (0x11, 0x06)])
    (tmp_path / "LIBC.LIB").write_bytes(_archive([("rand.obj", obj)]))
    linked = bytearray(RAND)
    linked[1:5] = b"\x50\x0a\x6b\x10"                 # the linker filled the address
    linked[0x11:0x15] = b"\x50\x0a\x6b\x10"
    image = _Image(0x10431000, b"\x90" * 0x351 + bytes(linked) + b"\x90" * 16)
    monkeypatch.setattr(cm, "Image", lambda path: image)
    corpus = _Corpus({"10431351": "FUN_10431351", "10431000": "FUN_10431000"})
    rows, report = cm.crt_pass(corpus, tmp_path / "vampire.dll", tmp_path)
    assert [(r.addr, r.name, r.tier) for r in rows] == [("10431351", "_rand", "binary")]
    assert "rand.obj" in rows[0].evidence and report["proposed"] == 1


def test_a_named_entry_is_verification_not_a_proposal(monkeypatch, tmp_path):
    obj = _coff([("_rand", 0, cm.IMAGE_SYM_CLASS_EXTERNAL)], RAND, [])
    (tmp_path / "LIBC.LIB").write_bytes(_archive([("rand.obj", obj)]))
    monkeypatch.setattr(cm, "Image", lambda path: _Image(0x10000000, RAND))
    rows, report = cm.crt_pass(_Corpus({"10000000": "_rand"}), tmp_path / "x.dll", tmp_path)
    assert rows == [] and report["agree"] == 1
    rows, report = cm.crt_pass(_Corpus({"10000000": "_srand"}), tmp_path / "x.dll", tmp_path)
    assert rows == [] and report["disagree"] == 1


def test_aliases_in_one_member_are_one_name(monkeypatch, tmp_path):
    obj = _coff([("_tolower", 0, cm.IMAGE_SYM_CLASS_EXTERNAL),
                 ("__tolower_lk", 0, cm.IMAGE_SYM_CLASS_EXTERNAL)], RAND, [])
    (tmp_path / "LIBC.LIB").write_bytes(_archive([("tolower.obj", obj)]))
    monkeypatch.setattr(cm, "Image", lambda path: _Image(0x10000000, RAND))
    rows, _ = cm.crt_pass(_Corpus({"10000000": "FUN_10000000"}), tmp_path / "x.dll", tmp_path)
    assert [r.name for r in rows] == ["_tolower"]
    assert "aliases __tolower_lk" in rows[0].evidence


def test_two_members_with_one_body_and_one_body_at_two_entries_are_unsettled(monkeypatch, tmp_path):
    strtol = _coff([("_strtol", 0, cm.IMAGE_SYM_CLASS_EXTERNAL)], RAND, [])
    wcstol = _coff([("_wcstol", 0, cm.IMAGE_SYM_CLASS_EXTERNAL)], RAND, [])
    (tmp_path / "LIBC.LIB").write_bytes(_archive([("strtol.obj", strtol), ("wcstol.obj", wcstol)]))
    monkeypatch.setattr(cm, "Image", lambda path: _Image(0x10000000, RAND))
    rows, report = cm.crt_pass(_Corpus({"10000000": "FUN_10000000"}), tmp_path / "x.dll", tmp_path)
    assert [(r.tier, r.source) for r in rows] == [("unsettled", "unsettled")]
    assert "_strtol, _wcstol" in rows[0].evidence and report["two symbols"] == 1

    # the same body linked twice under one symbol: neither entry is named
    memcpy = _coff([("_memcpy", 0, cm.IMAGE_SYM_CLASS_EXTERNAL)], RAND, [])
    (tmp_path / "LIBC.LIB").write_bytes(_archive([("memcpy.obj", memcpy)]))
    monkeypatch.setattr(cm, "Image", lambda path: _Image(0x10000000, RAND + b"\x90" * 8 + RAND))
    names = {"10000000": "FUN_10000000", f"{0x10000000 + len(RAND) + 8:08x}": "FUN_2"}
    rows, report = cm.crt_pass(_Corpus(names), tmp_path / "x.dll", tmp_path)
    assert sorted(r.addr for r in rows) == sorted(names) and all(r.tier == "unsettled" for r in rows)
    assert report["shared body"] == 2 and report["ambiguous"] == {"_memcpy": 2}


def test_decorated_symbols_are_reported_not_proposed(monkeypatch, tmp_path):
    obj = _coff([("??2@YAPAXI@Z", 0, cm.IMAGE_SYM_CLASS_EXTERNAL)], RAND, [])
    (tmp_path / "LIBC.LIB").write_bytes(_archive([("new.obj", obj)]))
    monkeypatch.setattr(cm, "Image", lambda path: _Image(0x10000000, RAND))
    rows, report = cm.crt_pass(_Corpus({"10000000": "FUN_10000000"}), tmp_path / "x.dll", tmp_path)
    assert rows == [] and report["decorated"] == 1
