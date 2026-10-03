#!/usr/bin/env python3
"""`uv run elysium research section <address|name>`

The docs section that is ABOUT an address or name -- the one whose heading holds it -- printed
without reading the file around it (`docs/vtmb/npc-ai/*.md` are 100-420 KB each). When several
sections qualify they are listed and `--pick N` prints another; a long one is paged with `--from`.
"""

import sys

from addr_index import run

if __name__ == "__main__":
    raise SystemExit(run("section", sys.argv[1:]))
