"""Posing a split-rotation bone the way retail draws it.

A character GLB writes the flagged bone's rotation channel exactly as the MDL stores it -- a
MODEL-space orientation, the parent's rotation skipped, the position still composed through the
parent -- and Blender's importer poses every channel by ordinary FK, so the body folds backwards
at the waist. `core.rig` says which bones those are; this puts them where retail puts them.

Blender has no per-bone "my parent's rotation does not reach me, but its pose position does".
Inherit Rotation off is the near miss, and it is a miss: it takes the parent's pose position but
turns the bone's own rest offset by the parent's REST rotation, which on `sm_blueblood`'s own
clips leaves the split bone's head 4-11 mm from the parent-attached position retail composes,
and every bone below it carries that offset. So the rotation keys are rebaked instead. Ordinary
parenting is what already makes the position exactly retail's -- a bone's position does not read
its own rotation channel -- and only the rotation changes:

    basis' = offset^-1 . rotation(parent)^-1 . GLTF_TO_BLENDER . offset . basis

with `offset` the bone's rest rotation in its parent's frame and `rotation(parent)` the parent's
armature-space rotation at that key time, walked from the ancestors' own channels rather than
from the depsgraph so an unbound Action can be posed as readily as a bound one.

The rebake reads the ancestors, so it runs only on a clip that owns them. A masked overlay does
not: the 49-bone upper-body mask owns the split bone and excludes the whole chain above it, which
is 308 of the 722 clips in `shared/male/move_and_ranged`. Those keep the raw model-space rotation
the file states, and the Action names them in `elysium_split_unresolved`. Substituting the bind
chain for the ancestors a clip does not carry is the larger error, not the smaller one: `Bip01`
is an ordinary animated bone that a clip turns 63-99 degrees from bind, so an overlay normalised
against it arrives carrying a root turn that is not its own. Composing an overlay needs the host
pose it rides on, and an Action on its own does not carry one.
"""

from __future__ import annotations

import math

import bpy
from mathutils import Matrix, Quaternion

from ..core import rig

#: The importer's Y-up to Z-up conversion, and the whole of what separates a bone's rest matrix
#: from its glTF node transform: an imported bone carries the node's own axes, so a channel's
#: model-space rotation reaches armature space by this and nothing else.
GLTF_TO_BLENDER = Matrix.Rotation(math.pi / 2.0, 4, "X").to_quaternion()

#: Action custom property naming the split bones a clip was left raw for, in bone order.
UNRESOLVED_PROPERTY = "elysium_split_unresolved"


def apply(armature: bpy.types.Object, payload: dict | None, actions) -> int:
    """Rebake every split-rotation bone the payload declares, in `actions`.

    Returns the number of clips changed. A clip that does not animate a split bone, and a
    payload whose bones this armature does not have, are both ordinary and cost nothing. A
    clip that animates a split bone without the chain above it is left raw and marked.
    """
    if armature is None or armature.type != "ARMATURE" or not actions:
        return 0

    skeleton = armature.data.bones
    bones = [
        skeleton[declared.name]
        for declared in rig.split_rotation_bones(payload)
        if declared.name in skeleton and skeleton[declared.name].parent is not None
    ]
    if not bones:
        return 0

    rest = _rest_rotations(armature)
    split = {bone.name for bone in bones}
    wanted = set()
    for bone in bones:
        node = bone
        while node is not None:
            wanted.add(node.name)
            node = node.parent

    posed = 0
    for action in actions:
        changed = False
        unresolved = set()
        for channelbag in _channelbags(action):
            curves = {name: _rotation_curves(channelbag, name) for name in wanted}
            owned = []
            for bone in bones:
                if curves[bone.name] is None:
                    continue
                if _owns_chain(bone, curves):
                    owned.append(bone)
                else:
                    unresolved.add(bone.name)
            # Baked in full before anything is written: a split bone above another one still
            # has to contribute its own model-space rotation to the chain below it.
            baked = [(curves[bone.name], _bake(bone, rest, curves, split)) for bone in owned]
            for found, values in baked:
                _write(found, values)
                changed = True
        _mark(action, sorted(unresolved))
        posed += 1 if changed else 0
    return posed


def _owns_chain(bone: bpy.types.Bone, curves: dict) -> bool:
    """Whether the clip animates every ancestor the rebake has to evaluate."""
    node = bone.parent
    while node is not None:
        if curves.get(node.name) is None:
            return False
        node = node.parent
    return True


def _mark(action: bpy.types.Action, unresolved: list) -> None:
    """Name the bones a clip was left raw for, and say nothing where there are none."""
    if unresolved:
        action[UNRESOLVED_PROPERTY] = unresolved
    elif action.get(UNRESOLVED_PROPERTY) is not None:
        del action[UNRESOLVED_PROPERTY]


def _bake(bone: bpy.types.Bone, rest: dict, curves: dict, split: set) -> list:
    """The bone's rotation keys, remade so its armature-space rotation is its own channel."""
    offset = rest[bone.name]
    inverse = offset.inverted()
    own = curves[bone.name]

    values = []
    previous = None
    for key in own[0].keyframe_points:
        frame = key.co[0]
        parent = _armature_rotation(bone.parent, frame, rest, curves, split)
        value = inverse @ parent.inverted() @ GLTF_TO_BLENDER @ offset @ _basis(own, frame)
        # A component-interpolated curve reads the shorter arc only while the keys agree on
        # sign, and a per-key rebake has no reason to preserve the sign the importer wrote.
        if previous is not None and value.dot(previous) < 0.0:
            value.negate()
        previous = value
        values.append(value)
    return values


def _armature_rotation(
    bone: bpy.types.Bone, frame: float, rest: dict, curves: dict, split: set
) -> Quaternion:
    """A bone's armature-space rotation at one key time, by retail's rules.

    Walked from the rest hierarchy and the channels themselves. Only ever called on a chain
    the clip owns outright, so the rest rotation a channel-less bone falls back to is the
    guard and not the path.
    """
    chain = []
    node = bone
    while node is not None:
        chain.append(node)
        node = node.parent

    rotation = Quaternion()
    for node in reversed(chain):
        local = rest[node.name] @ _basis(curves.get(node.name), frame)
        rotation = GLTF_TO_BLENDER @ local if node.name in split else rotation @ local
    return rotation


def _rest_rotations(armature: bpy.types.Object) -> dict:
    """Each bone's rest rotation in its parent's rest frame, by bone name."""
    rest = {}
    for bone in armature.data.bones:
        own = bone.matrix_local.to_quaternion()
        parent = bone.parent
        rest[bone.name] = (
            own if parent is None else parent.matrix_local.to_quaternion().inverted() @ own
        )
    return rest


def _channelbags(action: bpy.types.Action):
    """Every slot's channels. A bank clip and a body clip each carry exactly one slot."""
    from bpy_extras import anim_utils

    for slot in action.slots:
        channelbag = anim_utils.action_get_channelbag_for_slot(action, slot)
        if channelbag is not None:
            yield channelbag


def _rotation_curves(channelbag, bone_name: str) -> list | None:
    """One bone's four quaternion curves in component order, or None.

    None where a curve is missing and where the four disagree on how many keys they hold: the
    rebake replaces keyed values in place, so it needs the four to be the same key times.
    """
    path = 'pose.bones["%s"].rotation_quaternion' % bone_name
    found = [None, None, None, None]
    for curve in channelbag.fcurves:
        if curve.data_path == path and 0 <= curve.array_index < 4:
            found[curve.array_index] = curve
    if any(curve is None for curve in found):
        return None
    if len({len(curve.keyframe_points) for curve in found}) != 1:
        return None
    return found


def _basis(curves: list | None, frame: float) -> Quaternion:
    """The rotation a bone's channels hold at a time, relative to its rest."""
    if curves is None:
        return Quaternion()
    return Quaternion([curve.evaluate(frame) for curve in curves]).normalized()


def _write(curves: list, values: list) -> None:
    """Replace the keyed values, carrying each key's handles with them."""
    for index, curve in enumerate(curves):
        for key, value in zip(curve.keyframe_points, values):
            delta = value[index] - key.co[1]
            key.co[1] = value[index]
            key.handle_left[1] += delta
            key.handle_right[1] += delta
        curve.update()
