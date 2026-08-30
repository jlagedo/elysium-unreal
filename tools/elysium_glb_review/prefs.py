"""Add-on preferences.

The only setting that matters is where the corpus lives. A unit names its neighbours by
identity and records nothing about the corpus layout, so without a root the tool can
open one file and follow none of its references.
"""

from __future__ import annotations

import os
from pathlib import Path

import bpy

from .core import ids

#: Checked in order. The first names an explicit override; the second is where the
#: pipeline puts the corpus when nothing overrides it.
ROOT_ENVIRONMENT_VARIABLES = ("ELYSIUM_EXPORT_V2_ROOT",)
WORK_ROOT_VARIABLE = "ELYSIUM_WORK_ROOT"
WORK_ROOT_SUBDIRECTORY = "exports_v2"


def default_corpus_root() -> str:
    """The corpus root implied by this machine's environment, if any.

    Blender launched from a shortcut inherits none of the shell's variables, so this is
    a convenience rather than a mechanism; the preference is the real answer.
    """
    for name in ROOT_ENVIRONMENT_VARIABLES:
        value = os.environ.get(name, "").strip()
        if value:
            return value
    work = os.environ.get(WORK_ROOT_VARIABLE, "").strip()
    if work:
        return str(Path(work) / WORK_ROOT_SUBDIRECTORY)
    return ""


class ElysiumGlbReviewPreferences(bpy.types.AddonPreferences):
    """Where the corpus is, and whether it looks like a corpus."""

    # For an extension this is the namespaced module name, which differs per repository.
    bl_idname = __package__

    corpus_root: bpy.props.StringProperty(
        name="Corpus Root",
        description="Directory holding the models, materials, textures and "
        "surface-properties export trees",
        subtype="DIR_PATH",
        default=default_corpus_root(),
    )

    def draw(self, context: bpy.types.Context) -> None:
        layout = self.layout
        layout.prop(self, "corpus_root")

        root = Path(self.corpus_root) if self.corpus_root else None
        if root is None or not root.is_dir():
            layout.label(text="Set the corpus root to follow references", icon="ERROR")
            return

        present = [name for name in sorted(set(ids.SEAM_DIRECTORY.values())) if (root / name).is_dir()]
        missing = [name for name in sorted(set(ids.SEAM_DIRECTORY.values())) if not (root / name).is_dir()]
        row = layout.row()
        row.label(text="Seams found: " + (", ".join(present) or "none"),
                  icon="CHECKMARK" if present else "ERROR")
        if missing:
            layout.label(text="Missing: " + ", ".join(missing), icon="INFO")


def preferences(context: bpy.types.Context | None = None) -> ElysiumGlbReviewPreferences | None:
    """This add-on's preferences, or None when it is not enabled."""
    context = context or bpy.context
    entry = context.preferences.addons.get(__package__)
    return None if entry is None else entry.preferences


def corpus_root(context: bpy.types.Context | None = None, *, near: str | Path | None = None) -> Path | None:
    """The corpus root to resolve references against.

    A file opened from inside a corpus answers the question by itself, so importing one
    by hand works without configuring anything first. The preference wins when it is
    set, because it is the deliberate answer.
    """
    settings = preferences(context)
    if settings and settings.corpus_root:
        root = Path(bpy.path.abspath(settings.corpus_root))
        if root.is_dir():
            return root
    if near is not None:
        return ids.corpus_root_for(near)
    return None


CLASSES = (ElysiumGlbReviewPreferences,)
