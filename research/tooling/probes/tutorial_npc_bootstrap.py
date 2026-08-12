"""Compare the retail and patch-first ``sp_tutorial_1`` character bootstrap.

The generated ledger is game-derived evidence.  Write it below
``ELYSIUM_WORK_ROOT`` and never commit it.

Usage::

    uv run elysium research tutorial_npc_bootstrap
    uv run elysium research tutorial_npc_bootstrap --json <external-path>
"""
from __future__ import print_function

import argparse
import hashlib
import json
import os

from elysium_pipeline.formats import bsp, install
from research.tooling.probes.ent_survey import parse_entities, split_output


MAP_NAME = "sp_tutorial_1"
SCRIPT_RELATIVE = os.path.join("python", "tutorial", "tutorial.py")


def _sha256(path):
    with open(path, "rb") as handle:
        data = handle.read()
    return {"path": os.path.abspath(path), "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest()}


def _one(entity, key, default=""):
    values = entity.get(key, ())
    return values[0] if values else default


def _outputs(entity):
    rows = []
    ordinal = 0
    for output_name, values in entity.items():
        if not output_name.lower().startswith(("on", "out")):
            continue
        for raw in values:
            fields = split_output(raw)
            if fields is None:
                continue
            rows.append({
                "ordinal": ordinal,
                "output": output_name,
                "target": fields[0],
                "input": fields[1],
                "parameter": fields[2],
                "delay": fields[3],
                "times": fields[4],
                "python": fields[5],
                "extra": fields[6],
                "raw": raw,
            })
            ordinal += 1
    return rows


def _selected_keys(entity):
    names = (
        "targetname", "origin", "angles", "model", "stattemplate",
        "default_disposition", "default_camera", "StartHidden", "spawnflags",
        "use_interesting", "interesting_place_groups", "hint_groups",
        "dialogname", "additionalequipment", "alternateequipment", "base_gender",
        "player_reaction", "invincible", "no_alert_state", "usescript",
        "NPCType", "NPCTargetname", "NPCSquadname", "MaxNPCCount",
        "MaxLiveChildren", "SpawnFrequency", "StartDisabled", "Flag_StartDisabled",
        "Flag_InfChild", "InfChild",
    )
    return {name: list(entity[name]) for name in names if name in entity}


def _read_map(path):
    with open(path, "rb") as handle:
        raw = handle.read()
    text = bsp.read_lump(raw, 0).decode("ascii", "replace")
    return raw, parse_entities(text)


def _identity(index, entity):
    target = _one(entity, "targetname")
    if target:
        return target.lower()
    return "#%d:%s:%s" % (index, _one(entity, "classname"), _one(entity, "origin"))


def _variant(map_path, script_path):
    raw, entities = _read_map(map_path)
    direct = []
    makers = []
    autos = []
    incoming = {}
    character_names = set()

    for index, entity in enumerate(entities):
        classname = _one(entity, "classname")
        row = {
            "entity_index": index,
            "identity": _identity(index, entity),
            "classname": classname,
            "keys": _selected_keys(entity),
            "all_keys": {key: list(values) for key, values in entity.items()},
            "outputs": _outputs(entity),
        }
        if classname.lower() == "npc_maker":
            makers.append(row)
        elif classname.lower().startswith("npc_"):
            direct.append(row)
            target = _one(entity, "targetname")
            if target:
                character_names.add(target.lower())
        elif classname.lower() == "logic_auto":
            autos.append(row)

    for index, entity in enumerate(entities):
        producer = _identity(index, entity)
        for row in _outputs(entity):
            target = row["target"].lower()
            if target in character_names:
                incoming.setdefault(target, []).append({
                    "producer_entity_index": index,
                    "producer": producer,
                    "producer_classname": _one(entity, "classname"),
                    **row,
                })

    with open(script_path, "rb") as handle:
        script = handle.read()
    return {
        "provenance": {
            "bsp": {"path": os.path.abspath(map_path), "bytes": len(raw),
                    "sha256": hashlib.sha256(raw).hexdigest()},
            "script": {"path": os.path.abspath(script_path), "bytes": len(script),
                       "sha256": hashlib.sha256(script).hexdigest()},
        },
        "entity_count": len(entities),
        "direct_characters": direct,
        "npc_makers": makers,
        "logic_auto": autos,
        "incoming_character_outputs": incoming,
    }


def _named(rows):
    result = {}
    for row in rows:
        target = row["keys"].get("targetname", [])
        if target:
            result.setdefault(target[0].lower(), []).append(row)
    return result


def _diff_named(retail, patch):
    left = _named(retail["direct_characters"])
    right = _named(patch["direct_characters"])
    changed = []
    for name in sorted(set(left) & set(right)):
        if len(left[name]) == 1 and len(right[name]) == 1:
            if (left[name][0]["classname"] != right[name][0]["classname"]
                    or left[name][0]["all_keys"] != right[name][0]["all_keys"]):
                changed.append({"targetname": name,
                                "retail": left[name][0], "patch": right[name][0]})
    return {
        "retail_only": [row for name in sorted(set(left) - set(right)) for row in left[name]],
        "patch_only": [row for name in sorted(set(right) - set(left)) for row in right[name]],
        "changed": changed,
        "duplicate_targetnames": {
            "retail": {name: rows for name, rows in sorted(left.items()) if len(rows) != 1},
            "patch": {name: rows for name, rows in sorted(right.items()) if len(rows) != 1},
        },
    }


def build_report(root=None):
    if root is None:
        root = install.GAME_ROOT
    retail_root = os.path.join(root, "Vampire")
    patch_root = os.path.join(root, "Unofficial_Patch")
    retail_map = os.path.join(retail_root, "maps", MAP_NAME + ".bsp")
    patch_map = os.path.join(patch_root, "maps", MAP_NAME + ".bsp")
    retail_script = os.path.join(retail_root, SCRIPT_RELATIVE)
    patch_script = os.path.join(patch_root, SCRIPT_RELATIVE)
    user_cfg = os.path.join(patch_root, "cfg", "user.cfg")
    readme = os.path.join(root, "VTMBup-readme.txt")

    retail = _variant(retail_map, retail_script)
    patch = _variant(patch_map, patch_script)
    return {
        "map": MAP_NAME,
        "installation": {
            "root": os.path.abspath(root),
            "patch_user_cfg": _sha256(user_cfg),
            "patch_readme": _sha256(readme),
        },
        "retail": retail,
        "patch": patch,
        "named_character_diff": _diff_named(retail, patch),
    }


def print_report(report):
    print("%s retail/patch character bootstrap" % report["map"])
    for name in ("retail", "patch"):
        row = report[name]
        print("%-6s entities=%d direct_characters=%d makers=%d logic_auto=%d" % (
            name, row["entity_count"], len(row["direct_characters"]),
            len(row["npc_makers"]), len(row["logic_auto"])))
        print("       bsp=%s" % row["provenance"]["bsp"]["sha256"])
        print("       script=%s" % row["provenance"]["script"]["sha256"])
    diff = report["named_character_diff"]
    print("named diff: retail-only=%d patch-only=%d changed=%d" % (
        len(diff["retail_only"]), len(diff["patch_only"]), len(diff["changed"])))
    print("duplicate targetnames: retail=%d patch=%d" % (
        len(diff["duplicate_targetnames"]["retail"]),
        len(diff["duplicate_targetnames"]["patch"])))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", help="override the Bloodlines installation root")
    parser.add_argument("--json", help="write the game-derived ledger to this external path")
    args = parser.parse_args()
    report = build_report(args.root)
    print_report(report)
    if args.json:
        parent = os.path.dirname(os.path.abspath(args.json))
        if parent and not os.path.isdir(parent):
            os.makedirs(parent)
        with open(args.json, "w") as handle:
            json.dump(report, handle, indent=1, sort_keys=True)
        print("wrote %s" % os.path.abspath(args.json))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
