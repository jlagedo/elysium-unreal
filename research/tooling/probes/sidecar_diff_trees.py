# -*- coding: utf-8 -*-
"""0018 story 21-7: diff two `sidecar_snapshot` trees, line by line, and say what moved.

Usage::

    uv run elysium research sidecar_diff_trees --before <path>/before --after <path>/after
"""
from __future__ import annotations

import argparse
import collections
import difflib
import json
from pathlib import Path


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="sidecar_diff_trees", description=__doc__)
    parser.add_argument("--before", type=Path, required=True)
    parser.add_argument("--after", type=Path, required=True)
    parser.add_argument("--context", type=int, default=6, help="differing lines to print per file")
    arguments = parser.parse_args(argv)

    before_refused = json.loads((arguments.before / "_refused.json").read_text(encoding="utf-8"))
    after_refused = json.loads((arguments.after / "_refused.json").read_text(encoding="utf-8"))
    print(f"refused before: {sorted(before_refused)}")
    print(f"refused after : {sorted(after_refused)}")

    maps = sorted({p.name for p in arguments.before.iterdir() if p.is_dir()}
                  | {p.name for p in arguments.after.iterdir() if p.is_dir()})
    by_suffix: collections.Counter = collections.Counter()
    changed_maps: list[str] = []
    for name in maps:
        left, right = arguments.before / name, arguments.after / name
        names = sorted({p.name for p in left.glob("*")} | {p.name for p in right.glob("*")})
        printed = False
        for filename in names:
            a = (left / filename).read_text(encoding="utf-8", errors="replace").splitlines() \
                if (left / filename).is_file() else []
            b = (right / filename).read_text(encoding="utf-8", errors="replace").splitlines() \
                if (right / filename).is_file() else []
            if a == b:
                continue
            suffix = filename.split(".", 1)[-1]
            by_suffix[suffix] += 1
            if not printed:
                print(f"\n== {name}")
                printed = True
            changed_maps.append(name)
            lines = [line for line in difflib.unified_diff(a, b, filename, filename, n=0)
                     if line[:1] in "+-" and not line.startswith(("---", "+++"))]
            print(f"  {filename}: {len(lines)} differing line(s)")
            for line in lines[:arguments.context]:
                print(f"    {line[:160]}")
            if len(lines) > arguments.context:
                print(f"    ... {len(lines) - arguments.context} more")

    print(f"\nmaps with any difference: {len(set(changed_maps))} of {len(maps)}")
    print(f"files by suffix: {dict(sorted(by_suffix.items()))}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
