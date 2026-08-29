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
import os
import sys
import time
from pathlib import Path

import bpy


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
    declared = {e.name for e in loaded.glTF2ImportUserExtension().extensions if e.required}
    assert "ELYSIUM_vtmb_character" in declared, (
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
    path = corpus / "characters" / "gibs" / "head.glb"
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
    path = corpus / "characters" / "npc" / "common" / "blood_doll" / "blood_doll.glb"
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
    path = corpus / "characters" / "monster" / "andrei" / "andrei.glb"

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
    """A body's clips live in banks it only names, several files away."""
    animation = importlib.import_module(ARGS.module + ".adapters.animation")

    clear()
    path = corpus / "characters" / "npc" / "common" / "blood_doll" / "blood_doll.glb"
    assert "FINISHED" in bpy.ops.import_scene.gltf(filepath=str(path))
    body = next(o for o in bpy.data.objects if o.type == "ARMATURE")
    mesh = next(o for o in bpy.data.objects if o.name.startswith("vtmb:"))

    # A reviewer selects the visible mesh, not the armature.
    assert animation.armature_for(mesh) is body, "the mesh must resolve to its armature"

    payload = animation.body_payload(mesh)
    assert payload is not None, "the body payload was not stashed during import"
    closure = animation.closure_of({"extensions": {"ELYSIUM_vtmb_character": payload}}, corpus)
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
    check_previews(corpus)
    print("ALL CHECKS PASSED")


if __name__ == "__main__":
    main()
