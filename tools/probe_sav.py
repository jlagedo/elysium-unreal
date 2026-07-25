#!/usr/bin/env python3
"""Dump a VtMB `.sav` savegame — container, sections, blocks, entities, script state.

Reads a save file the user points it at; touches no game install. Format reference:
`docs/savegame_format.md`; decoder: `tools/sav.py`.

Usage
  python tools/probe_sav.py <file.sav>                 container + section summary
  python tools/probe_sav.py <file.sav> --map <name>    that section in detail
  python tools/probe_sav.py <file.sav> --entity player fields of matching entities
  python tools/probe_sav.py <file.sav> --python        the pickled script namespaces
  python tools/probe_sav.py <file.sav> --extract DIR   write inflated sections out
"""
import argparse
import io
import pickle
import struct
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sav  # noqa: E402


class _SafeUnpickler(pickle.Unpickler):
    """The save's namespaces are plain str->int dicts; refuse anything constructible."""

    def find_class(self, module, name):
        raise pickle.UnpicklingError(f"refusing {module}.{name}")


def python_namespaces(sec):
    """Decode the `Python` block: a chain of `byte tag; int len; pickle[len]` records."""
    b = sec.block("Python")
    if b is None or not b.loc_body:
        return []
    blob = sec.body[sec.base_file_pos + b.loc_body:]
    out, off = [], 0
    while off + 5 <= len(blob):
        tag = blob[off]
        n, = struct.unpack_from("<i", blob, off + 1)
        payload = blob[off + 5: off + 5 + n]
        try:
            out.append((tag, _SafeUnpickler(io.BytesIO(payload)).load()))
        except Exception as e:                                   # noqa: BLE001
            out.append((tag, f"<undecodable: {e}>"))
        off += 5 + n
    return out


def summarise(path):
    s = sav.parse_sav(path)
    print(f"== {Path(path).name}   version {s['version']}")
    for f in s["fields"]:
        print(f"   {f.name:<12} {f.guess()}")
    print(f"   {'sections':<12} {len(s['sections'])}")
    for name, data in s["sections"]:
        if name.endswith(".HL3"):
            n, = struct.unpack_from("<i", data, 0)
            ids = struct.unpack_from(f"<{n}i", data, 4)
            print(f"     {name:<26} transitioned entities: {n} {list(ids)}")
            continue
        sec = sav.HLSection(name, data)
        blocks = ", ".join(b.name for b in sec.blocks)
        extra = ""
        if sec.kind == "server":
            extra = f"entities={len(sec.entity_table())}"
        else:
            extra = f"decals={sec.decal_count} player={sec.player_name!r}"
        print(f"     {name:<26} {sec.kind:<6} {len(data):>8}B  {extra}  [{blocks}]")
    return s


def show_map(s, want):
    for name, data in s["sections"]:
        if not name.startswith(want) or name.endswith(".HL3"):
            continue
        sec = sav.HLSection(name, data)
        print(f"\n== {name} ({sec.kind})")
        print(f"   symbols: {sum(1 for t in sec.tokens if t)} used / {len(sec.tokens)} slots")
        print(f"   blocks:  " + ", ".join(
            f"{b.name}(header@{b.loc_header} body@{b.loc_body})" for b in sec.blocks))
        if sec.kind == "server":
            print(f"   baseFilePos: {sec.base_file_pos}")
            print("   -- global preamble --")
            for f in sav.read_fields(sec.body, 0, sec.base_file_pos, sec.tokens):
                if f.name in ("style", "LIGHTSTYLE", "index"):
                    continue
                print(f"      {f.name:<20} {f.guess()[:70]}")
            ents = sec.entity_table()
            cls = Counter(e["classname"].s() for e in ents if "classname" in e)
            print(f"   -- {len(ents)} entities, {len(cls)} classes --")
            for c, n in cls.most_common(25):
                print(f"      {n:>5}  {c}")
        else:
            print(f"   decals: {sec.decal_count}")
            for rec in sec.decal_list()[:10]:
                print("      " + " ".join(f"{k}={v.guess()}" for k, v in rec.items()
                                          if k != "DECALLIST"))


def show_entity(s, want):
    for name, data in s["sections"]:
        if not name.endswith(".HL1"):
            continue
        sec = sav.HLSection(name, data)
        for e in sec.entity_table():
            cn = e["classname"].s() if "classname" in e else ""
            if want.lower() not in cn.lower():
                continue
            head = " ".join(f"{k}={v.guess()}" for k, v in e.items() if k != "classname")
            print(f"\n== {name}  {cn}  {head}")
            for f in sec.entity_fields(e):
                print(f"   {f.name:<48} {f.guess()[:90]}")


def show_python(s):
    for name, data in s["sections"]:
        if not name.endswith(".HL1"):
            continue
        sec = sav.HLSection(name, data)
        for i, (tag, ns) in enumerate(python_namespaces(sec)):
            if isinstance(ns, dict):
                print(f"\n== {name}  namespace {i} (tag {tag}, {len(ns)} keys)")
                for k in sorted(ns):
                    print(f"   {k:<40} {ns[k]}")
            else:
                print(f"\n== {name}  namespace {i} (tag {tag}) {ns}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("file")
    ap.add_argument("--map", help="show one section in detail (name prefix)")
    ap.add_argument("--entity", help="dump fields of entities whose classname matches")
    ap.add_argument("--python", action="store_true", help="dump the pickled namespaces")
    ap.add_argument("--extract", help="write every inflated section into this directory")
    a = ap.parse_args()

    s = summarise(a.file)
    if a.extract:
        out = Path(a.extract)
        out.mkdir(parents=True, exist_ok=True)
        for name, data in s["sections"]:
            (out / name).write_bytes(data)
        print(f"\nwrote {len(s['sections'])} sections to {out}")
    if a.map:
        show_map(s, a.map)
    if a.entity:
        show_entity(s, a.entity)
    if a.python:
        show_python(s)


if __name__ == "__main__":
    main()
