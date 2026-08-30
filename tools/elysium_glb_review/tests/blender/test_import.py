"""Acceptance run inside Blender.

Run headless:

    blender --background --python-exit-code 1 --python tests/blender/test_import.py -- \
        --module <addon module> --corpus <exports_v2 root>

Every check is an assertion because the glTF importer swallows exceptions raised inside
a user extension and logs them. Without assertions a broken add-on produces a silent
no-op that looks exactly like a clean run.
"""

from __future__ import annotations

import argparse
import importlib
import math
import os
import struct
import sys
import time
from pathlib import Path

import bpy
from mathutils import Matrix, Quaternion

#: The importer's Y-up to Z-up conversion, spelled out here rather than imported so the check
#: below holds the add-on to the file and not to its own constant.
Y_UP_TO_Z_UP = Matrix.Rotation(math.pi / 2.0, 4, "X")


def parse_args() -> argparse.Namespace:
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--module", required=True, help="Enabled add-on module name")
    parser.add_argument("--corpus", default=os.environ.get("ELYSIUM_EXPORT_V2_ROOT", ""))
    return parser.parse_args(argv)


def enable(module: str) -> None:
    bpy.ops.preferences.addon_enable(module=module)
    assert module in bpy.context.preferences.addons.keys(), (
        "%s is not in preferences.addons; the glTF importer discovers extensions by "
        "scanning that collection, so it will never see the hook" % module
    )
    loaded = sys.modules[module]
    assert hasattr(loaded, "glTF2ImportUserExtension"), (
        "%s exposes no glTF2ImportUserExtension at module level" % module
    )
    core_seams = importlib.import_module(module + ".core.seams")
    declared = {e.name for e in loaded.glTF2ImportUserExtension().extensions if e.required}
    assert core_seams.MODEL_EXTENSION in declared, (
        "extensions must be declared required=True; required=False whitelists nothing"
    )
    print("enabled %s, declaring %d required extension(s)" % (module, len(declared)))


def clear() -> None:
    for collection in (bpy.data.objects, bpy.data.materials, bpy.data.images,
                       bpy.data.actions, bpy.data.meshes, bpy.data.armatures):
        for item in list(collection):
            collection.remove(item)


def check_gate(corpus: Path) -> None:
    """Every unit lists its extensions as required, so the gate is not optional."""
    path = corpus / "models" / "character" / "gibs" / "head.glb"
    clear()
    result = bpy.ops.import_scene.gltf(filepath=str(path))
    assert "FINISHED" in result, "import of %s did not finish: %r" % (path.name, result)

    armatures = list(bpy.data.armatures)
    assert armatures, "no armature was built"
    bones = [bone.name for bone in armatures[0].bones]
    assert "Bip01" in bones, "expected MDL bone names, got %r" % bones[:4]

    meshes = [m for m in bpy.data.meshes if m.name.startswith("vtmb:")]
    assert meshes, "no body mesh was built"
    assert meshes[0].polygons, "body mesh has no geometry"
    print("gate ok: %d bone(s), %d polygon(s)" % (len(bones), len(meshes[0].polygons)))


def check_materials_and_textures(corpus: Path) -> None:
    """A body's appearance lives entirely outside its own unit."""
    path = corpus / "models" / "character" / "npc" / "common" / "blood_doll" / "blood_doll.glb"
    clear()
    assert "FINISHED" in bpy.ops.import_scene.gltf(filepath=str(path))

    referenced = [m for m in bpy.data.materials if "elysium_material_reference" in m]
    assert len(referenced) >= 8, "expected the body's material identities, got %d" % len(
        referenced
    )

    adapters = importlib.import_module(ARGS.module + ".adapters.materials")
    summary = adapters.rebuild_all(referenced, corpus)
    assert summary.rebuilt, "no material was rebuilt"
    assert not summary.unresolved, "unresolved material(s): %r" % summary.missing

    core_glb = importlib.import_module(ARGS.module + ".core.glb")
    core_ids = importlib.import_module(ARGS.module + ".core.ids")
    core_seams = importlib.import_module(ARGS.module + ".core.seams")

    decoded = [i for i in bpy.data.images if "elysium_texture" in i]
    assert decoded, "no texture was decoded"
    for image in decoded:
        identity = image["elysium_texture"]
        unit = core_glb.read_json(core_ids.resolve(identity, corpus))
        declared = core_seams.root_extension(unit, core_seams.TEXTURE_EXTENSION)["dimensions"]
        # Reading `size` is what forces the packed buffer to decode. `has_data` only
        # reports whether a buffer is loaded right now, so it is False on a perfectly
        # good packed image until something touches it.
        assert tuple(image.size) == (declared["width"], declared["height"]), (
            "%s decoded to %s but the unit declares %sx%s"
            % (identity, tuple(image.size), declared["width"], declared["height"])
        )
        assert image.packed_file, "image %s was not embedded" % image.name
    print(
        "materials ok: %d rebuilt, %d approximate, %d sentinel, %d texture(s) decoded"
        % (summary.rebuilt, summary.approximated, summary.sentinels, len(decoded))
    )


def check_clip_filter(corpus: Path) -> None:
    """A bank declares hundreds of clips; the filter is what makes it openable."""
    hooks = importlib.import_module(ARGS.module + ".adapters.hooks")
    path = corpus / "models" / "character" / "monster" / "andrei" / "andrei.glb"

    clear()
    hooks.ImportState.reset()
    hooks.ImportState.wanted_clips = set()
    started = time.perf_counter()
    assert "FINISHED" in bpy.ops.import_scene.gltf(filepath=str(path))
    filtered_seconds = time.perf_counter() - started
    filtered = len(bpy.data.actions)
    assert filtered == 0, "clip filter kept %d action(s) when asked for none" % filtered

    clear()
    hooks.ImportState.reset()
    started = time.perf_counter()
    assert "FINISHED" in bpy.ops.import_scene.gltf(filepath=str(path))
    unfiltered_seconds = time.perf_counter() - started
    assert len(bpy.data.actions) > 50, "expected the body's own clips"
    print(
        "clip filter ok: %d action(s) in %.1fs filtered, %d in %.1fs unfiltered"
        % (filtered, filtered_seconds, len(bpy.data.actions), unfiltered_seconds)
    )


def check_data_only_seams(corpus: Path) -> None:
    """Materials, textures and surface properties carry no scene at all."""
    core_glb = importlib.import_module(ARGS.module + ".core.glb")
    core_seams = importlib.import_module(ARGS.module + ".core.seams")

    for relative, extension in (
        ("materials/brick/aspdra.glb", core_seams.MATERIAL_EXTENSION),
        ("textures/brick/aspdra.glb", core_seams.TEXTURE_EXTENSION),
        ("surface-properties/brick.glb", core_seams.SURFACE_PROPERTY_EXTENSION),
    ):
        document = core_glb.read_json(corpus / Path(relative))
        found = core_seams.extension_of(document)
        assert found is not None, "%s carries no seam extension" % relative
        assert found[0] == extension, "%s carries %s" % (relative, found[0])
        assert not document.get("meshes"), "%s unexpectedly carries geometry" % relative
    print("data-only seams ok")


def check_bank_loading(corpus: Path) -> None:
    """A body's clips live in models it only names, several files away."""
    animation = importlib.import_module(ARGS.module + ".adapters.animation")
    core_seams = importlib.import_module(ARGS.module + ".core.seams")

    clear()
    path = corpus / "models" / "character" / "npc" / "common" / "blood_doll" / "blood_doll.glb"
    assert "FINISHED" in bpy.ops.import_scene.gltf(filepath=str(path))
    body = next(o for o in bpy.data.objects if o.type == "ARMATURE")
    mesh = next(o for o in bpy.data.objects if o.name.startswith("vtmb:"))

    # A reviewer selects the visible mesh, not the armature.
    assert animation.armature_for(mesh) is body, "the mesh must resolve to its armature"

    payload = animation.body_payload(mesh)
    assert payload is not None, "the body payload was not stashed during import"
    closure = animation.closure_of(
        {"extensions": {core_seams.MODEL_EXTENSION: payload}}, corpus
    )
    assert len(closure.nodes) > 20, "expected a transitive closure, got %d" % len(closure.nodes)
    assert closure.clip_count > 1000, "expected four figures of clips"
    stubs = [node for node in closure.nodes if node.is_stub]
    assert stubs, "expected at least one include stub carrying no clips"

    bank = max(closure.with_clips(), key=lambda node: node.clip_count)
    names = animation.clip_names(bank.identity, corpus)
    assert len(names) == bank.clip_count, "clip listing disagrees with the closure"

    before = len(bpy.data.actions)
    objects_before = {o.name for o in bpy.data.objects}
    result = animation.load_clips(body, bank.identity, corpus, {names[0]})
    assert result.loaded == 1, "filter kept %d clip(s), wanted 1: %s" % (
        result.loaded, result.message
    )
    assert len(bpy.data.actions) == before + 1

    # The bank is a body in its own right; its proxy mesh and armature must not survive.
    assert {o.name for o in bpy.data.objects} == objects_before, "bank scaffolding was left behind"

    bound = body.animation_data
    assert bound is not None and bound.action is result.actions[0]
    assert bound.action_slot is not None, (
        "assigning an action to a second armature does not bind a slot, and an unbound "
        "action animates nothing while reporting no error"
    )
    print(
        "banks ok: %d file(s), %d clip(s), loaded 1 from %s"
        % (len(closure.nodes), closure.clip_count, bank.identity.split(":")[-1])
    )


def _floats(document: dict, binary: bytes, index: int, components: int) -> list:
    """One float accessor of a GLB, as tuples, without the importer's help."""
    accessor = document["accessors"][index]
    view = document["bufferViews"][accessor["bufferView"]]
    start = view.get("byteOffset", 0)
    data = binary[start : start + view["byteLength"]]
    stride = view.get("byteStride") or 4 * components
    offset = accessor.get("byteOffset", 0)
    return [
        struct.unpack_from("<" + "f" * components, data, offset + i * stride)
        for i in range(accessor["count"])
    ]


def _rotation_channel(document: dict, binary: bytes, clip: str, bone: str) -> list:
    """`(time, xyzw)` of one bone's rotation channel in one clip, as the file wrote it."""
    node = next(
        (i for i, n in enumerate(document.get("nodes") or []) if n.get("name") == bone), None
    )
    declared = next((a for a in document.get("animations") or [] if a["name"] == clip), None)
    if node is None or declared is None:
        return []
    for channel in declared["channels"]:
        target = channel["target"]
        if target.get("node") == node and target.get("path") == "rotation":
            sampler = declared["samplers"][channel["sampler"]]
            times = _floats(document, binary, sampler["input"], 1)
            values = _floats(document, binary, sampler["output"], 4)
            return [(t[0], q) for t, q in zip(times, values)]
    return []


def _channel_error(armature, action, document: dict, binary: bytes, clip: str, bone: str, expected):
    """Widest angle between the posed bone and what `expected` makes of the file's channel.

    Sampled at key times, which is where the rebake is exact; between them the curve
    interpolates, as every other bone's does.
    """
    keys = _rotation_channel(document, binary, clip, bone)
    assert keys, "%s carries no rotation channel for %s" % (clip, bone)

    animation_data = armature.animation_data or armature.animation_data_create()
    animation_data.action = action
    if animation_data.action_slot is None:
        animation_data.action_slot = animation_data.action_suitable_slots[0]

    # A pose property no Action drives keeps whatever the last one wrote, so a masked
    # overlay would otherwise be measured against the previous clip's host pose.
    for pose_bone in armature.pose.bones:
        pose_bone.location = (0.0, 0.0, 0.0)
        pose_bone.rotation_quaternion = (1.0, 0.0, 0.0, 0.0)
        pose_bone.scale = (1.0, 1.0, 1.0)

    fps = bpy.context.scene.render.fps
    worst = 0.0
    for time_, (x, y, z, w) in keys[:: max(1, len(keys) // 8)]:
        frame = time_ * fps
        bpy.context.scene.frame_set(int(frame), subframe=frame - int(frame))
        evaluated = armature.evaluated_get(bpy.context.evaluated_depsgraph_get())
        posed = evaluated.pose.bones[bone].matrix.to_quaternion()
        angle = expected(Quaternion((w, x, y, z))).rotation_difference(posed).angle
        # Two quaternions of opposite sign are the same rotation, and one of them reports
        # the turn the long way round.
        worst = max(worst, min(angle, 2.0 * math.pi - angle))
    return worst


def _model_space(stated: Quaternion) -> Quaternion:
    """Where retail puts a split bone: the channel IS the armature-space rotation."""
    return (Y_UP_TO_Z_UP @ stated.to_matrix().to_4x4()).to_quaternion()


def _raw_through(parent_rest: Quaternion):
    """Where an untouched channel lands: ordinary FK with the chain above it at rest."""
    return lambda stated: parent_rest @ stated


def check_split_rotation(corpus: Path) -> None:
    """The flagged bone's channel is already a MODEL-space rotation (A.4a).

    Ordinary FK on it folds the body backwards at the waist, so the add-on rebakes it. Held
    to the file's own statement: at a key time the bone's armature-space rotation IS that
    quaternion. A bank clip has to reach the same place, and it arrives on a body it was
    never imported with.

    A masked overlay is the case that does not: it owns the split bone and none of the chain
    above it, the rebake has nothing to normalise against, and the clip keeps the raw channel
    and says so on the Action.
    """
    core_glb = importlib.import_module(ARGS.module + ".core.glb")
    core_ids = importlib.import_module(ARGS.module + ".core.ids")
    core_rig = importlib.import_module(ARGS.module + ".core.rig")
    core_seams = importlib.import_module(ARGS.module + ".core.seams")
    animation = importlib.import_module(ARGS.module + ".adapters.animation")

    path = (corpus / "models" / "character" / "npc" / "unique" / "santa_monica"
            / "sm_blueblood" / "sm_blueblood.glb")
    document, binary = core_glb.read(path)
    payload = core_seams.root_extension(document, core_seams.MODEL_EXTENSION)
    declared = core_rig.split_rotation_bones(payload)
    assert declared, "sm_blueblood names no split-rotation bone"
    bone = declared[0].name

    clear()
    assert "FINISHED" in bpy.ops.import_scene.gltf(filepath=str(path))
    body = next(o for o in bpy.data.objects if o.type == "ARMATURE")
    clip = next(a["name"] for a in document["animations"] if a["name"].endswith("Line21_col_E"))
    own = _channel_error(
        body, bpy.data.actions[clip], document, binary, clip, bone, _model_space
    )
    assert own < 1e-3, "%s is %.4f rad from its own channel in %s" % (bone, own, clip)

    closure = animation.closure_of(
        {"extensions": {core_seams.MODEL_EXTENSION: payload}}, corpus
    )
    bank = max(closure.with_clips(), key=lambda node: node.clip_count)
    bank_document, bank_binary = core_glb.read(core_ids.resolve(bank.identity, corpus))

    # A clip owns the chain when it animates every ancestor the rebake has to evaluate; the
    # 49-bone upper-body mask owns the split bone and not one of them.
    index = {n.get("name"): i for i, n in enumerate(bank_document["nodes"])}
    ancestors = set()
    node = body.data.bones[bone].parent
    while node is not None:
        ancestors.add(index[node.name])
        node = node.parent

    owned = masked = None
    for declared in bank_document["animations"]:
        rotated = {
            c["target"]["node"] for c in declared["channels"] if c["target"]["path"] == "rotation"
        }
        if index[bone] not in rotated:
            continue
        if owned is None and ancestors <= rotated:
            owned = declared["name"]
        if masked is None and not (ancestors & rotated):
            masked = declared["name"]
    assert owned, "%s has no clip that owns the chain above %s" % (bank.identity, bone)
    assert masked, "%s has no masked overlay of %s" % (bank.identity, bone)

    result = animation.load_clips(body, bank.identity, corpus, {owned})
    assert result.loaded == 1, result.message
    loaded = _channel_error(
        body, result.actions[0], bank_document, bank_binary, owned, bone, _model_space
    )
    assert loaded < 1e-3, "%s is %.4f rad from %s's channel" % (bone, loaded, owned)
    assert result.actions[0].get("elysium_split_unresolved") is None, (
        "%s owns the chain above %s and was rebaked" % (owned, bone)
    )

    result = animation.load_clips(body, bank.identity, corpus, {masked})
    assert result.loaded == 1, result.message
    left = list(result.actions[0].get("elysium_split_unresolved") or [])
    assert left == [bone], "%s should name %s as left raw, names %r" % (masked, bone, left)
    untouched = _channel_error(
        body, result.actions[0], bank_document, bank_binary, masked, bone,
        _raw_through(body.data.bones[bone].parent.matrix_local.to_quaternion()),
    )
    assert untouched < 1e-5, "%s's keys were rewritten: %.4e rad" % (masked, untouched)

    print(
        "split rotation ok: %s within %.2e rad of its own clip and %.2e rad of %s; %s left raw"
        " within %.2e rad and marked" % (bone, own, loaded, owned, masked, untouched)
    )


def check_previews(corpus: Path) -> None:
    """Materials and textures carry no scene, so opening one means building something."""
    operators = importlib.import_module(ARGS.module + ".ui.operators")

    clear()
    ok, message = operators.preview_texture(bpy.context, corpus, "vtmb:texture:brick/aspdra")
    assert ok, message
    assert any("elysium_texture" in image for image in bpy.data.images), "no image was made"

    ok, message = operators.preview_material(
        bpy.context, corpus, "vtmb:material:glass/breaksurf/break_glass_1"
    )
    assert ok, message
    surfaces = [o for o in bpy.data.objects if o.type == "MESH" and o.data.materials]
    assert surfaces, "a material preview needs a surface to be seen on"
    print("previews ok: %s" % message)


def main() -> None:
    global ARGS
    ARGS = parse_args()
    corpus = Path(ARGS.corpus)
    assert corpus.is_dir(), "corpus root %s does not exist" % corpus

    enable(ARGS.module)
    check_gate(corpus)
    check_data_only_seams(corpus)
    check_materials_and_textures(corpus)
    check_clip_filter(corpus)
    check_bank_loading(corpus)
    check_split_rotation(corpus)
    check_previews(corpus)
    print("ALL CHECKS PASSED")


if __name__ == "__main__":
    main()
