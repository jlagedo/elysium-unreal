"""VtMB's model-header bone-chain records and the narrow AnimDynamics POC mapping.

``docs/vtmb/secondary_motion.md`` owns the retail layout and solve.  This module only decodes
the authored table and, for the two owner-selected proof bodies, resolves it into an
Unreal-native host recipe.  The mapping is intentionally named a POC: AnimDynamics is a
rigid-body constraint solver, not VtMB's point/segment Verlet solve, so copying a source float
does not make the two numerically equivalent.

Only three head-parented hair chains are admitted:

* ``malkavian_female_armor_0``: ``Bone05`` through ``Bone09``;
* ``jeanette``: ``Bone01`` through ``Bone07`` and ``Bone09`` through ``Bone13``.

The same models' breast records are excluded, as are the other Malkavian armour bodies and
Jeanette's independent renderer-cloth skirt payload.  Expanding this allow-list is an owner call,
not a side effect of exporting another model.
"""

from __future__ import annotations

import math
import struct
from dataclasses import dataclass


MDL_VERSION = 2531
H_NUM_CHAINS = 396
H_CHAIN_INDEX = 400
CHAIN_STRIDE = 28


# Exact installed-model paths and exact first moving bones.  Selecting by ``Bip01 Head`` alone
# would silently broaden the proof every time another body is exported.
ANIM_DYNAMICS_POC_CHAINS = {
    "models/character/pc/female/malkavian/armor0/malkavian_female_armor_0.mdl":
        frozenset(("bone05",)),
    "models/character/npc/unique/santa_monica/jeanette/jeanette.mdl":
        frozenset(("bone01", "bone09")),
}


@dataclass(frozen=True)
class BoneChainRecord:
    first_bone: int
    terminal_bone: int
    unnamed_field_8: float
    gravity: float
    damping: float
    spring_exponent: float
    max_angle_degrees: float


@dataclass(frozen=True)
class AnimDynamicsPocChain:
    """One complete host recipe baked into the generated skeletal mesh.

    The source record remains recoverable from the user's MDL and is deliberately not forwarded
    into the frame path.  These values are AnimDynamics settings, produced offline and labelled
    as provisional until the CAP5.5 numerical replay supplies a fitted mapping.
    """

    first_bone: str
    chain_end: str
    gravity_scale: float
    damping: float
    angular_spring: float
    cone_angle_degrees: float


def read_chain_records(blob: bytes) -> list[BoneChainRecord]:
    """Decode the ``MDLHeader +396/+400`` table, rejecting malformed declarations."""
    if len(blob) < H_CHAIN_INDEX + 4:
        raise ValueError("model image is shorter than the bone-chain header fields")
    version = struct.unpack_from("<i", blob, 4)[0]
    if version != MDL_VERSION:
        raise ValueError(f"model is version {version}, expected {MDL_VERSION}")

    count, offset = struct.unpack_from("<ii", blob, H_NUM_CHAINS)
    if count < 0:
        raise ValueError(f"bone-chain count is negative ({count})")
    if count == 0:
        return []
    if offset <= 0 or offset + count * CHAIN_STRIDE > len(blob):
        raise ValueError(
            f"{count} bone-chain record(s) at {offset} run past the {len(blob)}-byte model image"
        )

    out = []
    for index in range(count):
        at = offset + index * CHAIN_STRIDE
        first, terminal, unnamed, gravity, damping, spring, maximum = struct.unpack_from(
            "<ii5f", blob, at
        )
        values = (unnamed, gravity, damping, spring, maximum)
        if not all(math.isfinite(value) for value in values):
            raise ValueError(f"bone-chain record {index} contains a non-finite float")
        out.append(BoneChainRecord(
            first, terminal, unnamed, gravity, damping, spring, maximum
        ))
    return out


def child_walk(bones, first: int, terminal: int = -1) -> list[int]:
    """Resolve retail's ordered first-child walk, or its explicit terminal branch."""
    if not 0 <= first < len(bones):
        raise ValueError(f"first moving bone {first} is outside {len(bones)} bones")
    if terminal >= len(bones):
        raise ValueError(f"terminal bone {terminal} is outside {len(bones)} bones")

    children: dict[int, list[int]] = {}
    for bone in bones:
        children.setdefault(bone.parent, []).append(bone.index)

    walk = [first]
    visited = {first}
    while terminal < 0 or walk[-1] != terminal:
        next_bones = children.get(walk[-1], ())
        if not next_bones:
            if terminal >= 0:
                raise ValueError(
                    f"terminal bone {terminal} is not on the first-child walk from {first}"
                )
            break
        child = next_bones[0]
        if child in visited:
            raise ValueError(f"bone-chain walk from {first} contains a cycle at {child}")
        visited.add(child)
        walk.append(child)
    return walk


def anim_dynamics_poc_chains(model_path: str, blob: bytes, bones) -> list[AnimDynamicsPocChain]:
    """Resolve the owner-selected records into provisional native AnimDynamics settings."""
    normalized = model_path.replace("\\", "/").lower()
    selected = ANIM_DYNAMICS_POC_CHAINS.get(normalized)
    if selected is None:
        return []

    found: set[str] = set()
    out = []
    for record in read_chain_records(blob):
        if not 0 <= record.first_bone < len(bones):
            raise ValueError(
                f"{normalized}: first moving bone {record.first_bone} is outside the skeleton"
            )
        first = bones[record.first_bone]
        first_name = first.name.lower()
        if first_name not in selected:
            continue
        parent_name = bones[first.parent].name if 0 <= first.parent < len(bones) else ""
        if parent_name.lower() != "bip01 head":
            raise ValueError(
                f"{normalized}: selected chain {first.name} is parented to {parent_name!r}, "
                "not 'Bip01 Head'"
            )
        walk = child_walk(bones, record.first_bone, record.terminal_bone)
        if len(walk) < 2:
            raise ValueError(f"{normalized}: selected hair chain {first.name} has one bone")

        # POC mapping, not an equivalence claim.  Gravity and damping retain their authored
        # magnitudes as the first calibration hypothesis.  Retail stores the spring coefficient
        # as 10^-exponent; AnimDynamics needs a visibly useful constraint-scale constant, so the
        # native recipe starts at 4 * that coefficient.  CAP5.5 owns replacing these hypotheses
        # with fitted values from a controlled retail series.
        damping = min(1.0, max(0.7, record.damping))
        out.append(AnimDynamicsPocChain(
            first_bone=first.name,
            chain_end=bones[walk[-1]].name,
            gravity_scale=max(0.0, record.gravity),
            damping=damping,
            angular_spring=4.0 * math.pow(10.0, -record.spring_exponent),
            cone_angle_degrees=min(179.0, max(0.0, record.max_angle_degrees)),
        ))
        found.add(first_name)

    missing = sorted(selected - found)
    if missing:
        raise ValueError(
            f"{normalized}: selected AnimDynamics chain(s) are absent: {', '.join(missing)}"
        )
    return out
