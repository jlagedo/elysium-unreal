"""Which parts of the character bake this run still has to do.

The map bake has had per-stage receipts since it was staged, and it shows: a fully cached map
export finishes in ten seconds and launches no editor at all. The character bake had none, so
every run rebuilt every skeleton, re-imported every texture and re-baked every clip the slice
reached, whatever had actually changed.

The unit here is a **scope** — one rig family, or the global set the whole cast shares — rather
than a map. A scope's stage is stale when its declared inputs or the code that turns them into
assets have changed since the receipt was written; everything else is reused untouched.

Cascades are deliberately conservative, because a wrong reuse is a silently wrong asset and a
wrong rebuild only costs seconds:

- a family's skeleton changing invalidates that family's meshes and clips, because an untracked
  bone falls back to the skeleton's reference pose and a blend mask is content-addressed against
  its bone set;
- a BANK family's skeleton changing invalidates that bank family's clips in full and every model
  family's skeleton, because the compatibility declaration names it;
- a mesh changing does NOT invalidate its family's clips. That is where the iteration win lives:
  a re-textured body costs one mesh and touches no sequence.

The receipts live in the export manifest the offline half already keeps, so `--clean` clears them
with everything else and there is no second store to go stale on its own.
"""
from __future__ import annotations

from pathlib import Path

from elysium_pipeline import character_partition
from elysium_pipeline.tasking import (
    Manifest,
    Task,
    TaskResult,
    fingerprint_content,
    fingerprint_paths,
)

#: Bump to invalidate every character receipt — a change in what a stage MEANS, not in its inputs.
CACHE_REVISION = "elysium-character-stage-v1"

STAGES = ("textures", "bank_skeletons", "banks", "family_skeletons", "meshes", "clips")

GLOBAL_SCOPE = "_global"


def _code_paths(config) -> tuple[Path, ...]:
    """Everything that decides what a character asset comes out as.

    The C++ is hashed whole rather than per function. A change to it already costs a build and a
    link, so a full re-bake after one is not the loop worth optimising -- unlike `bake_map.py`,
    where the per-stage closure trick pays for itself on every edit.
    """
    repo = config.repo_root
    return (
        repo / "pipeline" / "unreal" / "bake_characters.py",
        repo / "pipeline" / "unreal" / "bake_lib.py",
        repo / "pipeline" / "src" / "elysium_pipeline" / "asset_names.py",
        repo / "pipeline" / "src" / "elysium_pipeline" / "character_partition.py",
        repo / "Source" / "ElysiumUE" / "Public" / "ElysiumSkeletalBuild.h",
        repo / "Source" / "ElysiumUE" / "Private" / "Editor" / "ElysiumSkeletalBuild.cpp",
        repo / "Source" / "ElysiumUE" / "Private" / "Visual" / "ElysiumSkeletalSource.h",
        repo / "Source" / "ElysiumUE" / "Private" / "Visual" / "ElysiumSkeletalSource.cpp",
        repo / "Source" / "ElysiumUE" / "Private" / "ElysiumContentPaths.h",
    )


def _container(npc_dir: Path, stem: str, *, bank: bool = False) -> Path:
    return npc_dir / "banks" / f"{stem}.eskm" if bank else npc_dir / f"{stem}.eskm"


def _blend_sidecar(npc_dir: Path, manifest: dict, owner: str, *, bank: bool) -> list[Path]:
    section = manifest["banks"] if bank else manifest["npcs"]
    relative = section.get(owner, {}).get("blends", "")
    return [npc_dir / relative] if relative else []


def stage_inputs(npc_dir: Path, manifest: dict, partition: dict,
                 scope: str, stage: str) -> list[Path]:
    """The files a scope's stage reads. A missing one fingerprints as absent, not as an error."""
    if scope == GLOBAL_SCOPE:
        return [npc_dir / "textures.json"] if stage == "textures" else []

    kind, _, family = scope.partition(".")
    is_bank = kind == "bank"
    members = character_partition.members_for(
        partition, "banks" if is_bank else "models", family)
    containers = [_container(npc_dir, stem, bank=is_bank) for stem in members]

    if stage in ("bank_skeletons", "family_skeletons"):
        return containers
    if stage == "meshes":
        return containers + [npc_dir / "textures.json"]
    if stage in ("banks", "clips"):
        sidecars: list[Path] = []
        owners = members if is_bank else _own_clip_owners(manifest, members)
        for owner in owners:
            sidecars.extend(_blend_sidecar(npc_dir, manifest, owner, bank=is_bank))
        return [_container(npc_dir, owner, bank=is_bank) for owner in owners] + sidecars
    return []


def _own_clip_owners(manifest: dict, members) -> list[str]:
    """The stems whose own containers hold the clips this family's members play.

    A body's dialogue can be owned by another body -- the manifest resolves that -- so the set is
    not simply the members.
    """
    owners: set[str] = set()
    for stem in members:
        record = manifest.get("npcs", {}).get(stem, {})
        owners.add(stem)
        for owner in record.get("clips", {}).values():
            if owner in manifest.get("npcs", {}):
                owners.add(owner)
    return sorted(owners)


def scopes_for(partition: dict, stems) -> list[str]:
    """Every scope a bake of `stems` touches, banks first — they gate the compatibility call."""
    banks = [f"bank.{family}" for family in sorted(partition["banks"])]
    families = sorted({partition["model_family_of"][stem] for stem in stems
                       if stem in partition["model_family_of"]})
    return [GLOBAL_SCOPE, *banks, *[f"model.{family}" for family in families]]


def plan_stages(config, manifest_store: Manifest, npc_dir: Path, manifest: dict,
                partition: dict, stems, *, force: bool = False,
                cache=None) -> dict[str, list[str]]:
    """{scope: stale stages}, with the cascades applied. A scope with none is fully reusable."""
    code = fingerprint_paths(_code_paths(config))
    partition_fingerprint = partition.get("corpus_fingerprint", "")

    def fingerprint_of(scope: str, stage: str) -> str:
        return fingerprint_content(
            stage_inputs(npc_dir, manifest, partition, scope, stage),
            extra=(CACHE_REVISION, scope, stage, code, partition_fingerprint),
            cache=cache,
        )

    stale: dict[str, list[str]] = {}
    for scope in scopes_for(partition, stems):
        kind = scope.split(".")[0]
        wanted = {
            GLOBAL_SCOPE: ("textures",),
            "bank": ("bank_skeletons", "banks"),
            "model": ("family_skeletons", "meshes", "clips"),
        }[kind if kind in ("bank", "model") else GLOBAL_SCOPE]
        dirty = []
        for stage in wanted:
            task = Task(name=f"chars:{scope}:{stage}", action=lambda: None)
            if force or not manifest_store.can_skip(task, fingerprint_of(scope, stage)):
                dirty.append(stage)
        stale[scope] = dirty

    _cascade(stale)
    return stale


def _cascade(stale: dict[str, list[str]]) -> None:
    """Widen the stale set until it is closed under what an asset actually depends on.

    Order matters. A family skeleton goes stale for two unrelated reasons, and only one of them
    reaches the assets built against it:

    - its own members' containers changed, so the bone TREE moved. An untracked bone falls back to
      that tree's reference pose and a blend mask is addressed by its bone set, so every mesh and
      every clip in the family has to follow.
    - a bank skeleton moved, so the compatibility declaration written into the same package has to
      be restated. The tree is untouched and nothing built against it cares.

    Conflating the two would make one changed bank rebuild all 166 meshes and every clip on the
    mount, which is most of what the receipts exist to avoid.
    """
    for scope, stages in stale.items():
        if scope.startswith("bank.") and "bank_skeletons" in stages and "banks" not in stages:
            # The blend-mask profiles live ON the skeleton and are content-addressed by the bones
            # it carries, so a rewritten skeleton renames them out from under every sequence.
            stages.append("banks")
        if scope.startswith("model.") and "family_skeletons" in stages:
            for stage in ("meshes", "clips"):
                if stage not in stages:
                    stages.append(stage)

    # Only now, so restating a declaration cannot be mistaken for the tree having moved.
    if any("bank_skeletons" in stages for scope, stages in stale.items()
           if scope.startswith("bank.")):
        for scope, stages in stale.items():
            if scope.startswith("model.") and "family_skeletons" not in stages:
                stages.append("family_skeletons")

    for stages in stale.values():
        stages.sort(key=STAGES.index)


def record(manifest_store: Manifest, config, npc_dir: Path, manifest: dict, partition: dict,
           stale: dict[str, list[str]], *, cache=None) -> None:
    """Promote receipts for everything the run just built. Called only after it verified."""
    code = fingerprint_paths(_code_paths(config))
    partition_fingerprint = partition.get("corpus_fingerprint", "")
    for scope, stages in stale.items():
        for stage in stages:
            inputs = stage_inputs(npc_dir, manifest, partition, scope, stage)
            manifest_store.record(TaskResult(
                name=f"chars:{scope}:{stage}",
                status="ok",
                duration_seconds=0.0,
                fingerprint=fingerprint_content(
                    inputs,
                    extra=(CACHE_REVISION, scope, stage, code, partition_fingerprint),
                    cache=cache,
                ),
            ))
