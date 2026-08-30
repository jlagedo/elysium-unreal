"""Loading included-model clips onto an imported body.

A body carries only its own clips and names its banks by identity. Following those names
transitively reaches a median of 35 files and 1,782 clips, so nothing is loaded until a
reviewer picks something; the closure exists to be listed, not imported.

An Action's data paths address bones by name, so a clip built against a bank armature
evaluates on a body whose bones carry the same names. That is usually every bone the clip
touches, but not always: `shared/female/move_and_ranged` animates a `bush hook` bone that
a blood doll does not have, and those channels land nowhere. The load reports which bones
went unmatched rather than leaving a clip that silently half-plays.

What never carries over is the slot binding. Auto-binding matches a slot identifier
against the object's name, so a bank's slots never bind to a body by luck, and an
unbound Action animates nothing while reporting no error at all.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

import bpy

from ..core import banks, ids
from . import hooks, pose


@dataclass
class LoadResult:
    """What one clip load did."""

    loaded: int = 0
    actions: list = field(default_factory=list)
    #: Bones the clips address that the body does not have.
    missing_bones: list = field(default_factory=list)
    message: str = ""


#: Listing a bank's clips needs no Blender, so it lives in core and is reused here.
clip_names = banks.clip_names


def body_bone_names(armature: bpy.types.Object) -> set[str]:
    return {bone.name for bone in armature.data.bones}


def _bind(target: bpy.types.Object, action: bpy.types.Action) -> bool:
    """Assign an Action to an object and bind a slot to it.

    Auto-binding matches a slot identifier against the object's name, so a bank's slots
    never bind to a body by luck. Without an explicit binding the Action is assigned and
    nothing moves.
    """
    animation_data = target.animation_data or target.animation_data_create()
    animation_data.action = action
    if animation_data.action_slot is None:
        suitable = animation_data.action_suitable_slots
        if not suitable:
            return False
        animation_data.action_slot = suitable[0]
    return animation_data.action_slot is not None


def _discard(objects, meshes, armatures) -> None:
    """Remove the scaffolding a bank import brings with its clips."""
    for obj in objects:
        if obj.name in bpy.data.objects:
            bpy.data.objects.remove(obj)
    for mesh in meshes:
        if mesh.users == 0 and mesh.name in bpy.data.meshes:
            bpy.data.meshes.remove(mesh)
    for armature in armatures:
        if armature.users == 0 and armature.name in bpy.data.armatures:
            bpy.data.armatures.remove(armature)


def load_clips(
    body: bpy.types.Object, identity: str, root: Path, wanted: set[str] | None
) -> LoadResult:
    """Import selected clips from a bank and put them on `body`.

    An included model is a body in its own right, so importing it brings a proxy mesh and
    a second armature. Those are discarded; only the Actions are kept.
    """
    result = LoadResult()
    path = ids.resolve(identity, root)
    if path is None or not path.is_file():
        result.message = "%s names no file" % identity
        return result
    if body is None or body.type != "ARMATURE":
        result.message = "select the body armature first"
        return result

    before_actions = set(bpy.data.actions)
    before_objects = set(bpy.data.objects)
    before_meshes = set(bpy.data.meshes)
    before_armatures = set(bpy.data.armatures)

    hooks.ImportState.reset()
    hooks.ImportState.wanted_clips = wanted
    # The bank's own armature is thrown away in a moment, so its split-rotation bones are
    # posed against the body's skeleton instead, below.
    hooks.ImportState.poses_split_rotation = False
    try:
        if "FINISHED" not in bpy.ops.import_scene.gltf(filepath=str(path)):
            result.message = "glTF import failed for %s" % path.name
            return result
    finally:
        hooks.ImportState.wanted_clips = None
        hooks.ImportState.poses_split_rotation = True

    created = [action for action in bpy.data.actions if action not in before_actions]
    _discard(
        [o for o in bpy.data.objects if o not in before_objects],
        [m for m in bpy.data.meshes if m not in before_meshes],
        [a for a in bpy.data.armatures if a not in before_armatures],
    )
    pose.apply(body, body_payload(body), created)

    available = body_bone_names(body)
    missing: set[str] = set()
    for action in created:
        for slot in action.slots:
            for bone in _addressed_bones(action, slot):
                if bone not in available:
                    missing.add(bone)

    for action in created:
        action.use_fake_user = True
        action["elysium_bank"] = identity
    result.actions = created
    result.loaded = len(created)
    result.missing_bones = sorted(missing)

    if created and not _bind(body, created[0]):
        result.message = "loaded %d clip(s) but none could be bound to %s" % (
            len(created),
            body.name,
        )
        return result

    result.message = "loaded %d clip(s) from %s" % (len(created), identity.split(":")[-1])
    if missing:
        result.message += "; %d bone(s) not on this body" % len(missing)
    return result


def _addressed_bones(action: bpy.types.Action, slot) -> set[str]:
    """Bone names an Action's channels address, from their data paths."""
    from bpy_extras import anim_utils

    channelbag = anim_utils.action_get_channelbag_for_slot(action, slot)
    if channelbag is None:
        return set()
    names = set()
    for curve in channelbag.fcurves:
        path = curve.data_path
        if path.startswith('pose.bones["'):
            names.add(path[12 : path.index('"]', 12)])
    return names


def closure_of(document: dict, root: Path) -> banks.Closure:
    """The banks reachable from a loaded body document."""
    return banks.closure(document, root)


def body_payload(obj: bpy.types.Object) -> dict | None:
    """The model payload stashed on an imported body, if it carries one."""
    return hooks.unstash(obj, hooks.MODEL_PROPERTY)


def armature_for(obj: bpy.types.Object) -> bpy.types.Object | None:
    """The armature a selected object belongs to.

    A reviewer selects the visible mesh far more often than the armature, and the mesh is
    also where the body payload is stashed, so both are accepted.
    """
    if obj is None:
        return None
    if obj.type == "ARMATURE":
        return obj
    if obj.parent is not None and obj.parent.type == "ARMATURE":
        return obj.parent
    for modifier in getattr(obj, "modifiers", []):
        if modifier.type == "ARMATURE" and modifier.object is not None:
            return modifier.object
    return None
