# -*- coding: utf-8 -*-
"""Census of the shipped brushes by CONTENTS SIGNATURE -- the four answers 0018 story 3 bakes.

A brush is not read by its class or its texture but by the four questions retail's own trace
masks ask of it (`research/tooling/data/contents_masks.json`): does it block the player
(`& 0x1400b`), block an NPC (`& 0x2400b`), block sight (`& 0x804091`), and is it a pedestrian
volume (`& 0x2000`). Those four answers are its signature, written `PNSp` with a dash for each
"no", and the bake gives one collision body per signature a map actually carries.

This walks the BRUSHES lump (18) of every `.bsp` under the engine's own loose search path --
the patch tree, which is what the runtime resolves first -- and reports the signature histogram
game-wide and per witness map, so the pins in the spec and in `navigation-jump-links.md` are
reproducible from the repository rather than from a scratch tree.

Read-only unless ``--json`` is supplied; generated reports belong below
``$ELYSIUM_WORK_ROOT/research`` and are never committed.

Usage::

    uv run elysium research contents_signatures
    uv run elysium research contents_signatures --map sp_tutorial_1 --map sm_hub_1
    uv run elysium research contents_signatures --json <external-path>
"""
from __future__ import annotations

import argparse
import collections
import glob
import json
import os
import struct

from elysium_pipeline.formats import install
from elysium_pipeline.paths import repo_root

#: The BRUSHES lump. Each row is 12 bytes: firstSide, numSides, contents.
LUMP_BRUSHES = 18
LUMP_TABLE_OFFSET = 8
LUMP_ENTRY_SIZE = 16
BRUSH_ROW_SIZE = 12

#: The recovered masks, shared with the generator that emits the runtime's table.
MASKS_JSON = ("research", "tooling", "data", "contents_masks.json")

#: The two maps story 3 bakes first; between them they carry all seven shipped signatures.
WITNESSES = ("sp_tutorial_1", "sm_hub_1")


def load_masks() -> list[tuple[str, str, int]]:
    """`(key, letter, mask)` per question, in the order the signature spells them.

    Each row carries the mask twice, as the decimal the code uses and as the hex the oracle
    quotes. They are compared rather than trusted: a hand-typed decimal that silently disagrees
    with its hex changes every signature downstream, and the census is the only place it would
    show -- as a pin that no longer matches retail.
    """

    path = repo_root().joinpath(*MASKS_JSON)
    document = json.loads(path.read_text(encoding="utf-8"))
    masks = []
    for row in document["masks"]:
        stated, written = int(row["mask"]), int(row["hex"], 16)
        if stated != written:
            raise SystemExit(
                f"{path.name}: mask {row['key']!r} is {stated} but its hex {row['hex']} is {written}")
        masks.append((row["key"], row["letter"], written))
    return masks


def brush_contents(path: str) -> list[int]:
    """The `contents` word of every brush in one `.bsp`."""

    with open(path, "rb") as handle:
        data = handle.read()
    offset, length = struct.unpack_from("<ii", data, LUMP_TABLE_OFFSET + LUMP_BRUSHES * LUMP_ENTRY_SIZE)
    return [
        struct.unpack_from("<iii", data, offset + index * BRUSH_ROW_SIZE)[2]
        for index in range(length // BRUSH_ROW_SIZE)
    ]


def signature(contents: int, masks: list[tuple[str, str, int]]) -> str:
    """`PNSp`, a dash where the answer is no; the empty signature is all dashes."""

    return "".join(letter if contents & mask else "-" for _key, letter, mask in masks)


def census(paths: list[str], masks: list[tuple[str, str, int]]) -> dict:
    """Signature histograms, game-wide and per map, plus the example contents behind each."""

    overall = collections.Counter()
    examples: dict[str, collections.Counter] = collections.defaultdict(collections.Counter)
    per_map: dict[str, dict[str, int]] = {}
    answering = 0
    for path in sorted(paths):
        name = os.path.basename(path)[: -len(".bsp")]
        local = collections.Counter()
        for contents in brush_contents(path):
            mark = signature(contents, masks)
            if set(mark) == {"-"}:
                continue                      # answers no mask: never staged, never a body
            answering += 1
            overall[mark] += 1
            local[mark] += 1
            examples[mark][f"{contents & 0xFFFFFFFF:#010x}"] += 1
        per_map[name] = dict(local.most_common())
    return {
        "maps": len(paths),
        "answeringBrushes": answering,
        "signatures": dict(overall.most_common()),
        "examples": {mark: dict(counter.most_common(3)) for mark, counter in examples.items()},
        "perMap": per_map,
    }


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="contents_signatures", add_help=False)
    parser.add_argument("--map", action="append", dest="maps", default=None,
                        help="restrict the walk to these maps (default: every shipped .bsp)")
    parser.add_argument("--json", dest="json_path", default=None,
                        help="also write the full report to this path (must be outside the repo)")
    args = parser.parse_args(argv)

    masks = load_masks()
    maps_dir = os.path.join(install.PATCH, "maps")
    if args.maps:
        paths = [os.path.join(maps_dir, f"{name}.bsp") for name in args.maps]
        missing = [path for path in paths if not os.path.exists(path)]
        if missing:
            print("FAIL: no such map: " + ", ".join(os.path.basename(path) for path in missing))
            return 1
    else:
        paths = glob.glob(os.path.join(maps_dir, "*.bsp"))

    report = census(paths, masks)
    order = "".join(letter for _key, letter, _mask in masks)
    print(f"{report['maps']} maps, {report['answeringBrushes']} brushes answering at least one mask")
    print(f"\nsignatures ({order}), game-wide:")
    for mark, count in report["signatures"].items():
        examples = ", ".join(report["examples"][mark])
        print(f"   {mark}  {count:7d}   e.g. {examples}")
    for name in (args.maps or WITNESSES):
        local = report["perMap"].get(name)
        if local is None:
            continue
        print(f"\n{name}: {sum(local.values())} answering brushes, {len(local)} signatures")
        for mark, count in local.items():
            print(f"   {mark}  {count:7d}")

    if args.json_path:
        target = os.path.abspath(args.json_path)
        if target.startswith(os.fspath(repo_root())):
            print(f"FAIL: {target} is inside the repository; reports belong under $ELYSIUM_WORK_ROOT")
            return 1
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, "w", encoding="utf-8") as handle:
            json.dump(report, handle, indent=2)
        print(f"\nwrote {target}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
