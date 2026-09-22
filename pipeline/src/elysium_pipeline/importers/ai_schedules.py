"""Deploy VtMB's NPC behaviour programs from the published GLB units.

`uv run elysium import ai-schedules` reads `$ELYSIUM_EXPORT_V2_ROOT/ai-schedules/**/*.glb` and
writes what the runtime opens:

```text
Content/ElysiumCorpus/ai/schedules/vocabulary.json        the parser's tables, from the root unit
Content/ElysiumCorpus/ai/schedules/<space>/space.json     the four spaces and their registrations
Content/ElysiumCorpus/ai/schedules/<space>/<name>.sch     one schedule text, verbatim
```

`FElysiumContentPaths::CorpusRoot()` is the root those sit under. The runtime reads the deployed
corpus and never the GLB, which is the rule `docs/specs/0018-world-ai-infrastructure/spec.md`
§ Scope states for every lane.

## Why this lane writes two kinds of byte

A `.sch` file is a **capsule**: the exact `.data` bytes retail's own parser read, proven by digest
at export and by read-back here. Nothing about it is this project's.

The two `.json` files are **derived products** (`seam_map_unit_contract.md`, "Derived products").
A class's registrations and its space parents are recovered from literal immediates ahead of call
sites -- no byte of `vampire.dll` spells "SCHED_VBRUJAH_WALK is 0x158" as data -- so they cannot be
capsule bytes under any arrangement, and a runtime that had only the texts could not build an id
space to compile them against. They are a pure function of the unit's own extension root, written
through the same atomic write, read-back, stamp and prune as a capsule.

## One file per text, not per owner

Retail's parser fails per text and its caller stops that class's load at the first failure, so
"which text failed" should be a filename in the log rather than a line number inside a blob. The
leaf is the schedule's own name, lower-cased.

Everything else -- recipe stamps, per-unit failure isolation, byte-equality verification,
`import_report.json`, pruning -- is `importers/corpus_deploy.py`'s.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Mapping

from elysium_pipeline.formats.ai_schedule_glb import (
    AI_SCHEDULE_EXTENSION,
    CORPUS_ROOT,
    ROOT_KEY,
    split_member_path,
)
from elysium_pipeline.importers import corpus_deploy
from elysium_pipeline.importers.corpus_deploy import (
    CorpusImportError,
    ImportResult,
    Lane,
    corpus_root,
)

LANE = "ai-schedules"

#: The `export_v2` family directory this lane reads.
FAMILY = "ai-schedules"

#: Bumped whenever the mapping from a unit to the files it deploys changes, which discards every
#: stamp written under the old one. v1: one `.sch` per text, one `space.json` per space unit, one
#: `vocabulary.json` from the root.
RECIPE_VERSION = "elysium-ai-schedules-corpus-v1"

#: The sidecar every space unit deploys beside its texts.
SPACE_SIDECAR = "space.json"

#: The root unit's derived product.
VOCABULARY_FILE = "vocabulary.json"


def target_of(source_path: str) -> tuple[str, ...]:
    """Where one capsuled schedule text lands, corpus-relative.

    A member path is `dlls/vampire.dll#ai/schedules/<space>/<name>.sch`: the tail after the `#`
    IS the deploy path, so this lane splits one string and knows nothing else about the seam. The
    root unit's member is the image itself and carries no `#`, so it deploys nowhere.
    """

    relative = str(source_path)
    if not relative:
        raise CorpusImportError("an ai-schedule member names no path")
    tail = split_member_path(relative)
    if tail is None:
        return ()
    if tail.endswith("/") or len(tail) <= len(CORPUS_ROOT):
        raise CorpusImportError(f"{source_path!r} names no schedule text")
    return (tail,)


def _serialize(body: Any) -> bytes:
    """The one spelling every derived product uses, so a stamp can trust its own digest."""

    return (json.dumps(body, indent=1, sort_keys=True) + "\n").encode("utf-8")


def derived_of(root: Mapping[str, Any]) -> dict[str, bytes]:
    """The JSON a unit deploys beside its texts, as a pure function of the unit."""

    identity = root.get("identity") or {}
    asset = str(identity.get("asset") or "")
    key = asset.rpartition(":")[2]
    if not key:
        raise CorpusImportError("an ai-schedule unit publishes no identity")

    if key == ROOT_KEY:
        return {
            f"{CORPUS_ROOT}{VOCABULARY_FILE}": _serialize(
                {
                    "namespaces": root.get("namespaces") or [],
                    "squadSlots": root.get("squadSlots") or [],
                    "vocabulary": root.get("vocabulary") or {},
                    "classes": root.get("classes") or [],
                    "order": root.get("order") or {},
                    "deadDoors": root.get("deadDoors") or [],
                    "census": root.get("census") or {},
                }
            )
        }

    return {
        f"{CORPUS_ROOT}{key}/{SPACE_SIDECAR}": _serialize(
            {
                "className": identity.get("className"),
                "classNames": identity.get("classNames") or [],
                "initBody": identity.get("initBody"),
                "spaces": root.get("spaces") or {},
                "registrations": root.get("registrations") or {},
                "texts": root.get("texts") or [],
            }
        )
    }


LANE_SPEC = Lane(
    name=LANE,
    recipe_version=RECIPE_VERSION,
    families=((FAMILY, AI_SCHEDULE_EXTENSION),),
    target_of=target_of,
    # `ai/schedules` rather than `ai`, so the rest of a future `ai/` tree stays another lane's.
    owned_directories=("ai/schedules",),
    empty_hint="run `uv run elysium export_v2 ai-schedules-glb` first",
    derived_of=derived_of,
)


def import_ai_schedules(export_v2_root: Path, destination_root: Path) -> ImportResult:
    """Deploy every ai-schedule unit's texts and sidecar into `destination_root`."""

    return corpus_deploy.deploy_lane(export_v2_root, destination_root, LANE_SPEC)


__all__ = [
    "FAMILY",
    "LANE",
    "LANE_SPEC",
    "RECIPE_VERSION",
    "SPACE_SIDECAR",
    "VOCABULARY_FILE",
    "CorpusImportError",
    "ImportResult",
    "corpus_root",
    "derived_of",
    "import_ai_schedules",
    "target_of",
]
