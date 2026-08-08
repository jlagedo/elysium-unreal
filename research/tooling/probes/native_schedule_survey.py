# -*- coding: utf-8 -*-
"""Decode native AI schedule/action demand from the retail server DLL.

VtMB stores its compiled AI schedule descriptions as null-terminated ASCII in
``vampire.dll``.  Each description names the schedule, its ordered task list,
task arguments, interrupt conditions and schedule flags.  This probe extracts
that producer surface without committing the game-derived strings.

Explicit ``ACTIVITY:ACT_*`` task arguments are joined to the generated NPC
manifest.  The join reports *direct* model/bank availability only: class and
weapon translation, disposition selection and the runtime fallback chain can
still turn a directly absent request into a valid final sequence.

Read-only unless ``--json`` is supplied.  Generated reports belong below
``$ELYSIUM_WORK_ROOT/research`` and must not be committed.

Usage::

    uv run elysium research native_schedule_survey
    uv run elysium research native_schedule_survey --json <external-path>
"""
from __future__ import print_function

import argparse
import collections
import hashlib
import json
import os
import re

from elysium_pipeline.paths import export_root, vtmb_root


SCHEDULE_RE = re.compile(
    r"\bSchedule\s+(\S+)\s+Tasks\b(.*?)\bInterrupts\b(.*)", re.DOTALL)
TASK_RE = re.compile(r"\bTASK_[A-Z0-9_]+\b")
ACTIVITY_RE = re.compile(r"\bACTIVITY:(ACT_[A-Z0-9_]+)\b")
CONDITION_RE = re.compile(r"\bCOND_[A-Z0-9_]+\b")


def _ascii_byte(value):
    return value in (9, 10, 13) or 32 <= value < 127


def iter_ascii_strings(data, minimum=4):
    """Yield ``(file_offset, text)`` for null-terminated printable strings."""
    start = 0
    for end, value in enumerate(bytearray(data)):
        if value == 0:
            raw = data[start:end]
            if len(raw) >= minimum and all(_ascii_byte(item) for item in bytearray(raw)):
                yield start, raw.decode("ascii")
            start = end + 1
        elif not _ascii_byte(value):
            start = end + 1


def _compact(value):
    return re.sub(r"\s+", " ", value).strip()


def parse_schedule(text, file_offset=0):
    """Parse one compiled schedule string, or return ``None`` when it is unrelated."""
    match = SCHEDULE_RE.search(text)
    if not match or "TASK_" not in match.group(2):
        return None

    name, task_body, tail = match.groups()
    task_matches = list(TASK_RE.finditer(task_body))
    tasks = []
    for index, task_match in enumerate(task_matches):
        end = task_matches[index + 1].start() if index + 1 < len(task_matches) else len(task_body)
        argument = _compact(task_body[task_match.end():end])
        tasks.append({
            "task": task_match.group(0),
            "argument": argument,
            "activities": ACTIVITY_RE.findall(argument),
        })

    interrupt_text, separator, flag_text = tail.partition("Flags")
    return {
        "name": name,
        "file_offset": file_offset,
        "tasks": tasks,
        "interrupts": CONDITION_RE.findall(interrupt_text),
        "flags": _compact(flag_text).split() if separator else [],
    }


def decode_schedules(data):
    """Return all compiled schedule descriptions found in ``data``."""
    schedules = []
    for offset, text in iter_ascii_strings(data):
        if "Tasks" not in text or "Interrupts" not in text or "TASK_" not in text:
            continue
        schedule = parse_schedule(text, offset)
        if schedule is not None:
            schedules.append(schedule)
    schedules.sort(key=lambda row: (row["file_offset"], row["name"]))
    return schedules


def _clip_metadata(manifest, npc_stem, owner, label):
    banks = manifest.get("banks", {})
    npcs = manifest.get("npcs", {})
    if owner in banks:
        return banks[owner].get("clips", {}).get(label, {})
    if owner == npc_stem:
        return npcs.get(npc_stem, {}).get("own_clips", {}).get(label, {})
    return npcs.get(owner, {}).get("own_clips", {}).get(label, {})


def manifest_activity_index(manifest):
    """Map a direct activity name to the NPC model clips that declare it."""
    result = collections.defaultdict(lambda: collections.defaultdict(list))
    for stem, npc in manifest.get("npcs", {}).items():
        for label, owner in npc.get("clips", {}).items():
            metadata = _clip_metadata(manifest, stem, owner, label)
            activity = str(metadata.get("activity", ""))
            if not activity:
                continue
            result[activity][stem].append({
                "label": label,
                "owner": owner,
                "weight": metadata.get("weight", 0),
                "flags": metadata.get("flags", 0),
                "frames": metadata.get("frames", 0),
                "fps": metadata.get("fps", 0),
            })
    return result


def join_explicit_activities(schedules, manifest_path):
    requested = collections.Counter()
    provenance = collections.defaultdict(list)
    for schedule in schedules:
        for ordinal, task in enumerate(schedule["tasks"]):
            for activity in task["activities"]:
                requested[activity] += 1
                provenance[activity].append({
                    "schedule": schedule["name"],
                    "task": task["task"],
                    "task_ordinal": ordinal,
                })

    result = {
        "available": False,
        "path": os.fspath(manifest_path),
        "requests": [],
    }
    try:
        with open(manifest_path, "r", errors="replace") as handle:
            manifest = json.load(handle)
    except (OSError, ValueError):
        for activity, count in requested.most_common():
            result["requests"].append({
                "activity": activity,
                "references": count,
                "provenance": provenance[activity],
                "direct_status": "manifest_unavailable",
            })
        return result

    activity_index = manifest_activity_index(manifest)
    result.update({
        "available": True,
        "manifest_version": manifest.get("manifest_version"),
        "npc_models": len(manifest.get("npcs", {})),
    })
    for activity, count in requested.most_common():
        model_clips = activity_index.get(activity, {})
        models = []
        for stem, clips in sorted(model_clips.items()):
            models.append({
                "stem": stem,
                "model": manifest["npcs"][stem].get("model", ""),
                "clips": clips,
            })
        result["requests"].append({
            "activity": activity,
            "references": count,
            "provenance": provenance[activity],
            "direct_status": "available" if models else "translation_or_fallback_required",
            "direct_model_count": len(models),
            "direct_clip_count": sum(len(row["clips"]) for row in models),
            "models": models,
        })
    return result


def summarize(schedules, activity_join):
    task_counts = collections.Counter()
    activity_counts = collections.Counter()
    activity_by_task = collections.Counter()
    for schedule in schedules:
        for task in schedule["tasks"]:
            task_counts[task["task"]] += 1
            for activity in task["activities"]:
                activity_counts[activity] += 1
                activity_by_task[task["task"]] += 1
    status_counts = collections.Counter(
        row["direct_status"] for row in activity_join["requests"])
    return {
        "schedules": len(schedules),
        "task_invocations": sum(task_counts.values()),
        "distinct_tasks": len(task_counts),
        "explicit_activity_references": sum(activity_counts.values()),
        "distinct_explicit_activities": len(activity_counts),
        "task_counts": dict(task_counts.most_common()),
        "explicit_activity_by_task": dict(activity_by_task.most_common()),
        "activity_counts": dict(activity_counts.most_common()),
        "direct_activity_status": dict(status_counts.most_common()),
    }


def build_report(binary_path=None, manifest_path=None):
    if binary_path is None:
        binary_path = vtmb_root() / "Vampire" / "dlls" / "vampire.dll"
    if manifest_path is None:
        manifest_path = export_root() / "npc" / "npc_manifest.json"
    binary_path = os.path.abspath(os.fspath(binary_path))
    with open(binary_path, "rb") as handle:
        data = handle.read()
    schedules = decode_schedules(data)
    activity_join = join_explicit_activities(schedules, manifest_path)
    report = {
        "binary": {
            "path": binary_path,
            "size": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
        },
        "schedules": schedules,
        "activity_manifest": activity_join,
    }
    report["summary"] = summarize(schedules, activity_join)
    return report


def print_report(report):
    summary = report["summary"]
    print("=" * 78)
    print("VtMB native schedule / action demand")
    print("=" * 78)
    print("binary: %s" % report["binary"]["path"])
    print("sha256: %s" % report["binary"]["sha256"])
    print("schedules: %(schedules)d | task invocations: %(task_invocations)d | "
          "distinct tasks: %(distinct_tasks)d" % summary)
    print("explicit activity references: %(explicit_activity_references)d | "
          "distinct activities: %(distinct_explicit_activities)d" % summary)
    print()
    print("Explicit activity producers")
    for task, count in summary["explicit_activity_by_task"].items():
        print("  %-42s %5d" % (task, count))
    print()
    manifest = report["activity_manifest"]
    if manifest["available"]:
        print("Direct npc_manifest v%s availability across %d NPC models" %
              (manifest["manifest_version"], manifest["npc_models"]))
        for status, count in summary["direct_activity_status"].items():
            print("  %-42s %5d" % (status, count))
        print("  (direct absence still enters class/weapon translation and fallback)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", help="override the retail vampire.dll path")
    parser.add_argument("--manifest", help="override npc_manifest.json")
    parser.add_argument("--json", help="write the complete generated ledger here")
    args = parser.parse_args()
    report = build_report(args.binary, args.manifest)
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
