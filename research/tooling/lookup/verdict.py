#!/usr/bin/env python3
"""`uv run elysium research verdict [address|name ...] [--band 5-9] [--verdict rule]`

The porting verdict of a retail function from `kernel_verdicts.tsv` -- verdict, band, target, and
the evidence that decided it -- or, with no address, the table filtered by band, verdict or target;
`--count` tabulates verdicts by band. The checklists are rendered from this table, so this reads it
directly instead of the 0.7-0.9 MB checklist that restates it.
"""

import sys

from addr_index import run

if __name__ == "__main__":
    raise SystemExit(run("verdict", sys.argv[1:]))
