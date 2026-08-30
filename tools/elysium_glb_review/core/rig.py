"""What a model unit says about its skeleton that changes how a clip is posed.

One rule does: VtMB flags a single bone per biped -- always `Bip01 Spine1` -- whose animated
rotation channel is already the bone's MODEL-space orientation. Retail's `BuildTransformations`
skips the parent's rotation for it and composes only the position through the parent
(`docs/vtmb/animation_and_movers.md` A.4a). A model GLB writes that channel verbatim, so a
reader that applies ordinary FK to it folds the body backwards at the waist.

A unit states the set outright in `mdl.splitRotationBones`. Units exported before that key
state the same thing in `mdl.bones[i].flags`, bit `0x2`, so both are read here and the unit's
own statement wins.
"""

from __future__ import annotations

from dataclasses import dataclass

#: `StudioBone.flags` bit for the split-rotation storage rule.
SPLIT_ROTATION_FLAG = 0x2

#: The rotation rule this add-on poses differently. Anything else is ordinary FK.
MODEL_SPACE = "model-space"


@dataclass(frozen=True)
class SplitBone:
    """One bone whose animated rotation channel is already a model-space orientation."""

    index: int
    name: str


def split_rotation_bones(payload: dict | None) -> list[SplitBone]:
    """Bones of a model payload that carry the split-rotation rule, in bone order.

    `splitRotationBones` names the rule per bone rather than implying it, so an entry that
    declares some other rotation storage is left to ordinary FK; an entry that declares none
    is the rule this list exists for.
    """
    mdl = ((payload or {}).get("mdl")) or {}
    bones = mdl.get("bones") or []
    declared = mdl.get("splitRotationBones")

    if declared is not None:
        found = []
        for entry in declared:
            index = entry.get("bone")
            if index is None or entry.get("rotation", MODEL_SPACE) != MODEL_SPACE:
                continue
            index = int(index)
            name = entry.get("name") or _name_at(bones, index)
            found.append(SplitBone(index=index, name=name))
        return found

    return [
        SplitBone(index=index, name=bone.get("name") or "")
        for index, bone in enumerate(bones)
        if int(bone.get("flags") or 0) & SPLIT_ROTATION_FLAG
    ]


def _name_at(bones: list, index: int) -> str:
    """The bone table's name for an index, for an entry that gave only the index."""
    if 0 <= index < len(bones):
        return bones[index].get("name") or ""
    return ""
