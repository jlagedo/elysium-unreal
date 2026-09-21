"""Deploy VtMB's console configuration -- `cfg/*` -- from the published GLB units.

`uv run elysium import engine-config` reads `$ELYSIUM_EXPORT_V2_ROOT/engine-config/**/*.glb`,
lifts each unit's source capsule (schema 1.1.0) and writes the file where the runtime reads it:

```text
Content/ElysiumCorpus/cfg/<name>
```

`FElysiumContentPaths::CfgDir()` is that directory. Two readers open it: the console store the
command bus and the Python VM seed their alias/cvar tables from
(`FElysiumConsoleStore::EnsureSeeded` / `LoadFromCfgDir`), and the script filesystem's `cfg/`
mount, which is what makes the Unofficial Patch's `FixKeyBindings` resolve.

## Why only `cfg/`

The seam is a closed vocabulary of 17 install members, and only the nine under `cfg/` have a
runtime reader. The rest are offline inputs to tools this project does not run:

| Declined | What it is |
|---|---|
| `lights.rad`, `detail.vbsp` | VBSP/VRAD compiler tables; the map bake reads the units, never a file |
| `maps/loadorder.txt` | the retail launcher's map order |
| `pack_values.txt`, `localized_list.txt` | the VPK packer's configuration and its localized-file list |
| `vidcfg.bin` (20 bytes), `voice_ban.dt` (4) | retail's own binary state files |
| `hl2.tmp` | a save fragment the corpus index already carries as residue -- no shipped code path reads it |

Deploying them would put bytes into a cooked game that nothing opens. Each is still published as
a unit and still decoded; what this lane decides is only what reaches `Content/`.

`cfg/` members whose names begin `elysium_` never arrive here either, and not by this lane's
choice: they are this project's own retail-capture scripts written into the patch tree
(`research/tooling/capture/`), which the corpus index classifies `foreign-file` and the seam does
not export. The Unreal port reads none of them.

Everything else -- recipe stamps, per-unit failure isolation, byte-equality verification against
the capsule, `import_report.json`, pruning -- is `importers/corpus_deploy.py`'s.
"""

from __future__ import annotations

from pathlib import Path

from elysium_pipeline.formats.engine_config_glb import ENGINE_CONFIG_EXTENSION
from elysium_pipeline.importers import corpus_deploy
from elysium_pipeline.importers.corpus_deploy import (
    CorpusImportError,
    ImportResult,
    Lane,
    corpus_root,
)

LANE = "engine-config"

#: The `export_v2` family directory this lane reads.
FAMILY = "engine-config"

#: The one install subtree with a runtime reader, and the corpus directory it lands in unchanged.
CFG_ROOT = "cfg/"

#: Bumped whenever the mapping from a unit to the files it deploys changes, which discards every
#: stamp written under the old one. v1: the nine `cfg/` members, the other eight declined.
RECIPE_VERSION = "elysium-engine-config-corpus-v1"


def target_of(source_path: str) -> tuple[str, ...]:
    """Where one capsuled engine-config member lands, corpus-relative.

    A `cfg/` member keeps its install-relative path exactly; every other member of this seam
    deploys nowhere. The member path is the one the unit published for itself, so a path that
    names no engine-config file at all is a defect in the unit rather than a file to write.
    """

    relative = str(source_path)
    if not relative or relative.endswith("/"):
        raise CorpusImportError(f"{source_path!r} names no engine-config file")
    if not relative.startswith(CFG_ROOT):
        return ()
    if len(relative) <= len(CFG_ROOT):
        raise CorpusImportError(f"{source_path!r} names no console config")
    return (relative,)


LANE_SPEC = Lane(
    name=LANE,
    recipe_version=RECIPE_VERSION,
    families=((FAMILY, ENGINE_CONFIG_EXTENSION),),
    target_of=target_of,
    owned_directories=("cfg",),
    empty_hint="run `uv run elysium export_v2 engine-configs-glb` first",
)


def import_engine_config(export_v2_root: Path, destination_root: Path) -> ImportResult:
    """Deploy every engine-config unit's `cfg/` member into `destination_root`."""

    return corpus_deploy.deploy_lane(export_v2_root, destination_root, LANE_SPEC)


__all__ = [
    "CFG_ROOT",
    "FAMILY",
    "LANE",
    "LANE_SPEC",
    "RECIPE_VERSION",
    "CorpusImportError",
    "ImportResult",
    "corpus_root",
    "import_engine_config",
    "target_of",
]
