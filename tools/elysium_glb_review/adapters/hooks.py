"""The glTF importer hook.

Every unit in the corpus lists its ELYSIUM extensions in `extensionsRequired`, so
Blender's stock importer refuses all 23414 of them outright. Declaring those names here
is the whole reason the add-on exists as an importer extension rather than a panel: the
importer whitelists a custom extension only when the declaration says `required=True`,
and an extension declared `required=False` unlocks nothing.

The hooks then do two things the rest of the tool depends on. They capture each unit's
extension payload while the importer still has the parsed document in hand, so no panel
ever has to re-read a 35 MB JSON chunk to answer a question. And they filter the
animation list before any Action is built, which is the only way to open a bank whose
clip closure runs to four figures.
"""

from __future__ import annotations

import json

import bpy
from io_scene_gltf2.io.com.gltf2_io_extensions import Extension

from ..core import seams

#: Object custom property holding the character payload of the body a node belongs to.
CHARACTER_PROPERTY = "elysium_vtmb_character"
#: Material custom property holding the identity the primitive named.
MATERIAL_REFERENCE_PROPERTY = "elysium_material_reference"
#: Set on anything this add-on created, so panels can tell it apart from other imports.
MARKER_PROPERTY = "elysium_glb_review"


class ImportState:
    """What one import needs to remember, and what it hands to the rest of the tool.

    The importer instantiates the hook class per import, but the filter has to be set
    before the operator runs, so the wanted-clip selection lives here at module scope
    rather than on the instance.
    """

    #: Clip names to keep, or None to keep everything the file declares.
    wanted_clips: set[str] | None = None
    #: Root extension payload of the most recent import, by extension name.
    last_document_extensions: dict = {}
    #: Filepath of the most recent import.
    last_filepath: str = ""

    @classmethod
    def reset(cls) -> None:
        cls.wanted_clips = None
        cls.last_document_extensions = {}
        cls.last_filepath = ""


def stash(target, key: str, payload) -> None:
    """Store a decoded payload on a datablock as JSON.

    ID property arrays must hold homogeneous scalars, and an extension payload is
    arbitrary decoder output across two hundred parameter keys, so a mixed-type array
    anywhere in the tree would raise on assignment. A string always survives.
    """
    target[key] = json.dumps(payload, separators=(",", ":"))


def unstash(target, key: str):
    """Read back a payload stored by `stash`, or None when there is none."""
    raw = target.get(key)
    if not raw:
        return None
    try:
        return json.loads(raw)
    except (TypeError, ValueError):
        return None


class glTF2ImportUserExtension:  # noqa: N801  (name fixed by the glTF importer)
    """Discovered by `io_scene_gltf2` on every enabled add-on, by exact class name."""

    def __init__(self) -> None:
        # A hook that raises is otherwise swallowed and logged, which would leave a
        # half-built scene looking like a successful import.
        self.is_critical = True
        self.extensions = [
            Extension(name=name, extension={}, required=True)
            for name in seams.ALL_EXTENSIONS
        ]

    # -- document ---------------------------------------------------------------

    def gather_import_gltf_before_hook(self, gltf) -> None:
        """Capture the root extension payloads before anything is built."""
        ImportState.last_filepath = getattr(gltf, "filename", "") or ""
        ImportState.last_document_extensions = dict(
            getattr(gltf.data, "extensions", None) or {}
        )

    # -- animations -------------------------------------------------------------

    def gather_import_animations(self, animations, options, gltf) -> None:
        """Drop the clips this import did not ask for.

        The importer passes its animation list by reference precisely so a hook can
        change it, and the list is consulted once, before any Action exists. Filtering
        here is therefore the difference between importing four clips and importing the
        seven hundred a shared bank declares.
        """
        wanted = ImportState.wanted_clips
        if wanted is None:
            return
        animations[:] = [
            animation
            for animation in animations
            if getattr(animation, "name", None) in wanted
        ]

    # -- objects ----------------------------------------------------------------

    def gather_import_node_after_hook(self, vnode, gltf_node, blender_object, gltf) -> None:
        """Mark created objects and attach the body payload to the mesh object."""
        if blender_object is None:
            return
        blender_object[MARKER_PROPERTY] = True

        payload = ImportState.last_document_extensions.get(seams.CHARACTER_EXTENSION)
        if payload is not None and blender_object.type == "MESH":
            stash(blender_object, CHARACTER_PROPERTY, payload)

    def gather_import_material_after_hook(
        self, gltf_material, vertex_color, blender_mat, gltf
    ) -> None:
        """Record the material identity the primitive named.

        The character unit carries no textures at all; its core materials are neutral
        placeholders whose only real content is this string. Everything the material
        adapter does starts from it.
        """
        if blender_mat is None:
            return
        blender_mat[MARKER_PROPERTY] = True
        extension = (getattr(gltf_material, "extensions", None) or {}).get(
            seams.MATERIAL_REFERENCE_EXTENSION
        )
        identity = (extension or {}).get("material")
        if identity:
            blender_mat[MATERIAL_REFERENCE_PROPERTY] = identity
