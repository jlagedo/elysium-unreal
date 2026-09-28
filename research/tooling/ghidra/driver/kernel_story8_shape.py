#!/usr/bin/env python3
"""Story 8's shape commit: every pass-I override declared and forwarding, one file set per family.

Spec 0019 story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` rules R1/R2 and
step 1. Twelve porters (the thirteen pass-I families) write `.cpp` only, in files they alone own,
and no lane edits a shared header; so the whole C++ surface a family needs exists before any body
is ported, and nothing the runtime does changes. This tool emits that surface from the inputs the
residue pin is made of:

* the residue pin's producer, `kernel_shape.override_rows` (which rows are `stub` / `no-override`);
* the thirteen family row files `$ELYSIUM_WORK_ROOT/research/npc-kernel-checklist/families-19-29/
  <Family>.tsv` joined with the verdict overlay `kernel_verdicts.tsv` (only `rule` rows count);
* the class map `kernel_classes.tsv` and the generated slot surface (`gen_kernel_shape.build`).

What it emits (idempotently -- a second run changes nothing; `--check` writes nothing and fails on
any difference):

a. A **forwarding override** per `no-override` rule row, declared on the port class of the retail
   class that owns the body (the most-base port class up the port chain whose retail class fills
   the slot with the same address, which is the class `override_rows` credits), spelled as
   `override_rows` expects -- the slot's bare port name and its lowered signature -- in a marked
   section of the class header, and defined in `ElysiumNpc<Family>Species.cpp` as a call to the
   explicit port base (the species classes define no `Super`). Behaviour is unchanged: the
   override calls exactly what the inherited dispatch reached.
b. For every `stub` rule row on `FElysiumNpcBase` / `FElysiumNpc` (the spine slots) the overlay
   target becomes `hand:<Owner>::<slot virtual>` and the definition moves to the family's spine
   file (`ElysiumNpcBase<Family>.cpp` / `ElysiumNpc<Family>.cpp`) with a body that tallies
   `elysium.stubs` exactly as the generated stub did (same surface, address, story). Run
   `gen_kernel_shape` afterwards so the generated files drop those definitions.
c. Per family, the empty files a porter needs: the two spine `.cpp`/`.inl` pairs (the `.inl`s are
   included inside the class bodies of `ElysiumNpcBase.h` / `ElysiumNpc.h`), the species part file,
   the family test file (and a second part of each for StartTask19's Troika body).
   File stems are `kernel_gate.family_stem`: the family name less its `19`, numbered where the
   stem already held an older concern's file (`ElysiumNpcDamage3.cpp`).
d. The pin `docs/vtmb/npc-kernel/story8-forwarding.tsv`: one row per `STORY8-FORWARD` body, read
   back from the markers; it must only fall to 0 as porters replace bodies.

A block whose marker a porter removed is the port, not this tool's: it is never re-emitted, and its
declaration stays. Rows this tool cannot forward without a behaviour change are listed, not guessed.

Usage::

    uv run elysium research kernel_story8_shape
    uv run elysium research kernel_story8_shape --check
"""

from __future__ import annotations

import argparse
import collections
import csv
import os
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

_HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(_HERE))
sys.path.insert(0, str(_HERE.parents[1]))

import gen_kernel_shape as g  # noqa: E402
import kernel_ledger as kl  # noqa: E402
import kernel_shape as ks  # noqa: E402
from kernel_gate import family_stem  # noqa: E402

from elysium_pipeline.paths import repo_root  # noqa: E402

FAMILIES = ("Conditions19", "RunAi19", "StartTask19", "RunTask19", "Select19", "Think19", "Spawn19",
            "Damage19", "Script19", "Boss19", "Werewolf19", "Misc19", "Damaged19")
# The family whose Troika body is split between two porters along the packet's chunk boundaries.
SPLIT = {"StartTask19": "0x102a1910"}
SPINE = ("FElysiumNpcBase", "FElysiumNpc")
BASE_RETAIL = {"FElysiumNpcBase": "CAI_BaseNPC", "FElysiumNpc": "CAI_BaseNPCTroika"}
MARK = "STORY8-FORWARD"
MARK_TAIL = "forwarding stub, not the port; the porter replaces this body."
SECTION = "// --- 0019/8 shape: forwarding overrides (replace the body, keep the declaration) ---"
SUBSTRATE = Path("Source/ElysiumUE/Private/Substrate")
TESTS = Path("Source/ElysiumUE/Private/Tests")
PIN = Path("docs/vtmb/npc-kernel/story8-forwarding.tsv")
VERDICTS = _HERE / "kernel_verdicts.tsv"
CLASSES = _HERE / "kernel_classes.tsv"
MARK_RE = re.compile(rf"^// {MARK} slot (\d+) 0x([0-9a-f]{{8}}) (\w+)::(\w+) ")

# The spine rows whose overlay target named another method than the slot's generated virtual: the
# flip writes the virtual's name (the body the generator declares and the spine file defines), and
# the block says what the row named before. Address -> the target it named.
RENAMED = {
    "10265e90": "FElysiumNpc::BaseOnTakeDamage",
    "10265ad0": "FElysiumNpc::BaseEvent_Killed",
    "10265ed0": "FElysiumNpc::BaseOnTakeDamage_Alive",
    "1026ca80": "FElysiumNpc::BaseNPCThink",
    "1026f110": "FElysiumNpc::BaseRunAI",
    "1026ec30": "FElysiumNpc::BaseGatherConditions",
    "102827f0": "FElysiumNpc::BaseStartTask",
    "10288780": "FElysiumNpc::BaseRunTask",
    "10292de0": "FElysiumNpc::Think",
}

# A PORT-mapped slot's override is of the port's own method, whose signature is the hand
# declaration's, not a lowering: slot -> (return, parameters with names and defaults, const).
# Each is checked against the declaration `override_rows` reads on the base.
PORT_SIGNATURES = {
    103: ("void", "", False),
    113: ("void", "", False),
    259: ("bool", "const FElysiumAnimEvent& Event", False),
    379: ("bool", "const FElysiumEntityHandle& Partner, EElysiumGrappleRole Role, "
                  "EElysiumGrappleType Type, int32 Position = INDEX_NONE, bool bHolster = true", False),
    438: ("int32", "", False),
}
# PORT-mapped slots whose port method is not virtual: an override cannot forward, and making the
# base virtual is a dispatch decision for the porter, not a shape.
NOT_VIRTUAL = {77: "`FElysiumEntity::ScriptHide` is not virtual (`ElysiumEntity.h`)"}


@dataclass
class Forward:
    """One forwarding override: a declaring port class, a slot, a body, the classes it carries."""

    family: str
    port: str
    retail: str
    base: str
    slot: int
    address: str          # 0x-prefixed
    name: str
    ret: str
    params: list[tuple[str, str]]      # (type, name) -- the name carries a default when it has one
    const: bool
    carries: list[str] = field(default_factory=list)

    def params_decl(self) -> str:
        return ", ".join(f"{t} {n}" for t, n in self.params)

    def params_def(self) -> str:
        return ", ".join(f"{t} {n.split('=')[0].strip()}" for t, n in self.params)

    def args(self) -> str:
        return ", ".join(n.split("=")[0].strip() for _, n in self.params)


@dataclass
class Spine:
    """One spine stub turned hand-owned: the slot row's owner, name and tally."""

    family: str
    owner: str
    retail: str
    slot: int
    address: str
    name: str
    declaration: str      # `<ret> <Owner>::<Name>(<types>)[ const]`
    ret: str
    story: str
    target: str           # the overlay target before the flip


# --- inputs ---------------------------------------------------------------------------------------


def family_rows() -> dict[str, list[str]]:
    root = Path(os.environ["ELYSIUM_WORK_ROOT"]) / "research" / "npc-kernel-checklist" / "families-19-29"
    out: dict[str, list[str]] = {}
    for family in FAMILIES:
        rows = []
        for line in (root / f"{family}.tsv").read_text(encoding="utf-8").splitlines():
            if line.startswith("#") or not line.strip():
                continue
            rows.append(line.split("\t")[0].strip().lower())
        out[family] = rows
    return out


def verdict_rows() -> dict[str, list[str]]:
    out = {}
    for line in VERDICTS.read_text(encoding="utf-8").splitlines():
        if line.startswith("#") or line.startswith("address\t"):
            continue
        cells = line.split("\t")
        if len(cells) >= 4:
            out[cells[0]] = cells
    return out


def function_labels(repo: Path) -> dict[str, str]:
    labels = {}
    for line in (repo / "docs/vtmb/npc-kernel/functions.md").read_text(encoding="utf-8").splitlines():
        m = re.match(r"^\| `0x([0-9a-f]{8})` \| ([^|]+?) \|", line)
        if m:
            labels[m.group(1)] = m.group(2).strip()
    return labels


def strip_comments(text: str) -> str:
    return re.sub(r"//[^\n]*", "", re.sub(r"/\*.*?\*/", "", text, flags=re.S))


def class_body_span(text: str, port: str) -> tuple[int, int]:
    """(index after the class's opening brace, index of its closing `};` line start)."""
    m = re.search(rf"^class {re.escape(port)}\b[^;{{]*\{{", text, flags=re.M)
    if not m:
        raise SystemExit(f"kernel_story8_shape: no `class {port}` in its header")
    depth, i = 1, m.end()
    code = text
    while depth:
        ch = code[i]
        if code.startswith("//", i):
            i = code.index("\n", i)
            continue
        if code.startswith("/*", i):
            i = code.index("*/", i) + 2
            continue
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
        i += 1
    close = text.rfind("\n", 0, i - 1) + 1
    return m.end(), close


# --- the model ------------------------------------------------------------------------------------


def split_params(text: str) -> list[str]:
    out, depth, cur = [], 0, ""
    for ch in text:
        depth += ch == "<"
        depth -= ch == ">"
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def gather(repo: Path):
    fam_rows = family_rows()
    verdicts = verdict_rows()
    rule = {}
    for family, rows in fam_rows.items():
        for addr in rows:
            if verdicts.get(addr, [None, None])[1] == "rule":
                rule.setdefault(addr, family)
    model = g.build(repo, kl.MODULE, kl.DEFAULT_DEPTH)
    ported, unported = ks.override_rows(repo, model)

    classes = list(csv.DictReader(CLASSES.read_text(encoding="utf-8").splitlines(), delimiter="\t"))
    ports = {r["retail_class"]: r["port_class"] for r in classes}
    port_base = {r["port_class"]: r["port_base"] for r in classes if r["port_class"].startswith("F")}
    retail_of = {p: r for r, p in ports.items() if p.startswith("F")}
    body_of = {(r.cls, r.slot): r.address for r in model.overrides}
    slot_rows = {r.slot: r for r in model.slots}
    branch_slots = {(r.cls, r.slot) for r in model.branch}
    retail_base = {r["retail_class"]: r["retail_base"] for r in classes}

    def introduced_by_branch(cls: str, slot: int) -> str:
        cursor = cls
        while cursor:
            if (cursor, slot) in branch_slots:
                return cursor
            cursor = retail_base.get(cursor, "")
        return ""

    substrate = repo / SUBSTRATE
    marks = read_marks(repo)

    # a. species rows: the unported ones, and the ones this tool already carries (marker present).
    groups: dict[tuple[str, int, str], list[str]] = collections.defaultdict(list)
    skipped: list[str] = []
    for cls, slot_text, address, verdict, kind, port in unported:
        addr = address[2:]
        if kind != "no-override" or addr not in rule:
            continue
        slot = int(slot_text)
        top, cursor = port, port_base.get(port, "")
        while cursor.startswith("F") and cursor not in SPINE \
                and body_of.get((retail_of.get(cursor, ""), slot)) == address:
            top, cursor = cursor, port_base.get(cursor, "")
        groups[(top, slot, address)].append(cls)
    for row in ported:
        addr = row["address"][2:]
        if addr in rule and (row["slot"], addr) in marks:
            groups[(row["declared"], row["slot"], row["address"])].append(row["cls"])

    forwards: list[Forward] = []
    for (top, slot, address), carried in sorted(groups.items()):
        addr = address[2:]
        family = rule[addr]
        retail = retail_of[top]
        what = f"{family} {retail} slot {slot} {address}"
        if introduced_by_branch(retail, slot):
            skipped.append(f"{what}: a branch virtual past the Troika table (introduced by "
                           f"`{introduced_by_branch(retail, slot)}`); the port declares no base to "
                           "forward to")
            continue
        if slot in NOT_VIRTUAL:
            skipped.append(f"{what}: {NOT_VIRTUAL[slot]}")
            continue
        srow = slot_rows[slot]
        name = srow.port_name.rsplit("::", 1)[-1]
        if srow.port_kind == g.PORT:
            if slot not in PORT_SIGNATURES:
                skipped.append(f"{what}: PORT slot `{srow.port_name}` has no reviewed signature")
                continue
            ret, params_text, const = PORT_SIGNATURES[slot]
            params = []
            for part in split_params(params_text):
                head, _, default = part.partition("=")
                m = re.match(r"^(.*?[\s\*&])([A-Za-z_]\w*)$", head.strip())
                params.append((m.group(1).strip(), m.group(2) + (f" = {default.strip()}" if default else "")))
        else:
            ret, const = srow.ret_port, srow.const
            params = [(t, f"Arg{i}") for i, t in enumerate(srow.params_port)]
        header = substrate / f"{top[1:]}.h"
        if not header.is_file():
            skipped.append(f"{what}: no header {header.name}")
            continue
        text = header.read_text(encoding="utf-8-sig")
        start, close = class_body_span(text, top)
        body = strip_comments(text[start:close])
        if re.search(rf"\b{re.escape(name)}\s*\(", body) and (slot, addr) not in marks \
                and not re.search(rf"\b{re.escape(name)}\s*\([^;{{]*\)[^;{{]*\boverride\b", body):
            skipped.append(f"{what}: `{top}` already declares `{name}` without `override`")
            continue
        macro = re.search(r"ELYSIUM_NPC_CLASS\(\"(\w+)\",\s*(\w+)\)", text)
        base = port_base[top]
        if not macro or macro.group(2) != base or macro.group(1) != retail:
            raise SystemExit(f"kernel_story8_shape: {header.name}'s ELYSIUM_NPC_CLASS disagrees with "
                             f"kernel_classes.tsv ({retail}, {base})")
        forwards.append(Forward(family, top, retail, base, slot, address, name, ret, params, const,
                                sorted(set(carried))))

    # b. spine rows: generated slot bodies on the two spine classes whose body is a family rule.
    spines: list[Spine] = []
    for srow in model.slots:
        for layer in srow.layers:
            addr = layer.body
            if layer.owner not in SPINE or addr not in rule or layer.default or layer.verdict != "rule":
                continue
            if not (layer.stubbed or (layer.hand and (srow.slot, addr) in marks)):
                continue
            target = verdicts[addr][3]
            before = RENAMED.get(addr, target.removeprefix(g.HAND_PREFIX))
            want = f"{layer.owner}::{layer.port_name}"
            if before != want and addr not in RENAMED:
                raise SystemExit(f"kernel_story8_shape: 0x{addr}'s target `{before}` is not the slot's "
                                 f"virtual `{want}`; add it to RENAMED after review")
            spines.append(Spine(rule[addr], layer.owner, layer.retail, srow.slot, f"0x{addr}",
                                layer.port_name, layer.port_declaration(f"{layer.owner}::"),
                                layer.ret_port, layer.story, before))
    return fam_rows, rule, forwards, spines, skipped, verdicts


def read_marks(repo: Path) -> dict[tuple[int, str], tuple[str, str]]:
    """(slot, address) -> (retail class, file) for every marker in the substrate."""
    out = {}
    for path in sorted((repo / SUBSTRATE).glob("*.cpp")):
        for line in path.read_text(encoding="utf-8-sig").splitlines():
            m = MARK_RE.match(line)
            if m:
                out[(int(m.group(1)), m.group(2))] = (m.group(3), path.name)
    return out


# --- rendering ------------------------------------------------------------------------------------


def wrap_comment(text: str, width: int = 100) -> list[str]:
    words, lines, cur = text.split(" "), [], "//"
    for word in words:
        if len(cur) + 1 + len(word) > width and cur != "//":
            lines.append(cur)
            cur = "//"
        cur += " " + word
    lines.append(cur)
    return lines


def owned_list(addresses: list[str], labels: dict[str, str]) -> str:
    return ", ".join(f"0x{a} {labels.get(a, '(unnamed)')}" for a in addresses) or "none"


def file_header(family: str, what: str, owns: str, labels, extra: list[str] = ()) -> list[str]:
    lines = wrap_comment(f"Story 0019/8 (29e under the strict verdict), family **{family}** -- {what}")
    lines.append("//")
    lines += wrap_comment("Created by the story-8 shape commit (`uv run elysium research "
                          "kernel_story8_shape`, spec 0019 story 8, "
                          "`docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), "
                          "before any body is ported, so that the family's lane owns this file alone.")
    for line in extra:
        lines.append("//")
        lines += wrap_comment(line)
    lines.append("//")
    lines += wrap_comment(f"Owns ({family}'s `rule` rows): {owns}.")
    return lines


def spine_block(s: Spine, fire: str) -> list[str]:
    lines = [f"// {MARK} slot {s.slot} {s.address} {s.retail}::{s.name} — {MARK_TAIL}"]
    if s.address[2:] in RENAMED:
        lines += wrap_comment(f"The verdict row named `{RENAMED[s.address[2:]]}`; the slot's generated "
                              f"virtual is `{s.name}`, so the definition and the `hand:` target use "
                              f"`{s.owner}::{s.name}`.")
    lines += wrap_comment("The body tallies `elysium.stubs` exactly as the generated stub did.")
    lines += [s.declaration, "{",
              f"\t{fire}(TEXT(\"{s.retail}::{s.name}\"), TEXT(\"{s.address}\"), TEXT(\"{s.story}\"), "
              "DebugString());"]
    if s.ret != "void":
        lines.append("\treturn {};")
    lines.append("}")
    return lines


def forward_block(f: Forward) -> list[str]:
    lines = [f"// {MARK} slot {f.slot} {f.address} {f.retail}::{f.name} — {MARK_TAIL}"]
    inherited = [c for c in f.carries if c != f.retail]
    if inherited:
        lines += wrap_comment(f"Also carries the inherited body of {', '.join(inherited)}.")
    call = f"{f.base}::{f.name}({f.args()})"
    lines += [f"{f.ret} {f.port}::{f.name}({f.params_def()}){' const' if f.const else ''}", "{",
              f"\t{'return ' if f.ret != 'void' else ''}{call};", "}"]
    return lines


def declaration(f: Forward) -> str:
    return f"\tvirtual {f.ret} {f.name}({f.params_decl()}){' const' if f.const else ''} override;"


def replace_blocks(text: str, blocks: dict[tuple[int, str], list[str]]) -> tuple[str, set]:
    """Rewrite every marker block this tool owns to its canonical text; return the keys present."""
    lines = text.split("\n")
    out, seen, i = [], set(), 0
    while i < len(lines):
        m = MARK_RE.match(lines[i])
        if m and (int(m.group(1)), m.group(2)) in blocks:
            key = (int(m.group(1)), m.group(2))
            end = i
            while lines[end] != "}":
                end += 1
            out += blocks[key]
            seen.add(key)
            i = end + 1
            continue
        out.append(lines[i])
        i += 1
    return "\n".join(out), seen


def emit_blocks(existing: str | None, head: list[str], blocks: dict[tuple[int, str], list[str]],
                marks: dict) -> str:
    if existing is None:
        body = head[:]
        for key in sorted(blocks):
            body += ["", *blocks[key]]
        return "\n".join(body) + "\n"
    text, seen = replace_blocks(existing, blocks)
    missing = [k for k in sorted(blocks) if k not in seen]
    if missing:
        text = text.rstrip("\n") + "\n"
        for key in missing:
            text += "\n" + "\n".join(blocks[key]) + "\n"
    return text


# --- main -----------------------------------------------------------------------------------------


def plan(repo: Path) -> tuple[dict[Path, str], list[str], list[Forward], list[Spine], list[tuple]]:
    fam_rows, rule, forwards, spines, skipped, verdicts = gather(repo)
    labels = function_labels(repo)
    marks = read_marks(repo)
    classes = list(csv.DictReader(CLASSES.read_text(encoding="utf-8").splitlines(), delimiter="\t"))
    species_retail = {r["retail_class"] for r in classes
                      if r["port_class"].startswith("F") and r["port_class"] not in SPINE}
    files: dict[Path, str] = {}

    def current(path: Path) -> str | None:
        full = repo / path
        if full in files:
            return files[full]
        return full.read_text(encoding="utf-8-sig") if full.is_file() else None

    def put(path: Path, text: str) -> None:
        files[repo / path] = text

    # d-prep: the overlay flip.
    flipped: list[tuple] = []
    lines = VERDICTS.read_text(encoding="utf-8").split("\n")
    by_addr = {s.address[2:]: s for s in spines}
    for index, line in enumerate(lines):
        cells = line.split("\t")
        if len(cells) >= 4 and cells[0] in by_addr:
            s = by_addr[cells[0]]
            want = f"{g.HAND_PREFIX}{s.owner}::{s.name}"
            if cells[3] != want:
                flipped.append((cells[0], cells[3], want))
                cells[3] = want
                lines[index] = "\t".join(cells)
    files[VERDICTS] = "\n".join(lines)

    for family in FAMILIES:
        rows = [a for a in fam_rows[family] if rule.get(a) == family]

        def cls(a: str) -> str:
            return labels.get(a, "").split("::", 1)[0] if "::" in labels.get(a, "") else ""
        base_rows = [a for a in rows if cls(a) == "CAI_BaseNPC"]
        species_rows = [a for a in rows if cls(a) in species_retail]
        troika_rows = [a for a in rows if a not in base_rows and a not in species_rows]
        split = SPLIT.get(family)

        for owner, stem, owned in (("FElysiumNpcBase", family_stem("ElysiumNpcBase", family), base_rows),
                                   ("FElysiumNpc", family_stem("ElysiumNpc", family), troika_rows)):
            header = "Substrate/ElysiumNpcBase.h" if owner == "FElysiumNpcBase" else "Substrate/ElysiumNpc.h"
            retail = BASE_RETAIL[owner]
            inl = SUBSTRATE / f"{stem}.inl"
            if current(inl) is None:
                put(inl, "\n".join(file_header(
                    family, f"`{retail}`'s helper declarations.", owned_list(owned, labels), labels,
                    [f"Included inside `class {owner}` by `{header}`; the definitions are in "
                     f"`{stem}.cpp`, or generated in the slot files for a slot body."])
                    + ["", f"// helper declarations of the {family} porter go here"]) + "\n")
            mine = [s for s in spines if s.family == family and s.owner == owner]
            fire = f"Fire{family}{'Base' if owner == 'FElysiumNpcBase' else ''}Slot"
            head = file_header(family, f"`{retail}`'s bodies.", owned_list(owned, labels), labels,
                               [f"Declarations are in `{stem}.inl` (included inside `class {owner}`) "
                                f"or generated in `{'ElysiumNpcBaseSlots' if owner == 'FElysiumNpcBase' else 'ElysiumNpcSlots'}.inl` "
                                "for a slot body. A `" + MARK + "` block is the generated stub moved "
                                "here unchanged (its overlay row reads `hand:`); the porter replaces "
                                "the body and drops the marker."]
                               + ([f"`{split}` is shared with `{stem}_2.cpp`: the cut follows the "
                                   "packet's chunk boundaries, one `switch`, retail's default arm "
                                   "once."] if split and owner == "FElysiumNpc" else []))
            head += ["", f'#include "{header}"']
            if mine:
                head += ["", '#include "ElysiumStub.h"', "", "namespace", "{",
                         "\t// The generated slot stubs' tally, verbatim (`gen_kernel_shape`'s "
                         "`Fire*Slot`), unit-prefixed for adaptive unity.",
                         f"\tvoid {fire}(const TCHAR* Surface, const TCHAR* Address, const TCHAR* Story,",
                         "\t\tconst FString& Receiver)", "\t{", "\t\tElysiumStub::FSurface Row;",
                         "\t\tRow.Kind = TEXT(\"slot\");", "\t\tRow.Surface = Surface;",
                         "\t\tRow.Address = Address;", "\t\tRow.Story = Story;",
                         "\t\tElysiumStub::Fired(Row, Receiver, FString(), TEXT(\"the NPC kernel\"));",
                         "\t}", "}"]
            blocks = {(s.slot, s.address[2:]): spine_block(s, fire) for s in mine}
            cpp = SUBSTRATE / f"{stem}.cpp"
            if mine and current(cpp) is not None and fire not in current(cpp):
                raise SystemExit(f"kernel_story8_shape: {cpp.name} exists without `{fire}`; add it by hand")
            put(cpp, emit_blocks(current(cpp), head, blocks, marks))
            if split and owner == "FElysiumNpc":
                cpp2 = SUBSTRATE / f"{stem}_2.cpp"
                if current(cpp2) is None:
                    put(cpp2, "\n".join(file_header(
                        family, f"`{retail}`'s bodies, second part.",
                        f"{split} {labels.get(split[2:], '')} (the arms past the cut; the first part "
                        f"is `{stem}.cpp`)", labels,
                        ["The cut follows the packet's chunk boundaries: one dispatch, retail's "
                         "default arm once, no case body shared across the cut. Unity-build names "
                         f"here are prefixed `{family}_2`."]) + ["", f'#include "{header}"']) + "\n")

        # Species part file.
        mine = [f for f in forwards if f.family == family]
        species = SUBSTRATE / f"{family_stem('ElysiumNpc', family)}Species.cpp"
        head = file_header(family, "the species classes' bodies.", owned_list(species_rows, labels),
                           labels,
                           ["A `" + MARK + "` block is a forwarding override declared on its class "
                            "(the header's `0019/8 shape` section): it calls the port base, which is "
                            "what the inherited dispatch ran, so it changes nothing. The porter "
                            "replaces the body, keeps the declaration, and drops the marker."])
        includes = sorted({f'#include "Substrate/{f.port[1:]}.h"' for f in mine})
        existing = current(species)
        if existing is None:
            head += [""] + (includes or ['#include "Substrate/ElysiumNpc.h"'])
        else:
            missing = [i for i in includes if i not in existing]
            if missing:
                raise SystemExit(f"kernel_story8_shape: {species.name} lacks {missing}; add by hand")
        put(species, emit_blocks(existing, head, {(f.slot, f.address[2:]): forward_block(f)
                                                  for f in mine}, marks))

        # Tests.
        for suffix, what in (("", "the family's tests."), ("_2", "the family's tests, second part.")):
            if suffix and not split:
                continue
            test = TESTS / f"{family_stem('ElysiumNpcKernel', family)}Tests{suffix}.cpp"
            if current(test) is None:
                owns = owned_list(rows, labels) if not suffix else f"{split} (the arms past the cut)"
                put(test, "\n".join(file_header(
                    family, what, owns, labels,
                    [f"Test names carry `Elysium.Substrate.NpcKernel{family}.` and the retail "
                     "address. No tests yet: the porter adds them with the bodies."])
                    + [""] + TEST_PRELUDE) + "\n")

    # Species headers: the section and its declarations.
    by_port: dict[str, list[Forward]] = collections.defaultdict(list)
    for f in forwards:
        by_port[f.port].append(f)
    for port, rows in sorted(by_port.items()):
        path = SUBSTRATE / f"{port[1:]}.h"
        text = current(path)
        start, close = class_body_span(text, port)
        body = strip_comments(text[start:close])
        new = [declaration(f) for f in sorted(rows, key=lambda f: f.slot)
               if not re.search(rf"\b{re.escape(f.name)}\s*\([^;{{]*\)[^;{{]*\boverride\b", body)]
        if not new:
            continue
        if SECTION in text[start:close]:
            at = text.index(SECTION, start)
            end = text.index("\n", at) + 1
            while text.startswith("\tvirtual ", end):
                end = text.index("\n", end) + 1
            text = text[:end] + "".join(line + "\n" for line in new) + text[end:]
        else:
            labels_seen = re.findall(r"^(public|protected|private):", body, flags=re.M)
            access = [] if not labels_seen or labels_seen[-1] == "public" else ["public:"]
            chunk = ["", *access, f"\t{SECTION}", *new]
            prefix = text[:close].rstrip("\n") + "\n"
            text = prefix + "".join(line + "\n" for line in chunk) + text[close:]
        put(path, text)

    # The two class headers include the family `.inl`s next to the State19 ones.
    for header, anchor, stem in (("ElysiumNpcBase.h", "ElysiumNpcBaseTranslate.inl", "ElysiumNpcBase"),
                                 ("ElysiumNpc.h", "ElysiumNpcMaintain.inl", "ElysiumNpc")):
        path = SUBSTRATE / header
        text = current(path)
        want = [f'\t#include "Substrate/{family_stem(stem, family)}.inl"' for family in FAMILIES]
        missing = [w for w in want if w not in text]
        if missing:
            line = f'\t#include "Substrate/{anchor}"\n'
            at = text.index(line) + len(line)
            text = text[:at] + "".join(m + "\n" for m in missing) + text[at:]
            put(path, text)
    return files, skipped, forwards, spines, flipped


TEST_PRELUDE = [
    '#include "Misc/AutomationTest.h"',
    "",
    "#if WITH_DEV_AUTOMATION_TESTS",
    "",
    '#include "ElysiumEntityDefs.h"',
    '#include "ElysiumMoveSolve.h"',
    '#include "ElysiumPlayer.h"',
    '#include "ElysiumRng.h"',
    '#include "Substrate/ElysiumGameSound.h"',
    '#include "Substrate/ElysiumNpc.h"',
    '#include "Substrate/ElysiumNpcConditions.h"',
    '#include "Substrate/ElysiumNpcEnemy.h"',
    '#include "ElysiumNpcFlags.h"',
    '#include "Substrate/ElysiumNpcSenses.h"',
    '#include "Substrate/ElysiumNpcWitness.h"',
    '#include "Substrate/ElysiumSchedule.h"',
    '#include "Tests/ElysiumNpcTestFixture.h"',
    '#include "Tests/ElysiumNpcTestCensus.h"',
    "",
    "#endif  // WITH_DEV_AUTOMATION_TESTS",
]


def file_family(name: str) -> str:
    """The family whose spine, `_2` or species part file `name` is."""
    stem = name.rsplit(".", 1)[0]
    for family in FAMILIES:
        for prefix in ("ElysiumNpcBase", "ElysiumNpc"):
            own = family_stem(prefix, family)
            if stem in (own, f"{own}_2", f"{own}Species"):
                return family
    raise SystemExit(f"kernel_story8_shape: {name} carries a {MARK} marker but is no family's file")


def render_pin(repo: Path) -> str:
    """The pin, read back from the markers on disk (so `--check` diffs it against the tree)."""
    rows = []
    for path in sorted((repo / SUBSTRATE).glob("*.cpp")):
        for line in path.read_text(encoding="utf-8-sig").splitlines():
            m = MARK_RE.match(line)
            if m:
                family = file_family(path.name)
                rows.append((family, m.group(3), m.group(1), f"0x{m.group(2)}",
                             (SUBSTRATE / path.name).as_posix()))
    rows.sort(key=lambda r: (r[0], r[1], int(r[2]), r[3]))
    return ("# Story 8's forwarding bodies (`uv run elysium research kernel_story8_shape`): one row per "
            f"`{MARK}` marker under Source/. A porter replaces the body and drops the marker; the set "
            "must only fall to 0.\n"
            "family\tclass\tslot\taddress\tfile\n" + "".join("\t".join(r) + "\n" for r in rows))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="verify; write nothing")
    args = parser.parse_args(argv)
    repo = repo_root()
    files, skipped, forwards, spines, flipped = plan(repo)
    stale = []
    for path, text in sorted(files.items()):
        old = path.read_text(encoding="utf-8-sig").replace("\r\n", "\n") if path.is_file() else None
        if old != text:
            stale.append(path)
            if not args.check:
                path.write_text(text, encoding="utf-8", newline="\n")
    pin = render_pin(repo)
    pin_path = repo / PIN
    old_pin = pin_path.read_text(encoding="utf-8") if pin_path.is_file() else None
    if old_pin != pin:
        stale.append(pin_path)
        if not args.check:
            pin_path.write_text(pin, encoding="utf-8", newline="\n")
    print(f"forwarding overrides {len(forwards)} (carrying "
          f"{sum(len(f.carries) for f in forwards)} residue rows) on "
          f"{len({f.port for f in forwards})} classes; spine bodies {len(spines)}; overlay flips "
          f"{len(flipped)}; pin rows {pin.count(chr(10)) - 2}")
    for addr, old, new in flipped:
        print(f"  flip 0x{addr}: {old} -> {new}")
    for line in skipped:
        print(f"  not forwarded: {line}")
    if args.check:
        for path in stale:
            print(f"CHECK FAILED: {path} differs from what kernel_story8_shape emits")
        return 1 if stale else 0
    for path in stale:
        print(f"  wrote {path.relative_to(repo) if path.is_relative_to(repo) else path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
