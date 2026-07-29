# -*- coding: utf-8 -*-
"""Survey every audio-facing seam in the currently exported VtMB corpus.

This is the audio-specific companion to ``ent_survey.py`` and
``script_api_survey.py``. It reads the regenerable, gitignored ``tools/out`` mirror and reports:

* map audio entities and their authored controls;
* Source I/O wires that drive sounds, schemes, and NPC audio overrides;
* environmental-audio room types;
* the class-specific meanings of the overloaded ``soundgroup`` key;
* executable audio-facing calls in the mirrored Python scripts.

Read-only unless ``--json`` is supplied. No game-derived data belongs in git.
"""
import argparse
import collections
import glob
import json
import os
import re


TOOLS = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(TOOLS, "out")

AUDIO_CLASSES = ("ambient_generic", "ambient_soundscheme", "trigger_environmental_audio")
AUDIO_INPUTS = {
    "PlaySound", "StopSound", "ToggleSound", "Volume", "FadeIn", "FadeOut",
    "SetFakeSilence", "SetSoundOverrideEnt",
}
PYTHON_AUDIO_METHODS = (
    "PlayDialogFile", "PlaySound", "StopSound", "ToggleSound", "Volume", "FadeIn",
    "FadeOut", "SetSafeArea", "SetSoundOverrideEnt", "SetFakeSilence", "Whisper",
)


def key_ci(keys, name, default=""):
    needle = name.casefold()
    for key, value in keys.items():
        if key.casefold() == needle:
            return value
    return default


def load_maps():
    maps = []
    for path in sorted(glob.glob(os.path.join(OUT, "*", "*.ents"))):
        try:
            root = json.load(open(path, "r", errors="replace"))
        except (OSError, ValueError):
            continue
        if isinstance(root, dict) and isinstance(root.get("entities"), list):
            maps.append(root)
    return maps


def resolve_targets(entities, target):
    query = str(target or "").casefold()
    if not query or query.startswith("!"):
        return []
    if query.endswith("*"):
        prefix = query[:-1]
        return [e for e in entities
                if str(e.get("targetname", "")).casefold().startswith(prefix)]
    return [e for e in entities if str(e.get("targetname", "")).casefold() == query]


def strip_python_comment(line):
    """Remove a Python comment while respecting single-line quoted strings."""
    quote = None
    escaped = False
    for index, char in enumerate(line):
        if escaped:
            escaped = False
            continue
        if char == "\\":
            escaped = True
            continue
        if quote:
            if char == quote:
                quote = None
            continue
        if char in ("'", '"'):
            quote = char
        elif char == "#":
            return line[:index]
    return line


def survey_python():
    counts = collections.Counter()
    sites = collections.defaultdict(list)
    files = [p for p in sorted(glob.glob(os.path.join(OUT, "scripts", "**", "*.py"),
                                         recursive=True))
             if os.sep + "lib" + os.sep not in p]
    patterns = {name: re.compile(r"\." + re.escape(name) + r"\s*\(")
                for name in PYTHON_AUDIO_METHODS}
    for path in files:
        rel = os.path.relpath(path, OUT).replace("\\", "/")
        for line_no, raw in enumerate(open(path, "r", encoding="latin-1", errors="replace"), 1):
            line = strip_python_comment(raw)
            for name, pattern in patterns.items():
                found = len(pattern.findall(line))
                if not found:
                    continue
                counts[name] += found
                if len(sites[name]) < 6:
                    sites[name].append("%s:%d" % (rel, line_no))
    return {"files": len(files), "calls": dict(counts), "sites": dict(sites)}


def survey_maps(maps):
    entity_counts = collections.Counter()
    map_rows = []
    ambient_extensions = collections.Counter()
    ambient_spawnflags = collections.Counter()
    ambient_key_presence = collections.Counter()
    ambient_nonzero = collections.Counter()
    scheme_start = collections.Counter()
    environmental_rooms = collections.Counter()
    soundgroup_classes = collections.Counter()
    soundgroups = collections.defaultdict(collections.Counter)
    routes = collections.Counter()
    route_sources = collections.Counter()
    unresolved = []
    music_outputs = collections.Counter()
    total_entities = 0

    for root in maps:
        map_name = root.get("map", "<unknown>")
        entities = root["entities"]
        total_entities += len(entities)
        per_map = collections.Counter()

        for ent in entities:
            classname = str(ent.get("classname", ""))
            class_lc = classname.casefold()
            keys = ent.get("keys", {})
            if class_lc in AUDIO_CLASSES:
                entity_counts[class_lc] += 1
                per_map[class_lc] += 1

            if class_lc == "ambient_generic":
                message = str(key_ci(keys, "message"))
                ambient_extensions[os.path.splitext(message)[1].casefold() or "<none>"] += 1
                ambient_spawnflags[str(key_ci(keys, "spawnflags", "0"))] += 1
                for key, value in keys.items():
                    ambient_key_presence[key] += 1
                    if str(value).strip() not in ("", "0", "0.0"):
                        ambient_nonzero[key] += 1
            elif class_lc == "ambient_soundscheme":
                enabled = str(key_ci(keys, "start_enabled", "0")).strip() not in ("", "0", "0.0")
                scheme_start["enabled" if enabled else "disabled"] += 1
            elif class_lc == "trigger_environmental_audio":
                environmental_rooms[str(key_ci(keys, "room_type", "<missing>"))] += 1

            group = str(key_ci(keys, "soundgroup", "")).strip()
            if group:
                soundgroup_classes[classname] += 1
                soundgroups[classname][group] += 1

            for output in ent.get("outputs", []):
                input_name = str(output.get("input", ""))
                if classname.casefold() == "events_world" and "music" in str(
                        output.get("name", "")).casefold():
                    music_outputs[str(output.get("name", ""))] += 1
                if input_name not in AUDIO_INPUTS and input_name != "Kill":
                    continue
                targets = resolve_targets(entities, output.get("target", ""))
                target_classes = sorted({str(t.get("classname", "")) for t in targets})
                if input_name == "Kill":
                    target_classes = [c for c in target_classes if c.casefold() == "ambient_generic"]
                if not target_classes:
                    if input_name in AUDIO_INPUTS and output.get("target") and not str(
                            output.get("target")).startswith("!"):
                        unresolved.append({
                            "map": map_name,
                            "source": ent.get("targetname") or classname,
                            "event": output.get("name", ""),
                            "target": output.get("target", ""),
                            "input": input_name,
                        })
                    continue
                for target_class in target_classes:
                    route = "%s.%s" % (target_class, input_name)
                    routes[route] += 1
                    source = "%s.%s -> %s" % (
                        classname, output.get("name", ""), route)
                    route_sources[source] += 1

        row = {"map": map_name}
        row.update({name: per_map[name] for name in AUDIO_CLASSES})
        row["total"] = sum(per_map.values())
        map_rows.append(row)

    map_rows.sort(key=lambda row: (-row["total"], row["map"]))
    return {
        "maps": len(maps),
        "entities": total_entities,
        "audio_entities": dict(entity_counts),
        "by_map": map_rows,
        "ambient_generic": {
            "extensions": dict(ambient_extensions),
            "spawnflags": dict(ambient_spawnflags),
            "key_presence": dict(ambient_key_presence),
            "nonzero_keys": dict(ambient_nonzero),
        },
        "ambient_soundscheme": {"start_state": dict(scheme_start)},
        "environmental_audio": {"room_types": dict(environmental_rooms)},
        "soundgroups": {
            "by_class": dict(soundgroup_classes),
            "tokens_by_class": {k: dict(v) for k, v in soundgroups.items()},
        },
        "io": {
            "routes": dict(routes.most_common()),
            "top_sources": dict(route_sources.most_common(30)),
            "unresolved": unresolved,
            "music_outputs": dict(music_outputs),
        },
    }


def print_report(data):
    maps = data["maps"]
    py = data["python"]
    print("=" * 78)
    print("VtMB exported audio surface")
    print("=" * 78)
    print("corpus: %d maps | %d entities | %d Python files" %
          (maps["maps"], maps["entities"], py["files"]))
    print("audio entities: " + ", ".join("%s=%d" % item
          for item in sorted(maps["audio_entities"].items())))
    print()
    print("Top maps")
    for row in maps["by_map"][:12]:
        print("  %-22s %4d  generic=%d scheme=%d env=%d" %
              (row["map"], row["total"], row["ambient_generic"],
               row["ambient_soundscheme"], row["trigger_environmental_audio"]))
    print()
    print("Authored audio I/O wires")
    for name, count in list(maps["io"]["routes"].items())[:16]:
        print("  %-44s %5d" % (name, count))
    print("  unresolved audio wires: %d" % len(maps["io"]["unresolved"]))
    print()
    print("Python audio-facing calls")
    for name in PYTHON_AUDIO_METHODS:
        if py["calls"].get(name):
            print("  %-24s %5d" % (name, py["calls"][name]))
    print()
    rooms = maps["environmental_audio"]["room_types"]
    print("environmental room_type values: " +
          ", ".join("%s=%s" % item for item in sorted(rooms.items())))
    print("soundgroup-bearing classes: " +
          ", ".join("%s=%s" % item for item in sorted(
              maps["soundgroups"]["by_class"].items(), key=lambda item: (-item[1], item[0]))[:12]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", help="write the complete survey ledger here")
    args = parser.parse_args()
    data = {"maps": survey_maps(load_maps()), "python": survey_python()}
    print_report(data)
    if args.json:
        with open(args.json, "w") as handle:
            json.dump(data, handle, indent=1, sort_keys=True)
        print("\nwrote %s" % args.json)


if __name__ == "__main__":
    main()
