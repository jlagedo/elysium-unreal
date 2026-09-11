"""Deploy the `.lip` companions of the sound corpus -- and nothing else -- from the units.

`uv run elysium import sound` reads `$ELYSIUM_EXPORT_V2_ROOT/sounds/**/*.glb`, lifts each unit's
`.lip` member out of its source capsule and writes it where the runtime reads it:

```text
FElysiumContentPaths::LipFile(Rel)  ->  CorpusRoot()/lip/<Rel>   e.g. character/dlg/a/line1.lip
```

`Rel` is the audio's own path below the install's `sound/`, lower-cased and forward-slashed:
`ElysiumLip::NormalizeLipRel` is `ElysiumScene::NormalizeSceneRel` with the extension swapped, so
the mirror's key is the audio key with `.lip` for `.wav`/`.mp3`.

## Why no audio (AUD1, owner call 2026-09-08)

Sound rendering moved to baked `USoundWave` assets: `uv run elysium bake sounds`
(`importers/sounds_bake.py` + `pipeline/unreal/import_sounds.py`) writes
`/ElysiumBaked/Sounds/**/SW_<name>` and the runtime addresses them by package path
(`asset_paths.baked_unit("vtmb:sound:" + key, "SW")`). The C++ side deleted `SoundDir()` and
`SoundFile()` outright, so not one audio byte below `Content/ElysiumCorpus/sound` is read any
more. Deploying ~1 GB of `.wav`/`.mp3` a second time, loose, would be a second way to reach bytes
that already live in an asset -- so this lane stopped. Same reasoning retired the *beside-audio*
`.lip` spelling (`sound/<rel>.lip`): with no audio next to it there is nothing for it to be beside,
and `LipFile` was always the accessor that mattered.

## Who prunes the retired `sound/` tree

The lane keeps `sound` in `owned_directories` even though it no longer writes a single file
there, and keeps `sound/schemes` in `foreign_directories`. `corpus_deploy._prune` deletes
everything under an owned directory that the run neither wrote nor kept and steps over a foreign
subtree whole, so the first run under the new `RECIPE_VERSION` sweeps the ~1 GB of orphaned audio
and the 7,105 beside-audio lips out of an existing deployment, leaves `sound/schemes/*.txt` (the
`sound-schemes` lane's deploy) alone, and every later run keeps that tree swept for free.

A one-time sweep guarded by a marker file would do the same once and then rot; declaring `sound`
prune-only says the true thing -- *this lane owns that tree and deploys nothing into it* -- in the
mechanism the lane already has, with no new code and no state to age out. The two lanes stay
exactly as consistent as before: `sound-schemes` owns `sound/schemes` and prunes only that, this
lane owns `sound` + `lip` and cannot touch the schemes.

Everything else -- recipe stamps, per-unit failure isolation, byte-equality verification against
the capsule, `import_report.json`, pruning -- is `importers/corpus_deploy.py`'s.
"""

from __future__ import annotations

from pathlib import Path

from elysium_pipeline.formats.sound_glb.model import SOUND_EXTENSION
from elysium_pipeline.importers import corpus_deploy
from elysium_pipeline.importers.corpus_deploy import (
    CorpusImportError,
    ImportResult,
    Lane,
    corpus_root,
)

LANE = "sound"

#: Bumped whenever the mapping from a unit to the files it deploys changes, which discards every
#: stamp written under the old one. v1: `sound/**` audio, `.lip` beside it and mirrored to `lip/**`.
#: v2 (AUD1.4): the `lip/**` mirror only -- audio is a baked `USoundWave`, and the first run under
#: this version prunes the retired loose audio and the beside-audio lips.
RECIPE_VERSION = "elysium-sound-corpus-v2"

#: The install prefix every member of a sound unit carries.
SOUND_ROOT = "sound/"
#: The one home a `.lip` gets, keyed exactly as `ElysiumLip::NormalizeLipRel` keys it.
LIP_CORPUS_ROOT = "lip/"

AUDIO_EXTENSIONS = (".wav", ".mp3")
LIP_EXTENSION = ".lip"


def target_of(source_path: str) -> tuple[str, ...]:
    """Where one capsuled member lands, corpus-relative.

    A `.lip` deploys once, under `lip/`. An audio member deploys nowhere: it is a baked
    `USoundWave` now, so the lane recognises it -- an unknown member is still a defect -- and
    returns no target.
    """

    if not source_path.startswith(SOUND_ROOT):
        raise CorpusImportError(f"{source_path!r} is not a member below sound/")
    relative = source_path[len(SOUND_ROOT):]
    if source_path.endswith(AUDIO_EXTENSIONS):
        return ()
    if source_path.endswith(LIP_EXTENSION):
        return (LIP_CORPUS_ROOT + relative,)
    raise CorpusImportError(f"{source_path!r} is not an audio member or a .lip companion")


#: `sound/schemes/*.txt` sits inside this lane's own root but is the `sound-schemes` lane's
#: deploy (`importers/sound_schemes.py`), so it is stepped over rather than pruned as an orphan.
FOREIGN_DIRECTORIES = ("sound/schemes",)

#: `lip` is where this lane writes; `sound` it owns for the prune alone -- see the module
#: docstring. Dropping `sound` here would strand ~1 GB of retired audio in every deployment that
#: ever ran the old recipe.
OWNED_DIRECTORIES = ("sound", "lip")

LANE_SPEC = Lane(
    name=LANE,
    recipe_version=RECIPE_VERSION,
    families=(("sounds", SOUND_EXTENSION),),
    target_of=target_of,
    owned_directories=OWNED_DIRECTORIES,
    empty_hint="run `uv run elysium export_v2 sounds-glb` first",
    foreign_directories=FOREIGN_DIRECTORIES,
)


def import_sound(export_v2_root: Path, destination_root: Path) -> ImportResult:
    """Deploy every sound unit's `.lip` under `export_v2_root` into `destination_root`."""

    return corpus_deploy.deploy_lane(export_v2_root, destination_root, LANE_SPEC)


__all__ = [
    "FOREIGN_DIRECTORIES",
    "LANE",
    "LANE_SPEC",
    "OWNED_DIRECTORIES",
    "RECIPE_VERSION",
    "CorpusImportError",
    "ImportResult",
    "corpus_root",
    "import_sound",
    "target_of",
]
