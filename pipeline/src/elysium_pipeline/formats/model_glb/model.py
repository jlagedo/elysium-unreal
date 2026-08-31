"""Identity and semantic records for one VtMB studio model unit.

One `models/**.mdl` is one unit whatever it holds: a character body, a shared animation bank, a
one-bone scenery prop, a wield model or a view model all decode through the same records with the
sections their bytes do not fill left empty. `seam_map_model.md` owns the rules; this module owns
the identity arithmetic and the record shapes the decoder fills and the writer reads.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import PurePosixPath
from typing import Any

import numpy as np

from elysium_pipeline.formats.unit_contract import (
    MATERIAL_REFERENCE,
    MODEL_REFERENCE,
    SourceMember,
    asset_id as unit_asset_id,
    extension_name,
    normalize_key,
    plain as contract_plain,
)

#: The unit kind, its extension and the schema the extension declares.
KIND = "model"
MODEL_EXTENSION = extension_name(KIND)
SCHEMA_VERSION = "2.0.0"

#: The install root a unit key is relative to. A published unit is written at the key itself
#: below the seam's own output root, so the seam states this prefix once.
SOURCE_ROOT = "models/"
SOURCE_EXTENSION = ".mdl"

#: `ELYSIUM_material_reference` binds a primitive's material slot; `ELYSIUM_model_reference` binds
#: an include-model row to the bank it names. Both are declared only where the unit used them.
REFERENCE_EXTENSIONS_USED = (MATERIAL_REFERENCE, MODEL_REFERENCE)

#: The animation-event codes whose `options` string names a sound, and those whose `options`
#: names a particle system. Both the decoder and the validator read them from here, so the rule
#: that every named reference is declared is stated once.
SOUND_EVENTS = frozenset({1004, 1005, 1008, 2005, 4020, 5004, 5005, *range(4150, 4156)})
PARTICLE_EVENTS = frozenset({5103, *range(5111, 5120)})

#: `identity.shape`, classified from the bytes rather than from the path.
SHAPE_BANK = "bank"
SHAPE_STATIC = "static"
SHAPE_SKELETAL = "skeletal"
SHAPES = (SHAPE_BANK, SHAPE_STATIC, SHAPE_SKELETAL)

#: `identity.roles`, the closed vocabulary of what another unit uses a model *as*
#: (`seam_map_model.md`, "identity"). The field is empty at export and filled by the corpus index
#: from the inverse of every `model` reference in the corpus.
ROLE_CHARACTER_BODY = "character-body"
ROLE_ANIMATION_BANK = "animation-bank"
ROLE_WIELD = "wield"
ROLE_VIEW_MODEL = "view-model"
ROLE_GROUND_ITEM = "ground-item"
ROLE_PLACED_PROP = "placed-prop"
ROLE_STATIC_PROP = "static-prop"
ROLE_INCLUDE_ONLY = "include-only"
ROLES = (
    ROLE_CHARACTER_BODY,
    ROLE_ANIMATION_BANK,
    ROLE_WIELD,
    ROLE_VIEW_MODEL,
    ROLE_GROUND_ITEM,
    ROLE_PLACED_PROP,
    ROLE_STATIC_PROP,
    ROLE_INCLUDE_ONLY,
)

#: `STUDIOHDR_FLAGS_STATIC_PROP` in the v2531 header's `Flags`@228.
STATIC_PROP_FLAG = 0x10

#: The bone flag that splits rotation and translation inheritance
#: (`docs/vtmb/animation_and_movers.md` §A.4a).
SPLIT_ROTATION_FLAG = 0x2

SOURCE_TO_GLTF_SCALE = 0.0254

#: The transform every spatial domain of the unit declares, per the unit contract. Physics stays in
#: IVP metres and cloth stays in source inches, so both say so rather than being silently rescaled.
COORDINATE_TRANSFORM: dict[str, Any] = {
    "source": "Source inches, Z-up, right-handed",
    "destination": "glTF metres, Y-up, right-handed",
    "scale": SOURCE_TO_GLTF_SCALE,
    "position": "(x, y, z)_gltf = (x, z, -y)_source * 0.0254",
    "direction": "(x, y, z)_gltf = (x, z, -y)_source",
    "quaternion": "(x, y, z, w)_gltf = (x, z, -y, w)_source",
    "domains": {
        "mesh": "position-and-scale",
        "skeleton": "position-and-scale",
        "animation": "position-and-scale",
        "morph": "position-and-scale",
        "attachments": "position-and-scale",
        "eyes": "position-and-scale",
        "physics": "IVP metres, axis-only",
        "cloth": "source inches in extension records",
    },
}


class ModelIdentityError(ValueError):
    """A path does not name a model unit of this seam."""


def normalize_model_key(path: str) -> str:
    """The unit key: lower-cased, forward-slashed and relative to `models/`, without `.mdl`.

    The argument tolerates the root prefix and the source extension, because the singular command
    accepts `models/scenery/misc/trashcan01.mdl`, `scenery/misc/trashcan01.mdl` and
    `scenery/misc/trashcan01` for one unit.
    """

    normalized = normalize_key(path).strip("/")
    if normalized.startswith(SOURCE_ROOT):
        normalized = normalized[len(SOURCE_ROOT):]
    if normalized.endswith(SOURCE_EXTENSION):
        normalized = normalized[: -len(SOURCE_EXTENSION)]
    if not normalized or normalized.startswith("../") or "/../" in normalized:
        raise ModelIdentityError(f"invalid model path {path!r}")
    return normalized


def source_path(key: str) -> str:
    """The install-relative member the key selects."""

    return SOURCE_ROOT + normalize_model_key(key) + SOURCE_EXTENSION


def asset_id(key: str) -> str:
    return unit_asset_id(KIND, normalize_model_key(key))


def output_relative_path(key: str) -> PurePosixPath:
    """Where the unit is written below the seam's family directory."""

    return PurePosixPath(normalize_model_key(key) + ".glb")


def family_of(key: str) -> str:
    """`identity.family`: the first path segment below `models/`.

    Two members sit directly below `models/` -- `null` and `w_null` -- and so have no segment
    above the file; they publish an empty family rather than borrowing their own file name.
    """

    normalized = normalize_model_key(key)
    head, separator, _rest = normalized.partition("/")
    return head if separator else ""


def shape_of(*, body_part_count: int, bone_count: int, local_animation_count: int,
             flags: int) -> str:
    """`identity.shape`, decided from the bytes in the order the seam states.

    Zero body parts is a bank; the static-prop flag or a single bone with no local animation is a
    static unit; everything else is skeletal.
    """

    if body_part_count <= 0:
        return SHAPE_BANK
    if int(flags) & STATIC_PROP_FLAG:
        return SHAPE_STATIC
    if bone_count == 1 and local_animation_count == 0:
        return SHAPE_STATIC
    return SHAPE_SKELETAL


@dataclass(frozen=True, slots=True)
class ModelUnit:
    """One decoded model, in the shape the writer publishes it.

    `mdl_data` is the selecting member's bytes the decode already holds. The animation sampler
    accessors are built from them at write time rather than materialised here, because a body's
    hundred local animations expand to hundreds of megabytes of Python floats when they are all
    resident at once. Nothing of the expansion reaches the product: the ledger is what accounts
    for those bytes.
    """

    key: str
    asset: str
    family: str
    shape: str
    roles: list[str]
    members: tuple[SourceMember, ...]
    mdl_data: bytes
    header: dict[str, Any]
    bones: list[dict[str, Any]]
    split_rotation_bones: list[dict[str, Any]]
    local_animations: list[dict[str, Any]]
    sequences: list[dict[str, Any]]
    pose_parameters: list[dict[str, Any]]
    attachments: list[dict[str, Any]]
    hitbox_sets: list[dict[str, Any]]
    ik_chains: list[dict[str, Any]]
    bone_controllers: list[dict[str, Any]]
    transition_graph: dict[str, Any]
    include_models: list[dict[str, Any]]
    sequence_groups: list[dict[str, Any]]
    textures: list[dict[str, Any]]
    search_paths: list[str]
    skin_table: list[list[int]]
    body_parts: list[dict[str, Any]]
    key_values: dict[str, Any] | None
    lods: list[dict[str, Any]]
    vtx_variants: list[dict[str, Any]]
    vtx_comparison: dict[str, Any]
    materials: list[dict[str, Any]]
    skin_families: list[list[str]]
    facial: dict[str, Any]
    procedural: dict[str, Any]
    secondary_motion: list[dict[str, Any]]
    cloth: dict[str, Any]
    physics: dict[str, Any] | None
    dependencies: list[dict[str, Any]]
    anomalies: list[dict[str, Any]]
    omissions: list[dict[str, Any]]
    byte_ledger: list[dict[str, Any]]
    typed_unidentified: list[dict[str, Any]] = field(default_factory=list)
    omitted_proven: list[dict[str, Any]] = field(default_factory=list)
    unresolved: list[dict[str, Any]] = field(default_factory=list)
    unsupported: list[dict[str, Any]] = field(default_factory=list)

    @property
    def source_paths(self) -> list[str]:
        return [member.path for member in self.members]


def plain(value: Any) -> Any:
    """The contract's JSON coercion over the record types the MDL decoders answer with.

    `formats/mdl_skel.py` and `formats/mdl_cloth.py` return named tuples and NumPy scalars; the
    contract's own coercion turns a named tuple into an array and cannot name a NumPy float, so
    both are resolved here before delegating the rest to it.
    """

    if isinstance(value, np.generic):
        return value.item()
    if isinstance(value, np.ndarray):
        return plain(value.tolist())
    if hasattr(value, "_asdict"):
        return {str(key): plain(item) for key, item in value._asdict().items()}
    if isinstance(value, dict):
        return {str(key): plain(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [plain(item) for item in value]
    return contract_plain(value)
