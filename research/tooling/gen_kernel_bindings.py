# -*- coding: utf-8 -*-
"""Emit the datamap-backed field/input bindings of the port's NPC-family classes.

Owner-run archaeology, not part of any build.  The datamap replay
(``$ELYSIUM_WORK_ROOT/research/ghidra/types/datamap_records-vampire.dll.json``)
records what retail's own registration wrote for the classes this port stands:
``CAI_BaseNPC`` and ``CAI_BaseNPCTroika`` (the shared ``npc_*`` leaf),
``CNPCMaker`` (the two ``npc_maker`` classnames) and ``CAI_InterestingPlace``
(retail's misspelled ``intersting_place``): every member with its offset, type
and flags, and — where retail had one — the external name a map, a script or an
input used.  This generator transcribes those rows into committed source:

* ``ElysiumNpcKernelBindings.h/.cpp`` — one ``Add…Fields`` per binding class
  (one ``ElysiumAddClassField`` row per field the replay names and the class's
  member map binds), the per-class ``Outputs``/``InputFuncs`` name tables and
  ``Counts`` (the four totals, as literals).

The classification is fixed.  A row with ``OUTPUT`` is an output name.  A row
with ``FUNCTIONTABLE`` is skipped.  An ``INPUT`` row whose type is ``void`` or
whose offset is 0 is an input *handler*, not a field.  Everything else with an
external name is a field row, bound when the class's member map carries a
member at that offset and left as an ``UNBOUND`` comment when it does not — the
comment is the recorded gap, the same posture the shape map's own ``_ABSENT``
rows take.  Save-only rows carry no external and are nobody's row here: where
the port binds one by hand (``m_cLiveChildren``, ``m_flGround``,
``m_iEnemySightings``), the hand row stays.

Member resolution is per class.  The NPC's two tables resolve through the shape
map (``ElysiumNpcKernelShapeMap.cpp``), which the census checks.  ``CNPCMaker``
and ``CAI_InterestingPlace`` have no shape map; they resolve through the small
hand-written ``CLASS_MEMBER_MAPS`` below, and the compiler is the check — a
wrong member name fails ``uv run elysium build``.

Flag mapping, fixed: ``SAVE`` -> ``EElysiumField::Save``; ``INPUT`` ->
``EElysiumField::Key``; neither -> ``EElysiumField::None``.  ``KEY`` alone
adds no flag: the registry applies spawn keyvalues regardless of ``Key``, so
retail's spawn-time ``KEY`` needs no port bit.

A bound row the binding API cannot express fails the build through
``ElysiumAddClassField``'s ``static_assert``; such a row moves to the
``UNBOUND`` comments through ``UNSUPPORTED_MEMBER_TYPES`` below rather than by
editing anything the generator emits.

Usage::

    uv run elysium research gen_kernel_bindings
    uv run elysium research gen_kernel_bindings --check
    uv run elysium research gen_kernel_bindings --report
"""

from __future__ import annotations

import argparse
import difflib
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

from elysium_pipeline.paths import repo_root, work_root

sys.path.insert(0, str(Path(__file__).resolve().parent / "ghidra" / "driver"))

import kernel_ledger as kl  # noqa: E402
import gen_kernel_shape as shape  # noqa: E402  (same directory; its own sys.path work)

REPLAY = ("research", "ghidra", "types", "datamap_records-vampire.dll.json")
SHAPE_MAP = ("Source", "ElysiumUE", "Private", "Substrate",
             "ElysiumNpcKernelShapeMap.cpp")
BINDINGS_H = ("Source", "ElysiumUE", "Private", "Substrate",
              "ElysiumNpcKernelBindings.h")
BINDINGS_CPP = ("Source", "ElysiumUE", "Private", "Substrate",
                "ElysiumNpcKernelBindings.cpp")

# The binding classes, each over the retail datamap tables whose rows it registers. The NPC's
# two tables are one binding class because the port stands one shared leaf for them; the maker
# and the place stand their own.
BINDING_CLASSES = (
    ("Npc", ("CAI_BaseNPC", "CAI_BaseNPCTroika")),
    ("NpcMaker", ("CNPCMaker",)),
    ("InterestingPlace", ("CAI_InterestingPlace",)),
)

# Bound NPC rows the binding API cannot express, by offset, with the type that fails it. A
# member whose owner is one of the kernel's component structs (FElysiumNpcScheduleHost and
# kin) does not derive FElysiumEntity, which is the one thing `ElysiumAddClassField`
# static_asserts on before member type even comes into it. Such a row lands in the UNBOUND
# comments; the seam that would carry it is a second API surface, which is nobody's one-line
# decision.
UNSUPPORTED_MEMBER_TYPES: dict[int, str] = {
    0x5DB0: "FElysiumNpcScheduleHost",
    0x62E0: "FElysiumNpcScheduleHost",
    0x6436: "FElysiumNpcScheduleHost",
}

# The retail-offset -> port-member map for the two classes with no shape map, keyed by retail
# class, then by offset. Written by hand from the walked members `ElysiumNpcClasses.cpp`
# binds; the compiler is the check — a wrong member name fails the build, never a silent
# wrong binding. A replay key absent here is an UNBOUND row (`no port member`).
CLASS_MEMBER_MAPS: dict[str, dict[int, tuple[str, str]]] = {
    "CNPCMaker": {
        0x665C: ("FElysiumNpcMaker", "NpcType"),
        0x6660: ("FElysiumNpcMaker", "RemainingTotal"),
        0x6664: ("FElysiumNpcMaker", "SpawnFrequency"),
        0x66B4: ("FElysiumNpcMaker", "MaxLiveChildren"),
        0x66BC: ("FElysiumNpcMaker", "ChildTargetName"),
        0x66C0: ("FElysiumNpcMaker", "bDisabled"),
        0x66C1: ("FElysiumNpcMaker", "bNpcClip"),
        0x66C2: ("FElysiumNpcMaker", "bFade"),
        0x66C3: ("FElysiumNpcMaker", "bInfinite"),
        0x66C4: ("FElysiumNpcMaker", "bNoDrop"),
        0x66C5: ("FElysiumNpcMaker", "bViewCone"),
        0x66C8: ("FElysiumNpcMaker", "MinPcDistance"),
    },
    "CAI_InterestingPlace": {
        0x544: ("FElysiumInterestingPlace", "Type"),
        0x568: ("FElysiumInterestingPlace", "MinTime"),
        0x56C: ("FElysiumInterestingPlace", "MaxTime"),
        0x570: ("FElysiumInterestingPlace", "bMatchOrientation"),
        0x574: ("FElysiumInterestingPlace", "GroupId"),
        0x578: ("FElysiumInterestingPlace", "Rating"),
        0x57C: ("FElysiumInterestingPlace", "bEnabled"),
        0x584: ("FElysiumInterestingPlace", "MaxNpcs"),
    },
}

# The shape map's two row shapes. The first is the binding regex: a _WORD or _WORD_NOTED row
# binds an offset to a port type and member. The second names the four no-member forms, so an
# UNBOUND row can say its offset landed on one of them.
BOUND_ROW_RE = re.compile(
    r"ELYSIUM_NPC_WORD(?:_NOTED)?\(\s*0x([0-9a-f]+)\s*,\s*(\w+)\s*,\s*(\w+)")
NO_MEMBER_ROW_RE = re.compile(
    r"ELYSIUM_NPC_WORD_(PRIVATE|CHAIN|IMPLICIT|ABSENT)\(\s*0x([0-9a-f]+)")


@dataclass
class Row:
    """One replay row that has an external name, classified."""

    cls: str            # the retail table the row was read from
    name: str           # the retail member, e.g. m_iNPCPerception
    type: str           # the replay's typeName
    offset: int
    external: str
    kind: str           # "field" | "output" | "inputfunc"
    flags: list[str] = field(default_factory=list)
    binding: tuple[str, str] | None = None   # (Type, Member) out of the class's member map
    reason: str = ""    # an unbound field's reason


def parse_shape_map(repo: Path) -> tuple[dict[int, tuple[str, str]], dict[int, str]]:
    """The shape map as `offset -> (Type, Member)` for the bound rows, and
    `offset -> form` for the four no-member forms."""
    text = repo.joinpath(*SHAPE_MAP).read_text(encoding="utf-8")
    bound = {int(m.group(1), 16): (m.group(2), m.group(3))
             for m in BOUND_ROW_RE.finditer(text)}
    no_member = {int(m.group(2), 16): m.group(1)
                 for m in NO_MEMBER_ROW_RE.finditer(text)}
    overlap = sorted(set(bound) & set(no_member))
    if overlap:
        raise SystemExit(f"gen_kernel_bindings: shape map offsets "
                         f"{[f'0x{o:x}' for o in overlap]} bind and disclaim a member")
    return bound, no_member


@dataclass
class ClassModel:
    """One binding class's classified rows."""

    name: str                          # Npc | NpcMaker | InterestingPlace
    tables: tuple[str, ...]            # the retail datamap tables it registers
    bound: list[Row] = field(default_factory=list)
    unbound: list[Row] = field(default_factory=list)
    outputs: list[Row] = field(default_factory=list)
    inputfuncs: list[Row] = field(default_factory=list)


def classify(replay: dict, repo: Path, model_offsets: set[int]) -> list[ClassModel]:
    """Every replay row of the binding classes that has an external name, classified."""
    bound, no_member = parse_shape_map(repo)
    classes: list[ClassModel] = []
    for binding, tables in BINDING_CLASSES:
        model = ClassModel(name=binding, tables=tables)
        for cls in tables:
            member_map = CLASS_MEMBER_MAPS.get(cls)
            for record in replay[cls]["records"]:
                external = record.get("external")
                if not external:
                    continue
                flags = list(record.get("flagNames") or [])
                offset = int(record["offset"])
                base = dict(cls=cls, name=record["name"], type=record["typeName"],
                            offset=offset, external=external, flags=flags)
                if "OUTPUT" in flags:
                    model.outputs.append(Row(kind="output", **base))
                elif "FUNCTIONTABLE" in flags:
                    continue
                elif "INPUT" in flags and (record["typeName"] == "void" or offset == 0):
                    model.inputfuncs.append(Row(kind="inputfunc", **base))
                else:
                    if member_map is not None:
                        # No shape map for this class: the hand-written member map is the
                        # whole resolution, and an offset it lacks is a recorded gap.
                        if offset in member_map:
                            row = Row(kind="field", binding=member_map[offset], **base)
                        else:
                            row = Row(kind="field", reason="no port member", **base)
                    else:
                        # The NPC's shape-map path.
                        if offset in bound:
                            if offset in UNSUPPORTED_MEMBER_TYPES:
                                row = Row(
                                    kind="field",
                                    reason=f"member type {UNSUPPORTED_MEMBER_TYPES[offset]}",
                                    **base)
                            else:
                                row = Row(kind="field", binding=bound[offset], **base)
                        elif offset in no_member:
                            row = Row(kind="field",
                                      reason=f"no shape-map member ({no_member[offset]})",
                                      **base)
                        else:
                            row = Row(kind="field", reason="no shape-map member", **base)
                        # The replay names a field whose offset the shape census never
                        # recorded. That is a disagreement between two committed artefacts,
                        # not a row to emit.
                        if offset not in model_offsets:
                            raise SystemExit(
                                f"gen_kernel_bindings: {cls} +0x{offset:x} "
                                f"({record['name']}) is not a word of the shape census")
                    if row.binding is not None:
                        model.bound.append(row)
                    else:
                        model.unbound.append(row)
        model.bound.sort(key=lambda r: r.external)
        model.unbound.sort(key=lambda r: r.external)
        model.outputs.sort(key=lambda r: r.external)
        model.inputfuncs.sort(key=lambda r: r.external)
        classes.append(model)
    return classes


@dataclass
class Model:
    classes: list[ClassModel]
    datamaps: dict[str, str]


def build(repo: Path) -> Model:
    # The shape generator's model, used only to confirm each NPC replay offset is a known word.
    shape_model = shape.build(repo_root(), kl.MODULE, kl.DEFAULT_DEPTH)
    model_offsets = {word.offset for word in shape_model.words}

    replay_path = work_root().joinpath(*REPLAY)
    replay = json.loads(replay_path.read_text(encoding="utf-8"))
    return Model(classes=classify(replay, repo, model_offsets),
                 datamaps={cls: replay[cls]["datamap"]
                           for _, tables in BINDING_CLASSES for cls in tables})


# --- Emission -----------------------------------------------------------------------------------


def _literal(text: str) -> str:
    escaped = (text or "").replace("\\", "\\\\").replace('"', '\\"')
    return f'TEXT("{escaped}")'


def _comment(text: str, indent: str = "") -> list[str]:
    """Wrap prose as `//` lines under the repository's 100-column rule."""
    out: list[str] = []
    current = ""
    for word in text.split():
        candidate = f"{current} {word}".strip()
        if current and len((indent + "// " + candidate).expandtabs(4)) > 100:
            out.append(f"{indent}// {current}")
            current = word
        else:
            current = candidate
    if current:
        out.append(f"{indent}// {current}")
    return out


def _wrapped(text: str, indent: str) -> list[str]:
    """One statement, broken after a comma when it would pass 100 columns."""
    if len((indent + text).expandtabs(4)) <= 100:
        return [indent + text]
    out: list[str] = []
    current = indent
    for piece in text.split(", "):
        candidate = current + piece + ", "
        if current.strip() and len(candidate.expandtabs(4)) > 100:
            out.append(current.rstrip())
            current = indent + "\t"
        current += piece + ", "
    out.append(current.rstrip().removesuffix(","))
    return out


def flags_of(row: Row) -> str:
    parts = []
    if "SAVE" in row.flags:
        parts.append("EElysiumField::Save")
    if "INPUT" in row.flags:
        parts.append("EElysiumField::Key")
    return " | ".join(parts) if parts else "EElysiumField::None"


def _header(model: Model) -> list[str]:
    provenance = (f"(`research/ghidra/types/datamap_records-vampire.dll.json`; "
                  + ", ".join(f"{cls} {model.datamaps[cls]}"
                              for _, tables in BINDING_CLASSES for cls in tables) + ").")
    return [
        "// Generated by `uv run elysium research gen_kernel_bindings`. Do not hand-edit.",
        "//",
        *_comment("The datamap-backed field/input bindings of the port's NPC-family classes, "
                  "transcribed from the datamap replay " + provenance),
        *_comment("The replay carries no module hash line, so the datamap addresses are the "
                  "provenance this file holds."),
    ]


def render_header(model: Model) -> str:
    out = _header(model)
    out += [
        "",
        '#include "Containers/ArrayView.h"',
        "",
        "struct FElysiumClassDesc;",
        "",
        "namespace ElysiumNpcKernelBindings",
        "{",
        "\tenum class EClass : uint8 { Npc, NpcMaker, InterestingPlace };",
        "",
        "\tvoid AddNpcFields(FElysiumClassDesc& D);",
        "\tvoid AddNpcMakerFields(FElysiumClassDesc& D);",
        "\tvoid AddInterestingPlaceFields(FElysiumClassDesc& D);",
        "\tTConstArrayView<const TCHAR*> Outputs(EClass Class = EClass::Npc);",
        "\tTConstArrayView<const TCHAR*> InputFuncs(EClass Class = EClass::Npc);",
        "\tstruct FCounts",
        "\t{",
        "\t\tint32 Bound;",
        "\t\tint32 Unbound;",
        "\t\tint32 Outputs;",
        "\t\tint32 InputFuncs;",
        "\t};",
        "\tFCounts Counts(EClass Class = EClass::Npc);",
        "}",
    ]
    return "\n".join(out) + "\n"


def _add_function_name(model_class: ClassModel) -> str:
    if model_class.name == "Npc":
        return "AddNpcFields"
    return f"Add{model_class.name}Fields"


def _render_add(model_class: ClassModel) -> list[str]:
    out = [
        f"\tvoid {_add_function_name(model_class)}(FElysiumClassDesc& D)",
        "\t{",
        "\t\t// One row per replay field row the class's member map binds, sorted by external.",
        "\t\t// Flags: SAVE -> EElysiumField::Save, INPUT -> EElysiumField::Key, neither ->",
        "\t\t// EElysiumField::None; KEY alone adds no flag, because the registry applies spawn",
        "\t\t// keyvalues regardless of Key.",
    ]
    for row in model_class.bound:
        assert row.binding is not None
        port_type, member = row.binding
        code = (f"ElysiumAddClassField(D, {_literal(row.external)}, "
                f"&{port_type}::{member}, {flags_of(row)});")
        lines = _wrapped(code, "\t\t")
        lines[-1] += f"  // +0x{row.offset:x} {row.name}"
        out += lines
    for row in model_class.unbound:
        out.append(f"\t\t// UNBOUND +0x{row.offset:x} {row.name} "
                   f"\"{row.external}\" — {row.reason}")
    out += ["\t}", ""]
    return out


def _array_symbol(model_class: ClassModel) -> str:
    return {"Npc": "Npc", "NpcMaker": "NpcMaker",
            "InterestingPlace": "InterestingPlace"}[model_class.name]


def _array_block(model_class: ClassModel, accessor: str, rows: list[Row]) -> list[str]:
    """One file-scope name table; a class with no rows of a kind emits none."""
    if not rows:
        return []
    out = [f"\tconst TCHAR* const G{_array_symbol(model_class)}{accessor}[] =",
           "\t{"]
    out += [f"\t\t{_literal(row.external)}," for row in rows]
    out += ["\t};", ""]
    return out


def render_cpp(model: Model) -> str:
    out = _header(model)
    out += [
        "",
        '#include "Substrate/ElysiumNpcKernelBindings.h"',
        "",
        '#include "Substrate/ElysiumClassFields.h"',
        '#include "Substrate/ElysiumInterestingPlace.h"',
        '#include "Substrate/ElysiumNpc.h"',
        '#include "Substrate/ElysiumNpcMaker.h"',
        "",
        "namespace ElysiumNpcKernelBindings",
        "{",
    ]
    for model_class in model.classes:
        out += _render_add(model_class)
    out += ["\tnamespace", "\t{"]
    for model_class in model.classes:
        out += _array_block(model_class, "Outputs", model_class.outputs)
        out += _array_block(model_class, "InputFuncs", model_class.inputfuncs)
    out += ["\t}", ""]

    for accessor in ("Outputs", "InputFuncs"):
        out += [f"\tTConstArrayView<const TCHAR*> {accessor}(EClass Class)", "\t{",
                "\t\tswitch (Class)", "\t\t{"]
        for model_class in model.classes:
            if model_class.name == "Npc":
                continue
            rows = model_class.outputs if accessor == "Outputs" \
                else model_class.inputfuncs
            out.append(f"\t\t\tcase EClass::{model_class.name}:")
            if rows:
                out.append(f"\t\t\t\treturn MakeArrayView(G{_array_symbol(model_class)}"
                           f"{accessor});")
            else:
                out.append("\t\t\t\treturn TConstArrayView<const TCHAR*>();")
        out += ["\t\t\tdefault:", f"\t\t\t\treturn MakeArrayView(GNpc{accessor});",
                "\t\t}", "\t}", ""]

    out += ["\tFCounts Counts(EClass Class)", "\t{", "\t\tswitch (Class)", "\t\t{"]
    npc = next(c for c in model.classes if c.name == "Npc")
    for model_class in model.classes:
        if model_class.name == "Npc":
            continue
        counts = ", ".join(str(n) for n in (len(model_class.bound), len(model_class.unbound),
                                            len(model_class.outputs),
                                            len(model_class.inputfuncs)))
        out.append(f"\t\t\tcase EClass::{model_class.name}:")
        out.append(f"\t\t\t\treturn {{{counts}}};")
    counts = ", ".join(str(n) for n in (len(npc.bound), len(npc.unbound),
                                        len(npc.outputs), len(npc.inputfuncs)))
    out += ["\t\t\tdefault:", f"\t\t\t\treturn {{{counts}}};", "\t\t}", "\t}", "}"]
    return "\n".join(out) + "\n"


# --- Driver -------------------------------------------------------------------------------------


def _emit(output: Path, text: str, check: bool) -> int:
    if check:
        if not output.is_file():
            print(f"\nCHECK FAILED: {output} is missing")
            return 1
        # `newline=""`: compare the bytes as written, with no universal-newline translation.
        with open(output, encoding="utf-8", newline="") as handle:
            current = handle.read().replace("\r\n", "\n")
        if current != text:
            print(f"\nCHECK FAILED: {output} is stale; regenerate it")
            diff = list(difflib.unified_diff(current.splitlines(), text.splitlines(),
                                             "committed", "generated", lineterm=""))
            for line in diff[:40]:
                print("  " + line)
            if len(diff) > 40:
                print(f"  … {len(diff) - 40} more diff lines")
            return 1
        print(f"check: {output.name} matches the replay")
        return 0
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {output}")
    return 0


def report(model: Model) -> None:
    line = "=" * 78
    for model_class in model.classes:
        tables = ", ".join(model_class.tables)
        print()
        print(line)
        print(f"{model_class.name} — {tables}")
        print(line)
        print("Table A — bound rows (external, type, member, flags)")
        print(line)
        for row in model_class.bound:
            assert row.binding is not None
            port_type, member = row.binding
            print(f"  {row.external:<26} {port_type}.{member:<24} {flags_of(row):<38} "
                  f"+0x{row.offset:x} {row.name}")
        print()
        print("Table B — unbound rows (external, name, offset, reason)")
        print(line)
        for row in model_class.unbound:
            print(f"  {row.external:<26} {row.name:<28} +0x{row.offset:<5x} {row.reason}")
        print()
        print("Table C — inputfuncs")
        print(line)
        print("  " + ", ".join(row.external for row in model_class.inputfuncs))
        print()
        print("Table D — outputs")
        print(line)
        print("  " + ", ".join(row.external for row in model_class.outputs))
        print()
        print("Table E — counts")
        print(line)
        print(f"  bound       {len(model_class.bound):4d}")
        print(f"  unbound     {len(model_class.unbound):4d}")
        print(f"  outputs     {len(model_class.outputs):4d}")
        print(f"  inputfuncs  {len(model_class.inputfuncs):4d}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true",
                        help="verify the committed files match; write nothing")
    parser.add_argument("--report", action="store_true",
                        help="print the classification tables and exit, writing nothing")
    args = parser.parse_args(argv)

    model = build(repo_root())
    report(model)
    if args.report:
        return 0

    repo = repo_root()
    status = 0
    status |= _emit(repo.joinpath(*BINDINGS_H), render_header(model), args.check)
    status |= _emit(repo.joinpath(*BINDINGS_CPP), render_cpp(model), args.check)
    return status


if __name__ == "__main__":
    sys.exit(main())
