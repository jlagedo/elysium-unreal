"""The six evidence-backed residue categories, exactly as the seam map lists them.

Residue is a classification, not an omission from the index: a residue row still carries the
member's length and hash. A category applied without evidence is what the guarantee exists to
catch, so each rule here states the fact that proves the engine never reads the member and
nothing falls into a category by default -- a member no rule names stays `unclaimed`.
"""

from __future__ import annotations

from dataclasses import dataclass
import re
from typing import Any, Callable, Iterable

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

#: `authoring-leftover` -- the two lip-sync tool products the sound tree ships. Neither is
#: claimed by its spelling alone: the rule reads the member's opening bytes and claims it only
#: when the content signature the tool writes is there, because the evidence for "no loader reads
#: this" is what the file *is*, not where it sits.
_SOUND_TEXT_SIGNATURES = (
    (
        "_phonemeaudit.txt",
        re.compile(rb"^\|\s*Microsoft Speech API"),
        "the lip-sync tool's phoneme audit dump, opening on its own "
        "'| Microsoft Speech API ... | LipSync API ...' column header; no loader composes a path "
        "to it and the sound seam publishes the waves it audits",
    ),
    (
        "_sound.txt",
        re.compile(rb"^\{[^{}\r\n]*\.wav\}\{"),
        "a `{<wave>}{<line text>}` line table the lip-sync tool was fed, opening on that "
        "signature; no loader composes a path to it and the sound seam publishes the waves it "
        "lists",
    ),
)
_SOUND_TREE = "sound/"

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

#: `unreachable-member` -- the model loader opens a VTX or PHY only beside the MDL it was
#: compiled with, so a VTX or PHY whose stem ships no `.mdl` is a member no engine path reaches.
#: The spellings are the ones `walk._model_companions` owns by their stem.
_MODEL_ORPHAN_TREE = "models/"

#: `unreachable-member` -- a LIP is opened beside the sound it was authored for, so a LIP whose
#: stem ships neither spelling of that sound is a member no engine path reaches.
_LIP_TWINS = (".wav", ".mp3")

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
    """What the neighbour-anchored, content-anchored and graph-anchored rules ask of the install.

    Several rules name something outside the key they classify -- the member it sits beside, the
    bytes it opens with, or the absence of any edge to it in the corpus reference graph -- so
    each is anchored to that fact instead of to the spelling alone. A rule whose fact this
    record cannot answer claims nothing and the member stays unclaimed.
    """

    #: Every install key the walk found.
    members: frozenset[str]
    #: Every directory below `models/` that holds at least one `.mdl`.
    model_directories: frozenset[str]
    #: `key -> the member's opening bytes`, for the rules whose evidence is a content signature.
    #: None where the caller cannot read the install; those rules then claim nothing.
    head: Callable[[str], bytes] | None = None
    #: Every identity the published corpus's reference graph holds an edge to -- the key set of
    #: `inverse`. None where the graph is not available yet, and the rules that need it claim
    #: nothing, because their evidence is that the graph names the member nowhere.
    referenced: frozenset[str] | None = None

    @classmethod
    def of(
        cls,
        keys: Iterable[str],
        *,
        head: Callable[[str], bytes] | None = None,
        referenced: Iterable[str] | None = None,
    ) -> "InstallFacts":
        members = frozenset(keys)
        return cls(
            members=members,
            model_directories=frozenset(
                key.rsplit("/", 1)[0]
                for key in members
                if key.startswith("models/") and key.endswith(".mdl") and "/" in key
            ),
            head=head,
            referenced=None if referenced is None else frozenset(referenced),
        )

    def opening_bytes(self, key: str, limit: int = 256) -> bytes:
        """The member's first bytes, or empty where the install cannot be read."""

        if self.head is None:
            return b""
        return self.head(key)[:limit]


def _row(category: str, evidence: str) -> dict[str, Any]:
    if category not in RESIDUE_CATEGORIES:
        raise ValueError(f"unknown residue category {category!r}")
    return {"category": category, "evidence": evidence}


def _model_stems(key: str) -> tuple[str, ...]:
    """The MDL stems one VTX or PHY spelling could belong to, `walk._model_companions`' rule.

    `<model>.dx80.vtx` and `<model>.dx7_2bone.vtx` carry a variant infix; `<model>.vtx` and
    `<model>.phy` do not. Both spellings are tried for a VTX, so a model stem that itself carries
    a dot is still matched by the second.
    """

    if key.endswith(".vtx"):
        bare = key[: -len(".vtx")]
        if "." in bare.rsplit("/", 1)[-1]:
            return (bare.rpartition(".")[0], bare)
        return (bare,)
    if key.endswith(".phy"):
        return (key[: -len(".phy")],)
    return ()


def classify(
    path: str, facts: InstallFacts | None = None, byte_length: int | None = None
) -> dict[str, Any] | None:
    """The residue row for one install-relative key, or None when no category claims it.

    Order matters only where two rules could both match. Each named rule states a fact about the
    member -- a tree, an extension, a neighbour, a content signature, the reference graph -- and
    those do not overlap; the zero-length rule is last because it is the weakest evidence there
    is, so a member a named rule can explain keeps that explanation. `facts` carries the install
    the key was walked from; the rules anchored to a neighbouring member, to a content signature
    or to the reference graph claim nothing without it, because those facts are their evidence.
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
    if key.startswith(_SOUND_TREE) and extension == ".txt" and facts is not None:
        for tail, signature, evidence in _SOUND_TEXT_SIGNATURES:
            if name.endswith(tail) and signature.match(facts.opening_bytes(key)):
                return _row("authoring-leftover", evidence)
    if key.startswith("models/") and extension == ".txt":
        # A `.txt` with no `.mdl` in its directory proves nothing by its spelling; it falls
        # through to the rules below, which is where the zero-length ones are answered.
        if facts is not None and key.rsplit("/", 1)[0] in facts.model_directories:
            return _row(
                "authoring-leftover",
                "texture-compiler configuration in the directory of the model it was authored "
                "with; the engine composes models/<model>.mdl and never a sibling .txt",
            )
    elif key.startswith("materials/") and extension == ".txt" and key != "materials/fonts/fontlist.txt":
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

    if key.startswith(_UNPACKED_TREE):
        return _row(
            "unreachable-member",
            "the model path rule composes models/<model>.mdl, so no engine path reaches this "
            "prefix",
        )
    if key.startswith("models/") and extension == ".vmt":
        return _row(
            "unreachable-member",
            "the material path rule composes materials/<material>.vmt, so no engine path "
            "reaches a .vmt below models/",
        )
    if key.startswith(_MODEL_ORPHAN_TREE) and facts is not None:
        stems = _model_stems(key)
        if stems and not any(stem + ".mdl" in facts.members for stem in stems):
            return _row(
                "unreachable-member",
                "the model loader opens a VTX or PHY only beside the MDL it was compiled with, "
                f"and {stems[0]}.mdl is absent from the install",
            )
    if key.startswith(_SOUND_TREE) and extension == ".lip" and facts is not None:
        stem = key[: -len(".lip")]
        if not any(stem + twin in facts.members for twin in _LIP_TWINS):
            return _row(
                "unreachable-member",
                "a LIP is loaded beside the sound it was authored for, and neither "
                f"{stem}.wav nor {stem}.mp3 is in the install",
            )
    if (
        key.startswith(_SOUND_TREE)
        and not extension
        and facts is not None
        and facts.referenced is not None
    ):
        # The sound seam keys a unit by the member's own path below `sound/`, so a member with no
        # extension can only be reached by a reference that spells it exactly; the inverse graph
        # is where every such reference would appear.
        identity = "vtmb:sound:" + key[len(_SOUND_TREE):]
        if identity not in facts.referenced:
            return _row(
                "unreachable-member",
                "a sound member with no extension: no authored reference spells it, so the "
                f"corpus reference graph holds no edge to {identity}",
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

    # `authoring-leftover`, last, and the weakest evidence there is: the member carries no byte,
    # so whatever its spelling promises, no loader reads content out of it. A member a named rule
    # above could explain has already returned with that explanation.
    if byte_length == 0:
        return _row("authoring-leftover", "zero-length member")

    return None
