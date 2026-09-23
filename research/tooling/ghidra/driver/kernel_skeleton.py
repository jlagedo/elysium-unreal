#!/usr/bin/env python3
"""The mechanical skeleton of a retail function: pass R's tier 0, spec 0019 story 8.

A reading packet has two halves. One half is structure — every conditional branch and both its
targets, every memory operand and whose field it is, every global and its value, every call with
its thunks followed and its virtual slot named — and a model transcribing that gets it wrong in
ways the pilot measured (a misread `JNZ`, a push handed to the wrong callee). The other half is
meaning, which only a reader can supply. This tool writes the first half from the listing
database and the image, complete by construction, so the reader is only ever asked for the second.

Per function it emits, in the order a reader walks them:

* **Arm inventory** — one row per conditional jump (address, mnemonic, taken target, fall-through),
  every `RET`, and every indirect `JMP` through a table with the table's entries read from the
  image while they fall inside the function.
* **Reads / Writes** — every memory operand, with the base register resolved to `this`, to a
  word loaded off `this` (`[this+0x5d34]+0x18`), or left raw. A `this`-relative offset is joined to
  the datamap (`CAI_Navigator::m_navType int`) through the function's own class and its base chain,
  to `layout.tsv`'s recovered member, and to the port's shape-map member.
* **Globals** — every absolute operand with its value read from `vampire.dll` at the width the
  instruction used (`float ptr` → float32, `double ptr` → float64), or "zero-initialised" when
  the address is in `.bss`.
* **Calls** — every `CALL`, thunks followed to the body that has one, a `CALL [reg+0xNNN]` turned
  into slot `NNN/4` and named from the port's slot table when the vtable came off `this`, a
  `CALL [0xADDR]` through a function pointer resolved from the image.

What it does not do, on purpose: say what a comparison means, map pushes to callee parameters
(that needs the callee's `RET n`), or follow into a callee. Those are the reader's rows and the
brief asks for exactly them.

Modes::

    uv run elysium research kernel_skeleton 0x10271900               # one function, to stdout
    uv run elysium research kernel_skeleton --family Conditions19    # one family file → md + json
    uv run elysium research kernel_skeleton --all                    # every family under --families
    uv run elysium research kernel_skeleton --check                  # every family row has a section

The reading side of pass R — briefs, walk checks, walk diffs, drill briefs, the packets and their
check — are subcommands of this same command, implemented in `kernel_packet.py`::

    uv run elysium research kernel_skeleton brief --band small --batch 10 --out <dir>
    uv run elysium research kernel_skeleton check-walk <list.tsv> <walk.md>
    uv run elysium research kernel_skeleton diff <list.tsv> <walkA.md> <walkB.md>
    uv run elysium research kernel_skeleton drill-brief <list.tsv> <walkA.md> <walkB.md> --out <brief.md>
    uv run elysium research kernel_skeleton packet --family Conditions19 --a <walks> --b <walks>
    uv run elysium research kernel_skeleton contra-brief --family Conditions19 --out <brief.md>
    uv run elysium research kernel_skeleton check-packet --all

Family row files are `$ELYSIUM_WORK_ROOT/research/npc-kernel-checklist/families-19-29/<Family>.tsv`
(first column the address); output goes beside them under `skeletons-19-29/<Family>.md` and
`.json`. The JSON is the diff key for pass R's two readers: arms and calls by instruction address.
"""

from __future__ import annotations

import argparse
import json
import re
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

_HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(_HERE))

import corpus  # noqa: E402

from elysium_pipeline.paths import repo_root, research_root, vtmb_root  # noqa: E402

MODULE = "vampire.dll"
COND_JUMPS = frozenset({
    "JZ", "JNZ", "JE", "JNE", "JA", "JAE", "JB", "JBE", "JG", "JGE", "JL", "JLE", "JS", "JNS",
    "JC", "JNC", "JO", "JNO", "JP", "JNP", "JPE", "JPO", "JCXZ", "JECXZ",
})
# Instructions whose FIRST operand is written. Everything else with a memory operand reads it.
WRITE_FIRST = frozenset({
    "MOV", "MOVSS", "MOVSD", "MOVZX", "MOVSX", "LEA", "AND", "OR", "XOR", "ADD", "SUB", "INC",
    "DEC", "NEG", "NOT", "SHL", "SHR", "SAR", "FST", "FSTP", "FIST", "FISTP", "FNSTSW", "FNSTCW",
    "SETZ", "SETNZ", "SETE", "SETNE", "SETG", "SETGE", "SETL", "SETLE", "SETA", "SETAE", "SETB",
    "SETBE", "POP", "STOSD", "STOSB", "CMPXCHG", "XCHG", "IMUL",
})
# Read-modify-write: the first operand is read as well as written.
READ_WRITE_FIRST = frozenset({
    "AND", "OR", "XOR", "ADD", "SUB", "INC", "DEC", "NEG", "NOT", "SHL", "SHR", "SAR", "IMUL",
    "CMPXCHG", "XCHG",
})
WIDTH_FMT = {"byte": ("<B", 1), "word": ("<H", 2), "dword": ("<I", 4), "float": ("<f", 4),
             "double": ("<d", 8), "qword": ("<Q", 8)}
REGS32 = ("EAX", "EBX", "ECX", "EDX", "ESI", "EDI", "EBP", "ESP")
MEM_RE = re.compile(r"(?:(byte|word|dword|qword|float|double|tbyte) ptr )?\[([^\]]+)\]")
INS_RE = re.compile(r"^([0-9a-f]{8})\s+(\S+)(?:\s+(.*))?$")
SHAPE_RE = re.compile(r"ELYSIUM_NPC_WORD(?:_NOTED|_PRIVATE|_CHAIN|_IMPLICIT|_ABSENT)?\("
                      r"(0x[0-9a-fA-F]+),\s*([^,)]+)(?:,\s*([^,)]+))?")
SLOT_RE = re.compile(r"//\s*slot (\d+) (0x[0-9a-f]{8})(?: \((\w+)\))? `([^`]*)`")


# ---------------------------------------------------------------------------------------------
# The image: section table and typed reads.


class Image:
    """`vampire.dll` as bytes, with the PE section table to turn a VA into a file offset."""

    def __init__(self, path: Path):
        data = path.read_bytes()
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        count = struct.unpack_from("<H", data, pe + 6)[0]
        opt = struct.unpack_from("<H", data, pe + 20)[0]
        self.base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
        self.data = data
        self.sections: list[tuple[str, int, int, int, int]] = []
        for i in range(count):
            at = pe + 24 + opt + i * 40
            name = data[at:at + 8].rstrip(b"\0").decode("ascii", "replace")
            vsize, va, rsize, roff = struct.unpack_from("<IIII", data, at + 8)
            self.sections.append((name, va, vsize, rsize, roff))

    def read(self, va: int, width: str) -> tuple[str | None, object]:
        rva = va - self.base
        for name, v, vsize, rsize, roff in self.sections:
            if v <= rva < v + vsize:
                if rva - v >= rsize:
                    return name, "zero-initialised (not in the file)"
                fmt, size = WIDTH_FMT.get(width, ("<I", 4))
                return name, struct.unpack_from(fmt, self.data, roff + (rva - v))[0]
        return None, "outside every section"

    def dwords_while(self, va: int, keep) -> list[int]:
        """Consecutive dwords from `va` while `keep(value)` holds — a jump table's entries."""
        out = []
        while True:
            _, value = self.read(va + 4 * len(out), "dword")
            if not isinstance(value, int) or not keep(value) or len(out) > 512:
                return out
            out.append(value)


# ---------------------------------------------------------------------------------------------
# The name sources: datamap, layout, shape map, slot table.


class Names:
    def __init__(self):
        self.datamap = self._datamap()
        self.layout = self._layout()
        self.shape = self._shape()
        self.slots = self._slots()

    @staticmethod
    def _datamap() -> dict:
        path = research_root() / "ghidra" / "types" / f"datamap_records-{MODULE}.json"
        return json.loads(path.read_text(encoding="utf-8")) if path.is_file() else {}

    @staticmethod
    def _layout() -> dict[int, tuple[str, str]]:
        """`layout.tsv`: the recovered NPC member at each Troika-layout offset."""
        path = repo_root() / "docs" / "vtmb" / "npc-kernel" / "layout.tsv"
        out: dict[int, tuple[str, str]] = {}
        if not path.is_file():
            return out
        rows = path.read_text(encoding="utf-8").splitlines()
        head = rows[0].split("\t")
        for line in rows[1:]:
            cells = dict(zip(head, line.split("\t")))
            try:
                off = int(cells["offset"], 16)
            except (KeyError, ValueError):
                continue
            out.setdefault(off, (cells.get("member", ""), cells.get("type", "")))
        return out

    @staticmethod
    def _shape() -> dict[int, str]:
        path = (repo_root() / "Source" / "ElysiumUE" / "Private" / "Substrate"
                / "ElysiumNpcKernelShapeMap.cpp")
        out: dict[int, str] = {}
        if not path.is_file():
            return out
        for m in SHAPE_RE.finditer(path.read_text(encoding="utf-8")):
            off = int(m.group(1), 16)
            second, third = m.group(2).strip(), (m.group(3) or "").strip()
            if second.startswith('"'):
                out[off] = second.strip('"')                       # CHAIN / PRIVATE: a path
            elif third and not third.startswith('"'):
                out[off] = f"{second}::{third}"                    # WORD / NOTED: type, member
            else:
                out[off] = second
        return out

    @staticmethod
    def _slots() -> dict[int, tuple[str, str, str]]:
        """slot → (base body address, walk state, retail signature) from the port's slot table."""
        path = (repo_root() / "Source" / "ElysiumUE" / "Private" / "Substrate"
                / "ElysiumNpcKernelSlots.inl")
        out: dict[int, tuple[str, str, str]] = {}
        if not path.is_file():
            return out
        for m in SLOT_RE.finditer(path.read_text(encoding="utf-8")):
            out.setdefault(int(m.group(1)), (m.group(2), m.group(3) or "", m.group(4)))
        return out

    def field(self, cls: str | None, off: int) -> tuple[str, str, str, str | None] | None:
        """(class, name, type, embedded class) for `off` in `cls` or its base chain."""
        if off == 0:
            return None                      # the vtable pointer, never a datamap field
        seen = 0
        while cls and cls in self.datamap and seen < 16:
            for r in self.datamap[cls].get("records", ()):
                # Input and output records carry offset 0 or a handler, not a field.
                if r.get("offset") == off and not ({"INPUT", "OUTPUT"} & set(r.get("flagNames") or ())):
                    return cls, r["name"], r.get("typeName", ""), r.get("embedded")
            cls = self.datamap[cls].get("base")
            seen += 1
        return None

    def describe(self, cls: str | None, off: int, is_npc: bool) -> str:
        if off == 0:
            return "vtable pointer"
        parts = []
        f = self.field(cls, off)
        if f:
            parts.append(f"{f[0]}::{f[1]} {f[2]}" + (f" → {f[3]}" if f[3] else ""))
        if is_npc and off in self.layout:
            member, typ = self.layout[off]
            if not f or f[1] != member:
                parts.append(f"layout {member} {typ}".rstrip())
        if is_npc and off in self.shape:
            parts.append(f"port {self.shape[off]}")
        return "; ".join(parts) if parts else "(no datamap, layout or shape row)"


# ---------------------------------------------------------------------------------------------
# The skeleton.


@dataclass
class Skeleton:
    addr: int
    label: str
    cls: str | None
    size: int
    count: int
    arms: list[dict] = field(default_factory=list)
    rets: list[int] = field(default_factory=list)
    tables: list[dict] = field(default_factory=list)
    reads: list[dict] = field(default_factory=list)
    writes: list[dict] = field(default_factory=list)
    globals_: list[dict] = field(default_factory=list)
    calls: list[dict] = field(default_factory=list)
    damaged: str = ""

    def to_json(self) -> dict:
        return {"addr": f"0x{self.addr:08x}", "label": self.label, "class": self.cls,
                "size": self.size, "instructions": self.count, "damaged": self.damaged,
                "arms": self.arms, "rets": [f"0x{r:08x}" for r in self.rets],
                "tables": self.tables, "reads": self.reads, "writes": self.writes,
                "globals": self.globals_, "calls": self.calls}

    def to_markdown(self) -> str:
        out = [f"## 0x{self.addr:08x} {self.label}", "",
               f"class `{self.cls or '—'}` · {self.size} bytes · {self.count} instructions"
               + (f" · **{self.damaged}**" if self.damaged else ""), ""]
        out += ["### Arm inventory", "", "| branch | op | taken → | not taken → |", "|---|---|---|---|"]
        out += [f"| {a['at']} | {a['op']} | {a['taken']} | {a['next']} |" for a in self.arms]
        out.append("")
        out.append("Returns at: " + (", ".join(f"0x{r:08x}" for r in self.rets) or "none") + ".")
        for t in self.tables:
            out.append(f"Jump table at {t['at']} through `{t['operand']}`: table {t['table']}, "
                       f"{len(t['entries'])} entries inside the body"
                       + (f", byte index table {t['index_table']}" if t.get("index_table") else "")
                       + ": " + ", ".join(t["entries"]))
        out += ["", "### Reads", ""]
        out += [f"- {r['at']} {r['op']:6} {r['width']:6} {r['what']}" for r in self.reads] or ["- none"]
        out += ["", "### Writes", ""]
        out += [f"- {w['at']} {w['op']:6} {w['width']:6} {w['what']}" for w in self.writes] or ["- none"]
        out += ["", "### Globals", ""]
        out += [f"- {g['at']} {g['op']:6} {g['rw']:5} {g['va']} ({g['width']}, {g['section']}) = {g['value']}"
                for g in self.globals_] or ["- none"]
        out += ["", "### Calls", ""]
        out += [f"- {c['at']} {c['what']}" for c in self.calls] or ["- none"]
        out.append("")
        return "\n".join(out)


class Builder:
    def __init__(self, image: Image | None, names: Names):
        self.conn = corpus._connect()
        self.conn.row_factory = corpus.sqlite3.Row
        self.listing = corpus._listing_connection()
        self.image = image
        self.names = names

    # --- corpus lookups -----------------------------------------------------------------------

    def function(self, addr: int):
        return self.conn.execute("SELECT * FROM functions WHERE module=? AND addr=?",
                                 (MODULE, f"{addr:08x}")).fetchone()

    def resolve_call(self, addr: int) -> tuple[int, str, list[str]]:
        """Follow thunks to the body that has one: (final address, label, chain of hops).

        The edge table records no edge out of a thunk, so a thunk is followed through its own
        listing: a thunk is one `JMP 0x........`, and the JMP's target is the next hop.
        """
        hops: list[str] = []
        cur = addr
        for _ in range(6):
            row = self.function(cur)
            if row is None:
                return cur, "(not a corpus function)", hops
            label = corpus._label(row).split(None, 2)[2]
            if not row["thunk"]:
                return cur, label, hops
            asm = self.listing.execute("SELECT asm FROM listing WHERE module=? AND addr=?",
                                       (MODULE, f"{cur:08x}")).fetchone()
            m = re.search(r"\sJMP 0x([0-9a-f]{8})\b", asm["asm"]) if asm else None
            if m is None:
                return cur, label, hops
            hops.append(f"0x{cur:08x}")
            cur = int(m.group(1), 16)
        return cur, label, hops

    # --- the walk -----------------------------------------------------------------------------

    def build(self, addr: int) -> Skeleton:
        row = self.function(addr)
        if row is None:
            raise SystemExit(f"0x{addr:08x} is not a function in the corpus")
        label = corpus._label(row).split(None, 2)[2]
        cls = row["ns"] if row["ns"] not in ("", "Global") else None
        asm_row = self.listing.execute("SELECT asm FROM listing WHERE module=? AND addr=?",
                                       (MODULE, row["addr"])).fetchone()
        if asm_row is None:
            raise SystemExit(f"0x{addr:08x} is not in the listing database")
        ins = []
        for line in asm_row["asm"].splitlines():
            m = INS_RE.match(line.strip())
            if m:
                ins.append((int(m.group(1), 16), m.group(2).upper(), (m.group(3) or "").strip()))
        severe = corpus._severe(row["warn"] or "")
        sk = Skeleton(addr, label, cls, row["size"], len(ins),
                      damaged=("damaged decompilation: " + "; ".join(severe)) if severe else "")
        end = addr + (row["size"] or 0)
        nxt = {a: (ins[i + 1][0] if i + 1 < len(ins) else None) for i, (a, _, _) in enumerate(ins)}
        is_npc = self._is_npc(cls)

        this_regs = {"ECX"}                     # __thiscall / __fastcall: `this` arrives in ECX
        alias: dict[str, tuple[str, str | None]] = {}   # reg → (text, class of the pointee)
        vtable_of: dict[str, str] = {}          # reg → "this" | alias text, when reg = [obj]
        last_index_table: str | None = None

        for a, op, args in ins:
            at = f"0x{a:08x}"
            if op in COND_JUMPS:
                sk.arms.append({"at": at, "op": op, "taken": f"0x{int(args, 16):08x}",
                                "next": f"0x{nxt[a]:08x}" if nxt[a] else "—"})
            elif op == "RET":
                sk.rets.append(a)
            elif op == "JMP" and re.fullmatch(r"0x[0-9a-f]+", args) and not (addr <= int(args, 16) < end):
                # A direct tail call: the body forwards to another function and returns its result.
                call = self._call(args, this_regs, alias, vtable_of)
                call["kind"] = "tail-" + call["kind"]
                call["what"] = "tail-jump " + call["what"]
                sk.calls.append({"at": at, **call})
            elif op == "JMP" and "ptr [" in args:
                tail = re.fullmatch(r"dword ptr \[(\w+) \+ (0x[0-9a-f]+)\]", args)
                if tail:
                    # A virtual TAIL call in an epilogue — what Ghidra reports as an unrecovered
                    # jump table on the band's damaged rows; `0xNNN / 4` is the slot.
                    call = self._call(args, this_regs, alias, vtable_of)
                    call["kind"] = "tail-" + call["kind"]
                    call["what"] = "tail-jump " + call["what"]
                    sk.calls.append({"at": at, **call})
                    continue
                m = re.search(r"\[(?:(\w+)\*0x4 \+ )?(0x[0-9a-f]+)\]", args)
                if m and self.image:
                    table = int(m.group(2), 16)
                    entries = self.image.dwords_while(table, lambda v: addr <= v < end)
                    rec = {"at": at, "operand": args, "table": f"0x{table:08x}",
                           "entries": [f"0x{e:08x}" for e in entries],
                           "index_table": None, "cases": []}
                    rec.update(self._switch_cases(ins, a, entries))
                    sk.tables.append(rec)

            if op == "CALL":
                sk.calls.append({"at": at, **self._call(args, this_regs, alias, vtable_of)})
                # Caller-saved registers are dead after a call; `this` may survive in ESI/EDI/EBX/EBP.
                for r in ("EAX", "ECX", "EDX"):
                    this_regs.discard(r)
                    alias.pop(r, None)
                    vtable_of.pop(r, None)
                continue

            # Memory operands: classify, resolve, name.
            for i, mm in enumerate(MEM_RE.finditer(args)):
                width = mm.group(1) or "dword"
                inner = mm.group(2)
                first = args.startswith(mm.group(0))
                is_write = first and op in WRITE_FIRST and op != "LEA"
                is_read = (not is_write) or (op in READ_WRITE_FIRST)
                if op == "LEA":
                    is_read = False
                parts = [p.strip() for p in re.split(r"\s*\+\s*", inner)]
                base = parts[0]
                disp = 0
                if len(parts) > 1 and re.fullmatch(r"-?0x[0-9a-f]+", parts[-1]):
                    disp = int(parts[-1], 16)
                if re.fullmatch(r"0x[0-9a-f]+", base) and len(parts) == 1:
                    va = int(base, 16)
                    section, value = self.image.read(va, width) if self.image else (None, "no image")
                    sk.globals_.append({"at": at, "op": op, "va": f"0x{va:08x}", "width": width,
                                        "section": section or "?", "value": self._fmt(value),
                                        "rw": "write" if is_write else "read"})
                    continue
                what = self._operand(base, parts, disp, this_regs, alias, is_npc, cls)
                entry = {"at": at, "op": op, "width": width, "what": what}
                if is_write:
                    sk.writes.append(entry)
                if is_read:
                    sk.reads.append(entry)

            # Register tracking, after the operands were classified.
            self._track(op, args, this_regs, alias, vtable_of, cls, is_npc)
        return sk

    # --- helpers ------------------------------------------------------------------------------

    def _switch_cases(self, ins, jump_at: int, entries: list[int]) -> dict:
        """The MSVC switch shape behind an indirect JMP, read back from the eight instructions
        before it: `LEA/SUB reg,base` (the first case id), `CMP reg,N` + `JA default` (the last),
        and for a two-level switch `MOV xL,byte ptr [reg + IDX]` (the byte table that maps an id
        to an arm index). Returns the id → arm map so a reader never has to count cases."""
        idx = next(i for i, (a, _, _) in enumerate(ins) if a == jump_at)
        window = ins[max(0, idx - 8):idx]
        first, bound, index_table, default = 0, None, None, None
        for a, op, args in window:
            # `LEA ECX,[EAX + -0x5]` is id - 5, so the first case id is 5; `SUB EAX,0x5` likewise.
            m = re.fullmatch(r"\w+,\[\w+ \+ (-?)(0x[0-9a-f]+)\]", args)
            if op == "LEA" and m:
                first = int(m.group(2), 16) if m.group(1) else -int(m.group(2), 16)
            m = re.fullmatch(r"\w+,(-?0x[0-9a-f]+|-?\d+)", args)
            if op == "SUB" and m:
                first = int(m.group(1), 0)
            if op == "CMP" and m:
                bound = int(m.group(1), 0)
            if op == "JA":
                default = int(args, 16)
            m = re.fullmatch(r"[A-D]L,byte ptr \[\w+ \+ (0x[0-9a-f]+)\]", args)
            if op in ("MOV", "MOVZX") and m:
                index_table = int(m.group(1), 16)
        cases = []
        if bound is not None and self.image:
            for i in range(bound + 1):
                cid = first + i
                if index_table is not None:
                    _, arm = self.image.read(index_table + i, "byte")
                    target = entries[arm] if isinstance(arm, int) and arm < len(entries) else None
                else:
                    target = entries[i] if i < len(entries) else None
                if target is not None and (default is None or target != default):
                    cases.append({"id": cid, "arm": f"0x{target:08x}"})
        return {"index_table": f"0x{index_table:08x}" if index_table else None,
                "first_id": first, "bound": bound,
                "default": f"0x{default:08x}" if default else None, "cases": cases}

    def _is_npc(self, cls: str | None) -> bool:
        seen = 0
        while cls and seen < 16:
            if cls in ("CAI_BaseNPC", "CAI_BaseNPCTroika"):
                return True
            cls = self.names.datamap.get(cls, {}).get("base")
            seen += 1
        return False

    def _operand(self, base, parts, disp, this_regs, alias, is_npc, cls) -> str:
        if base in this_regs:
            return f"this+0x{disp:04x}  {self.names.describe(cls, disp, is_npc)}"
        if base in alias:
            text, pointee = alias[base]
            named = self.names.describe(pointee, disp, False) if pointee else ""
            return f"[{text}]+0x{disp:04x}" + (f"  {named}" if named and not named.startswith("(no") else "")
        return " + ".join(parts)

    def _track(self, op, args, this_regs, alias, vtable_of, cls, is_npc) -> None:
        m = re.fullmatch(r"(E[A-D]X|E[SD]I|EBP),\s*(E[A-D]X|E[SD]I|EBP)", args)
        if op == "MOV" and m:
            dst, src = m.group(1), m.group(2)
            if src in this_regs:
                this_regs.add(dst)
            else:
                this_regs.discard(dst)
            alias.pop(dst, None)
            if src in alias:
                alias[dst] = alias[src]
            vtable_of.pop(dst, None)
            if src in vtable_of:
                vtable_of[dst] = vtable_of[src]
            return
        m = re.fullmatch(r"(\w+),\s*dword ptr \[(\w+)(?: \+ (0x[0-9a-f]+))?\]", args)
        if op == "MOV" and m:
            dst, base, off = m.group(1), m.group(2), int(m.group(3) or "0", 16)
            this_regs.discard(dst)
            alias.pop(dst, None)
            vtable_of.pop(dst, None)
            if base in this_regs:
                if off == 0:
                    vtable_of[dst] = "this"
                else:
                    f = self.names.field(cls, off) if is_npc else None
                    alias[dst] = (f"this+0x{off:04x}", f[3] if f else None)
            elif base in alias:
                text, pointee = alias[base]
                if off == 0:
                    vtable_of[dst] = text
                else:
                    f = self.names.field(pointee, off) if pointee else None
                    alias[dst] = (f"{text}+0x{off:04x}", f[3] if f else None)
            return
        # `POP` is left alone on purpose: a `PUSH reg … POP reg` around a call restores the
        # register, and an early-return epilogue's `POP ESI; POP EDI` precedes a `RET`, after
        # which the next instruction is a new block that still holds the same `this`.
        if op in ("MOV", "LEA", "MOVZX", "MOVSX", "XOR", "AND", "OR", "ADD", "SUB", "IMUL"):
            dst = args.split(",")[0].strip()
            if dst in REGS32:
                this_regs.discard(dst)
                alias.pop(dst, None)
                vtable_of.pop(dst, None)

    def _call(self, args, this_regs, alias, vtable_of) -> dict:
        m = re.fullmatch(r"dword ptr \[(\w+) \+ (0x[0-9a-f]+)\]", args)
        if m:
            reg, off = m.group(1), int(m.group(2), 16)
            slot = off // 4
            owner = vtable_of.get(reg)
            if owner == "this":
                base, state, sig = self.names.slots.get(slot, ("?", "", ""))
                return {"kind": "virtual", "slot": slot, "receiver": "this",
                        "what": f"virtual slot {slot} (vtable+0x{off:x}) on this — base body {base}"
                                + (f" ({state})" if state else "") + (f" `{sig}`" if sig else "")}
            return {"kind": "virtual", "slot": slot, "receiver": owner or reg,
                    "what": f"virtual slot {slot} (vtable+0x{off:x}) on {owner or ('the object in ' + reg)}"}
        m = re.fullmatch(r"dword ptr \[(0x[0-9a-f]+)\]", args)
        if m:
            ptr = int(m.group(1), 16)
            _, value = self.image.read(ptr, "dword") if self.image else (None, None)
            if isinstance(value, int):
                final, label, hops = self.resolve_call(value)
                return {"kind": "pointer", "target": f"0x{final:08x}",
                        "what": f"through pointer [0x{ptr:08x}] = 0x{value:08x} → 0x{final:08x} {label}"}
            return {"kind": "pointer", "target": None, "what": f"through pointer [0x{ptr:08x}] (unreadable)"}
        m = re.fullmatch(r"0x([0-9a-f]+)", args)
        if m:
            target = int(m.group(1), 16)
            final, label, hops = self.resolve_call(target)
            via = (" via " + " → ".join(hops[1:])) if len(hops) > 1 else ""
            thunk = " (thunk)" if hops else ""
            return {"kind": "direct", "target": f"0x{final:08x}",
                    "what": f"0x{target:08x}{thunk}{via} → 0x{final:08x} {label}"}
        return {"kind": "other", "target": None, "what": args}

    @staticmethod
    def _fmt(value) -> str:
        if isinstance(value, float):
            return repr(value)
        if isinstance(value, int):
            return f"{value} (0x{value:08x})"
        return str(value)


# ---------------------------------------------------------------------------------------------
# Families.


def families_dir(explicit: str | None) -> Path:
    if explicit:
        return Path(explicit)
    return research_root() / "npc-kernel-checklist" / "families-19-29"


def family_rows(path: Path) -> list[int]:
    out = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        cell = line.split("\t")[0].strip().lower()
        if cell.startswith("0x"):
            cell = cell[2:]
        if re.fullmatch(r"[0-9a-f]{8}", cell):
            out.append(int(cell, 16))
    return out


def write_family(builder: Builder, fam: Path, out_dir: Path) -> tuple[int, int]:
    rows = family_rows(fam)
    skeletons = [builder.build(a) for a in rows]
    out_dir.mkdir(parents=True, exist_ok=True)
    name = fam.stem
    md = [f"<!-- generated by `uv run elysium research kernel_skeleton --family {name}`; do not hand-edit -->",
          f"# Skeletons — {name}", "",
          f"{len(skeletons)} functions from `{fam.name}`. Structure only: arms, operands, globals, "
          "calls. The reader supplies meaning, argument mapping and callee effects.", ""]
    md += [s.to_markdown() for s in skeletons]
    (out_dir / f"{name}.md").write_text("\n".join(md), encoding="utf-8")
    (out_dir / f"{name}.json").write_text(
        json.dumps([s.to_json() for s in skeletons], indent=1), encoding="utf-8")
    return len(skeletons), sum(len(s.arms) for s in skeletons)


def check(fams: Path, out_dir: Path) -> int:
    failures = 0
    for fam in sorted(fams.glob("*.tsv")):
        rows = family_rows(fam)
        js = out_dir / f"{fam.stem}.json"
        if not js.is_file():
            print(f"{fam.stem}: no skeleton file")
            failures += 1
            continue
        have = {int(s["addr"], 16) for s in json.loads(js.read_text(encoding="utf-8"))}
        missing = [a for a in rows if a not in have]
        if missing:
            print(f"{fam.stem}: {len(missing)} rows without a skeleton: "
                  + ", ".join(f"0x{a:08x}" for a in missing[:8]))
            failures += 1
        else:
            print(f"{fam.stem}: {len(rows)} rows, all present")
    return 1 if failures else 0


def main(argv: list[str] | None = None) -> int:
    argv = sys.argv[1:] if argv is None else argv
    if argv and argv[0] in ("brief", "check-walk", "diff", "drill-brief", "packet", "contra-brief", "check-packet"):
        import kernel_packet
        return kernel_packet.main(argv)
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("address", nargs="?", help="one function, printed to stdout")
    parser.add_argument("--family", help="a family file stem under --families (Conditions19)")
    parser.add_argument("--all", action="store_true", help="every family under --families")
    parser.add_argument("--check", action="store_true",
                        help="every family row has a skeleton section")
    parser.add_argument("--families", help="directory of family row files")
    parser.add_argument("--out", help="output directory (default: skeletons-19-29 beside families)")
    parser.add_argument("--json", action="store_true", help="with an address: print JSON")
    args = parser.parse_args(argv)

    fams = families_dir(args.families)
    out_dir = Path(args.out) if args.out else fams.parent / "skeletons-19-29"
    if args.check:
        return check(fams, out_dir)

    dll = vtmb_root() / "Vampire" / "dlls" / MODULE
    image = Image(dll) if dll.is_file() else None
    if image is None:
        print(f"warning: {dll} not found; globals and jump tables will be unread", file=sys.stderr)
    builder = Builder(image, Names())

    if args.address:
        ref = args.address.lower()
        addr = int(ref[2:] if ref.startswith("0x") else ref, 16)
        sk = builder.build(addr)
        print(json.dumps(sk.to_json(), indent=1) if args.json else sk.to_markdown())
        return 0
    targets = sorted(fams.glob("*.tsv")) if args.all else (
        [fams / f"{args.family}.tsv"] if args.family else [])
    if not targets:
        parser.error("give an address, --family <name>, --all or --check")
    for fam in targets:
        if not fam.is_file():
            print(f"no family file {fam}")
            return 1
        n, arms = write_family(builder, fam, out_dir)
        print(f"{fam.stem}: {n} functions, {arms} arms → {out_dir / (fam.stem + '.md')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
