"""Deploy VtMB's authored UI string table from the published `ui-resource` units.

`uv run elysium import ui-strings` reads `$ELYSIUM_EXPORT_V2_ROOT/ui-resources/**/*.glb`, lifts
each unit's source capsule (schema 1.1.0) and writes the one member the runtime opens:

```text
Content/ElysiumCorpus/ui/resource/gameui_english.txt
```

`FElysiumContentPaths::UiStrings()` is that file, and `FElysiumUIStrings` parses it as the Valve
KeyValues document the install ships -- `"lang" { "Tokens" { "<token>" "<text>" ... } }`, UTF-16 LE
with a byte-order mark. It is deployed verbatim rather than as a derived table because the capsule
is the only byte-exact thing a unit carries, and a corpus lane that transformed its input would be
the one lane whose output no digest could prove.

## Why one member out of fifty-two

The `ui-resource` seam publishes every `.res` layout, both schemes, the HUD sprite tables, the
key-binding tables and the launcher/options scripts. The port reads none of them: its screens are
a modern re-skin authored in Slate, VtMB's own layouts are structural reference
(`docs/vtmb/vtmb-ui.md`), and every picture the UI draws is an imported `T_` asset
(`UI/ElysiumUiArt.h`). The authored string table is the single exception -- menu labels are
`VMainMenu_BTN_*` tokens resolved at draw time -- so it is the single member deployed.
`scripts/kb_trans.lst`, the other UTF-16 member, has no runtime reader either.

Everything else -- recipe stamps, per-unit failure isolation, byte-equality verification against
the capsule, `import_report.json`, pruning -- is `importers/corpus_deploy.py`'s.
"""

from __future__ import annotations

from pathlib import Path

from elysium_pipeline.formats.ui_resource_glb import UI_RESOURCE_EXTENSION
from elysium_pipeline.importers import corpus_deploy
from elysium_pipeline.importers.corpus_deploy import (
    CorpusImportError,
    ImportResult,
    Lane,
    corpus_root,
)

LANE = "ui-strings"

#: The `export_v2` family directory this lane reads.
FAMILY = "ui-resources"

#: The one member with a runtime reader, spelled as the install spells it.
STRINGS_MEMBER = "resource/gameui_english.txt"

#: Where it lands, corpus-relative: below `ui/`, keeping the member's own `resource/` directory so
#: a second UI member added later needs no rule beyond its name.
CORPUS_ROOT = "ui"

#: Bumped whenever the mapping from a unit to the files it deploys changes, which discards every
#: stamp written under the old one. v1: one file, from one of the seam's 52 units.
RECIPE_VERSION = "elysium-ui-strings-corpus-v1"


def target_of(source_path: str) -> tuple[str, ...]:
    """Where one capsuled ui-resource member lands, corpus-relative.

    Exactly one member deploys; every other unit of the family contributes nothing and is a
    no-op rather than a failure, because "the port does not read this" is not a defect.
    """

    relative = str(source_path)
    if not relative or relative.endswith("/"):
        raise CorpusImportError(f"{source_path!r} names no ui-resource file")
    if relative.lower() != STRINGS_MEMBER:
        return ()
    return (f"{CORPUS_ROOT}/{STRINGS_MEMBER}",)


LANE_SPEC = Lane(
    name=LANE,
    recipe_version=RECIPE_VERSION,
    families=((FAMILY, UI_RESOURCE_EXTENSION),),
    target_of=target_of,
    owned_directories=(CORPUS_ROOT,),
    empty_hint="run `uv run elysium export_v2 ui-resources-glb` first",
)


def import_ui_strings(export_v2_root: Path, destination_root: Path) -> ImportResult:
    """Deploy the authored UI string table into `destination_root`."""

    return corpus_deploy.deploy_lane(export_v2_root, destination_root, LANE_SPEC)


__all__ = [
    "CORPUS_ROOT",
    "FAMILY",
    "LANE",
    "LANE_SPEC",
    "RECIPE_VERSION",
    "STRINGS_MEMBER",
    "CorpusImportError",
    "ImportResult",
    "corpus_root",
    "import_ui_strings",
    "target_of",
]
