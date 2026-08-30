"""Animation-bank browsing and clip loading.

A body names one or two banks; following those names transitively reaches about 35 files
and 1,782 clips. This lists that closure and loads only what is asked for, because the
alternative is minutes of import and a `bpy.data.actions` nobody can navigate.
"""

from __future__ import annotations

import bpy
from bpy.props import BoolProperty, CollectionProperty, IntProperty, StringProperty

from .. import prefs
from ..adapters import animation, pose
from ..core import seams

#: Clip names of the highlighted bank, read on demand and kept until it changes.
_clips: dict = {}


def _unresolved(context, name: str) -> list:
    """Split bones a loaded clip was left raw for, from the Action it built.

    A clip row names an Action only once that clip is loaded, and only the bank it was
    loaded from owns that name, so the bank stamp is what makes the lookup safe.
    """
    entry = _active_bank(context)
    action = bpy.data.actions.get(name)
    if entry is None or action is None or action.get("elysium_bank") != entry.identity:
        return []
    return list(action.get(pose.UNRESOLVED_PROPERTY) or [])


def _bank_entries(context):
    return context.scene.elysium_banks


def _active_bank(context):
    entries = _bank_entries(context)
    position = context.scene.elysium_bank_index
    return entries[position] if 0 <= position < len(entries) else None


def _load_clip_names(context) -> None:
    entry = _active_bank(context)
    if entry is None or entry.identity in _clips:
        return
    root = prefs.corpus_root(context)
    if root is None:
        return
    _clips[entry.identity] = animation.clip_names(entry.identity, root)

    scene = context.scene
    scene.elysium_clip_list.clear()
    for name in _clips[entry.identity]:
        scene.elysium_clip_list.add().name = name
    scene.elysium_clip_index = 0


def _on_bank_changed(self, context) -> None:
    _load_clip_names(context)


class ElysiumBankEntry(bpy.types.PropertyGroup):
    name: StringProperty()
    identity: StringProperty()
    clip_count: IntProperty()
    depth: IntProperty()
    is_stub: BoolProperty()
    missing: BoolProperty()


class ElysiumClipEntry(bpy.types.PropertyGroup):
    name: StringProperty()


class ELYSIUM_UL_banks(bpy.types.UIList):
    bl_idname = "ELYSIUM_UL_banks"

    def draw_item(self, context, layout, data, item, icon, active_data, active_propname, index_):
        row = layout.row(align=True)
        if item.missing:
            row.label(text=item.name, icon="ERROR")
        elif item.is_stub:
            # Carries no clips of its own; it only forwards to other banks.
            row.label(text=item.name, icon="DECORATE_LINKED")
        else:
            row.label(text=item.name, icon="ANIM")
        count = row.row()
        count.alignment = "RIGHT"
        count.label(text=str(item.clip_count) if item.clip_count else "")


class ELYSIUM_UL_clips(bpy.types.UIList):
    bl_idname = "ELYSIUM_UL_clips"

    def draw_item(self, context, layout, data, item, icon, active_data, active_propname, index_):
        masked = bool(_unresolved(context, item.name))
        layout.label(text=item.name, icon="INFO" if masked else "ACTION")


class ELYSIUM_OT_scan_banks(bpy.types.Operator):
    """Walk every bank reachable from the selected body."""

    bl_idname = "elysium.scan_banks"
    bl_label = "Scan Banks"
    bl_options = {"REGISTER"}

    @classmethod
    def poll(cls, context: bpy.types.Context) -> bool:
        return (
            animation.body_payload(context.object) is not None
            and prefs.corpus_root(context) is not None
        )

    def execute(self, context: bpy.types.Context):
        payload = animation.body_payload(context.object)
        root = prefs.corpus_root(context)
        if payload is None or root is None:
            self.report({"ERROR"}, "Select an imported body, and set a corpus root")
            return {"CANCELLED"}

        window = context.window
        window.cursor_set("WAIT")
        try:
            closure = animation.closure_of(
                {"extensions": {seams.MODEL_EXTENSION: payload}}, root
            )
        finally:
            window.cursor_set("DEFAULT")

        scene = context.scene
        _clips.clear()
        scene.elysium_clip_list.clear()
        scene.elysium_banks.clear()
        for node in sorted(closure.nodes, key=lambda n: (-n.clip_count, n.identity)):
            entry = scene.elysium_banks.add()
            entry.name = node.identity.split(":", 2)[-1]
            entry.identity = node.identity
            entry.clip_count = node.clip_count
            entry.depth = node.depth
            entry.is_stub = node.is_stub
            entry.missing = not node.exists
        scene.elysium_bank_index = 0

        self.report(
            {"INFO"},
            "%d bank(s), %d clip(s), %.0f MB"
            % (len(closure.nodes), closure.clip_count, closure.byte_size / 1e6),
        )
        return {"FINISHED"}


class ELYSIUM_OT_load_clips(bpy.types.Operator):
    """Load clips from the highlighted bank onto the selected body."""

    bl_idname = "elysium.load_clips"
    bl_label = "Load"
    bl_options = {"REGISTER", "UNDO"}

    #: Load only the highlighted clip rather than the whole bank.
    one: BoolProperty(default=False, options={"SKIP_SAVE"})

    @classmethod
    def poll(cls, context: bpy.types.Context) -> bool:
        entry = _active_bank(context)
        return (
            entry is not None
            and entry.clip_count > 0
            and animation.armature_for(context.object) is not None
        )

    def execute(self, context: bpy.types.Context):
        entry = _active_bank(context)
        body = animation.armature_for(context.object)
        root = prefs.corpus_root(context)
        if entry is None or body is None or root is None:
            self.report({"ERROR"}, "Select an imported body, and set a corpus root")
            return {"CANCELLED"}

        scene = context.scene
        wanted = None
        if self.one:
            entries = scene.elysium_clip_list
            position = scene.elysium_clip_index
            if not (0 <= position < len(entries)):
                self.report({"ERROR"}, "No clip highlighted")
                return {"CANCELLED"}
            wanted = {entries[position].name}

        window = context.window
        window.cursor_set("WAIT")
        try:
            result = animation.load_clips(body, entry.identity, root, wanted)
        finally:
            window.cursor_set("DEFAULT")

        if not result.loaded:
            self.report({"ERROR"}, result.message or "nothing was loaded")
            return {"CANCELLED"}
        self.report({"WARNING"} if result.missing_bones else {"INFO"}, result.message)
        return {"FINISHED"}


class ELYSIUM_PT_clips(bpy.types.Panel):
    """The bank closure of the selected body."""

    bl_idname = "ELYSIUM_PT_clips"
    bl_label = "Animation Banks"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "Elysium"

    @classmethod
    def poll(cls, context: bpy.types.Context) -> bool:
        return animation.body_payload(context.object) is not None

    def draw(self, context: bpy.types.Context) -> None:
        layout = self.layout
        scene = context.scene

        layout.operator("elysium.scan_banks", icon="FILE_REFRESH")
        entries = scene.elysium_banks
        if not len(entries):
            layout.label(text="Scan to list the banks this body reaches")
            return

        total = sum(entry.clip_count for entry in entries)
        layout.label(text="%d file(s), %d clip(s)" % (len(entries), total))
        layout.template_list(
            "ELYSIUM_UL_banks", "", scene, "elysium_banks", scene, "elysium_bank_index", rows=6
        )

        entry = _active_bank(context)
        if entry is None:
            return
        if entry.missing:
            layout.label(text="Named but never exported", icon="ERROR")
            return
        if entry.is_stub:
            layout.label(text="Include stub: forwards to other banks", icon="INFO")
            return

        if animation.armature_for(context.object) is None:
            layout.label(text="Select the body to load onto", icon="INFO")

        row = layout.row(align=True)
        row.operator("elysium.load_clips", text="Load All %d" % entry.clip_count).one = False

        clips = scene.elysium_clip_list
        if not len(clips):
            return
        layout.template_list(
            "ELYSIUM_UL_clips", "", scene, "elysium_clip_list", scene, "elysium_clip_index", rows=6
        )
        highlighted = clips[scene.elysium_clip_index] if (
            0 <= scene.elysium_clip_index < len(clips)
        ) else None
        if highlighted is not None:
            masked = _unresolved(context, highlighted.name)
            if masked:
                layout.label(
                    text="Masked overlay: %s left model-space (no host chain)"
                    % ", ".join(masked),
                    icon="INFO",
                )
        layout.operator("elysium.load_clips", text="Load Highlighted").one = True


def register_properties() -> None:
    scene = bpy.types.Scene
    scene.elysium_banks = CollectionProperty(type=ElysiumBankEntry)
    scene.elysium_bank_index = IntProperty(default=0, update=_on_bank_changed)
    scene.elysium_clip_list = CollectionProperty(type=ElysiumClipEntry)
    scene.elysium_clip_index = IntProperty(default=0)


def unregister_properties() -> None:
    scene = bpy.types.Scene
    for name in ("elysium_clip_index", "elysium_clip_list", "elysium_bank_index", "elysium_banks"):
        if hasattr(scene, name):
            delattr(scene, name)
    _clips.clear()


CLASSES = (
    ElysiumBankEntry,
    ElysiumClipEntry,
    ELYSIUM_UL_banks,
    ELYSIUM_UL_clips,
    ELYSIUM_OT_scan_banks,
    ELYSIUM_OT_load_clips,
    ELYSIUM_PT_clips,
)
