from __future__ import annotations

import unittest

import pytest

from elysium_pipeline import character_partition as cp
from elysium_pipeline import character_recipes
from elysium_pipeline.formats import eskm

# Synthetic bone trees. Game-independent by construction, per pipeline/CLAUDE.md: these are the
# SHAPES the partition reasons about, not any model's actual rig.
#
# The three trees reproduce the failure the declared BANK partition exists to prevent -- the
# model half no longer groups bodies at all, so instability is bank-only. `A_SEED` binds Bone01
# under spine, `Z_FORK` binds it under head, so they conflict. `M_PLAIN` carries no Bone01 at
# all, so it is compatible with BOTH -- and, when these shapes name banks, which family it lands
# in depends entirely on who else is in the input set. The same three trees double as MODEL
# fixtures, where no such instability exists: each stem's entry is a singleton of its own.
A_SEED = {"root": "", "spine": "root", "Bone01": "spine"}
M_PLAIN = {"root": "", "spine": "root"}
Z_FORK = {"root": "", "spine": "root", "head": "spine", "Bone01": "head"}
# A prop shares no bone with the biped, so nothing conflicts -- but merging it would leave the
# reference skeleton with two roots, which Unreal refuses at mesh-build time.
PROP = {"phone": "", "handset": "phone"}

MODELS = {"a_seed": A_SEED, "m_plain": M_PLAIN, "z_fork": Z_FORK}
BANKS = {"bank_one": M_PLAIN, "bank_two": A_SEED}


def test_case_folds():
    # FName is case-insensitive and rig_trees_compatible folds case, so two trees Unreal
    # would call identical must not fingerprint apart.
    assert cp.tree_fingerprint({"Root": "", "Spine": "Root"}) == cp.tree_fingerprint({"root": "", "spine": "root"})


def test_ignores_insertion_order():
    assert cp.tree_fingerprint({"root": "", "spine": "root", "head": "spine"}) == cp.tree_fingerprint({"head": "spine", "root": "", "spine": "root"})


def test_distinguishes_reparenting():
    assert cp.tree_fingerprint(A_SEED) != cp.tree_fingerprint(Z_FORK)


def test_distinguishes_an_added_bone():
    assert cp.tree_fingerprint(M_PLAIN) != cp.tree_fingerprint(A_SEED)


# RigFamilySemanticsTests
# The properties `character_partition` relies on, pinned so a change to them is visible.

def test_agreeing_trees_merge():
    families = eskm.rig_families({"a": A_SEED, "b": A_SEED}, ["a", "b"])
    assert len(families) == 1
    assert families[0]["stems"] == ["a", "b"]


def test_parent_disagreement_splits():
    families = eskm.rig_families({"a_seed": A_SEED, "z_fork": Z_FORK}, ["a_seed", "z_fork"])
    assert [f["name"] for f in families] == ["a_seed", "z_fork"]


def test_a_second_root_splits_even_with_no_shared_bone():
    families = eskm.rig_families({"a_seed": A_SEED, "prop": PROP}, ["a_seed", "prop"])
    assert len(families) == 2


def test_a_family_is_named_for_its_lowest_sorted_member():
    families = eskm.rig_families({"zz": A_SEED, "aa": A_SEED}, ["zz", "aa"])
    assert families[0]["name"] == "aa"


# SubsetInstabilityTests
# Why the BANK partition is declared rather than recomputed.
#
# These assert the behaviour of the RAW partition function, which is subset-sensitive by
# construction. They are the reason `build_partition` is fed the whole corpus and the answer is
# written down -- not a defect in `rig_families`, which documents this contract itself. The
# model half of the declared partition has no such hazard: every stem is a singleton regardless
# of who else is in the input set.

def test_a_slice_renames_a_family():
    whole = cp.build_partition({}, MODELS)
    assert cp.bank_family(whole, "m_plain") == "a_seed"

    sliced = eskm.rig_families({"m_plain": M_PLAIN, "z_fork": Z_FORK},
                               ["m_plain", "z_fork"])
    assert sliced[0]["name"] == "m_plain"


def test_a_slice_merges_two_families_the_corpus_splits():
    whole = cp.build_partition({}, MODELS)
    assert cp.bank_family(whole, "m_plain") != cp.bank_family(whole, "z_fork")

    sliced = eskm.rig_families({"m_plain": M_PLAIN, "z_fork": Z_FORK},
                               ["m_plain", "z_fork"])
    assert len(sliced) == 1
    assert sliced[0]["stems"] == ["m_plain", "z_fork"]


def test_is_independent_of_input_ordering():
    forward = cp.build_partition(MODELS, BANKS)
    backward = cp.build_partition(dict(reversed(list(MODELS.items()))),
                                  dict(reversed(list(BANKS.items()))), )
    assert forward == backward


def test_names_every_member_exactly_once():
    partition = cp.build_partition(MODELS, BANKS)
    named = [stem for entry in partition["models"].values() for stem in entry["members"]]
    assert sorted(named) == sorted(MODELS)
    assert sorted(partition["model_family_of"]) == sorted(MODELS)
    assert sorted(partition["bank_family_of"]) == sorted(BANKS)


def test_carries_a_skeleton_path_per_family():
    partition = cp.build_partition(MODELS, BANKS)
    assert partition["models"]["a_seed"]["skeleton"] == f"{cp.SKELETON_DIR}/{cp.MODEL_SKELETON_PREFIX}a_seed"
    assert partition["models"]["m_plain"]["skeleton"] == f"{cp.SKELETON_DIR}/{cp.MODEL_SKELETON_PREFIX}m_plain"
    bank_family = cp.bank_family(partition, "bank_one")
    assert partition["banks"][bank_family]["skeleton"] == f"{cp.SKELETON_DIR}/{cp.BANK_SKELETON_PREFIX}{bank_family}"


def test_every_model_gets_its_own_skeleton():
    # m_plain's tree is compatible with a_seed's -- the old family partition would have
    # merged them onto one skeleton -- but every model now names its own regardless.
    partition = cp.build_partition(MODELS, {})
    assert partition["models"]["m_plain"]["skeleton"] == f"{cp.SKELETON_DIR}/{cp.MODEL_SKELETON_PREFIX}m_plain"
    assert partition["models"]["m_plain"]["skeleton"] != partition["models"]["a_seed"]["skeleton"]


def test_every_models_tree_fingerprint_is_its_own():
    # m_plain's tree is compatible with a_seed's, but each model's entry states only its own
    # container's tree -- no union, no absorption.
    partition = cp.build_partition(MODELS, {})
    assert partition["models"]["m_plain"]["tree_fingerprint"] == cp.tree_fingerprint(M_PLAIN)
    assert partition["models"]["m_plain"]["bones"] == len(M_PLAIN)


def test_bank_tree_fingerprint_covers_the_merged_union():
    # a_seed-shaped bank absorbs m_plain-shaped bank, which adds no bone, so the family's
    # tree is a_seed's own.
    partition = cp.build_partition({}, MODELS)
    assert partition["banks"]["a_seed"]["tree_fingerprint"] == cp.tree_fingerprint(A_SEED)
    assert partition["banks"]["a_seed"]["bones"] == len(A_SEED)


def test_models_and_banks_partition_independently():
    partition = cp.build_partition(MODELS, BANKS)
    assert "bank_one" not in partition["model_family_of"]
    assert "a_seed" not in partition["bank_family_of"]


def test_rejects_a_stale_version():
    partition = cp.build_partition(MODELS, {})
    partition["version"] = cp.VERSION + 1
    with pytest.raises(ValueError):
        cp.check(partition)


def test_rejects_a_foreign_document():
    with pytest.raises(ValueError):
        cp.check({"schema": "something.else", "version": cp.VERSION})


def test_rejects_a_missing_table():
    partition = cp.build_partition(MODELS, {})
    del partition["bank_family_of"]
    with pytest.raises(ValueError):
        cp.check(partition)


MANIFEST = {
    "npcs": {
        # Both shapes, because both are written: a label declared by two banks and one
        # declared by a single bank.
        "a_seed": {"clips": {"walk": ["bank_one", "bank_two"], "idle": "bank_one"}},
        "m_plain": {"clips": {"idle": ["bank_two"]}},
    }
}


# FocusedScopeTests
# `scopes_for` reading the cast manifest's clip table.
#
# A clip label names every bank that DECLARES it, in include-tree order, so the manifest maps a
# label to a LIST of owners. A focused bake resolves the bank families its named bodies reach by
# reading that table, and read as if each value were one owner it puts a list into a set --
# which raised before the run had written anything, so no bake could be scoped to a body at all.

def test_a_multi_owner_clip_label_resolves_its_banks():
    partition = cp.build_partition(MODELS, BANKS)
    got = character_recipes.scopes_for(partition, ["a_seed"], manifest=MANIFEST)
    families = {cp.bank_family(partition, owner) for owner in ("bank_one", "bank_two")}
    assert sorted(got) == sorted([character_recipes.GLOBAL_SCOPE, "model.a_seed",
                *(f"bank.{family}" for family in families)])


def test_a_single_owner_string_still_resolves():
    partition = cp.build_partition(MODELS, BANKS)
    got = character_recipes.scopes_for(partition, ["m_plain"], manifest=MANIFEST)
    assert f"bank.{cp.bank_family(partition, 'bank_two')}" in got
    assert "model.m_plain" in got
