# -*- coding: utf-8 -*-
"""Decode VtMB's attack2/weapon-secondary client-to-server button path.

The pinned client registers two press/release pairs. ``+attack2`` owns the
ordinary attack2 ``kbutton_t``. ``+wpn_secondaryatk`` owns a second button and
then deliberately forwards into the same attack2 handler. The client button
packer emits the second button as ``0x08000000``; the pinned server has one
direct ``player+0x2088`` test for that mask, the grounded weapon-eligible block
predicate which produces ``PLAYER_BLOCK``.

Read-only unless ``--json`` is supplied. Generated reports are binary-derived
and belong below ``$ELYSIUM_WORK_ROOT/research``; never commit them.
"""
from __future__ import print_function

import argparse
import hashlib
import json
import os

from elysium_pipeline.paths import vtmb_root
from research.tooling.probes.player_action_survey import (
    validate_instruction_evidence,
)
from research.tooling.probes.weapon_activity_survey import PEImage, PINNED_SHA256


CLIENT_SHA256 = "e88beae0dd03af06493c71c5e8d87a6993b54e590cb6ad37cd3513c588582870"
BLOCK_BUTTON_MASK = 0x08000000
BLOCK_MASK_TEST = b"\xf7\x86\x88\x20\x00\x00\x00\x00\x00\x08"


CLIENT_EVIDENCE = (
    (0x10105090,
     bytes.fromhex("6a 00 6a 00 6a 00 68 30 21 10 10 68 d8 db 2b 10 b9 5c 35 4d 10 e8 86 a3 fd ff c3"),
     "+attack2 registers press handler 0x10102130"),
    (0x101050E0,
     bytes.fromhex("6a 00 6a 00 6a 00 68 40 21 10 10 68 e4 db 2b 10 b9 ec 2e 4d 10 e8 36 a3 fd ff c3"),
     "-attack2 registers release handler 0x10102140"),
    (0x10105CC0,
     bytes.fromhex("6a 00 6a 00 6a 00 68 e0 24 10 10 68 58 dd 2b 10 b9 48 2f 4d 10 e8 56 97 fd ff c3"),
     "+wpn_secondaryatk registers composite press handler 0x101024e0"),
    (0x10105D10,
     bytes.fromhex("6a 00 6a 00 6a 00 68 f0 24 10 10 68 6c dd 2b 10 b9 20 37 4d 10 e8 06 97 fd ff c3"),
     "-wpn_secondaryatk registers composite release handler 0x101024f0"),
    (0x10102130,
     bytes.fromhex("6a 00 68 a0 2d 4d 10 e8 e4 fc ff ff 83 c4 08 c3"),
     "+attack2 presses kbutton 0x104d2da0"),
    (0x10102140,
     bytes.fromhex("6a 00 68 a0 2d 4d 10 e8 54 fd ff ff 83 c4 08 c3"),
     "-attack2 releases kbutton 0x104d2da0"),
    (0x10102250,
     bytes.fromhex("6a 00 68 e0 36 4d 10 e8 c4 fb ff ff 83 c4 08 c3"),
     "weapon-secondary presses block kbutton 0x104d36e0"),
    (0x10102260,
     bytes.fromhex("6a 00 68 e0 36 4d 10 e8 34 fc ff ff 83 c4 08 c3"),
     "weapon-secondary releases block kbutton 0x104d36e0"),
    (0x101024E0,
     bytes.fromhex("e8 6b fd ff ff e9 46 fc ff ff"),
     "+wpn_secondaryatk presses the block button then jumps to +attack2"),
    (0x101024F0,
     bytes.fromhex("e8 6b fd ff ff e9 46 fc ff ff"),
     "-wpn_secondaryatk releases the block button then jumps to -attack2"),
    (0x10104600,
     bytes.fromhex("8d 54 24 08 53 68 a0 2d 4d 10 51 68 00 00 00 01 68 00 08 00 00 52 e8 15 19 00 00"),
     "client packs attack2 held as 0x800 and its press edge as 0x01000000"),
    (0x10104638,
     bytes.fromhex("a1 38 3e 4d 10 53 68 e0 36 4d 10 50 50 8d 54 24 48 68 00 00 00 08 52 e8 dc 18 00 00"),
     "client packs the weapon-secondary block kbutton as held bit 0x08000000"),
)


SERVER_EVIDENCE = (
    (0x10160EC0,
     bytes.fromhex("56 8b f1 e8 bc 7c ea ff a8 01 74 69 8b ce e8 46 6f ea ff 85 c0 74 11 8b 10 8b c8 ff 92 a0 05 00 00 a9 00 80 01 00 74 4d f7 86 88 20 00 00 00 00 00 08"),
     "block predicate requires ground, a weapon capability in 0x18000 and player button 0x08000000"),
    (0x1016BC28,
     bytes.fromhex("8b ce e8 4e 76 ea ff 84 c0 74 07 b8 0d 00 00 00"),
     "the classifier maps a true block predicate to PLAYER_BLOCK code 13"),
)


def count_pattern(image, pattern):
    """Count exact byte-pattern occurrences in the PE text section."""
    _offset, text = image.section_bytes(".text")
    count = 0
    cursor = 0
    while True:
        cursor = text.find(pattern, cursor)
        if cursor < 0:
            return count
        count += 1
        cursor += 1


def _load(path, digest):
    path = os.path.abspath(os.fspath(path))
    with open(path, "rb") as handle:
        data = handle.read()
    actual = hashlib.sha256(data).hexdigest()
    if actual != digest:
        raise ValueError("unsupported %s SHA-256 %s" % (path, actual))
    return path, data, PEImage(data), actual


def build_report(client_path=None, server_path=None):
    root = vtmb_root() / "Vampire"
    if client_path is None:
        client_path = root / "cl_dlls" / "client.dll"
    if server_path is None:
        server_path = root / "dlls" / "vampire.dll"
    client_path, client_data, client, client_hash = _load(
        client_path, CLIENT_SHA256)
    server_path, server_data, server, server_hash = _load(
        server_path, PINNED_SHA256)
    block_tests = count_pattern(server, BLOCK_MASK_TEST)
    if block_tests != 1:
        raise ValueError("server block-button direct-test surface drifted: %d" %
                         block_tests)
    names = {}
    for name, address in (
        ("attack2_press", 0x102BDBD8),
        ("attack2_release", 0x102BDBE4),
        ("weapon_secondary_press", 0x102BDD58),
        ("weapon_secondary_release", 0x102BDD6C),
    ):
        names[name] = client.read_cstring_va(address)
    expected_names = {
        "attack2_press": "+attack2",
        "attack2_release": "-attack2",
        "weapon_secondary_press": "+wpn_secondaryatk",
        "weapon_secondary_release": "-wpn_secondaryatk",
    }
    if names != expected_names:
        raise ValueError("client command-name surface drifted: %r" % names)
    return {
        "client": {
            "path": client_path,
            "size": len(client_data),
            "sha256": client_hash,
        },
        "server": {
            "path": server_path,
            "size": len(server_data),
            "sha256": server_hash,
        },
        "command_names": names,
        "client_instruction_evidence": validate_instruction_evidence(
            client, CLIENT_EVIDENCE),
        "server_instruction_evidence": validate_instruction_evidence(
            server, SERVER_EVIDENCE),
        "mapping": {
            "+attack2": {
                "held_button": "0x00000800",
                "press_edge": "0x01000000",
                "block_button": False,
            },
            "+wpn_secondaryatk": {
                "held_button": "0x00000800",
                "press_edge": "0x01000000",
                "additional_held_button": "0x08000000",
                "block_button": True,
            },
            "server_block_result": {
                "compact_code": 13,
                "compact_name": "PLAYER_BLOCK",
                "base_activity": "ACT_PREBLOCK",
                "policy": "held +wpn_secondaryatk, grounded, active weapon capability mask intersects 0x18000",
            },
        },
        "summary": {
            "command_pairs": 2,
            "client_instruction_spans": len(CLIENT_EVIDENCE),
            "server_instruction_spans": len(SERVER_EVIDENCE),
            "direct_server_block_mask_tests": block_tests,
            "block_command": "+wpn_secondaryatk",
        },
    }


def print_report(report):
    print("=" * 78)
    print("VtMB input-action survey")
    print("=" * 78)
    print("client: %s" % report["client"]["path"])
    print("server: %s" % report["server"]["path"])
    print("block command: %s" % report["summary"]["block_command"])
    print("+attack2: attack2 only")
    print("+wpn_secondaryatk: block 0x08000000 plus attack2")
    print("server result: PLAYER_BLOCK -> ACT_PREBLOCK")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", help="override pinned client.dll path")
    parser.add_argument("--server", help="override pinned vampire.dll path")
    parser.add_argument("--json", help="write the generated ledger here")
    args = parser.parse_args()
    report = build_report(args.client, args.server)
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
