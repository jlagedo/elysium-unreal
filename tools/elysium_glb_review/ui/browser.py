"""A corpus browser.

Importing a unit by hand through the file dialog means already knowing which of 23,414
files to open. This lists them, filters them, and reports what each one contains before
anything is imported.

The list is built from filenames alone, which takes a few seconds for the whole corpus
against the forty a full parse would cost. A unit's contents are read when it is
selected, and cached, so browsing stays responsive and only pays for what is looked at.
"""

from __future__ import annotations

import bpy
from bpy.props import CollectionProperty, EnumProperty, IntProperty, StringProperty

from .. import prefs
from ..core import index
from . import operators

ALL_GROUPS = "__all__"

#: Details keyed by identity. Cleared whenever the index is rebuilt.
_details: dict = {}

#: Enum items must outlive the callback that returns them, or Blender reads freed
#: strings and shows garbage.
_group_items: list = []
_seam_items: list = []


def _entries(context: bpy.types.Context):
    return context.scene.elysium_units


def _active(context: bpy.types.Context):
    entries = _entries(context)
    position = context.scene.elysium_unit_index
    return entries[position] if 0 <= position < len(entries) else None


def _seam_enum(self, context):
    _seam_items.clear()
    _seam_items.extend(
        (name, name.replace("-", " ").title(), "Browse the %s seam" % name)
        for name in index.SEAM_KIND
    )
    return _seam_items


def _group_enum(self, context):
    _group_items.clear()
    _group_items.append((ALL_GROUPS, "All", "Every subtree of this seam"))
    _group_items.extend(
        (name, name, "Units under %s/" % name)
        for name in sorted({entry.group for entry in _entries(context) if entry.group})
    )
    return _group_items


def _load_details(context: bpy.types.Context) -> None:
    """Read the active unit, unless it has already been read."""
    entry = _active(context)
    if entry is None or entry.identity in _details:
        return
    root = prefs.corpus_root(context)
    if root is None:
        return
    unit = index.Unit(entry.seam, entry.relative, entry.identity, 0)
    try:
        _details[entry.identity] = index.details(unit, root)
    except Exception as error:  # a unit that will not parse is worth showing as such
        _details[entry.identity] = index.Details(
            entry.identity, entry.seam, (), ("could not be read: %s" % error,)
        )


def _on_active_changed(self, context) -> None:
    _load_details(context)


class ElysiumUnitEntry(bpy.types.PropertyGroup):
    """One row of the browser."""

    # UIList searches on `name`, so this holds the identity path a reviewer recognises.
    name: StringProperty()
    identity: StringProperty()
    relative: StringProperty()
    seam: StringProperty()
    group: StringProperty()
    size: IntProperty()


class ELYSIUM_UL_units(bpy.types.UIList):
    """The unit list. 'GRID' was removed in 5.0; only DEFAULT and COMPACT exist."""

    bl_idname = "ELYSIUM_UL_units"

    def draw_item(
        self, context, layout, data, item, icon, active_data, active_propname, index_
    ):
        row = layout.row(align=True)
        row.label(
            text=item.name,
            icon="OUTLINER_OB_ARMATURE" if item.seam == "characters" else "DOT",
        )
        size = row.row()
        size.alignment = "RIGHT"
        size.label(text="%.1f MB" % (item.size / 1e6) if item.size >= 1e5 else "")

    def filter_items(self, context, data, propname):
        """Combine the built-in search box with the subtree menu."""
        entries = getattr(data, propname)
        helper = bpy.types.UI_UL_list

        if self.filter_name:
            flags = helper.filter_items_by_name(
                self.filter_name, self.bitflag_filter_item, entries, "name"
            )
        else:
            flags = [self.bitflag_filter_item] * len(entries)

        chosen = context.scene.elysium_group
        if chosen != ALL_GROUPS:
            for position, entry in enumerate(entries):
                if entry.group != chosen:
                    flags[position] = 0

        order = helper.sort_items_by_name(entries, "name") if self.use_filter_sort_alpha else []
        return flags, order


class ELYSIUM_OT_refresh_index(bpy.types.Operator):
    """List the units of the selected seam."""

    bl_idname = "elysium.refresh_index"
    bl_label = "Refresh"
    bl_options = {"REGISTER"}

    @classmethod
    def poll(cls, context: bpy.types.Context) -> bool:
        return prefs.corpus_root(context) is not None

    def execute(self, context: bpy.types.Context):
        root = prefs.corpus_root(context)
        if root is None:
            self.report({"ERROR"}, "No corpus root configured")
            return {"CANCELLED"}

        scene = context.scene
        window = context.window
        window.cursor_set("WAIT")
        try:
            units = index.scan(root, scene.elysium_seam)
        finally:
            window.cursor_set("DEFAULT")

        _details.clear()
        scene.elysium_units.clear()
        for unit in units:
            entry = scene.elysium_units.add()
            entry.name = unit.stem
            entry.identity = unit.identity
            entry.relative = unit.relative
            entry.seam = unit.seam
            entry.group = unit.group
            entry.size = unit.byte_size
        scene.elysium_unit_index = 0
        scene.elysium_group = ALL_GROUPS

        self.report({"INFO"}, "%d unit(s) in %s" % (len(units), scene.elysium_seam))
        return {"FINISHED"}


class ELYSIUM_OT_import_selected(bpy.types.Operator):
    """Import the highlighted unit."""

    bl_idname = "elysium.import_selected"
    bl_label = "Import Selected"
    bl_options = {"REGISTER", "UNDO"}

    @classmethod
    def poll(cls, context: bpy.types.Context) -> bool:
        entry = _active(context)
        return entry is not None and entry.seam == "characters"

    def execute(self, context: bpy.types.Context):
        entry = _active(context)
        root = prefs.corpus_root(context)
        if entry is None or root is None:
            self.report({"ERROR"}, "Nothing selected, or no corpus root")
            return {"CANCELLED"}

        scene = context.scene
        window = context.window
        window.cursor_set("WAIT")
        try:
            ok, message = operators.import_unit(
                context, root / entry.relative, clips=scene.elysium_clips
            )
        finally:
            window.cursor_set("DEFAULT")
        self.report({"INFO"} if ok else {"ERROR"}, message)
        return {"FINISHED"} if ok else {"CANCELLED"}


class ELYSIUM_PT_browser(bpy.types.Panel):
    """Browse the corpus without opening anything."""

    bl_idname = "ELYSIUM_PT_browser"
    bl_label = "Browse"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "Elysium"

    def draw(self, context: bpy.types.Context) -> None:
        layout = self.layout
        scene = context.scene

        if prefs.corpus_root(context) is None:
            layout.label(text="No corpus root set", icon="ERROR")
            return

        row = layout.row(align=True)
        row.prop(scene, "elysium_seam", text="")
        row.operator("elysium.refresh_index", text="", icon="FILE_REFRESH")

        entries = scene.elysium_units
        if not len(entries):
            layout.label(text="Refresh to list this seam")
            return

        layout.prop(scene, "elysium_group", text="Subtree")
        layout.template_list(
            "ELYSIUM_UL_units", "", scene, "elysium_units", scene, "elysium_unit_index", rows=10
        )

        entry = _active(context)
        if entry is None:
            return

        if entry.seam == "characters":
            column = layout.column(align=True)
            column.prop(scene, "elysium_clips", text="")
            column.operator("elysium.import_selected", icon="IMPORT")

        detail = _details.get(entry.identity)
        if detail is None:
            layout.label(text=entry.identity)
            return

        box = layout.box()
        box.label(text=entry.identity)
        for key, value in detail.rows:
            line = box.row()
            line.label(text=key)
            line.label(text=value)
        for warning in detail.warnings:
            box.label(text=warning, icon="ERROR")


def register_properties() -> None:
    scene = bpy.types.Scene
    scene.elysium_units = CollectionProperty(type=ElysiumUnitEntry)
    scene.elysium_unit_index = IntProperty(default=0, update=_on_active_changed)
    scene.elysium_seam = EnumProperty(
        name="Seam", items=_seam_enum, description="Which export tree to list"
    )
    scene.elysium_group = EnumProperty(
        name="Subtree", items=_group_enum, description="Narrow the list to one subtree"
    )
    scene.elysium_clips = EnumProperty(
        name="Animations",
        items=operators.CLIP_MODES,
        default="NONE",
        description=(
            "A shared bank declares up to 722 clips, so nothing is imported unless asked"
        ),
    )


def unregister_properties() -> None:
    scene = bpy.types.Scene
    for name in (
        "elysium_clips",
        "elysium_group",
        "elysium_seam",
        "elysium_unit_index",
        "elysium_units",
    ):
        if hasattr(scene, name):
            delattr(scene, name)
    _details.clear()


CLASSES = (
    ElysiumUnitEntry,
    ELYSIUM_UL_units,
    ELYSIUM_OT_refresh_index,
    ELYSIUM_OT_import_selected,
    ELYSIUM_PT_browser,
)
