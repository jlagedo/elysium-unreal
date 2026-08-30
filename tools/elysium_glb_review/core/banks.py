"""Include-model closure.

A model unit carries only its own clips and names the models it includes by identity.
That name is not the whole story: an included model includes others in turn, and several
of the ones a body names directly are include-stubs holding no clips at all. blood_doll
declares one; the clips actually reachable from it live across sixteen files.

The closure is therefore a graph walk, and it is expensive: across the 285 bodies that
declare includes the median closure is 35 files, 1782 clips and 275 MB. Nothing here loads
a clip. The walk exists so a caller can show the closure and let a reviewer choose,
because importing it wholesale would take minutes and bury bpy.data.actions.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from . import glb, ids, seams

#: The dependency role a model unit gives every model it includes.
BANK_ROLE = "model"


@dataclass(frozen=True)
class BankNode:
    """One file in a closure."""

    identity: str
    path: Path
    exists: bool
    #: Clips the file itself carries. Zero marks an include-stub.
    clip_count: int
    byte_size: int
    #: Banks this file names in turn.
    children: tuple[str, ...]
    #: Distance from the body that started the walk.
    depth: int

    @property
    def is_stub(self) -> bool:
        """Whether the file holds no clips of its own and only forwards to others."""
        return self.exists and self.clip_count == 0


@dataclass(frozen=True)
class Closure:
    """Every bank reachable from one body."""

    root: str
    nodes: tuple[BankNode, ...]

    @property
    def clip_count(self) -> int:
        return sum(node.clip_count for node in self.nodes)

    @property
    def byte_size(self) -> int:
        return sum(node.byte_size for node in self.nodes)

    @property
    def missing(self) -> tuple[BankNode, ...]:
        return tuple(node for node in self.nodes if not node.exists)

    def with_clips(self) -> tuple[BankNode, ...]:
        """The subset worth offering to a reviewer."""
        return tuple(node for node in self.nodes if node.clip_count)


def _inspect(identity: str, root: Path, depth: int) -> BankNode:
    path = ids.resolve(identity, root)
    if path is None or not path.is_file():
        return BankNode(identity, path or Path(), False, 0, 0, (), depth)

    document = glb.read_json(path)
    payload = seams.root_extension(document, seams.MODEL_EXTENSION) or {}
    children = tuple(
        reference.identity
        for reference in seams.dependencies(payload)
        if reference.role == BANK_ROLE
    )
    return BankNode(
        identity=identity,
        path=path,
        exists=True,
        clip_count=len(document.get("animations") or []),
        byte_size=path.stat().st_size,
        children=children,
        depth=depth,
    )


def closure(document: dict, root: str | Path, *, max_depth: int = 16) -> Closure:
    """Walk every included model reachable from a loaded model document.

    Breadth-first so a node's recorded depth is its shortest distance from the body.
    Visited identities are skipped, which makes the walk safe on the cycles the include
    graph contains.
    """
    root = Path(root)
    payload = seams.root_extension(document, seams.MODEL_EXTENSION) or {}
    identity = seams.asset_id(document) or "<unknown>"

    queue = [
        (reference.identity, 1)
        for reference in seams.dependencies(payload)
        if reference.role == BANK_ROLE
    ]
    seen: set[str] = set()
    nodes: list[BankNode] = []

    while queue:
        bank, depth = queue.pop(0)
        if bank in seen or depth > max_depth:
            continue
        seen.add(bank)
        node = _inspect(bank, root, depth)
        nodes.append(node)
        queue.extend((child, depth + 1) for child in node.children if child not in seen)

    return Closure(root=identity, nodes=tuple(nodes))


def clip_names(identity: str, root: str | Path) -> list[str]:
    """The clips one bank declares, in file order.

    Names carry the source index (`0:npc_run_0`), which is what the clip filter matches
    on and what a reviewer sees in the list.
    """
    path = ids.resolve(identity, root)
    if path is None or not path.is_file():
        return []
    document = glb.read_json(path)
    return [
        animation.get("name", str(position))
        for position, animation in enumerate(document.get("animations") or [])
    ]


def bone_names(document: dict) -> tuple[str, ...]:
    """Bone names in MDL order, the only key that joins a bank to a body.

    Bank and body declare their own bone tables, so indices do not correspond; a bank's
    bones are a subset of the body's, with names equal. Retargeting matches on name.
    """
    payload = seams.root_extension(document, seams.MODEL_EXTENSION) or {}
    bones = (payload.get("mdl") or {}).get("bones") or []
    return tuple(bone.get("name", "") for bone in bones)


def bone_remaps(document: dict) -> dict[str, list[dict]]:
    """Per-include bone remap tables, keyed by the included model's source path.

    This is the exporter's authoritative record of how a body's bones map onto a bank's,
    and is more trustworthy than assuming the two tables line up.
    """
    payload = seams.root_extension(document, seams.MODEL_EXTENSION) or {}
    includes = (payload.get("mdl") or {}).get("includeModels") or []
    return {
        entry.get("path", ""): entry.get("boneRemap") or []
        for entry in includes
    }
