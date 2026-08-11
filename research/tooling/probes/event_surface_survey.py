"""Diff the event surface of every exported map against one baseline map.

The survey reads only the regenerable ``$ELYSIUM_EXPORT_ROOT`` JSON/VCD mirror. It joins:

* entity class and trigger-class demand;
* source classname + output-name producers;
* resolved receiver classname + input routes, including wildcard fan-out;
* field-5 Python identifiers;
* map-referenced choreographed-scene event types, firetrigger values, and Python payloads.

Generated reports are game-derived and belong below ``ELYSIUM_WORK_ROOT``. The tracked script and
canonical behavioral conclusions belong in the repository.

Usage:
    uv run elysium research event_surface_survey [--baseline sp_tutorial_1] [--json <path>]
"""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import os
import re
from pathlib import Path

from elysium_pipeline.paths import export_root


PY_IDENTIFIER = re.compile(r"\b([A-Za-z_]\w*)\s*(?=\(|=|\+=|-=)")
VCD_EVENT = re.compile(r'^\s*event\s+(\w+)\s+"([^"]*)"', re.IGNORECASE)
VCD_TIME = re.compile(r"^\s*time\s+([^\s]+)\s+([^\s]+)", re.IGNORECASE)
VCD_PARAM = re.compile(r'^\s*param\s+"(.*)"', re.IGNORECASE)


def _key_ci(keys: dict, name: str, default: str = "") -> str:
    needle = name.casefold()
    for key, value in keys.items():
        if str(key).casefold() == needle:
            return str(value)
    return default


def _load_maps(root: Path) -> list[tuple[Path, dict]]:
    maps = []
    for path in sorted(root.glob("*/*.ents")):
        try:
            data = json.loads(path.read_text(encoding="utf-8", errors="replace"))
        except (OSError, ValueError):
            continue
        if isinstance(data, dict) and isinstance(data.get("entities"), list):
            maps.append((path, data))
    return maps


def _resolve_targets(entities: list[dict], target: str) -> list[dict]:
    query = str(target or "").casefold()
    if not query or query.startswith("!"):
        return []
    if query.endswith("*"):
        prefix = query[:-1]
        return [entity for entity in entities
                if str(entity.get("targetname", ""))
                and str(entity.get("targetname", "")).casefold().startswith(prefix)]
    return [entity for entity in entities
            if str(entity.get("targetname", "")).casefold() == query]


def _record(counter: collections.Counter, maps: dict[str, set[str]], examples: dict[str, list[str]],
            key: str, map_name: str, example: str = "") -> None:
    counter[key] += 1
    maps[key].add(map_name)
    if example and example not in examples[key] and len(examples[key]) < 5:
        examples[key].append(example)


def _python_identifiers(source: str) -> list[str]:
    ignored = {"if", "else", "for", "while", "return", "and", "or", "not", "in", "is"}
    return [match.group(1) for match in PY_IDENTIFIER.finditer(source)
            if match.group(1) not in ignored]


def _vcd_index(root: Path) -> dict[str, Path]:
    scenes = root / "scenes"
    out = {}
    if not scenes.is_dir():
        return out
    for path in scenes.rglob("*.vcd"):
        rel = path.relative_to(scenes).as_posix().casefold()
        out[rel] = path
    return out


def _scene_key(value: str) -> str:
    key = str(value or "").replace("\\", "/").strip().casefold()
    for prefix in ("sound/", "scenes/"):
        if key.startswith(prefix):
            key = key[len(prefix):]
    return key


def _parse_vcd(path: Path) -> list[dict]:
    events = []
    current = None
    for raw in path.read_text(encoding="latin-1", errors="replace").splitlines():
        match = VCD_EVENT.match(raw)
        if match:
            current = {"type": match.group(1).casefold(), "name": match.group(2),
                       "start": None, "end": None, "param": ""}
            events.append(current)
            continue
        if current is None:
            continue
        match = VCD_TIME.match(raw)
        if match:
            current["start"], current["end"] = match.groups()
            continue
        match = VCD_PARAM.match(raw)
        if match:
            current["param"] = match.group(1)
    return events


def _summarize(selected: list[tuple[Path, dict]], vcds: dict[str, Path]) -> dict:
    counts = collections.Counter()
    counters: dict[str, collections.Counter] = collections.defaultdict(collections.Counter)
    maps_by: dict[str, dict[str, set[str]]] = collections.defaultdict(
        lambda: collections.defaultdict(set))
    examples_by: dict[str, dict[str, list[str]]] = collections.defaultdict(
        lambda: collections.defaultdict(list))
    scene_refs: dict[str, set[str]] = collections.defaultdict(set)
    missing_scenes = []

    for path, root in selected:
        map_name = str(root.get("map") or path.stem)
        entities = root["entities"]
        counts["maps"] += 1
        counts["entities"] += len(entities)
        for entity in entities:
            classname = str(entity.get("classname", "<missing>"))
            targetname = str(entity.get("targetname", ""))
            keys = entity.get("keys", {}) or {}
            _record(counters["entity_classes"], maps_by["entity_classes"],
                    examples_by["entity_classes"], classname, map_name)
            if classname.casefold().startswith("trigger_"):
                _record(counters["trigger_classes"], maps_by["trigger_classes"],
                        examples_by["trigger_classes"], classname, map_name)

            if classname.casefold() == "logic_choreographed_scene":
                scene_file = _key_ci(keys, "SceneFile")
                if scene_file:
                    key = _scene_key(scene_file)
                    scene_refs[key].add(map_name)

            for output in entity.get("outputs", []) or []:
                counts["outputs"] += 1
                output_name = str(output.get("name", "<missing>"))
                producer = f"{classname}.{output_name}"
                source_label = targetname or f"<{classname}>"
                _record(counters["producer_events"], maps_by["producer_events"],
                        examples_by["producer_events"], producer, map_name,
                        f"{map_name}:{source_label}")

                python = str(output.get("python", ""))
                if python:
                    counts["python_outputs"] += 1
                    for identifier in _python_identifiers(python):
                        _record(counters["python_identifiers"], maps_by["python_identifiers"],
                                examples_by["python_identifiers"], identifier, map_name,
                                f"{map_name}:{source_label}.{output_name}: {python}")

                input_name = str(output.get("input", ""))
                target = str(output.get("target", ""))
                if not input_name:
                    continue
                if target.casefold().startswith("!"):
                    special = f"{target.casefold()}.{input_name}"
                    _record(counters["special_target_inputs"], maps_by["special_target_inputs"],
                            examples_by["special_target_inputs"], special, map_name,
                            f"{map_name}:{source_label}.{output_name}")
                    continue
                targets = _resolve_targets(entities, target)
                if not targets:
                    unresolved = f"{target}.{input_name}"
                    _record(counters["unresolved_target_inputs"],
                            maps_by["unresolved_target_inputs"],
                            examples_by["unresolved_target_inputs"], unresolved, map_name,
                            f"{map_name}:{source_label}.{output_name}")
                    continue
                for target_class in sorted({str(item.get("classname", "<missing>"))
                                            for item in targets}):
                    receiver = f"{target_class}.{input_name}"
                    _record(counters["receiver_inputs"], maps_by["receiver_inputs"],
                            examples_by["receiver_inputs"], receiver, map_name,
                            f"{map_name}:{source_label}.{output_name} -> {target}")

    for key, ref_maps in sorted(scene_refs.items()):
        path = vcds.get(key)
        if path is None:
            missing_scenes.append({"scene": key, "maps": sorted(ref_maps)})
            continue
        counts["referenced_vcds"] += 1
        for event in _parse_vcd(path):
            event_type = event["type"]
            example = f"{key}@{event['start']} {event['name']} {event['param']}".strip()
            for map_name in ref_maps:
                _record(counters["vcd_event_types"], maps_by["vcd_event_types"],
                        examples_by["vcd_event_types"], event_type, map_name, example)
                if event_type == "firetrigger":
                    fire = event["param"] or "<empty>"
                    _record(counters["vcd_firetriggers"], maps_by["vcd_firetriggers"],
                            examples_by["vcd_firetriggers"], fire, map_name, example)
                elif event_type == "python":
                    source = event["param"] or "<empty>"
                    _record(counters["vcd_python"], maps_by["vcd_python"],
                            examples_by["vcd_python"], source, map_name, example)

    return {
        "counts": dict(counts),
        "surfaces": {name: {
            key: {
                "count": count,
                "maps": sorted(maps_by[name][key]),
                "examples": examples_by[name][key],
            }
            for key, count in sorted(counter.items(), key=lambda item: (-item[1], item[0].casefold()))
        } for name, counter in counters.items()},
        "scene_refs": {key: sorted(value) for key, value in sorted(scene_refs.items())},
        "missing_scenes": missing_scenes,
    }


def _diff(full: dict, baseline: dict) -> dict:
    result = {}
    casefold_surfaces = {
        "entity_classes",
        "trigger_classes",
        "producer_events",
        "receiver_inputs",
        "unresolved_target_inputs",
        "special_target_inputs",
        "vcd_event_types",
    }
    for surface, rows in full["surfaces"].items():
        # Entity names, classnames and datamap field lookup are case-insensitive in retail. Keep
        # the authored spelling in the report, but do not mislabel a casing-only spelling change
        # as a new semantic route. Python identifiers and event payloads remain case-sensitive.
        normalize = str.casefold if surface in casefold_surfaces else str
        base_keys = {normalize(key) for key in baseline["surfaces"].get(surface, {})}
        result[surface] = {
            key: value for key, value in rows.items() if normalize(key) not in base_keys
        }
    return result


def _manifest(maps: list[tuple[Path, dict]], root: Path, vcds: dict[str, Path],
              scene_refs: dict[str, list[str]]) -> dict:
    entity_rows = []
    scene_rows = []
    digest = hashlib.sha256()
    for path, data in maps:
        raw = path.read_bytes()
        sha = hashlib.sha256(raw).hexdigest().upper()
        name = str(data.get("map") or path.stem)
        rel = path.relative_to(root).as_posix()
        entity_rows.append({"map": name, "path": rel, "bytes": len(raw), "sha256": sha})
        digest.update(rel.encode("utf-8"))
        digest.update(b"\0")
        digest.update(sha.encode("ascii"))
        digest.update(b"\n")
    for key in sorted(scene_refs):
        path = vcds.get(key)
        if path is None:
            continue
        raw = path.read_bytes()
        sha = hashlib.sha256(raw).hexdigest().upper()
        rel = path.relative_to(root).as_posix()
        scene_rows.append({"scene": key, "path": rel, "bytes": len(raw), "sha256": sha,
                           "maps": scene_refs[key]})
        digest.update(rel.encode("utf-8"))
        digest.update(b"\0")
        digest.update(sha.encode("ascii"))
        digest.update(b"\n")
    return {
        "entity_files": entity_rows,
        "scene_files": scene_rows,
        "manifest_sha256": digest.hexdigest().upper(),
    }


def _print_rows(title: str, rows: dict, limit: int = 40) -> None:
    print(f"\n### {title} ({len(rows)})")
    for key, value in list(rows.items())[:limit]:
        maps = ",".join(value["maps"][:6])
        suffix = "…" if len(value["maps"]) > 6 else ""
        print(f"{value['count']:5d}  {key:<55}  {maps}{suffix}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", default="sp_tutorial_1")
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()

    root = Path(export_root())
    maps = _load_maps(root)
    baseline_maps = [item for item in maps if str(item[1].get("map")) == args.baseline]
    if not maps:
        parser.error(f"no exported .ents maps found below {root}")
    if len(baseline_maps) != 1:
        parser.error(f"baseline {args.baseline!r} resolved to {len(baseline_maps)} maps")

    vcds = _vcd_index(root)
    full = _summarize(maps, vcds)
    baseline = _summarize(baseline_maps, vcds)
    report = {
        "schema": "elysium.event-surface-survey",
        "version": 1,
        "export_root": os.fspath(root),
        "baseline": args.baseline,
        "manifest": _manifest(maps, root, vcds, full["scene_refs"]),
        "corpus": full,
        "baseline_surface": baseline,
        "novel_vs_baseline": _diff(full, baseline),
    }

    print("maps={maps} entities={entities} outputs={outputs} python={python_outputs} "
          "referenced_vcds={referenced_vcds}".format(**full["counts"]))
    print("baseline={0} entities={1} outputs={2} python={3} referenced_vcds={4}".format(
        args.baseline,
        baseline["counts"].get("entities", 0),
        baseline["counts"].get("outputs", 0),
        baseline["counts"].get("python_outputs", 0),
        baseline["counts"].get("referenced_vcds", 0)))
    print("manifest_sha256=" + report["manifest"]["manifest_sha256"])
    for key, title in (
        ("trigger_classes", "new trigger classes"),
        ("producer_events", "new producer classname.output pairs"),
        ("receiver_inputs", "new resolved receiver classname.input pairs"),
        ("unresolved_target_inputs", "new static-unresolved target.input pairs"),
        ("special_target_inputs", "new special-target inputs"),
        ("python_identifiers", "new field-5 Python identifiers"),
        ("vcd_event_types", "new map-referenced VCD event types"),
        ("vcd_firetriggers", "new map-referenced VCD firetrigger params"),
        ("vcd_python", "new map-referenced VCD Python payloads"),
    ):
        _print_rows(title, report["novel_vs_baseline"].get(key, {}))
    if full["missing_scenes"]:
        print(f"\nmissing referenced VCDs: {len(full['missing_scenes'])}")

    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n",
                             encoding="utf-8")
        print(f"\njson={args.json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
