# -*- coding: utf-8 -*-
"""Survey VtMB model autolayers and pin their native composition policy.

The old v2531 autolayer payload is only a descriptor-relative array of local
sequence indices.  This probe walks every patch-first MDL, validates every
binding, and then hash-pins the two native facts the file cannot carry:

* client ``FUN_10089c40`` passes literal ``1.0f`` to ``FUN_10088e10`` for every
  model-declared autolayer;
* server ``CBaseAnimatingOverlay::SetLayer`` initializes the separate four-slot
  combat layer to weight ``0.1f`` with ``0.2f`` blend fields and playback rate
  ``1.0f``.

Generated JSON contains game-derived model and sequence names.  Keep it below
``$ELYSIUM_WORK_ROOT`` and never commit it.
"""
from __future__ import print_function

import argparse
import collections
import hashlib
import json
import os
import struct

from elysium_pipeline.formats import install, mdl_skel
from elysium_pipeline.paths import vtmb_root
from research.tooling.probes.animation_event_survey import (
    CLIENT_PINNED_SHA256,
    SEQDESC_STRIDE,
)
from research.tooling.probes.weapon_activity_survey import PEImage, PINNED_SHA256


CLIENT_AUTOLAYER_DISPATCH = 0x10089C40
CLIENT_AUTOLAYER_WEIGHT_PUSH = 0x1008A0CE
CLIENT_ACCUMULATOR_CALL = 0x1008A0E7
CLIENT_ACCUMULATOR = 0x10088E10
SERVER_SET_LAYER = 0x10099020


def _read_bytes(image, va, size):
    offset = image.va_to_offset(va)
    if offset is None or offset + size > len(image.data):
        raise ValueError("VA 0x%x is outside the image" % va)
    return image.data[offset:offset + size]


def _rel32_call_target(image, va):
    data = _read_bytes(image, va, 5)
    if data[0] != 0xE8:
        raise ValueError("expected CALL rel32 at 0x%x" % va)
    return va + 5 + struct.unpack_from("<i", data, 1)[0]


def decode_native_policy(server_binary, client_binary):
    """Validate and report the exact pinned autolayer/layer constants."""
    server = PEImage(server_binary)
    client = PEImage(client_binary)

    push = _read_bytes(client, CLIENT_AUTOLAYER_WEIGHT_PUSH, 5)
    if push[0] != 0x68 or struct.unpack_from("<I", push, 1)[0] != 0x3F800000:
        raise ValueError("client autolayer call no longer pushes literal 1.0f")
    target = _rel32_call_target(client, CLIENT_ACCUMULATOR_CALL)
    if target != CLIENT_ACCUMULATOR:
        raise ValueError("client autolayer accumulator moved to 0x%x" % target)

    expected = {
        0x1009905B: 0x3F800000,  # playback rate
        0x10099067: 0x3E4CCCCD,  # blend field A
        0x10099071: 0x3E4CCCCD,  # blend field B
        0x1009907B: 0x3DCCCCCD,  # layer weight
        0x10099085: 0x3F800000,  # secondary scale/state
    }
    observed = {}
    for va, value in sorted(expected.items()):
        actual = struct.unpack("<I", _read_bytes(server, va, 4))[0]
        if actual != value:
            raise ValueError(
                "server SetLayer constant at 0x%x is 0x%x, expected 0x%x" %
                (va, actual, value)
            )
        observed["0x%x" % va] = struct.unpack("<f", struct.pack("<I", actual))[0]

    return {
        "model_autolayers": {
            "dispatcher": "0x%x" % CLIENT_AUTOLAYER_DISPATCH,
            "accumulator": "0x%x" % CLIENT_ACCUMULATOR,
            "caller_weight": 1.0,
            "policy": "evaluate each target at the host cycle in authored order, then accumulate at full caller weight times the target animation's per-bone mask",
        },
        "combat_layers": {
            "set_layer": "0x%x" % SERVER_SET_LAYER,
            "slot_count": 4,
            "slot_stride": 0x30,
            "initial_cycle": 0.0,
            "initial_playback_rate": 1.0,
            "initial_weight": 0.1,
            "blend_fields": [0.2, 0.2],
            "observed_immediates": observed,
            "policy": "base pose first, then active slots in ascending array index; no priority sort and no server-side weight ramp",
        },
    }


def _valid_header(data):
    return len(data) >= 280 and data[:4] == b"IDST" and struct.unpack_from("<i", data, 4)[0] == 2531


def survey_models(index):
    """Return the complete patch-first model autolayer census."""
    histogram = collections.Counter()
    carriers = collections.Counter()
    records = []
    invalid = []
    models = 0
    sequences = 0
    entries = 0
    self_references = 0
    recursive_targets = 0
    target_with_activity = 0
    target_suffixes = collections.Counter()
    two_entry_order = collections.Counter()

    for key in sorted(value for value in index if value.endswith(".mdl")):
        data = install.read(index, key)
        if data is None or not _valid_header(data):
            invalid.append({"model": key, "reason": "missing or non-v2531 IDST"})
            continue
        models += 1
        count, base = struct.unpack_from("<ii", data, 272)
        if count < 0 or count > 100000 or base < 0 or base + count * SEQDESC_STRIDE > len(data):
            invalid.append({"model": key, "reason": "invalid sequence descriptor array"})
            continue
        sequences += count
        labels = []
        activities = []
        declared = []
        for sequence_index in range(count):
            descriptor = base + sequence_index * SEQDESC_STRIDE
            try:
                labels.append(mdl_skel._cstr_rel(data, descriptor, 0))
                activities.append(mdl_skel._cstr_rel(data, descriptor, 4))
            except (ValueError, struct.error):
                labels.append("")
                activities.append("")
            declared.append(struct.unpack_from("<i", data, descriptor + 660)[0])

        for sequence_index, amount in enumerate(declared):
            descriptor = base + sequence_index * SEQDESC_STRIDE
            if amount == 0:
                histogram[0] += 1
                continue
            if not 0 < amount <= mdl_skel._MAX_AUTOLAYERS:
                invalid.append({
                    "model": key,
                    "sequence_index": sequence_index,
                    "sequence": labels[sequence_index],
                    "declared": amount,
                    "reason": "implausible autolayer count",
                })
                continue
            targets = mdl_skel.read_autolayers(data, descriptor, count)
            if len(targets) != amount:
                invalid.append({
                    "model": key,
                    "sequence_index": sequence_index,
                    "sequence": labels[sequence_index],
                    "declared": amount,
                    "decoded": len(targets),
                    "reason": "invalid autolayer array or target",
                })
                continue

            histogram[amount] += 1
            carriers[key] += 1
            entries += len(targets)
            target_labels = [labels[target] for target in targets]
            for target in targets:
                if target == sequence_index:
                    self_references += 1
                if 0 < declared[target] <= mdl_skel._MAX_AUTOLAYERS:
                    recursive_targets += 1
                if activities[target]:
                    target_with_activity += 1
                lower = labels[target].lower()
                if lower.endswith("_delta"):
                    target_suffixes["delta"] += 1
                elif lower.endswith("_layer"):
                    target_suffixes["layer"] += 1
                else:
                    target_suffixes["other"] += 1
            if len(targets) == 2:
                kinds = []
                for label in target_labels:
                    lower = label.lower()
                    kinds.append("delta" if lower.endswith("_delta") else
                                 "layer" if lower.endswith("_layer") else "other")
                two_entry_order["->".join(kinds)] += 1
            records.append({
                "model": key,
                "sequence_index": sequence_index,
                "sequence": labels[sequence_index],
                "targets": target_labels,
            })

    return {
        "summary": {
            "v2531_models": models,
            "sequences": sequences,
            "hosts": len(records),
            "entries": entries,
            "carrier_models": len(carriers),
            "max_fanout": max([len(row["targets"]) for row in records] or [0]),
            "self_references": self_references,
            "recursive_targets": recursive_targets,
            "targets_with_activity": target_with_activity,
            "invalid_records": len(invalid),
        },
        "count_histogram": {str(key): value for key, value in sorted(histogram.items())},
        "target_suffixes": dict(sorted(target_suffixes.items())),
        "two_entry_order": dict(sorted(two_entry_order.items())),
        "carrier_models": dict(sorted(carriers.items())),
        "bindings": records,
        "invalid_records": invalid,
    }


def _load(path, digest, label):
    path = os.path.abspath(os.fspath(path))
    with open(path, "rb") as handle:
        data = handle.read()
    actual = hashlib.sha256(data).hexdigest()
    if actual != digest:
        raise ValueError("unsupported %s SHA-256 %s" % (label, actual))
    return path, data, actual


def build_report(server_path=None, client_path=None):
    root = vtmb_root() / "Vampire"
    server_path = server_path or root / "dlls" / "vampire.dll"
    client_path = client_path or root / "cl_dlls" / "client.dll"
    server_path, server_binary, server_hash = _load(
        server_path, PINNED_SHA256, "vampire.dll"
    )
    client_path, client_binary, client_hash = _load(
        client_path, CLIENT_PINNED_SHA256, "client.dll"
    )
    return {
        "binaries": {
            "server": {"path": server_path, "size": len(server_binary), "sha256": server_hash},
            "client": {"path": client_path, "size": len(client_binary), "sha256": client_hash},
        },
        "native_policy": decode_native_policy(server_binary, client_binary),
        "models": survey_models(install.build_index(("models",), verbose=False)),
    }


def print_report(report):
    summary = report["models"]["summary"]
    policy = report["native_policy"]
    print("=" * 78)
    print("VtMB autolayer / combat-layer surface")
    print("=" * 78)
    print("v2531 MDLs: %(v2531_models)d | sequences: %(sequences)d | hosts: %(hosts)d | entries: %(entries)d" % summary)
    print("carriers: %(carrier_models)d | max fanout: %(max_fanout)d | self: %(self_references)d | recursive: %(recursive_targets)d" % summary)
    print("targets with activity: %(targets_with_activity)d | invalid: %(invalid_records)d" % summary)
    print("autolayer caller weight: %.1f | combat SetLayer weight: %.1f" %
          (policy["model_autolayers"]["caller_weight"],
           policy["combat_layers"]["initial_weight"]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server-binary")
    parser.add_argument("--client-binary")
    parser.add_argument("--json")
    args = parser.parse_args()
    report = build_report(args.server_binary, args.client_binary)
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
