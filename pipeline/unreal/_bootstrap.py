"""Expose the tracked pipeline package to Unreal's embedded editor Python."""

from __future__ import annotations

import os
import sys


REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(REPO, "pipeline", "src")
for path in (REPO, SRC):
    if path not in sys.path:
        sys.path.insert(0, path)
