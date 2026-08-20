"""VtMB's model-header bone-chain records and the native AnimDynamics recipes.

``docs/vtmb/secondary_motion.md`` owns the retail layout and solve.  This module only
decodes the authored table and resolves selected rows into Unreal-native host recipes.
AnimDynamics is a rigid-body constraint solver, not VtMB's point/segment Verlet solve, so
copying a source float does not make the two numerically equivalent.

Hair chains stay a two-body proof:

* ``malkavian_female_armor_0``: ``Bone05`` through ``Bone09``;
* ``jeanette``: ``Bone01`` through ``Bone07`` and ``Bone09`` through ``Bone13``.

One-bone breast rows are classified here but not written into a body and not installed at
runtime.  Head-parented one-bone curls stay hair (and out of both recipes).  Expanding the
hair allow-list is an owner call, not a side effect of exporting another model.
"""

from __future__ import annotations

import math
import struct
from dataclasses import dataclass


MDL_VERSION = 2531
H_NUM_CHAINS = 396
H_CHAIN_INDEX = 400
CHAIN_STRIDE = 28

BREAST_NAME_TOKENS = ("breast", "boob", "tit")


# Exact installed-model paths and exact first moving bones.  Selecting by ``Bip01 Head`` alone
# would silently broaden the proof every time another body is exported.
ANIM_DYNAMICS_POC_CHAINS = {
    "models/character/pc/female/malkavian/armor0/malkavian_female_armor_0.mdl":
        frozenset(("bone05",)),
    "models/character/npc/unique/santa_monica/jeanette/jeanette.mdl":
        frozenset(("bone01", "bone09")),
}

# One-bone ``Bip01 Spine*`` rows whose first bone is not a breast token.  Paths are
# install-relative and lowercase.  A new anonymous spine singleton fails export rather than
# shipping rigid; add it here or to ``REJECTED_SPINE_SINGLE_BONES``.
ANONYMOUS_BREAST_FIRST_BONES = {
    "models/character/npc/common/citizen/chinatown/female1/chinese_girl.mdl":
        frozenset(("bone13", "bone15")),
    "models/character/npc/common/dancer/female_4/female_dancer_4.mdl":
        frozenset(("bone01", "bone03")),
    "models/character/npc/common/prostitute/prostitute_1/prostitute_1.mdl":
        frozenset(("bone03", "bone05")),
    "models/character/npc/common/stripper/strippdk.mdl":
        frozenset(("bone03", "bone05")),
    "models/character/npc/common/stripper/strippdr.mdl":
        frozenset(("bone03", "bone05")),
    "models/character/npc/common/stripper/stripper.mdl":
        frozenset(("bone03", "bone05")),
    "models/character/npc/common/stripper/stripper_reduced_01.mdl":
        frozenset(("bone03", "bone05")),
    "models/character/npc/unique/downtown/damsel/damsel.mdl":
        frozenset(("bone30", "bone32")),
    "models/character/npc/unique/downtown/vv/vv.mdl":
        frozenset(("bone01", "bone03")),
    "models/character/npc/unique/hollywood/kerri/kerri.mdl":
        frozenset(("bone03", "bone05")),
    "models/character/npc/unique/hollywood/misti/misti.mdl":
        frozenset(("bone03", "bone05")),
    "models/character/npc/unique/hollywood/misti/mistidance.mdl":
        frozenset(("bone03", "bone05")),
    "models/character/npc/unique/hollywood/vvstrip/vv.mdl":
        frozenset(("bone01", "bone03")),
    "models/character/npc/unique/santa_monica/heather/heather.mdl":
        frozenset(("bone01", "bone03")),
    "models/character/npc/unique/santa_monica/heather/heather_3.mdl":
        frozenset(("bone01", "bone03")),
    "models/character/npc/unique/santa_monica/heather/heather_goth.mdl":
        frozenset(("bone01", "bone03")),
    "models/character/pc/female/gangrel/armor0/gangrel_female_armor_0.mdl":
        frozenset(("bone05", "bone07")),
    "models/character/pc/female/malkavian/armor0/malkavian_female_armor_0.mdl":
        frozenset(("bone01", "bone03")),
}

# Tzimisce creation torso flaps / ribs.  One-bone spine rows, not breasts.
REJECTED_SPINE_SINGLE_BONES = {
    "models/character/monster/tzimisce/creation1/creation1_full.mdl":
        frozenset((
            "bone05", "bone07",
            "left rib top", "right rib top",
            "left rib bottom", "right rib bottom",
        )),
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
    as provisional until a fitted mapping replaces them.
    """

    first_bone: str
    chain_end: str
    gravity_scale: float
    damping: float
    angular_spring: float
    cone_angle_degrees: float


@dataclass(frozen=True)
class AnimDynamicsPocBody:
    """One single-body AnimDynamics recipe for a one-bone breast record."""

    bound_bone: str
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


def _native_gravity_damping_spring(record: BoneChainRecord) -> tuple[float, float, float]:
    """Hair and breast share the first-calibration hypotheses; cone limits do not."""
    return (
        max(0.0, record.gravity),
        min(1.0, max(0.7, record.damping)),
        4.0 * math.pow(10.0, -record.spring_exponent),
    )


def _breast_token_in(name: str) -> bool:
    lowered = name.lower()
    return any(token in lowered for token in BREAST_NAME_TOKENS)


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

        gravity, damping, spring = _native_gravity_damping_spring(record)
        out.append(AnimDynamicsPocChain(
            first_bone=first.name,
            chain_end=bones[walk[-1]].name,
            gravity_scale=gravity,
            damping=damping,
            angular_spring=spring,
            cone_angle_degrees=min(179.0, max(0.0, record.max_angle_degrees)),
        ))
        found.add(first_name)

    missing = sorted(selected - found)
    if missing:
        raise ValueError(
            f"{normalized}: selected AnimDynamics chain(s) are absent: {', '.join(missing)}"
        )
    return out


def anim_dynamics_breast_bodies(model_path: str, blob: bytes, bones) -> list[AnimDynamicsPocBody]:
    """Resolve every one-bone breast record into a single-body AnimDynamics recipe."""
    normalized = model_path.replace("\\", "/").lower()
    out = []
    for record in read_chain_records(blob):
        if not 0 <= record.first_bone < len(bones):
            raise ValueError(
                f"{normalized}: first moving bone {record.first_bone} is outside the skeleton"
            )
        first = bones[record.first_bone]
        parent_name = bones[first.parent].name if 0 <= first.parent < len(bones) else ""
        if not parent_name.lower().startswith("bip01 spine"):
            continue
        walk = child_walk(bones, record.first_bone, record.terminal_bone)
        if len(walk) != 1:
            continue

        first_name = first.name.lower()
        if _breast_token_in(first.name):
            pass
        elif first_name in ANONYMOUS_BREAST_FIRST_BONES.get(normalized, ()):
            pass
        elif first_name in REJECTED_SPINE_SINGLE_BONES.get(normalized, ()):
            continue
        else:
            raise ValueError(
                f"{normalized}: unclassified one-bone spine chain {first.name!r}; "
                "add it to ANONYMOUS_BREAST_FIRST_BONES or REJECTED_SPINE_SINGLE_BONES"
            )

        gravity, damping, spring = _native_gravity_damping_spring(record)
        out.append(AnimDynamicsPocBody(
            bound_bone=first.name,
            gravity_scale=gravity,
            damping=damping,
            angular_spring=spring,
            cone_angle_degrees=min(90.0, max(0.0, record.max_angle_degrees)),
        ))
    return out
