"""Operators.

Import goes through Blender's own glTF importer rather than a parser of our own. The
importer is where the extension gate lives, where the animation filter is consulted, and
where the skinned mesh and armature get built correctly; reimplementing it would mean
owning all three.
"""

from __future__ import annotations

import json
import os
from pathlib import Path

import bpy
from bpy.props import BoolProperty, CollectionProperty, EnumProperty, StringProperty
from bpy_extras.io_utils import ImportHelper

from .. import prefs
from ..adapters import hooks, images, materials
from ..core import report

CLIP_MODES = (
    ("NONE", "No clips", "Import geometry and skeleton only"),
    ("ALL", "All clips", "Import every animation the file declares"),
)


def import_unit(
    context: bpy.types.Context,
    path: Path,
    *,
    clips: str = "NONE",
    build_materials: bool = True,
) -> tuple[bool, str]:
    """Import one unit and rebuild what it references.

    Returns `(ok, message)` so both the file dialog and the browser can report the same
    thing in their own way.
    """
    hooks.ImportState.reset()
    hooks.ImportState.wanted_clips = set() if clips == "NONE" else None

    before = set(bpy.data.materials)
    if "FINISHED" not in bpy.ops.import_scene.gltf(filepath=str(path)):
        return False, "glTF import failed for %s" % path.name

    if not build_materials:
        return True, "imported %s" % path.name

    root = prefs.corpus_root(context, near=path)
    if root is None:
        return True, (
            "imported %s; no corpus root, so materials are placeholders" % path.name
        )

    created = [material for material in bpy.data.materials if material not in before]
    summary = materials.rebuild_all(created, root)
    message = "imported %s: %d material(s), %d approximate" % (
        path.name,
        summary.rebuilt,
        summary.approximated,
    )
    if summary.unresolved:
        message += ", %d unresolved" % summary.unresolved
    return True, message


class ELYSIUM_OT_import_glb(bpy.types.Operator, ImportHelper):
    """Import an Elysium export_v2 unit and rebuild what it references."""

    bl_idname = "elysium.import_glb"
    bl_label = "Import Elysium GLB"
    bl_options = {"REGISTER", "UNDO"}

    filename_ext = ".glb"
    filter_glob: StringProperty(default="*.glb", options={"HIDDEN"})
    directory: StringProperty(subtype="DIR_PATH", options={"HIDDEN", "SKIP_SAVE"})
    files: CollectionProperty(
        type=bpy.types.OperatorFileListElement, options={"HIDDEN", "SKIP_SAVE"}
    )

    clips: EnumProperty(
        name="Animations",
        items=CLIP_MODES,
        default="NONE",
        description=(
            "A shared bank declares up to 722 clips and a body's full closure runs to "
            "four figures, so nothing is imported unless asked for"
        ),
    )
    build_materials: BoolProperty(
        name="Rebuild Materials",
        default=True,
        description=(
            "Resolve each material identity against the corpus, decode its textures "
            "and build a shader that labels every approximation it makes"
        ),
    )

    def draw(self, context: bpy.types.Context) -> None:
        layout = self.layout
        layout.use_property_split = True
        layout.prop(self, "clips")
        layout.prop(self, "build_materials")

    def execute(self, context: bpy.types.Context):
        paths = [Path(self.directory) / entry.name for entry in self.files] or [
            Path(self.filepath)
        ]

        window = context.window
        window.cursor_set("WAIT")
        try:
            for path in paths:
                ok, message = import_unit(
                    context, path, clips=self.clips, build_materials=self.build_materials
                )
                self.report({"INFO"} if ok else {"ERROR"}, message)
        finally:
            window.cursor_set("DEFAULT")
        return {"FINISHED"}


class ELYSIUM_OT_corpus_report(bpy.types.Operator):
    """Sweep the whole corpus and write an integrity report."""

    bl_idname = "elysium.corpus_report"
    bl_label = "Corpus Integrity Report"
    bl_options = {"REGISTER"}

    write_json: BoolProperty(
        name="Write JSON",
        default=True,
        description="Write the full report beside the corpus as well as summarising it",
    )

    @classmethod
    def poll(cls, context: bpy.types.Context) -> bool:
        return prefs.corpus_root(context) is not None

    def execute(self, context: bpy.types.Context):
        root = prefs.corpus_root(context)
        if root is None:
            self.report({"ERROR"}, "No corpus root configured")
            return {"CANCELLED"}

        window = context.window
        window.cursor_set("WAIT")
        try:
            result = report.scan(root)
        finally:
            window.cursor_set("DEFAULT")

        print(report.summary(result))
        if self.write_json:
            destination = root / "elysium_glb_review_report.json"
            destination.write_text(
                json.dumps(report.to_dict(result), indent=2), encoding="utf-8"
            )
            self.report({"INFO"}, "Report written to %s" % destination)

        level = "WARNING" if result.findings else "INFO"
        self.report(
            {level},
            "%d unit(s), %d finding(s) in %.0fs"
            % (sum(result.files.values()), len(result.findings), result.seconds),
        )
        return {"FINISHED"}


def preview_texture(context: bpy.types.Context, root: Path, identity: str) -> tuple[bool, str]:
    """Decode a texture unit into an image, and show it if an editor is open.

    A texture unit has no scene to import, so the reviewable product is the image
    itself.
    """
    cache = images.TextureCache(root)
    decoded = cache.get(identity, non_color=False)
    if decoded is None or decoded.image is None:
        return False, "%s could not be decoded" % identity

    for area in context.screen.areas:
        if area.type == "IMAGE_EDITOR":
            area.spaces.active.image = decoded.image
            break
    return True, "%s: %s" % (identity.split(":", 2)[-1], decoded.note)


def preview_material(context: bpy.types.Context, root: Path, identity: str) -> tuple[bool, str]:
    """Build a material unit onto a plane, so its shader can be looked at.

    A material unit carries one placeholder core material and no geometry. Giving it a
    surface is the only way to see what the reconstruction actually does.
    """
    stem = identity.split(":", 2)[-1]
    material = bpy.data.materials.new(stem)
    material[hooks.MATERIAL_REFERENCE_PROPERTY] = identity

    status = materials.rebuild(material, root, images.TextureCache(root))
    if status == "missing":
        bpy.data.materials.remove(material)
        return False, "%s names no exported unit" % identity

    mesh = bpy.data.meshes.new(stem)
    mesh.from_pydata(
        [(-1, -1, 0), (1, -1, 0), (1, 1, 0), (-1, 1, 0)], [], [(0, 1, 2, 3)]
    )
    layer = mesh.uv_layers.new()
    for position, coordinate in enumerate([(0, 0), (1, 0), (1, 1), (0, 1)]):
        layer.data[position].uv = coordinate
    mesh.update()
    mesh.materials.append(material)

    obj = bpy.data.objects.new(stem, mesh)
    obj[hooks.MARKER_PROPERTY] = True
    context.scene.collection.objects.link(obj)

    unreproduced = json.loads(material.get(materials.APPROXIMATION_PROPERTY, "[]"))
    if status == "sentinel":
        return True, "%s names no VMT; showing the engine error checker" % stem
    return True, "%s: %s, %d parameter(s) not reproduced" % (
        stem,
        material.get(materials.SHADER_PROPERTY, "?"),
        len(unreproduced),
    )


def _menu_import(self, context: bpy.types.Context) -> None:
    self.layout.operator(ELYSIUM_OT_import_glb.bl_idname, text="Elysium GLB (.glb)")


def register_menus() -> None:
    bpy.types.TOPBAR_MT_file_import.append(_menu_import)


def unregister_menus() -> None:
    # Reinstalling reloads the module, so the entry the menu holds belongs to the
    # previous module object and removing this one raises. That must not abort the
    # rest of the teardown.
    try:
        bpy.types.TOPBAR_MT_file_import.remove(_menu_import)
    except ValueError:
        pass


CLASSES = (ELYSIUM_OT_import_glb, ELYSIUM_OT_corpus_report)
