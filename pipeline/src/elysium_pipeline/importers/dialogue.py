"""Deploy the dialogue corpus -- `.dlg` conversations and their `.vcd` scenes -- from the units.

`uv run elysium import dialogue` reads `$ELYSIUM_EXPORT_V2_ROOT/dialogues/**/*.glb` and
`.../scenes/**/*.glb`, lifts each unit's source capsule and writes it where the runtime reads it:

```text
Content/ElysiumCorpus/dlg/<hub>/<name>.dlg          <- FElysiumContentPaths::DlgFromDialogname
Content/ElysiumCorpus/scenes/<path>.vcd             <- FElysiumContentPaths::SceneFile
```

Both trees mirror the legacy loose export (`exporters/UE_extract_scripts.py` for `dlg/`,
`UE_extract_scenes.py` for `scenes/`) exactly, so the reader flip from `Root()` to `CorpusRoot()`
is a change of prefix and nothing else. A scene's install path is `sound/<rel>.vcd` and the
runtime addresses it by `<rel>` with that prefix already stripped
(`ElysiumScene::NormalizeSceneRel`), so this lane strips it too.

Everything else -- recipe stamps, per-unit failure isolation, byte-equality verification against
the capsule, `import_report.json`, pruning -- is `importers/corpus_deploy.py`'s.
"""

from __future__ import annotations

from pathlib import Path

from elysium_pipeline.formats.dialogue_glb.model import DIALOGUE_EXTENSION
from elysium_pipeline.formats.scene_glb.model import SCENE_EXTENSION
from elysium_pipeline.importers import corpus_deploy
from elysium_pipeline.importers.corpus_deploy import (
    CorpusImportError,
    ImportResult,
    Lane,
    corpus_root,
)

LANE = "dialogue"

#: Bumped whenever the mapping from a unit to the files it deploys changes, which discards every
#: stamp written under the old one. v1: `dlg/**` and `scenes/**`, capsule bytes, verbatim.
RECIPE_VERSION = "elysium-dialogue-corpus-v1"

#: The install prefix a `.dlg` member carries, kept in the corpus so `DlgDir()` is one rename.
DLG_ROOT = "dlg/"
#: The install prefix a `.vcd` member carries, and the corpus directory it is re-rooted under.
SCENE_SOURCE_ROOT = "sound/"
SCENE_CORPUS_ROOT = "scenes/"


def target_of(source_path: str) -> tuple[str, ...]:
    """Where one capsuled member lands, corpus-relative. An unexpected member deploys nowhere."""

    if source_path.startswith(DLG_ROOT) and source_path.endswith(".dlg"):
        return (source_path,)
    if source_path.startswith(SCENE_SOURCE_ROOT) and source_path.endswith(".vcd"):
        return (SCENE_CORPUS_ROOT + source_path[len(SCENE_SOURCE_ROOT):],)
    raise CorpusImportError(f"{source_path!r} is neither a dialogue nor a scene member")


LANE_SPEC = Lane(
    name=LANE,
    recipe_version=RECIPE_VERSION,
    families=(("dialogues", DIALOGUE_EXTENSION), ("scenes", SCENE_EXTENSION)),
    target_of=target_of,
    owned_directories=("dlg", "scenes"),
    empty_hint="run `uv run elysium export_v2 dialogues-glb` and `... scenes-glb` first",
)


def import_dialogue(export_v2_root: Path, destination_root: Path) -> ImportResult:
    """Deploy every dialogue and scene unit under `export_v2_root` into `destination_root`."""

    return corpus_deploy.deploy_lane(export_v2_root, destination_root, LANE_SPEC)


__all__ = [
    "LANE",
    "LANE_SPEC",
    "RECIPE_VERSION",
    "CorpusImportError",
    "ImportResult",
    "corpus_root",
    "import_dialogue",
    "target_of",
]
