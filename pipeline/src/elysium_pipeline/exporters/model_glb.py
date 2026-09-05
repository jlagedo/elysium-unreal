"""The Model GLB writer: one `models/**.mdl` becomes one binary glTF unit.

Where core glTF can carry the datum -- the skeleton, the skin, the geometry, the animations, the
morph targets, the collision hulls -- it does, and the `ELYSIUM_vtmb_model` extension carries the
VTMB-only information beside it. Nothing is stated twice: the extension names records and
identities, and the numbers a consumer draws live in the BIN chunk.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any, Callable

import numpy as np

from elysium_pipeline.formats import mdl_skel
from elysium_pipeline.formats.model_glb import (
    COORDINATE_TRANSFORM,
    MODEL_EXTENSION,
    SCHEMA_VERSION,
    ModelUnit,
    decode_model,
    load_source_closure,
    output_relative_path,
    source_keys as _source_keys,
    surface_property_names,
)
from elysium_pipeline.formats.model_glb.model import (
    KIND,
    REFERENCE_EXTENSIONS_USED,
    SOURCE_TO_GLTF_SCALE,
    plain,
)
from elysium_pipeline.formats.unit_contract import (
    MATERIAL_REFERENCE,
    MODEL_REFERENCE,
    asset_block,
    coverage_block,
    extension_root,
    identity_block,
    reference_extension,
    source_resolution,
    write_glb,
)

FLOAT = 5126
U16 = 5123
U32 = 5125
ARRAY_BUFFER = 34962
ELEMENT_ARRAY_BUFFER = 34963

#: `asset.generator`, spelled the way the unit contract requires.
GENERATOR_TITLE = "Model"

#: The extension sections a unit can represent, in publication order. `coverage.mapped` names
#: only the ones this unit's own bytes filled: a prop with no PHY companion, no facial rig and no
#: cloth does not claim to have mapped them.
MAPPED_SECTIONS = (
    "identity",
    "sourceResolution",
    "coordinateTransform",
    "mdl",
    "vtx",
    "physics",
    "materialBindings",
    "facial",
    "procedural",
    "secondaryMotion",
    "cloth",
    "dependencies",
    "anomalies",
    "omissions",
)


def _mapped_sections(extension: dict[str, Any]) -> list[str]:
    """The sections the unit actually carries, by the contract's field-or-record grading."""

    always = {"identity", "sourceResolution", "coordinateTransform", "mdl"}
    mapped = []
    for name in MAPPED_SECTIONS:
        if name in always:
            mapped.append(name)
            continue
        value = extension.get(name)
        if isinstance(value, dict):
            filled = any(bool(item) for item in value.values())
        else:
            filled = bool(value)
        if filled:
            mapped.append(name)
    return mapped


class ModelGlbError(RuntimeError):
    """The complete model unit could not be represented as a valid GLB."""


def source_keys(index: dict) -> list[str]:
    """Every model unit key the UP-first install index resolves."""

    return _source_keys(index)


def _position(value) -> tuple[float, float, float]:
    return (
        float(value[0]) * SOURCE_TO_GLTF_SCALE,
        float(value[2]) * SOURCE_TO_GLTF_SCALE,
        -float(value[1]) * SOURCE_TO_GLTF_SCALE,
    )


def _direction(value) -> tuple[float, float, float]:
    return (float(value[0]), float(value[2]), -float(value[1]))


def _quaternion(value) -> tuple[float, float, float, float]:
    """The source rotation in glTF axes, normalised; a degenerate one becomes the identity.

    A bone whose stored quaternion does not normalise is listed in `anomalies[] degenerate-bind`
    by the decode, so the substitution here is recorded rather than silent.
    """

    converted = np.asarray(
        (float(value[0]), float(value[2]), -float(value[1]), float(value[3])), dtype=np.float64
    )
    length = float(np.linalg.norm(converted))
    if not np.isfinite(length) or length <= 1e-12:
        return (0.0, 0.0, 0.0, 1.0)
    converted /= length
    return tuple(float(component) for component in converted)


#: Why a record still carries a stored bind value the joint node would otherwise have said.
STORED_TRANSFORM_NOTE = (
    "the node built from this record does not recover the stored value exactly"
)


def _position_round_trips(value) -> bool:
    """Whether the node translation this position built recovers the stored float32 triple."""

    node = _position(value)
    recovered = (
        node[0] / SOURCE_TO_GLTF_SCALE,
        -node[2] / SOURCE_TO_GLTF_SCALE,
        node[1] / SOURCE_TO_GLTF_SCALE,
    )
    return all(
        np.float32(candidate) == np.float32(stored)
        for candidate, stored in zip(recovered, value)
    )


def _rotation_round_trips(value) -> bool:
    """Whether the node rotation recovers the stored quaternion, which a non-unit one does not."""

    node = _quaternion(value)
    recovered = (node[0], -node[2], node[1], node[3])
    return all(
        np.float32(candidate) == np.float32(stored)
        for candidate, stored in zip(recovered, value)
    )


def _without_bind(
    row: dict[str, Any], position_key: str | None, rotation_key: str | None
) -> dict[str, Any]:
    """One record without the bind transform its glTF node already carries.

    `seam_map_model.md` § Core content gives the bind locals to the joint, attachment and eye
    nodes, so restating them here would state one datum twice. A stored value the node cannot
    give back -- a quaternion the writer had to normalise -- stays, and says why.
    """

    published = {
        key: value
        for key, value in row.items()
        if key not in {position_key, rotation_key}
    }
    kept: dict[str, Any] = {}
    if position_key and not _position_round_trips(row[position_key]):
        kept["storedPosition"] = row[position_key]
    if rotation_key and not _rotation_round_trips(row[rotation_key]):
        kept["storedRotation"] = row[rotation_key]
    if kept:
        published.update(kept)
        published["storedTransformNote"] = STORED_TRANSFORM_NOTE
    return published


def _published_body_parts(unit: ModelUnit) -> list[dict[str, Any]]:
    """The body parts with each eyeball's bind origin left to its own node."""

    return [
        {
            **part,
            "models": [
                {
                    **body_model,
                    "eyeballs": [
                        _without_bind(eyeball, "org", None)
                        for eyeball in body_model["eyeballs"]
                    ],
                }
                for body_model in part["models"]
            ],
        }
        for part in unit.body_parts
    ]


def _matrix(quaternion, translation) -> np.ndarray:
    x, y, z, w = quaternion
    result = np.eye(4, dtype=np.float64)
    result[:3, :3] = (
        (1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)),
        (2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)),
        (2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)),
    )
    result[:3, 3] = translation
    return result


class GlbBuilder:
    """The BIN chunk and the accessors that read it, four-byte aligned throughout."""

    def __init__(self) -> None:
        self.binary = bytearray()
        self.buffer_views: list[dict[str, Any]] = []
        self.accessors: list[dict[str, Any]] = []

    def view(self, data: bytes, *, target: int | None = None) -> int:
        while len(self.binary) % 4:
            self.binary.append(0)
        offset = len(self.binary)
        self.binary.extend(data)
        view: dict[str, Any] = {"buffer": 0, "byteOffset": offset, "byteLength": len(data)}
        if target is not None:
            view["target"] = target
        self.buffer_views.append(view)
        return len(self.buffer_views) - 1

    def accessor(
        self,
        values,
        component_type: int,
        shape: str,
        *,
        target: int | None = None,
        bounds: bool = False,
    ) -> int:
        array = np.ascontiguousarray(values)
        if array.ndim == 0:
            array = array.reshape(1)
        accessor: dict[str, Any] = {
            "bufferView": self.view(array.tobytes(), target=target),
            "componentType": component_type,
            "count": int(array.shape[0]),
            "type": shape,
        }
        if bounds and array.shape[0]:
            accessor["min"] = np.asarray(array).min(axis=0).reshape(-1).tolist()
            accessor["max"] = np.asarray(array).max(axis=0).reshape(-1).tolist()
        self.accessors.append(accessor)
        return len(self.accessors) - 1

    def precise(self, values):
        from elysium_pipeline.formats.unit_contract.precision import encode
        values = np.ascontiguousarray(values, dtype="<f8")
        return encode(self.view, values.tobytes(), values.shape)


def _skeleton(
    builder: GlbBuilder, unit: ModelUnit
) -> tuple[list[dict[str, Any]], list[int], dict[str, Any] | None]:
    """The joint nodes in MDL bone order, then attachments, then eyeballs."""

    nodes: list[dict[str, Any]] = []
    roots: list[int] = []
    globals_: list[np.ndarray | None] = [None] * len(unit.bones)
    for bone in unit.bones:
        nodes.append(
            {
                "name": bone["name"],
                "translation": list(_position(bone["position"])),
                "rotation": list(_quaternion(bone["rotation"])),
            }
        )
    for bone in unit.bones:
        index = bone["index"]
        local = _matrix(_quaternion(bone["rotation"]), _position(bone["position"]))
        parent = bone["parent"]
        if parent < 0:
            roots.append(index)
            globals_[index] = local
        else:
            if not 0 <= parent < len(nodes) or globals_[parent] is None:
                raise ModelGlbError(f"bone {bone['name']} has unresolved parent {parent}")
            nodes[parent].setdefault("children", []).append(index)
            globals_[index] = globals_[parent] @ local

    skin: dict[str, Any] | None = None
    if unit.bones:
        inverse = np.asarray(
            [np.linalg.inv(matrix).T.reshape(16) for matrix in globals_], dtype=np.float32
        )
        skin = {
            "inverseBindMatrices": builder.accessor(inverse, FLOAT, "MAT4"),
            "joints": list(range(len(unit.bones))),
        }
        if len(roots) == 1:
            skin["skeleton"] = roots[0]

    for attachment in unit.attachments:
        bone = int(attachment["bone"])
        if not 0 <= bone < len(unit.bones):
            raise ModelGlbError(f"attachment {attachment.get('name')!r} names bone {bone}")
        node_index = len(nodes)
        nodes.append(
            {
                "name": attachment["name"],
                "translation": list(_position(attachment["pos"])),
                "rotation": list(_quaternion(attachment["quat"])),
                "extensions": {
                    MODEL_EXTENSION: {
                        "attachmentIndex": attachment.get("index", node_index),
                        "flags": attachment.get("flags", 0),
                        "bone": bone,
                    }
                },
            }
        )
        nodes[bone].setdefault("children", []).append(node_index)

    for bodypart in unit.body_parts:
        for body_model in bodypart["models"]:
            for eyeball in body_model["eyeballs"]:
                bone = int(eyeball["bone"])
                if not 0 <= bone < len(unit.bones):
                    raise ModelGlbError(f"eyeball {eyeball.get('name')!r} names bone {bone}")
                node_index = len(nodes)
                nodes.append(
                    {
                        "name": eyeball.get("name") or f"eye-{eyeball.get('index', node_index)}",
                        "translation": list(_position(eyeball["org"])),
                        "extensions": {
                            MODEL_EXTENSION: {
                                "eyeballIndex": eyeball.get("index"),
                                "bone": bone,
                                "radius": eyeball.get("radius"),
                                "irisScale": eyeball.get("iris_scale"),
                            }
                        },
                    }
                )
                nodes[bone].setdefault("children", []).append(node_index)

    scene_roots = list(roots)
    if len(roots) > 1:
        # Several parentless bones need one addressable root, and no bone transform is invented:
        # the common root is the identity.
        common = len(nodes)
        nodes.append({"name": f"{unit.asset}:root", "children": list(roots)})
        scene_roots = [common]
    return nodes, scene_roots, skin


def _material_index(unit: ModelUnit) -> tuple[list[dict[str, Any]], dict[str, int]]:
    """One neutral core material per distinct material identity, in first-use order."""

    materials: list[dict[str, Any]] = []
    by_id: dict[str, int] = {}
    for row in unit.materials:
        identifier = row["material"]
        if identifier in by_id:
            continue
        by_id[identifier] = len(materials)
        materials.append(
            {
                "name": row["sourceName"],
                "pbrMetallicRoughness": {
                    "baseColorFactor": [0.6, 0.6, 0.62, 1.0],
                    "metallicFactor": 0.0,
                    "roughnessFactor": 1.0,
                },
                "doubleSided": True,
                **reference_extension(MATERIAL_REFERENCE, identifier),
            }
        )
    return materials, by_id


def _primitive_material(unit: ModelUnit, skin_reference: int) -> str:
    """The material identity one section binds, through skin family 0 where the model has one."""

    if unit.skin_families and 0 <= skin_reference < len(unit.skin_families[0]):
        return unit.skin_families[0][skin_reference]
    if 0 <= skin_reference < len(unit.materials):
        return unit.materials[skin_reference]["material"]
    raise ModelGlbError(f"primitive names skin reference {skin_reference} with no material")


def _meshes(
    builder: GlbBuilder, unit: ModelUnit, material_ids: dict[str, int]
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    meshes: list[dict[str, Any]] = []
    lod_rows: list[dict[str, Any]] = []
    morph_names = [row["name"] for row in unit.facial.get("morphTargets") or []]
    skinned = bool(unit.bones)
    for lod in unit.lods:
        primitives = []
        for source in lod["primitives"]:
            positions = np.asarray(
                [_position(value) for value in source["positions"]], np.float32
            )
            normals = np.asarray([_direction(value) for value in source["normals"]], np.float32)
            uvs = np.asarray(source["uvs"], np.float32)
            triangles = np.asarray(source["triangles"], np.uint32).reshape(-1)
            material_id = _primitive_material(unit, source["skinReference"])
            if material_id not in material_ids:
                raise ModelGlbError(f"primitive material {material_id!r} is not declared")
            attributes: dict[str, Any] = {
                "POSITION": builder.accessor(
                    positions, FLOAT, "VEC3", target=ARRAY_BUFFER, bounds=True
                ),
                "NORMAL": builder.accessor(normals, FLOAT, "VEC3", target=ARRAY_BUFFER),
                "TEXCOORD_0": builder.accessor(uvs, FLOAT, "VEC2", target=ARRAY_BUFFER),
            }
            if skinned:
                attributes["JOINTS_0"] = builder.accessor(
                    np.asarray(source["joints"], np.uint16), U16, "VEC4", target=ARRAY_BUFFER
                )
                attributes["WEIGHTS_0"] = builder.accessor(
                    np.asarray(source["weights"], np.float32), FLOAT, "VEC4",
                    target=ARRAY_BUFFER,
                )
            if source.get("tangents"):
                tangents = np.asarray(
                    [(*_direction(value[:3]), float(value[3])) for value in source["tangents"]],
                    np.float32,
                )
                attributes["TANGENT"] = builder.accessor(
                    tangents, FLOAT, "VEC4", target=ARRAY_BUFFER
                )
            primitive: dict[str, Any] = {
                "attributes": attributes,
                "indices": builder.accessor(
                    triangles, U32, "SCALAR", target=ELEMENT_ARRAY_BUFFER
                ),
                "material": material_ids[material_id],
                "extensions": {
                    MATERIAL_REFERENCE: {"asset": material_id},
                    MODEL_EXTENSION: {
                        "bodyPart": source["bodyPart"],
                        "model": source["model"],
                        "mesh": source["mesh"],
                        "skinReference": source["skinReference"],
                        "sourceVertices": source["sourceVertices"],
                        "stripGroups": source.get("stripGroups", []),
                        "morphRecords": source.get("morphRecords", []),
                        "sourcePositions": builder.precise(np.asarray(source["positions"]).reshape(-1, 3)),
                        "sourceNormals": builder.precise(np.asarray(source["sourceNormals"]).reshape(-1, 3)),
                    },
                },
            }
            morphs = source.get("morphTargets")
            if morphs:
                primitive["targets"] = [
                    {
                        "POSITION": builder.accessor(
                            np.asarray(
                                [_position(value) for value in target["position"]], np.float32
                            ),
                            FLOAT,
                            "VEC3",
                        ),
                        "NORMAL": builder.accessor(
                            np.asarray(
                                [_direction(value) for value in target["normal"]], np.float32
                            ),
                            FLOAT,
                            "VEC3",
                        ),
                    }
                    for target in morphs
                ]
            primitives.append(primitive)
        if not primitives:
            continue
        mesh: dict[str, Any] = {
            "name": f"{unit.key}:lod{lod['index']}",
            "primitives": primitives,
        }
        if lod["index"] == 0 and morph_names:
            mesh["weights"] = [0.0] * len(morph_names)
            mesh["extras"] = {"targetNames": morph_names}
        lod_rows.append(
            {
                "index": lod["index"],
                "mesh": len(meshes),
                "switchPoints": lod["switchPoints"],
                "primitiveCount": len(primitives),
            }
        )
        meshes.append(mesh)
    return meshes, lod_rows


def _animations(
    builder: GlbBuilder, unit: ModelUnit
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    """One `animations` entry per MDL-local animation, sampled from the source frames."""

    if not unit.local_animations:
        return [], []
    bone_records = mdl_skel.read_bones(unit.mdl_data)
    animations: list[dict[str, Any]] = []
    rows: list[dict[str, Any]] = []
    for row in unit.local_animations:
        frame_count = int(row["frameCount"])
        weights = row["boneWeights"]
        contributing = [bone for bone in bone_records if weights[bone.index] != 0.0]
        published = dict(row)
        if not contributing or frame_count <= 0:
            # A bone that carries no weight in this animation contributes no channel, so an
            # animation nothing weights carries none at all and no glTF object is built for it.
            published["animation"] = None
            published["channelsOmitted"] = "no-bone-carries-weight-in-this-animation"
            rows.append(published)
            continue
        frames = mdl_skel.read_anim(
            unit.mdl_data, bone_records, row["sourceOffset"], frame_count
        )
        published["sourceSamples"] = builder.precise([
            [(*position, *rotation) for position, rotation in frame] for frame in frames])
        times = np.arange(frame_count, dtype=np.float32) / float(row["fps"] or 30.0)
        time_accessor = builder.accessor(times, FLOAT, "SCALAR", bounds=True)
        channels: list[dict[str, Any]] = []
        samplers: list[dict[str, Any]] = []
        for bone in contributing:
            translations = np.asarray(
                [_position(frames[frame][bone.index][0]) for frame in range(frame_count)],
                np.float32,
            )
            rotations = np.asarray(
                [_quaternion(frames[frame][bone.index][1]) for frame in range(frame_count)],
                np.float32,
            )
            for path, values, shape in (
                ("translation", translations, "VEC3"),
                ("rotation", rotations, "VEC4"),
            ):
                samplers.append(
                    {
                        "input": time_accessor,
                        "output": builder.accessor(values, FLOAT, shape),
                        "interpolation": "LINEAR",
                    }
                )
                channels.append(
                    {
                        "sampler": len(samplers) - 1,
                        "target": {"node": bone.index, "path": path},
                    }
                )
        published["animation"] = len(animations)
        animations.append(
            {"name": f"{row['index']}:{row['name']}", "channels": channels, "samplers": samplers}
        )
        rows.append(published)
    return animations, rows


def _physics_accessors(builder: GlbBuilder, unit: ModelUnit) -> dict[str, Any] | None:
    if unit.physics is None:
        return None
    result = plain(unit.physics)
    for solid in result["solids"]:
        for hull in solid["hulls"]:
            vertices = np.asarray(hull.pop("vertices"), np.float32)
            triangles = np.asarray(hull.pop("triangles"), np.uint32).reshape(-1)
            hull["positions"] = builder.accessor(vertices, FLOAT, "VEC3", bounds=True)
            hull["indices"] = builder.accessor(triangles, U32, "SCALAR")
    return result


def build_document(unit: ModelUnit) -> tuple[dict[str, Any], bytes]:
    """Build the complete glTF JSON document and BIN chunk from one decoded unit."""

    builder = GlbBuilder()
    nodes, roots, skin = _skeleton(builder, unit)
    materials, material_ids = _material_index(unit)
    meshes, lod_rows = _meshes(builder, unit, material_ids)
    animations, animation_rows = _animations(builder, unit)
    physics = _physics_accessors(builder, unit)

    scene_nodes = list(roots)
    if meshes:
        mesh_node: dict[str, Any] = {"name": unit.asset, "mesh": 0}
        if skin is not None:
            mesh_node["skin"] = 0
        scene_nodes.append(len(nodes))
        nodes.append(mesh_node)
    # `models/null.mdl` and its sibling declare a body part with no bone and no triangle: there is
    # nothing a general consumer can draw, so the unit is scene-less and the extension carries the
    # whole of it.
    sceneless = not nodes and not meshes

    sections: dict[str, Any] = {
        "coordinateTransform": COORDINATE_TRANSFORM,
        "mdl": {
            "header": unit.header,
            "bones": [_without_bind(bone, "position", "rotation") for bone in unit.bones],
            "splitRotationBones": unit.split_rotation_bones,
            "localAnimations": animation_rows,
            "sequences": unit.sequences,
            "poseParameters": unit.pose_parameters,
            "attachments": [
                _without_bind(attachment, "pos", "quat") for attachment in unit.attachments
            ],
            "hitboxSets": unit.hitbox_sets,
            "ikChains": unit.ik_chains,
            "boneControllers": unit.bone_controllers,
            "transitionGraph": unit.transition_graph,
            "includeModels": [
                {
                    **row,
                    **reference_extension(MODEL_REFERENCE, row["asset"]),
                }
                for row in unit.include_models
            ],
            "sequenceGroups": unit.sequence_groups,
            "textures": unit.textures,
            "searchPaths": unit.search_paths,
            "skinTable": unit.skin_table,
            "bodyParts": _published_body_parts(unit),
            "keyValues": unit.key_values,
        },
        "vtx": {
            "variants": unit.vtx_variants,
            "comparison": unit.vtx_comparison,
            "lods": lod_rows,
        },
        "physics": physics,
        "materialBindings": {"slots": unit.materials, "skinFamilies": unit.skin_families},
        "facial": unit.facial,
        "procedural": unit.procedural,
        "secondaryMotion": unit.secondary_motion,
        "cloth": unit.cloth,
        "anomalies": unit.anomalies,
        "omissions": unit.omissions,
    }
    extension = extension_root(
        schema_version=SCHEMA_VERSION,
        identity=identity_block(
            unit.asset,
            unit.source_paths,
            modelPath=unit.members[0].path,
            family=unit.family,
            shape=unit.shape,
            roles=list(unit.roles),
        ),
        source_resolution=source_resolution(unit.members),
        dependencies=unit.dependencies,
        coverage=coverage_block(
            mapped=_mapped_sections({**sections, "dependencies": unit.dependencies}),
            typed_unidentified=unit.typed_unidentified,
            omitted_proven=unit.omitted_proven,
            byte_ledger=unit.byte_ledger,
            unresolved=unit.unresolved,
            unsupported=unit.unsupported,
        ),
        **sections,
    )

    # A cross-reference extension is declared where it is used: a prop that binds no material and
    # includes no bank declares neither.
    used = {MATERIAL_REFERENCE: bool(materials), MODEL_REFERENCE: bool(unit.include_models)}
    declared = [name for name in REFERENCE_EXTENSIONS_USED if used[name]]
    document: dict[str, Any] = {
        "asset": asset_block(GENERATOR_TITLE),
        "extensionsUsed": [*declared, MODEL_EXTENSION],
        "extensionsRequired": [*declared, MODEL_EXTENSION],
    }
    if not sceneless:
        document["scene"] = 0
        document["scenes"] = [{"nodes": scene_nodes}]
        document["nodes"] = nodes
        if skin is not None:
            document["skins"] = [skin]
        if meshes:
            document["meshes"] = meshes
        if animations:
            document["animations"] = animations
    if materials:
        document["materials"] = materials
    document["extensions"] = {MODEL_EXTENSION: extension}
    if builder.accessors:
        document["accessors"] = builder.accessors
        document["bufferViews"] = builder.buffer_views
        document["buffers"] = [{"byteLength": len(builder.binary)}]
    return plain(document), bytes(builder.binary)


def export(
    index: dict,
    key: str,
    output_root: Path,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
    anorms: list[tuple[float, float, float]] | None = None,
    surface_properties: frozenset[str] | None = None,
) -> Path:
    """Resolve, decode, validate and write one model unit below `output_root`.

    `anorms` and `surface_properties` let a corpus run read each shared source once; on their own
    the call reads both itself, so one unit costs one command.
    """

    closure = load_source_closure(index, key, read_bytes=read_bytes)
    if anorms is None:
        anorms = mdl_skel.load_anorms()
    if surface_properties is None:
        surface_properties = surface_property_names(index, read_bytes=read_bytes)
    unit = decode_model(
        closure,
        index,
        read_bytes=read_bytes,
        anorms=anorms,
        surface_properties=surface_properties,
    )
    document, binary = build_document(unit)
    from elysium_pipeline.validation import model_glb as validation

    validation.validate_document(document, binary, source_members=closure.members())
    destination = output_root / Path(*output_relative_path(closure.key).parts)
    write_glb(document, binary, destination)
    return destination


__all__ = ["ModelGlbError", "KIND", "build_document", "export", "plain", "source_keys"]
