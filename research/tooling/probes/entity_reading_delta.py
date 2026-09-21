# -*- coding: utf-8 -*-
"""0018 story 21-7 job 1: what the entity asset carries today, against retail's own reading.

The port's `.ents` / `DA_<map>_Entities` join rebuilds the ENTITIES lump as text
(`UE_map_sidecars.entity_lump_text`) and re-runs the legacy regexes over it, under the all-legacy
`EntityDivergences` default. Retail reads the lump with the tokeniser `0x10136ce0` and parses an
output row with `0x100ccf90` (`docs/vtmb/entity_io.md` -> "The lump tokeniser and what a keyvalue
actually becomes"). This probe measures the difference, decomposed by cause, so each flip lands
with the rows it changed.

Read-only. Generated reports belong under `$ELYSIUM_WORK_ROOT/research`.

Usage::

    uv run elysium research entity_reading_delta
    uv run elysium research entity_reading_delta --six
    uv run elysium research entity_reading_delta --json <external-path>
"""
from __future__ import annotations

import argparse
import collections
import json
import re
from pathlib import Path
from typing import Any

from elysium_pipeline.exporters import UE_map_sidecars as producer
from elysium_pipeline.formats.map_entities_glb import model as entity_model
from elysium_pipeline.paths import export_v2_root

#: The six maps 0018 story group 21 is witnessed on.
SIX = ("sp_tutorial_1", "sm_hub_1", "sp_soc_3", "sm_pawnshop_1", "sp_theatre", "sp_genesisdevice_1")

#: Retail caps a key and a value at 255 characters plus NUL (`Q_strncpy(..., 0x100)`, `10136f3c`).
TOKEN_CAP = 255

#: The legacy block and pair regexes, and the escape rule the reconstruction inverted with. Copied
#: here rather than imported: story 21-7 deletes `entity_lump_text`, `_requote` and
#: `parse_entity_blocks` from the producer, and this probe is what measures what their deletion
#: changed, so it has to outlive them.
_BLOCK = re.compile(r"\{([^{}]*)\}", re.S)
_PAIR = re.compile(r'"([^"]*)"\s+"([^"]*)"')


def structural_pairs(row: dict[str, Any]) -> list[tuple[str, str]]:
    """The tokeniser's own output: `(sourceKey, value)` in authored order, repeats kept."""

    return [(str(kv.get("sourceKey", kv.get("key", ""))), str(kv.get("value", "")))
            for kv in row.get("keyValues") or []]


def _requote(text: str, quoted: bool) -> str:
    if not quoted:
        return text
    escaped = text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
    return f'"{escaped}"'


def legacy_pairs(entity_rows) -> list[list[tuple[str, str]]] | None:
    """What the producer read before 21-7, or `None` when the reconstruction refuses the map.

    The reconstruction's byteLength gate is reproduced, because refusing three maps outright is one
    of the two costs being measured.
    """

    parts: list[str] = []
    for row in entity_rows:
        parts.append("{\n")
        for keyvalue in row.get("keyValues") or []:
            key = _requote(str(keyvalue.get("sourceKey", keyvalue.get("key", ""))),
                           bool(keyvalue.get("quotedKey", True)))
            value = _requote(str(keyvalue.get("value", "")),
                             bool(keyvalue.get("quotedValue", True)))
            length = int(keyvalue.get("byteLength", len(key) + len(value) + 1))
            if len(key) + len(value) + 1 != length:
                return None
            parts.append(f"{key} {value}\n")
        parts.append("}\n")
    text = "".join(parts)
    return [_PAIR.findall(block) for block in _BLOCK.findall(text)]


def _classname(pairs) -> str:
    return next((v for k, v in reversed(pairs) if k.strip().lower() == "classname"), "")


def _shaped_output(key: str) -> bool:
    """The legacy gate: `^(On|Out)` case-insensitively. Inlined for the same reason as above."""

    return bool(re.match(r"^(On|Out)", key, re.I))


def _declared_output(classname: str, source_key: str) -> bool:
    """Retail's gate: the class's datamap types the key (`0x101a5a80`, type 10 custom)."""

    folded = source_key.lower()
    if folded.endswith(entity_model.DISABLED_KEY_SUFFIX):
        return False
    folded_class = (classname or "").strip().lower()
    if (folded_class, folded) in entity_model.NOT_OUTPUT_KEYS:
        return False
    if entity_model.OUTPUT_KEY.match(source_key) is not None:
        return True
    return folded in entity_model.OUTPUT_KEYS_BY_CLASS.get(folded_class, frozenset())


def measure_map(map_name: str, root: Path) -> dict[str, Any]:
    units = producer.read_units(map_name, root)
    entity_rows = units.entities["entities"]
    legacy = legacy_pairs(entity_rows)

    counts: collections.Counter = collections.Counter()
    examples: dict[str, list[str]] = collections.defaultdict(list)

    def note(cause: str, detail: str) -> None:
        counts[cause] += 1
        if len(examples[cause]) < 6:
            examples[cause].append(detail)

    for index, row in enumerate(entity_rows):
        pairs = structural_pairs(row)
        where = f"{map_name}[{index}]"

        # The escaped-quote re-pairing: does the text round trip read the same pairs at all?
        if legacy is not None and index < len(legacy):
            mine = [(str(k), str(v)) for k, v in legacy[index]]
            if mine != pairs:
                note("repairing", f"{where} {_classname(pairs)}: "
                                  f"{len(mine)} legacy pairs vs {len(pairs)} authored")

        classname = _classname(pairs)
        for key, value in pairs:
            # The tokeniser's own caps, which the unit's lexer does not apply.
            if len(key) > TOKEN_CAP:
                note("key_over_255", f"{where} {key[:40]!r} is {len(key)} bytes")
            if len(value) > TOKEN_CAP:
                note("value_over_255", f"{where} {key!r} value is {len(value)} bytes")
            if key != key.rstrip(" "):
                note("key_trailing_space", f"{where} {key!r}")

            shaped = _shaped_output(key)
            declared = _declared_output(classname, key)
            commas = value.count(",")

            # Datamap typing, and the comma gate retail does not have.
            if declared and not shaped:
                note("output_promoted", f"{where} {classname}.{key}")
            if shaped and not declared:
                note("output_demoted", f"{where} {classname}.{key}")
            if declared and commas < 4:
                note("output_under_gate", f"{where} {classname}.{key} = {value!r}")

            if not (shaped or declared) or commas < 4:
                continue
            fields = value.split(",")

            def at(position: int) -> str:
                return fields[position] if position < len(fields) else ""

            # Retail's splitter trims nothing.
            for name, position in (("target", 0), ("input", 1), ("python", 5)):
                if at(position) != at(position).strip():
                    note(f"strip_{name}", f"{where} {classname}.{key} {name}={at(position)!r}")

            # delay: plain float() against atof.
            try:
                plain = float(at(3))
            except ValueError:
                plain = 0.0
            if plain != producer.atof(at(3)):
                note("delay_atof", f"{where} {classname}.{key} delay={at(3)!r}")

            # times: int(float()) against atoi, and the authored 0.
            try:
                port_times = int(float(at(4)))
            except ValueError:
                port_times = -1
            retail_times = entity_model.atoi(at(4)) or entity_model.UNLIMITED_TIMES
            if port_times != retail_times:
                note("times", f"{where} {classname}.{key} times={at(4)!r} "
                              f"port={port_times} retail={retail_times}")

            # The empty input token retail substitutes `Use` for.
            if at(1) == "":
                note("empty_input", f"{where} {classname}.{key}")

            # The seventh field retail never reads.
            if len(fields) > 6:
                counts["has_extra"] += 1

        # fold_keys: a key repeated under two spellings.
        spellings: dict[str, set] = collections.defaultdict(set)
        for key, _value in pairs:
            spellings[key.lower()].add(key)
        for _folded, seen in spellings.items():
            if len(seen) > 1:
                note("fold_keys", f"{where} {sorted(seen)}")

    return {
        "map": map_name,
        "entities": len(entity_rows),
        "lumpRefused": legacy is None,
        "counts": dict(sorted(counts.items())),
        "examples": {cause: rows for cause, rows in sorted(examples.items())},
    }


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="entity_reading_delta", description=__doc__)
    parser.add_argument("--maps", nargs="*", default=None, help="map stems; default every unit")
    parser.add_argument("--six", action="store_true", help="only the story-21 witness maps")
    parser.add_argument("--json", type=Path, default=None, help="write the full report here")
    arguments = parser.parse_args(argv)

    root = export_v2_root()
    if arguments.six:
        names = list(SIX)
    elif arguments.maps:
        names = list(arguments.maps)
    else:
        names = sorted(path.name.split(".")[0] for path in (root / "maps").glob("*.entities.glb"))

    reports = [measure_map(name, root) for name in names]
    totals: collections.Counter = collections.Counter()
    for report in reports:
        totals.update(report["counts"])

    width = max(len(name) for name in names)
    causes = sorted(totals)
    print(f"{'map':<{width}}  entities  " + "  ".join(causes))
    for report in reports:
        cells = "  ".join(str(report["counts"].get(cause, 0)).rjust(len(cause)) for cause in causes)
        flag = " REFUSED" if report["lumpRefused"] else ""
        print(f"{report['map']:<{width}}  {report['entities']:>8}  {cells}{flag}")
    print(f"{'TOTAL':<{width}}  {sum(r['entities'] for r in reports):>8}  "
          + "  ".join(str(totals[cause]).rjust(len(cause)) for cause in causes))

    for report in reports:
        if report["map"] in SIX and report["examples"]:
            print(f"\n-- {report['map']}")
            for cause, rows in report["examples"].items():
                for row in rows:
                    print(f"   {cause}: {row}")

    if arguments.json:
        arguments.json.parent.mkdir(parents=True, exist_ok=True)
        arguments.json.write_text(json.dumps(
            {"totals": dict(sorted(totals.items())), "maps": reports}, indent=2), encoding="utf-8")
        print(f"\nwrote {arguments.json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
