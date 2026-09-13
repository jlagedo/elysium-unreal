# -*- coding: utf-8 -*-
"""Emit the NPC kernel's shape census as project source.

Owner-run archaeology, not part of any build.  The kernel ledger
(``docs/vtmb/npc-kernel/``) says what ``CAI_BaseNPCTroika`` *is*: every word of
its flattened layout with a type, every primary-vtable slot with a declaration,
and the 77-class tree with the entity classnames each claims.  This generator
transcribes that ledger into three committed artifacts the runtime can assert
against:

* ``ElysiumNpcKernelShape.cpp`` — the **census**.  One row per retail word
  (offset, retail name, type, owning chain layer, tier), one row per slot
  (declaration, tier, the Troika-line body's address and ``order.md`` layer, and
  the port's callable), one row per family class (direct base, vtable, slot
  count, entity classnames) and one row per species slot override.  The census
  is data only: nothing in it invents a behaviour.
* ``ElysiumNpcKernelSlots.inl`` — one ``virtual`` declaration per Troika-line
  slot the port does not already implement under a mapped name, included inside
  ``class FElysiumNpc``.  Each carries ``// slot N  0x……  (tier)``.
* ``ElysiumNpcKernelSlots.cpp`` — those virtuals' bodies: a named stub that
  tallies ``elysium.stubs`` with the retail address and the owning story, so the
  tally joins ``functions.md`` by address.

The shape's *members* are not generated.  Every retail word lands by hand on the
struct that owns its concern and is bound to its offset by the compile-checked
registry in ``ElysiumNpcKernelShapeMap.cpp``; the census is what that registry is
checked against.

Retail types are lowered to the port's, once, by ``lower_type`` below: an entity
class becomes ``FElysiumEntity*``, ``Vector`` becomes ``FVector``, a type the
port has no counterpart for becomes ``void*`` (pointer or reference) or
``int32`` (scalar) and says so in the declaration comment.  Arity is never
changed — an ``unsettled`` slot lands as its recorded word count.

A slot whose retail name is already declared anywhere in the port's entity chain
is a **decision**, not a default: generation fails until ``SLOT_PORT_MAP`` says
which port method is that slot's callable, or that the collision is accidental
and the slot takes a suffixed name.

Since story 29c the generator also reads the **verdict overlay**
(``ghidra/driver/kernel_verdicts.tsv``, rendered as
``docs/vtmb/npc-kernel/checklist-<band>.md``).  A slot whose retail body carries
a verdict stops being an undifferentiated stub:

* ``rule`` with ``default:<literal>`` or ``default:void`` — retail's whole body
  is ``return <literal>;`` or ``return;``.  That is a recovered *fact*, not a
  behaviour someone wrote, so the generator emits the body, records the literal
  in the census, and emits a probe row the automation test calls the virtual
  through.  The same argument the story makes for species overrides: a
  constant-returning virtual is data.
* ``rule``/``present``/``mechanism`` with ``hand:<PortMethod>`` — the body is
  written by hand in the owning substrate file, so the generator declares the
  virtual and emits **no** definition.  A ``hand:`` row with no definition is a
  link error, which is the failure this spelling exists to produce.  Story 29c-1
  added ``mechanism``: a slot whose retail body is an Unreal service call still
  has to ANSWER through the seam rather than tally a stub, and which of the three
  verdicts the body carries says where the answer comes from, not whether the
  slot has one.
* anything else — the stub stays, and the verdict travels in its comment.

Usage::

    uv run elysium research gen_kernel_shape
    uv run elysium research gen_kernel_shape --check
    uv run elysium research gen_kernel_shape --report
"""

from __future__ import annotations

import argparse
import collections
import difflib
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

from elysium_pipeline.paths import repo_root

sys.path.insert(0, str(Path(__file__).resolve().parent / "ghidra" / "driver"))

import kernel_ledger as kl  # noqa: E402
import kernel_shape as ks  # noqa: E402

SUBSTRATE = ("Source", "ElysiumUE", "Private", "Substrate")
CENSUS_OUTPUT = (*SUBSTRATE, "ElysiumNpcKernelShape.cpp")
SLOTS_INL_OUTPUT = (*SUBSTRATE, "ElysiumNpcKernelSlots.inl")
SLOTS_CPP_OUTPUT = (*SUBSTRATE, "ElysiumNpcKernelSlots.cpp")

# The verdict overlay's target spellings that change what the generator emits (story 29c).
DEFAULT_PREFIX = "default:"
HAND_PREFIX = "hand:"
# A retail default the generator will emit. Anything else in a `default:` target is a reading that
# has not settled — a `DAT_`, a `param_1`, a `this` — and generation fails rather than inventing.
DEFAULT_LITERAL_RE = re.compile(r"^(?:void|-?\d+(?:\.\d+)?|0x[0-9a-fA-F]+)$")
REGISTRY_PREFIX = "registry:"
# The whole body of a constant-returning override, and nothing else. Deliberately strict: the
# census may carry the literal a species answers only when the body cannot be anything else.
CONSTANT_BODY_RE = re.compile(
    r"^\s*\{\s*return\s*(;|(?:-?\d+|0x[0-9a-fA-F]+)\s*;)\s*\}\s*$", re.S)

# The table the port's leaf stands for. Species classes add words past its end; those are rows in
# the class registry, not members of the leaf.
BASE_TABLE = "CAI_BaseNPCTroika"

# The chain layers whose words belong to the NPC rather than to the entity classes under it. A word
# below this set is an entity-chain word: the census carries it, the registry maps it to whichever
# port type already owns that concern, and this story declares no new member for it.
NPC_LAYERS = ("CAI_BaseNPC", "CAI_BaseNPCTroika")

# Troika's own table is 617 slots (29b-0, `npc-ai/shape.md`); past it a branch declares unrelated
# virtuals at the same index, and those are class-registry rows.
TROIKA_SLOTS = 617

# `order.md` layer bands to the story that ports them (spec 0002, `## Build order`).
STORY_BANDS = ((0, 9, "29c"), (10, 18, "29d"), (19, 99, "29e"))

# The port headers whose declared method names a generated slot name may not silently shadow, and
# where to stop reading each. `ElysiumPlayer.h` declares `FElysiumAnimating` and
# `FElysiumCombatCharacter` — two of the NPC's own bases — before `FElysiumPlayer`, which is a
# sibling leaf: the scan stops where the player's own surface begins, because a name only the
# player declares is not a name the NPC's chain holds.
PORT_CHAIN_HEADERS = (
    ("Source/ElysiumUE/Public/ElysiumEntity.h", None),
    ("Source/ElysiumUE/Public/ElysiumPlayer.h", "class FElysiumPlayer final"),
    ("Source/ElysiumUE/Private/Substrate/ElysiumCameraOverride.h", None),
    ("Source/ElysiumUE/Private/Substrate/ElysiumScriptedCharacter.h", None),
    ("Source/ElysiumUE/Private/Substrate/ElysiumNpc.h", None),
    ("Source/ElysiumUE/Private/Substrate/ElysiumSchedule.h", None),
)

# A method declaration in one of those headers: a return type (or `virtual`) then `Name(`.
DECL_RE = re.compile(r"^[\t ]*(?:virtual\s+|static\s+|inline\s+)*[A-Za-z_][\w:<>,*&\s]*?"
                     r"\b([A-Za-z_]\w*)\s*\(")

# A data member of one of those classes: one tab of indent (a class body's own level, not an inline
# function's), a type, a name, then an initialiser, a semicolon or an array bound. Data members
# matter as much as methods: a member function declared on the leaf HIDES a base class's data member
# of the same name, and the compiler does not warn.
MEMBER_RE = re.compile(r"^\t(?:mutable\s+|static\s+|constexpr\s+|const\s+)*"
                       r"[A-Za-z_][\w:<>,*&\s]*?\b([A-Za-z_]\w*)\s*(?:=[^=]|;|\[)")

# --- The retail → port type lowering ------------------------------------------------------------
#
# Written once, here, and followed everywhere. The rule has three arms: a type the port already
# carries is spelled as the port spells it; every entity class becomes the port's one entity
# pointer, because this port stands one leaf for the whole `npc_V*` family; anything else is lowered
# to a word of the same size and says what it was lowered from.

SCALARS = {
    "void": "void",
    "bool": "bool",
    "char": "int8",
    "unsigned char": "uint8",
    "short": "int16",
    "unsigned short": "uint16",
    "int": "int32",
    "unsigned int": "uint32",
    "long": "int32",
    "unsigned long": "uint32",
    "float": "float",
    "double": "double",
}

# Retail spellings the port has a real counterpart for.
KNOWN = {
    "const char*": "const TCHAR*",
    "char*": "TCHAR*",
    "string_t": "FName",
    "Vector": "FVector",
    "Vector&": "FVector&",
    "const Vector&": "const FVector&",
    "Vector*": "FVector*",
    "const Vector*": "const FVector*",
    "QAngle": "FRotator",
    "QAngle&": "FRotator&",
    "const QAngle&": "const FRotator&",
    "QAngle*": "FRotator*",
    "EHANDLE": "FElysiumEntityHandle",
    "CBaseHandle&": "FElysiumEntityHandle&",
    "const CBaseHandle&": "const FElysiumEntityHandle&",
    "NPC_STATE": "EElysiumNpcState",
    "Activity": "int32",
}

# The entity base classes the family hangs off. A pointer to any of these, or to a family class, is
# the port's `FElysiumEntity*` — the port registers one leaf per classname and the leaf is the
# entity.
ENTITY_BASES = {
    "CBaseEntity", "CBaseAnimating", "CBaseAnimatingOverlay", "CBaseFlex", "CBaseToggle",
    "CBaseCombatCharacter", "CBaseCombatWeapon", "CBasePlayer", "CBaseDoor", "CBaseGrenade",
    "CBaseProp", "CBaseTrigger", "CBaseViewModel", "CAI_BaseNPC", "CAI_BaseNPCTroika",
}


def lower_type(retail: str, family: set[str]) -> tuple[str, str]:
    """One retail type as the port spells it, plus the note the declaration carries.

    The note is empty when the port has a real counterpart, and names what the word was lowered
    from when it does not.
    """
    text = " ".join(retail.split())
    if not text:
        return "void", ""
    if text in SCALARS:
        return SCALARS[text], ""
    if text in KNOWN:
        return KNOWN[text], ""
    stripped = text.rstrip("*& ")
    base = stripped[len("const "):] if stripped.startswith("const ") else stripped
    base = base.strip()
    if base in ENTITY_BASES or base in family:
        if text.endswith("*"):
            return "FElysiumEntity*", ""
        if text.endswith("&"):
            return "FElysiumEntity*", f"`{text}` by reference"
        return "FElysiumEntity*", f"`{text}` by value"
    if text.endswith("*"):
        return "void*", f"`{text}`"
    if text.endswith("&"):
        return "void*", f"`{text}`"
    # A by-value class or an enum the port has no counterpart for. One word, and the retail
    # spelling travels in the comment so the day it is recovered the row names it.
    return "int32", f"`{text}`"


# --- The slot map -------------------------------------------------------------------------------
#
# A retail slot name the port's entity chain already declares is a decision. `PORT` says the
# existing method IS that slot's body and no virtual is generated for it; `SUFFIX` says the
# collision is accidental and the slot takes `<Name>Slot<N>`; the third column is why.

PORT = "port"
SUFFIX = "suffix"

SLOT_PORT_MAP: dict[int, tuple[str, str, str]] = {}


def _load_slot_map() -> None:
    """Fill `SLOT_PORT_MAP` from the table below.

    Kept as a function so the rows read as a reviewed list rather than a dict literal folded by a
    formatter: slot, kind, the port's callable (or the suffixed name), and the reason.
    """
    rows = (
        # --- The entity chain's own surface ------------------------------------------------------
        # A slot whose concern the port already carries on `FElysiumEntity`,
        # `FElysiumAnimating` or `FElysiumCombatCharacter`. The method there IS the slot's body;
        # a second virtual on the leaf would be a second producer of the same state.
        (15, PORT, "FElysiumNpc::SetAttackExtents", "`CBaseEntity::SetAttackExtents` 0x1009af40"),
        (50, PORT, "FElysiumCombatCharacter::GetCameraViewpointPosition",
         "the dialogue camera's viewpoint source"),
        (51, PORT, "FElysiumCombatCharacter::GetCameraTargetPosition",
         "the dialogue camera's target source"),
        (52, PORT, "FElysiumCombatCharacter::GetCameraFadeInTime", "the camera override's fade in"),
        (53, PORT, "FElysiumCombatCharacter::GetCameraFadeOutTime",
         "the camera override's fade out"),
        (77, PORT, "FElysiumEntity::ScriptHide", "whole-entity off, with the think it saves"),
        (78, PORT, "FElysiumEntity::ScriptUnhide", "its exact inverse"),
        (97, PORT, "FElysiumEntity::GetOwnerEntity", "the owner handle"),
        (103, PORT, "FElysiumNpc::Spawn", "the leaf's own spawn"),
        (113, PORT, "FElysiumNpc::Activate", "the leaf's own activate"),
        (117, PORT, "FElysiumEntity::ObjectCaps", "the `FCAP_*` bitfield the port reads one bit of"),
        (119, PORT, "FElysiumEntity::Kill", "terminal: mark dead and go inert"),
        (134, PORT, "FElysiumNpc::Think", "`NPCThink` 0x10292de0, the whole pass"),
        (173, PORT, "FElysiumEntity::Use", "the `+use` door"),
        (193, PORT, "FElysiumEntity::EyePosition", "origin plus the view offset"),
        (202, PORT, "FElysiumEntity::SetOwnerEntity", "the owner handle's writer"),
        (259, PORT, "FElysiumNpc::HandleAnimEvent", "`CAI_BaseNPC::HandleAnimEvent` 0x10274e30"),
        (294, PORT, "FElysiumNpc::IsValidStealthKillTarget", "0x102c2300"),
        (312, PORT, "FElysiumNpc::UpdateCharacter", "`UpdateCharacter`, on the update clock"),
        (351, PORT, "FElysiumCombatCharacter::FeedBegin", "the feed transaction's entry"),
        (352, PORT, "FElysiumCombatCharacter::Feed", "its per-pulse body"),
        (353, PORT, "FElysiumCombatCharacter::FeedInterrupt", "its interrupt"),
        (368, PORT, "FElysiumCombatCharacter::BodyDirection2D", "the body's flattened facing"),
        (379, PORT, "FElysiumNpc::EnterGrappleState", "the grapple role pair's entry"),
        (380, PORT, "FElysiumNpc::LeaveGrappleState", "its exit"),
        # --- The kernel surface the port already runs -------------------------------------------
        (435, PORT, "FElysiumNpc::OnScheduleChange",
         "the schedule-change release, ported in story 8"),
        (438, PORT, "FElysiumNpc::SelectSchedule",
         "the state switch of the base selector 0x1028a380"),
        (440, PORT, "FElysiumNpc::TranslateSchedule",
         "the schedule translation, ported in story 25"),
        (448, PORT, "FElysiumNpc::TaskFail", "the failure route, ported in story 13"),
        (453, PORT, "FElysiumNpc::BuildScheduleTestBits", "the interrupt mask, ported in story 25"),
        (534, PORT, "FElysiumCombatCharacter::EyeLookTargetHandle",
         "the gaze cascade's chosen subject; `EyeLookTarget` beside it is the point it resolved to"),
        (614, PORT, "FElysiumNpc::ResetThinkTimers", "the four think stamps, ported in story 21"),
        # `ClearSchedule` is not a slot: retail's 0x10280d30 is a non-virtual the kernel calls
        # directly. `FElysiumNpc::ClearSchedule` stands for it and is declared by hand.
    )
    for slot, kind, port_name, why in rows:
        SLOT_PORT_MAP[slot] = (kind, port_name, why)


_load_slot_map()


# --- The model ----------------------------------------------------------------------------------


@dataclass
class Word:
    """One top-level retail word of a layout, with its collapsed interiors."""

    table: str
    offset: int
    member: str
    type: str
    field_type: str
    layer: str
    tier: str
    size: int
    count: int
    interiors: int = 0
    writers: int = 0
    readers: int = 0

    @property
    def npc(self) -> bool:
        return self.layer in NPC_LAYERS


@dataclass
class Slot:
    """One primary-vtable slot, as the port declares it."""

    slot: int
    cls: str
    method: str
    ret: str
    params: str
    const: bool
    tier: str
    words: str
    address: str = ""
    body: str = ""
    layer: int = -1
    story: str = ""
    port_name: str = ""
    port_kind: str = ""
    port_why: str = ""
    notes: list[str] = field(default_factory=list)
    ret_port: str = "void"
    params_port: list[str] = field(default_factory=list)
    verdict: str = ""        # the overlay's word for this slot's retail body
    verdict_target: str = ""
    default: str = ""        # the retail literal, or `void`, when the body is a constant
    hand: str = ""           # the port method that defines this virtual by hand

    @property
    def generated(self) -> bool:
        return self.port_kind != PORT

    @property
    def stubbed(self) -> bool:
        """A generated virtual with no recovered body and no hand-written definition."""
        return self.generated and not self.default and not self.hand

    @property
    def declaration(self) -> str:
        name = self.method or f"vfunc{self.slot}"
        return f"{self.ret} {name}({self.params}){' const' if self.const else ''}".replace("  ", " ")

    def port_declaration(self, prefix: str = "") -> str:
        args = ", ".join(self.params_port)
        return (f"{self.ret_port} {prefix}{self.port_name}({args})"
                f"{' const' if self.const else ''}")


def default_body(row: Slot) -> tuple[str, str, int]:
    """One constant-returning retail body as the port spells it.

    Returns the statement the generated definition carries (empty for a `void` slot), the
    expression the automation probe compares, and the integer the probe expects. A return type
    this cannot lower is a decision, not a default: generation fails and the row is argued in the
    overlay instead of guessed here.
    """
    literal = row.default
    port = row.ret_port
    if literal == "void":
        if port != "void":
            raise SystemExit(f"gen_kernel_shape: slot {row.slot} ({row.address}) is "
                             f"`default:void` but returns `{port}`")
        return "", "", 0
    if port == "void":
        raise SystemExit(f"gen_kernel_shape: slot {row.slot} ({row.address}) returns void but the "
                         f"overlay records `default:{literal}`")
    if "." in literal:
        # A float body's constant. The probe compares integers, so a fractional default would be
        # asserted against a truncation; that is an argued row, not a generated one.
        if port not in ("float", "double"):
            raise SystemExit(f"gen_kernel_shape: slot {row.slot} records `default:{literal}` but "
                             f"returns `{port}`")
        if float(literal) != int(float(literal)):
            raise SystemExit(f"gen_kernel_shape: slot {row.slot} records the fractional default "
                             f"`{literal}`; the probe compares integers, so argue the row instead")
        value = int(float(literal))
        return (f"return static_cast<{port}>({literal});", "static_cast<int64>(Npc.{call})", value)
    value = int(literal, 0)
    if port == "bool":
        if value not in (0, 1):
            raise SystemExit(f"gen_kernel_shape: slot {row.slot} returns bool but retail returns "
                             f"{literal}")
        return f"return {'true' if value else 'false'};", "Npc.{call} ? 1 : 0", value
    if port.endswith("*"):
        if value != 0:
            raise SystemExit(f"gen_kernel_shape: slot {row.slot} returns `{port}` but retail "
                             f"returns {literal}; a pointer constant is not a default")
        return "return nullptr;", "static_cast<int64>(reinterpret_cast<UPTRINT>(Npc.{call}))", 0
    if port == "EElysiumNpcState":
        return (f"return static_cast<EElysiumNpcState>({literal});",
                "static_cast<int64>(Npc.{call})", value)
    if port in ("float", "double"):
        return (f"return static_cast<{port}>({literal});", "static_cast<int64>(Npc.{call})", value)
    widths = {"int8": (8, True), "uint8": (8, False), "int16": (16, True), "uint16": (16, False),
              "int32": (32, True), "uint32": (32, False), "int64": (64, True),
              "uint64": (64, False)}
    if port in widths:
        bits, signed = widths[port]
        wrapped = value & ((1 << bits) - 1)
        if signed and wrapped >= (1 << (bits - 1)):
            wrapped -= 1 << bits
        # `static_cast`, not the bare literal: retail's `return 0xffffffff;` out of an `int` body
        # is -1, and the cast says so where a bare `0xffffffff` would be a narrowing warning.
        return (f"return static_cast<{port}>({literal});", "static_cast<int64>(Npc.{call})",
                wrapped)
    raise SystemExit(f"gen_kernel_shape: slot {row.slot} returns `{port}`, which has no default "
                     f"lowering for retail `{literal}`")


@dataclass
class ClassRow:
    name: str
    base: str
    vtable: str
    slots: int
    own: int
    classnames: list[str]


@dataclass
class OverrideRow:
    cls: str
    slot: int
    address: str
    method: str
    verdict: str = ""
    default: str = ""


def constant_return(code: str) -> str:
    """The literal a body returns when its whole body is `return <literal>;`, else `""`.

    Species are data in this port, so a species override that answers one constant is a row in a
    class → slot → value table rather than a C++ body. The *decision* that a body is that shape is
    a reading and lives in the verdict overlay; this only reads the value back off the decompiled
    C, and only when the body has exactly one statement, so it cannot be fooled into recording a
    behaviour as a value.
    """
    body = code[code.index("{"):code.rindex("}") + 1] if "{" in code and "}" in code else ""
    match = CONSTANT_BODY_RE.match(body)
    if not match:
        return ""
    tail = match.group(1).rstrip(";").strip()
    return tail or "void"


@dataclass
class Model:
    words: list[Word]
    slots: list[Slot]
    branch: list[Slot]
    classes: list[ClassRow]
    overrides: list[OverrideRow]
    reserved: set[str]
    family: set[str]
    meta: dict


def story_for(layer: int) -> str:
    for low, high, story in STORY_BANDS:
        if low <= layer <= high:
            return story
    return ""


def reserved_names(repo: Path) -> set[str]:
    """Every method name the port's entity chain declares.

    Over-detection is the safe direction: a name this set holds by accident costs one reviewed row
    in `SLOT_PORT_MAP`, while a name it misses would let a generated virtual silently shadow a
    working method.
    """
    names: set[str] = set()
    for rel, stop in PORT_CHAIN_HEADERS:
        path = repo / rel
        if not path.is_file():
            raise SystemExit(f"gen_kernel_shape: {rel} is missing; the chain scan cannot run")
        for line in path.read_text(encoding="utf-8").splitlines():
            if stop and line.startswith(stop):
                break
            for pattern in (DECL_RE, MEMBER_RE):
                match = pattern.match(line)
                if match:
                    names.add(match.group(1))
    # The control-flow keywords the regex cannot tell from a declaration.
    names -= {"if", "for", "while", "switch", "return", "sizeof", "static_cast", "else", "catch"}
    return names


def build(repo: Path, module: str, depth: int) -> Model:
    shape, rows, sigs = ks.build(module, depth, repo)
    ledger = shape.ledger

    # --- words -----------------------------------------------------------------------------------
    words: list[Word] = []
    interiors: dict[tuple[str, int], int] = collections.Counter()
    top: dict[tuple[str, str], Word] = {}
    for row in rows:
        member = row.member
        if "." in member or "[" in member or "+" in member:
            # An interior of a record the datamaps do not describe inside, or an array element.
            # One port member per retail aggregate, so the interior is a count on its owner.
            owner = re.split(r"[.\[+]", member, maxsplit=1)[0]
            interiors[(row.table, owner)] += 1
            continue
        word = Word(table=row.table, offset=row.off, member=member, type=row.type,
                    field_type=row.field_type, layer=row.layer, tier=row.tier,
                    size=row.size or 0, count=row.count,
                    writers=len(row.evidence.writers) if row.evidence else 0,
                    readers=len(row.evidence.readers) if row.evidence else 0)
        words.append(word)
        top[(row.table, member)] = word
    for (table, owner), count in interiors.items():
        word = top.get((table, owner))
        if word is not None:
            word.interiors = count

    # --- slots -----------------------------------------------------------------------------------
    reserved = reserved_names(repo)
    family = set(ledger.family)
    slots: list[Slot] = []
    branch: list[Slot] = []
    seen: dict[tuple[str, str, str], int] = {}
    for sig in sigs:
        row = Slot(slot=sig["slot"], cls=sig["cls"], method=sig["method"] or "",
                   ret=sig["ret"], params=sig["params"], const=bool(sig["const"]),
                   tier=sig["tier"], words="/".join(map(str, sig["words"])))
        bodies = ledger.slot_bodies.get(row.slot, {})
        holder = row.cls or BASE_TABLE
        body = bodies.get(holder) or bodies.get("CAI_BaseNPC") or ""
        if not body and bodies:
            body = sorted(bodies.values())[0]
        row.body = body
        row.address = f"0x{body}" if body else ""
        row.layer = ledger.layer_of.get(body, -1)
        row.story = story_for(row.layer) if row.layer >= 0 else ""
        verdict = ledger.verdicts.get(body)
        if verdict is not None:
            row.verdict, row.verdict_target = verdict.verdict, verdict.target
        (slots if row.cls == "" else branch).append(row)

    # The port's name for each Troika-line slot, and the collisions that need a decision.
    collisions: list[Slot] = []
    for row in slots:
        name = row.method or f"Slot{row.slot}"
        name = re.sub(r"[^A-Za-z0-9_]", "_", name.split(" / ")[0])
        if row.method and " / " in row.method:
            row.notes.append(f"the bodies disagree on the name: {row.method}")
        if name.startswith("_"):
            # A destructor or an operator: the slot is a lifetime concern the port's own
            # destructor owns, and a generated virtual for it would be a second one.
            name = f"Slot{row.slot}"
            row.notes.append(f"retail `{row.method}` is a lifetime slot; declared by index")
        decision = SLOT_PORT_MAP.get(row.slot)
        if decision is not None:
            kind, port_name, why = decision
            row.port_kind, row.port_why = kind, why
            row.port_name = port_name if kind == PORT else port_name or f"{name}Slot{row.slot}"
            if kind == PORT:
                continue
        elif name in reserved:
            collisions.append(row)
            continue
        else:
            row.port_name = name
        key = (row.port_name, row.ret, row.params + ("c" if row.const else ""))
        if key in seen:
            row.notes.append(f"slot {seen[key]} declares the same method; "
                             "which body is which is unrecovered")
            row.port_name = f"{row.port_name}Slot{row.slot}"
        else:
            seen[key] = row.slot
        row.ret_port, note = lower_type(row.ret, family)
        if row.ret_port.endswith("&"):
            row.ret_port = "void*"
            note = f"`{row.ret}`"
        if note:
            row.notes.append(f"returns {note}")
        for param in [p for p in row.params.split(",") if p.strip()]:
            lowered, param_note = lower_type(param, family)
            row.params_port.append(lowered)
            if param_note:
                row.notes.append(f"takes {param_note}")
        # What the verdict does to the emission. A `rule` whose whole retail body is a constant
        # lands as that constant; a `rule` or `present` whose body is written by hand in the
        # substrate loses its definition here. Everything else keeps its stub and carries the
        # verdict in the comment.
        if row.verdict in ("rule", "present", "mechanism"):
            if row.verdict_target.startswith(DEFAULT_PREFIX):
                literal = row.verdict_target[len(DEFAULT_PREFIX):].strip()
                if not DEFAULT_LITERAL_RE.match(literal):
                    raise SystemExit(
                        f"gen_kernel_shape: slot {row.slot} ({row.address}) records "
                        f"`{row.verdict_target}`, which is not a literal this generator emits")
                # The overlay's literal is a reading, and the body is the fact. They have to
                # agree: a `default:` row is the one place a verdict puts a VALUE into project
                # source, so a misread constant would be an invented behaviour that compiles.
                body = ledger.functions.get(row.body)
                read = constant_return(body.code or "") if body is not None else ""
                if read and read != literal and int(read, 0) != int(literal, 0):
                    raise SystemExit(
                        f"gen_kernel_shape: slot {row.slot} ({row.address}) records "
                        f"`default:{literal}` but the body returns `{read}`")
                row.default = literal
                default_body(row)   # fail here, not at the C++ compiler, on a type it cannot lower
            elif row.verdict_target.startswith(HAND_PREFIX):
                row.hand = row.verdict_target[len(HAND_PREFIX):].strip()

    if collisions:
        print("gen_kernel_shape: the port's entity chain already declares these slot names; add a "
              "`SLOT_PORT_MAP` row for each (PORT names the method that IS the slot, SUFFIX says "
              "the collision is accidental):")
        for row in collisions:
            print(f"  {row.slot:4d}  {row.method or '(unnamed)'}  —  {row.declaration}")
        raise SystemExit(1)

    for row in branch:
        row.port_name = row.method or f"Slot{row.slot}"

    # --- classes and the species overrides -------------------------------------------------------
    own = collections.Counter()
    for slot, bodies in ledger.slot_bodies.items():
        for cls, fn_addr in bodies.items():
            fn = ledger.functions.get(fn_addr)
            if fn and fn.ns == cls:
                own[cls] += 1
    classes = [ClassRow(name=cls, base=ledger.bases.get(cls, ""),
                        vtable=f"0x{ledger.vtable_addr.get(cls, '')}" if ledger.vtable_addr.get(cls)
                        else "",
                        slots=ledger.slot_count.get(cls, 0), own=own[cls],
                        classnames=list(ledger.classnames.get(cls, [])))
               for cls in ledger.family]

    overrides: list[OverrideRow] = []
    for slot in sorted(ledger.slot_bodies):
        bodies = ledger.slot_bodies[slot]
        base = bodies.get("CAI_BaseNPC", "")
        troika = bodies.get("CAI_BaseNPCTroika", "")
        for cls in sorted(bodies):
            addr = bodies[cls]
            if cls in ("CAI_BaseNPC", "CAI_BaseNPCTroika") or addr in (base, troika):
                continue
            if cls not in family:
                continue
            fn = ledger.functions.get(addr)
            verdict = ledger.verdicts.get(addr)
            row = OverrideRow(cls=cls, slot=slot, address=f"0x{addr}",
                              method=(fn.name if fn else ""))
            if verdict is not None:
                row.verdict = verdict.verdict
                if verdict.target.startswith(REGISTRY_PREFIX) and fn is not None:
                    row.default = constant_return(fn.code or "")
            overrides.append(row)

    return Model(words=words, slots=slots, branch=branch, classes=classes, overrides=overrides,
                 reserved=reserved, family=family, meta=dict(ledger.meta))


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


def _row(cells: list[str], indent: str = "\t\t", end: str = " },") -> list[str]:
    """One brace initialiser, wrapped at 100 columns with its continuations indented once."""
    out: list[str] = []
    current = f"{indent}{{ "
    for index, cell in enumerate(cells):
        piece = cell + ("," if index + 1 < len(cells) else end)
        if current.strip() != "{" and len((current + piece).expandtabs(4)) > 100:
            out.append(current.rstrip())
            current = f"{indent}\t"
        current += piece + (" " if index + 1 < len(cells) else "")
    out.append(current.rstrip())
    return out


def _tier(tier: str) -> str:
    return "ETier::" + {"datamap": "Datamap", "interior": "Interior", "sdk": "Sdk",
                        "sdk-order": "SdkOrder", "doc": "Doc", "walked": "Walked",
                        "evidence": "Evidence", "open": "Open",
                        "unsettled": "Unsettled"}.get(tier, "Open")


def digest_of(model: Model) -> int:
    """FNV-1a 64 over the census row stream, reproduced verbatim in C++."""
    value = 0xCBF29CE484222325
    lines = []
    for word in model.words:
        lines.append(f"w|{word.table}|{word.offset:x}|{word.member}|{word.type}|{word.tier}")
    for row in model.slots + model.branch:
        lines.append(f"s|{row.slot}|{row.cls}|{row.declaration}|{row.tier}|{row.port_name}"
                     f"|{row.verdict}|{row.default}")
    for cls in model.classes:
        lines.append(f"c|{cls.name}|{cls.base}|{cls.slots}")
    for row in model.overrides:
        lines.append(f"o|{row.cls}|{row.slot}|{row.address}|{row.verdict}|{row.default}")
    for line in lines:
        for byte in (line + "\n").encode("utf-8"):
            value ^= byte
            value = (value * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def _header(module: str, meta: dict, counts: str) -> list[str]:
    return [
        "// Generated by `uv run elysium research gen_kernel_shape`. Do not hand-edit.",
        "//",
        "// The NPC kernel's shape, transcribed from `docs/vtmb/npc-kernel/` — `layout.md` (every",
        "// word of the flattened `CAI_BaseNPCTroika` layout, typed), `signatures.md` (every",
        "// primary-vtable slot's declaration), `classes.md` (the family tree and the entity",
        "// classnames each class claims) and `slots.md` (the bodies per class). It is the census",
        "// the port's own shape is asserted against; it carries no behaviour and no rule.",
        "//",
        *_comment(counts),
        "//",
        *_comment(f"{module} sha256 `{(meta.get('sha256') or '')[:16]}…`; the ledger's own "
                  "provenance line is in every table under `docs/vtmb/npc-kernel/`."),
    ]


def render_census(model: Model, module: str) -> str:
    troika_words = [w for w in model.words if w.table == BASE_TABLE]
    species_words = [w for w in model.words if w.table != BASE_TABLE]
    counts = (f"{len(model.words)} words ({len(troika_words)} on the flattened Troika table, "
              f"{len(species_words)} added by a species), {len(model.slots)} Troika-line slots and "
              f"{len(model.branch)} per-branch virtuals past them, {len(model.classes)} classes, "
              f"{len(model.overrides)} species slot overrides.")
    out = _header(module, model.meta, counts)
    out += ["", '#include "Substrate/ElysiumNpcKernelShape.h"', "",
            '#include "Containers/StringConv.h"', "",
            "namespace ElysiumNpcKernelShape", "{", "namespace", "{",
            "\tusing ETier = EElysiumNpcShapeTier;", ""]

    out.append("\t// Every top-level word of the flattened `CAI_BaseNPCTroika` layout, then each")
    out.append("\t// species class's own words. An interior — `COutputEvent`, `CUtlVector`,")
    out.append("\t// `Vector`, an array element — is collapsed onto the word that owns it and")
    out.append("\t// counted in `Interiors`, because the port declares one member per retail")
    out.append("\t// aggregate.")
    out.append("\tconstexpr FElysiumNpcWord GWords[] =")
    out.append("\t{")
    for word in model.words:
        out += _row([f"0x{word.offset:04x}", _literal(word.table), _literal(word.member),
                     _literal(word.type), _literal(word.layer), _tier(word.tier),
                     str(word.size), str(word.count), str(word.interiors)])
    out.append("\t};")
    out.append("")

    out.append("\t// One row per primary-vtable slot. `Class` is empty for a virtual of the Troika")
    out.append("\t// line; past a base's table each branch declares unrelated virtuals at the same")
    out.append("\t// index, and the row names the class that introduces it. `Address` is the body")
    out.append("\t// the holder fills, `Layer` its position in `order.md`, `Story` the spec story")
    out.append("\t// whose band that layer falls in, and `PortMethod` the port's callable.")
    out.append("\t// `Verdict` is what `kernel_verdicts.tsv` records for that body and `Default`")
    out.append("\t// the retail literal the whole body returns, where it is one.")
    out.append("\tconstexpr FElysiumNpcSlot GSlots[] =")
    out.append("\t{")
    for row in model.slots + model.branch:
        out += _row([str(row.slot), _literal(row.cls), _literal(row.method),
                     _literal(row.declaration), _literal(row.port_name), _tier(row.tier),
                     _literal(row.address), str(row.layer), _literal(row.story),
                     "true" if row.port_kind == PORT else "false", _literal(row.verdict),
                     _literal(row.default)])
    out.append("\t};")
    out.append("")

    for cls in model.classes:
        if not cls.classnames:
            continue
        out.append(f"\tconstexpr const TCHAR* {_symbol(cls.name)}_Classnames[] =")
        out += _row([_literal(name) for name in cls.classnames], "\t\t", " };")
    out.append("")
    out.append("\t// The 77 classes whose primary vtable spans the NPC slot range, their direct")
    out.append("\t// bases from the RTTI walk, and the entity classnames each claims.")
    out.append("\tconstexpr FElysiumNpcClass GClasses[] =")
    out.append("\t{")
    for cls in model.classes:
        names = f"{_symbol(cls.name)}_Classnames" if cls.classnames else "nullptr"
        out += _row([_literal(cls.name), _literal(cls.base), _literal(cls.vtable),
                     str(cls.slots), str(cls.own), names, str(len(cls.classnames))])
    out.append("\t};")
    out.append("")

    out.append("\t// Species overrides, as rows rather than subclasses: the port stands one leaf")
    out.append("\t// for every `npc_V*` classname and a species' own body is data about which slot")
    out.append("\t// it replaces, not a C++ type. `Verdict` is the overlay's word for that body and")
    out.append("\t// `Default` the literal it answers where the whole body is one `return`, which")
    out.append("\t// is the class → slot → value table story 29c's `registry:` rows record.")
    out.append("\tconstexpr FElysiumNpcClassSlot GOverrides[] =")
    out.append("\t{")
    for row in model.overrides:
        out += _row([_literal(row.cls), str(row.slot), _literal(row.address),
                     _literal(row.method), _literal(row.verdict), _literal(row.default)])
    out.append("\t};")
    out.append("}")
    out.append("")
    out.append("TArrayView<const FElysiumNpcWord> Words()")
    out.append("{")
    out.append("\treturn MakeArrayView(GWords);")
    out.append("}")
    out.append("")
    out.append("TArrayView<const FElysiumNpcSlot> Slots()")
    out.append("{")
    out.append("\treturn MakeArrayView(GSlots);")
    out.append("}")
    out.append("")
    out.append("TArrayView<const FElysiumNpcClass> Classes()")
    out.append("{")
    out.append("\treturn MakeArrayView(GClasses);")
    out.append("}")
    out.append("")
    out.append("TArrayView<const FElysiumNpcClassSlot> Overrides()")
    out.append("{")
    out.append("\treturn MakeArrayView(GOverrides);")
    out.append("}")
    out.append("")
    out.append("const FElysiumNpcShapeCensus& Census()")
    out.append("{")
    out.append("\tstatic const FElysiumNpcShapeCensus GCensus =")
    out.append("\t{")
    tiers = collections.Counter(w.tier for w in model.words)
    slot_tiers = collections.Counter(r.tier for r in model.slots)
    out.append(f"\t\t/* Words            */ {len(model.words)},")
    out.append(f"\t\t/* TroikaWords      */ {len(troika_words)},")
    out.append(f"\t\t/* SpeciesWords     */ {len(species_words)},")
    out.append(f"\t\t/* NpcWords         */ {len([w for w in troika_words if w.npc])},")
    out.append(f"\t\t/* Interiors        */ {sum(w.interiors for w in model.words)},")
    out.append(f"\t\t/* UnsettledWords   */ {tiers['unsettled']},")
    out.append(f"\t\t/* Slots            */ {len(model.slots) + len(model.branch)},")
    out.append(f"\t\t/* TroikaSlots      */ {len(model.slots)},")
    out.append(f"\t\t/* BranchSlots      */ {len(model.branch)},")
    out.append(f"\t\t/* PortedSlots      */ "
               f"{len([r for r in model.slots if r.port_kind == PORT])},")
    out.append(f"\t\t/* UnsettledSlots   */ {slot_tiers['unsettled']},")
    out.append(f"\t\t/* Classes          */ {len(model.classes)},")
    out.append(f"\t\t/* Classnames       */ {sum(len(c.classnames) for c in model.classes)},")
    out.append(f"\t\t/* Overrides        */ {len(model.overrides)},")
    out.append(f"\t\t/* VerdictedSlots   */ {len([r for r in model.slots if r.verdict])},")
    out.append(f"\t\t/* DefaultSlots     */ {len([r for r in model.slots if r.default])},")
    out.append(f"\t\t/* VerdictedOverrides */ "
               f"{len([r for r in model.overrides if r.verdict])},")
    out.append(f"\t\t/* RegistryValues   */ {len([r for r in model.overrides if r.default])},")
    out.append(f"\t\t/* RowDigest        */ 0x{digest_of(model):016x}ull,")
    out.append("\t};")
    out.append("\treturn GCensus;")
    out.append("}")
    out += CENSUS_TAIL
    out.append("}")
    return "\n".join(out) + "\n"


# The two functions over the tables that are code rather than data. They are emitted with the
# tables so the digest walks the arrays it was taken from: the stored constant catches a bad
# regeneration, and this walk catches a hand-edit of a row the constant was not regenerated for.
CENSUS_TAIL = """
const TCHAR* TierName(EElysiumNpcShapeTier Tier)
{
\tswitch (Tier)
\t{
\tcase EElysiumNpcShapeTier::Datamap:   return TEXT("datamap");
\tcase EElysiumNpcShapeTier::Interior:  return TEXT("interior");
\tcase EElysiumNpcShapeTier::Sdk:       return TEXT("sdk");
\tcase EElysiumNpcShapeTier::SdkOrder:  return TEXT("sdk-order");
\tcase EElysiumNpcShapeTier::Doc:       return TEXT("doc");
\tcase EElysiumNpcShapeTier::Walked:    return TEXT("walked");
\tcase EElysiumNpcShapeTier::Evidence:  return TEXT("evidence");
\tcase EElysiumNpcShapeTier::Unsettled: return TEXT("unsettled");
\tdefault:                              return TEXT("open");
\t}
}

uint64 DigestOfRows()
{
\t// FNV-1a 64 over the generator's own row stream, byte for byte: `w|`, `s|`, `c|`, `o|` in
\t// that order, each line UTF-8 and newline-terminated.
\tuint64 Value = 0xcbf29ce484222325ull;
\tauto Fold = [&Value](const FString& Line)
\t{
\t\tconst auto Utf8 = StringCast<UTF8CHAR>(*Line);
\t\tfor (int32 Index = 0; Index < Utf8.Length(); ++Index)
\t\t{
\t\t\tValue ^= static_cast<uint8>(Utf8.Get()[Index]);
\t\t\tValue *= 0x100000001b3ull;
\t\t}
\t\tValue ^= static_cast<uint8>('\\n');
\t\tValue *= 0x100000001b3ull;
\t};

\tfor (const FElysiumNpcWord& Row : Words())
\t{
\t\tFold(FString::Printf(TEXT("w|%s|%x|%s|%s|%s"), Row.Table, Row.Offset, Row.Member, Row.Type,
\t\t\tTierName(Row.Tier)));
\t}
\tfor (const FElysiumNpcSlot& Row : Slots())
\t{
\t\tFold(FString::Printf(TEXT("s|%d|%s|%s|%s|%s|%s|%s"), Row.Slot, Row.Class, Row.Declaration,
\t\t\tTierName(Row.Tier), Row.PortMethod, Row.Verdict, Row.Default));
\t}
\tfor (const FElysiumNpcClass& Row : Classes())
\t{
\t\tFold(FString::Printf(TEXT("c|%s|%s|%d"), Row.Name, Row.Base, Row.Slots));
\t}
\tfor (const FElysiumNpcClassSlot& Row : Overrides())
\t{
\t\tFold(FString::Printf(TEXT("o|%s|%d|%s|%s|%s"), Row.Class, Row.Slot, Row.Address, Row.Verdict,
\t\t\tRow.Default));
\t}
\treturn Value;
}""".split("\n")


def _symbol(name: str) -> str:
    return re.sub(r"[^A-Za-z0-9_]", "_", name)


def _slot_comment(row: Slot, indent: str = "\t") -> list[str]:
    lines = _comment(f"slot {row.slot}  {row.address or 'no body'}  ({row.tier})  "
                     f"`{row.declaration}`", indent)
    for note in row.notes:
        lines.append(f"{indent}//   {note}")
    if row.story:
        lines.append(f"{indent}//   layer {row.layer}, story {row.story}")
    return lines


def render_slots_inl(model: Model, module: str) -> str:
    generated = [r for r in model.slots if r.generated]
    counts = (f"{len(generated)} of the {len(model.slots)} Troika-line slots; the other "
              f"{len(model.slots) - len(generated)} are declared by hand, because the port already "
              "implements them under the name `SLOT_PORT_MAP` records.")
    out = _header(module, model.meta, counts)
    out += [
        "//",
        "// This file is included INSIDE `class FElysiumNpc` (`Substrate/ElysiumNpc.h`). It is not",
        "// a header: it has no include guard and declares nothing of its own. One `virtual` per",
        "// slot, in slot order, with the retail declaration in the comment and the port's lowered",
        "// signature in the code. `FElysiumNpc` is `final` and stays final — a virtual here",
        "// declares the surface retail dispatches through, not an extension point.",
        "//",
        "// A body lands on one of these in 29c/29d/29e. Until then the definition in",
        "// `ElysiumNpcKernelSlots.cpp` tallies `elysium.stubs` with the retail address — except",
        "// where the verdict overlay records that retail's whole body is one literal, which the",
        "// generator emits, or that the body is written by hand in the substrate, in which case no",
        "// definition is generated at all and the linker is what checks the claim.",
        "//",
        "// The slots NOT declared here are the ones the port already runs. They are listed rather",
        "// than left implicit, because \"this slot has a body somewhere else\" is exactly the fact a",
        "// reader of this file needs, and the census carries the same pairing as data:",
    ]
    for row in model.slots:
        if row.port_kind != PORT:
            continue
        out += _comment(f"  slot {row.slot:>3}  {row.address}  {row.port_name} — {row.port_why}")
    out.append("")
    for row in generated:
        out += _slot_comment(row)
        out.append(f"\tvirtual {row.port_declaration()};")
    return "\n".join(out) + "\n"


def _default_return(port_type: str) -> str:
    if port_type == "void":
        return ""
    return "\treturn {};"


def _probe_arguments(row: Slot) -> tuple[list[str], str]:
    """Locals for the probe's call, and the argument list.

    Every parameter is default-constructed. A reference parameter needs an lvalue, so it gets a
    named local; everything else is passed as a value-initialised temporary. The probe asks one
    question — what does this virtual answer — and never what it does with its arguments.
    """
    locals_: list[str] = []
    args: list[str] = []
    for index, param in enumerate(row.params_port):
        base = param.strip()
        if base.endswith("&"):
            # A reference binds to a local, and the local is the referent's own type: a
            # `const T&` parameter takes a mutable `T`, which still binds.
            base = base.removesuffix("&").strip().removeprefix("const ").strip()
        locals_.append(f"{base} Arg{index}{{}};")
        args.append(f"Arg{index}")
    return locals_, ", ".join(args)


def render_slots_cpp(model: Model, module: str) -> str:
    generated = [r for r in model.slots if r.generated]
    stubbed = [r for r in generated if r.stubbed]
    defaults = [r for r in generated if r.default]
    hand = [r for r in generated if r.hand]
    stories = collections.Counter(r.story or "unassigned" for r in stubbed)
    counts = (f"{len(generated)} generated virtuals: {len(defaults)} carry the retail default "
              f"story 29c recovered, {len(hand)} are defined by hand in the substrate, and "
              f"{len(stubbed)} are still stubs — "
              + ", ".join(f"{count} {story}" for story, count in sorted(stories.items())) + ".")
    out = _header(module, model.meta, counts)
    out += [
        "//",
        "// A stub body says one thing: this slot has no port implementation yet. The tally row",
        "// carries the retail address and the story that owns it, so `elysium.stubs` joins",
        "// `docs/vtmb/npc-kernel/functions.md` by address.",
        "//",
        "// A body with a `default:` verdict says something stronger: retail's whole body at that",
        "// slot is `return <literal>;`, so the port answers the same literal and stops tallying.",
        "// The literal is a recovered fact, not a written behaviour — the same argument the story",
        "// makes for a species override of a constant-returning virtual — and `GDefaults` below",
        "// is what `Elysium.Substrate.NpcKernelSlots.Defaults` calls every one of them through.",
        "",
        '#include "Substrate/ElysiumNpc.h"',
        "",
        '#include "ElysiumStub.h"',
        '#include "Substrate/ElysiumNpcKernelShape.h"',
        "",
        "namespace",
        "{",
        "\t// One shape for every slot stub: the surface is the retail class and method, which is",
        "\t// what the ledger joins on, and never an instance name. Unit-prefixed because the module",
        "\t// builds adaptive-unity and this anonymous namespace is regularly merged with others.",
        "\tvoid FireKernelSlot(const TCHAR* Method, const TCHAR* Address, const TCHAR* Story,",
        "\t\tconst FString& Receiver)",
        "\t{",
        "\t\tElysiumStub::FSurface Surface;",
        "\t\tSurface.Kind = TEXT(\"slot\");",
        "\t\tSurface.Surface = FString::Printf(TEXT(\"CAI_BaseNPCTroika::%s\"), Method);",
        "\t\tSurface.Address = Address;",
        "\t\tSurface.Story = Story;",
        "\t\tElysiumStub::Fired(Surface, Receiver, FString(), TEXT(\"the NPC kernel\"));",
        "\t}",
        "}",
        "",
    ]
    for row in generated:
        if row.hand:
            out += _slot_comment(row, "")
            out += _comment(f"verdict `{row.verdict}`: the body is `{row.hand}`, written by hand "
                            "in the substrate. Declared here, defined there.", "")
            out.append("")
            continue
        out += _slot_comment(row, "")
        if row.default:
            statement, _, _ = default_body(row)
            out += _comment(f"verdict `rule`: retail's whole body is "
                            f"`{'return;' if row.default == 'void' else f'return {row.default};'}`",
                            "")
        out += _wrapped(row.port_declaration("FElysiumNpc::"), "")
        out.append("{")
        if row.default:
            if statement:
                out.append(f"\t{statement}")
        else:
            out += _wrapped(f"FireKernelSlot({_literal(row.port_name)}, {_literal(row.address)}, "
                            f"{_literal(row.story)}, DebugString());", "\t")
            tail = _default_return(row.ret_port)
            if tail:
                out.append(tail)
        out.append("}")
        out.append("")

    out += [
        "namespace ElysiumNpcKernelShape",
        "{",
        "\tnamespace",
        "\t{",
        "\t\t// One probe per recovered default: it calls the port's virtual with",
        "\t\t// value-initialised arguments and renders the answer as an integer, so the suite can",
        "\t\t// require retail's literal back without naming hundreds of methods by hand. A `void`",
        "\t\t// slot has no answer to render and the suite asserts only that calling it tallies",
        "\t\t// nothing.",
    ]
    if defaults:
        out += ["\t\tconstexpr FElysiumNpcSlotDefault GDefaults[] =", "\t\t{"]
        for row in defaults:
            _, expression, value = default_body(row)
            locals_, args = _probe_arguments(row)
            call = f"{row.port_name}({args})"
            prologue = " ".join(locals_) + (" " if locals_ else "")
            if row.default == "void":
                lambda_body = f"{prologue}Npc.{call}; return 0;"
            else:
                lambda_body = f"{prologue}return {expression.format(call=call)};"
            out += _row([str(row.slot), _literal(row.address), _literal(row.port_name),
                         _literal(row.default), str(value),
                         "true" if row.default == "void" else "false",
                         "[](FElysiumNpc& Npc) -> int64 { " + lambda_body + " }"], "\t\t\t")
        out.append("\t\t};")
    else:
        out.append("\t\t// No slot carries a recovered default yet.")
    out += [
        "\t}",
        "",
        "\tTArrayView<const FElysiumNpcSlotDefault> SlotDefaults()",
        "\t{",
        ("\t\treturn MakeArrayView(GDefaults);" if defaults
         else "\t\treturn TArrayView<const FElysiumNpcSlotDefault>();"),
        "\t}",
        "}",
    ]
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
        print(f"check: {output.name} matches the ledger")
        return 0
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {output}")
    return 0


def report(model: Model) -> None:
    print("=" * 78)
    print("NPC kernel shape — census")
    print("=" * 78)
    troika = [w for w in model.words if w.table == BASE_TABLE]
    print(f"words:      {len(model.words):5d}  ({len(troika)} Troika, "
          f"{len(model.words) - len(troika)} species)")
    print(f"  NPC layer {len([w for w in troika if w.npc]):5d}  "
          f"(CAI_BaseNPC + CAI_BaseNPCTroika)")
    print(f"  interiors {sum(w.interiors for w in model.words):5d}  collapsed onto their owner")
    for tier, count in sorted(collections.Counter(w.tier for w in model.words).items()):
        print(f"    {tier:<10} {count:5d}")
    print(f"slots:      {len(model.slots):5d}  Troika line, {len(model.branch)} per-branch")
    print(f"  ported    {len([r for r in model.slots if r.port_kind == PORT]):5d}")
    print(f"  generated {len([r for r in model.slots if r.generated]):5d}")
    for story, count in sorted(collections.Counter(
            r.story or "unassigned" for r in model.slots if r.generated).items()):
        print(f"    {story:<10} {count:5d}")
    print(f"  verdicted {len([r for r in model.slots if r.verdict]):5d}")
    print(f"    default {len([r for r in model.slots if r.default]):5d}  "
          f"the port answers retail's literal")
    print(f"    hand    {len([r for r in model.slots if r.hand]):5d}  "
          f"defined in the substrate, not stubbed here")
    print(f"    stub    {len([r for r in model.slots if r.stubbed]):5d}")
    # The stub count PER STORY BAND, split by whether a story has read the body — which is what
    # 29c-1's acceptance measures. A band's stub is one of three things and they are not the same
    # claim: a `rule` the porting story still owes, a `present`/`mechanism` slot whose body was read
    # and whose port answer lives elsewhere, or a slot in the band by `order.md` layer that no
    # verdict covers at all. The third kind is the entity chain's: `--checklist` verdicts the CORE
    # of a band (a family method, or a body touching an offset past `CBaseCombatCharacter`), and a
    # `CBaseEntity` virtual at a low layer is in neither that set nor any story's rows.
    print("  stubs by story band")
    for story in sorted({r.story or "unassigned" for r in model.slots if r.stubbed}):
        rows = [r for r in model.slots if r.stubbed and (r.story or "unassigned") == story]
        kinds = collections.Counter(r.verdict or "no verdict" for r in rows)
        detail = ", ".join(f"{count} {kind}" for kind, count in sorted(kinds.items()))
        print(f"    {story:<10} {len(rows):5d}  {detail}")
    print(f"classes:    {len(model.classes):5d}  "
          f"{sum(len(c.classnames) for c in model.classes)} entity classnames")
    print(f"overrides:  {len(model.overrides):5d}  species slot bodies")
    print(f"  verdicted {len([r for r in model.overrides if r.verdict]):5d}  "
          f"{len([r for r in model.overrides if r.default])} carry a registry value")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--module", default=kl.MODULE)
    parser.add_argument("--depth", type=int, default=kl.DEFAULT_DEPTH)
    parser.add_argument("--check", action="store_true",
                        help="verify the committed files match; write nothing")
    parser.add_argument("--report", action="store_true",
                        help="print the census summary and exit, writing nothing")
    args = parser.parse_args(argv)

    repo = repo_root()
    model = build(repo, args.module, args.depth)
    report(model)
    if args.report:
        return 0

    status = 0
    status |= _emit(repo.joinpath(*CENSUS_OUTPUT), render_census(model, args.module), args.check)
    status |= _emit(repo.joinpath(*SLOTS_INL_OUTPUT), render_slots_inl(model, args.module),
                    args.check)
    status |= _emit(repo.joinpath(*SLOTS_CPP_OUTPUT), render_slots_cpp(model, args.module),
                    args.check)
    return status


if __name__ == "__main__":
    sys.exit(main())
