#!/usr/bin/env python3
"""Reconstruct every `datamap_t` in a VtMB module, record by record, with its field types.

The corpus field ledger (`fields-<module>.jsonl`) reads back `fieldType 0` for ~28k records,
because MSVC's static-init builders store most record slots from a register
(`MOV EAX,0x4 ... MOV [rec],EAX`) and only immediate stores were followed. This replays every
static-init function's straight-line code from the disassembly database with a register file,
so a register-sourced store resolves too, then reads each labelled `datamap_<Class>`:

    datamap_t          { typedescription_t *dataDesc; int dataNumFields;
                         const char *dataClassName; datamap_t *baseMap; }
    typedescription_t  (0x2C) { +0x00 fieldType; +0x04 fieldName; +0x08 fieldOffset[2];
                         +0x10 u16 fieldSize; +0x12 i16 flags; +0x14 externalName;
                         +0x18 ISaveRestoreOps *; +0x1C inputFunc; +0x20 datamap_t *td;
                         +0x24 fieldSizeInBytes (always 0 in VtMB); +0x28 override_field }

A slot no builder writes is read from the PE image (statically initialised data).

Usage
  uv run elysium research datamap_records [vampire.dll]

Writes `$ELYSIUM_WORK_ROOT/research/ghidra/types/datamap_records-<module>.json`:
`{Class: {"base": Base|null, "records": [{name, type, typeName, offset, count, flags,
external, ops, embedded}]}}`.
"""
import json
import re
import sqlite3
import struct
import sys
from pathlib import Path

from elysium_pipeline.paths import research_root

REC = 0x2C
FT = {0: "void", 1: "float", 2: "string", 3: "vector", 4: "int", 5: "bool", 6: "short",
      7: "char", 8: "color32", 9: "embedded", 10: "custom", 11: "classptr", 12: "ehandle",
      13: "edict", 14: "position", 15: "time", 16: "modelname", 17: "soundname",
      18: "input", 19: "function"}
FLAGS = {0x1: "GLOBAL", 0x2: "SAVE", 0x4: "KEY", 0x8: "INPUT", 0x10: "OUTPUT",
         0x20: "FUNCTIONTABLE", 0x40: "PTR", 0x80: "OVERRIDE"}

REG32 = ("EAX", "ECX", "EDX", "EBX", "ESP", "EBP", "ESI", "EDI")
SUB = {"AX": ("EAX", 0, 16), "CX": ("ECX", 0, 16), "DX": ("EDX", 0, 16), "BX": ("EBX", 0, 16),
       "SI": ("ESI", 0, 16), "DI": ("EDI", 0, 16), "BP": ("EBP", 0, 16),
       "AL": ("EAX", 0, 8), "CL": ("ECX", 0, 8), "DL": ("EDX", 0, 8), "BL": ("EBX", 0, 8),
       "AH": ("EAX", 8, 8), "CH": ("ECX", 8, 8), "DH": ("EDX", 8, 8), "BH": ("EBX", 8, 8)}


class PE:
    def __init__(self, path):
        self.d = Path(path).read_bytes()
        pe = struct.unpack_from("<I", self.d, 0x3C)[0]
        nsec = struct.unpack_from("<H", self.d, pe + 6)[0]
        opt = struct.unpack_from("<H", self.d, pe + 20)[0]
        self.base = struct.unpack_from("<I", self.d, pe + 24 + 28)[0]
        self.secs = []
        off = pe + 24 + opt
        for i in range(nsec):
            vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", self.d, off + i * 40 + 8)
            self.secs.append((self.base + vaddr, max(vsize, rsize), raddr, rsize))

    def u32(self, va):
        for start, size, raddr, rsize in self.secs:
            if start <= va < start + size:
                o = va - start
                if o + 4 <= rsize:
                    return struct.unpack_from("<I", self.d, raddr + o)[0]
                return 0
        return None

    def u16(self, va):
        v = self.u32(va)
        return None if v is None else v & 0xFFFF

    def cstr(self, va):
        for start, size, raddr, rsize in self.secs:
            if start <= va < start + size:
                o = raddr + (va - start)
                end = self.d.find(b"\x00", o, o + 512)
                if end < 0:
                    return None
                s = self.d[o:end]
                return s.decode("latin-1") if all(32 <= c < 127 for c in s) else None
        return None


LINE = re.compile(r"^([0-9a-f]{8})\s+(\S+)\s*(.*?)\s*(?:\"(.*)\")?$")
MEM = re.compile(r"^(?:(dword|word|byte) ptr )?\[0x([0-9a-f]+)\]$")


class Machine:
    """Straight-line replay of MOV/XOR/INC/DEC/ADD/SUB/LEA over a register file."""

    def __init__(self, mem):
        self.mem = mem          # va -> (value, width)
        self.r = {}

    def get(self, name):
        if name in REG32:
            return self.r.get(name)
        if name in SUB:
            full, sh, bits = SUB[name]
            v = self.r.get(full)
            return None if v is None else (v >> sh) & ((1 << bits) - 1)
        return None

    def set(self, name, v):
        if name in REG32:
            self.r[name] = None if v is None else v & 0xFFFFFFFF
        elif name in SUB:
            full, sh, bits = SUB[name]
            cur = self.r.get(full)
            if cur is None or v is None:
                self.r[full] = None
            else:
                mask = ((1 << bits) - 1) << sh
                self.r[full] = (cur & ~mask) | ((v << sh) & mask)

    def value(self, op):
        op = op.strip()
        if re.fullmatch(r"0x[0-9a-f]+", op):
            return int(op, 16)
        if re.fullmatch(r"-?\d+", op):
            return int(op) & 0xFFFFFFFF
        m = MEM.match(op)
        if m:
            got = self.mem.get(int(m.group(2), 16))
            return None if got is None else got[0]
        return self.get(op.upper())

    def store(self, dst, v, width_hint=None):
        m = MEM.match(dst.strip())
        if not m:
            return False
        width = {"dword": 4, "word": 2, "byte": 1}.get(m.group(1) or "", width_hint or 4)
        addr = int(m.group(2), 16)
        if v is None:
            self.mem[addr] = (None, width)
        else:
            self.mem[addr] = (v & ((1 << (8 * width)) - 1), width)
        return True

    def step(self, mnem, ops):
        parts = [p.strip() for p in re.split(r",(?![^\[]*\])", ops)] if ops else []
        m = mnem.upper()
        if m == "MOV" and len(parts) == 2:
            dst, src = parts
            v = self.value(src)
            if MEM.match(dst):
                reg_w = 4 if src.upper() in REG32 else (2 if src.upper() in SUB and
                                                        SUB[src.upper()][2] == 16 else
                                                        1 if src.upper() in SUB else None)
                self.store(dst, v, reg_w)
            else:
                self.set(dst.upper(), v)
        elif m == "XOR" and len(parts) == 2 and parts[0] == parts[1]:
            self.set(parts[0].upper(), 0)
        elif m in ("INC", "DEC") and len(parts) == 1 and not MEM.match(parts[0]):
            v = self.get(parts[0].upper())
            self.set(parts[0].upper(), None if v is None else v + (1 if m == "INC" else -1))
        elif m in ("ADD", "SUB", "OR", "AND") and len(parts) == 2 and not MEM.match(parts[0]):
            a, b = self.get(parts[0].upper()), self.value(parts[1])
            if a is None or b is None:
                self.set(parts[0].upper(), None)
            else:
                self.set(parts[0].upper(), {"ADD": a + b, "SUB": a - b, "OR": a | b,
                                            "AND": a & b}[m])
        elif m == "LEA" and len(parts) == 2:
            mm = re.fullmatch(r"\[0x([0-9a-f]+)\]", parts[1])
            self.set(parts[0].upper(), int(mm.group(1), 16) if mm else None)
        elif m in ("CALL",):
            for r in ("EAX", "ECX", "EDX"):
                self.r[r] = None
        elif m == "POP" and parts:
            self.set(parts[0].upper(), None)
        elif parts and not MEM.match(parts[0]) and parts[0].upper() in REG32 + tuple(SUB) \
                and m not in ("PUSH", "CMP", "TEST", "JMP") and not m.startswith("J"):
            self.set(parts[0].upper(), None)      # an op we do not model clobbers its dest


def replay(module):
    corpus = research_root() / "ghidra" / "corpus"
    c = sqlite3.connect(corpus / "corpus.sqlite")
    funcs = [a for (a,) in c.execute(
        "select addr from functions where module=? and (name like 'datamap_%builder' "
        "or name like 'staticinit_%')", (module,))]
    lst = sqlite3.connect(corpus / "listing.sqlite")
    mem = {}
    for addr in funcs:
        row = lst.execute("select asm from listing where module=? and addr=?",
                          (module, addr)).fetchone()
        if not row:
            continue
        mach = Machine(mem)
        for line in row[0].splitlines():
            mm = LINE.match(line.strip())
            if not mm:
                continue
            mach.step(mm.group(2), mm.group(3))
    maps = {}
    for addr, name in c.execute("select addr, name from globals where module=? and "
                                "name like 'datamap\\_%' escape '\\'", (module,)):
        if name.endswith("_builder"):
            continue
        maps[int(addr, 16)] = re.sub(r"_[0-9a-f]{8}$", "", name[len("datamap_"):])
    vtables = {int(a, 16): n[len("vftable_"):] for a, n in c.execute(
        "select addr, name from globals where module=? and name like 'vftable\\_%' escape '\\'",
        (module,))}
    binary = c.execute("select binary from meta where module=?", (module,)).fetchone()[0]
    return mem, maps, vtables, PE(binary)


def read_maps(module):
    mem, maps, vtables, pe = replay(module)

    def u32(va):
        got = mem.get(va)
        if got is not None and got[1] == 4:
            return got[0]
        if got is not None and got[1] == 2:
            hi = mem.get(va + 2)
            if hi and hi[0] is not None and got[0] is not None:
                return got[0] | (hi[0] << 16)
        return pe.u32(va)

    def u16(va):
        got = mem.get(va)
        if got is not None and got[1] == 2:
            return got[0]
        if got is not None and got[1] == 4:
            return None if got[0] is None else got[0] & 0xFFFF
        hi = mem.get(va - 2)
        if hi is not None and hi[1] == 4:
            return None if hi[0] is None else hi[0] >> 16
        return pe.u16(va)

    def string(va):
        return pe.cstr(va) if va else None

    by_addr = {}
    out = {}
    for maddr, cls in sorted(maps.items()):
        recs, count = u32(maddr), u32(maddr + 4)
        cname = string(u32(maddr + 8) or 0) or cls
        base = u32(maddr + 12)
        by_addr[maddr] = cname
        out.setdefault(cname, []).append((maddr, recs, count, base))
    result = {}
    for cname, variants in out.items():
        # a class with several maps (address-qualified labels): keep the one with records
        maddr, recs, count, base = max(variants, key=lambda v: (v[2] or 0))
        rows = []
        if recs and count and 0 < count < 4096:
            for i in range(count):
                r = recs + i * REC
                t = u32(r)
                nm = string(u32(r + 4) or 0)
                ops = u32(r + 0x18)
                ops_cls = None
                if ops:
                    vt = u32(ops)
                    ops_cls = vtables.get(vt) if vt else None
                td = u32(r + 0x20)
                flags = u16(r + 0x12)
                rows.append({
                    "name": nm, "type": t, "typeName": FT.get(t) if t is not None else None,
                    "offset": u32(r + 8), "count": u16(r + 0x10), "flags": flags,
                    "flagNames": [n for b, n in FLAGS.items() if flags and flags & b],
                    "external": string(u32(r + 0x14) or 0),
                    "ops": ops_cls or (("0x%08x" % ops) if ops else None),
                    "embedded": by_addr.get(td) if td else None,
                })
        result[cname] = {"datamap": "0x%08x" % maddr, "base": by_addr.get(base) if base else None,
                         "records": rows}
    return result


def find_named_records(binary, names, mem=None):
    """Records located by their field-name string rather than by a datamap label.

    For modules the census did not label (engine.dll's save headers, vphysics.dll's
    `vphysics_save_*`) and for `WriteFields` arrays that are not a `datamap_t` (`PEvent`,
    `EventQueue`): every dword — in the image, or stored by a replayed builder — that points at
    a wanted name string is taken as a record's `fieldName` slot, and the record is read
    around it. Returns {name: [record, ...]} (a name can live in several structures).
    """
    pe = PE(binary)
    mem = mem or {}
    str_va = {}
    for nm in names:
        needle = b"\x00" + nm.encode("latin-1") + b"\x00"
        start = 0
        while True:
            i = pe.d.find(needle, start)
            if i < 0:
                break
            for s_va, size, raddr, rsize in pe.secs:
                if raddr <= i + 1 < raddr + rsize:
                    str_va.setdefault(s_va + (i + 1 - raddr), nm)
            start = i + 1
    hits = []
    for s_va, size, raddr, rsize in pe.secs:
        for o in range(0, rsize - 3, 4):
            v = struct.unpack_from("<I", pe.d, raddr + o)[0]
            if v in str_va:
                hits.append((s_va + o, str_va[v]))
    for addr, (v, w) in mem.items():
        if w == 4 and v in str_va:
            hits.append((addr, str_va[v]))

    def u32(va):
        got = mem.get(va)
        return got[0] if got is not None and got[1] == 4 else pe.u32(va)

    out = {}
    for slot, nm in hits:
        r = slot - 4
        t = u32(r)
        if t is None or not 1 <= t <= 19:
            continue
        cnt = (u32(r + 0x10) or 0) & 0xFFFF
        off = u32(r + 8)
        if not 1 <= cnt <= 4096 or off is None or off > 0x100000:
            continue
        flags = ((u32(r + 0x10) or 0) >> 16) & 0xFFFF
        rec = {"name": nm, "type": t, "typeName": FT[t], "offset": off, "count": cnt,
               "flags": flags, "flagNames": [n for b, n in FLAGS.items() if flags & b],
               "external": pe.cstr(u32(r + 0x14) or 0), "record": "0x%08x" % r}
        if rec not in out.setdefault(nm, []):
            out[nm].append(rec)
    return out


def main():
    module = sys.argv[1] if len(sys.argv) > 1 else "vampire.dll"
    if len(sys.argv) > 3 and sys.argv[2] == "--names":
        # e.g. datamap_records engine.dll --names mapName,skyName,... (or @file with one per line)
        spec = sys.argv[3]
        names = (Path(spec[1:]).read_text(encoding="utf-8").split() if spec.startswith("@")
                 else spec.split(","))
        corpus = research_root() / "ghidra" / "corpus"
        c = sqlite3.connect(corpus / "corpus.sqlite")
        row = c.execute("select binary from meta where module=?", (module,)).fetchone()
        binary = row[0] if row and row[0] else sys.argv[4]
        found = find_named_records(binary, names)
        out = research_root() / "ghidra" / "types" / f"named_records-{module}.json"
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(found, indent=1), encoding="utf-8")
        print(f"{module}: {len(found)}/{len(names)} names located -> {out}")
        return
    result = read_maps(module)
    out = research_root() / "ghidra" / "types" / f"datamap_records-{module}.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(result, indent=1), encoding="utf-8")
    total = sum(len(v["records"]) for v in result.values())
    untyped = sum(1 for v in result.values() for r in v["records"] if not r["type"])
    unnamed = sum(1 for v in result.values() for r in v["records"] if not r["name"])
    print(f"{module}: {len(result)} datamaps, {total} records, {untyped} without a type, "
          f"{unnamed} without a name -> {out}")


if __name__ == "__main__":
    main()
