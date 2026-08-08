# -*- coding: utf-8 -*-
"""Decode VtMB's player paired-action modes and recovered producer surface.

The pinned ``vampire.dll`` maps a compact grapple mode through
``CBaseCombatCharacter::GetInitialGrappleActivity`` before the player router
advances the paired action.  This probe decodes the nine-entry jump table,
verifies every direct ``StartGrappleAttack`` call in the server, attaches the
recovered continuation leaf, and inventories executable Python
``SeductiveFeed`` calls in the exported corpus.

Read-only unless ``--json`` is supplied.  The generated ledger is game-derived
and belongs below ``$ELYSIUM_WORK_ROOT/research``; never commit it.

Usage::

    uv run elysium research player_grapple_survey
    uv run elysium research player_grapple_survey --json <external-path>
"""
from __future__ import print_function

import argparse
import glob
import hashlib
import json
import os
import struct

from elysium_pipeline.paths import export_root, vtmb_root
from research.tooling.probes.action_animation_survey import scan_python_text
from research.tooling.probes.weapon_activity_survey import (
    PEImage,
    PINNED_SHA256,
    decode_activity_registry,
)


INITIAL_ACTIVITY_TABLE = 0x10328D74
START_GRAPPLE_THUNK = 0x1000DF99
MODE_META = {
    0: ("feeding", "0x101655b0", "stateful", ["0x10168442"]),
    1: ("feeding_alias", "0x101655b0", "stateful", []),
    2: ("seductive_feeding", "0x10165ae0", "stateful", ["0x10198266"]),
    3: ("sneakattack_success", "0x10165d90", "completion_effect", ["0x101673a8"]),
    4: ("sneakattack_failure", "sequence_finished", "single_shot", []),
    5: ("payphone", "0x101654e0", "stateful", ["0x10178350"]),
    6: ("rat_feeding", "0x10165330", "stateful", ["0x10168442"]),
    7: ("finishing_move", "sequence_finished", "single_shot", []),
    8: ("zombie_feeding", "0x10165f20", "stateful", ["0x101687b0"]),
}
PRODUCER_CALLS = {
    0x101673A8: {
        "function": "0x10167370",
        "producer": "player_use_sneak_target",
        "modes": [3],
    },
    0x10168442: {
        "function": "0x10168320",
        "producer": "player_replenish_feed_target",
        "modes": [0, 6],
    },
    0x101687B0: {
        "function": "0x10168700",
        "producer": "player_be_fed_on_by_zombie",
        "modes": [8],
    },
    0x10178350: {
        "function": "0x10178280",
        "producer": "player_payphone_dialogue",
        "modes": [5],
    },
    0x10198266: {
        "function": "0x10198150",
        "producer": "python_seductive_feed",
        "modes": [2],
    },
}


def mov_eax_immediate(image, address, limit=16):
    """Return the first ``mov eax, imm32`` constant at one jump-table leaf."""
    offset = image.va_to_offset(address)
    if offset is None:
        raise ValueError("unmapped grapple jump-table target 0x%x" % address)
    body = image.data[offset:offset + limit]
    for index in range(max(0, len(body) - 4)):
        if body[index] == 0xB8:
            return struct.unpack_from("<I", body, index + 1)[0]
    raise ValueError("grapple jump-table target 0x%x has no mov eax immediate" % address)


def decode_player_grapple_modes(image, registrations):
    """Decode all nine initial activities and attach recovered router metadata."""
    by_id = {row["id"]: row["name"] for row in registrations}
    modes = []
    for mode in range(9):
        target = image.read_u32_va(INITIAL_ACTIVITY_TABLE + mode * 4)
        if target is None:
            raise ValueError("missing player grapple jump-table entry %d" % mode)
        activity_id = mov_eax_immediate(image, target)
        if activity_id not in by_id:
            raise ValueError("unknown initial grapple activity 0x%x" % activity_id)
        name, leaf, policy, call_sites = MODE_META[mode]
        modes.append({
            "mode": mode,
            "name": name,
            "jump_target": "0x%x" % target,
            "initial_activity_id": activity_id,
            "initial_activity": by_id[activity_id],
            "continuation_leaf": leaf,
            "continuation_policy": policy,
            "producer_call_sites": list(call_sites),
            "has_pinned_binary_producer": bool(call_sites),
        })
    return modes


def direct_relative_calls(image, target):
    """Find every direct near call whose resolved target equals ``target``."""
    text_offset, text = image.section_bytes(".text")
    calls = []
    for index in range(len(text) - 4):
        if text[index] != 0xE8:
            continue
        displacement = struct.unpack_from("<i", text, index + 1)[0]
        call_va = image.offset_to_va(text_offset + index)
        if call_va is not None and call_va + 5 + displacement == target:
            calls.append(call_va)
    return calls


def recover_producers(image):
    """Validate and describe the complete direct StartGrappleAttack caller set."""
    call_sites = direct_relative_calls(image, START_GRAPPLE_THUNK)
    unknown = sorted(set(call_sites) - set(PRODUCER_CALLS))
    missing = sorted(set(PRODUCER_CALLS) - set(call_sites))
    if unknown or missing:
        raise ValueError("StartGrappleAttack caller drift: unknown=%s missing=%s" %
                         (["0x%x" % value for value in unknown],
                          ["0x%x" % value for value in missing]))
    rows = []
    for call_site in sorted(call_sites):
        row = dict(PRODUCER_CALLS[call_site])
        row["call_site"] = "0x%x" % call_site
        rows.append(row)
    return rows


def survey_seductive_feed_calls(root):
    """Return executable exported Python calls to the mode-2 binding."""
    rows = []
    pattern = os.path.join(os.fspath(root), "scripts", "**", "*.py")
    for path in sorted(glob.glob(pattern, recursive=True)):
        if os.sep + "lib" + os.sep in path:
            continue
        with open(path, "r", encoding="latin-1", errors="replace") as handle:
            scanned = scan_python_text(
                handle.read(), "py", os.path.relpath(path, root).replace("\\", "/"))
        rows.extend(row for row in scanned if row["call"] == "SeductiveFeed")
    return rows


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
    modes = decode_player_grapple_modes(image, decode_activity_registry(image))
    producers = recover_producers(image)
    python_calls = survey_seductive_feed_calls(root)
    return {
        "binary": {
            "path": binary_path,
            "size": len(data),
            "sha256": digest,
            "image_base": "0x%x" % image.image_base,
        },
        "addresses": {
            "get_initial_activity": "0x10328c80",
            "initial_activity_table": "0x%x" % INITIAL_ACTIVITY_TABLE,
            "start_grapple_attack": "0x10328df0",
            "enter_grapple": "0x10329760",
            "set_grapple_activity": "0x1032a100",
            "end_grapple": "0x10329560",
            "player_router": "0x10164240",
        },
        "modes": modes,
        "native_and_binding_producers": producers,
        "current_python_seductive_feed_calls": python_calls,
        "summary": {
            "modes": len(modes),
            "modes_with_pinned_binary_producer": sum(
                int(row["has_pinned_binary_producer"]) for row in modes),
            "modes_without_pinned_binary_producer": [
                row["mode"] for row in modes
                if not row["has_pinned_binary_producer"]
            ],
            "start_grapple_call_sites": len(producers),
            "current_python_seductive_feed_calls": len(python_calls),
        },
    }


def print_report(report):
    summary = report["summary"]
    print("=" * 78)
    print("VtMB player paired-action survey")
    print("=" * 78)
    print("binary: %s" % report["binary"]["path"])
    print("sha256: %s" % report["binary"]["sha256"])
    print("modes: %(modes)d | with producer: %(modes_with_pinned_binary_producer)d | "
          "StartGrappleAttack calls: %(start_grapple_call_sites)d" % summary)
    for row in report["modes"]:
        producer = "producer" if row["has_pinned_binary_producer"] else "no caller"
        print("  %d  %-34s %-18s %s" % (
            row["mode"], row["initial_activity"], row["continuation_policy"], producer))
    print("current executable Python SeductiveFeed calls: %d" %
          summary["current_python_seductive_feed_calls"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", help="override the pinned retail vampire.dll path")
    parser.add_argument("--root", help="override the exported corpus root")
    parser.add_argument("--json", help="write the generated ledger here")
    args = parser.parse_args()
    report = build_report(args.binary, args.root)
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
