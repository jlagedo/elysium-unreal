"""Animation-bank closure.

A character body carries only its own clips and names its animation banks by identity.
That name is not the whole story: banks include other banks, and several of the ones a
body names directly are include-stubs holding no clips at all. blood_doll declares one
bank; the clips actually reachable from it live across sixteen files.

The closure is therefore a graph walk, and it is expensive: across the 285 bodies that
declare banks the median closure is 35 files, 1782 clips and 275 MB. Nothing here loads
a clip. The walk exists so a caller can show the closure and let a reviewer choose,
because importing it wholesale would take minutes and bury bpy.data.actions.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from . import glb, ids, seams

BANK_ROLE = "animation-bank"


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
    payload = seams.root_extension(document, seams.CHARACTER_EXTENSION) or {}
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
    """Walk every bank reachable from a loaded character document.

    Breadth-first so a node's recorded depth is its shortest distance from the body.
    Visited identities are skipped, which makes the walk safe on the cycles the include
    graph contains.
    """
    root = Path(root)
    payload = seams.root_extension(document, seams.CHARACTER_EXTENSION) or {}
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


def bone_names(document: dict) -> tuple[str, ...]:
    """Bone names in MDL order, the only key that joins a bank to a body.

    Bank and body declare their own bone tables, so indices do not correspond; a bank's
    bones are a subset of the body's, with names equal. Retargeting matches on name.
    """
    payload = seams.root_extension(document, seams.CHARACTER_EXTENSION) or {}
    bones = (payload.get("mdl") or {}).get("bones") or []
    return tuple(bone.get("name", "") for bone in bones)


def bone_remaps(document: dict) -> dict[str, list[dict]]:
    """Per-include bone remap tables, keyed by the included model's source path.

    This is the exporter's authoritative record of how a body's bones map onto a bank's,
    and is more trustworthy than assuming the two tables line up.
    """
    payload = seams.root_extension(document, seams.CHARACTER_EXTENSION) or {}
    includes = ((payload.get("mdl") or {}).get("header") or {}).get("includeModels") or []
    return {
        entry.get("path", ""): entry.get("boneRemap") or []
        for entry in includes
    }
