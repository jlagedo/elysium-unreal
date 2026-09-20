# -*- coding: utf-8 -*-
"""Replay retail's `NAI_Hull` table out of `vampire.dll` -- the 22 rows navigation is sized by.

The table is not data in the image: each row's BSS record is filled by one static initialiser,
and the pointer table `0x1060a750` names the records in bit order (a row's index IS its hull bit,
`1 << i`). This probe replays the initialisers in `0x102d4440 .. 0x102d6100` and walks the pointer
table, recovering per row::

    {bit, name, mins, maxs, smallMins, smallMaxs}

Record layout, read off `staticinit_102d4440` (HUMAN_HULL, record `0x109254a0`)::

    +0x00 bits            +0x04 name (char*)
    +0x08 mins[3]         +0x14 maxs[3]
    +0x20 smallMins[3]    +0x2c smallMaxs[3]

`NAI_Hull::Bits 0x102d6210` and `NAI_Hull::Name 0x102d6230` are the accessors; the full extents
are read by `0x102d6100` / `0x102d6120` and the small ones by `0x102d6140` / `0x102d6160`.

Only the instruction forms this region actually uses are decoded -- immediate-to-stack,
stack-to-register, register-to-absolute and immediate-to-absolute moves, plus `xor reg,reg`.
Anything else in the range ends the current initialiser's register state rather than being
guessed at, and a row the walk could not fill is reported instead of silently defaulted. That
keeps the probe free of a disassembler dependency; `--check` is what guards the transcription.

The recovered rows are committed as `docs/vtmb/data/hull_table.json`, which
`research/tooling/gen_hull_table.py` turns into the engine's supported agents and the runtime's
`RetailHullExtents`. Read-only unless `--write` is supplied.

Usage::

    uv run elysium research hull_table                  # print the rows
    uv run elysium research hull_table --check          # diff against the committed JSON
    uv run elysium research hull_table --write          # rewrite the committed JSON
"""
from __future__ import annotations

import argparse
import json
import struct
import sys

from elysium_pipeline.formats import install
from elysium_pipeline.paths import repo_root

#: The image's preferred base. Every address in this probe is a virtual address under it.
IMAGE_BASE = 0x10000000

#: The static initialisers that fill the hull records, as one contiguous run.
INIT_START, INIT_END = 0x102D4440, 0x102D6100

#: The pointer table: one BSS record address per hull bit, in bit order.
POINTER_TABLE = 0x1060A750

#: A record's fields, by offset. The two extent pairs are Source units, not centimetres.
OFF_BITS, OFF_NAME = 0x00, 0x04
OFF_MINS, OFF_MAXS, OFF_SMALL_MINS, OFF_SMALL_MAXS = 0x08, 0x14, 0x20, 0x2C
RECORD_SIZE = 0x38

#: Retail's hull count, asserted by the `.ain` loader `0x102f5bd0` on every graph it reads.
EXPECTED_HULLS = 22

#: The committed transcription, repo-relative.
JSON_PATH = ("docs", "vtmb", "data", "hull_table.json")

#: The BSS records live here; a pointer outside this window ends the table.
BSS_LOW, BSS_HIGH = 0x10900000, 0x10A00000

_REG_EAX, _REG_ECX, _REG_EDX = 0, 1, 2


def dll_path() -> str:
    return f"{install.GAME}/dlls/vampire.dll"


class _Image:
    """`vampire.dll` with a virtual-address reader.

    The section table is walked rather than assumed flat: the out-of-repo original indexed the
    file by `va - IMAGE_BASE`, which happens to hold for this image only because its raw and
    virtual offsets coincide.
    """

    def __init__(self, data: bytes):
        self.data = data
        self.sections: list[tuple[int, int, int, int]] = []
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        num_sections = struct.unpack_from("<H", data, pe + 6)[0]
        opt_size = struct.unpack_from("<H", data, pe + 20)[0]
        table = pe + 24 + opt_size
        for index in range(num_sections):
            # Section header: an 8-byte name, then VirtualSize, VirtualAddress, SizeOfRawData,
            # PointerToRawData.
            virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from(
                "<IIII", data, table + index * 40 + 8)
            self.sections.append((virtual_address, virtual_size, raw_offset, raw_size))

    def offset(self, va: int) -> int | None:
        """The file offset holding `va`, or None when no section maps it (BSS has no bytes)."""

        rva = va - IMAGE_BASE
        for virtual_address, virtual_size, raw_offset, raw_size in self.sections:
            if virtual_address <= rva < virtual_address + virtual_size:
                delta = rva - virtual_address
                return raw_offset + delta if delta < raw_size else None
        return None

    def u32(self, va: int) -> int | None:
        offset = self.offset(va)
        if offset is None or offset + 4 > len(self.data):
            return None
        return struct.unpack_from("<I", self.data, offset)[0]

    def cstring(self, va: int) -> str | None:
        offset = self.offset(va)
        if offset is None:
            return None
        end = self.data.index(b"\0", offset)
        return self.data[offset:end].decode("ascii", "replace")


def _as_float(word: int | None) -> float | None:
    if word is None:
        return None
    return struct.unpack("<f", struct.pack("<I", word))[0]


def replay_initialisers(image: _Image) -> dict[int, int]:
    """Every absolute address the initialiser run writes, mapped to the word it receives.

    One pass, tracking the three registers and the stack slots the compiler staged the float
    constants through. `ret` clears that state, so one initialiser never reads another's.
    """

    start = image.offset(INIT_START)
    if start is None:
        raise SystemExit(f"{INIT_START:#x} is not mapped in {dll_path()}")
    code = image.data[start:start + (INIT_END - INIT_START)]

    written: dict[int, int] = {}
    registers: dict[int, int] = {}
    stack: dict[int, int] = {}
    pos = 0
    while pos < len(code):
        op = code[pos]
        if op == 0xC7 and code[pos + 1] == 0x04 and code[pos + 2] == 0x24:          # mov [esp], imm32
            stack[0] = struct.unpack_from("<I", code, pos + 3)[0]
            pos += 7
        elif op == 0xC7 and code[pos + 1] == 0x44 and code[pos + 2] == 0x24:        # mov [esp+d8], imm32
            stack[code[pos + 3]] = struct.unpack_from("<I", code, pos + 4)[0]
            pos += 8
        elif op == 0xC7 and code[pos + 1] == 0x05:                                  # mov [addr32], imm32
            address, value = struct.unpack_from("<II", code, pos + 2)
            written[address] = value
            pos += 10
        elif op == 0x8B and code[pos + 1] & 0xC7 == 0x04 and code[pos + 2] == 0x24:  # mov reg, [esp]
            registers[(code[pos + 1] >> 3) & 7] = stack.get(0)
            pos += 3
        elif op == 0x8B and code[pos + 1] & 0xC7 == 0x44 and code[pos + 2] == 0x24:  # mov reg, [esp+d8]
            registers[(code[pos + 1] >> 3) & 7] = stack.get(code[pos + 3])
            pos += 4
        elif op == 0xA3:                                                            # mov [addr32], eax
            written[struct.unpack_from("<I", code, pos + 1)[0]] = registers.get(_REG_EAX)
            pos += 5
        elif op == 0x89 and code[pos + 1] == 0x0D:                                  # mov [addr32], ecx
            written[struct.unpack_from("<I", code, pos + 2)[0]] = registers.get(_REG_ECX)
            pos += 6
        elif op == 0x89 and code[pos + 1] == 0x15:                                  # mov [addr32], edx
            written[struct.unpack_from("<I", code, pos + 2)[0]] = registers.get(_REG_EDX)
            pos += 6
        elif op == 0x31 and code[pos + 1] & 0xC0 == 0xC0 and (code[pos + 1] >> 3) & 7 == code[pos + 1] & 7:
            registers[code[pos + 1] & 7] = 0                                        # xor reg, reg
            pos += 2
        elif op == 0xC3:                                                            # ret
            registers, stack = {}, {}
            pos += 1
        elif op == 0x83 and code[pos + 1] in (0xC4, 0xEC):                          # add/sub esp, imm8
            pos += 3
        else:
            # An unmodelled form: drop the staged state rather than carry a stale register into
            # the next write, and keep walking. A row this starves shows up as a null field.
            registers, stack = {}, {}
            pos += 1
    return {address: value for address, value in written.items() if value is not None}


def read_rows(image: _Image) -> list[dict]:
    """The hull rows, in bit order, as the pointer table names them."""

    written = replay_initialisers(image)
    rows: list[dict] = []
    index = 0
    while True:
        record = image.u32(POINTER_TABLE + 4 * index)
        if record is None or not BSS_LOW <= record < BSS_HIGH:
            break
        name_pointer = written.get(record + OFF_NAME)
        triple = lambda base: [_as_float(written.get(record + base + 4 * axis)) for axis in range(3)]
        rows.append({
            "bit": index,
            "mask": written.get(record + OFF_BITS),
            "name": image.cstring(name_pointer) if name_pointer else None,
            "record": f"{record:#010x}",
            "mins": triple(OFF_MINS),
            "maxs": triple(OFF_MAXS),
            "smallMins": triple(OFF_SMALL_MINS),
            "smallMaxs": triple(OFF_SMALL_MAXS),
        })
        index += 1
    return rows


def document(rows: list[dict]) -> dict:
    return {
        "source": "vampire.dll NAI_Hull table",
        "pointerTable": f"{POINTER_TABLE:#010x}",
        "staticInitialisers": [f"{INIT_START:#010x}", f"{INIT_END:#010x}"],
        "recordLayout": {
            "bits": OFF_BITS, "name": OFF_NAME, "mins": OFF_MINS,
            "maxs": OFF_MAXS, "smallMins": OFF_SMALL_MINS, "smallMaxs": OFF_SMALL_MAXS,
            "size": RECORD_SIZE,
        },
        "units": "source",
        "probe": "uv run elysium research hull_table --check",
        "rows": rows,
    }


def json_file():
    return repo_root().joinpath(*JSON_PATH)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="hull_table", add_help=False)
    parser.add_argument("--check", action="store_true", help="diff the replay against the committed JSON")
    parser.add_argument("--write", action="store_true", help="rewrite the committed JSON from the replay")
    args = parser.parse_args(argv)

    with open(dll_path(), "rb") as handle:
        image = _Image(handle.read())
    rows = read_rows(image)

    if len(rows) != EXPECTED_HULLS:
        print(f"FAIL: pointer table walked {len(rows)} records, retail asserts {EXPECTED_HULLS}")
        return 1
    starved = [row["bit"] for row in rows if row["name"] is None or None in row["mins"] + row["maxs"]]
    if starved:
        print(f"FAIL: the replay did not fill rows {starved}")
        return 1

    payload = document(rows)
    target = json_file()
    if args.write:
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
        print(f"wrote {target} ({len(rows)} rows)")
        return 0
    if args.check:
        if not target.exists():
            print(f"FAIL: {target} does not exist; run with --write")
            return 1
        committed = json.loads(target.read_text(encoding="utf-8"))
        if committed == payload:
            print(f"OK: {target.name} matches the image ({len(rows)} rows)")
            return 0
        print(f"FAIL: {target.name} differs from the image")
        for row, live in zip(committed.get("rows", []), rows):
            if row != live:
                print(f"  bit {live['bit']}: committed {row}\n         image {live}")
        return 1

    for row in rows:
        print(f"{row['bit']:2d} {row['record']} mask {row['mask']:#09x} {row['name'] or '?':24s} "
              f"mins {row['mins']} maxs {row['maxs']} small {row['smallMins']} {row['smallMaxs']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
