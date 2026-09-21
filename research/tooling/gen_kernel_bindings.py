# -*- coding: utf-8 -*-
"""Emit the datamap-backed field/input bindings of the port's NPC-family classes.

Owner-run archaeology, not part of any build.  The datamap replay
(``$ELYSIUM_WORK_ROOT/research/ghidra/types/datamap_records-vampire.dll.json``)
records what retail's own registration wrote for the classes this port stands:
``CAI_BaseNPC`` and ``CAI_BaseNPCTroika`` (the shared ``npc_*`` leaf),
``CNPCMaker`` (the ``npc_maker`` classnames) and its ``CNPCMaker_Zombie`` rows,
``CAI_InterestingPlace`` (retail's misspelled ``intersting_place``),
``CAI_InterestingPlaceConverstation`` and ``CAI_Hint`` (``ai_hint``): every
member with its offset, type and flags, and — where retail had one — the
external name a map, a script or an input used.  This generator transcribes
those rows into committed source:

* ``ElysiumNpcKernelBindings.h/.cpp`` — one ``Add…Fields`` per binding class
  (one ``ElysiumAddClassField`` row per field the replay names and the class's
  member map binds), the per-class ``Outputs``/``InputFuncs`` name tables and
  ``Counts`` (the four totals, as literals).
* ``AiInfra/ElysiumInfraKeyfields.h`` — the reflected keyfield structs the
  baked infrastructure actors hold (0018 story 2): one ``UPROPERTY`` per keyed
  row of each struct's tables, named by its external.

The classification is fixed.  A row with ``OUTPUT`` is an output name.  A row
with ``FUNCTIONTABLE`` is skipped.  An ``INPUT`` row whose type is ``void`` or
whose offset is 0 is an input *handler*, not a field.  A row with an external
name is a field row, bound when the class's member map carries a member at that
offset and left as an ``UNBOUND`` comment when it does not — the comment is the
recorded gap, the same posture the shape map's own ``_ABSENT`` rows take.  A row
with NO external name and the ``SAVE`` flag is retail's persistence and nothing
else, and lands in the NPC's ``AddNpcSaveFields`` under its retail member name.

The NPC's entity chain is four more binding classes (pass B, 2026-09-21).
``ReadKeyField`` (``0x100acab0``) walks ``CAI_BaseNPCTroika`` up to
``CBaseEntity``, and the port's registry carries the same walk through
``FElysiumClassDesc::BaseName``, so ``CBaseEntity``, ``CBaseToggle``,
``CBaseAnimating`` and ``CBaseCombatCharacter`` each get their own
``Add…Fields`` registered on the matching chain node.  ``CBaseFlex`` and
``CBaseAnimatingOverlay`` name no external and get none.  ``CBaseCombatCharacter``
carries the character sheet: retail gives every ELEMENT of its four trait arrays
its own datamap row, so the 148 trait names are recovered data, emitted as
``ElysiumAddSheetField`` rows against the port's compiled slot table.

Member resolution is per class.  The NPC's two tables resolve through the shape
map (``ElysiumNpcKernelShapeMap.cpp``), which the census checks; a row whose
shape-map member belongs to one of the port's component structs binds through
``ElysiumAddClassFieldVia`` and ``NPC_COMPONENT_PATHS``.  The chain classes
resolve through ``CHAIN_MEMBER_MAPS``, and a chain row in neither that nor
``CHAIN_UNBOUND``/``BOUND_ELSEWHERE`` FAILS the generator — that is the check
that every retail ``KEY`` on a stood class has a member or a stated reason.
``CNPCMaker`` and ``CAI_InterestingPlace`` resolve through ``CLASS_MEMBER_MAPS``.
Everywhere, the compiler is the second check — a wrong member name fails
``uv run elysium build``.

Flag mapping, fixed: ``SAVE`` -> ``EElysiumField::Save``; ``INPUT`` ->
``EElysiumField::Key``; neither -> ``EElysiumField::None``.  ``KEY`` alone
adds no flag, and that is retail: ``ReadKeyField``'s gate is ``KEY|OUTPUT``
(a read), ``AcceptInput``'s field write needs ``INPUT`` *and* ``KEY``
(``0x100abc90``), and ``Entity.__setattr__``'s gate is ``INPUT`` alone
(``docs/vtmb/python_bridge.md``).  The port's ``bKeyable`` is that write gate.

A bound row the binding API cannot express fails the build through
``ElysiumAddClassFieldVia``'s ``static_assert``; such a row moves to the
``UNBOUND`` / ``SAVE_UNBOUND`` comments rather than by editing anything the
generator emits.

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
KEYFIELDS_H = ("Source", "ElysiumUE", "Private", "AiInfra", "ElysiumInfraKeyfields.h")

# The binding classes, each over the retail datamap tables whose rows it registers. The NPC's
# two tables are one binding class because the port stands one shared leaf for them; the maker
# and the place stand their own.
BINDING_CLASSES = (
    ("BaseEntity", ("CBaseEntity",)),
    ("Toggle", ("CBaseToggle",)),
    ("Animating", ("CBaseAnimating",)),
    ("CombatCharacter", ("CBaseCombatCharacter",)),
    ("Npc", ("CAI_BaseNPC", "CAI_BaseNPCTroika")),
    ("NpcMaker", ("CNPCMaker",)),
    ("InterestingPlace", ("CAI_InterestingPlace",)),
    ("Hint", ("CAI_Hint",)),
    ("ConversationPlace", ("CAI_InterestingPlaceConverstation",)),
    ("NpcMakerZombie", ("CNPCMaker_Zombie",)),
)

# The port class each binding class registers against. The first five are the NPC's entity chain:
# retail's `ReadKeyField` walks `CAI_BaseNPCTroika` -> `CAI_BaseNPC` -> `CBaseCombatCharacter` ->
# `CBaseFlex` -> `CBaseAnimatingOverlay` -> `CBaseAnimating` -> `CBaseToggle` -> `CBaseEntity`, and
# the port's registry carries the same walk through `FElysiumClassDesc::BaseName`. `CBaseFlex` and
# `CBaseAnimatingOverlay` name no external at all, so neither gets a binding class.
PORT_CLASS = {
    "BaseEntity": "FElysiumEntity",
    "Toggle": "FElysiumAnimating",
    "Animating": "FElysiumAnimating",
    "CombatCharacter": "FElysiumCombatCharacter",
    "Npc": "FElysiumNpc",
}

# The NPC's entity chain, keyed by retail class then offset, as (port type, member). The port's
# chain is the same shape with one stated divergence: **there is no `CBaseToggle` node.** Retail
# derives `CBaseCombatCharacter` from `CBaseToggle`, so every character inherits the mover's nine
# keys; this port keeps mover data on `FElysiumMoverBase`, a sibling of the character chain, so
# those nine rows are recorded gaps on the node that stands where retail's toggle does.
#
# Written by hand from the members the port already declares, exactly as `CLASS_MEMBER_MAPS` below
# is: the compiler is the check -- a wrong member name fails the build, never a silent wrong
# binding. An offset in neither this map nor `CHAIN_UNBOUND` fails the generator.
CHAIN_MEMBER_MAPS: dict[str, dict[int, tuple[str, str]]] = {
    "CBaseEntity": {
        0x00C0: ("FElysiumEntity", "SoundGroup"),
        0x00E0: ("FElysiumEntity", "bStartHidden"),
        0x00FC: ("FElysiumEntity", "bNpcTransparent"),
        0x00FD: ("FElysiumEntity", "bBlocksTraces"),
        0x0124: ("FElysiumEntity", "ParentName"),
        0x0128: ("FElysiumEntity", "DialogName"),
        0x0164: ("FElysiumEntity", "AuthoredSpeed"),
        0x017C: ("FElysiumEntity", "NextThink"),
        0x0204: ("FElysiumEntity", "SpawnFlags"),
        0x0208: ("FElysiumEntity", "MaxHealth"),
        0x020C: ("FElysiumEntity", "Target"),
        0x0210: ("FElysiumEntity", "Health"),
        0x0214: ("FElysiumEntity", "UseFilterName"),
        0x021C: ("FElysiumEntity", "DamageFilterName"),
        0x026C: ("FElysiumEntity", "TargetName"),
        0x0374: ("FElysiumEntity", "UseScript"),
        0x0388: ("FElysiumEntity", "Model"),
        0x03B0: ("FElysiumEntity", "BaseVelocity"),
        0x03C8: ("FElysiumEntity", "AngularVelocity"),
        0x03D4: ("FElysiumEntity", "Velocity"),
        0x03E0: ("FElysiumEntity", "WaterLevel"),
        0x03E4: ("FElysiumEntity", "WaterType"),
        0x03EC: ("FElysiumEntity", "Gravity"),
        0x03F0: ("FElysiumEntity", "Friction"),
        0x03F4: ("FElysiumEntity", "LocalTime"),
        0x0428: ("FElysiumEntity", "Angles"),
        0x0434: ("FElysiumEntity", "Flags"),
    },
    "CBaseToggle": {},
    "CBaseAnimating": {
        0x0670: ("FElysiumAnimating", "Skin"),
    },
    "CBaseCombatCharacter": {
        0x13D8: ("FElysiumCombatCharacter", "Money"),
    },
}

# The one reason the nine `CBaseToggle` rows share, expanded from the `TOGGLE` marker below.
TOGGLE_REASON = (
    "retail derives `CBaseCombatCharacter` from `CBaseToggle`, so every character inherits the "
    "mover's keys; this port keeps mover data on `FElysiumMoverBase`, a sibling of the character "
    "chain, so there is no member on this node")

# Why a chain row has no binding. Never "not got to it": each names the retail word and says what
# this port does instead. A row here is a recorded gap `--check` accepts; a row in neither map is
# a fault.
CHAIN_UNBOUND: dict[tuple[str, int], str] = {
    # --- CBaseEntity ---
    ("CBaseEntity", 0x011C):
        "the def's identity: `FElysiumEntityDef::Classname` is hoisted out of the keys and the "
        "registry keys the descriptor on it, so there is no member a write could land on",
    ("CBaseEntity", 0x0120):
        "`m_iGlobalname` is retail's FTYPEDESC_GLOBAL carry across a `trigger_changelevel`; this "
        "port carries state across a level change in the map snapshot, and no shipped map "
        "authors the key",
    ("CBaseEntity", 0x012C):
        "`m_bBlocked` is the blocked-by-a-mover latch `CBaseToggle`'s movers set; this port's "
        "movers carry their own, and no shipped map authors the key",
    ("CBaseEntity", 0x0168):
        "a render word: `m_nRenderFX`'s effect table is Source's, and Unreal renders",
    ("CBaseEntity", 0x016C):
        "a render word: `m_nRenderMode`'s blend modes are Source's, and Unreal renders",
    ("CBaseEntity", 0x0184):
        "`m_vecViewOffset` is the eye height every sight test starts from; this port takes an "
        "NPC's eye from its body (0018 story 6), and no shipped map authors the key",
    ("CBaseEntity", 0x0190):
        "`m_vecMoveDir` is the mover's authored direction, which this port keeps on "
        "`FElysiumMoverBase`; no shipped map authors it on a character",
    ("CBaseEntity", 0x019C):
        "a render word: `m_fEffects` is Source's EF_ bit field, and Unreal renders",
    ("CBaseEntity", 0x01A0):
        "a render word, and a `color32`: the binding API marshals no colour type because nothing "
        "in the substrate reads one -- Unreal renders",
    ("CBaseEntity", 0x01A4):
        "`m_nModelIndex` is the engine's precache slot for `model`, an index into Source's own "
        "model table; this port resolves a model by name",
    ("CBaseEntity", 0x038C):
        "`m_vecSize` is the brush extent Source derives at spawn; this port takes it from the "
        "baked hulls (`FElysiumEntityDef::Hulls`), and no shipped map authors the key",
    # --- CBaseToggle: the nine the port's chain does not carry ---
    ("CBaseToggle", 0x045C): "TOGGLE",
    ("CBaseToggle", 0x0480): "TOGGLE",
    ("CBaseToggle", 0x0488): "TOGGLE",
    ("CBaseToggle", 0x048C): "TOGGLE",
    ("CBaseToggle", 0x0490): "TOGGLE",
    ("CBaseToggle", 0x04A0): "TOGGLE",
    ("CBaseToggle", 0x0500): "TOGGLE",
    ("CBaseToggle", 0x0504): "TOGGLE",
    ("CBaseToggle", 0x0538): "TOGGLE",
    # --- CBaseAnimating ---
    ("CBaseAnimating", 0x05A4):
        "`m_flEffectStartTime` stamps the render effect `m_fEffects` drives, which Unreal renders",
    ("CBaseAnimating", 0x0678):
        "`m_flSkinCrossfadeTime` is the dissolve Source runs between two `skin` families; this "
        "port swaps the material family outright",
    ("CBaseAnimating", 0x067C):
        "`m_nBody` is Source's bodygroup word; this port composes a character from the model "
        "record's own parts",
    ("CBaseAnimating", 0x0680):
        "`m_nHitboxSet` selects a studiomdl hitbox set; this port traces against the Unreal "
        "skeletal body's physics asset",
    ("CBaseAnimating", 0x0684):
        "`m_flModelScale` is the studiomdl render scale, which the baked mesh carries",
    ("CBaseAnimating", 0x0688):
        "`m_nTopColor` is VtMB's two-tone model tint, which Unreal renders",
    ("CBaseAnimating", 0x068C):
        "`m_nBottomColor` is VtMB's two-tone model tint, which Unreal renders",
    ("CBaseAnimating", 0x06F0):
        "`m_nSequence` is the studiomdl sequence index; this port plays a clip by name through "
        "the animation seam (`IElysiumAnimatable::PlayAnimClip`)",
    ("CBaseAnimating", 0x06F4):
        "`m_flPlaybackRate` is half the studiomdl playback cursor; Unreal's animation graph owns "
        "the port's",
    ("CBaseAnimating", 0x06F8):
        "`m_flCycle` is the studiomdl playback cursor; Unreal's animation graph owns the port's",
    # --- CBaseCombatCharacter ---
    ("CBaseCombatCharacter", 0x00DC):
        "`m_impactEnergyScale` scales the damage a physics impact deals; this port has no "
        "physics-impact damage path yet, so there is nothing the word would reach",
    ("CBaseCombatCharacter", 0x10AC):
        "`m_sTeamName` is Source's team string; VtMB decides hostility through the relationship "
        "table and the disposition, and the six maps that author the key have no reader",
    ("CBaseCombatCharacter", 0x13A0):
        "`m_iVHistoryID` is bound on `player` alone: this port stores the History index on "
        "`FElysiumPlayerRecord`, which no NPC has",
    ("CBaseCombatCharacter", 0x1468):
        "`m_iCurVReaction` is the reaction row a character is currently playing; this port "
        "carries the live reaction on `FElysiumCombatCharacter`'s reaction state, not as an index",
    ("CBaseCombatCharacter", 0x1584):
        "`m_RelationshipString` is the authored relationship line; this port parses it into "
        "`FElysiumNpc::Relationships` through the `SetRelationship` input, so the string itself "
        "has no member",
    ("CBaseCombatCharacter", 0x158C):
        "`m_LootableType` selects the corpse's loot table; this port's inventory has no "
        "loot-table seam yet",
}

# Rows the port binds on ANOTHER binding class, because it stores a word retail puts on the chain
# on a leaf instead. The row is a recorded gap here and a binding there, so the key still resolves
# through the registry's chain walk for the classes that have the member.
BOUND_ELSEWHERE: dict[tuple[str, int], str] = {
    ("CAI_BaseNPCTroika", 0x6558): "Animating (`FElysiumAnimating::Disposition`)",
    ("CBaseCombatCharacter", 0x10E4): "Npc (`FElysiumNpc::StatTemplate`)",
    ("CBaseCombatCharacter", 0x10E8): "Npc (`FElysiumNpc::FloatSoundFrequency`)",
    ("CBaseCombatCharacter", 0x1589): "Npc (`FElysiumNpc::bCantDropWeapons`)",
}

# A `CBaseCombatCharacter` sheet row: retail gives every ELEMENT of the four trait arrays its own
# datamap row, named by the slot it holds. `(container, base-or-current)` -> the array's first
# offset and its element count; a row's slot index is its distance from that first offset.
SHEET_ROW_RE = re.compile(r"m_iV(Attributes|Abilities|Disciplines|ActiveDisciplines)(Base|Current)\[")
SHEET_ARRAY_BASE = {
    ("Attributes", "Base"): (0x10F0, 35),
    ("Attributes", "Current"): (0x117C, 35),
    ("Abilities", "Base"): (0x1210, 13),
    ("Abilities", "Current"): (0x1244, 13),
    ("Disciplines", "Base"): (0x1280, 13),
    ("Disciplines", "Current"): (0x12C4, 13),
    ("ActiveDisciplines", "Base"): (0x1310, 13),
    ("ActiveDisciplines", "Current"): (0x1354, 13),
}

# The component structs this port splits the NPC object into, and the path from `FElysiumNpc` to
# each. Retail holds `CAI_BaseNPCTroika` as one flat object, so a datamap row reaches any word by
# one offset; this port owns the same words on separate structs, so a row whose shape-map member
# belongs to one binds through `ElysiumAddClassFieldVia` and this path rather than through a
# pointer-to-member. `FElysiumNpcMemory` and `FElysiumNpcPerception` are two levels down because
# `FElysiumNpcSenses` owns them, exactly as retail's `CAI_Senses` owns its.
NPC_COMPONENT_PATHS: dict[str, str] = {
    "FElysiumNpcScheduleHost": "ScheduleHost",
    "FElysiumNpcMind": "Mind",
    "FElysiumNpcSenses": "Senses",
    "FElysiumNpcPerception": "Senses.Perception",
    "FElysiumNpcMemory": "Senses.Memory",
    "FElysiumNpcEnemyMemory": "EnemyMemory",
    "FElysiumNpcWitness": "Witness",
    "FElysiumNpcCognition": "Cognition",
    "FElysiumNpcDialogue": "Dialogue",
    "FElysiumNpcFlags": "NpcFlags",
    "FElysiumScheduleState": "Schedule",
    "FElysiumStanceState": "Stance",
    "FElysiumStanceClips": "StanceClips",
    "FElysiumNpcCombatSelector": "CombatSelector",
}

# Rows the generator must NOT emit even though the shape map binds a member, with the reason. One
# row: `interesting_place_groups`' sibling `hint_groups` parses on the write the way retail's
# `0x102989e0` does, so the port owns the pair as one hand accessor and a generated plain binding
# would silently drop the parse.
HAND_OWNED_MEMBERS: dict[int, str] = {
    0x62D8: "hand-owned by its parse-on-write accessor "
            "(`FElysiumNpc::SetInterestingPlaceGroups`)",
    0x62E0: "hand-owned by its parse-on-write accessor (`FElysiumNpc::SetHintGroups`)",
}

# --- The SAVE walk -----------------------------------------------------------------------------
#
# A datamap row with no external name and the `SAVE` flag is retail's persistence and nothing else:
# `ReadKeyField` cannot resolve it (its gate is `KEY|OUTPUT`, `0x100acab0`), `AcceptInput` cannot
# reach it (`INPUT`, `0x100abc90`) and no Python name addresses it. The port's equivalent is a
# registry field flagged `Save` and nothing else, registered under the RETAIL MEMBER NAME because
# there is no external to use -- which is the posture the port already took by hand for
# `m_iEnemySightings`, `m_CurrStance` and `m_flStanceTime`.
#
# Resolution is the shape map's, the same as a keyed NPC row's. Two tables qualify it.

# Rows whose shape-map member is an aggregate several retail rows share: the map records the owning
# struct, and the datamap records which word of it. The path is from `FElysiumNpc`, compiled, so a
# re-home is a build error. Every one is a component-struct member, which is what makes them this
# story's rather than a later one's.
SAVE_ROW_PATHS: dict[int, str] = {
    # The four seen-by-disposition slots, in `ESeen` order (HATE, FEAR, DISLIKE, NEMESIS).
    0x5B68: "Senses.Memory.LastSeen[0]",
    0x5B6C: "Senses.Memory.LastSeen[1]",
    0x5B70: "Senses.Memory.LastSeen[2]",
    0x5B74: "Senses.Memory.LastSeen[3]",
    # The two witness channels, `EChannel` order (Criminal 0, Supernatural 1). `+0x635c` is the
    # `CSecureType` half 0019 story 1 named dead inside a row that stays a rule: the port stores the
    # criminal level plain, so the scrambler has no port word and the level binds like its sibling.
    0x635C: "Witness.Channels[0].Level",
    0x6368: "Witness.Channels[1].Level",
    0x636C: "Witness.Channels[0].Processed",
    0x6370: "Witness.Channels[1].Processed",
    0x6374: "Witness.Channels[1].Location",
    0x6380: "Witness.Channels[0].Location",
    0x638C: "Witness.Channels[0].Offender",
    0x6390: "Witness.Channels[1].Offender",
    0x6398: "Witness.Channels[0].IgnoreUntil",
    0x639C: "Witness.Channels[1].IgnoreUntil",
    # The stance pair retail saves, and the two latches beside them that it does not -- all four
    # are words of one port struct.
    0x64C8: "Stance.Current",
    0x64E0: "Stance.bInFidget",
    0x64E1: "Stance.bInChange",
    0x64E4: "Stance.LastChangeTime",
    # `m_eForcedState` is one word of the pushed scripted-schedule order, which this port folds
    # into a struct (`FElysiumScriptedScheduleOrder`); the shape map binds the whole struct to the
    # offset, so the datamap row is what says which word of it.
    0x65CC: "ScriptedScheduleOrder.ForcedState",
}

# Why a SAVE row is not a generated field. Two kinds, each with its reason on the row:
# an aggregate whose port member carries its own typed serializer -- which is what retail's own
# nested datamap is, a `FIELD_EMBEDDED` row pointing at a second `datamap_t` -- and a word this
# port re-derives rather than stores.
SAVE_UNBOUND: dict[int, str] = {
    0x1A9C: "EMBEDDED",
    0x5C40: "EMBEDDED",
    0x5CDC: "EMBEDDED",
    0x60B0: "EMBEDDED",
    0x60DC: "EMBEDDED",
    0x6108: "EMBEDDED",
    0x6134: "EMBEDDED",
    0x6160: "EMBEDDED",
    0x618C: "EMBEDDED",
    0x61B8: "EMBEDDED",
    0x61E4: "EMBEDDED",
    0x6210: "EMBEDDED",
    0x6594: "EMBEDDED",
    0x5DDC: "`m_pHintNode` is a FIELD_CLASSPTR: retail saves the pointer through its own entity "
            "table, and this port holds the hint as a handle the navigator re-resolves",
    0x5DE8: "`m_pGoalEnt` is a FIELD_CLASSPTR, the same case as `m_pHintNode`",
    0x64D8: "`m_flMinBlink` is a word of the RESOLVED disposition row "
            "(`FElysiumDisposition::MinBlinkInterval`), which this port re-derives from the "
            "disposition table whenever the model or the disposition changes "
            "(`FElysiumNpc::StanceResolvedFor`); persisting it would restore a stale derivation",
    0x64DC: "`m_flMaxBlink` is the same resolved-row word as `m_flMinBlink`",
}

# What each of the shape map's four no-member forms means for the save walk. They are not one
# answer: a PRIVATE row HAS a port member and cannot be named from outside, a CHAIN row is carried
# by a class below the NPC and persists with that class, and only IMPLICIT and ABSENT mean there is
# no word to carry.
NO_MEMBER_SAVE_REASON = {
    "PRIVATE": "the port member exists but its owner keeps it private, so no compiled path reaches "
               "it; the owning struct's own `Serialize` carries it, which is where it stays until "
               "that struct exposes an accessor",
    "CHAIN": "the port carries this concern on the entity chain BELOW the NPC, and the class that "
             "owns the member is the class that persists it",
    "IMPLICIT": "the language or an existing mechanism provides the word, so there is no member to "
                "persist",
    "ABSENT": "this port declares no member for the word (the shape map's row says why), so there "
              "is nothing for the save walk to carry",
}

EMBEDDED_REASON = (
    "a FIELD_EMBEDDED row: retail's datamap points at a second `datamap_t` and recurses, and this "
    "port's matching member carries its own typed `Serialize`, which is the same shape")

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
    # `ai_hint` (0018 story 2): every keyfield the replay names, one port member each.
    "CAI_Hint": {
        0x454: ("FElysiumHint", "TargetAngleRange"),
        0x45C: ("FElysiumHint", "TargetDistMin"),
        0x460: ("FElysiumHint", "TargetDistMax"),
        0x464: ("FElysiumHint", "HintRating"),
        0x468: ("FElysiumHint", "InterestTargetName"),
        0x46C: ("FElysiumHint", "IpPercent"),
        0x470: ("FElysiumHint", "GroupId"),
        0x5D4: ("FElysiumHint", "UserData"),
        0x5DC: ("FElysiumHint", "HintType"),
        0x5E8: ("FElysiumHint", "Disabled"),
        0x5F0: ("FElysiumHint", "Group"),
    },
    # `intersting_place_conversation` (0018 story 2). Its base is `CBaseEntity`, not the place.
    "CAI_InterestingPlaceConverstation": {
        0x450: ("FElysiumConversationPlace", "InterestingPlaces"),
        0x454: ("FElysiumConversationPlace", "SoundLoop"),
        0x458: ("FElysiumConversationPlace", "SoundOnce"),
        0x45C: ("FElysiumConversationPlace", "bEnabled"),
        0x464: ("FElysiumConversationPlace", "PlayerDist"),
        0x468: ("FElysiumConversationPlace", "AudibleDist"),
        0x50C: ("FElysiumConversationPlace", "MinTime"),
        0x510: ("FElysiumConversationPlace", "MaxTime"),
        0x514: ("FElysiumConversationPlace", "bTurnTowardsTalker"),
        0x515: ("FElysiumConversationPlace", "bSoundOccluded"),
    },
    # `npc_maker_zombie`'s own three rows, on the shared maker leaf.
    "CNPCMaker_Zombie": {
        0x76D0: ("FElysiumNpcMaker", "ZombieAiType"),
        0x76D4: ("FElysiumNpcMaker", "bShouldRagdoll"),
        0x76D8: ("FElysiumNpcMaker", "RemoveDistance"),
    },
}

# The reflected keyfield structs the baked infrastructure actors carry (0018 story 2), each over
# the replay tables whose keyed rows become its `UPROPERTY`s. The NPC struct is the chain under
# `CAI_BaseNPCTroika` down to (not including) `CBaseEntity`, walked through the replay's own
# `base` links, so a table the chain adds or drops moves the struct with it.
KEYFIELD_STRUCTS = (
    ("FElysiumBaseEntityKeyfields", ("CBaseEntity",)),
    ("FElysiumHintKeyfields", ("CAI_Hint",)),
    ("FElysiumPlaceKeyfields", ("CAI_InterestingPlace",)),
    ("FElysiumConversationPlaceKeyfields", ("CAI_InterestingPlaceConverstation",)),
    ("FElysiumMakerKeyfields", ("CNPCMaker", "CNPCMaker_Zombie")),
    ("FElysiumNpcKeyfields", "chain:CAI_BaseNPCTroika"),
)

# The replay's type names, as the reflected C++ type a keyfield is held in. Anything the table
# does not name (`color32`, `custom`, ...) is held as the raw keyvalue string.
KEYFIELD_TYPES = {
    "int": "int32", "char": "int32", "short": "int32",
    "float": "float", "time": "float",
    "bool": "bool",
    "string": "FString", "modelname": "FString", "soundname": "FString",
    "vector": "FVector", "position": "FVector",
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
    # ("ScheduleHost", "HintGroup"): a word the entity reaches through one of the port's component
    # structs, bound by compiled path instead of by pointer-to-member.
    via: tuple[str, str] | None = None
    # (container, slot, is-base) for a `CBaseCombatCharacter` character-sheet element row.
    sheet: tuple[str, int, bool] | None = None
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
    # The `SAVE`-only rows: retail's persistence, which no name resolves. Only the NPC binding
    # class fills these -- the chain classes' save rows belong to the port classes that own those
    # words, and their persistence is already theirs.
    saved: list[Row] = field(default_factory=list)
    save_unbound: list[Row] = field(default_factory=list)


def _sheet_of(record: dict) -> tuple[str, int, bool] | None:
    """A `CBaseCombatCharacter` trait-array element row as (container, slot, is-base)."""
    match = SHEET_ROW_RE.match(record["name"])
    if match is None:
        return None
    container, half = match.group(1), match.group(2)
    first, count = SHEET_ARRAY_BASE[(container, half)]
    slot, remainder = divmod(int(record["offset"]) - first, 4)
    if remainder != 0 or not 0 <= slot < count:
        raise SystemExit(f"gen_kernel_bindings: {record['name']} at "
                         f"+0x{int(record['offset']):x} is not an element of "
                         f"{container}{half} (first +0x{first:x}, {count} slots)")
    return container, slot, half == "Base"


def _classify_field(cls: str, record: dict, base: dict, bound: dict, no_member: dict,
                    model_offsets: set[int]) -> Row:
    """One field row, resolved against whichever member map its class uses."""
    offset = base["offset"]
    # The NPC's own two tables resolve through the shape map, which the census checks.
    if cls in ("CAI_BaseNPC", "CAI_BaseNPCTroika"):
        if offset not in model_offsets:
            # The replay names a field whose offset the shape census never recorded. That is a
            # disagreement between two committed artefacts, not a row to emit.
            raise SystemExit(f"gen_kernel_bindings: {cls} +0x{offset:x} "
                             f"({record['name']}) is not a word of the shape census")
        if offset in HAND_OWNED_MEMBERS:
            return Row(kind="field", reason=HAND_OWNED_MEMBERS[offset], **base)
        where = BOUND_ELSEWHERE.get((cls, offset))
        if where is not None:
            return Row(kind="field", reason=f"bound by binding class {where}", **base)
        if offset in bound:
            port_type, member = bound[offset]
            path = NPC_COMPONENT_PATHS.get(port_type)
            if path is not None:
                return Row(kind="field", via=(path, member), **base)
            return Row(kind="field", binding=bound[offset], **base)
        if offset in no_member:
            return Row(kind="field",
                       reason=f"no shape-map member ({no_member[offset]})", **base)
        return Row(kind="field", reason="no shape-map member", **base)

    # The entity chain: the hand chain map, the sheet, or a recorded reason.
    if cls in CHAIN_MEMBER_MAPS:
        chain = CHAIN_MEMBER_MAPS[cls]
        if offset in chain:
            return Row(kind="field", binding=chain[offset], **base)
        sheet = _sheet_of(record)
        if sheet is not None:
            return Row(kind="field", sheet=sheet, **base)
        where = BOUND_ELSEWHERE.get((cls, offset))
        if where is not None:
            return Row(kind="field", reason=f"bound by binding class {where}", **base)
        reason = CHAIN_UNBOUND.get((cls, offset))
        if reason is None:
            raise SystemExit(
                f"gen_kernel_bindings: {cls} +0x{offset:x} ({record['name']}, "
                f"\"{base['external']}\") is a retail KEY with no entry in "
                f"CHAIN_MEMBER_MAPS, BOUND_ELSEWHERE or CHAIN_UNBOUND")
        return Row(kind="field",
                   reason=TOGGLE_REASON if reason == "TOGGLE" else reason, **base)

    # A class with its own hand member map (the maker, the places, the hint).
    member_map = CLASS_MEMBER_MAPS[cls]
    if offset in member_map:
        return Row(kind="field", binding=member_map[offset], **base)
    return Row(kind="field", reason="no port member", **base)


def _classify_save(cls: str, record: dict, base: dict, bound: dict, no_member: dict) -> Row:
    """One `SAVE`-only row: a generated save field, or a recorded reason."""
    offset = base["offset"]
    path = SAVE_ROW_PATHS.get(offset)
    if path is not None:
        return Row(kind="save", via=(path, ""), **base)
    reason = SAVE_UNBOUND.get(offset)
    if reason is not None:
        return Row(kind="save", reason=EMBEDDED_REASON if reason == "EMBEDDED" else reason, **base)
    if offset in bound:
        port_type, member = bound[offset]
        component = NPC_COMPONENT_PATHS.get(port_type)
        if component is not None:
            return Row(kind="save", via=(component, member), **base)
        return Row(kind="save", binding=bound[offset], **base)
    if offset in no_member:
        return Row(kind="save", reason=NO_MEMBER_SAVE_REASON[no_member[offset]], **base)
    return Row(kind="save", reason="no shape-map member", **base)


def classify(replay: dict, repo: Path, model_offsets: set[int]) -> list[ClassModel]:
    """Every replay row of the binding classes that has an external name, classified."""
    bound, no_member = parse_shape_map(repo)
    classes: list[ClassModel] = []
    for binding, tables in BINDING_CLASSES:
        model = ClassModel(name=binding, tables=tables)
        for cls in tables:
            for record in replay[cls]["records"]:
                flags = list(record.get("flagNames") or [])
                if "FUNCTIONTABLE" in flags:
                    continue
                offset = int(record["offset"])
                external = record.get("external")
                base = dict(cls=cls, name=record["name"], type=record["typeName"],
                            offset=offset, external=external or "", flags=flags)
                if not external:
                    # Only the NPC's own two tables are this story's save walk; the chain's save
                    # rows are the port classes' own, and those classes already persist them.
                    if binding == "Npc" and "SAVE" in flags:
                        row = _classify_save(cls, record, base, bound, no_member)
                        if row.via is not None or row.binding is not None:
                            model.saved.append(row)
                        else:
                            model.save_unbound.append(row)
                    continue
                if "OUTPUT" in flags:
                    model.outputs.append(Row(kind="output", **base))
                    continue
                if "INPUT" in flags and (record["typeName"] == "void" or offset == 0):
                    model.inputfuncs.append(Row(kind="inputfunc", **base))
                    continue
                row = _classify_field(cls, record, base, bound, no_member, model_offsets)
                if row.binding is not None or row.via is not None or row.sheet is not None:
                    model.bound.append(row)
                else:
                    model.unbound.append(row)
        model.bound.sort(key=lambda r: r.external)
        model.unbound.sort(key=lambda r: r.external)
        model.outputs.sort(key=lambda r: r.external)
        model.inputfuncs.sort(key=lambda r: r.external)
        model.saved.sort(key=lambda r: r.name)
        model.save_unbound.sort(key=lambda r: r.name)
        classes.append(model)
    return classes


@dataclass
class KeyfieldRow:
    """One keyed replay row as a reflected property."""

    table: str
    name: str
    external: str
    type: str
    offset: int
    flags: list[str]


@dataclass
class KeyfieldStruct:
    name: str
    tables: tuple[str, ...]
    rows: list[KeyfieldRow] = field(default_factory=list)


@dataclass
class Model:
    classes: list[ClassModel]
    datamaps: dict[str, str]
    keyfields: list[KeyfieldStruct] = field(default_factory=list)


def _chain_below(replay: dict, top: str, stop: str = "CBaseEntity") -> tuple[str, ...]:
    """`top` and every replay base under it, stopping before `stop`."""
    out: list[str] = []
    cls: str | None = top
    while cls and cls != stop:
        if cls not in replay:
            raise SystemExit(f"gen_kernel_bindings: replay has no table {cls}")
        out.append(cls)
        cls = replay[cls].get("base")
    return tuple(out)


def keyfield_structs(replay: dict) -> list[KeyfieldStruct]:
    """The reflected keyfield structs: every keyed field row of each struct's tables."""
    structs: list[KeyfieldStruct] = []
    for name, spec in KEYFIELD_STRUCTS:
        tables = _chain_below(replay, spec.split(":", 1)[1]) if isinstance(spec, str) else spec
        struct = KeyfieldStruct(name=name, tables=tables)
        seen: dict[str, str] = {}
        for cls in tables:
            for record in replay[cls]["records"]:
                external = record.get("external")
                flags = list(record.get("flagNames") or [])
                if not external or "OUTPUT" in flags or "FUNCTIONTABLE" in flags:
                    continue
                if "INPUT" in flags and (record["typeName"] == "void"
                                         or int(record["offset"]) == 0):
                    continue
                if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", external):
                    raise SystemExit(f"gen_kernel_bindings: {cls}.{external} is not an identifier")
                folded = external.lower()
                if folded in seen:
                    # A reflected property name folds case, exactly as the registry's FName does.
                    raise SystemExit(f"gen_kernel_bindings: {name} repeats {external} "
                                     f"({seen[folded]} and {cls})")
                seen[folded] = cls
                struct.rows.append(KeyfieldRow(
                    table=cls, name=record["name"], external=external,
                    type=record["typeName"], offset=int(record["offset"]), flags=flags))
        structs.append(struct)
    return structs


def build(repo: Path) -> Model:
    # The shape generator's model, used only to confirm each NPC replay offset is a known word.
    shape_model = shape.build(repo_root(), kl.MODULE, kl.DEFAULT_DEPTH)
    model_offsets = {word.offset for word in shape_model.words}

    replay_path = work_root().joinpath(*REPLAY)
    replay = json.loads(replay_path.read_text(encoding="utf-8"))
    keyfields = keyfield_structs(replay)
    datamaps = {cls: replay[cls]["datamap"] for _, tables in BINDING_CLASSES for cls in tables}
    for struct in keyfields:
        for cls in struct.tables:
            datamaps.setdefault(cls, replay[cls]["datamap"])
    return Model(classes=classify(replay, repo, model_offsets), datamaps=datamaps,
                 keyfields=keyfields)


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
        "#pragma once",
        "",
        '#include "Containers/ArrayView.h"',
        "",
        "struct FElysiumClassDesc;",
        "",
        "namespace ElysiumNpcKernelBindings",
        "{",
        *_wrapped("enum class EClass : uint8 { "
                  + ", ".join(c.name for c in model.classes) + " };", "\t"),
        "",
        *[f"\tvoid {_add_function_name(c)}(FElysiumClassDesc& D);" for c in model.classes],
        *[f"\tvoid Add{c.name}SaveFields(FElysiumClassDesc& D);"
          for c in model.classes if c.saved or c.save_unbound],
        "\tTConstArrayView<const TCHAR*> Outputs(EClass Class = EClass::Npc);",
        "\tTConstArrayView<const TCHAR*> InputFuncs(EClass Class = EClass::Npc);",
        "\tstruct FCounts",
        "\t{",
        "\t\tint32 Bound;",
        "\t\tint32 Unbound;",
        "\t\tint32 Outputs;",
        "\t\tint32 InputFuncs;",
        "\t\tint32 Saved;",
        "\t};",
        "\tFCounts Counts(EClass Class = EClass::Npc);",
        "}",
    ]
    return "\n".join(out) + "\n"


def _add_function_name(model_class: ClassModel) -> str:
    if model_class.name == "Npc":
        return "AddNpcFields"
    return f"Add{model_class.name}Fields"


def _row_code(model_class: ClassModel, row: Row) -> str:
    """The one registration statement a bound row emits."""
    name = _literal(row.external if row.kind != "save" else row.name)
    if row.sheet is not None:
        container, slot, is_base = row.sheet
        return (f"ElysiumAddSheetField(D, {name}, "
                f"EElysiumTraitContainer::{container}, {slot}, "
                f"/*bBase=*/{'true' if is_base else 'false'}, {flags_of(row)});")
    if row.via is not None:
        path, member = row.via
        reach = f"{path}.{member}" if member else path
        return (f"ElysiumAddClassFieldVia<{PORT_CLASS[model_class.name]}>(D, {name}, "
                f"[](auto& E) -> auto&{{ return E.{reach}; }}, {flags_of(row)});")
    assert row.binding is not None
    port_type, member = row.binding
    return (f"ElysiumAddClassField(D, {name}, "
            f"&{port_type}::{member}, {flags_of(row)});")


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
        lines = _wrapped(_row_code(model_class, row), "\t\t")
        lines[-1] += f"  // +0x{row.offset:x} {row.name}"
        out += lines
    for row in model_class.unbound:
        out += _comment(f"UNBOUND +0x{row.offset:x} {row.name} \"{row.external}\" "
                        f"— {row.reason}", "\t\t")
    out += ["\t}", ""]
    return out


def _render_save(model_class: ClassModel) -> list[str]:
    """The `SAVE`-only walk: one field per persisted word, under its retail member name."""
    if not model_class.saved and not model_class.save_unbound:
        return []
    out = [
        f"\tvoid Add{model_class.name}SaveFields(FElysiumClassDesc& D)",
        "\t{",
        "\t\t// Retail's persistence, and only that: a `SAVE` row with no external name is",
        "\t\t// reachable by no keyvalue, no input and no Python attribute, so each registers",
        "\t\t// under its RETAIL MEMBER NAME with `EElysiumField::Save` alone. The names are",
        "\t\t// `m_`-prefixed for exactly that reason: they are not a namespace a map can author,",
        "\t\t// and they cannot collide with the externals above.",
    ]
    for row in model_class.saved:
        lines = _wrapped(_row_code(model_class, row), "\t\t")
        lines[-1] += f"  // +0x{row.offset:x} {row.type}"
        out += lines
    for row in model_class.save_unbound:
        out += _comment(f"NOT SAVED +0x{row.offset:x} {row.name} ({row.type}) — {row.reason}",
                        "\t\t")
    out += ["\t}", ""]
    return out


def _array_symbol(model_class: ClassModel) -> str:
    return model_class.name


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
        '#include "ElysiumPlayer.h"',
        '#include "ElysiumSheetSlots.h"',
        '#include "Substrate/ElysiumClassFields.h"',
        '#include "Substrate/ElysiumConversationPlace.h"',
        '#include "Substrate/ElysiumHint.h"',
        '#include "Substrate/ElysiumInterestingPlace.h"',
        '#include "Substrate/ElysiumNpc.h"',
        '#include "Substrate/ElysiumNpcMaker.h"',
        '#include "Substrate/ElysiumSheetFields.h"',
        "",
        "namespace ElysiumNpcKernelBindings",
        "{",
    ]
    for model_class in model.classes:
        out += _render_add(model_class)
        out += _render_save(model_class)
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
                                            len(model_class.inputfuncs),
                                            len(model_class.saved)))
        out.append(f"\t\t\tcase EClass::{model_class.name}:")
        out.append(f"\t\t\t\treturn {{{counts}}};")
    counts = ", ".join(str(n) for n in (len(npc.bound), len(npc.unbound),
                                        len(npc.outputs), len(npc.inputfuncs),
                                        len(npc.saved)))
    out += ["\t\t\tdefault:", f"\t\t\t\treturn {{{counts}}};", "\t\t}", "\t}", "}"]
    return "\n".join(out) + "\n"


def render_keyfields(model: Model) -> str:
    """The reflected keyfield structs the baked infrastructure actors hold.

    One `UPROPERTY` per keyed replay row, named by its external so the reflected name IS the
    keyvalue a map authors (`FindPropertyByName` folds case exactly as the registry's `FName`
    does). Every property is zero/empty by default: retail's constructors leave these words zero
    (`CAI_Hint` `0x102d2e30`), and an actor emits only what it authored, so a default is never
    stamped into an entity def.
    """
    tables = [cls for struct in model.keyfields for cls in struct.tables]
    provenance = ", ".join(f"{cls} {model.datamaps[cls]}" for cls in tables)
    out = [
        "// Generated by `uv run elysium research gen_kernel_bindings`. Do not hand-edit.",
        "//",
        *_comment("The keyed datamap rows of the classes 0018 story 2 bakes as actors, as reflected "
                  "keyfield structs, transcribed from the datamap replay "
                  f"(`research/ghidra/types/datamap_records-vampire.dll.json`; {provenance})."),
        *_comment("A property's name is the retail external name, so the reflected name is the "
                  "keyvalue a map authors. Every default is zero: an actor emits only the keys "
                  "it authored (`AiInfra/ElysiumInfraActor.h`), so a default never reaches an "
                  "entity def."),
        "",
        "#pragma once",
        "",
        '#include "CoreMinimal.h"',
        "",
        '#include "ElysiumInfraKeyfields.generated.h"',
    ]
    defaults = {"int32": " = 0", "float": " = 0.0f", "bool": " = false",
                "FString": "", "FVector": " = FVector::ZeroVector"}
    for struct in model.keyfields:
        out += ["", *_comment(f"{', '.join(struct.tables)} — {len(struct.rows)} keyed rows."),
                "USTRUCT(BlueprintType)", f"struct {struct.name}", "{", "\tGENERATED_BODY()", ""]
        for row in struct.rows:
            cpp = KEYFIELD_TYPES.get(row.type, "FString")
            raw = "" if row.type in KEYFIELD_TYPES else f", held raw ({row.type})"
            out.append(f"\t// +0x{row.offset:x} {row.name} ({', '.join(row.flags)}){raw}")
            # `classname` / `targetname` are the def's identity, hoisted out of its keys; the actor
            # carries them apart (`SourceClassname`, `TargetName`), so these two rows only show.
            access = ("VisibleAnywhere, BlueprintReadOnly"
                      if row.external in ("classname", "targetname")
                      else "EditAnywhere, BlueprintReadWrite")
            out.append(f'\tUPROPERTY({access}, Category = "{row.table}")')
            out.append(f"\t{cpp} {row.external}{defaults[cpp]};")
        out += ["};"]
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
            if row.sheet is not None:
                container, slot, is_base = row.sheet
                where = f"Sheet.{container}[{slot}]{' base' if is_base else ''}"
            elif row.via is not None:
                where = f"{PORT_CLASS[model_class.name]}.{'.'.join(row.via)}"
            else:
                assert row.binding is not None
                where = f"{row.binding[0]}.{row.binding[1]}"
            print(f"  {row.external:<26} {where:<40} {flags_of(row):<38} "
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
        if model_class.saved or model_class.save_unbound:
            print()
            print("Table F — the SAVE walk (retail member, port reach)")
            print(line)
            for row in model_class.saved:
                where = (f"{PORT_CLASS[model_class.name]}."
                         + ".".join(p for p in row.via if p)) if row.via \
                    else f"{row.binding[0]}.{row.binding[1]}"
                print(f"  {row.name:<34} {where:<46} +0x{row.offset:x} {row.type}")
            print()
            print("Table G — SAVE rows with no generated field")
            print(line)
            for row in model_class.save_unbound:
                print(f"  {row.name:<34} +0x{row.offset:<6x} {row.type:<10} {row.reason}")
            print()
        print(f"  bound       {len(model_class.bound):4d}")
        print(f"  unbound     {len(model_class.unbound):4d}")
        print(f"  outputs     {len(model_class.outputs):4d}")
        print(f"  inputfuncs  {len(model_class.inputfuncs):4d}")
        print(f"  saved       {len(model_class.saved):4d}")
        print(f"  not saved   {len(model_class.save_unbound):4d}")


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
    status |= _emit(repo.joinpath(*KEYFIELDS_H), render_keyfields(model), args.check)
    return status


if __name__ == "__main__":
    sys.exit(main())
