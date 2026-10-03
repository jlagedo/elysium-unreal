#!/usr/bin/env python3
"""`uv run elysium research rows <table.tsv> [col=value ...] [--count-by col] [--cols a,b]`

Filter, count or project any of the project's `.tsv` tables (`docs/` and `research/tooling/`), by
path or bare file name -- the `csv.DictReader` and `Counter` a one-off script wrote each time.

    rows unported.tsv verdict=rule --count-by class
    rows kernel_verdicts.tsv band=19-29 verdict=dead --cols address,target
    rows layout.tsv member~script table=CAI_BaseNPCTroika
"""

import sys

from addr_index import run

if __name__ == "__main__":
    raise SystemExit(run("rows", sys.argv[1:]))
