# -*- coding: utf-8 -*-
"""Emit the NPC kernel's tunables table as project source, verified against the pinned image.

Owner-run archaeology, not part of any build.  Spec 0019 story 4.  Every threshold a kernel body
reads was "read out of the pinned ``vampire.dll`` at its cited address"; this tool is where that
sentence becomes a check instead of a paragraph.  One overlay,
``research/tooling/ghidra/driver/kernel_tunables.tsv`` (address, name, type, value, evidence),
is rendered into two committed files:

* ``ElysiumNpcKernelTunables.h`` — one ``inline constexpr`` per cell or immediate, under its
  retail meaning's name in ``namespace ElysiumNpcTunables``, and the ConVar surface: an enum with
  one enumerator per ``convar_*`` row, ``ConVarFloat`` (retail's ``+0x28`` read), ``ConVarInt``
  (``+0x2c``) and the value setter a test or console drives.
* ``ElysiumNpcKernelTunables.cpp`` — the ConVar rows (console name, object, shipped default) and
  the live value store.

The row types:

* ``f32`` / ``f64`` / ``i32`` / ``u32`` — an ``.rdata`` / ``.data`` cell, read at that width.  The
  width is the reading instruction's (``dword ptr`` / ``qword`` / ``double ptr``): eight cells in
  ``docs/vtmb/npc-ai/rdata-cells.md`` are doubles whose low dword reads as a plausible ``0.0``.
* ``imm_f32`` / ``imm_i32`` — an instruction's 32-bit immediate (``PUSH imm32``, ``MOV [..], imm32``);
  the address is the instruction.
* ``convar_f32`` / ``convar_i32`` — a ``ConVar``; the address is the OBJECT (the oracle's ``DAT_``
  pointer is object ``+ 4``), the value its default string, verbatim.  The evidence opens with the
  console name in backticks.  The static-initialiser shape ``PUSH default / PUSH name / MOV ECX,
  object / CALL ctor`` (``docs/vtmb/npc-ai/convars.md``) is what ``--check`` reads both strings
  from.  ``ConVar::Create`` parses the default as ``m_fValue = atof``, ``m_nValue = atoi``.

``--check`` re-renders and compares the committed files, AND reads every row back out of the image
at its stated width: a float is compared bit for bit at its width, not by its printed decimal.
``--report`` lists the ``DAT_`` cells the NPC substrate still spells inline that the overlay does
not hold — story 6's migration queue.
"""

from __future__ import annotations

import argparse
import hashlib
import math
import re
import struct
import sys
import textwrap
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "ghidra" / "driver"))

import kernel_ledger as kl  # noqa: E402  (also loads `.elysium.local.env`)
sys.path.insert(0, str(Path(__file__).resolve().parent))
from gen_kernel_shape import _emit  # noqa: E402

from elysium_pipeline.formats.ai_schedule_glb.image import PINNED_SHA256, PEImage  # noqa: E402
from elysium_pipeline.paths import repo_root, vtmb_root  # noqa: E402

SUBSTRATE = ("Source", "ElysiumUE", "Private", "Substrate")
HEADER_OUTPUT = (*SUBSTRATE, "ElysiumNpcKernelTunables.h")
SOURCE_OUTPUT = (*SUBSTRATE, "ElysiumNpcKernelTunables.cpp")
OVERLAY = Path(__file__).resolve().parent / "ghidra" / "driver" / kl.TUNABLES_TSV

CELL_FORMATS = {"f32": "<f", "f64": "<d", "i32": "<i", "u32": "<I"}
CPP_TYPES = {"f32": "float", "f64": "double", "i32": "int32", "u32": "uint32",
             "imm_f32": "float", "imm_i32": "int32"}
CONSOLE_RE = re.compile(r"^`([A-Za-z0-9_]+)`")
ATOI_RE = re.compile(r"^\s*[+-]?\d+")


class Mismatch(Exception):
    """The image does not say what the row says."""


# --- Values -------------------------------------------------------------------------------------


def _f32(value: float) -> float:
    return struct.unpack("<f", struct.pack("<f", value))[0]


def _bits(value: float | int, fmt: str) -> bytes:
    return struct.pack(fmt, value)


def _parse(kind: str, text: str) -> float | int:
    if kind in ("f32", "f64", "imm_f32"):
        return float(text)
    return int(text, 0)


def _float_literal(value: float, width: str) -> str:
    """The shortest decimal that rounds back to the same bits at the stored width."""
    if not math.isfinite(value):
        raise SystemExit(f"gen_kernel_tunables: {value!r} has no C++ literal")
    if width == "f64":
        text = repr(value)
    else:
        target = _bits(value, "<f")
        text = repr(next(float(t) for t in (f"{value:.{p}g}" for p in range(1, 18))
                         if _bits(float(t), "<f") == target))
    if not any(c in text for c in ".e"):
        text += ".0"
    elif text.startswith("-."):
        text = "-0" + text[1:]
    elif text.startswith("."):
        text = "0" + text
    return text if width == "f64" else text + "f"


def atof(text: str) -> float:
    """`atof`'s answer on a ConVar default: the longest leading float, else 0."""
    match = re.match(r"^\s*[+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?", text)
    return float(match.group(0)) if match else 0.0


def atoi(text: str) -> int:
    match = ATOI_RE.match(text)
    return int(match.group(0)) if match else 0


def console_name(row: kl.Tunable) -> str:
    match = CONSOLE_RE.match(row.evidence)
    if not match:
        raise SystemExit(f"gen_kernel_tunables: {row.name}: a ConVar row's evidence opens with "
                         "the console name in backticks")
    return match.group(1)


# --- The image ----------------------------------------------------------------------------------


def load_image(binary: str | None) -> PEImage:
    path = Path(binary) if binary else vtmb_root() / "Vampire" / "dlls" / "vampire.dll"
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != PINNED_SHA256:
        raise SystemExit(f"gen_kernel_tunables: {path} is sha256 {digest}, not the pinned image")
    return PEImage(data)


def _immediate(image: PEImage, va: int) -> bytes:
    """The imm32 of `PUSH imm32` or `MOV r/m32, imm32` at an instruction."""
    offset = image.va_to_offset(va)
    if offset is None:
        raise Mismatch("the instruction is not in the image")
    data = image.data
    op = data[offset]
    if op == 0x68:
        return data[offset + 1:offset + 5]
    if op == 0xC7:
        modrm = data[offset + 1]
        mod, rm = modrm >> 6, modrm & 7
        length = 2 + (1 if rm == 4 and mod != 3 else 0)
        length += {0: 4 if rm == 5 else 0, 1: 1, 2: 4, 3: 0}[mod]
        return data[offset + length:offset + length + 4]
    raise Mismatch(f"opcode {op:#04x} is not PUSH imm32 / MOV r/m32, imm32")


def convar_strings(image: PEImage, obj: int) -> tuple[str, str]:
    """(console name, default) from the static initialiser that constructs `obj`."""
    needle = b"\xb9" + struct.pack("<I", obj)
    start, text = image.section_bytes(".text")
    hits = []
    at = text.find(needle)
    while at >= 0:
        p = start + at
        data = image.data
        if data[p + 5] == 0xE8 and data[p - 5] == 0x68 and data[p - 10] == 0x68:
            name = image.read_cstring_va(struct.unpack_from("<I", data, p - 4)[0])
            default = image.read_cstring_va(struct.unpack_from("<I", data, p - 9)[0])
            hits.append((name, default))
        at = text.find(needle, at + 1)
    if len(hits) != 1:
        raise Mismatch(f"{len(hits)} ConVar constructions of object {obj:#010x}, expected one")
    return hits[0]


def verify(image: PEImage, row: kl.Tunable) -> None:
    va = int(row.address, 16)
    if row.type in CELL_FORMATS:
        fmt = CELL_FORMATS[row.type]
        held = image.read_scalar_va(va, fmt)
        if held is None:
            raise Mismatch("the cell is not in the image")
        if _bits(held, fmt) != _bits(_parse(row.type, row.value), fmt):
            raise Mismatch(f"the image holds {held!r}, the row says {row.value}")
    elif row.type.startswith("imm_"):
        fmt = "<f" if row.type == "imm_f32" else "<i"
        held = struct.unpack(fmt, _immediate(image, va))[0]
        if _bits(held, fmt) != _bits(_parse(row.type, row.value), fmt):
            raise Mismatch(f"the immediate is {held!r}, the row says {row.value}")
    else:
        name, default = convar_strings(image, va)
        if name != console_name(row):
            raise Mismatch(f"the image names it `{name}`, the row `{console_name(row)}`")
        if default != row.value:
            raise Mismatch(f"the image's default is {default!r}, the row says {row.value!r}")


# --- Rendering ----------------------------------------------------------------------------------


BANNER = "// Generated by `uv run elysium research gen_kernel_tunables`. Do not hand-edit."


def _comment(text: str, indent: str = "") -> list[str]:
    return [f"{indent}// {line}" for line in textwrap.wrap(text, 100 - len(indent) - 3,
                                                                   break_on_hyphens=False)]


def _header(rows: list[kl.Tunable]) -> list[str]:
    cells = [r for r in rows if not r.type.startswith("convar_")]
    convars = [r for r in rows if r.type.startswith("convar_")]
    return [
        BANNER,
        "//",
        *_comment("The NPC kernel's tunables: every retail number a kernel body reads, from "
                  "`research/tooling/ghidra/driver/kernel_tunables.tsv` (spec 0019 story 4). "
                  "Each row was read out of the pinned `Vampire/dlls/vampire.dll` (image base "
                  "`0x10000000`) at the width its type states, and `--check` reads it again. "
                  "A body reads the NAME; the address and its evidence are here, once."),
        "//",
        *_comment(f"{len(cells)} cells and immediates, {len(convars)} ConVars; image sha256 "
                  f"`{PINNED_SHA256[:16]}…`."),
    ]


def _cpp_string(text: str) -> str:
    return "TEXT(\"" + text.replace("\\", "\\\\").replace("\"", "\\\"") + "\")"


def render_header(rows: list[kl.Tunable]) -> str:
    out = _header(rows)
    out += ["", "#pragma once", "", '#include "CoreMinimal.h"', "",
            "namespace ElysiumNpcTunables", "{"]
    out.append("\t// --- Cells and immediates, in overlay order ---------------------------------"
               "--------------")
    for row in rows:
        if row.type.startswith("convar_"):
            continue
        kind = CPP_TYPES[row.type]
        value = _parse(row.type, row.value)
        if kind in ("float", "double"):
            literal = _float_literal(value, "f64" if kind == "double" else "f32")
        elif kind == "uint32":
            literal = f"{value:#010x}u"
        else:
            literal = str(value)
        out.append("")
        out += _comment(f"`0x{row.address}` {row.type} — {row.evidence}", "\t")
        out.append(f"\tinline constexpr {kind} {row.name} = {literal};")
    out += ["", "\t// --- ConVars -------------------------------------------------------------"
                "------------------", "\t//"]
    out += _comment("Retail reads a ConVar as `cv->vtable[4]() ? 0 : cv->m_fValue` (`+0x28`) or "
                    "`… : cv->m_nValue` (`+0x2c`). No shipped config, script or vdata file sets "
                    "any of these (`docs/vtmb/npc-ai/convars.md`), so the default is what retail "
                    "runs; `SetConVar` is ConVar::SetValue(float) for a test or the console.", "\t")
    out += ["\tenum class EConVar : uint8", "\t{"]
    for row in rows:
        if row.type.startswith("convar_"):
            out.append(f"\t\t{row.name}, // `{console_name(row)}` \"{row.value}\", object "
                       f"`0x{row.address}`")
    out += ["\t\tCount", "\t};", "",
            "\tstruct FConVarRow",
            "\t{",
            "\t\tconst TCHAR* ConsoleName;",
            "\t\tuint32 Object;",
            "\t\tconst TCHAR* Default;",
            "\t\tfloat Float; // `ConVar::Create`'s `m_fValue = atof(default)`",
            "\t\tint32 Int;   // … and `m_nValue = atoi(default)`",
            "\t};",
            "",
            "\tconst FConVarRow& ConVarRow(EConVar ConVar);",
            "\tfloat ConVarFloat(EConVar ConVar);",
            "\tint32 ConVarInt(EConVar ConVar);",
            "\tvoid SetConVar(EConVar ConVar, float Value);",
            "\tvoid ResetConVars();",
            "}"]
    return "\n".join(out) + "\n"


def render_source(rows: list[kl.Tunable]) -> str:
    out = _header(rows)
    out += ["", '#include "Substrate/ElysiumNpcKernelTunables.h"', "",
            "namespace ElysiumNpcTunables", "{", "namespace", "{",
            "\tconstexpr FConVarRow GConVarRows[] =", "\t{"]
    for row in rows:
        if not row.type.startswith("convar_"):
            continue
        number = atof(row.value)
        out.append(f"\t\t{{ {_cpp_string(console_name(row))}, 0x{row.address}u, "
                   f"{_cpp_string(row.value)}, {_float_literal(_f32(number), 'f32')}, "
                   f"{atoi(row.value)} }},")
    out += ["\t};",
            "\tstatic_assert(UE_ARRAY_COUNT(GConVarRows) == static_cast<int32>(EConVar::Count),",
            "\t\t\"one row per EConVar enumerator\");",
            "",
            "\t// The live values. Game-thread only, like the rest of the substrate; a slot that was "
            "never",
            "\t// set answers the row's default.",
            "\tstruct FConVarLive",
            "\t{",
            "\t\tfloat Float = 0.f;",
            "\t\tint32 Int = 0;",
            "\t\tbool bSet = false;",
            "\t};",
            "\tFConVarLive GConVarLive[static_cast<int32>(EConVar::Count)];",
            "}",
            "",
            "const FConVarRow& ConVarRow(EConVar ConVar)",
            "{",
            "\tcheck(ConVar < EConVar::Count);",
            "\treturn GConVarRows[static_cast<int32>(ConVar)];",
            "}",
            "",
            "float ConVarFloat(EConVar ConVar)",
            "{",
            "\tconst FConVarLive& Live = GConVarLive[static_cast<int32>(ConVar)];",
            "\treturn Live.bSet ? Live.Float : ConVarRow(ConVar).Float;",
            "}",
            "",
            "int32 ConVarInt(EConVar ConVar)",
            "{",
            "\tconst FConVarLive& Live = GConVarLive[static_cast<int32>(ConVar)];",
            "\treturn Live.bSet ? Live.Int : ConVarRow(ConVar).Int;",
            "}",
            "",
            "void SetConVar(EConVar ConVar, float Value)",
            "{",
            "\t// `ConVar::SetValue(float)`: `m_fValue = value; m_nValue = (int)value`.",
            "\tcheck(ConVar < EConVar::Count);",
            "\tGConVarLive[static_cast<int32>(ConVar)] = { Value, static_cast<int32>(Value), true };",
            "}",
            "",
            "void ResetConVars()",
            "{",
            "\tfor (FConVarLive& Live : GConVarLive)",
            "\t{",
            "\t\tLive = FConVarLive();",
            "\t}",
            "}",
            "}"]
    return "\n".join(out) + "\n"


# --- Driver -------------------------------------------------------------------------------------


def report(repo: Path, rows: list[kl.Tunable], image: PEImage) -> None:
    by_type: dict[str, int] = {}
    for row in rows:
        by_type[row.type] = by_type.get(row.type, 0) + 1
    print("NPC kernel tunables:", ", ".join(f"{n} {k}" for k, n in by_type.items()),
          f"({len(rows)} rows)")
    queue = kl.inline_cells(repo, rows)
    total = set().union(*queue.values()) if queue else set()
    print(f"outstanding inline DAT_ cells: {len(total)} over {len(queue)} files")
    for name, cells in sorted(queue.items(), key=lambda item: (-len(item[1]), item[0])):
        sections = {}
        for cell in cells:
            section = image.section_of_va(int(cell, 16)) or "unmapped"
            sections[section] = sections.get(section, 0) + 1
        detail = ", ".join(f"{n} {s}" for s, n in sorted(sections.items()))
        print(f"  {len(cells):4d}  {name}  ({detail})")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", help="override the pinned retail vampire.dll path")
    parser.add_argument("--check", action="store_true",
                        help="verify the committed files and every row against the image")
    parser.add_argument("--report", action="store_true",
                        help="list the inline DAT_ cells the overlay does not hold yet")
    args = parser.parse_args(argv)

    repo = repo_root()
    rows = kl.load_tunables(OVERLAY)
    image = load_image(args.binary)
    if args.report:
        report(repo, rows, image)
        return 0

    status = 0
    for row in rows:
        try:
            verify(image, row)
        except Mismatch as error:
            print(f"MISMATCH: 0x{row.address} {row.name} ({row.type}): {error}")
            status = 1
    if status == 0:
        print(f"check: all {len(rows)} rows agree with the pinned image")
    elif not args.check:
        print("not writing: fix the overlay first")
        return status
    status |= _emit(repo.joinpath(*HEADER_OUTPUT), render_header(rows), args.check)
    status |= _emit(repo.joinpath(*SOURCE_OUTPUT), render_source(rows), args.check)
    return status


if __name__ == "__main__":
    raise SystemExit(main())
