"""Deploy VtMB's level scripts -- the Python 2.1 tree the embedded VM imports -- from the units.

`uv run elysium import scripts` reads `$ELYSIUM_EXPORT_V2_ROOT/scripts/**/*.glb`, lifts each unit's
source capsule (schema 1.1.0) and writes the source where the runtime reads it:

```text
Content/ElysiumCorpus/scripts/<path>.py
```

`FElysiumContentPaths::ScriptsDir()` is that directory, and it is what
`UElysiumPythonVM` puts on `sys.path` and what the script filesystem's `python/` mount serves.

## Why only the `.py`

A unit keyed `<path>` carries the `.py` the UP-first policy selected and, where one ships, the
same-stem `.pyc` as the companion role `pyc`. Only the source is deployed:

  * CPython 2.1 reads a `.pyc` only when it has no `.py` sibling, or when the modification time
    the compiler wrote into it matches the sibling's on disk. A deploy gives both files fresh
    times, so a deployed `.pyc` could never validate and the interpreter would recompile from the
    source anyway -- and then overwrite the deployed file, which the next run would rewrite, for
    ever. The bytes the game runs are the source's either way.
  * The 24 `.pyc` that ship only inside a VPK never ran at all: CPython 2.1 predates `zipimport`,
    so the interpreter could not read a pack even in retail.

Every unit the corpus publishes resolves a `.py`, so this deploys the whole executable tree. A
`pyc-only` unit -- the seam admits one, keyed by a `.pyc` with no source sibling -- is the case
this rule would silently drop a module for, so `refuse_compiled_only` makes it a named failure
instead. If the install ever grows one, that is the signal to decide what the VM should import.

## The `.pyc` the VM writes back

CPython 2.1 compiles beside the source it imports and has no `dont_write_bytecode` to stop it, so
`Content/ElysiumCorpus/scripts/**` fills with `.pyc` files this lane never wrote. They are the
runtime's own, not orphans of a retired unit, so the lane names `.pyc` in `kept_suffixes` and the
prune steps over them.

Everything else -- recipe stamps, per-unit failure isolation, byte-equality verification against
the capsule, `import_report.json`, pruning -- is `importers/corpus_deploy.py`'s.
"""

from __future__ import annotations

from pathlib import Path
from typing import Sequence

from elysium_pipeline.formats.script_glb import (
    COMPILED_EXTENSION,
    SCRIPT_EXTENSION,
    SCRIPT_ROOT,
    SOURCE_EXTENSION,
)
from elysium_pipeline.importers import corpus_deploy
from elysium_pipeline.importers.corpus_deploy import (
    CorpusImportError,
    ImportResult,
    Lane,
    corpus_root,
)

LANE = "scripts"

#: The `export_v2` family directory this lane reads, matching `export_manager`'s own
#: `family="scripts"` for the seam.
FAMILY = "scripts"

#: Where the tree lands, corpus-relative. NOT `python/`: the runtime has called it `scripts/`
#: since the first mirror, `ScriptsDir()` names it, and the script filesystem's `python/` mount
#: is the one place the install's own spelling is answered.
CORPUS_ROOT = "scripts"

#: Bumped whenever the mapping from a unit to the files it deploys changes, which discards every
#: stamp written under the old one. v1: one `scripts/<path>.py` per unit, the companion declined.
RECIPE_VERSION = "elysium-scripts-corpus-v1"


def target_of(source_path: str) -> tuple[str, ...]:
    """Where one capsuled script member lands, corpus-relative.

    The source member lands under `scripts/` at its path below `python/`; the compiled companion
    lands nowhere, for the reasons the module docstring gives.
    """

    relative = str(source_path)
    if not relative.startswith(SCRIPT_ROOT):
        raise CorpusImportError(f"{source_path!r} is not a script member")
    below = relative[len(SCRIPT_ROOT):]
    if not below:
        raise CorpusImportError(f"{source_path!r} names no script")
    if below.lower().endswith(COMPILED_EXTENSION):
        return ()
    if not below.lower().endswith(SOURCE_EXTENSION):
        raise CorpusImportError(f"{source_path!r} is neither Python source nor its companion")
    return (f"{CORPUS_ROOT}/{below}",)


def refuse_compiled_only(member_paths: Sequence[str]) -> None:
    """A unit that resolved a companion and no source deploys nothing, so it is named, not skipped.

    `identity.sourceKind` is `pyc-only` for such a unit. Dropping it quietly would cost a module
    the VM imports and surface as a map failing to load for no stated reason.
    """

    if not any(str(path).lower().endswith(SOURCE_EXTENSION) for path in member_paths):
        raise CorpusImportError(
            "the unit resolved no Python source, only a compiled companion this lane does not "
            "deploy; decide what the VM should import before it can be deployed"
        )


LANE_SPEC = Lane(
    name=LANE,
    recipe_version=RECIPE_VERSION,
    families=((FAMILY, SCRIPT_EXTENSION),),
    target_of=target_of,
    owned_directories=(CORPUS_ROOT,),
    empty_hint="run `uv run elysium export_v2 scripts-glb` first",
    kept_suffixes=(COMPILED_EXTENSION,),
    unit_guard=refuse_compiled_only,
)


def import_scripts(export_v2_root: Path, destination_root: Path) -> ImportResult:
    """Deploy every script unit under `export_v2_root` into `destination_root`."""

    return corpus_deploy.deploy_lane(export_v2_root, destination_root, LANE_SPEC)


__all__ = [
    "CORPUS_ROOT",
    "FAMILY",
    "LANE",
    "LANE_SPEC",
    "RECIPE_VERSION",
    "CorpusImportError",
    "ImportResult",
    "corpus_root",
    "import_scripts",
    "refuse_compiled_only",
    "target_of",
]
