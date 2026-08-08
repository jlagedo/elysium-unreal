# -*- coding: utf-8 -*-
"""Decode VtMB's complete player compact-action vocabulary and reachability.

The pinned ``vampire.dll`` carries a 17-name ``PLAYER_*`` table used by
targeted-discipline ``Player_Anim`` fields.  The same integer is consumed by
``CBasePlayer::SetAnimation``.  This probe decodes that table, inventories every
indirect ``+0x704`` call in the server, separates genuine player-animation
producers from the unrelated NPC task-name virtual at the same byte offset,
checks both player RTTI classes for overrides, and surveys patch-first vdata.

Read-only unless ``--json`` is supplied.  The generated ledger is game-derived
and belongs below ``$ELYSIUM_WORK_ROOT/research``; never commit it.

Usage::

    uv run elysium research player_action_survey
    uv run elysium research player_action_survey --json <external-path>
"""
from __future__ import print_function

import argparse
import collections
import hashlib
import json
import os
import re
import struct

from elysium_pipeline.paths import vtmb_root
from research.tooling.probes.weapon_activity_survey import (
    PEImage,
    PINNED_SHA256,
    follow_jump,
)


PLAYER_ACTION_NAMES = 0x106AC3A0
PLAYER_ACTION_COUNT = 17
SET_ANIMATION_SLOT = 0x704
PLAYER_SLOTS = {
    "protected_owner": 0x670,
    "ordinary_selector": 0x684,
    "set_animation_router": 0x704,
    "special_owner_inventory_forward": 0x720,
    "special_owner_inventory_receiver": 0x724,
}


# The completion/restart chain for a non-looping ordinary activity.  These are
# kept as instruction blocks rather than prose-only addresses so the generated
# report fails if the pinned server ever stops supporting the decoded rule.
CROUCH_RESTART_EVIDENCE = (
    (
        0x1008F2CF,
        b"\xc6\x86\x5c\x06\x00\x00\x01",
        "StudioFrameAdvance sets m_bSequenceFinished after clamping a non-looping sequence",
    ),
    (
        0x10164595,
        b"\x3b\xf9\x75\x0e\x8a\x86\x5c\x06\x00\x00\x84\xc0\x75\x04\x32\xc0\xeb\x02\xb0\x01",
        "the player apply path treats an unchanged activity as dirty when m_bSequenceFinished is set",
    ),
    (
        0x101645F1,
        b"\x39\xae\xf0\x06\x00\x00\x75\x0a\x8a\x86\x5c\x06\x00\x00\x84\xc0\x74\x2b",
        "the same finished flag forces reset even when weighted selection returns the current sequence",
    ),
    (
        0x1016461B,
        b"\x8b\xce\xe8\x8f\xd1\xea\xff",
        "the forced path calls ResetSequenceInfo",
    ),
    (
        0x10090A35,
        b"\x3a\xc3\x88\x9e\x5c\x06\x00\x00\x89\x9e\x58\x06\x00\x00",
        "ResetSequenceInfo clears m_bSequenceFinished and cycle for the restarted sequence",
    ),
)


def _policy(name, producer, selector, status="reachable"):
    return {
        "expected_name": name,
        "producer_policy": producer,
        "selector_policy": selector,
        "reachability": status,
    }


# The names are verified against the retail table rather than treated as the
# source of truth.  Policies summarize the hash-pinned classifier/selector and
# the complete +0x704 caller pass.
ACTION_POLICIES = {
    0: _policy(
        "PLAYER_IDLE",
        "classifier when grounded and stationary; forced-idle helper 0x10181680",
        "ACT_AIM for an eligible non-unarmed weapon, otherwise ACT_CROUCH when ducked, else ACT_IDLE",
    ),
    1: _policy(
        "PLAYER_WALK",
        "classifier when grounded with nonzero horizontal velocity; movement helper 0x1016a9b0",
        "ACT_SNEAK when ducked; otherwise speed selects ACT_WALK/ACT_RUN or relaxed variants",
    ),
    2: _policy(
        "PLAYER_JUMP",
        "classifier while jump/landing state is positive and water level is zero; jump helper 0x1016a870",
        "jump state 1..11 selects ACT_HOP through ACT_LAND_HARD",
    ),
    3: _policy(
        "PLAYER_SUPERJUMP",
        "no classifier, native call-site, retained-latch writer, or patch-first Player_Anim producer",
        "no dedicated ordinary-selector branch; falls through to the realized locomotion base",
        "unreachable_in_pinned_surface",
    ),
    4: _policy(
        "PLAYER_DIE",
        "player death routine 0x10163af0",
        "death/protected activity ownership has priority; the ordinary selector has no dedicated code-4 branch",
    ),
    5: _policy(
        "PLAYER_ATTACK1",
        "ranged, base/thrown, frag-grenade, and discipline-weapon attack paths",
        "melee capability selects ACT_MELEE_ATTACK/ACT_MELEE_AIR_ATTACK; ranged capability adds ACT_RANGE_ATTACK1_LAYER",
    ),
    6: _policy(
        "PLAYER_FEED",
        "no classifier, native call-site, retained-latch writer, or patch-first Player_Anim producer",
        "no dedicated ordinary-selector branch; feeding pairs use PLAYER_GRAPPLE plus the separate nine-mode paired-action router",
        "unreachable_in_pinned_surface",
    ),
    7: _policy(
        "PLAYER_PRAY",
        "classifier when InPrayer is true or both form/prayer gate bytes are asserted",
        "ACT_PRAYING_BEGIN/IDLE/END state machine, returning to ACT_IDLE after the end",
    ),
    8: _policy(
        "PLAYER_GRAPPLE",
        "classifier while the feed/grapple target handle is live in release state zero",
        "release flags select ACT_FEEDING_RELEASED_IDLE_ATTACKER or ACT_SEDUCTIVE_RELEASED_IDLE_ATTACKER",
    ),
    9: _policy(
        "PLAYER_SWIM",
        "classifier at water level above two, or at level two while not grounded",
        "realized speed/vertical velocity selects ACT_SWIM or ACT_TREADWATER",
    ),
    10: _policy(
        "PLAYER_USE",
        "classifier while the interaction handle is live",
        "uses the interaction entity's supplied activity when it is valid",
    ),
    11: _policy(
        "PLAYER_LADDER",
        "classifier when movement type is ten",
        "vertical velocity selects ACT_CLIMB_UP or ACT_CLIMB_DOWN",
    ),
    12: _policy(
        "PLAYER_VOMIT",
        "two native purge/vomit calls plus the only patch-first discipline Player_Anim value",
        "protected retained state advances ACT_VOMIT_INTO -> ACT_VOMIT_IDLE -> ACT_VOMIT_GETOUT -> ACT_IDLE",
    ),
    13: _policy(
        "PLAYER_BLOCK",
        "held +wpn_secondaryatk while grounded and the active weapon capability mask intersects 0x18000",
        "ACT_PREBLOCK",
    ),
    14: _policy(
        "PLAYER_RELOAD",
        "common reload helper and frag-grenade reload-style path",
        "adds ACT_RELOAD_LAYER when an active weapon exists",
    ),
    15: _policy(
        "PLAYER_START_AIMING",
        "no classifier, native call-site, retained-latch writer, or patch-first Player_Anim producer",
        "no dedicated ordinary-selector branch",
        "unreachable_in_pinned_surface",
    ),
    16: _policy(
        "PLAYER_LEAVE_AIMING",
        "no classifier, native call-site, retained-latch writer, or patch-first Player_Anim producer",
        "no dedicated ordinary-selector branch",
        "unreachable_in_pinned_surface",
    ),
}


# Every real call through CBasePlayer's +0x704 SetAnimation slot.  The second
# argument is kept because it distinguishes ordinary PostThink work from forced
# weapon/discipline and protected-action requests.
PLAYER_ACTION_CALLS = {
    0x10163CAD: ("0x10163af0", "player_death", 4, 0),
    0x1016440D: ("0x10164240", "paired_action_router_reentry", None, 0),
    0x1016976B: ("0x10169660", "purge_or_vomit", 12, 1),
    0x1016979F: ("0x10169660", "purge_or_vomit", 12, 1),
    0x1016A8C6: ("0x1016a870", "jump_state_entry", 2, 0),
    0x1016A9C8: ("0x1016a9b0", "moving_state_entry", 1, 0),
    0x1016C2A5: ("0x1016be10", "postthink_classifier_result", None, "dynamic"),
    0x1018169F: ("0x10181680", "forced_idle_state", 0, "caller_argument"),
    0x101DE9E2: ("0x101de660", "discipline_player_anim_field", None, 1),
    0x10238661: ("0x10238580", "ranged_weapon_attack", 5, 1),
    0x10254D08: ("0x10254cd0", "common_weapon_reload", 14, 0),
    0x1025579A: ("0x10255700", "base_or_thrown_weapon_attack", 5, 0),
    0x103EE393: ("0x103ee340", "frag_grenade_reload_style", 14, 0),
    0x103EE556: ("0x103ee510", "frag_grenade_attack", 5, 0),
    0x103F2245: ("0x103f2220", "discipline_weapon_attack", 5, 0),
}


# CAI_BaseNPC uses another virtual at the same numeric byte offset to stringify
# task IDs.  These calls are explicitly dispositioned so a raw displacement scan
# cannot silently inflate the player producer surface.
NON_ACTION_SLOT_CALLS = {
    0x10276E5A, 0x10276EF5, 0x1027780A, 0x1027788E, 0x10277DE8,
    0x10281D5C, 0x10281D7C, 0x10281F6F, 0x10286F68, 0x102896FA,
    0x1036C86A, 0x103CCE1B, 0x103CE029, 0x103CE847,
}


PLAYER_ANIM_RE = re.compile(br'"Player_Anim"\s*"([^"]+)"', re.IGNORECASE)


def decode_action_names(image):
    """Read the complete 17-entry ``PLAYER_*`` pointer table."""
    names = []
    for code in range(PLAYER_ACTION_COUNT):
        pointer = image.read_u32_va(PLAYER_ACTION_NAMES + code * 4)
        if pointer is None:
            raise ValueError("missing player action name pointer %d" % code)
        name = image.read_cstring_va(pointer)
        if not name:
            raise ValueError("empty player action name %d" % code)
        expected = ACTION_POLICIES[code]["expected_name"]
        if name != expected:
            raise ValueError("player action %d drifted: %s != %s" %
                             (code, name, expected))
        row = dict(ACTION_POLICIES[code])
        row.update({"code": code, "name_pointer": "0x%x" % pointer})
        names.append(row)
    return names


def indirect_calls_with_displacement(image, displacement):
    """Find ``call [reg+disp32]`` instructions in ``.text``.

    VtMB's 32-bit MSVC build emits this form as ``FF /2`` with mod=2.  Requiring
    the ModRM shape and exact displacement avoids matching MOV field accesses.
    """
    text_offset, text = image.section_bytes(".text")
    calls = []
    for index in range(len(text) - 5):
        if text[index] != 0xFF:
            continue
        modrm = text[index + 1]
        if (modrm & 0xC0) != 0x80 or (modrm & 0x38) != 0x10:
            continue
        if struct.unpack_from("<I", text, index + 2)[0] != displacement:
            continue
        call_va = image.offset_to_va(text_offset + index)
        if call_va is not None:
            calls.append(call_va)
    return calls


def validate_instruction_evidence(image, evidence):
    """Validate exact instruction spans and return report-safe rows."""
    rows = []
    for address, expected, contract in evidence:
        offset = image.va_to_offset(address)
        actual = (image.data[offset:offset + len(expected)]
                  if offset is not None else b"")
        if actual != expected:
            raise ValueError(
                "instruction evidence drift at 0x%x: %s != %s" %
                (address, actual.hex(), expected.hex())
            )
        rows.append({
            "address": "0x%x" % address,
            "bytes": expected.hex(),
            "contract": contract,
        })
    return rows


def decode_crouch_completion(image):
    """Materialize the exact repeated-request rule for unarmed crouch."""
    return {
        "activity": "ACT_CROUCH",
        "activity_id": 63,
        "sequence": "crouch",
        "sequence_index": 8,
        "model_flags": "non-looping",
        "result": "restart_same_sequence_after_completion",
        "policy": (
            "StudioFrameAdvance marks the one-shot finished; the next identical "
            "ordinary request reselects sequence 8 and ResetSequenceInfo clears "
            "cycle and the finished flag, so a sustained hold repeats crouch"
        ),
        "instruction_evidence": validate_instruction_evidence(
            image, CROUCH_RESTART_EVIDENCE),
    }


def recover_call_surface(image):
    """Validate and materialize all calls using the numeric +0x704 slot."""
    calls = set(indirect_calls_with_displacement(image, SET_ANIMATION_SLOT))
    expected = set(PLAYER_ACTION_CALLS) | NON_ACTION_SLOT_CALLS
    unknown = sorted(calls - expected)
    missing = sorted(expected - calls)
    if unknown or missing:
        raise ValueError("+0x704 call surface drift: unknown=%s missing=%s" %
                         (["0x%x" % value for value in unknown],
                          ["0x%x" % value for value in missing]))
    rows = []
    for call_site in sorted(PLAYER_ACTION_CALLS):
        function, producer, code, second = PLAYER_ACTION_CALLS[call_site]
        rows.append({
            "call_site": "0x%x" % call_site,
            "function": function,
            "producer": producer,
            "action_code": code,
            "action_name": (ACTION_POLICIES[code]["expected_name"]
                            if code is not None else "dynamic"),
            "second_argument": second,
        })
    return rows, sorted("0x%x" % value for value in NON_ACTION_SLOT_CALLS)


def scan_player_anim_vdata(files):
    """Return every targeted-discipline ``Player_Anim`` value in byte files."""
    rows = []
    for path, data in files:
        for match in PLAYER_ANIM_RE.finditer(data or b""):
            rows.append({
                "path": path,
                "value": match.group(1).decode("latin-1"),
                "byte_offset": match.start(1),
            })
    return rows


def survey_patch_first_vdata(index=None):
    """Survey the effective vdata search path, with loose patch files winning."""
    from elysium_pipeline.formats import install

    if index is None:
        index = install.build_index(("vdata",), verbose=False)
    files = []
    for key in sorted(index):
        if key.startswith("vdata/"):
            files.append((key, install.read(index, key)))
    return scan_player_anim_vdata(files)


def decode_player_classes(image):
    """Prove the player family shares one protected/router/ordinary surface."""
    # Import lazily so unit tests for the byte decoders do not require a game
    # path merely to import this module.
    from research.tooling.probes.animation_event_survey import find_descendant_classes

    rows = find_descendant_classes(image, "CBasePlayer")
    decoded = []
    for row in rows:
        slots = {}
        for name, slot in PLAYER_SLOTS.items():
            entry = image.read_u32_va(row["vtable_va"] + slot)
            if entry is None:
                raise ValueError("missing player slot 0x%x on %s" %
                                 (slot, row["cpp_class"]))
            slots[name] = "0x%x" % follow_jump(image, entry)
        decoded.append({
            "cpp_class": row["cpp_class"],
            "direct_base": row["direct_base"],
            "vtable_va": "0x%x" % row["vtable_va"],
            "functions": slots,
        })
    expected = {
        "protected_owner": "0x10161200",
        "ordinary_selector": "0x10164870",
        "set_animation_router": "0x10164240",
        "special_owner_inventory_forward": "0x10170b50",
        "special_owner_inventory_receiver": "0x100b7fe0",
    }
    if len(decoded) != 2 or any(row["functions"] != expected for row in decoded):
        raise ValueError("player RTTI/animation override surface drifted: %r" % decoded)
    return decoded


def build_report(binary_path=None, vdata_index=None):
    if binary_path is None:
        binary_path = vtmb_root() / "Vampire" / "dlls" / "vampire.dll"
    binary_path = os.path.abspath(os.fspath(binary_path))
    with open(binary_path, "rb") as handle:
        data = handle.read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != PINNED_SHA256:
        raise ValueError("unsupported vampire.dll SHA-256 %s" % digest)
    image = PEImage(data)
    actions = decode_action_names(image)
    calls, non_action_calls = recover_call_surface(image)
    player_classes = decode_player_classes(image)
    crouch_completion = decode_crouch_completion(image)
    vdata = survey_patch_first_vdata(vdata_index)
    by_name = {row["expected_name"]: code for code, row in ACTION_POLICIES.items()}
    for row in vdata:
        if row["value"] not in by_name:
            raise ValueError("unknown discipline Player_Anim %s in %s" %
                             (row["value"], row["path"]))
        row["action_code"] = by_name[row["value"]]
    unreachable = [row["code"] for row in actions
                   if row["reachability"] != "reachable"]
    return {
        "binary": {
            "path": binary_path,
            "size": len(data),
            "sha256": digest,
            "image_base": "0x%x" % image.image_base,
        },
        "addresses": {
            "action_name_table": "0x%x" % PLAYER_ACTION_NAMES,
            "classifier": "0x1016bb50",
            "set_animation_router": "0x10164240",
            "ordinary_selector": "0x10164870",
            "postthink": "0x1016be10",
            "discipline_player_anim_parser": "0x101ddfb0",
            "discipline_action_executor": "0x101de660",
        },
        "actions": actions,
        "player_classes": player_classes,
        "crouch_completion": crouch_completion,
        "set_animation_calls": calls,
        "non_action_calls_at_same_slot": non_action_calls,
        "patch_first_player_anim_values": vdata,
        "retained_action": {
            "latch_offset": "0x1cb0",
            "code_offset": "0x1cb4",
            "only_latched_code": 12,
            "policy": "classifier clears any latched code other than PLAYER_VOMIT; constructor/spawn clear the latch and the vomit owner is its only setter",
        },
        "summary": {
            "action_codes": len(actions),
            "reachable_codes": len(actions) - len(unreachable),
            "unreachable_codes": unreachable,
            "set_animation_calls": len(calls),
            "non_action_same_slot_calls": len(non_action_calls),
            "patch_first_player_anim_records": len(vdata),
            "patch_first_player_anim_codes": dict(collections.Counter(
                str(row["action_code"]) for row in vdata)),
            "player_classes": len(player_classes),
            "crouch_completion_closed": True,
        },
    }


def print_report(report):
    summary = report["summary"]
    print("=" * 78)
    print("VtMB player compact-action survey")
    print("=" * 78)
    print("binary: %s" % report["binary"]["path"])
    print("sha256: %s" % report["binary"]["sha256"])
    print("codes: %(action_codes)d | reachable: %(reachable_codes)d | "
          "SetAnimation calls: %(set_animation_calls)d | same-slot non-actions: "
          "%(non_action_same_slot_calls)d" % summary)
    for row in report["actions"]:
        print("  %2d  %-22s %s" %
              (row["code"], row["expected_name"], row["reachability"]))
    print("patch-first Player_Anim rows: %d" %
          summary["patch_first_player_anim_records"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", help="override the pinned retail vampire.dll path")
    parser.add_argument("--json", help="write the generated ledger here")
    args = parser.parse_args()
    report = build_report(args.binary)
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
