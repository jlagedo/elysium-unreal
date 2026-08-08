# -*- coding: utf-8 -*-
"""Decode VtMB v2531 sequence events and every native event-handler surface.

The old model record is 76 bytes and carries only a normalized cycle, numeric
event id, type, and a fixed 64-byte options field.  This survey walks every MDL
in the patch-first install, then walks MSVC RTTI across ``vampire.dll`` and
``client.dll``.  It resolves server ``CBaseAnimating::HandleAnimEvent`` and
weapon ``Operator_HandleAnimEvent`` plus client ``C_BaseAnimating::FireEvent``
and the viewmodel weapon hook.  The result is a reproducible model-event
inventory plus the complete native handler ownership surface.

Generated JSON contains game-derived model names and options.  Keep it below
``$ELYSIUM_WORK_ROOT`` and never commit it.

Usage::

    uv run elysium research animation_event_survey
    uv run elysium research animation_event_survey --json <external-path>
"""
from __future__ import print_function

import argparse
import collections
import hashlib
import json
import math
import os
import struct

from elysium_pipeline.formats import install, mdl_skel
from elysium_pipeline.paths import vtmb_root
from research.tooling.probes.npc_translation_survey import _primary_vtable
from research.tooling.probes.weapon_activity_survey import (
    PEImage,
    PINNED_SHA256,
    TYPE_DESCRIPTOR_RE,
    _find_all,
    _rtti_bases,
    _undecorate_type,
    follow_jump,
)


SERVER_ANIMATING_BASE = "CBaseAnimating"
SERVER_WEAPON_BASE = "CBaseCombatWeapon"
CLIENT_ANIMATING_BASE = "C_BaseAnimating"
CLIENT_WEAPON_BASE = "C_BaseCombatWeapon"
HANDLE_ANIM_EVENT_SLOT = 0x40C
SERVER_WEAPON_OPERATOR_SLOT = 0x5C8
CLIENT_FIRE_EVENT_SLOT = 0x1FC
CLIENT_WEAPON_EVENT_SLOT = 0x3A8
CLIENT_PINNED_SHA256 = "e88beae0dd03af06493c71c5e8d87a6993b54e590cb6ad37cd3513c588582870"
SEQDESC_STRIDE = 764


def _route(event_ids, behavior):
    return {"event_ids": list(event_ids), "behavior": behavior}


# Every effective body present at CBaseAnimating::HandleAnimEvent's virtual slot
# in the pinned vampire.dll.  These policies are a checked transcription of the
# decompiled switch bodies; the RTTI walk below proves there is no unlisted body.
HANDLER_POLICIES = {
    0x10071900: {
        "owner": "CCameraAnimated",
        "routes": [_route((1003,), "options 1..8 fire OnScriptEvent01..08")],
        "fallback": "CBaseAnimating",
    },
    0x10091DA0: {
        "owner": "CBaseAnimating family",
        "routes": [
            _route((2070, 2071), "resolve the named attachment and toggle its paired state"),
            _route((4005,), "select and commit the weighted sequence named by options"),
        ],
        "fallback": "warn for an unhandled event",
    },
    0x10178A10: {
        "owner": "CBasePlayer family",
        "routes": [
            _route((2060,), "parse the options envelope and apply the player facial/breath channel"),
            _route((4050, 4051), "attach then detach the options-selected player action object"),
        ],
        "fallback": "CBaseCombatCharacter after source/player gating",
    },
    0x1024F0C0: {
        "owner": "CBaseCombatWeapon family",
        "routes": [],
        "fallback": "empty HandleAnimEvent body; weapon-range events arrive through Operator_HandleAnimEvent",
    },
    0x10274E30: {
        "owner": "CAI_BaseNPC family",
        "routes": [
            _route((1000, 1010, 1020, 1021, 1022), "scripted/bodygroup state events"),
            _route((1001, 1002), "notify the owning scripted_sequence of its event phase"),
            _route((1003,), "route the numeric options payload to the scripted, direct, or expression owner"),
            _route((1004, 1005, 1008, 1009), "base scripted sound/effect hooks"),
            _route((2001, 2002, 2010), "body-drop and swish event hooks"),
            _route((2020,), "commit ACT_IDLE and the sequence movement/effect state for a 180 turn"),
            _route((2022,), "apply an options-relative movement yaw"),
            _route((2040,), "pick up the options-selected or reserved weapon"),
            _route((2041, 2042, 2043, 2044), "drop or drive the active weapon sequence/activity"),
            _route((2050, 2051, 2052, 2053), "left/right foot event families"),
        ],
        "fallback": "CBaseCombatCharacter; source-owned weapon events route to Weapon_HandleAnimEvent",
    },
    0x1029B290: {
        "owner": "CAI_BaseNPCTroika family",
        "routes": [
            _route((2005, 2006), "Troika attachment/effect hooks"),
            _route((2021,), "Troika movement-turn continuation"),
            _route((2040,), "Troika item/weapon pickup continuation"),
            _route((2060,), "parse and apply the NPC facial/breath envelope"),
            _route((2061,), "add the options-selected timed sequence layer"),
            _route((4150, 4151, 4152, 4153, 4154, 4155), "Troika weapon-conditioned interesting-place sound hooks"),
        ],
        "fallback": "CAI_BaseNPC",
    },
    0x1032E330: {
        "owner": "CBaseCombatCharacter family",
        "routes": [
            _route(tuple(range(3000, 4000)), "forward weapon-range or foreign-source events to the active weapon Operator_HandleAnimEvent"),
            _route((4006, 4007), "invoke the active weapon's paired visibility/state virtual"),
            _route((4020,), "emit the options-selected combat-character sound"),
            _route((4100, 4102), "create and attach the options-selected model; 4102 first clears the prior attachment"),
            _route((4101,), "clear the attached model"),
        ],
        "fallback": "CBaseAnimating",
    },
    0x1034A680: {
        "owner": "CGenericNPC",
        "routes": [_route((1,), "invoke the generic NPC event callback")],
        "fallback": "CAI_BaseNPC",
    },
    0x10357820: {
        "owner": "CNPC_VCrow",
        "routes": [
            _route((2,), "crow target/takeoff helper"),
            _route((3,), "SetActivity(ACT_FLY)"),
            _route((4,), "crow flight-physics takeoff helper"),
        ],
        "fallback": "CAI_BaseNPC",
    },
    0x10368EC0: {
        "owner": "camera NPC family",
        "routes": [],
        "fallback": "empty HandleAnimEvent body",
    },
    0x10374280: {
        "owner": "CNPC_VDog",
        "routes": [_route((3001,), "apply the dog melee bite when an owner body exists")],
        "fallback": "CAI_BaseNPCTroika",
    },
    0x103786C0: {
        "owner": "CNPC_VGargoyle",
        "routes": [
            _route((1,), "handled no-op"),
            _route((2,), "emit the gargoyle roar sound"),
            _route((2050, 2051), "gargoyle foot impact plus the class foot-event virtual"),
        ],
        "fallback": "CAI_BaseNPCTroika",
    },
    0x1037FB60: {
        "owner": "CNPC_VHengeyokai",
        "routes": [
            _route((2040,), "pick up the reserved fish/body object"),
            _route((2050, 2051), "conditional impact plus the class foot-event virtual"),
            _route((3005,), "throw the held fish/body object"),
        ],
        "fallback": "CAI_BaseNPCTroika",
    },
    0x1038E000: {
        "owner": "CNPC_VManBat",
        "routes": [_route((1, 2, 3), "emit the three ManBat form-specific sound/effect hooks")],
        "fallback": "CAI_BaseNPCTroika",
    },
    0x10392A70: {
        "owner": "CNPC_VMingXiao",
        "routes": [
            _route((2040,), "pick up the reserved throwable"),
            _route((2050, 2051), "handled no-op"),
            _route((2100, 2101), "apply the two Ming impact-radius variants"),
            _route((3005,), "throw the held object"),
            _route((3031,), "run the Ming weapon-event helper before base dispatch"),
        ],
        "fallback": "CAI_BaseNPCTroika",
    },
    0x103A7000: {
        "owner": "CNPC_VSabbatLeader",
        "routes": [_route((2050, 2051), "dispatch the SabbatLeader foot-event virtual")],
        "fallback": "CAI_BaseNPCTroika",
    },
    0x103BA410: {
        "owner": "CNPC_VTzimisce",
        "routes": [
            _route(tuple(range(2, 10)), "dispatch the current-form event virtual"),
            _route((2040,), "pick up the reserved body"),
            _route((2050, 2051), "impact plus the Tzimisce foot-event virtual"),
            _route((3003,), "dispatch the Tzimisce melee-swish virtual"),
            _route((3005,), "throw the held body"),
        ],
        "fallback": "CAI_BaseNPCTroika",
    },
    0x103C1540: {
        "owner": "CNPC_VTzimisceHeadClaw",
        "routes": [_route((2050, 2051), "impact plus the side-selected claw foot-event virtual")],
        "fallback": "CAI_BaseNPCTroika",
    },
    0x103C32C0: {
        "owner": "CNPC_VTzimisceRunner",
        "routes": [_route((2050, 2051), "dispatch the side-selected runner foot-event virtual")],
        "fallback": "CAI_BaseNPCTroika",
    },
    0x103D88E0: {
        "owner": "CNPC_VWerewolf",
        "routes": [
            _route((1003,), "route numeric script events among the three stored event owners"),
            _route((2050, 2051, 2052, 2053), "werewolf foot-impact helper"),
            _route(tuple(range(2100, 2110)), "werewolf impact/damage family; 2100 and 2101 have distinct damage helpers"),
        ],
        "fallback": "CAI_BaseNPCTroika",
    },
}


# The active weapon receives every server event in [3000, 3999] (and every
# foreign-source event) through CBaseCombatWeapon::Operator_HandleAnimEvent at
# vtable +0x5c8.  These seven bodies cover all 169 retail weapon subclasses.
SERVER_WEAPON_POLICIES = {
    0x10238160: {
        "owner": "CWeaponRanged family",
        "routes": [
            _route(tuple(range(3030, 3045)), "commit the ranged fire-state transition selected by the operator/global mode gate"),
            _route((4001, 4002), "parse options as a bodygroup and set it shown then hidden"),
        ],
        "fallback": "CBaseCombatWeapon warning body",
    },
    0x1024F030: {
        "owner": "CBaseCombatWeapon family",
        "routes": [],
        "fallback": "warn with the event id, operator class, and weapon class",
    },
    0x103E8BE0: {
        "owner": "CWeaponMelee_TzimisceMelee",
        "routes": [
            _route((3003,), "handled no-op"),
            _route((3045, 3046), "select Tzimisce melee variant 1 or 2 and invoke its attack virtual when the operator gate permits"),
        ],
        "fallback": "common melee policy",
    },
    0x103EA5B0: {
        "owner": "common CWeaponMelee family",
        "routes": [
            _route((3001, 3003), "handled no-op; damage and swish ownership remains with the operator/class event path"),
            _route(tuple(range(3030, 3038)), "handled no-op ranged-event compatibility window"),
            _route((3047,), "invoke the class melee-event virtual when the operator gate permits"),
            _route((4001, 4002), "parse options as a bodygroup and set it shown then hidden"),
        ],
        "fallback": "CBaseCombatWeapon warning body",
    },
    0x103EC460: {
        "owner": "CWeaponMelee_MingXiaoMelee",
        "routes": [_route((3003,), "handled no-op")],
        "fallback": "common melee policy",
    },
    0x103ECA20: {
        "owner": "CWeaponMelee_MingXiaoTentacle",
        "routes": [_route((3003,), "handled no-op")],
        "fallback": "common melee policy",
    },
    0x103F4470: {
        "owner": "inventory/non-combat weapon family",
        "routes": [_route((3014, 3200), "handled no-op")],
        "fallback": "CBaseCombatWeapon warning body",
    },
}


# Client FireEvent at vtable +0x1fc is the >=5000 half excluded by the server
# dispatcher.  Three bodies cover every one of the 237 C_BaseAnimating
# descendants in the pinned client DLL.
CLIENT_FIRE_EVENT_POLICIES = {
    0x100935A0: {
        "owner": "C_BaseAnimating family",
        "routes": [
            _route((5001, 5011, 5021, 5031), "player muzzle flash on attachments 0..3"),
            _route((5002,), "disabled client model-spark event; emits the retail re-enable warning"),
            _route((5003, 5013, 5023, 5033), "NPC muzzle flash on attachments 0..3"),
            _route((5004,), "emit the options-selected sound"),
            _route((5005,), "emit the options-selected Disciplines sound"),
            _route((5101, 5102), "parse options as a bodygroup and hide then show it"),
            _route((5103,), "stop and remove the model-bound effect handle"),
            _route((5105,), "look up the options ACT_* activity, select its weighted sequence, commit it, and reset playback state"),
            _route((5111, 5112, 5113, 5114), "emit the options-selected effect on attachments 1..4"),
            _route((5115, 5116, 5119), "emit the options-selected effect on the default eyes, mouth, or crotch attachment"),
            _route((5117,), "emit the options-selected effect at entity origin and angles"),
            _route((5118,), "parse attachment;emitter and bind the named emitter to the named attachment"),
        ],
        "fallback": "unrecognized client event is a no-op",
    },
    0x10099C00: {
        "owner": "C_BaseCombatCharacter/player family",
        "routes": [
            _route((5003, 5013, 5023, 5033), "prefer the active weapon muzzle-flash attachment/effect, otherwise use the character attachment"),
            _route((5120,), "emit the options-selected effect from the active weapon slampoint attachment"),
        ],
        "fallback": "C_BaseAnimating",
    },
    0x100AB530: {
        "owner": "C_BaseViewModel",
        "routes": [
            _route((6001, 6002, 6003, 6004), "parse two integers and emit the repeated weapon effect on attachments 1..4"),
            _route((6011, 6012, 6013, 6014), "parse one integer and emit the single weapon effect on attachments 1..4"),
        ],
        "fallback": "first offer every event to the active weapon hook; if it returns false, delegate to C_BaseAnimating",
    },
}


# All 214 client weapon subclasses share this one viewmodel event-hook body.
CLIENT_WEAPON_EVENT_POLICIES = {
    0x1009C970: {
        "owner": "all C_BaseCombatWeapon descendants",
        "routes": [
            _route((5001, 5011, 5021, 5031), "intercept eligible viewmodel/owner muzzle flashes on attachments 0..3"),
            _route((6001, 6002, 6003, 6004), "intercept eligible repeated viewmodel weapon effects on attachments 1..4"),
            _route((6011, 6012, 6013, 6014), "intercept eligible single viewmodel weapon effects on attachments 1..4"),
        ],
        "fallback": "return false so C_BaseViewModel/C_BaseAnimating can process the event",
    },
}


def find_descendant_classes(image, base_name):
    """Return primary RTTI/vtable records for every named-base descendant."""
    rdata_offset, rdata = image.section_bytes(".rdata")
    classes = []
    seen = set()
    for match in TYPE_DESCRIPTOR_RE.finditer(image.data):
        decorated = match.group(0)[:-1].decode("ascii")
        type_va = image.offset_to_va(match.start() - 8)
        if type_va is None:
            continue
        for type_reference in _find_all(rdata, struct.pack("<I", type_va)):
            if type_reference < 12:
                continue
            locator_offset = rdata_offset + type_reference - 12
            signature, object_offset, cd_offset = struct.unpack_from(
                "<III", image.data, locator_offset
            )
            if signature != 0 or object_offset != 0 or cd_offset >= 0x1000:
                continue
            locator_va = image.offset_to_va(locator_offset)
            hierarchy_va = image.read_u32_va(locator_va + 16)
            bases = _rtti_bases(image, hierarchy_va) if hierarchy_va else []
            if base_name not in bases:
                continue
            vtable_va = _primary_vtable(image, locator_va)
            name = _undecorate_type(decorated)
            if vtable_va is not None and name not in seen:
                seen.add(name)
                classes.append({
                    "cpp_class": name,
                    "direct_base": bases[1] if len(bases) > 1 else "",
                    "bases": bases,
                    "type_descriptor_va": "0x%x" % type_va,
                    "vtable_va": vtable_va,
                })
            break
    classes.sort(key=lambda row: row["cpp_class"])
    return classes


def decode_virtual_groups(image, classes, slot, policies, field_name):
    """Resolve one virtual slot and group every class by its effective body."""
    groups = collections.defaultdict(list)
    for row in classes:
        entry = image.read_u32_va(row["vtable_va"] + slot)
        if entry is None:
            raise ValueError("missing %s slot on %s" % (field_name, row["cpp_class"]))
        body = follow_jump(image, entry)
        groups[body].append(row["cpp_class"])
        row["vtable_va"] = "0x%x" % row["vtable_va"]
        row[field_name] = "0x%x" % body

    unknown = set(groups) - set(policies)
    missing = set(policies) - set(groups)
    if unknown or missing:
        raise ValueError(
            "handler policy mismatch; unknown=%s missing=%s" %
            (["0x%x" % value for value in sorted(unknown)],
             ["0x%x" % value for value in sorted(missing)])
        )

    result = []
    for body, owners in sorted(groups.items()):
        policy = policies[body]
        result.append({
            "handler": "0x%x" % body,
            "owner": policy["owner"],
            "class_count": len(owners),
            "classes": sorted(owners),
            "routes": policy["routes"],
            "fallback": policy["fallback"],
        })
    return result


def find_animating_classes(image):
    """Backward-compatible server CBaseAnimating RTTI helper."""
    return find_descendant_classes(image, SERVER_ANIMATING_BASE)


def decode_handler_groups(image, classes):
    """Backward-compatible server HandleAnimEvent grouping helper."""
    return decode_virtual_groups(
        image, classes, HANDLE_ANIM_EVENT_SLOT, HANDLER_POLICIES,
        "handle_anim_event",
    )


def _new_partition():
    return {
        "models": 0,
        "event_sequences": 0,
        "events": 0,
        "max_events_per_sequence": 0,
        "event_ids": collections.Counter(),
        "types": collections.Counter(),
        "options": collections.Counter(),
        "nonempty_options": 0,
    }


def _materialize_partition(value):
    return {
        "models": value["models"],
        "event_sequences": value["event_sequences"],
        "events": value["events"],
        "max_events_per_sequence": value["max_events_per_sequence"],
        "distinct_event_ids": len(value["event_ids"]),
        "distinct_options": len(value["options"]),
        "nonempty_options": value["nonempty_options"],
        "event_ids": {str(key): count for key, count in sorted(value["event_ids"].items())},
        "types": {str(key): count for key, count in sorted(value["types"].items())},
        "options": dict(value["options"].most_common()),
    }


def survey_models(index):
    """Survey every patch-first v2531 MDL and return its decoded event ledger."""
    partitions = {name: _new_partition() for name in ("all", "character", "weapons")}
    records = []
    invalid = []
    non_v2531 = []
    for key in sorted(value for value in index if value.endswith(".mdl")):
        data = install.read(index, key)
        if data is None or len(data) < 280 or data[:4] != b"IDST":
            invalid.append({"model": key, "reason": "missing or invalid IDST header"})
            continue
        version = struct.unpack_from("<i", data, 4)[0]
        if version != 2531:
            non_v2531.append({"model": key, "version": version})
            continue

        names = ["all"]
        if key.startswith("models/character/"):
            names.append("character")
        if key.startswith("models/weapons/"):
            names.append("weapons")
        for name in names:
            partitions[name]["models"] += 1

        sequence_count = struct.unpack_from("<i", data, 272)[0]
        sequence_base = struct.unpack_from("<i", data, 276)[0]
        if (sequence_count < 0 or sequence_count > 100000 or sequence_base < 0 or
                sequence_base + sequence_count * SEQDESC_STRIDE > len(data)):
            invalid.append({"model": key, "reason": "invalid sequence descriptor array"})
            continue

        for sequence_index in range(sequence_count):
            descriptor = sequence_base + sequence_index * SEQDESC_STRIDE
            declared = struct.unpack_from("<i", data, descriptor + 20)[0]
            if declared <= 0:
                continue
            events = mdl_skel.read_events(data, descriptor)
            if len(events) != declared:
                invalid.append({
                    "model": key,
                    "sequence_index": sequence_index,
                    "declared_events": declared,
                    "decoded_events": len(events),
                    "reason": "invalid event array",
                })
                continue
            try:
                label = mdl_skel._cstr_rel(data, descriptor, 0)
            except (ValueError, struct.error):
                label = ""
            row = {
                "model": key,
                "sequence_index": sequence_index,
                "sequence": label,
                "events": [],
            }
            for event in events:
                row["events"].append({
                    "cycle": event.cycle,
                    "event": event.event,
                    "type": event.type,
                    "options": event.options,
                })
                if not math.isfinite(event.cycle) or not 0.0 <= event.cycle <= 1.0:
                    invalid.append({
                        "model": key,
                        "sequence_index": sequence_index,
                        "event": event.event,
                        "cycle": event.cycle,
                        "reason": "event cycle outside [0,1]",
                    })
            records.append(row)
            for name in names:
                part = partitions[name]
                part["event_sequences"] += 1
                part["events"] += len(events)
                part["max_events_per_sequence"] = max(
                    part["max_events_per_sequence"], len(events)
                )
                for event in events:
                    part["event_ids"][event.event] += 1
                    part["types"][event.type] += 1
                    part["options"][event.options] += 1
                    if event.options:
                        part["nonempty_options"] += 1

    return {
        "partitions": {
            name: _materialize_partition(value)
            for name, value in partitions.items()
        },
        "event_sequences": records,
        "invalid_records": invalid,
        "non_v2531_models": non_v2531,
    }


def _load_pinned_image(path, expected_digest, label):
    path = os.path.abspath(os.fspath(path))
    with open(path, "rb") as handle:
        binary = handle.read()
    digest = hashlib.sha256(binary).hexdigest()
    if digest != expected_digest:
        raise ValueError("unsupported %s SHA-256 %s" % (label, digest))
    return path, binary, digest, PEImage(binary)


def build_report(binary_path=None, client_binary_path=None):
    if binary_path is None:
        binary_path = vtmb_root() / "Vampire" / "dlls" / "vampire.dll"
    if client_binary_path is None:
        client_binary_path = vtmb_root() / "Vampire" / "cl_dlls" / "client.dll"

    server_path, server_binary, server_digest, server_image = _load_pinned_image(
        binary_path, PINNED_SHA256, "vampire.dll"
    )
    client_path, client_binary, client_digest, client_image = _load_pinned_image(
        client_binary_path, CLIENT_PINNED_SHA256, "client.dll"
    )

    server_animating_classes = find_descendant_classes(
        server_image, SERVER_ANIMATING_BASE
    )
    server_handlers = decode_virtual_groups(
        server_image, server_animating_classes, HANDLE_ANIM_EVENT_SLOT,
        HANDLER_POLICIES, "handle_anim_event",
    )
    server_weapon_classes = find_descendant_classes(
        server_image, SERVER_WEAPON_BASE
    )
    server_weapon_handlers = decode_virtual_groups(
        server_image, server_weapon_classes, SERVER_WEAPON_OPERATOR_SLOT,
        SERVER_WEAPON_POLICIES, "operator_handle_anim_event",
    )
    client_animating_classes = find_descendant_classes(
        client_image, CLIENT_ANIMATING_BASE
    )
    client_handlers = decode_virtual_groups(
        client_image, client_animating_classes, CLIENT_FIRE_EVENT_SLOT,
        CLIENT_FIRE_EVENT_POLICIES, "fire_event",
    )
    client_weapon_classes = find_descendant_classes(
        client_image, CLIENT_WEAPON_BASE
    )
    client_weapon_handlers = decode_virtual_groups(
        client_image, client_weapon_classes, CLIENT_WEAPON_EVENT_SLOT,
        CLIENT_WEAPON_EVENT_POLICIES, "on_fire_event",
    )
    models = survey_models(install.build_index(("models",), verbose=False))
    all_models = models["partitions"]["all"]
    summary = {
        "v2531_models": all_models["models"],
        "event_sequences": all_models["event_sequences"],
        "events": all_models["events"],
        "distinct_event_ids": all_models["distinct_event_ids"],
        "distinct_options": all_models["distinct_options"],
        "nonempty_options": all_models["nonempty_options"],
        "max_events_per_sequence": all_models["max_events_per_sequence"],
        "invalid_records": len(models["invalid_records"]),
        "non_v2531_models": len(models["non_v2531_models"]),
        "server_animating_classes": len(server_animating_classes),
        "server_handler_bodies": len(server_handlers),
        "server_weapon_classes": len(server_weapon_classes),
        "server_weapon_handler_bodies": len(server_weapon_handlers),
        "client_animating_classes": len(client_animating_classes),
        "client_handler_bodies": len(client_handlers),
        "client_weapon_classes": len(client_weapon_classes),
        "client_weapon_handler_bodies": len(client_weapon_handlers),
        "event_record_stride": mdl_skel._EVENT_STRIDE,
    }
    return {
        "binaries": {
            "server": {"path": server_path, "size": len(server_binary), "sha256": server_digest},
            "client": {"path": client_path, "size": len(client_binary), "sha256": client_digest},
        },
        "slots": {
            "server_handle_anim_event": "0x%x" % HANDLE_ANIM_EVENT_SLOT,
            "server_weapon_operator": "0x%x" % SERVER_WEAPON_OPERATOR_SLOT,
            "client_fire_event": "0x%x" % CLIENT_FIRE_EVENT_SLOT,
            "client_weapon_event": "0x%x" % CLIENT_WEAPON_EVENT_SLOT,
        },
        "summary": summary,
        "models": models,
        "server_handler_groups": server_handlers,
        "server_weapon_operator_groups": server_weapon_handlers,
        "client_fire_event_groups": client_handlers,
        "client_weapon_event_groups": client_weapon_handlers,
        "classes": {
            "server_animating": server_animating_classes,
            "server_weapons": server_weapon_classes,
            "client_animating": client_animating_classes,
            "client_weapons": client_weapon_classes,
        },
    }


def print_report(report):
    summary = report["summary"]
    parts = report["models"]["partitions"]
    print("=" * 78)
    print("VtMB sequence-event / native handler surface")
    print("=" * 78)
    print("server: %s" % report["binaries"]["server"]["path"])
    print("server sha256: %s" % report["binaries"]["server"]["sha256"])
    print("client: %s" % report["binaries"]["client"]["path"])
    print("client sha256: %s" % report["binaries"]["client"]["sha256"])
    print("v2531 MDLs: %(v2531_models)d | event sequences: %(event_sequences)d | "
          "events: %(events)d" % summary)
    print("event IDs: %(distinct_event_ids)d | options: %(distinct_options)d "
          "(%(nonempty_options)d nonempty records) | max/sequence: "
          "%(max_events_per_sequence)d" % summary)
    print("record stride: %(event_record_stride)d | invalid: %(invalid_records)d | "
          "non-v2531: %(non_v2531_models)d" % summary)
    print("character: %(models)d models / %(event_sequences)d sequences / %(events)d events" %
          parts["character"])
    print("weapons:   %(models)d models / %(event_sequences)d sequences / %(events)d events" %
          parts["weapons"])
    print("server animating: %(server_animating_classes)d classes / "
          "%(server_handler_bodies)d bodies | weapons: %(server_weapon_classes)d / "
          "%(server_weapon_handler_bodies)d" % summary)
    print("client animating: %(client_animating_classes)d classes / "
          "%(client_handler_bodies)d bodies | weapons: %(client_weapon_classes)d / "
          "%(client_weapon_handler_bodies)d" % summary)
    print()
    for title, key in (
        ("Server HandleAnimEvent groups", "server_handler_groups"),
        ("Server weapon Operator_HandleAnimEvent groups", "server_weapon_operator_groups"),
        ("Client FireEvent groups", "client_fire_event_groups"),
        ("Client weapon event-hook groups", "client_weapon_event_groups"),
    ):
        print(title)
        for row in report[key]:
            print("  %-10s classes=%-3d %s" %
                  (row["handler"], row["class_count"], row["owner"]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", help="override the retail vampire.dll path")
    parser.add_argument("--client-binary", help="override the retail client.dll path")
    parser.add_argument("--json", help="write the generated game-derived ledger here")
    args = parser.parse_args()
    report = build_report(args.binary, args.client_binary)
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
