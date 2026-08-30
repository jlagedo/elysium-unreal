"""The six evidence-backed residue categories, exactly as the seam map lists them.

Residue is a classification, not an omission from the index: a residue row still carries the
member's length and hash. A category applied without evidence is what the guarantee exists to
catch, so each rule here states the fact that proves the engine never reads the member and
nothing falls into a category by default -- a member no rule names stays `unclaimed`.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Iterable

from elysium_pipeline.formats.corpus_index_glb.model import RESIDUE_CATEGORIES

#: `authoring-leftover` -- tool-owned formats or dead extensions the engine's loaders do not
#: compose. The Sound Forge peak caches, the Worldcraft command sequences, the model compiler's
#: reject list and its per-texture configuration files, the texture compiler's `.vmt.txt` and
#: `.txt` siblings, the two backup spellings and the two design documents.
#: Each rule is anchored to the tree its evidence is about, so a member of the same spelling
#: appearing anywhere else surfaces as unclaimed rather than inheriting a category proven
#: somewhere else.
_TOOL_EXTENSIONS = {
    ".sfk": ("sound/", "Sound Forge peak cache written beside the wave it was edited from"),
    ".pk": ("sound/", "Sound Forge peak cache written beside the wave it was edited from"),
}
_BAD_MODELS = "bad_models.txt"
_COMMAND_SEQUENCE = "cmdseq.wc"
_COMMAND_SEQUENCE_TREE = "models/"

_NAMED_AUTHORING_LEFTOVERS = {
    "scripts/liblist.gam~": "editor backup of scripts/liblist.gam under a dead extension",
    "python/warehouse/warehouse.old": (
        "Python source under an extension the interpreter's importer never composes"
    ),
    "scripts/hl2_scripts.dsp": "Visual Studio project file for the HL2 script sources",
    "vdata/system/stealth.xls": "design spreadsheet no shipped code path opens",
}

#: `unreachable-member` -- the one loose tree the VPKs ship a model closure into, below no
#: root any engine path composes.
_UNPACKED_TREE = "unpacked 0.74/"

_ENGINE_BINARIES = ("dlls/", "cl_dlls/")
_ENGINE_BINARY_NAMED = {"dlls/vampire.dll.12"}

#: `user-data` -- written by a play session. Each rule names both the tree and the extension the
#: engine writes there, so a file of another kind appearing below one of them stays unclaimed
#: rather than inheriting the category.
_USER_DATA_TREES = (
    ("save/", ".sav", "a saved game, written by a play session"),
    ("logs/", ".log", "the console transcript, written by a play session"),
)

#: `foreign-file` -- not shipped by retail or the patch; placed by this project. The capture and
#: replay console scripts this repository writes into the install's `cfg/` tree.
_FOREIGN_PREFIX = "cfg/elysium_"

#: `excluded-by-decision` -- the owner call that the logo videos are not exported; size and hash
#: only.
_EXCLUDED_TREE = "media/"
_EXCLUDED_EXTENSION = ".bik"


@dataclass(frozen=True, slots=True)
class InstallFacts:
    """What the sibling-anchored rules ask of the install, counted once per walk.

    Two rules name the member they sit beside -- the texture-compiler configuration below
    `models/` and the one below `materials/` -- so each is anchored to that member instead of to
    the spelling alone. A member with no such neighbour proves nothing and stays unclaimed.
    """

    #: Every install key the walk found.
    members: frozenset[str]
    #: Every directory below `models/` that holds at least one `.mdl`.
    model_directories: frozenset[str]

    @classmethod
    def of(cls, keys: Iterable[str]) -> "InstallFacts":
        members = frozenset(keys)
        return cls(
            members=members,
            model_directories=frozenset(
                key.rsplit("/", 1)[0]
                for key in members
                if key.startswith("models/") and key.endswith(".mdl") and "/" in key
            ),
        )


def _row(category: str, evidence: str) -> dict[str, Any]:
    if category not in RESIDUE_CATEGORIES:
        raise ValueError(f"unknown residue category {category!r}")
    return {"category": category, "evidence": evidence}


def classify(path: str, facts: InstallFacts | None = None) -> dict[str, Any] | None:
    """The residue row for one install-relative key, or None when no category claims it.

    Order matters only where two rules could both match; they do not, because each rule names a
    tree, an extension or a literal path that no other rule names. `facts` carries the install
    the key was walked from; the two rules that name a neighbouring member claim nothing without
    it, because their evidence is that neighbour.
    """

    key = str(path).replace("\\", "/").lower()
    name = key.rsplit("/", 1)[-1]
    _, dot, suffix = name.rpartition(".")
    extension = ("." + suffix) if dot else ""

    if key in _NAMED_AUTHORING_LEFTOVERS:
        return _row("authoring-leftover", _NAMED_AUTHORING_LEFTOVERS[key])
    if extension in _TOOL_EXTENSIONS:
        tree, evidence = _TOOL_EXTENSIONS[extension]
        if key.startswith(tree):
            return _row("authoring-leftover", evidence)
    if name == _COMMAND_SEQUENCE and key.startswith(_COMMAND_SEQUENCE_TREE):
        return _row(
            "authoring-leftover",
            "Worldcraft/Hammer command sequence the model compiler was driven with",
        )
    if name == _BAD_MODELS and key.startswith("models/"):
        return _row(
            "authoring-leftover",
            "the model compiler's reject list; no engine loader composes a path to it",
        )
    if key.startswith("models/") and extension == ".txt":
        if facts is not None and key.rsplit("/", 1)[0] in facts.model_directories:
            return _row(
                "authoring-leftover",
                "texture-compiler configuration in the directory of the model it was authored "
                "with; the engine composes models/<model>.mdl and never a sibling .txt",
            )
        return None
    if key.startswith("materials/") and extension == ".txt" and key != "materials/fonts/fontlist.txt":
        # Both spellings the install ships: `<material>.vmt.txt` and `<material>.txt`.
        material = key[: -len(".txt")]
        if not material.endswith(".vmt"):
            material += ".vmt"
        if facts is not None and material in facts.members:
            return _row(
                "authoring-leftover",
                "texture-compiler configuration beside the material it was authored with; the "
                "engine composes materials/<material>.vmt and never a sibling .txt",
            )
        return None

    if key.startswith(_UNPACKED_TREE):
        return _row(
            "unreachable-member",
            "the model path rule composes models/<model>.mdl, so no engine path reaches this "
            "prefix (seam_map_model.md, 'Retired identities')",
        )
    if key.startswith("models/") and extension == ".vmt":
        return _row(
            "unreachable-member",
            "the material path rule composes materials/<material>.vmt, so no engine path "
            "reaches a .vmt below models/ (seam_map_material.md, 'Unit identity')",
        )

    if key in _ENGINE_BINARY_NAMED or (
        extension == ".dll" and key.startswith(_ENGINE_BINARIES)
    ):
        return _row("engine-binary", "code, not content")

    for tree, tree_extension, evidence in _USER_DATA_TREES:
        if key.startswith(tree) and extension == tree_extension:
            return _row("user-data", evidence)

    if key.startswith(_FOREIGN_PREFIX):
        return _row(
            "foreign-file",
            "not shipped by retail or the patch; placed in the install by this project",
        )

    if key.startswith(_EXCLUDED_TREE) and extension == _EXCLUDED_EXTENSION:
        return _row(
            "excluded-by-decision",
            "the owner call that the logo videos are not exported; size and hash only",
        )

    return None
