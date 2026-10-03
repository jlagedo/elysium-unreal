#!/usr/bin/env python3
"""`uv run elysium research where <address|name|field|offset|"slot N"> ...`

Where the project already speaks about a retail address, name, field or vtable slot: its
`docs/vtmb` sections, its `npc-kernel` ledger rows, the port lines that cite it, the specs that
mention it. One call, from a generated index (`addr_index.py`); a name finds its address through the
ledger's own name tables, so `CAI_BaseNPC::SelectSchedule` and `0x1028a380` answer alike.

    where 0x1028a380
    where CAI_BaseNPC::SelectSchedule m_scriptState "slot 442"
    where +0x5cc0
"""

import sys

from addr_index import run

if __name__ == "__main__":
    raise SystemExit(run("where", sys.argv[1:]))
