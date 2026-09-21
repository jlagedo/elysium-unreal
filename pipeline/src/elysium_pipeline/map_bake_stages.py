"""The map bake's stages, named once for both halves of the lane.

`bake map` runs in two processes -- this interpreter stages the offline inputs, a commandlet
authors -- and `--from <stage>` has to mean the same thing in both: the CLI refuses an unknown
stage before paying for an editor boot, and `pipeline/unreal/bake_map.py` turns the name into the
set its `AssetTracker` forces. One list, so the two cannot drift.

Added by 0018 story 21-2, which folded the three `import map-*` commands into the bake and so
needed a way to re-run part of it.
"""

from __future__ import annotations

#: The stages, in the order `bake_map.bake_one` runs them.
#:
#: There is deliberately no `nav` stage. The nav-area marks have to be placed before the meshes
#: are cut, the meshes before the level is written, and the level is written by `level` -- so
#: navigation is not separable from it. `props` is absent because only the corpus bake has one.
STAGE_ORDER = (
    "textures", "materials", "world", "sky", "particles",
    "entities", "environment", "collision", "level",
)


def stages_from(stage: str) -> frozenset[str]:
    """`--from <stage>`: that stage and every one after it.

    It FORCES rather than skips, and it has to. `stage_level` authors from tables the earlier
    stages fill on their reuse paths as well as their build paths -- the resolved materials, the
    world chunk meshes, the staged geometry rows -- and it computes the level's own recipe from
    them. A stage that did not run leaves those empty, so a level stamped after skipping one
    carries a hash that means nothing. Every stage always runs; this only decides which of them
    may reuse what is already on the mount.
    """
    if not stage:
        return frozenset()
    if stage not in STAGE_ORDER:
        raise ValueError(
            f"{stage!r} is not a bake stage; the stages are: {', '.join(STAGE_ORDER)}")
    return frozenset(STAGE_ORDER[STAGE_ORDER.index(stage):])
