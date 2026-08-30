"""The glTF importer hook.

Every unit in the corpus lists its ELYSIUM extensions in `extensionsRequired`, so
Blender's stock importer refuses all 23414 of them outright. Declaring those names here
is the whole reason the add-on exists as an importer extension rather than a panel: the
importer whitelists a custom extension only when the declaration says `required=True`,
and an extension declared `required=False` unlocks nothing.

The hooks then do three things the rest of the tool depends on. They capture each unit's
extension payload while the importer still has the parsed document in hand, so no panel
ever has to re-read a 35 MB JSON chunk to answer a question. They filter the animation
list before any Action is built, which is the only way to open a bank whose clip closure
runs to four figures. And once the clips exist they hand them to `pose`, which is where
the split-rotation bones stop being posed by ordinary FK.
"""

from __future__ import annotations

import json

import bpy
from io_scene_gltf2.io.com.gltf2_io_extensions import Extension

from ..core import seams
from . import pose

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
    #: Whether this import poses its own split-rotation bones. A bank import defers: its
    #: clips are rebound to a body, and it is that body's skeleton they must agree with.
    poses_split_rotation: bool = True
    #: Root extension payload of the most recent import, by extension name.
    last_document_extensions: dict = {}
    #: Filepath of the most recent import.
    last_filepath: str = ""
    #: Armature objects the running import created.
    armatures: list = []
    #: Actions that existed before it started, so the ones it built can be told apart.
    previous_actions: set = set()

    @classmethod
    def reset(cls) -> None:
        cls.wanted_clips = None
        cls.poses_split_rotation = True
        cls.last_document_extensions = {}
        cls.last_filepath = ""
        cls.armatures = []
        cls.previous_actions = set()


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
        ImportState.armatures = []
        ImportState.previous_actions = set(bpy.data.actions)

    # -- animations -------------------------------------------------------------

    def gather_import_animations(self, animations, options, gltf) -> None:
        """Drop the clips this import did not ask for.

        The importer passes its animation list by reference precisely so a hook can
        change it, and the list is consulted once, before any Action exists. Filtering
        here is therefore the difference between importing four clips and importing the
        seven hundred a shared bank declares.

        Dropping a clip renumbers the list, and the importer indexed every node's channels
        by each clip's position in it before this hook ran. That table has to be renumbered
        with the list: left alone, a surviving clip is built from whatever channels the clip
        that used to hold its index had.
        """
        wanted = ImportState.wanted_clips
        if wanted is None:
            return
        kept = [
            index
            for index, animation in enumerate(animations)
            if getattr(animation, "name", None) in wanted
        ]
        animations[:] = [animations[index] for index in kept]

        renumbered = {old: new for new, old in enumerate(kept)}
        for node in getattr(gltf.data, "nodes", None) or []:
            table = getattr(node, "animations", None)
            if table:
                node.animations = {
                    renumbered[old]: channels
                    for old, channels in table.items()
                    if old in renumbered
                }

    def gather_import_scene_after_animation_hook(self, gltf_scene, blender_scene, gltf) -> None:
        """Pose the split-rotation bones of the clips this import built.

        The last point at which the Actions exist and the import still knows which of them
        are its own. A bank import defers the work to `animation.load_clips`, which knows
        the body the clips are about to be rebound to.
        """
        payload = ImportState.last_document_extensions.get(seams.CHARACTER_EXTENSION)
        if payload is None or not ImportState.poses_split_rotation:
            return
        created = [
            action
            for action in bpy.data.actions
            if action not in ImportState.previous_actions
        ]
        for armature in ImportState.armatures:
            pose.apply(armature, payload, created)

    # -- objects ----------------------------------------------------------------

    def gather_import_node_after_hook(self, vnode, gltf_node, blender_object, gltf) -> None:
        """Mark created objects and attach the body payload to the ones that answer for it.

        The mesh is what a reviewer selects; the armature is what carries the clips and the
        skeleton the payload describes. Both are asked for the payload, so both hold it.
        """
        if blender_object is None:
            return
        blender_object[MARKER_PROPERTY] = True
        if blender_object.type == "ARMATURE":
            ImportState.armatures.append(blender_object)

        payload = ImportState.last_document_extensions.get(seams.CHARACTER_EXTENSION)
        if payload is not None and blender_object.type in {"MESH", "ARMATURE"}:
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
