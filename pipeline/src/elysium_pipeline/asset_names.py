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


#: The suffix a styled face group's key carries, restating
#: `importers.map_geometry.LIGHTSTYLE_SUFFIX`. Restated rather than imported because the editor's
#: embedded Python -- where the bake runs -- has no numpy, and that module needs it. The two are
#: pinned equal by `test_map_geometry.py`.
LIGHTSTYLE_KEY_SUFFIX = "#style"
#: What :func:`safe_name` folds that suffix to: `#` is illegal in an Unreal object name, so the
#: fold turns it into the one underscore this marker names.
FOLDED_LIGHTSTYLE_MARKER = "_style"


def brush_slot_style(slot_names) -> int:
    """The lightstyle a BRUSH-ENTITY mesh animates on, read off its material slot names.

    R7.4 contract 3 reaches a world/sky chunk through the `elysium.style=<n>` tag the bake writes
    on the chunk ACTOR, because the bake places that actor. A brush entity's mesh is never placed:
    the runtime builds one component per body when the entity world embodies it
    (`UElysiumEntityBodies::BuildBrushVisual`), long after the level walk that reads those tags. So
    the fact travels on the ASSET instead -- and it already does, because the bake names each
    section's slot :func:`safe_name` of the face group's key and a styled group's key ends in
    `#style<n>` (`importers.map_geometry.section_key`), which folds to `_style<n>`.

    One primitive carries one CustomPrimitiveData slot, so it carries one style: a mesh whose
    sections disagree, or that mixes styled sections with unstyled ones, animates on none (0)
    rather than dragging unstyled geometry along with it. `ElysiumLightStyle::StyleFromSlotNames`
    (`Source/ElysiumUE/Public/ElysiumFog.h`) is the runtime twin of exactly this function, and
    `bake_map.py` fails the bake if an UNSTYLED group key ever folds to this shape.

    It lives here, beside the fold it inverts, rather than beside the key grammar: the bake and the
    bake's verifier both read it and neither wants `map_geometry`'s numpy dependency to say so.
    """

    slots = list(slot_names)
    if not slots:
        return 0
    agreed = 0
    for slot in slots:
        head, sep, tail = str(slot).rpartition(FOLDED_LIGHTSTYLE_MARKER)
        if not sep or not head or not tail.isdigit():
            return 0
        style = int(tail)
        if style < 1 or (agreed and style != agreed):
            return 0
        agreed = style
    return agreed
