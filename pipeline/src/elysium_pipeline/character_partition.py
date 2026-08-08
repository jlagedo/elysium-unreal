"""The declared rig partition: which models and banks share one `USkeleton`.

`formats/eskm.rig_families` answers the question, but it answers it about **the set it is
given** -- it is greedy, a family's tree is the growing union of its members, and a family is
named for its lowest-sorted member. Partitioning a slice therefore does not merely rename: two
models the whole cast keeps apart can merge, because the family that would have absorbed one of
them is absent from the slice.

That matters because the family name is the path contract. `Anims/<family>/<owner>/A_*` and
`SKEL_Elysium_<family>` are addressed by it, so a slice that repartitions writes a second answer
for a label that already had one, under a skeleton nothing else points at.

So the partition is computed **once, over the whole corpus**, and written down. This module owns
that computation and its serialised form; the bake reads the file and never partitions again.

The file is game-derived like every other export product and is regenerated, never tracked.
"""
from __future__ import annotations

import hashlib

from elysium_pipeline.formats import eskm

SCHEMA = "elysium.character-families"
VERSION = 1
#: Bumping this invalidates every character receipt keyed on a partition entry. Bump it when the
#: shape of an entry changes, not when the corpus does -- the corpus is covered by the trees.
REVISION = "elysium-character-families-v1"

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
            "bones": len(family["tree"]),
        }
        for stem in family["stems"]:
            owner[stem] = name
    return out, owner


def build_partition(model_trees, bank_trees, *, corpus_fingerprint=""):
    """The declared partition, as the body of `npc/families.json`.

    `model_trees` / `bank_trees` are {stem: {bone: parent}} over the WHOLE corpus. Passing a
    subset is the bug this module exists to prevent, so callers hand it everything the manifest
    names and let the bake slice afterwards.
    """
    models, model_family_of = _entries(model_trees, list(model_trees), MODEL_SKELETON_PREFIX)
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
    """The family a model belongs to, or "" when the partition does not name it."""
    return partition.get("model_family_of", {}).get(stem, "")


def bank_family(partition, bank):
    """The family a bank belongs to, or "" when the partition does not name it."""
    return partition.get("bank_family_of", {}).get(bank, "")


def members_for(partition, kind, family):
    """Every member of a declared family, in declared order. Empty for an unknown family."""
    return list(partition.get(kind, {}).get(family, {}).get("members", []))


def bone_trees(paths):
    """{stem: {bone: parent}} read from `.eskm` containers -> {stem: path}.

    Only the `SKEL` section is touched; a container is 0.4-30 MB and its clips are the editor's
    business.
    """
    return {stem: eskm.bone_parents(eskm.read(path)) for stem, path in paths.items()}
