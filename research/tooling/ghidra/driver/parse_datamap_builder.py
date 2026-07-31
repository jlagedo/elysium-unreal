# -*- coding: utf-8 -*-
"""Reconstruct a VtMB `datamap_t` from its **decompiled builder**, not from the image.

VtMB has no static `DEFINE_FIELD` arrays: each class's datamap is populated at static-init
time by a per-class builder that writes 44-byte `typedescription_t` records into `.data`.
Most builders are only *half* static -- the leading records ship initialized in `.data`, but
everything the builder assigns (which for the big character classes is the whole input set)
reads back as **zero** from the un-run image. So `DumpDatamap` over a raw image read reports
`INPUTS (0)` for a class that plainly has dozens.

This reads the builder's Ghidra pseudocode instead and replays its assignments symbolically.
Each `_DAT_<hex> = <value>;` line is bucketed into `(record index, field offset)` against the
record-array base, using the layout `docs/vtmb/python_bridge.md` pins:

    fieldType @0   internalName @4   fieldOffset @8   fieldSize @0x10
    flags @0x12 (bit 0x8 = keyable/writable)         externalName @0x14
    pSaveRestoreOps @0x18            inputFunc @0x1C  td @0x20  fieldSizeInBytes @0x24

Names come free: the decompiler renders a string pointer as `s_<content>_<address>`, so the
symbol carries the literal. A `FUN_<addr>` value in the `inputFunc` slot is the input handler
to decompile next.

Usage:
    uv run elysium research parse_datamap_builder <builder_dump.txt> --recs <hex> [--count N]
                                                        [--json <path>]

`--recs` is the record-array base the builder's tail assigns to `datamap_t.dataDesc`
(the `_DAT_<map> = &DAT_<recs>` pair at the end of the function). Local-only RE instrument,
like everything else under `$ELYSIUM_WORK_ROOT/research/ghidra/`.
"""
import argparse
import collections
import json
import re

REC_SIZE = 0x2C

FIELDS = {
    0x00: "fieldType",
    0x04: "internalName",
    0x08: "fieldOffset",
    0x10: "fieldSize",
    0x12: "flags",
    0x14: "externalName",
    0x18: "saveRestoreOps",
    0x1C: "inputFunc",
    0x20: "td",
    0x24: "fieldSizeInBytes",
}

# VtMB's own fieldtype_t (it lacks modern Source's FIELD_QUATERNION and FIELD_TICK, so every
# code from 4 up is shifted -- python_bridge.md "Divergence").
FIELD_TYPE = [
    "VOID", "FLOAT", "STRING", "VECTOR", "INTEGER", "BOOLEAN", "SHORT", "CHARACTER",
    "COLOR32", "EMBEDDED", "CUSTOM", "CLASSPTR", "EHANDLE", "EDICT", "POSITION_VECTOR",
    "TIME", "MODELNAME", "SOUNDNAME", "INPUT", "FUNCTION",
]

ASSIGN = re.compile(r"^\s*_?DAT_([0-9a-fA-F]{8})\s*=\s*(.+?);\s*$", re.M)
# `s_m_flMoveStartTime_1061ec90` / `s_lootable_type_1061b4a4` -> the literal between s_ and _addr
SYM_STR = re.compile(r"^&?s_(.*)_[0-9a-fA-F]{8}$")
SYM_FUN = re.compile(r"^&?(?:thunk_)?(FUN_[0-9a-fA-F]{8}|LAB_[0-9a-fA-F]{8})$")


def decode_value(raw):
    """A pseudocode rvalue -> (kind, python value)."""
    raw = raw.strip()
    m = SYM_STR.match(raw)
    if m:
        return "str", m.group(1)
    m = SYM_FUN.match(raw)
    if m:
        return "fn", m.group(1)
    if re.match(r"^0x[0-9a-fA-F]+$", raw):
        return "int", int(raw, 16)
    if re.match(r"^-?\d+$", raw):
        return "int", int(raw)
    return "raw", raw


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("dump", help="DumpFuncs output for the builder function")
    ap.add_argument("--recs", required=True, help="record-array base address (hex)")
    ap.add_argument("--count", type=int, default=0, help="record count (0 = infer from the span)")
    ap.add_argument("--json", help="also write the reconstructed records here")
    args = ap.parse_args()

    base = int(args.recs, 16)
    text = open(args.dump, "r", errors="replace").read()

    recs = collections.defaultdict(dict)
    stray = 0
    for addr_hex, raw in ASSIGN.findall(text):
        addr = int(addr_hex, 16)
        if addr < base:
            continue
        idx, off = divmod(addr - base, REC_SIZE)
        if args.count and idx >= args.count:
            continue
        name = FIELDS.get(off)
        if name is None:
            stray += 1
            continue
        recs[idx][name] = decode_value(raw)

    inputs, outputs, keys, out_json = [], [], [], []
    for idx in sorted(recs):
        r = recs[idx]
        ftype = r.get("fieldType", ("int", 0))[1]
        ext = r.get("externalName", ("", ""))[1]
        internal = r.get("internalName", ("", ""))[1]
        flags = r.get("flags", ("int", 0))[1]
        offset = r.get("fieldOffset", ("int", 0))[1]
        infn = r.get("inputFunc", (None, None))
        tname = FIELD_TYPE[ftype] if isinstance(ftype, int) and ftype < len(FIELD_TYPE) else str(ftype)

        row = {
            "index": idx, "external": ext, "internal": internal,
            "type": tname, "type_code": ftype,
            "offset": offset if isinstance(offset, int) else None,
            "flags": flags if isinstance(flags, int) else None,
            "keyable": bool(isinstance(flags, int) and flags & 0x8),
            "inputFunc": infn[1] if infn[0] == "fn" else None,
        }
        out_json.append(row)

        line = "  %-34s %-34s %-16s off=%-8s flags=%-6s%s%s" % (
            ext or "(no external name)", internal, tname,
            hex(offset) if isinstance(offset, int) else "?",
            hex(flags) if isinstance(flags, int) else "?",
            " keyable" if row["keyable"] else "",
            "  inputFunc=%s" % row["inputFunc"] if row["inputFunc"] else "")

        if row["inputFunc"]:
            inputs.append(line)
        elif ftype == 10:                      # FIELD_CUSTOM is how outputs are tagged
            outputs.append(line)
        else:
            keys.append(line)

    print("// builder: %s   records base %08x   reconstructed %d records"
          % (args.dump, base, len(recs)))
    if stray:
        print("// %d assignments landed on non-field offsets (padding/other globals)" % stray)
    for title, rows in (("INPUTS", inputs), ("OUTPUTS", outputs), ("FIELDS", keys)):
        print("---- %s (%d) ----" % (title, len(rows)))
        for r in rows:
            print(r)

    if args.json:
        with open(args.json, "w") as f:
            json.dump(out_json, f, indent=1)
        print("// wrote %s" % args.json)


if __name__ == "__main__":
    main()
