"""The UP-first source closure of one model unit.

The MDL selects the unit; its VTX pair and its PHY resolve independently and may come from
different containers. The facial tables the model stem selects are **not** members: they are
`vtmb:expression-table:` units with one owner each, and inlining them here would make hundreds of
models authoritative for one file.
"""

from __future__ import annotations

from dataclasses import dataclass
import struct
from typing import Callable, Iterable

from elysium_pipeline.formats.model_glb.model import (
    SOURCE_EXTENSION,
    SOURCE_ROOT,
    ModelIdentityError,
    asset_id,
    normalize_model_key,
    source_path,
)
from elysium_pipeline.formats.unit_contract import (
    Origin,
    SourceMember,
    origin_of,
    read_member,
)

#: The MDL role, then the two topology variants in the order the engine prefers them, then the
#: optional physics companion.
MDL_ROLE = "mdl"
PRIMARY_VTX_ROLE = "vtx-dx80"
LEGACY_VTX_ROLE = "vtx-dx7-2bone"
PHY_ROLE = "phy"

#: `<stem><suffix>` for each optional companion, by role.
COMPANION_SUFFIXES = {
    PRIMARY_VTX_ROLE: ".dx80.vtx",
    LEGACY_VTX_ROLE: ".dx7_2bone.vtx",
    PHY_ROLE: ".phy",
}

#: Where each format stores the compiler checksum that binds the closure together.
MDL_CHECKSUM_OFFSET = 8
VTX_CHECKSUM_OFFSET = 16
PHY_CHECKSUM_OFFSET = 12


class ModelSourceError(RuntimeError):
    """The requested model source closure is absent or internally inconsistent."""


@dataclass(frozen=True, slots=True)
class DisagreeingVariant:
    """A VTX variant whose stored checksum is not the MDL's.

    The seam is written against what the bytes support rather than what a header claims, so the
    checksum alone does not decide: a variant that still indexes this MDL's vertex pools is
    admitted and named in `anomalies[]`, and one that does not is named there and left out of the
    closure, because decoding it would read another model's topology.
    """

    role: str
    path: str
    byte_length: int
    mdl_checksum: int
    variant_checksum: int
    admitted: bool
    reason: str


@dataclass(frozen=True, slots=True)
class ModelSourceClosure:
    """Every member one model unit was decoded from."""

    key: str
    asset: str
    mdl: SourceMember
    primary_vtx: SourceMember | None
    alternate_vtx: SourceMember | None
    phy: SourceMember | None
    disagreeing_variants: tuple[DisagreeingVariant, ...] = ()
    absent_variants: tuple[str, ...] = ()

    def members(self) -> tuple[SourceMember, ...]:
        optional = (self.primary_vtx, self.alternate_vtx, self.phy)
        return (self.mdl, *(member for member in optional if member is not None))

    @property
    def vtx_members(self) -> tuple[SourceMember, ...]:
        return tuple(
            member for member in (self.primary_vtx, self.alternate_vtx) if member is not None
        )


def _i32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<i", data, offset)[0]


def _u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


#: `StudioModel`, `StudioMesh` and `StripGroupHeader_t` strides, and the vertex-table form bits the
#: strip-group flags select.
_MODEL_STRIDE = 224
_MESH_STRIDE = 60
_STRIPGROUP_STRIDE = 20
_VERTS_ARE_BONED = 0x08
_VERTS_ARE_PLAIN = 0x10


def indexes_this_mdl(mdl_data: bytes, vtx_data: bytes) -> bool:
    """Whether a VTX image addresses this MDL's body parts, meshes and vertex pools.

    This is the question the checksum stands in for. A variant the compiler stamped from another
    revision usually fails it -- it indexes a vertex the pool does not hold -- and a variant that
    passes it decodes to this model's topology whatever the stamp says.
    """

    try:
        if len(vtx_data) < 36 or len(mdl_data) < 424:
            return False
        bodypart_count = _i32(mdl_data, 320)
        bodypart_base = _i32(mdl_data, 324)
        if _i32(vtx_data, 28) != bodypart_count:
            return False
        vtx_bodypart_base = _i32(vtx_data, 32)
        for bodypart_index in range(bodypart_count):
            bodypart = bodypart_base + bodypart_index * 16
            vtx_bodypart = vtx_bodypart_base + bodypart_index * 8
            model_count = _i32(mdl_data, bodypart + 4)
            if _i32(vtx_data, vtx_bodypart) != model_count:
                return False
            model_relative = _i32(mdl_data, bodypart + 12)
            vtx_model_relative = _i32(vtx_data, vtx_bodypart + 4)
            for model_index in range(model_count):
                model = bodypart + model_relative + model_index * _MODEL_STRIDE
                vtx_model = vtx_bodypart + vtx_model_relative + model_index * 8
                mesh_count = _i32(mdl_data, model + 136)
                mesh_base = model + _i32(mdl_data, model + 140)
                vertex_count = _i32(mdl_data, model + 144)
                lod_count = _i32(vtx_data, vtx_model)
                lod_base = vtx_model + _i32(vtx_data, vtx_model + 4)
                for lod_index in range(lod_count):
                    lod = lod_base + lod_index * 12
                    if _i32(vtx_data, lod) != mesh_count:
                        return False
                    vtx_mesh_base = lod + _i32(vtx_data, lod + 4)
                    for mesh_index in range(mesh_count):
                        vtx_mesh = vtx_mesh_base + mesh_index * 8
                        vertex_offset = _i32(
                            mdl_data, mesh_base + mesh_index * _MESH_STRIDE + 12
                        )
                        group_base = vtx_mesh + _i32(vtx_data, vtx_mesh + 4)
                        for group_index in range(_u16(vtx_data, vtx_mesh)):
                            group = group_base + group_index * _STRIPGROUP_STRIDE
                            flags = vtx_data[group + 6]
                            if flags & _VERTS_ARE_BONED:
                                stride, at = 12, 10
                            elif flags & _VERTS_ARE_PLAIN:
                                stride, at = 2, 0
                            else:
                                return False
                            table = group + _i32(vtx_data, group + 8)
                            for vertex in range(_u16(vtx_data, group)):
                                local = _u16(vtx_data, table + vertex * stride + at)
                                if not 0 <= vertex_offset + local < vertex_count:
                                    return False
        return True
    except (struct.error, IndexError):
        return False


def _checksum(data: bytes, offset: int, label: str) -> int:
    if len(data) < offset + 4:
        raise ModelSourceError(f"{label} is shorter than its checksum field at {offset}")
    return struct.unpack_from("<I", data, offset)[0]


def _member(
    index: dict,
    key: str,
    role: str,
    read_bytes: Callable[[dict, str], bytes | None] | None,
    *,
    required: bool = False,
) -> SourceMember | None:
    entry = index.get(key)
    data = read_member(index, key, read_bytes=read_bytes) if entry else None
    if data is None:
        if required:
            raise ModelSourceError(f"missing required {role}: {key}")
        return None
    origin: Origin = origin_of(entry)
    return SourceMember(role=role, path=key, data=data, origin=origin)


def source_keys(index: dict) -> list[str]:
    """Every model unit the index resolves, as keys below `models/`.

    A member under any other prefix is not a model unit: the engine composes model paths below
    `models/` and cannot reach `unpacked 0.74/shovelhead/shovelhead_short.mdl`, whichever pack
    ships it.
    """

    keys = set()
    for path in index:
        if not path.startswith(SOURCE_ROOT) or not path.endswith(SOURCE_EXTENSION):
            continue
        try:
            keys.add(normalize_model_key(path))
        except ModelIdentityError:
            continue
    return sorted(keys)


def load_source_closure(
    index: dict,
    key: str,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
) -> ModelSourceClosure:
    """Resolve every member of one model independently through the UP-first index."""

    normalized = normalize_model_key(key)
    mdl_path = source_path(normalized)
    mdl = _member(index, mdl_path, MDL_ROLE, read_bytes, required=True)
    assert mdl is not None                                  # `required` proved it
    stem = mdl_path[: -len(SOURCE_EXTENSION)]
    checksum = _checksum(mdl.data, MDL_CHECKSUM_OFFSET, mdl.path)

    admitted: dict[str, SourceMember] = {}
    disagreeing: list[DisagreeingVariant] = []
    absent: list[str] = []
    for role in (PRIMARY_VTX_ROLE, LEGACY_VTX_ROLE):
        found = _member(index, stem + COMPANION_SUFFIXES[role], role, read_bytes)
        if found is None:
            absent.append(role)
            continue
        variant = _checksum(found.data, VTX_CHECKSUM_OFFSET, found.path)
        if variant != checksum:
            coherent = indexes_this_mdl(mdl.data, found.data)
            disagreeing.append(
                DisagreeingVariant(
                    role=role,
                    path=found.path,
                    byte_length=found.byte_length,
                    mdl_checksum=checksum,
                    variant_checksum=variant,
                    admitted=coherent,
                    reason=(
                        "checksum-disagrees-with-mdl"
                        if coherent
                        else "checksum-and-topology-disagree-with-mdl"
                    ),
                )
            )
            if not coherent:
                continue
        admitted[role] = found

    primary = admitted.get(PRIMARY_VTX_ROLE) or admitted.get(LEGACY_VTX_ROLE)
    alternate = admitted.get(LEGACY_VTX_ROLE) if PRIMARY_VTX_ROLE in admitted else None

    phy = _member(index, stem + COMPANION_SUFFIXES[PHY_ROLE], PHY_ROLE, read_bytes)
    if phy is not None:
        # The PHY keeps its compiler checksum as provenance. Unlike the VTX pair it is not an
        # admission gate: `VCollideLoad` receives the solid count and the bytes after the header.
        _checksum(phy.data, PHY_CHECKSUM_OFFSET, phy.path)

    return ModelSourceClosure(
        key=normalized,
        asset=asset_id(normalized),
        mdl=mdl,
        primary_vtx=primary,
        alternate_vtx=alternate,
        phy=phy,
        disagreeing_variants=tuple(disagreeing),
        absent_variants=tuple(absent),
    )


def surface_property_names(
    index: dict,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
    path: str = "scripts/surfaceproperties.txt",
) -> frozenset[str]:
    """The surface names `scripts/surfaceproperties.txt` declares, folded to lower case.

    A model names surfaces from four places -- its header, its bones, its PHY solids and the
    `$surfaceprop` of the materials it binds -- and every one of them has to state whether the
    reference resolves. The table's entries are its depth-zero block names, which is all this seam
    needs; their content belongs to `vtmb:surface-property:`.
    """

    if path not in index:
        return frozenset()
    data = read_member(index, path, read_bytes=read_bytes)
    if data is None:
        return frozenset()
    return frozenset(_top_level_block_names(data))


def _top_level_block_names(data: bytes) -> Iterable[str]:
    text = data.decode("utf-8-sig", "replace")
    tokens: list[str] = []
    position = 0
    while position < len(text):
        character = text[position]
        if character.isspace():
            position += 1
            continue
        if text.startswith("//", position):
            end = text.find("\n", position)
            position = len(text) if end < 0 else end
            continue
        if character in "{}":
            tokens.append(character)
            position += 1
            continue
        if character == '"':
            end = text.find('"', position + 1)
            if end < 0:
                break
            tokens.append(text[position + 1:end])
            position = end + 1
            continue
        start = position
        while position < len(text) and not text[position].isspace() and text[position] not in "{}":
            position += 1
        tokens.append(text[start:position])
    depth = 0
    for position, token in enumerate(tokens):
        if token == "{":
            depth += 1
        elif token == "}":
            depth = max(0, depth - 1)
        elif depth == 0 and tokens[position + 1:position + 2] == ["{"]:
            yield token.strip().lower()
