"""Process-wide setup the pipeline tests share, in one place.

Two things used to be done by the test modules themselves, at import, and each was a hazard:

**The roots.** `formats.install` and the research tools the tests import (`name_passes`,
`crt_match`, the kernel tools) resolve `ELYSIUM_WORK_ROOT` / `ELYSIUM_VTMB_ROOT` when they are
*imported*, and a test module imports them at its top, so the roots have to exist before
collection -- which a fixture cannot do. Twelve modules therefore ran
`os.environ.setdefault("ELYSIUM_WORK_ROOT", tempfile.gettempdir())` as they were collected: the
process environment was mutated by whichever module happened to be imported first, to the machine's
shared temp directory. The root `conftest.py` already applies a configured checkout's roots; the
hook below covers only the checkout that has none, and gives it throwaway roots that are removed
afterwards instead of the shared temp directory.

**`formats.install`'s constants.** The module freezes `ELYSIUM_VTMB_ROOT` into `GAME`, `PATCH` and
`LOOSE_ROOTS` the first time it is imported. A test that monkeypatches that variable and then
imports `install` for the first time in the process (`test_oracle_source`'s
`test_default_source_uses_winning_install_index` is one) bakes its own `tmp_path` into the module
for every later test, and `test_gen_contents_signatures::...as_the_probe_does` then reads a
`.bsp` out of a directory pytest already removed. Whether that happens depends on test order and on
which files a worker collected, so it fails under chunking and under `-n`. Importing the module
here, once, under the real environment, makes the order irrelevant.
"""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import tempfile

_ROOT_VARIABLES = {
    "ELYSIUM_WORK_ROOT": "work",
    "ELYSIUM_VTMB_ROOT": "vtmb",
}

#: The throwaway tree this process made for a checkout with no roots, or `None`.
_fallback_tree: Path | None = None


def pytest_configure(config) -> None:
    global _fallback_tree
    missing = [name for name in _ROOT_VARIABLES if not os.environ.get(name, "").strip()]
    if missing:
        _fallback_tree = Path(tempfile.mkdtemp(prefix="elysium-pytest-roots-"))
        for name in missing:
            root = _fallback_tree / _ROOT_VARIABLES[name]
            root.mkdir()
            # Set before `-n` workers spawn, so each inherits it rather than making its own.
            os.environ[name] = str(root)
    try:
        import elysium_pipeline.formats.install  # noqa: F401
    except Exception:
        # A configured root that is not usable is reported by the test that needs it, with its own
        # traceback; failing the whole session from a hook would hide every other test.
        pass


def pytest_unconfigure(config) -> None:
    global _fallback_tree
    if _fallback_tree is not None:
        shutil.rmtree(_fallback_tree, ignore_errors=True)
        _fallback_tree = None
