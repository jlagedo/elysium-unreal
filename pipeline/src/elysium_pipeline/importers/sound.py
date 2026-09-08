"""Deploy the sound corpus -- every `.wav`/`.mp3` and its `.lip` companion -- from the units.

`uv run elysium import sound` reads `$ELYSIUM_EXPORT_V2_ROOT/sounds/**/*.glb`, lifts each unit's
source capsule and writes it where the runtime reads it.

## The layout, and why it is this one

A sound unit carries two members: the audio file that selects it and, when the install ships one,
the same-stem `.lip` (`formats/sound_glb/source.py`). The runtime reads them through two
different accessors that today root at two different directories:

```text
FElysiumContentPaths::SoundFile(Rel)  ->  Root()/sound/<Rel>     e.g. character/dlg/a/line1.mp3
FElysiumContentPaths::LipFile(Rel)    ->  Root()/lip/<Rel>       e.g. character/dlg/a/line1.lip
```

`Rel` is the same string in both cases: `ElysiumLip::NormalizeLipRel` is
`ElysiumScene::NormalizeSceneRel` with the extension swapped, so a `.lip` is keyed by the audio's
own path below `sound/`, lower-cased and forward-slashed, and the legacy `lip/` mirror is that
key verbatim. So this lane deploys each `.lip` **twice**:

* `Content/ElysiumCorpus/sound/<rel>.lip` -- beside its audio, which is what the dialogue plan
  asks for ("deploys every sound unit -> `.../sound/**` with its `.lip` beside it, so
  `SoundDir()` has one root"); and
* `Content/ElysiumCorpus/lip/<rel>.lip` -- the legacy mirror's own shape, so flipping
  `LipDir()` from `Root()/lip` to `CorpusRoot()/lip` is a one-word change that needs no new fold.

Both spellings are the same bytes out of the same capsule, so whichever of the two roots the C++
slice settles on, the file it opens is the install's. The duplication costs ~31 MB across 7,136
documents -- a rounding error beside the ~1 GB of audio -- and buys the C++ flip the freedom to go
either way without a re-import. Retiring one spelling is a `RECIPE_VERSION` bump and one edit to
`target_of`, which prunes the other tree on the next run.

Paths are the units' own keys, which are folded to lower case. The legacy `sound/` mirror kept the
install's mixed case (`sound/Area/Chinatown/Asian_Chimes1.wav`); the corpus does not, matching
every other corpus family and the fold every runtime reader already applies before it looks
(`NormalizeSceneRel`, and the sound resolver's own fold). Windows is case-insensitive, so a raw
`ambient_generic` `message` value resolves against either spelling.

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
RECIPE_VERSION = "elysium-sound-corpus-v1"

#: The install prefix every member of a sound unit carries.
SOUND_ROOT = "sound/"
#: The second home a `.lip` gets, keyed exactly as `ElysiumLip::NormalizeLipRel` keys it.
LIP_CORPUS_ROOT = "lip/"

AUDIO_EXTENSIONS = (".wav", ".mp3")
LIP_EXTENSION = ".lip"


def target_of(source_path: str) -> tuple[str, ...]:
    """Where one capsuled member lands, corpus-relative.

    Audio deploys once, below `sound/`. A `.lip` deploys twice -- see this module's docstring for
    the two readers that each want their own root.
    """

    if not source_path.startswith(SOUND_ROOT):
        raise CorpusImportError(f"{source_path!r} is not a member below sound/")
    relative = source_path[len(SOUND_ROOT):]
    if source_path.endswith(AUDIO_EXTENSIONS):
        return (source_path,)
    if source_path.endswith(LIP_EXTENSION):
        return (source_path, LIP_CORPUS_ROOT + relative)
    raise CorpusImportError(f"{source_path!r} is not an audio member or a .lip companion")


#: `sound/schemes/*.txt` sits inside this lane's own root but is the `sound-schemes` lane's
#: deploy (`importers/sound_schemes.py`), so it is stepped over rather than pruned as an orphan.
FOREIGN_DIRECTORIES = ("sound/schemes",)

LANE_SPEC = Lane(
    name=LANE,
    recipe_version=RECIPE_VERSION,
    families=(("sounds", SOUND_EXTENSION),),
    target_of=target_of,
    owned_directories=("sound", "lip"),
    empty_hint="run `uv run elysium export_v2 sounds-glb` first",
    foreign_directories=FOREIGN_DIRECTORIES,
)


def import_sound(export_v2_root: Path, destination_root: Path) -> ImportResult:
    """Deploy every sound unit under `export_v2_root` into `destination_root`."""

    return corpus_deploy.deploy_lane(export_v2_root, destination_root, LANE_SPEC)


__all__ = [
    "FOREIGN_DIRECTORIES",
    "LANE",
    "LANE_SPEC",
    "RECIPE_VERSION",
    "CorpusImportError",
    "ImportResult",
    "corpus_root",
    "import_sound",
    "target_of",
]
