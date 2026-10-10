# -*- coding: utf-8 -*-
"""Emit the NPC kernel's shape census as project source.

Owner-run archaeology, not part of any build.  The kernel ledger
(``docs/vtmb/npc-kernel/``) says what ``CAI_BaseNPCTroika`` *is*: every word of
its flattened layout with a type, every primary-vtable slot with a declaration,
and the 77-class tree with the entity classnames each claims.  This generator
transcribes that ledger into committed artifacts the runtime is asserted against
(0019 story 5 closed the class tree: every live retail class is a C++ class, and
nothing here dispatches):

* ``ElysiumNpcKernelShape.cpp`` — the **census**.  One row per retail word
  (offset, retail name, type, owning chain layer, tier), one row per slot
  (declaration, tier, the Troika-line body's address and ``order.md`` layer, and
  the port's callable), one row per family class (direct base, vtable, slot
  count, entity classnames) and one row per class's own slot body.  The census
  is data only: nothing in it invents a behaviour, and the class tree takes its
  rows as identity (``ClassNamed``), never as a dispatch key.
* per chain class (``SLOT_SURFACES``: the entity, animating, overlay, flex and
  combat-character nodes, ``FElysiumNpcBase``, ``FElysiumNpc``), ``…Slots.inl``
  — one ``virtual`` declaration per slot the class introduces or refills whose
  body the port does not already carry under a mapped name, each with
  ``// slot N  0x……  (tier)`` — and ``…Slots.cpp``, those virtuals' bodies:
  retail's one-constant body where a ``default:`` verdict records it, else a
  named stub that tallies ``elysium.stubs`` with the retail address and the
  owning story (the tally joins ``functions.md`` by address).  Both are port
  code: stubs keep counting until a story ports the body, and one-constant
  bodies stay.

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
* no verdict, but the address is in ``RETAIL_DEFAULTS`` (story ``L0.tooling.default-stubs``) —
  the same emission as ``default:``.  These are the entity chain's bodies no band's checklist
  covers, which the 2026-10-06 re-check (``docs/specs/layers/audit.tsv``, ``firm:carried``) found
  empty or answering one constant the stub's value-initialised answer already gave; only the
  stub's tally differed.  The table is a reviewed reading, like ``SLOT_PORT_MAP``; the body is
  the fact, so a readable one-literal body that disagrees fails generation, and an entry that
  lands on no generated stub is stale and fails it too.  It decides the emission rows only: the
  census row's ``Default`` stays the overlay's (a default only where a verdict read the body).
* ``dead`` with either spelling (spec 0019 story 1) — the same emission as above.  A
  ``dead`` row keeps its port target until 0019/6 deletes the body and writes ``-``,
  so judging a slot dead changes nothing the runtime does.
* a **closed** row (0019/6: ``dead`` at ``-``, ``mechanism`` at a service word of
  ``kernel_ledger.SERVICE_TARGETS``) — the port body is gone and nothing may tally for it:

  ======================  ==========================================  ===========================
  layer                   the slot is dispatched, or another body     neither
                          at it stays (a live layer, a species
                          body not closed, an overridden-below row)
  ======================  ==========================================  ===========================
  introducer (virtual)    declared, with the generated ``default:``   **nothing**: no virtual,
                          body — retail's literal where its whole     no table row; the census
                          body is one (a probed ``Default`` row),     row stays with an empty
                          else the value-initialised answer           port callable (``CLOSED``)
                          (``Closed``); never a stub
  override (per owner)    **nothing**: the class inherits             the same
  ======================  ==========================================  ===========================

  "Dispatched" is the ledger's ``slot_dispatch_sites`` — the *Sites* column of ``slots.md``, the
  fact story 1 wrote "no dispatch site" from.  A species own body at ``-`` / a service word emits
  no override either (``kernel_shape.override_rows`` drops it; the class inherits).  A closed row
  never emits a stub: a firing stub is for an open ``rule`` / ``present`` / ``mechanism`` row.
  A ``CHAIN_HAND`` body or a ``SLOT_PORT_MAP`` ``PORT`` row over a closed body is a
  contradiction; the first fails generation, the second needs a ``DELETED`` row.
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
from dataclasses import dataclass, field, replace
from pathlib import Path

from elysium_pipeline.paths import repo_root

sys.path.insert(0, str(Path(__file__).resolve().parent / "ghidra" / "driver"))

import kernel_ledger as kl  # noqa: E402
import kernel_shape as ks  # noqa: E402

SUBSTRATE = ("Source", "ElysiumUE", "Private", "Substrate")
PUBLIC = ("Source", "ElysiumUE", "Public")
CENSUS_OUTPUT = (*SUBSTRATE, "ElysiumNpcKernelShape.cpp")
# The layer classes and their port types.
BASE_LAYER = "CAI_BaseNPC"
LAYER_PORT = {"CAI_BaseNPC": "FElysiumNpcBase", "CAI_BaseNPCTroika": "FElysiumNpc"}

# The retail chain under the Troika leaf, base first, and the port class that stands for each node
# (story 0019/5 step 6). The port has no `CBaseToggle` node (`gen_kernel_bindings`' stated
# divergence): a body `CBaseToggle`'s own table holds stands on `FElysiumAnimating`, the nearest port
# class below it.
CHAIN = ("CBaseEntity", "CBaseToggle", "CBaseAnimating", "CBaseAnimatingOverlay", "CBaseFlex",
         "CBaseCombatCharacter", "CAI_BaseNPC", "CAI_BaseNPCTroika")
CHAIN_PORT = {
    "CBaseEntity": "FElysiumEntity",
    "CBaseToggle": "FElysiumAnimating",
    "CBaseAnimating": "FElysiumAnimating",
    "CBaseAnimatingOverlay": "FElysiumAnimatingOverlay",
    "CBaseFlex": "FElysiumFlex",
    "CBaseCombatCharacter": "FElysiumCombatCharacter",
    "CAI_BaseNPC": "FElysiumNpcBase",
    "CAI_BaseNPCTroika": "FElysiumNpc",
}
PORT_CHAIN = ("FElysiumEntity", "FElysiumAnimating", "FElysiumAnimatingOverlay", "FElysiumFlex",
              "FElysiumCombatCharacter", "FElysiumNpcBase", "FElysiumNpc")
# The class a port class derives from, for the using-declarations that keep an overridden name's
# other overloads visible. `FElysiumScriptedCharacter` is the port-only node between the combat
# character and the NPC base; it declares no slot.
PORT_PARENT = {
    "FElysiumAnimating": "FElysiumEntity",
    "FElysiumAnimatingOverlay": "FElysiumAnimating",
    "FElysiumFlex": "FElysiumAnimatingOverlay",
    "FElysiumCombatCharacter": "FElysiumFlex",
    "FElysiumNpcBase": "FElysiumScriptedCharacter",
    "FElysiumNpc": "FElysiumNpcBase",
}


@dataclass(frozen=True)
class SlotSurface:
    """One port class's generated slot surface: the declarations (included inside the class body),
    their definitions, the header that declares the class, and the unit-unique stub helper."""

    inl: tuple[str, ...]
    cpp: tuple[str, ...]
    header: str
    fire: str


# The slot surface, split by the class that owns each body (story 0019/5 step 6; step 5 split it by
# NPC layer only). A slot is declared on the port class of the retail class that introduces it, with
# that class's own body; each port class whose retail table refills the slot overrides it.
SLOT_SURFACES = {
    "FElysiumEntity": SlotSurface((*PUBLIC, "ElysiumEntitySlots.inl"),
                                  (*SUBSTRATE, "ElysiumEntitySlots.cpp"),
                                  "ElysiumEntity.h", "FireEntitySlot"),
    "FElysiumAnimating": SlotSurface((*PUBLIC, "ElysiumAnimatingSlots.inl"),
                                     (*SUBSTRATE, "ElysiumAnimatingSlots.cpp"),
                                     "ElysiumAnimating.h", "FireAnimatingSlot"),
    "FElysiumAnimatingOverlay": SlotSurface((*PUBLIC, "ElysiumAnimatingOverlaySlots.inl"),
                                            (*SUBSTRATE, "ElysiumAnimatingOverlaySlots.cpp"),
                                            "ElysiumAnimatingOverlay.h", "FireAnimatingOverlaySlot"),
    "FElysiumFlex": SlotSurface((*PUBLIC, "ElysiumFlexSlots.inl"),
                                (*SUBSTRATE, "ElysiumFlexSlots.cpp"),
                                "ElysiumFlex.h", "FireFlexSlot"),
    "FElysiumCombatCharacter": SlotSurface((*PUBLIC, "ElysiumCombatCharacterSlots.inl"),
                                           (*SUBSTRATE, "ElysiumCombatCharacterSlots.cpp"),
                                           "ElysiumPlayer.h", "FireCombatCharacterSlot"),
    "FElysiumNpcBase": SlotSurface((*SUBSTRATE, "ElysiumNpcBaseSlots.inl"),
                                   (*SUBSTRATE, "ElysiumNpcBaseSlots.cpp"),
                                   "Substrate/ElysiumNpcBase.h", "FireKernelBaseSlot"),
    "FElysiumNpc": SlotSurface((*SUBSTRATE, "ElysiumNpcSlots.inl"),
                               (*SUBSTRATE, "ElysiumNpcSlots.cpp"),
                               "Substrate/ElysiumNpc.h", "FireKernelSlot"),
}

# The verdict overlay's target spellings that change what the generator emits (story 29c).
DEFAULT_PREFIX = "default:"
HAND_PREFIX = "hand:"
# 0019/6: a mechanism row whose hand body is a one-line forward into its service. Generated exactly
# like `hand:` (declaration only, definition in the substrate); the ledger counts it closed.
SEAM_PREFIX = "seam:"


def hand_target(target: str) -> str:
    """The hand body a `hand:` or `seam:` target names, else ""."""
    for prefix in (HAND_PREFIX, SEAM_PREFIX):
        if target.startswith(prefix):
            return target[len(prefix):].strip()
    return ""
# A retail default the generator will emit. Anything else in a `default:` target is a reading that
# has not settled — a `DAT_`, a `param_1`, a `this` — and generation fails rather than inventing.
DEFAULT_LITERAL_RE = re.compile(r"^(?:void|-?\d+(?:\.\d+)?|0x[0-9a-fA-F]+)$")
REGISTRY_PREFIX = "registry:"
# The whole body of a constant-returning override, and nothing else. Deliberately strict: the
# census may carry the literal a species answers only when the body cannot be anything else.
CONSTANT_BODY_RE = re.compile(
    r"^\s*\{\s*return\s*(;|(?:-?\d+|0x[0-9a-fA-F]+)\s*;)\s*\}\s*$", re.S)

# Retail-default bodies the verdict overlay does not record (story `L0.tooling.default-stubs`): the
# entity chain's slot bodies the 2026-10-06 re-check (`docs/specs/layers/audit.tsv`, status
# `firm:carried`, the port line a generated stub) found empty or answering one constant that the
# stub's value-initialised answer already matched. Each is emitted as a `default:` body: retail's
# answer, no tally. The 113- and 118-byte bodies are VPROF's scope-trace push and pop around nothing
# (`g_ScopeTraceStack`, `g_ScopeTraceStackDepth`: depth +1 then -1, read by no rule) and then the
# constant; the eight other stubs of that re-check (`GetRefEHandle`, `GetDataDescMap`,
# `GetLocalVelocity`, slot 240, `IsActivityFinished`, `GetViewtarget`, the overlay's
# `StudioFrameAdvance` and `DispatchAnimEvents`) answer a field, a pointer or real work and stay
# stubs. address -> (literal, the reading).
RETAIL_DEFAULTS: dict[str, tuple[str, str]] = {
    "10026590": ("void", "CBaseEntity::OnVictimHitByMe 0x10026590: `ret 4`"),
    "100266b0": ("0", "CBaseEntity::GetHighlightMaterial 0x100266b0: `return 0;`"),
    "10026a90": ("0", "CBaseEntity::ShouldIgnoreCollision 0x10026a90: `return false;`"),
    "10026ab0": ("0", "CBaseEntity::NavIgnoreCollision 0x10026ab0: `return false;`"),
    "10026ad0": ("0", "CBaseEntity slot 72 0x10026ad0: `return 0;`"),
    "10026af0": ("0", "CBaseEntity::CausesImpactDamage 0x10026af0: `return false;`"),
    "10026b10": ("0", "CBaseEntity::ReceivesImpactDamage 0x10026b10: `return false;`"),
    "100aa900": ("0", "CBaseEntity::TestCollision 0x100aa900: the scope-trace push and pop, then "
                      "the low byte of EAX cleared (`& 0xffffff00`): false, the trace untouched"),
    "100aa9a0": ("0", "CBaseEntity::TestHitboxes 0x100aa9a0: the scope-trace push and pop, then "
                      "the low byte of EAX cleared (`& 0xffffff00`): false, the trace untouched"),
    "10026b90": ("void", "CBaseEntity::Precache 0x10026b90: `ret`"),
    "10026bb0": ("void", "CBaseEntity::MemberSync 0x10026bb0: `ret`"),
    "10026d30": ("0", "CBaseEntity slot 136 0x10026d30: `return 0;`"),
    "10026d50": ("0", "CBaseEntity::GetBaseAnimating 0x10026d50: `return NULL;`"),
    "100a7a80": ("0", "CBaseEntity::Classify 0x100a7a80: `return 0;` (CLASS_NONE)"),
    "10026e50": ("0.0", "CBaseEntity::GetDelay 0x10026e50: `return _DAT_104454c4;`, the pooled "
                        "float 0.0"),
    "10027000": ("0", "CBaseEntity::GetEnemy const 0x10027000: `return NULL;`"),
    "100b5080": ("0", "CBaseEntity::CreateVPhysics 0x100b5080: `return false;`"),
    "100a65a0": ("void", "CBaseEntity::VPhysicsShadowCollision 0x100a65a0: the scope-trace push "
                         "and pop, nothing else"),
    "100273d0": ("void", "CBaseEntity::VPhysicsShadowUpdate 0x100273d0: `ret 4`"),
    "10039ff0": ("void", "CBaseEntity::PerformCustomPhysics 0x10039ff0: the scope-trace push and "
                         "pop; the four out-arguments untouched"),
}

# The flattened table `FElysiumNpc` stands for. Species classes add words past its end; the census
# carries them as rows, and each is a member of its own port class.
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
# where to stop reading each. The middle chain classes have their own headers, and `ElysiumPlayer.h`
# declares `FElysiumCombatCharacter` before `FElysiumPlayer`, which is a sibling leaf: the scan
# stops where the player's own surface begins, because a name only the player declares is not a name
# the NPC's chain holds.
PORT_CHAIN_HEADERS = (
    ("Source/ElysiumUE/Public/ElysiumEntity.h", None),
    ("Source/ElysiumUE/Public/ElysiumAnimating.h", None),
    ("Source/ElysiumUE/Public/ElysiumAnimatingOverlay.h", None),
    ("Source/ElysiumUE/Public/ElysiumFlex.h", None),
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
# collision is accidental and the slot takes `<Name>Slot<N>`. The last column records why. (The one
# `generated-override` row, slot 488 over the schedule runner's `DeathSound` hook, retired with that
# hook at story 8 wave 2: slot 488 is an ordinary generated slot now.)

PORT = "port"
SUFFIX = "suffix"
# A stub whose body is dead, uncalled and overridden nowhere (0019/5 step 6): no virtual is
# generated, and the census row stays with an empty port callable.
DELETED = "deleted"
# Not a map row: the kind `close_layers` gives a slot whose every body is closed (0019/6) and that
# nothing dispatches. Emitted exactly as `DELETED`: no virtual, an empty census port callable.
CLOSED = "closed"
# A slot a class below the NPC line overrides (0019/5 commit B): the virtual is generated under its
# retail name and every class below the NPC line that declares the name declares it `override`, so
# the member IS the slot's body on that class. A same-name member without `override` (a hide under
# another signature) fails generation; commit B retired the hides this kind replaced.
OVERRIDDEN_BELOW = "overridden-below"

SLOT_PORT_MAP: dict[int, tuple[str, str, str]] = {}

# The chain slots whose body is written by hand on the owning chain class, beside the verdict
# overlay's `hand:` spellings (0019/5 step 6; the audited dispositions are the spec's story-5 job): slot -> (the
# port return type where the lowering would lose it, the reason). A reference return the generator
# lowers to `void*` is restored here, because the port has the value it refers to.
CHAIN_HAND: dict[int, tuple[str, str]] = {
    62: ("", "`CBaseEntity::SetOrigin` 0x100b2be0: writes through `SetRuntimeOrigin` when the origin "
             "differs; the change-tracker byte `+0x1b1` stays the Slot88/89 refusal"),
    94: ("", "`CBaseEntity::GetMoveType` 0x100aac30: reads the `RetailMoveType` word slot 93 writes. Live check "
             "0019/6: the counting stub answered 0 and failed `CheckOnGround`'s `!= MOVETYPE_STEP` guard "
             "on every NPC, every think"),
    208: ("", "`CBaseEntity::SetGroundEntity` 0x100b1420: writes the `RetailGroundEntity` handle word "
              "(`m_hGroundEntity +0x384`); the floor-facts seam's ground entity lands here (0019/6)"),
    209: ("", "`CBaseEntity::GetGroundEntity` 0x100b1510: resolves the `RetailGroundEntity` handle word "
              "(0019/6)"),
    93: ("", "`CBaseEntity::SetMoveType` 0x100aad70: the `RetailMoveType`/`RetailMoveCollide` seam, "
             "written only when the type differs; the physics-object notify is a refusal"),
    194: ("const FVector&", "`EyeAngles` 0x100b4bc0: slot 219's answer (hand body, verdict overlay)"),
    195: ("const FVector&", "`LocalEyeAngles` 0x100b4be0: slot 221's answer (hand body, verdict overlay)"),
    217: ("const FVector&", "`GetAbsOrigin` 0x100b31b0: `Origin`, in port units"),
    219: ("const FVector&", "`GetAbsAngles` 0x100b3280: `Angles`, Source degrees"),
    220: ("const FVector&", "`GetOrigin` 0x100b3070: `Origin`; the port has no local/abs split"),
    221: ("const FVector&", "`GetAngles` 0x100b3110: `Angles`; the port has no local/abs split"),
    222: ("", "`CBaseEntity::GetSoundEmissionOrigin` 0x100a9eb0: the scope-trace label, then slot 192 "
              "`WorldSpaceCenter` through the dispatch, returned as the sound emission origin "
              "(L0.audio.sound-emission-origin, walks/L0-r006.md)"),
    142: ("", "`CBaseCombatCharacter::OnTakeDamage` 0x1032ef60: the m_takedamage and team gates, the "
              "life-state split into slots 390/391/392 and the death arm (story 8 wave 2, L13)"),
    390: ("", "`CBaseCombatCharacter::OnTakeDamage_Alive` 0x103302e0: the resolver and the typed "
              "health commit (story 8 wave 2, L13)"),
    144: ("", "`CBaseCombatCharacter::Event_Killed` 0x1032b9b0: LIFE_DYING, the weapon drop, the "
              "grapple partner's feed teardown, the owner notice and slot 301 (story 8 wave 2, L13)"),
    301: ("", "`CBaseCombatCharacter::CreateCorpse` 0x1032c0e0: the ragdoll corpse, "
              "`BecomeClientRagdoll` (story 8 wave 2, L13)"),
}

# A `CHAIN_HAND` slot whose hand body stands on ONE chain owner only; the other chain classes that
# hold a body at the slot keep theirs as generated: slot -> the port owner.
CHAIN_HAND_OWNER: dict[int, str] = {
    142: "FElysiumCombatCharacter",
    390: "FElysiumCombatCharacter",
    144: "FElysiumCombatCharacter",
    301: "FElysiumCombatCharacter",
}


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
        (15, PORT, "FElysiumEntity::SetAttackExtents", "`CBaseEntity::SetAttackExtents` 0x1009af40"),
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
        (118, PORT, "FElysiumEntityWorld::AcceptInput",
         "`CBaseEntity::AcceptInput` 0x100abc90 is the datamap INPUT walk; the world's chokepoint and "
         "the class registry are that walk (0019/5 step 6)"),
        (119, PORT, "FElysiumEntity::Kill", "terminal: mark dead and go inert"),
        (134, PORT, "FElysiumNpc::Think", "`NPCThink` 0x10292de0, the whole pass"),
        (580, PORT, "FElysiumNpcBase::ClassScheduleIdSpace",
         "the typed id space: the base body 0x101a6d00 and `FElysiumNpc`'s override, the Troika body "
         "0x101aa790 (0019/5 step 6, the step-5 carried item); a `void*` beside it would be a second "
         "producer of the same answer"),
        (173, PORT, "FElysiumEntity::Use", "the `+use` door"),
        (177, PORT, "FElysiumDoorBase::StartBlocked",
         "`CBaseDoor::StartBlocked` 0x100f1340, the door's own body (0018/7): the activator's NPC "
         "gets flags `|= 2` and `OnDoorBlocked`, the blocker's `|= 0x80` and hit-by-door 0x1027dfb0; "
         "its dispatcher is unrecovered, so nothing calls it yet"),
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
        (438, PORT, "FElysiumNpc::SpeciesSelectSchedule",
         "the overridable half of the selector: `FElysiumNpc::SelectSchedule` asks it first and runs "
         "the Troika body 0x1028a380 (the state switch) when it answers 0, which is where a species "
         "body's direct call to the base lands; each species body is this virtual's override "
         "(0019/5 commit B named the virtual, so `kernel_shape --unported` sees the overrides)"),
        (440, PORT, "FElysiumNpc::TranslateScheduleRetail",
         "the schedule translation in retail numbers (story 25), which the typed runner hook "
         "`FElysiumNpc::TranslateSchedule` wraps; each species body is this virtual's override "
         "(0019/5 commit B named the virtual rather than the wrapper)"),
        (448, PORT, "FElysiumNpc::TaskFail", "the failure route, ported in story 13"),
        (453, PORT, "FElysiumNpc::BuildScheduleTestBits", "the interrupt mask, ported in story 25"),
        (460, PORT, "FElysiumNpcBase::PreSelectIdealStateRetail",
         "0019/8 shape: the typed `SelectIdealState()` wrapper keeps the census name; the slot's "
         "virtual is the Retail one"),
        (461, PORT, "FElysiumNpcBase::SelectIdealStateRetail",
         "0019/8 shape: the typed `SelectIdealState()` wrapper keeps the census name; the slot's "
         "virtual is the Retail one"),
        (534, PORT, "FElysiumCombatCharacter::EyeLookTargetHandle",
         "the gaze cascade's chosen subject; `EyeLookTarget` beside it is the point it resolved to"),
        # --- Slots a class below the NPC line overrides (0019/5 commit B) ---------------------
        (66, OVERRIDDEN_BELOW, "", "`FElysiumWeapon::Hide()`: `CBaseEntity::Hide` 0x1009d2a0 on the "
         "class that carries the EF_NODRAW bit, the owner's wield model going with it"),
        (67, OVERRIDDEN_BELOW, "", "`FElysiumWeapon::Unhide()`: `CBaseEntity::Unhide` 0x1009d380, as "
         "slot 66"),
        (104, OVERRIDDEN_BELOW, "", "`FElysiumAmbientGeneric::Precache()`: `CAmbientGeneric::Precache` "
         "0x101ac930, the ambient's override of `CBaseEntity::Precache` 0x10026b90 (a bare `ret`), reached "
         "as its Spawn's virtual tail `JMP [vtable + 0x1a0]` (L0.audio.ambient-init)"),
        (86, OVERRIDDEN_BELOW, "", "`FElysiumCameraCinematic::ShouldTransmit`: `CBaseCineCam::vfunc86` "
         "0x1006e6a0, the subject-only transmit, over the single player as recipient"),
        (123, OVERRIDDEN_BELOW, "", "`FElysiumCameraCinematic::DrawDebugGeometryOverlays`: "
         "`CBaseCineCam` 0x1006ff40, gated on `camera_showdebug == 1`"),
        (133, OVERRIDDEN_BELOW, "", "`FElysiumMoverBase::MoveDone()`: the mover line's own `MoveDone` "
         "(retail's doors and buttons override `CBaseToggle::MoveDone`)"),
        (153, OVERRIDDEN_BELOW, "", "`FElysiumMoverBase::IsMoving()`: `CBaseEntity::IsMoving` "
         "0x10026e70 (`m_vecVelocity != 0`), which on a mover is a move in flight"),
        (582, DELETED, "",
         "`CAI_BaseNPC::ReportOverThinkLimit` 0x10277d90: dead, no caller, overridden nowhere "
         "(0019/5 step 6)"),
        (534, DELETED, "",
         "`CBaseCombatCharacter` slot 534 0x1026b270: dead (0019/1), closed at `-`; the port's "
         "`FElysiumCombatCharacter::EyeLookTargetHandle` stays a plain method, no virtual (0019/6)"),
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
    default_why: str = ""    # a `RETAIL_DEFAULTS` reading, when no verdict records the default
    hand: str = ""           # the port method that defines this virtual by hand
    # The emission by owner (story 0019/5 steps 5-6). `layers` holds the rows the slot files emit
    # for this slot, one per port class whose retail table holds a body of its own there: the
    # introducer's (`virtual`) and each more-derived class's that refills it (`override`). `retail`
    # is the retail class that owns the row's body (the most-base table holding that pointer); the
    # stub names it. The merged row itself — the body a Troika instance runs — is what the census
    # and the probes read.
    owner: str = "FElysiumNpc"
    retail: str = "CAI_BaseNPCTroika"
    override: bool = False
    layers: list["Slot"] = field(default_factory=list)

    @property
    def generated(self) -> bool:
        return self.port_kind not in (PORT, DELETED, CLOSED)

    @property
    def closed(self) -> bool:
        """The overlay closes this row's body (0019/6): `dead` at `-`, `mechanism` at a service word."""
        return kl.body_closed(self.verdict, self.verdict_target)

    @property
    def stubbed(self) -> bool:
        """A generated virtual with no recovered body, no hand-written definition, and a body the
        overlay has not closed (a closed body answers silently; it never tallies)."""
        return self.generated and not self.default and not self.hand and not self.closed

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
        return (f"return static_cast<{port}>({literal});", "static_cast<int64>(Receiver.{call})", value)
    value = int(literal, 0)
    if port == "bool":
        if value not in (0, 1):
            raise SystemExit(f"gen_kernel_shape: slot {row.slot} returns bool but retail returns "
                             f"{literal}")
        return f"return {'true' if value else 'false'};", "Receiver.{call} ? 1 : 0", value
    if port.endswith("*"):
        if value != 0:
            raise SystemExit(f"gen_kernel_shape: slot {row.slot} returns `{port}` but retail "
                             f"returns {literal}; a pointer constant is not a default")
        return "return nullptr;", "static_cast<int64>(reinterpret_cast<UPTRINT>(Receiver.{call}))", 0
    if port == "EElysiumNpcState":
        return (f"return static_cast<EElysiumNpcState>({literal});",
                "static_cast<int64>(Receiver.{call})", value)
    if port in ("float", "double"):
        return (f"return static_cast<{port}>({literal});", "static_cast<int64>(Receiver.{call})", value)
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
        return (f"return static_cast<{port}>({literal});", "static_cast<int64>(Receiver.{call})",
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
    # The overlay closes this own body (0019/6): no override is emitted or owed; the class inherits.
    closed: bool = False


def constant_return(code: str) -> str:
    """The literal a body returns when its whole body is `return <literal>;`, else `""`.

    The census carries the value a class's own body answers where the whole body is one constant
    (the `registry:` verdicts), so a species override can be checked against it. The *decision*
    that a body is that shape is a reading and lives in the verdict overlay; this only reads the value back off the decompiled
    C, and only when the body has exactly one statement, so it cannot be fooled into recording a
    behaviour as a value.
    """
    body = code[code.index("{"):code.rindex("}") + 1] if "{" in code and "}" in code else ""
    match = CONSTANT_BODY_RE.match(body)
    if not match:
        return ""
    tail = match.group(1).rstrip(";").strip()
    return tail or "void"


def same_word(read: str, literal: str) -> bool:
    """Whether two integer literals are the same 32-bit return word.

    The decompiler prints a return in the signedness of the prototype it holds, so re-applying
    names and prototypes to the corpus can turn `return 0xffffffff;` into `return -1;` with no
    change to the body. Both are the one word EAX carries.
    """
    return int(read, 0) & 0xFFFFFFFF == int(literal, 0) & 0xFFFFFFFF


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


# A class declaration and its bases: `class [API] Name [final] : public A, public B {`.
CLASS_DECL_RE = re.compile(r"^\s*(?:class|struct)\s+(?:\w+_API\s+)?(\w+)(?:\s+final)?\s*:\s*([^{;]+)\{",
                           re.M)
# The port classes whose descendants are the NPC line; their names are the surface this generator
# declares or overrides on purpose.
NPC_LINE = ("FElysiumScriptedCharacter", "FElysiumNpcBase", "FElysiumNpc")


_BRACE_RE = re.compile(r"[{}]")


def _class_body(text: str, start: int) -> str:
    """The text from `start` to the brace that closes the one before it (to the last character
    but one when nothing closes it). Brace by brace, not character by character: this was half of
    the scan over every source file."""
    depth = 1
    for brace in _BRACE_RE.finditer(text, start):
        depth += 1 if brace.group() == "{" else -1
        if depth == 0:
            return text[start:brace.start()]
    return text[start:max(len(text), start) - 1]


def _class_scope_lines(body: str) -> list[str]:
    """The lines of a class body at its own brace depth: a local inside an inline method body is
    not a member."""
    out, depth = [], 0
    for line in body.splitlines():
        if depth == 0:
            out.append(line)
        depth += line.count("{") - line.count("}")
        depth = max(depth, 0)
    return out


def chain_subclass_names(repo: Path, overrides: dict[str, set[str]] | None = None) -> dict[str, set[str]]:
    """Member name -> the classes that declare it, over every class that derives from the chain
    below the NPC line: the player, items, props, triggers, the makers and directors, the ~50
    `FElysiumEntity` subclasses (story 0019/5 step 6).

    From step 6 a generated slot on `FElysiumEntity`, `FElysiumAnimating`, the overlay, the flex
    node or the combat character is a virtual every one of those inherits. A same-name member there
    would silently override it (same signature) or hide it (another signature), and neither is a
    default: the collision is a `SLOT_PORT_MAP` decision. `overrides`, when given, collects the
    classes whose declaration of a name carries `override` (an `OVERRIDDEN_BELOW` row's proof).
    """
    bases: dict[str, list[str]] = {}
    bodies: dict[str, list[str]] = collections.defaultdict(list)
    for path in sorted((repo / "Source" / "ElysiumUE").rglob("*")):
        if path.suffix not in (".h", ".cpp"):
            continue
        text = path.read_text(encoding="utf-8-sig", errors="replace")
        for match in CLASS_DECL_RE.finditer(text):
            name = match.group(1)
            parents = [re.sub(r"^(?:public|protected|private|virtual)\s+", "", p.strip()).split("<")[0]
                       for p in match.group(2).split(",")]
            bases.setdefault(name, []).extend(p.split("::")[-1].strip() for p in parents)
            bodies[name].append(_class_body(text, match.end()))

    def derives(name: str, target: str, seen: set[str]) -> bool:
        if name == target:
            return True
        if name in seen:
            return False
        seen.add(name)
        return any(derives(parent, target, seen) for parent in bases.get(name, ()))

    names: dict[str, set[str]] = collections.defaultdict(set)
    for name in bases:
        if name in PORT_CHAIN or name in PORT_PARENT.values():
            continue
        if not derives(name, "FElysiumEntity", set()):
            continue
        if any(derives(name, line, set()) for line in NPC_LINE):
            continue
        for body in bodies[name]:
            for line in _class_scope_lines(body):
                for pattern in (DECL_RE, MEMBER_RE):
                    match = pattern.match(line)
                    if match:
                        names[match.group(1)].add(name)
                        # The declaration's own text: a trailing `//` or inline `/* */` comment that
                        # spells `override` is not one.
                        code = re.sub(r"/\*.*?\*/", "", line.split("//", 1)[0])
                        if overrides is not None and pattern is DECL_RE and re.search(r"\boverride\b", code):
                            overrides.setdefault(match.group(1), set()).add(name)
    for keyword in ("if", "for", "while", "switch", "return", "sizeof", "static_cast", "else", "catch"):
        names.pop(keyword, None)
    return names


def apply_verdict(row: Slot, ledger) -> None:
    """Read the overlay's verdict for `row.body` into the row's emission (default / hand)."""
    row.verdict, row.verdict_target, row.default, row.hand = "", "", "", ""
    row.default_why = ""
    verdict = ledger.verdicts.get(row.body)
    if verdict is not None:
        row.verdict, row.verdict_target = verdict.verdict, verdict.target
    # What the verdict does to the emission. A `rule` whose whole retail body is a constant
    # lands as that constant; a `rule` or `present` whose body is written by hand in the
    # substrate loses its definition here. Everything else keeps its stub and carries the
    # verdict in the comment.
    # `dead` rides along since 0019/1: a `dead` row keeps its port target until 0019/6 removes
    # the body and writes `-`, so a slot the pass judged dead emits exactly what it emitted
    # before and the judgment alone changes no runtime behaviour.
    if row.verdict in ("rule", "present", "mechanism", "dead"):
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
            if read and read != literal and not same_word(read, literal):
                raise SystemExit(
                    f"gen_kernel_shape: slot {row.slot} ({row.address}) records "
                    f"`default:{literal}` but the body returns `{read}`")
            row.default = literal
            default_body(row)   # fail here, not at the C++ compiler, on a type it cannot lower
        elif hand_target(row.verdict_target):
            row.hand = hand_target(row.verdict_target)


def apply_retail_default(row: Slot, ledger) -> None:
    """A still-stubbed emission row whose retail body `RETAIL_DEFAULTS` reads as one constant
    answers it. Layer rows only (`split_layers`): the census row's `Default` stays the overlay's."""
    reading = RETAIL_DEFAULTS.get(row.body)
    if reading is None or not row.generated or row.default or row.hand or row.closed:
        return
    literal, why = reading
    body = ledger.functions.get(row.body)
    read = constant_return(body.code or "") if body is not None else ""
    if read and read != literal and not same_word(read, literal):
        raise SystemExit(f"gen_kernel_shape: `RETAIL_DEFAULTS` reads {row.address} as "
                         f"`{literal}` but the body returns `{read}`")
    row.default, row.default_why = literal, why
    default_body(row)   # fail here, not at the C++ compiler, on a type it cannot lower


def chain_tables(ledger) -> dict[str, dict[int, str]]:
    """Each retail chain node's primary vtable, thunk-resolved, from the corpus. The ledger's own
    `slot_bodies` covers only the 77 family classes; the entity chain's nodes are below them."""
    tables: dict[str, dict[int, str]] = {}
    for cls in CHAIN:
        rows = ledger.db.execute(
            "SELECT slot, func FROM vtables WHERE module = ? AND sub = 0 AND cls = ?",
            (ledger.module, cls))
        tables[cls] = {r["slot"]: ledger.resolve(r["func"]) for r in rows}
        if not tables[cls]:
            raise SystemExit(f"gen_kernel_shape: the corpus has no primary vtable for {cls}")
    return tables


def chain_bodies(slot: int, tables: dict[str, dict[int, str]]) -> list[tuple[str, str, str]]:
    """(port class, owning retail class, body) per port class whose body at `slot` differs from the
    one it inherits, base first. A port class's body is the one its most-derived retail node holds;
    the owner is the most-base node holding that same pointer."""
    holders = [cls for cls in CHAIN if slot in tables[cls]]
    out: list[tuple[str, str, str]] = []
    previous = None
    for port in PORT_CHAIN:
        nodes = [cls for cls in holders if CHAIN_PORT[cls] == port]
        if not nodes:
            continue
        body = tables[nodes[-1]][slot]
        if body == previous:
            continue
        owner = next(cls for cls in holders if tables[cls][slot] == body)
        out.append((port, owner, body))
        previous = body
    return out


def split_layers(row: Slot, ledger, tables: dict[str, dict[int, str]]) -> None:
    """The per-owner emission of a generated Troika-line slot (story 0019/5 steps 5-6).

    A slot is declared on the port class of the retail class that introduces it, with that class's
    own body; every more-derived port class whose retail table holds another body overrides it
    (`chain_bodies`). A slot past the base's table is Troika's own and declared on `FElysiumNpc`.
    Each layer row carries its own body's address, layer, story and verdict, so a Troika instance
    dispatches exactly what the merged row names and a shallower instance runs its own class's body.
    """
    if not row.generated:
        return

    def layer_row(body: str, owner: str, retail: str, override: bool) -> Slot:
        layer = replace(row, body=body, address=f"0x{body}" if body else "", owner=owner,
                        retail=retail, override=override, layers=[], notes=list(row.notes))
        layer.layer = ledger.layer_of.get(body, -1)
        layer.story = story_for(layer.layer) if layer.layer >= 0 else ""
        apply_verdict(layer, ledger)
        apply_retail_default(layer, ledger)
        if row.slot in CHAIN_HAND and owner == CHAIN_PORT[retail] and owner in SLOT_SURFACES \
                and owner not in LAYER_PORT.values() \
                and CHAIN_HAND_OWNER.get(row.slot, owner) == owner:
            layer.hand = f"{owner}::{row.port_name}"
        return layer

    if row.slot in tables[BASE_LAYER]:
        for index, (port, retail, body) in enumerate(chain_bodies(row.slot, tables)):
            row.layers.append(layer_row(body, port, retail, index > 0))
        # The merged row is the body a Troika instance runs: a chain hand body it inherits is its.
        if not row.hand and row.layers[-1].hand and row.layers[-1].body == row.body:
            row.hand = row.layers[-1].hand
    else:
        row.layers.append(layer_row(row.body, LAYER_PORT[BASE_TABLE], BASE_TABLE, False))


def close_layers(row: Slot, ledger) -> None:
    """Apply 0019/6's closed rows to a generated slot's layers (the table in the module doc).

    A closed override layer is dropped: its class inherits. A closed introducer is kept only while
    something can still reach the virtual — a dispatch site at the slot, a layer that stays, a
    species own body the overlay has not closed, or a class below the NPC line overriding it — and
    then answers the generated `default:` body instead of a stub. Otherwise the slot emits nothing.
    """
    if not row.generated or not row.layers:
        return
    for layer in row.layers:
        if layer.closed and layer.hand and not hand_target(layer.verdict_target):
            raise SystemExit(f"gen_kernel_shape: slot {row.slot} ({layer.address}) is closed at "
                             f"`{layer.verdict_target}` but `CHAIN_HAND` still names its hand body "
                             f"`{layer.hand}`; retire the `CHAIN_HAND` row with the body")
    kept = [layer for index, layer in enumerate(row.layers) if index == 0 or not layer.closed]
    first = kept[0]
    if first.closed:
        closed_bodies = {layer.body for layer in row.layers if layer.closed}
        species_live = any(
            addr not in closed_bodies and not (addr in ledger.verdicts and kl.body_closed(
                ledger.verdicts[addr].verdict, ledger.verdicts[addr].target))
            for addr in ledger.slot_bodies.get(row.slot, {}).values())
        reached = (ledger.slot_dispatch_sites.get(row.slot, 0) > 0 or len(kept) > 1 or species_live
                   or row.port_kind == OVERRIDDEN_BELOW)
        if not reached:
            row.layers = []
            row.port_kind, row.port_name = CLOSED, ""
            row.port_why = (f"closed at `{first.verdict_target}` ({first.verdict}): no dispatch site "
                            "and no other body at the slot (0019/6)")
            return
        body = ledger.functions.get(first.body)
        literal = constant_return(body.code or "") if body is not None else ""
        if literal and DEFAULT_LITERAL_RE.match(literal):
            first.default = literal
            try:
                default_body(first)
            except SystemExit:
                first.default = ""   # a constant the probe cannot lower: the silent body instead
    if len(kept) < len(row.layers):
        # The census row names the body a Troika instance RUNS. With a closed override dropped, that
        # is the deepest surviving layer, not the holder's retail body (0019/6: slot 17's
        # `CAI_BaseNPC::TraceMessage 0x1028de90` is gone; the NPC runs `CBaseEntity`'s 0x1009b380).
        survivor = kept[-1]
        row.body = survivor.body
        row.address = f"0x{survivor.body}" if survivor.body else ""
        row.layer = getattr(ledger, "layer_of", {}).get(survivor.body, row.layer)
        row.story = story_for(row.layer) if row.layer >= 0 else row.story
        verdict = getattr(ledger, "verdicts", {}).get(survivor.body)
        if verdict is not None:
            row.verdict, row.verdict_target = verdict.verdict, verdict.target
    row.layers = kept


def build(repo: Path, module: str, depth: int) -> Model:
    """The census model on `kernel_shape.build`."""
    return model_of(repo, ks.build(module, depth, repo))


def model_of(repo: Path, built: tuple) -> Model:
    """The census model on `built`, a `kernel_shape.build` answer (shape, rows, signatures). Rows
    left out (`rows=False`) leave the words out and nothing else."""
    shape, rows, sigs = built
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
    tables = chain_tables(ledger)
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
            row.port_name = (port_name if kind in (PORT, DELETED) else name if kind == OVERRIDDEN_BELOW
                             else port_name or f"{name}Slot{row.slot}")
            if kind in (PORT, DELETED):
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
        restored = CHAIN_HAND.get(row.slot, ("", ""))[0]
        if restored:
            # The port holds the value retail's reference names, so the reference survives.
            row.ret_port, note = restored, ""
        if note:
            row.notes.append(f"returns {note}")
        for param in [p for p in row.params.split(",") if p.strip()]:
            lowered, param_note = lower_type(param, family)
            row.params_port.append(lowered)
            if param_note:
                row.notes.append(f"takes {param_note}")
        apply_verdict(row, ledger)
        split_layers(row, ledger, tables)
        close_layers(row, ledger)

    # A slot the chain now declares below the NPC line is inherited by every entity class; a
    # same-name member on one of them is a decision, not a default (story 0019/5 step 6).
    subclass_overrides: dict[str, set[str]] = {}
    subclass_names = chain_subclass_names(repo, subclass_overrides)
    for row in slots:
        if row.port_kind == OVERRIDDEN_BELOW:
            declaring = subclass_names.get(row.port_name, set())
            hiding = declaring - subclass_overrides.get(row.port_name, set())
            if not declaring or hiding:
                raise SystemExit(f"gen_kernel_shape: slot {row.slot} `{row.port_name}` is mapped "
                                 f"{OVERRIDDEN_BELOW}, but "
                                 + (f"{', '.join(sorted(hiding))} declare(s) it without `override`"
                                    if declaring else "no class below the NPC line declares it"))
            continue
        if not row.generated or row.slot in SLOT_PORT_MAP:
            continue
        if any(layer.owner not in LAYER_PORT.values() for layer in row.layers)                 and row.port_name in subclass_names:
            # A declaration that carries `override` is an override of the generated virtual, not a
            # hide: a suite-local or world class standing a retail class the maps never place
            # (0019/6: `FCornerChainTestCorner::GetNextTarget`, slot 172). Only a same-name member
            # WITHOUT `override` is the collision that needs a `SLOT_PORT_MAP` decision.
            declaring = subclass_names[row.port_name]
            hiding = declaring - subclass_overrides.get(row.port_name, set())
            if not hiding:
                row.notes.append("overridden by " + ", ".join(sorted(declaring)))
                continue
            row.notes.append("declared by " + ", ".join(sorted(hiding)))
            collisions.append(row)

    if collisions:
        print("gen_kernel_shape: the port's entity chain already declares these slot names; add a "
              "`SLOT_PORT_MAP` row for each (PORT names the method that IS the slot, SUFFIX says "
              "the collision is accidental):")
        for row in collisions:
            print(f"  {row.slot:4d}  {row.method or '(unnamed)'}  —  {row.declaration}"
                  + (f"  [{row.notes[-1]}]" if row.notes and row.notes[-1].startswith("declared by")
                     else ""))
        raise SystemExit(1)

    for row in branch:
        row.port_name = row.method or f"Slot{row.slot}"

    # A reading that lands on no generated body is stale: the slot was ported, mapped or closed.
    landed = {layer.body for row in slots for layer in row.layers if layer.default_why}
    stale = sorted(set(RETAIL_DEFAULTS) - landed)
    if stale:
        raise SystemExit("gen_kernel_shape: `RETAIL_DEFAULTS` rows that are no generated slot "
                         "body any more (drop them): " + ", ".join(f"0x{a}" for a in stale))

    # --- classes and the species overrides -------------------------------------------------------
    # Own bodies by primary-vtable diff against the direct base (the ledger's count, story 5
    # commit B; the name-prefix count missed unnamed and misfiled fills).
    own = ledger.own_bodies()
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
                row.closed = kl.body_closed(verdict.verdict, verdict.target)
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

    out.append("	// Each class's own slot bodies (census only: the port's answer is the class's")
    out.append("	// override on its C++ class). `Verdict` is the overlay's word for that body and")
    out.append("	// `Default` the literal it answers where the whole body is one `return`.")
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


# The three functions over the tables that are code rather than data (`ClassNamed` is the class
# tree's identity lookup, story 5 commit B). They are emitted with the
# tables so the digest walks the arrays it was taken from: the stored constant catches a bad
# regeneration, and this walk catches a hand-edit of a row the constant was not regenerated for.
CENSUS_TAIL = """
const FElysiumNpcClass* ClassNamed(const TCHAR* Name)
{
	if (Name == nullptr)
	{
		return nullptr;
	}
	for (const FElysiumNpcClass& Row : Classes())
	{
		if (FCString::Strcmp(Row.Name, Name) == 0)
		{
			return &Row;
		}
	}
	return nullptr;
}

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


def _layer_rows(model: Model, owner: str) -> list[Slot]:
    """The emission rows of one layer's slot file, in slot order."""
    return [layer for row in model.slots for layer in row.layers if layer.owner == owner]


def render_slots_inl(model: Model, module: str, owner: str) -> str:
    rows = _layer_rows(model, owner)
    surface = SLOT_SURFACES[owner]
    overrides = len([r for r in rows if r.override])
    retail = sorted({r.retail for r in rows})
    counts = (f"{len(rows)} generated slot rows of `{owner}` ({', '.join(retail) or 'none'}): "
              f"{len(rows) - overrides} it introduces and {overrides} it overrides with a body of "
              "its own.")
    out = _header(module, model.meta, counts)
    cpp = surface.cpp[-1]
    out += [
        "//",
        f"// This file is included INSIDE `class {owner}` (`{surface.header}`). It is not",
        "// a header: it has no include guard and declares nothing of its own. One declaration per",
        "// slot, in slot order, with the retail declaration in the comment and the port's lowered",
        "// signature in the code.",
        "//",
        "// Each slot is declared by the port class of the retail class that introduces it, with that",
        "// class's own body, and overridden by every port class whose retail table holds another",
        "// body there (story 0019/5 step 6). The stub names the retail class that owns its body.",
        "//",
        "// A body lands on one of these in 29c/29d/29e. Until then the definition in",
        f"// `{cpp}` tallies `elysium.stubs` with the retail address — except",
        "// where the verdict overlay records that retail's whole body is one literal, which the",
        "// generator emits, or that the body is written by hand in the substrate, in which case no",
        "// definition is generated at all and the linker is what checks the claim.",
    ]
    if owner == LAYER_PORT[BASE_TABLE]:
        out += [
            "//",
            "// The slots NOT declared anywhere in the generated surface are the ones the port already",
            "// runs, or a dead stub deleted in 0019/5 step 6. They are listed rather than left",
            "// implicit, because \"this slot has a body somewhere else\" is exactly the fact a reader",
            "// needs, and the census carries the same pairing as data:",
        ]
        for row in model.slots:
            if row.port_kind == PORT:
                out += _comment(f"  slot {row.slot:>3}  {row.address}  {row.port_name} — "
                                f"{row.port_why}")
            elif row.port_kind == DELETED:
                out += _comment(f"  slot {row.slot:>3}  {row.address}  deleted — {row.port_why}")
            elif row.port_kind == CLOSED:
                out += _comment(f"  slot {row.slot:>3}  {row.address}  closed — {row.port_why}")
    out.append("")
    for row in rows:
        out += _slot_comment(row)
        if row.override:
            out.append(f"\t{row.port_declaration()} override;")
        else:
            out.append(f"\tvirtual {row.port_declaration()};")
    # An override of one overload hides the inherited others (C++ name lookup stops at the first
    # scope that declares the name), so a name an ancestor declares more than once keeps its set
    # visible here.
    ancestors = PORT_CHAIN[:PORT_CHAIN.index(owner)]
    inherited = collections.Counter(r.port_name for cls in ancestors for r in _layer_rows(model, cls)
                                    if not r.override)
    hidden = sorted({r.port_name for r in rows if r.override and inherited[r.port_name] > 1})
    if hidden:
        out.append("")
        out.append("\t// The inherited overloads of an overridden name stay visible.")
        out += [f"\tusing {PORT_PARENT[owner]}::{name};" for name in hidden]
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


def slot_rows_accessor(owner: str) -> str:
    """The census accessor of one port class's slot table: `FElysiumEntity` -> `EntitySlotRows`."""
    return owner.removeprefix("FElysium") + "SlotRows"


def _signature(row: Slot) -> str:
    """The row's port signature as a function type, the way the `.inl` declares it."""
    return f"{row.ret_port}({', '.join(row.params_port)}){' const' if row.const else ''}"


def _slot_table_row(row: Slot, owner: str) -> list[str]:
    """One row of `owner`'s slot table.

    `bDeclaredHere` is a compile-time answer: `TDeclaredOn` deduces the class of the member pointer
    `&Owner::Method` for exactly the row's signature, and a slot declared on a base instead would
    deduce the base. A default row carries the probe that calls the virtual on a receiver typed to
    the owner; a stub or hand row carries none (a stub fired here would tally, and a hand body
    reaches state a bare receiver does not have). A closed introducer with no literal is `Closed`
    (0019/6): it answers the value-initialised default and carries no probe.
    """
    kind = ("Default" if row.default else "Hand" if row.hand else "Closed" if row.closed
            else "Stub")
    declared = (f"ElysiumNpcKernelShape::TDeclaredOn<{owner}, {_signature(row)}>::Test("
                f"&{owner}::{row.port_name})")
    if row.default:
        _, expression, value = default_body(row)
        locals_, args = _probe_arguments(row)
        call = f"{row.port_name}({args})"
        prologue = " ".join(locals_) + (" " if locals_ else "")
        if row.default == "void":
            body = f"{prologue}Receiver.{call}; return 0;"
        else:
            body = f"{prologue}return {expression.format(call=call)};"
        invoke = f"[]({owner}& Receiver) -> int64 {{ {body} }}"
        literal, void = row.default, row.default == "void"
    else:
        invoke, literal, value, void = "nullptr", "", 0, False
    return _row([str(row.slot), _literal(row.address), _literal(row.retail), _literal(row.port_name),
                 f"EElysiumNpcSlotBody::{kind}", _literal(literal), str(value),
                 "true" if void else "false", "true" if row.override else "false", declared, invoke],
                "\t\t\t")


def render_slots_cpp(model: Model, module: str, owner: str) -> str:
    surface = SLOT_SURFACES[owner]
    generated = _layer_rows(model, owner)
    stubbed = [r for r in generated if r.stubbed]
    layer_defaults = [r for r in generated if r.default]
    hand = [r for r in generated if r.hand]
    stories = collections.Counter(r.story or "unassigned" for r in stubbed)
    counts = (f"{len(generated)} generated slot bodies of `{owner}`: {len(layer_defaults)} carry "
              f"retail's one-constant default (story 29c's verdicts, the L0 re-check's "
              f"`RETAIL_DEFAULTS`), {len(hand)} are defined by hand in the "
              f"substrate, and {len(stubbed)} are still stubs"
              + (" — " + ", ".join(f"{count} {story}" for story, count in sorted(stories.items()))
                 if stories else "") + ".")
    silent = [r for r in generated if r.closed and not r.default and not r.hand]
    if silent:
        counts += (f" {len(silent)} are closed (0019/6) and answer the value-initialised default "
                   "without tallying.")
    fire = surface.fire
    out = _header(module, model.meta, counts)
    out += [
        "//",
        "// A stub body says one thing: this slot has no port implementation yet. The tally row",
        "// carries the retail class that owns the body, its address and the story that owns it, so",
        "// `elysium.stubs` joins `docs/vtmb/npc-kernel/functions.md` by address.",
        "//",
        "// A body with a `default:` verdict says something stronger: retail's whole body at that",
        "// slot is `return <literal>;`, so the port answers the same literal and stops tallying.",
        "// The literal is a recovered fact, not a written behaviour, and the slot table at the end",
        "// of this file is what `Elysium.Substrate.NpcKernelSlots.Defaults` calls every one of them",
        "// through, on a receiver typed to this class.",
        "",
        f'#include "{surface.header}"',
        "",
    ]
    out += ['#include "ElysiumStub.h"', '#include "Substrate/ElysiumNpcKernelShape.h"', ""]
    out += [
        "namespace",
        "{",
        "\t// One shape for every slot stub: the surface is the retail class and method, which is",
        "\t// what the ledger joins on, and never an instance name. Unit-prefixed because the module",
        "\t// builds adaptive-unity and this anonymous namespace is regularly merged with others.",
        f"\tvoid {fire}(const TCHAR* Surface, const TCHAR* Address, const TCHAR* Story,",
        "\t\tconst FString& Receiver)",
        "\t{",
        "\t\tElysiumStub::FSurface Row;",
        "\t\tRow.Kind = TEXT(\"slot\");",
        "\t\tRow.Surface = Surface;",
        "\t\tRow.Address = Address;",
        "\t\tRow.Story = Story;",
        "\t\tElysiumStub::Fired(Row, Receiver, FString(), TEXT(\"the NPC kernel\"));",
        "\t}",
        "}",
        "",
    ]
    for row in generated:
        if row.hand:
            out += _slot_comment(row, "")
            why = (f"verdict `{row.verdict}`: the body is `{row.hand}`, written by hand in the "
                   "substrate." if hand_target(row.verdict_target) else
                   f"the body is `{row.hand}`, written by hand in the substrate: "
                   f"{CHAIN_HAND.get(row.slot, ('', 'see spec.md story 5, the generated slot bodies'))[1]}.")
            out += _comment(f"{why} Declared here, defined there.", "")
            out.append("")
            continue
        out += _slot_comment(row, "")
        if row.default:
            statement, _, _ = default_body(row)
            if row.default_why:
                out += _comment(f"retail default (L0.tooling.default-stubs): {row.default_why}", "")
            else:
                out += _comment(f"verdict `{row.verdict}`: retail's whole body is "
                                f"`{'return;' if row.default == 'void' else f'return {row.default};'}`",
                                "")
        out += _wrapped(row.port_declaration(f"{owner}::"), "")
        out.append("{")
        if row.default:
            if statement:
                out.append(f"\t{statement}")
        elif row.closed:
            out += _comment(f"verdict `{row.verdict}`, closed at `{row.verdict_target}` (0019/6): "
                            "nothing observes this body, but the slot is still reached, so it "
                            "answers the value-initialised default and tallies nothing.", "\t")
            tail = _default_return(row.ret_port)
            if tail:
                out.append(tail)
        else:
            out += _wrapped(f"{fire}({_literal(f'{row.retail}::{row.port_name}')}, "
                            f"{_literal(row.address)}, {_literal(row.story)}, DebugString());", "\t")
            tail = _default_return(row.ret_port)
            if tail:
                out.append(tail)
        out.append("}")
        out.append("")

    accessor = slot_rows_accessor(owner)
    row_type = f"TElysiumNpcSlotRow<{owner}>"
    out += [
        "namespace ElysiumNpcKernelShape",
        "{",
        "\tnamespace",
        "\t{",
        f"\t\t// Every generated slot row of `{owner}`, in slot order: the census the class answers",
        "\t\t// for (`Elysium.Substrate.NpcKernelShape.SlotOwners`) and, for each recovered default,",
        "\t\t// the probe `Elysium.Substrate.NpcKernelSlots.Defaults` calls on a receiver of this",
        "\t\t// class. `bDeclaredHere` is decided by the compiler, not written.",
    ]
    if generated:
        out += [f"\t\tconst {row_type} G{accessor}[] =", "\t\t{"]
        for row in generated:
            out += _slot_table_row(row, owner)
        out.append("\t\t};")
    else:
        out.append("\t\t// This class declares no generated slot.")
    out += [
        "\t}",
        "",
        f"\tTArrayView<const {row_type}> {accessor}()",
        "\t{",
        (f"\t\treturn MakeArrayView(G{accessor});" if generated
         else f"\t\treturn TArrayView<const {row_type}>();"),
        "\t}",
        "}",
    ]
    return "\n".join(out) + "\n"


# --- Driver -------------------------------------------------------------------------------------


# --- The override census (0019 story 5 commit B) ------------------------------------------------
#
# One row per live (class, slot) own body the port carries as an override, each with a compile-time
# proof: `TDeclaredOn<DeclaringClass, Signature>::Test(&DeclaringClass::Method)` is true only when
# the method with that exact signature is declared on that class itself, and `std::is_base_of_v`
# holds the class the retail factory builds to the declaring class (an inherited override is carried
# where its owner declares it). A name that exists nowhere stops the build; an override deleted while
# a base keeps the name reads false and `NpcKernelShape.Overrides` fails; `--check` stops a
# stale table. `kernel_shape --unported` is the other half: the rows the port does not carry yet.

OVERRIDE_CENSUS_OUTPUT = ("Source", "ElysiumUE", "Private", "Tests", "ElysiumNpcKernelOverrideCensus.cpp")


def _port_header(port: str) -> str:
    return f"Substrate/{port[1:]}.h"


def render_override_census(model: Model, module: str, repo: Path) -> str:
    ported, unported = ks.override_rows(repo, model)
    live_rows = len(ported) + len([r for r in unported if r[4] == "no-override"])
    missing = [r for r in ported if r["signature"] is None]
    if missing:
        raise SystemExit("gen_kernel_shape: no declaration reads for the ported override(s) "
                         + ", ".join(f"{r['cls']}#{r['slot']} {r['declared']}::{r['method']}"
                                     for r in missing))
    ported.sort(key=lambda r: (r["cls"], r["slot"], r["address"]))
    headers = sorted({_port_header(r["port"]) for r in ported} | {_port_header(r["declared"]) for r in ported})
    out = _header(module, model.meta,
                  f"{len(ported)} live own bodies the port carries as overrides, of {live_rows} live "
                  f"(class, slot) own-body rows on live classes; `kernel_shape --unported` lists the "
                  f"other {live_rows - len(ported)}.")
    out += ["//",
            "// The override census (0019 story 5 commit B): each row's two booleans are compile-time",
            "// proofs -- the method with the recorded signature is declared on the declaring class",
            "// itself, and the class the retail factory builds derives from it. Read by",
            "// `Elysium.Substrate.NpcKernelShape.Overrides`.",
            "",
            '#include "Tests/ElysiumNpcKernelOverrideCensus.h"',
            "",
            "#if WITH_DEV_AUTOMATION_TESTS",
            ""]
    out += [f'#include "{h}"' for h in headers]
    out += ["",
            "#include <type_traits>",
            "",
            "namespace ElysiumNpcKernelOverrideCensus",
            "{",
            "namespace",
            "{",
            "\tusing ElysiumNpcKernelShape::TDeclaredOn;",
            "",
            "\tconst FElysiumNpcPortedOverride GRows[] =",
            "\t{"]
    for r in ported:
        ret, params, const = r["signature"]
        sig = f"{ret}({params})" + (" const" if const else "")
        proof = f"TDeclaredOn<{r['declared']}, {sig}>::Test(&{r['declared']}::{r['method']})"
        inherits = f"std::is_base_of_v<{r['declared']}, {r['port']}>"
        out += _row([_literal(r["cls"]), str(r["slot"]), _literal(r["address"]), _literal(r["verdict"]),
                     _literal(r["port"]), _literal(r["declared"]), _literal(r["method"]),
                     f"&{r['port']}::StaticRetailClass", proof, inherits])
    out += ["\t};",
            "}",
            "",
            "TArrayView<const FElysiumNpcPortedOverride> Rows()",
            "{",
            "\treturn MakeArrayView(GRows);",
            "}",
            "",
            "int32 LiveRows()",
            "{",
            f"\treturn {live_rows};",
            "}",
            "}",
            "",
            "#endif  // WITH_DEV_AUTOMATION_TESTS"]
    return "\n".join(out) + "\n"


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
    # Unchanged text is not rewritten. These are C++ sources, headers and `.inl`s included inside
    # the kernel's class bodies: a rewrite with the same bytes still stamps a new time on them, and
    # the next build recompiles the whole module (spec 0002 T6: 3 m 22 s after a `research kernel`
    # that changed nothing).
    if output.is_file():
        with open(output, encoding="utf-8", newline="") as handle:
            if handle.read().replace("\r\n", "\n") == text:
                print(f"unchanged {output}")
                return 0
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
    print(f"  closed    {len([r for r in model.slots if r.port_kind == CLOSED]):5d}  "
          "every body closed and nothing reaches it: no virtual (0019/6)")
    silent = [layer for r in model.slots for layer in r.layers if layer.closed]
    print(f"    reached {len(silent):5d}  closed layer rows still declared "
          f"({len([layer for layer in silent if layer.default])} answer retail's literal)")
    owed = [r for r in model.slots if r.port_kind == PORT and r.closed]
    if owed:
        print("  PORT rows over a closed body (each needs a `SLOT_PORT_MAP` DELETED row): "
              + ", ".join(f"{r.slot} {r.address} {r.port_name}" for r in owed))
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
    print(f"overrides:  {len(model.overrides):5d}  species slot bodies "
          f"({len([r for r in model.overrides if r.closed])} closed: no override owed)")
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
    if args.check and not args.report:
        return kl.kernel_cache.stamped("gen_kernel_shape", vars(args), repo, lambda: _main(args, repo))
    return _main(args, repo)


def _main(args: argparse.Namespace, repo: Path) -> int:
    model = build(repo, args.module, args.depth)
    report(model)
    if args.report:
        return 0

    status = 0
    status |= _emit(repo.joinpath(*CENSUS_OUTPUT), render_census(model, args.module), args.check)
    status |= _emit(repo.joinpath(*OVERRIDE_CENSUS_OUTPUT), render_override_census(model, args.module, repo),
                    args.check)
    for owner, surface in SLOT_SURFACES.items():
        status |= _emit(repo.joinpath(*surface.inl), render_slots_inl(model, args.module, owner),
                        args.check)
        status |= _emit(repo.joinpath(*surface.cpp), render_slots_cpp(model, args.module, owner),
                        args.check)
    return status


if __name__ == "__main__":
    sys.exit(main())
