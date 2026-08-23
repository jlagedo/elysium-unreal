"""The declared character rig layout: every model its own `USkeleton`, banks partitioned.

A model does not join a rig family. Each body gets a skeleton seeded from its own container
alone (`SKEL_Elysium_<stem>`), the shape an animated prop and a wielded weapon already have, so
its entry here is a singleton: the stem, its own tree fingerprint, its own bone count. Nothing
about one model's layout depends on any other model.

The BANKS are still partitioned, by `formats/eskm.rig_families` -- a shared bank is baked once
on a skeleton several banks can agree on, and that grouping is what keeps 7,871 clips from
multiplying across the cast. `rig_families` answers about **the set it is given**: it is
greedy, a family's tree is the growing union of its members, and a family is named for its
lowest-sorted member, so partitioning a slice can merge two banks the whole corpus keeps apart
while `SKEL_ElysiumBank_<family>` and `Anims/_banks/<owner>` are addressed by the result. The
bank partition is therefore computed **once, over the whole corpus**, and written down; the
bake reads the file and never partitions again.

The file is game-derived like every other export product and is regenerated, never tracked.
"""
from __future__ import annotations

import hashlib

from elysium_pipeline.formats import eskm

SCHEMA = "elysium.character-families"
VERSION = 2
#: Names the shape of an entry, for a reader of the file; nothing hashes it.
REVISION = "elysium-character-families-v2"

SKELETON_DIR = "/ElysiumBaked/Characters/Skeletons"
MODEL_SKELETON_PREFIX = "SKEL_Elysium_"
BANK_SKELETON_PREFIX = "SKEL_ElysiumBank_"


def tree_fingerprint(tree):
    """A stable digest of a bone tree's *shape*, case-folded.

    Case-folded because `rig_trees_compatible` compares that way and `FName` is case-insensitive:
    two trees Unreal would call identical must not fingerprint apart. Sorted because a tree is a
    set of (bone, parent) facts, not an ordered one -- the union that `rig_families` accumulates
    has no meaningful order.
    """
    rows = sorted((bone.lower(), (parent or "").lower()) for bone, parent in tree.items())
    digest = hashlib.sha256()
    for bone, parent in rows:
        digest.update(bone.encode("utf-8"))
        digest.update(b"\0")
        digest.update(parent.encode("utf-8"))
        digest.update(b"\0")
    return digest.hexdigest()


def _entries(trees, stems, prefix):
    families = eskm.rig_families(trees, stems)
    out, owner = {}, {}
    for family in families:
        name = family["name"]
        out[name] = {
            "members": sorted(family["stems"]),
            "skeleton": f"{SKELETON_DIR}/{prefix}{name}",
            "tree_fingerprint": tree_fingerprint(family["tree"]),
            # Counted case-folded: Unreal bone names are case-insensitive, so the engine's
            # skeleton merge folds `Bip01 L Forearm` and `Bip01 L ForeArm` (both shipped)
            # into one bone, and the declared count must match what that merge builds.
            "bones": len({bone.lower() for bone in family["tree"]}),
        }
        for stem in family["stems"]:
            owner[stem] = name
    return out, owner


def _singletons(trees, prefix):
    """One entry per stem: its own skeleton, its own tree, no grouping."""
    out = {stem: {
        "members": [stem],
        "skeleton": f"{SKELETON_DIR}/{prefix}{stem}",
        "tree_fingerprint": tree_fingerprint(tree),
        "bones": len({bone.lower() for bone in tree}),
    } for stem, tree in trees.items()}
    return out, {stem: stem for stem in trees}


def build_partition(model_trees, bank_trees, *, corpus_fingerprint=""):
    """The declared layout, as the body of `npc/families.json`.

    `model_trees` / `bank_trees` are {stem: {bone: parent}} over the WHOLE corpus. Only the
    banks are grouped, but both halves are whole-corpus: the bank partition because a subset
    repartitions it, the models because the file is the one statement of which bodies are
    declared at all.
    """
    models, model_family_of = _singletons(model_trees, MODEL_SKELETON_PREFIX)
    banks, bank_family_of = _entries(bank_trees, list(bank_trees), BANK_SKELETON_PREFIX)
    return {
        "schema": SCHEMA,
        "version": VERSION,
        "revision": REVISION,
        "corpus_fingerprint": corpus_fingerprint,
        "models": models,
        "banks": banks,
        "model_family_of": model_family_of,
        "bank_family_of": bank_family_of,
    }


def check(partition):
    """Raise ValueError unless `partition` is a partition this build can read."""
    if not isinstance(partition, dict):
        raise ValueError("families.json is not an object")
    if partition.get("schema") != SCHEMA:
        raise ValueError(f"families.json is {partition.get('schema')!r}, expected {SCHEMA!r}")
    if partition.get("version") != VERSION:
        raise ValueError(
            f"families.json is version {partition.get('version')}, expected {VERSION}"
            " - re-run: uv run elysium export characters"
        )
    for key in ("models", "banks", "model_family_of", "bank_family_of"):
        if not isinstance(partition.get(key), dict):
            raise ValueError(f"families.json is missing '{key}'")
    return partition


def model_family(partition, stem):
    """The stem itself when the partition declares the model, else "". Models are singletons,
    so this is a declaration test; the name survives for its callers in the tests."""
    return partition.get("model_family_of", {}).get(stem, "")


def bank_family(partition, bank):
    """The family a bank belongs to, or "" when the partition does not name it."""
    return partition.get("bank_family_of", {}).get(bank, "")


def members_for(partition, kind, family):
    """Every member of a declared family, in declared order. Empty for an unknown family."""
    return list(partition.get(kind, {}).get(family, {}).get("members", []))
