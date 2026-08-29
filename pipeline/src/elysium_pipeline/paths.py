"""One path contract for every offline command.

No game install or generated corpus defaults inside the repository. The
``elysium`` command loads ``.elysium.local.env`` before importing decoders;
direct library callers may set the same environment variables themselves.
"""

from __future__ import annotations

import os
from pathlib import Path


def repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def _required_env(name: str) -> Path:
    raw = os.environ.get(name, "").strip()
    if not raw:
        raise RuntimeError(
            f"{name} is not configured; copy dev/paths.example.env to "
            ".elysium.local.env and set the local path"
        )
    return Path(raw).expanduser().resolve()


def work_root() -> Path:
    return _required_env("ELYSIUM_WORK_ROOT")


def vtmb_root() -> Path:
    return _required_env("ELYSIUM_VTMB_ROOT")


def export_root() -> Path:
    override = os.environ.get("ELYSIUM_EXPORT_ROOT", "").strip()
    return Path(override).expanduser().resolve() if override else work_root() / "exports"


def export_v2_root() -> Path:
    """The root the isolated GLB seams publish under, separate from the bake corpus."""
    override = os.environ.get("ELYSIUM_EXPORT_V2_ROOT", "").strip()
    return Path(override).expanduser().resolve() if override else work_root() / "exports_v2"


def research_root() -> Path:
    return work_root() / "research"


def cache_root() -> Path:
    return work_root() / "cache"


def log_root() -> Path:
    return work_root() / "logs"


def scratch_root() -> Path:
    return work_root() / "scratch"
