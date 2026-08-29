"""Inspector panels.

Everything drawn here comes from payloads captured during import and stashed on the
datablock. Nothing re-reads a file: a character's extension JSON runs to 35 MB, and
paying that to redraw a sidebar would make the panel the most expensive thing in the
tool.
"""

from __future__ import annotations

import json

import bpy

from .. import prefs
from ..adapters import hooks, materials
from ..core import seams

CATEGORY = "Elysium"


def _draw_tree(layout, node, path: str, depth: int = 0) -> None:
    """Draw a decoded payload as collapsible rows.

    Blender has no template for an arbitrary tree, and the payloads have no fixed shape,
    so this walks whatever it is given rather than assuming a schema.
    """
    if isinstance(node, dict):
        items = node.items()
    elif isinstance(node, list):
        if len(node) > 12:
            layout.label(text="%d entries" % len(node))
            return
        items = ((str(index), value) for index, value in enumerate(node))
    else:
        layout.label(text=str(node)[:96])
        return

    for key, value in items:
        if isinstance(value, (dict, list)) and value:
            header, body = layout.panel("%s_%s" % (path, key), default_closed=True)
            count = len(value)
            header.label(text="%s (%d)" % (key, count))
            if body and depth < 4:
                _draw_tree(body, value, "%s_%s" % (path, key), depth + 1)
        else:
            row = layout.row()
            row.label(text=str(key))
            row.label(text="" if value is None else str(value)[:64])


class ElysiumPanel:
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = CATEGORY


class ELYSIUM_PT_corpus(ElysiumPanel, bpy.types.Panel):
    """Where the corpus is and what can be done to it as a whole."""

    bl_idname = "ELYSIUM_PT_corpus"
    bl_label = "Corpus"

    def draw(self, context: bpy.types.Context) -> None:
        layout = self.layout
        root = prefs.corpus_root(context)
        if root is None:
            layout.label(text="No corpus root set", icon="ERROR")
            layout.label(text="Set one in Preferences > Add-ons")
        else:
            layout.label(text=str(root), icon="FILE_FOLDER")
        layout.operator("elysium.import_glb", icon="IMPORT")
        row = layout.row()
        row.enabled = root is not None
        row.operator("elysium.corpus_report", icon="CHECKMARK")


class ELYSIUM_PT_character(ElysiumPanel, bpy.types.Panel):
    """The body payload of the selected object."""

    bl_idname = "ELYSIUM_PT_character"
    bl_label = "Character"

    @classmethod
    def poll(cls, context: bpy.types.Context) -> bool:
        obj = context.object
        return obj is not None and hooks.CHARACTER_PROPERTY in obj

    def draw(self, context: bpy.types.Context) -> None:
        layout = self.layout
        payload = hooks.unstash(context.object, hooks.CHARACTER_PROPERTY)
        if payload is None:
            layout.label(text="Payload could not be read", icon="ERROR")
            return

        identity = (payload.get("identity") or {}).get("asset", "?")
        layout.label(text=identity)

        coverage = seams.coverage(payload)
        box = layout.box()
        box.label(
            text="Coverage: %s" % ("complete" if coverage.clean else "incomplete"),
            icon="CHECKMARK" if coverage.clean else "ERROR",
        )
        box.label(text="mapped %d  proven omissions %d"
                  % (len(coverage.mapped), len(coverage.omitted_proven)))
        if coverage.typed_unidentified:
            box.label(text="typed but unidentified: %d" % len(coverage.typed_unidentified))

        header, body = layout.panel("elysium_character_tree", default_closed=True)
        header.label(text="Extension payload")
        if body:
            _draw_tree(body, payload, "elysium_character")


class ELYSIUM_PT_material(ElysiumPanel, bpy.types.Panel):
    """What the active material reproduces, and what it does not."""

    bl_idname = "ELYSIUM_PT_material"
    bl_label = "Material"

    @classmethod
    def poll(cls, context: bpy.types.Context) -> bool:
        material = getattr(context.object, "active_material", None)
        return material is not None and hooks.MATERIAL_REFERENCE_PROPERTY in material

    def draw(self, context: bpy.types.Context) -> None:
        layout = self.layout
        material = context.object.active_material
        layout.label(text=material[hooks.MATERIAL_REFERENCE_PROPERTY])

        if material.get(materials.SENTINEL_PROPERTY):
            # The source names a studio texture that resolves to no VMT. The engine
            # draws its error checker here too, so this is faithful rather than broken.
            layout.label(text="No VMT: engine draws the error checker", icon="INFO")
            return

        shader = material.get(materials.SHADER_PROPERTY)
        if shader:
            layout.label(text="Shader: %s" % shader)

        unreproduced = json.loads(material.get(materials.APPROXIMATION_PROPERTY, "[]"))
        if unreproduced:
            box = layout.box()
            box.label(text="Not reproduced (%d)" % len(unreproduced), icon="ERROR")
            for parameter in unreproduced[:12]:
                box.label(text=parameter)
            if len(unreproduced) > 12:
                box.label(text="... and %d more" % (len(unreproduced) - 12))
        else:
            layout.label(text="Fully reproduced", icon="CHECKMARK")

        payload = hooks.unstash(material, "elysium_vtmb_material")
        if payload is None:
            return
        header, body = layout.panel("elysium_material_tree", default_closed=True)
        header.label(text="Extension payload")
        if body:
            _draw_tree(body, payload, "elysium_material")


CLASSES = (ELYSIUM_PT_corpus, ELYSIUM_PT_character, ELYSIUM_PT_material)
