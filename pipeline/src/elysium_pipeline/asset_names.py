"""Folding an arbitrary VtMB name into a legal Unreal object name.

Two folds exist, and they are **not** the same function:

- :func:`safe_name` is what `pipeline/unreal/bake_lib.py` has always applied to texture,
  material and mesh-slot names. It strips leading and trailing underscores and substitutes a
  fallback for a name that folds away entirely.
- :func:`baked_asset_name` is the exact behaviour of `FElysiumContentPaths::BakedAssetName`,
  which the bake and the runtime both apply to clip and blend-space labels. It folds runs and
  stops there, so a leading or trailing run survives as one underscore.

They disagree on 4 of the 2,494 shipped clip labels (`wolf_Form_attack[Bite]` and its three
siblings, which end in an illegal character). Today the two never meet on the same input --
clip labels go through the C++ fold on both the bake and the verify side, and texture names go
through the Python fold on both -- so the divergence is latent. This module exists so that the
divergence is stated in one place rather than discovered, and so that the offline half can fold
a name without importing `unreal`.

Both live here because a name is a contract between the pipeline and the runtime, and a contract
with two implementations in two languages needs at least one of them to be readable without an
editor.
"""
from __future__ import annotations

import re

_UNSAFE = re.compile(r"[^A-Za-z0-9_]+")


def safe_name(text):
    """An OBJ material / texture path turned into a legal Unreal object name."""
    return _UNSAFE.sub("_", text).strip("_") or "unnamed"


def baked_asset_name(text):
    """`FElysiumContentPaths::BakedAssetName` in Python: one underscore per illegal run.

    Leading and trailing runs are kept, because the C++ keeps them. A caller comparing against
    an asset the C++ named must use this, not :func:`safe_name`.
    """
    return _UNSAFE.sub("_", text)


_RIG_UNSAFE = re.compile(r"[^A-Za-z0-9_\-.| ]")


def rig_bone_name(name):
    """A VtMB bone name every Unreal animation surface can carry, folded character by character.

    Unreal's sequencer-backed animation data model stores each bone track as an FK Control Rig
    element, and `URigHierarchy::SanitizeName` folds any character outside letters, digits,
    `_ - . |` and a non-leading space to `_` (`UAnimSequencerController::AddBoneControl`). A
    `USkeleton` accepts the raw name, so a bone whose name folds never binds to its own track and
    the track is silently dropped on save. Folding once here, at the exporter boundary, keeps
    every product -- skeleton, mesh, clip track, dynamics chain, garment sidecar -- naming the
    bone the one way Unreal can store it. Character per character rather than per run, matching
    the engine's own fold, so two authored names that differ stay different wherever a legal
    character separates them.
    """
    folded = _RIG_UNSAFE.sub("_", name)
    if folded.startswith(" "):
        folded = "_" + folded[1:]
    return folded


def texture_asset_name(uri):
    """The `T_*` package name a character albedo imports under, from its export-relative uri."""
    stem = uri.replace("\\", "/").rsplit("/", 1)[-1]
    if "." in stem:
        stem = stem.rsplit(".", 1)[0]
    return "T_" + safe_name(stem)
