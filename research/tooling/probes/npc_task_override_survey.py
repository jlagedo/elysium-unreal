# -*- coding: utf-8 -*-
"""Decode VtMB NPC task registrations and every StartTask/RunTask override.

The shared schedule corpus names tasks but does not preserve their numeric IDs,
and class-local tasks reuse the same IDs from ``0x14a`` upward.  This probe
recovers both the shared and class-local registration calls from the pinned
``vampire.dll``, walks the two task virtuals on every ``CAI_BaseNPC`` RTTI
descendant, joins current map demands, and publishes the decompiled
task-to-activity policies for every animation-bearing override.

Generated JSON is game-derived and belongs below ``$ELYSIUM_WORK_ROOT``.

Usage::

    uv run elysium research npc_task_override_survey
    uv run elysium research npc_task_override_survey --json <external-path>
"""
from __future__ import print_function

import argparse
import collections
import hashlib
import json
import os
import re
import struct

from elysium_pipeline.paths import export_root, vtmb_root
from research.tooling.probes.native_schedule_survey import decode_schedules
from research.tooling.probes.npc_translation_survey import (
    _constructor_aliases,
    decode_translation_slots,
    find_npc_classes,
    survey_current_classes,
)
from research.tooling.probes.weapon_activity_survey import (
    PEImage,
    PINNED_SHA256,
    _find_all,
    decode_activity_registry,
    follow_jump,
)


START_TASK_SLOT = 0x6E8
RUN_TASK_SLOT = 0x6F0
SHARED_TASK_COUNT = 0x14A
TASK_STRING_RE = re.compile(rb"(?<![A-Z0-9_])(TASK_[A-Z0-9_]+)\x00")

SHARED_HANDLERS = {
    "start": {0x102827F0, 0x102A1910},
    "run": {0x10288780, 0x102AACF0},
}

# MSVC keeps most class-local pairs as two stack-local immediate stores.  Ten
# final/adjacent entries reuse an already-loaded register instead; their exact
# IDs are confirmed by their registration functions in the pinned binary.
OPTIMIZED_CUSTOM_TASK_IDS = {
    "TASK_VCHANGBROS_TELEPORT_POST": 0x153,
    "TASK_VCHANGBROS_UNITED_MOVE_POST": 0x15D,
    "TASK_MANBAT_LAND": 0x150,
    "TASK_MANBAT_RESET_FLYBY_SOUND": 0x164,
    "TASK_VMING_XIAO_PARTICLE_DEATH": 0x15E,
    "TASK_VMING_XIAO_PARTICLE_DEATH_PROXY": 0x15F,
    "TASK_VMING_XIAO_TENTACLE_FORCED_DEATH": 0x156,
    "TASK_VSABBATLEADER_SELECT_TELEPORT_ARCH": 0x155,
    "TASK_VSABBATLEADER_CHARGE_RELEASE": 0x163,
    "TASK_VWEREWOLF_OBS_DOOR_HINT": 0x161,
}

TASK_OWNER_PREFIXES = (
    ("TASK_VMING_XIAO_TENTACLE_", "CNPC_VMingXiaoTentacle"),
    ("TASK_VMING_XIAO_", "CNPC_VMingXiao"),
    ("TASK_VANDREIBLOOD_", "CNPC_VAndreiBlood"),
    ("TASK_VASIANVAMPIRE_", "CNPC_VAsianVampire"),
    ("TASK_VCHANGBROS_", "CNPC_VChangBros"),
    ("TASK_VFRENZYSHADOW_", "CNPC_VFrenzyShadow"),
    ("TASK_VGHOUL_CROUCHER_", "CNPC_VGhoulCroucher"),
    ("TASK_VHENGEYOKAI_", "CNPC_VHengeyokai"),
    ("TASK_VSABBATLEADER_", "CNPC_VSabbatLeader"),
    ("TASK_VSCURRYING_", "CNPC_VScurrying"),
    ("TASK_VSHERIFFMAN_", "CNPC_VSheriffMan"),
    ("TASK_VSHERIFFSWARM_", "CNPC_VSheriffSwarm"),
    ("TASK_VVAMPIREBOSS_", "CNPC_VVampireBoss"),
    ("TASK_VWEREWOLF_", "CNPC_VWerewolf"),
    ("TASK_VZOMBIE_", "CNPC_VZombie"),
    ("TASK_CROW_", "CNPC_VCrow"),
    ("TASK_VBACH_", "CNPC_VBach"),
    ("TASK_VBATSWARM_", "CNPC_VBatSwarm"),
    ("TASK_VCOP_", "CNPC_VCop"),
    ("TASK_MANBAT_", "CNPC_VManBat"),
)


def _policy(handler, phase, tasks, route, activities=(), condition=""):
    if isinstance(tasks, str):
        tasks = (tasks,)
    if isinstance(activities, str):
        activities = (activities,)
    return {
        "handler": handler,
        "phase": phase,
        "tasks": list(tasks),
        "route": route,
        "activities": list(activities),
        "condition": condition,
    }


# Decompiled policy ledger.  Every row below ends at SetIdealActivity, the
# restart helper (clear equal current activity, then SetIdealActivity), immediate
# SetActivity, or a deliberate remap to the shared TASK_SET_ACTIVITY handler.
# The complete vtable ledger separately records handlers with no such route.
ACTION_POLICIES = [
    _policy(0x10358330, "start", "TASK_CROW_TAKEOFF", "set_ideal",
            "ACT_CROW_TAKEOFF", "runtime-registered activity stored at 0x1093a118"),
    _policy(0x10358330, "start", "TASK_CROW_FLY", "set_ideal", "ACT_FLY"),
    _policy(0x10358330, "start", "TASK_CROW_HOP", "set_ideal", "ACT_HOP"),
    _policy(0x103587B0, "run", "TASK_CROW_TAKEOFF", "set_ideal", "ACT_FLY",
            "after the takeoff sequence completes"),
    _policy(0x103587B0, "run", "TASK_CROW_HOP", "set_ideal", "ACT_IDLE",
            "after the hop sequence completes"),

    _policy(0x1035D1B0, "start", "TASK_VANDREIBLOOD_TELEPORT_OUT", "restart_ideal",
            "ACT_ANDREI_TELEPORT_OUT"),
    _policy(0x1035D1B0, "start", "TASK_VANDREIBLOOD_TELEPORT_IN", "restart_ideal",
            "ACT_ANDREI_TELEPORT_IN"),
    _policy(0x1035D1B0, "start", "TASK_VANDREIBLOOD_SUMMON_HEADRUNNER", "restart_ideal",
            "ACT_ANDREI_SUMMON"),

    _policy(0x1035F650, "start", ("TASK_MELEE_FEINT1", "TASK_MELEE_FEINT2"),
            "restart_ideal", "ACT_MELEE_ATTACK"),
    _policy(0x1035F650, "start", "TASK_MELEE_DODGE", "restart_ideal", "ACT_DODGE"),
    _policy(0x1035F650, "start", "TASK_MELEE_BLOCK", "restart_ideal", "ACT_BLOCK"),
    _policy(0x103612E0, "run", "TASK_JUMP", "restart_ideal", "ACT_LEAP_ASCEND",
            "reasserted while meaningful vertical velocity remains"),
    _policy(0x103645A0, "start", "TASK_WAIT_ATTACK_TIME2", "restart_ideal", "ACT_AIM",
            "Bach's delayed ranged-attack branch when no pending target helper owns it"),

    _policy(0x1036B750, "start", "TASK_VCHANGBROS_TELEPORT_PRE", "restart_ideal",
            "ACT_CHANG_TELEPORT_IN"),
    _policy(0x1036B750, "start", "TASK_VCHANGBROS_TELEPORT_POST", "restart_ideal",
            "ACT_CHANG_TELEPORT_OUT"),
    _policy(0x1036B750, "start", "TASK_VCHANGBROS_ENERGY_BALL_CHARGE", "restart_ideal",
            "ACT_CHANG_UNITED_IDLE"),
    _policy(0x1036B750, "start", "TASK_VCHANGBROS_ENERGY_BALL_RELEASE", "restart_ideal",
            "ACT_CHANG_RANGE_ATTACK"),
    _policy(0x1036B750, "start", "TASK_VCHANGBROS_UNITED_MOVE_PRE", "restart_ideal",
            "ACT_CHANG_UNITED_PRE"),
    _policy(0x1036B750, "start", "TASK_VCHANGBROS_UNITED_MOVE_IDLE", "restart_ideal",
            "ACT_CHANG_UNITED_IDLE"),
    _policy(0x1036B750, "start", "TASK_VCHANGBROS_UNITED_MOVE_POST", "restart_ideal",
            "ACT_CHANG_UNITED_POST"),
    _policy(0x1036BFC0, "run", "TASK_JUMP", "restart_ideal", "ACT_LEAP_ASCEND",
            "reasserted while meaningful vertical velocity remains"),

    _policy(0x10374940, "start", "TASK_SPECIAL_IDLE_ACTIVITY", "set_activity",
            "ACT_FIDGET", "immediate commit before delegating to the animal handler"),
    _policy(0x10374940, "start", "TASK_MELEE_ATTACK1", "restart_ideal",
            "ACT_MELEE_ATTACK"),
    _policy(0x10375F50, "start", ("TASK_CIRCLE_ENEMY", "TASK_CIRCLE_ENEMY_FULLCYCLE"),
            "restart_ideal_choice", ("ACT_RUN", "ACT_WALK"),
            "use ACT_RUN when available, otherwise ACT_WALK"),
    _policy(0x103790D0, "start", "TASK_BASH_TARGET", "restart_ideal",
            "ACT_SPECIAL_ATTACK1"),

    _policy(0x1037B8B0, "start", "TASK_VGHOUL_CROUCHER_PLAY_UNAWARE_ACTIVITY",
            "restart_ideal_choice",
            ("ACT_MADNESS_IDLE", "ACT_LAUGH_IDLE", "ACT_SOBBING_IDLE"),
            "state index 0/1/2/3 selects madness/laugh/sobbing/madness"),
    _policy(0x1037B8B0, "start", "TASK_VGHOUL_CROUCHER_PLAY_UNAWARE_ACTIVITY_EXIT",
            "restart_ideal_choice",
            ("ACT_MADNESS_GETOUT", "ACT_LAUGH_GETOUT", "ACT_SOBBING_GETOUT"),
            "state index 0/1/2/3 selects madness/laugh/sobbing/madness get-out"),

    _policy(0x103805D0, "start", "TASK_PICKUP_FISH", "restart_ideal", "ACT_PICKUP_LIGHT"),
    _policy(0x103805D0, "start", ("TASK_THROW_FISH", "TASK_THROW_FISH_FAKE"),
            "restart_ideal", "ACT_PICKUP_LIGHTTHROW"),

    _policy(0x103847F0, "start", ("TASK_MELEE_FEINT1", "TASK_MELEE_FEINT2"),
            "restart_ideal", "ACT_MELEE_ATTACK"),
    _policy(0x103847F0, "start", "TASK_MELEE_DODGE", "restart_ideal_choice",
            "ACT_STEPBACK",
            "an active weapon sequence may replace ACT_STEPBACK with its descriptor activity"),
    _policy(0x103847F0, "start", "TASK_MELEE_PREBLOCK", "restart_ideal", "ACT_PREBLOCK"),
    _policy(0x103847F0, "start", "TASK_MELEE_BLOCK", "restart_ideal", "ACT_BLOCK"),
    _policy(0x103847F0, "start", "TASK_MELEE_BLOCK_HEAVY", "restart_ideal", "ACT_BLOCK_HEAVY"),
    _policy(0x103847F0, "start", "TASK_MELEE_BLOCKED_REACTION_LEFT", "restart_ideal",
            "ACT_BLOCKED_REACTION_LEFT"),
    _policy(0x103847F0, "start", "TASK_MELEE_BLOCKED_REACTION_RIGHT", "restart_ideal",
            "ACT_BLOCKED_REACTION_RIGHT"),

    _policy(0x1038C390, "start", "TASK_MANBAT_TAKEOFF", "set_ideal", "ACT_HOP"),
    _policy(0x1038C390, "start", "TASK_MANBAT_FALL_TO_GROUND", "set_ideal", "ACT_FALLING"),
    _policy(0x1038C390, "start", "TASK_MANBAT_LAND", "set_ideal", "ACT_LAND"),
    _policy(0x1038C390, "start", "TASK_MANBAT_RISE_UP", "set_ideal",
            "ACT_MANBAT_FLY_UP_WITH_MISSILE"),
    _policy(0x1038C390, "start", "TASK_MANBAT_THROW_MISSILE", "set_ideal", "ACT_THROW"),
    _policy(0x1038C390, "start", "TASK_MANBAT_SCREECH", "set_ideal", "ACT_MANBAT_SCREECH"),
    _policy(0x1038C390, "start", ("TASK_MANBAT_FLYBY_ATTACK", "TASK_MANBAT_GRAB_COP"),
            "set_ideal", "ACT_MELEE_ATTACK"),
    _policy(0x1038C390, "start", "TASK_MANBAT_BREAK_SPOTLIGHT", "set_ideal", "ACT_GETUP_BACK"),
    _policy(0x1038D130, "run", "TASK_MANBAT_FALL_TO_GROUND", "set_ideal",
            "ACT_MANBAT_WRITHE", "when the ground trace completes"),
    _policy(0x1038D130, "run", "TASK_MANBAT_LAND", "set_ideal", "ACT_IDLE",
            "after landing completes"),
    _policy(0x1038D130, "run", "TASK_MANBAT_FLY_BY_COP", "set_ideal", "ACT_MELEE_ATTACK",
            "at the cop-contact continuation"),

    _policy(0x10392D80, "start", ("TASK_MELEE_FEINT1", "TASK_MELEE_FEINT2"),
            "restart_ideal", "ACT_MELEE_ATTACK"),
    _policy(0x10392D80, "start", "TASK_MELEE_DODGE", "restart_ideal", "ACT_STEPBACK"),
    _policy(0x10392D80, "start", "TASK_MELEE_BLOCK", "restart_ideal", "ACT_BLOCK"),
    _policy(0x10392D80, "start", "TASK_VMING_XIAO_ATTACK_BACK_RIGHT", "restart_ideal",
            "ACT_THROW_RIGHT"),
    _policy(0x10392D80, "start", "TASK_VMING_XIAO_ATTACK_BACK_LEFT", "restart_ideal",
            "ACT_THROW_LEFT"),
    _policy(0x10392D80, "start", "TASK_VMING_XIAO_ATTACK_SPIT", "restart_ideal",
            "ACT_RANGE_ATTACK1"),
    _policy(0x10392D80, "start", "TASK_VMING_XIAO_PICKUP_THROWABLE", "restart_ideal_choice",
            ("ACT_PICKUP_LEFT", "ACT_PICKUP_RIGHT"), "side is selected from the throwable geometry"),
    _policy(0x10392D80, "start", "TASK_VMING_XIAO_THROW_THROWABLE", "restart_ideal_choice",
            ("ACT_THROW_LEFT", "ACT_THROW_RIGHT"), "side is retained from pickup"),
    _policy(0x10392D80, "start", "TASK_VMING_XIAO_PLAY_HEAD_HIT", "restart_ideal", "ACT_HIT_HEAD"),
    _policy(0x10392D80, "start", "TASK_VMING_XIAO_PLAY_TENTACLE_HIT", "restart_ideal",
            "ACT_HIT_TORSO"),
    _policy(0x10393930, "run", "TASK_VMING_XIAO_PLAY_TENTACLE_HIT", "restart_ideal", "ACT_IDLE",
            "after the hit sequence completes"),

    _policy(0x1039C4C0, "start", "TASK_MELEE_DODGE", "restart_ideal", "ACT_STEPBACK"),
    _policy(0x1039C4C0, "start", "TASK_MELEE_BLOCK", "restart_ideal", "ACT_BLOCK"),
    _policy(0x1039C4C0, "start", "TASK_VMING_XIAO_TENTACLE_PLAY_HIT", "restart_ideal",
            "ACT_SMALL_FLINCH"),
    _policy(0x1039D750, "run", "TASK_VMING_XIAO_TENTACLE_PLAY_HIT", "restart_ideal", "ACT_IDLE",
            "after the hit sequence completes"),

    _policy(0x103A78C0, "start", "TASK_VSABBATLEADER_DIVE_IN", "restart_ideal",
            "ACT_ANDREI_DIVE_IN"),
    _policy(0x103A78C0, "start", "TASK_VSABBATLEADER_DIVE_OUT", "restart_ideal",
            "ACT_ANDREI_DIVE_OUT"),
    _policy(0x103A78C0, "start", "TASK_VSABBATLEADER_ROAR", "restart_ideal", "ACT_ANDREI_ROAR"),
    _policy(0x103A78C0, "start", "TASK_VSABBATLEADER_CHARGE_INTO", "restart_ideal",
            "ACT_ANDREI_CHARGE_INTO"),
    _policy(0x103A78C0, "start", "TASK_VSABBATLEADER_CHARGE_IDLE", "restart_ideal",
            "ACT_ANDREI_CHARGE_IDLE"),
    _policy(0x103A78C0, "start", "TASK_VSABBATLEADER_CHARGE_RELEASE", "restart_ideal",
            "ACT_ANDREI_CHARGE_RELEASE"),
    _policy(0x103AC740, "start", "TASK_SPECIAL_IDLE_ACTIVITY", "set_activity", "ACT_FIDGET"),
    _policy(0x103B36D0, "start", "TASK_RUN_DIALOG", "set_activity", "ACT_FACING_IDLE"),
    _policy(0x103B38A0, "run", "TASK_RUN_DIALOG", "set_activity", "ACT_IDLE",
            "when the dialogue-facing task completes"),

    _policy(0x103BA7C0, "start", ("TASK_MELEE_FEINT1", "TASK_MELEE_FEINT2"),
            "restart_ideal", "ACT_MELEE_ATTACK"),
    _policy(0x103BA7C0, "start", "TASK_MELEE_DODGE", "restart_ideal", "ACT_DODGE"),
    _policy(0x103BA7C0, "start", "TASK_MELEE_BLOCK", "restart_ideal", "ACT_BLOCK"),
    _policy(0x103BA7C0, "start", "TASK_THROW_BODY", "restart_ideal", "ACT_THROW_BODY"),
    _policy(0x103BA7C0, "start", "TASK_THROW_BODY_FAKE", "restart_ideal", "ACT_THROW_BODY_FAKE"),
    _policy(0x103BA7C0, "start", "TASK_PICKUP_BODY", "restart_ideal_choice",
            ("ACT_PICKUP_BODY_NORMAL", "ACT_PICKUP_BODY_NORMAL_L"), "body side selects the variant"),
    _policy(0x103BA7C0, "start", "TASK_POUNCE_ATTACK", "restart_ideal", "ACT_POUNCE"),
    _policy(0x103BA7C0, "start", "TASK_POUNCE_ATTACK1", "restart_ideal", "ACT_POUNCE1"),
    _policy(0x103BA7C0, "start", "TASK_POUNCE_ATTACK2", "restart_ideal", "ACT_POUNCE2"),
    _policy(0x103BA7C0, "start", "TASK_POUNCE_ATTACK3", "restart_ideal", "ACT_POUNCE3"),
    _policy(0x103BA7C0, "start", "TASK_PLAY_CLAW_SEQUENCE", "set_ideal_argument", (),
            "the task's float argument is cast to an activity ID"),
    _policy(0x103BB1E0, "run", "TASK_PLAY_CLAW_SEQUENCE", "set_ideal_argument", (),
            "retranslate the argument and restart it until the translated activity completes"),
    _policy(0x103C1820, "start",
            ("TASK_MELEE_CIRCLE_ENEMY", "TASK_CIRCLE_ENEMY", "TASK_CIRCLE_ENEMY_FULLCYCLE"),
            "remap_shared_task", "ACT_IDLE", "delegates as TASK_SET_ACTIVITY with argument ACT_IDLE"),
    _policy(0x103C35D0, "start",
            ("TASK_MELEE_CIRCLE_ENEMY", "TASK_CIRCLE_ENEMY", "TASK_CIRCLE_ENEMY_FULLCYCLE"),
            "restart_ideal", "ACT_IDLE"),

    _policy(0x103CCDA0, "start", "TASK_VWEREWOLF_PLAY_UNREACHABLE_FIDGET",
            "restart_ideal_choice", ("ACT_ROAR_LONG", "ACT_SNIFFING", "ACT_SEARCH"),
            "roar when its state flag is set; otherwise random sniff/search"),
    _policy(0x103CCDA0, "start", "TASK_VWEREWOLF_WAIT_FOR_MOVEMENT_TELEPORT",
            "set_ideal_navigator", "ACT_IDLE", "navigator movement activity when present, otherwise idle"),
    _policy(0x103CDFB0, "run", "TASK_VWEREWOLF_WAIT_FOR_MOVEMENT_TELEPORT",
            "set_ideal_navigator", "ACT_IDLE", "reassert on navigator phase changes; otherwise idle"),
    _policy(0x103CCDA0, "start", "TASK_VWEREWOLF_DEATH_INTO", "restart_ideal", "ACT_DEATH_INTO"),
    _policy(0x103CCDA0, "start", "TASK_VWEREWOLF_DEATH_ATTACK", "restart_ideal", "ACT_DEATH_ATTACK"),
    _policy(0x103CCDA0, "start", "TASK_VWEREWOLF_DEATH_OUTOF", "restart_ideal", "ACT_DEATH_OUTOF"),
    _policy(0x103CCDA0, "start", "TASK_VWEREWOLF_DEATH_FINALE", "restart_ideal", "ACT_DEATH_FINALE"),
    _policy(0x103CCDA0, "start", "TASK_VWEREWOLF_PLAY_DEAD", "restart_ideal", "ACT_PLAY_DEAD"),
    _policy(0x103CDFB0, "run", "TASK_VWEREWOLF_PLAY_DEAD", "restart_ideal", "ACT_PLAY_DEAD",
            "reasserted by the play-dead continuation"),
    _policy(0x103CCDA0, "start", "TASK_VWEREWOLF_OBS_DOOR_HINT", "restart_ideal",
            "ACT_OBS_DOOR_SQUEEZE"),

    _policy(0x103DFD80, "start", "TASK_MELEE_ATTACK1", "restart_ideal", "ACT_MELEE_ATTACK"),
    _policy(0x103DFD80, "start", "TASK_VZOMBIE_CRAWL_OUT_OF_GROUND", "restart_ideal",
            "ACT_GETUP_FRONT"),
    _policy(0x103DFD80, "start", "TASK_VZOMBIE_PERFORM_ANIMATED_DEATH", "set_ideal",
            "ACT_CAULDRON_DEATH"),
    _policy(0x103DFD80, "start", "TASK_VZOMBIE_PERFORM_FLINCH", "set_ideal", "ACT_BIG_FLINCH"),
    _policy(0x103DFD80, "start", "TASK_VZOMBIE_FEAR_SOMETHING", "restart_ideal_choice",
            ("ACT_COWER", "ACT_COWER2", "ACT_COWER3"),
            "fear mode 1/2/3 selects the cower variant; mode zero retains this+0xfec"),
]


def _task_id_at_reference(data, operand_offset):
    """Decode the registration ID adjacent to one task-string immediate."""
    # Shared registrations: push class; push id (imm8/imm32); push task name.
    if operand_offset >= 3 and data[operand_offset - 1] == 0x68:
        if data[operand_offset - 3] == 0x6A:
            return data[operand_offset - 2], "push8"
        if operand_offset >= 6 and data[operand_offset - 6] == 0x68:
            return struct.unpack_from("<I", data, operand_offset - 5)[0], "push32"
    # Class-local registrations: mov [esp+n], name; mov [esp+n+4], id.
    if (operand_offset >= 4 and data[operand_offset - 4:operand_offset - 1] == b"\xc7\x44\x24"
            and data[operand_offset + 4:operand_offset + 7] == b"\xc7\x44\x24"):
        return struct.unpack_from("<I", data, operand_offset + 8)[0], "stack_pair"
    return None, ""


def _task_owner(name, task_id):
    if task_id < SHARED_TASK_COUNT:
        return "shared"
    for prefix, owner in TASK_OWNER_PREFIXES:
        if name.startswith(prefix):
            return owner
    return "class_local_unassigned"


def decode_task_registrations(image):
    """Recover the shared 0..0x149 and every class-local task registration."""
    data = image.data
    text_offset, text = image.section_bytes(".text")
    rows = {}
    string_locations = collections.defaultdict(list)
    for match in TASK_STRING_RE.finditer(data):
        name = match.group(1).decode("ascii")
        string_va = image.offset_to_va(match.start())
        if string_va is None:
            continue
        string_locations[name].append(string_va)
        for relative in _find_all(text, struct.pack("<I", string_va)):
            operand_offset = text_offset + relative
            task_id, pattern = _task_id_at_reference(data, operand_offset)
            if task_id is None:
                continue
            owner = _task_owner(name, task_id)
            rows[(owner, task_id, name)] = {
                "owner": owner,
                "id": task_id,
                "name": name,
                "string_va": "0x%x" % string_va,
                "reference_va": "0x%x" % image.offset_to_va(operand_offset),
                "decode": pattern,
            }

    for name, task_id in OPTIMIZED_CUSTOM_TASK_IDS.items():
        key = (_task_owner(name, task_id), task_id, name)
        if key in rows:
            continue
        locations = string_locations.get(name, [])
        if not locations:
            raise ValueError("optimized task string missing: %s" % name)
        string_va = locations[0]
        references = list(_find_all(text, struct.pack("<I", string_va)))
        rows[key] = {
            "owner": key[0],
            "id": task_id,
            "name": name,
            "string_va": "0x%x" % string_va,
            "reference_va": ("0x%x" % image.offset_to_va(text_offset + references[0])
                             if references else ""),
            "decode": "confirmed_register_reuse",
        }

    registrations = sorted(rows.values(), key=lambda row: (
        row["owner"] != "shared", row["owner"], row["id"], row["name"]))
    shared = [row for row in registrations if row["owner"] == "shared"]
    if len(shared) != SHARED_TASK_COUNT or {row["id"] for row in shared} != set(range(SHARED_TASK_COUNT)):
        raise ValueError("shared task registration range is not contiguous 0..0x149")
    return registrations


def decode_task_virtuals(image, classes):
    """Attach effective StartTask/RunTask bodies and entity aliases."""
    for row in classes:
        vtable = row["vtable_va"]
        functions = {}
        for phase, slot in (("start", START_TASK_SLOT), ("run", RUN_TASK_SLOT)):
            entry = image.read_u32_va(vtable + slot)
            if entry is None:
                raise ValueError("missing task slot 0x%x on %s" % (slot, row["cpp_class"]))
            functions[phase] = "0x%x" % follow_jump(image, entry)
        row["task_functions"] = functions
        row["entity_classnames"] = _constructor_aliases(image, vtable)
    return classes


def build_task_usage(schedules):
    usage = collections.defaultdict(lambda: {
        "invocations": 0,
        "schedules": set(),
        "arguments": collections.Counter(),
    })
    for schedule in schedules:
        for task in schedule["tasks"]:
            row = usage[task["task"]]
            row["invocations"] += 1
            row["schedules"].add(schedule["name"])
            row["arguments"][task["argument"]] += 1
    return [{
        "task": name,
        "invocations": row["invocations"],
        "schedule_count": len(row["schedules"]),
        "schedules": sorted(row["schedules"]),
        "arguments": dict(row["arguments"].most_common()),
    } for name, row in sorted(usage.items())]


def materialize_policies(registrations, activity_registrations):
    tasks = collections.defaultdict(list)
    for row in registrations:
        tasks[row["name"]].append(row)
    activities = {row["name"]: row for row in activity_registrations}
    result = []
    for source in ACTION_POLICIES:
        row = dict(source)
        row["handler"] = "0x%x" % source["handler"]
        row["task_registrations"] = []
        for name in source["tasks"]:
            matches = tasks.get(name, [])
            if len(matches) != 1:
                raise ValueError("policy task %s has %d registrations" % (name, len(matches)))
            row["task_registrations"].append({
                "name": name,
                "owner": matches[0]["owner"],
                "id": matches[0]["id"],
            })
        row["activity_registrations"] = []
        for name in source["activities"]:
            match = activities.get(name)
            row["activity_registrations"].append({
                "name": name,
                "id": match["id"] if match else None,
                "kind": match["kind"] if match else "runtime_registered",
            })
        result.append(row)
    return result


def group_handlers(classes, policies):
    policy_counts = collections.Counter((row["phase"], row["handler"]) for row in policies)
    groups = []
    for phase in ("start", "run"):
        by_body = collections.defaultdict(list)
        for row in classes:
            by_body[row["task_functions"][phase]].append(row["cpp_class"])
        for body, owners in sorted(by_body.items(), key=lambda item: int(item[0], 16)):
            address = int(body, 16)
            groups.append({
                "phase": phase,
                "handler": body,
                "classes": sorted(owners),
                "class_count": len(owners),
                "shared_dispatcher": address in SHARED_HANDLERS[phase],
                "action_policy_count": policy_counts[(phase, body)],
                "direct_animation_policy": policy_counts[(phase, body)] != 0,
            })
    return groups


def summarize(classes, registrations, schedules, task_usage, policies, handlers, demands):
    custom_regs = [row for row in registrations if row["owner"] != "shared"]
    custom_handlers = [row for row in handlers if not row["shared_dispatcher"]]
    current_custom = set()
    for demand in demands:
        if demand["status"] != "resolved":
            continue
        for phase, body in demand.get("task_functions", {}).items():
            if int(body, 16) not in SHARED_HANDLERS[phase]:
                current_custom.add((phase, body))
    usage_by_name = {row["task"]: row["invocations"] for row in task_usage}
    return {
        "npc_subclasses": len(classes),
        "distinct_start_task_handlers": len({row["task_functions"]["start"] for row in classes}),
        "distinct_run_task_handlers": len({row["task_functions"]["run"] for row in classes}),
        "custom_handler_bodies": len(custom_handlers),
        "custom_handlers_with_direct_animation_policy": sum(
            row["direct_animation_policy"] for row in custom_handlers),
        "custom_handlers_without_direct_animation_policy": sum(
            not row["direct_animation_policy"] for row in custom_handlers),
        "shared_task_registrations": len(registrations) - len(custom_regs),
        "class_local_task_registrations": len(custom_regs),
        "class_local_task_owners": len({row["owner"] for row in custom_regs}),
        "compiled_schedules": len(schedules),
        "compiled_task_invocations": sum(row["invocations"] for row in task_usage),
        "distinct_compiled_tasks": len(task_usage),
        "registered_custom_tasks_used_by_schedules": sum(
            usage_by_name.get(row["name"], 0) > 0 for row in custom_regs),
        "registered_custom_task_invocations": sum(
            usage_by_name.get(row["name"], 0) for row in custom_regs),
        "action_policy_rows": len(policies),
        "action_policy_task_routes": sum(len(row["tasks"]) for row in policies),
        "exact_sequence_label_routes_in_custom_handlers": 0,
        "layer_routes_in_custom_handlers": 0,
        "current_map_demands": len(demands),
        "current_custom_task_handlers": len(current_custom),
    }


def build_report(binary_path=None, root=None):
    if binary_path is None:
        binary_path = vtmb_root() / "Vampire" / "dlls" / "vampire.dll"
    if root is None:
        root = export_root()
    binary_path = os.path.abspath(os.fspath(binary_path))
    with open(binary_path, "rb") as handle:
        data = handle.read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != PINNED_SHA256:
        raise ValueError("unsupported vampire.dll SHA-256 %s" % digest)

    image = PEImage(data)
    registrations = decode_task_registrations(image)
    activity_registrations = decode_activity_registry(image)
    schedules = decode_schedules(data)
    usage = build_task_usage(schedules)
    classes = decode_task_virtuals(image, find_npc_classes(image))
    # Retain the translation ledger too so the existing current-corpus resolver
    # can identify entity aliases without a second RTTI walk.
    classes = decode_translation_slots(image, classes)
    demands = survey_current_classes(root, classes)
    class_index = {row["cpp_class"]: row for row in classes}
    for demand in demands:
        if len(demand["cpp_classes"]) == 1:
            demand["task_functions"] = class_index[demand["cpp_classes"][0]]["task_functions"]
        else:
            demand["task_functions"] = {}
    policies = materialize_policies(registrations, activity_registrations)
    handlers = group_handlers(classes, policies)
    report = {
        "binary": {
            "path": binary_path,
            "size": len(data),
            "sha256": digest,
        },
        "task_slots": {
            "start": "0x%x" % START_TASK_SLOT,
            "run": "0x%x" % RUN_TASK_SLOT,
        },
        "task_registrations": registrations,
        "task_usage": usage,
        "action_policies": policies,
        "handler_groups": handlers,
        "npc_classes": classes,
        "current_demands": demands,
    }
    report["summary"] = summarize(
        classes, registrations, schedules, usage, policies, handlers, demands)
    return report


def print_report(report):
    summary = report["summary"]
    print("=" * 78)
    print("VtMB NPC task override / animation surface")
    print("=" * 78)
    print("binary: %s" % report["binary"]["path"])
    print("sha256: %s" % report["binary"]["sha256"])
    print("NPC subclasses: %(npc_subclasses)d | StartTask bodies: "
          "%(distinct_start_task_handlers)d | RunTask bodies: %(distinct_run_task_handlers)d" % summary)
    print("shared registrations: %(shared_task_registrations)d | class-local: "
          "%(class_local_task_registrations)d across %(class_local_task_owners)d owners" % summary)
    print("custom handler bodies: %(custom_handler_bodies)d | animation-bearing: "
          "%(custom_handlers_with_direct_animation_policy)d | non-animation: "
          "%(custom_handlers_without_direct_animation_policy)d" % summary)
    print("action policy rows: %(action_policy_rows)d | task routes: "
          "%(action_policy_task_routes)d" % summary)
    print("custom exact-label routes: %(exact_sequence_label_routes_in_custom_handlers)d | "
          "custom layer routes: %(layer_routes_in_custom_handlers)d" % summary)
    print()
    print("Custom handlers")
    for row in report["handler_groups"]:
        if row["shared_dispatcher"]:
            continue
        print("  %-5s %-10s classes=%-2d policies=%-2d %s" % (
            row["phase"], row["handler"], row["class_count"],
            row["action_policy_count"], ", ".join(row["classes"])))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", help="override the retail vampire.dll path")
    parser.add_argument("--export-root", help="override the exported map root")
    parser.add_argument("--json", help="write the generated game-derived ledger here")
    args = parser.parse_args()
    report = build_report(args.binary, args.export_root)
    print_report(report)
    if args.json:
        parent = os.path.dirname(os.path.abspath(args.json))
        if parent:
            os.makedirs(parent, exist_ok=True)
        with open(args.json, "w") as handle:
            json.dump(report, handle, indent=1, sort_keys=True)
        print("\nwrote %s" % args.json)


if __name__ == "__main__":
    main()
