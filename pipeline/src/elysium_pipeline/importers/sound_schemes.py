"""Deploy the sound-scheme corpus -- `sound/schemes/*.txt` -- from the published GLB units.

`uv run elysium import sound-schemes` reads `$ELYSIUM_EXPORT_V2_ROOT/sound-schemes/**/*.glb`,
lifts each unit's source capsule (schema 1.1.0) and writes the scheme file where the runtime
reads it:

```text
Content/ElysiumCorpus/sound/schemes/<stem>.txt
```

## Why the leaf is lower case

A map's `ambient_soundscheme` names its scheme as the install spells it --
`"sound/Schemes/SP_Tutorial_City.txt"` -- but the seam's identity key is the lower-cased stem
(`formats/sound_scheme_glb/model.py::normalize_stem`), and every other corpus family deploys
under its key. So does this one, and the runtime's `SchemeFile(Rel)` accessor folds before it
looks, exactly as the sound resolver and `NormalizeSceneRel` already do. Windows is
case-insensitive anyway, so the authored spelling resolves either way; the fold is what makes the
corpus reproducible on a case-sensitive filesystem.

## Sharing `sound/` with the audio lane

`sound/schemes/` is a subtree of the `sound` lane's own root. That lane names it in its
`foreign_directories`, so `import sound` steps over these files instead of pruning them as
orphans, and this lane prunes only `sound/schemes`. Neither lane can delete the other's deploy.

Everything else -- recipe stamps, per-unit failure isolation, byte-equality verification against
the capsule, `import_report.json`, pruning -- is `importers/corpus_deploy.py`'s.
"""

from __future__ import annotations

from pathlib import Path

from elysium_pipeline.formats.sound_scheme_glb import SOUND_SCHEME_EXTENSION
from elysium_pipeline.formats.sound_scheme_glb.model import SOURCE_ROOT, SOURCE_SUFFIX
from elysium_pipeline.importers import corpus_deploy
from elysium_pipeline.importers.corpus_deploy import (
    CorpusImportError,
    ImportResult,
    Lane,
    corpus_root,
)

LANE = "sound-schemes"

#: The `export_v2` family directory this lane reads, matching `export_manager`'s own
#: `family="sound-schemes"` for the seam.
FAMILY = "sound-schemes"

#: Bumped whenever the mapping from a unit to the files it deploys changes, which discards every
#: stamp written under the old one. v1: one `sound/schemes/<stem>.txt` per unit.
RECIPE_VERSION = "elysium-sound-schemes-corpus-v1"


def target_of(source_path: str) -> tuple[str, ...]:
    """Where one capsuled scheme member lands, corpus-relative.

    One member, one file, at the member's own install-relative path folded to lower case. The
    fold is belt and braces -- `normalize_stem` already lower-cased the key the member path was
    built from -- so that a unit published by some future writer that kept the install's spelling
    still deploys under the key the runtime looks for.
    """

    relative = str(source_path).lower()
    if not relative.startswith(SOURCE_ROOT) or not relative.endswith(SOURCE_SUFFIX):
        raise CorpusImportError(f"{source_path!r} is not a sound-scheme member")
    if len(relative) <= len(SOURCE_ROOT) + len(SOURCE_SUFFIX):
        raise CorpusImportError(f"{source_path!r} names no sound scheme")
    return (relative,)


LANE_SPEC = Lane(
    name=LANE,
    recipe_version=RECIPE_VERSION,
    families=((FAMILY, SOUND_SCHEME_EXTENSION),),
    target_of=target_of,
    owned_directories=("sound/schemes",),
    empty_hint="run `uv run elysium export_v2 sound-schemes-glb` first",
)


def import_sound_schemes(export_v2_root: Path, destination_root: Path) -> ImportResult:
    """Deploy every sound-scheme unit under `export_v2_root` into `destination_root`."""

    return corpus_deploy.deploy_lane(export_v2_root, destination_root, LANE_SPEC)


__all__ = [
    "FAMILY",
    "LANE",
    "LANE_SPEC",
    "RECIPE_VERSION",
    "CorpusImportError",
    "ImportResult",
    "corpus_root",
    "import_sound_schemes",
    "target_of",
]
