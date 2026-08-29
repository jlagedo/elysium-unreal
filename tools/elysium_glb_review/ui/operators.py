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
from ..adapters import hooks, materials
from ..core import report

CLIP_MODES = (
    ("ALL", "All clips", "Import every animation the file declares"),
    ("NONE", "No clips", "Import geometry and skeleton only"),
)


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
                self._import_one(context, path)
        finally:
            window.cursor_set("DEFAULT")
        return {"FINISHED"}

    def _import_one(self, context: bpy.types.Context, path: Path) -> None:
        hooks.ImportState.reset()
        hooks.ImportState.wanted_clips = set() if self.clips == "NONE" else None

        before = set(bpy.data.materials)
        result = bpy.ops.import_scene.gltf(filepath=str(path))
        if "FINISHED" not in result:
            self.report({"ERROR"}, "glTF import failed for %s" % path.name)
            return

        if not self.build_materials:
            return
        root = prefs.corpus_root(context, near=path)
        if root is None:
            self.report(
                {"WARNING"},
                "No corpus root: materials left as placeholders. Set one in the add-on "
                "preferences.",
            )
            return

        created = [material for material in bpy.data.materials if material not in before]
        summary = materials.rebuild_all(created, root)
        if summary.approximated:
            self.report(
                {"INFO"},
                "%d material(s) rebuilt, %d carry approximations"
                % (summary.rebuilt, summary.approximated),
            )


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
