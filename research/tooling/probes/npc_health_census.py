"""Resolve every patch-first NPC template's authored Max_Health pool.

Usage:
    uv run elysium research npc_health_census <vdata-system> [--json <path>]

The report distinguishes a literal value, a value inherited through
ParentTemplateName, and the stats.txt default. Generated JSON belongs below
ELYSIUM_WORK_ROOT; game-derived template rows are never written into Git.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from elysium_pipeline.formats.kv import parse


DEFAULT_MAX_HEALTH = 100


def _records(value: object) -> list[dict[str, object]]:
    if value is None:
        return []
    if isinstance(value, dict):
        return [value]
    if isinstance(value, list) and all(isinstance(item, dict) for item in value):
        return value
    raise ValueError(f"unexpected ClanData shape: {type(value).__name__}")


def load_templates(
    vdata_system: Path,
) -> tuple[dict[str, dict[str, object]], list[dict[str, object]], list[str]]:
    templates: dict[str, dict[str, object]] = {}
    declarations: list[dict[str, object]] = []
    duplicate_names: list[str] = []
    for path in sorted(vdata_system.glob("npctemplate*.txt")):
        document = parse(path.read_text(encoding="latin1"))
        for record in _records(document.get("clandata")):
            text = record.get("text")
            attributes = record.get("attributes", {})
            if not isinstance(text, dict) or not isinstance(attributes, dict):
                raise ValueError(f"{path}: malformed ClanData Text or Attributes block")
            name = str(text.get("templatename", "")).strip()
            if not name:
                raise ValueError(f"{path}: ClanData has no TemplateName")
            key = name.casefold()
            raw_health = attributes.get("max_health")
            template = {
                "name": name,
                "parent": str(text.get("parenttemplatename", "")).strip(),
                "explicit_max_health": int(raw_health) if raw_health is not None else None,
            }
            if key in templates:
                prior = templates[key]
                if (
                    prior["parent"] != template["parent"]
                    or prior["explicit_max_health"] != template["explicit_max_health"]
                ):
                    raise ValueError(f"duplicate NPC template changes health contract: {name!r}")
                duplicate_names.append(name)
            else:
                templates[key] = template
            declarations.append(template)
    return templates, declarations, duplicate_names


def resolve_health(
    key: str,
    templates: dict[str, dict[str, object]],
    active: tuple[str, ...] = (),
) -> tuple[int, str, list[str]]:
    if key in active:
        chain = " -> ".join((*active, key))
        raise ValueError(f"NPC template inheritance cycle: {chain}")
    template = templates[key]
    explicit = template["explicit_max_health"]
    if explicit is not None:
        return int(explicit), "explicit", [str(template["name"])]
    parent = str(template["parent"])
    if not parent:
        return DEFAULT_MAX_HEALTH, "default", [str(template["name"])]
    parent_key = parent.casefold()
    if parent_key not in templates:
        raise ValueError(f"NPC template {template['name']!r} has missing parent {parent!r}")
    value, source, chain = resolve_health(parent_key, templates, (*active, key))
    inherited_source = f"parent:{templates[parent_key]['name']}" if source != "default" else "default"
    return value, inherited_source, [str(template["name"]), *chain]


def build_report(vdata_system: Path) -> dict[str, object]:
    templates, declarations, duplicate_names = load_templates(vdata_system)
    rows = []
    for template in declarations:
        key = str(template["name"]).casefold()
        value, source, chain = resolve_health(key, templates)
        rows.append(
            {
                **template,
                "effective_max_health": value,
                "source": source,
                "inheritance_chain": chain,
            }
        )
    rows.sort(key=lambda row: (int(row["effective_max_health"]), str(row["name"]).casefold()))
    explicit = sum(row["source"] == "explicit" for row in rows)
    defaulted = sum(row["source"] == "default" for row in rows)
    return {
        "vdata_system": str(vdata_system.resolve()),
        "files": len(list(vdata_system.glob("npctemplate*.txt"))),
        "unique_template_names": len(templates),
        "templates": len(rows),
        "duplicate_names": duplicate_names,
        "explicit": explicit,
        "inherited": len(rows) - explicit - defaulted,
        "defaulted": defaulted,
        "minimum": min(int(row["effective_max_health"]) for row in rows),
        "maximum": max(int(row["effective_max_health"]) for row in rows),
        "rows": rows,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("vdata_system", type=Path)
    parser.add_argument("--json", type=Path, dest="json_path")
    args = parser.parse_args()
    if not args.vdata_system.is_dir():
        parser.error(f"not a vdata/system directory: {args.vdata_system}")

    report = build_report(args.vdata_system)
    print(
        "NPC Max_Health: "
        f"{report['files']} files, {report['templates']} declarations / "
        f"{report['unique_template_names']} distinct names, "
        f"{report['explicit']} explicit, {report['inherited']} inherited, "
        f"{report['defaulted']} defaulted, range {report['minimum']}..{report['maximum']}"
    )
    if report["duplicate_names"]:
        print(f"duplicate names: {', '.join(report['duplicate_names'])}")
    for row in report["rows"]:
        print(f"{row['effective_max_health']:4}  {row['name']:<34} {row['source']}")

    if args.json_path:
        args.json_path.parent.mkdir(parents=True, exist_ok=True)
        args.json_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(f"JSON: {args.json_path.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
