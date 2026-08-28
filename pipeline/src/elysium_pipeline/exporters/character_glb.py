"""Isolated full-slice Character GLB Exporter.

This product is independent of the ESKM character bake. It resolves one character's
direct patch-first source closure, builds an exporter-owned semantic model, and writes
standard glTF 2.0 core data plus the required ``ELYSIUM_vtmb_character`` extension.
"""

from __future__ import annotations

import json
import math
import os
from pathlib import Path
import struct
from typing import Any

import numpy as np

from elysium_pipeline.formats import mdl_skel
from elysium_pipeline.formats.character_glb import decode_character, load_source_closure
from elysium_pipeline.formats.character_glb.model import (
    CHARACTER_EXTENSION,
    MATERIAL_EXTENSION,
    SCHEMA_VERSION,
    CharacterModel,
    output_relative_path,
    plain,
)


FLOAT = 5126
U16 = 5123
U32 = 5125
ARRAY_BUFFER = 34962
ELEMENT_ARRAY_BUFFER = 34963
SOURCE_TO_GLTF_SCALE = 0.0254
COORDINATE_TRANSFORM = {
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


class CharacterGlbError(RuntimeError):
    """The complete character unit could not be represented as a valid GLB."""


def _position(value) -> tuple[float, float, float]:
    return (
        float(value[0]) * SOURCE_TO_GLTF_SCALE,
        float(value[2]) * SOURCE_TO_GLTF_SCALE,
        -float(value[1]) * SOURCE_TO_GLTF_SCALE,
    )


def _direction(value) -> tuple[float, float, float]:
    return (float(value[0]), float(value[2]), -float(value[1]))


def _quaternion(value) -> tuple[float, float, float, float]:
    converted = np.asarray(
        (float(value[0]), float(value[2]), -float(value[1]), float(value[3])),
        dtype=np.float64,
    )
    length = np.linalg.norm(converted)
    if not np.isfinite(length) or length <= 1e-12:
        raise CharacterGlbError(f"invalid quaternion {tuple(value)!r}")
    converted /= length
    return tuple(float(component) for component in converted)


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
    """Small self-contained GLB writer; no existing Elysium GLB writer is imported."""

    def __init__(self) -> None:
        self.binary = bytearray()
        self.buffer_views: list[dict[str, Any]] = []
        self.accessors: list[dict[str, Any]] = []

    def view(self, data: bytes, *, target: int | None = None) -> int:
        while len(self.binary) % 4:
            self.binary.append(0)
        offset = len(self.binary)
        self.binary.extend(data)
        result: dict[str, Any] = {
            "buffer": 0,
            "byteOffset": offset,
            "byteLength": len(data),
        }
        if target is not None:
            result["target"] = target
        self.buffer_views.append(result)
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
        count = int(array.shape[0])
        result: dict[str, Any] = {
            "bufferView": self.view(array.tobytes(), target=target),
            "componentType": component_type,
            "count": count,
            "type": shape,
        }
        if bounds and count:
            result["min"] = np.asarray(array).min(axis=0).reshape(-1).tolist()
            result["max"] = np.asarray(array).max(axis=0).reshape(-1).tolist()
        self.accessors.append(result)
        return len(self.accessors) - 1


def _skeleton(builder: GlbBuilder, model: CharacterModel):
    nodes = []
    roots = []
    globals_: list[np.ndarray | None] = [None] * len(model.bones)
    for bone in model.bones:
        translation = _position(bone["position"])
        rotation = _quaternion(bone["rotation"])
        nodes.append(
            {
                "name": bone["name"],
                "translation": list(translation),
                "rotation": list(rotation),
            }
        )
    for bone in model.bones:
        index = bone["index"]
        local = _matrix(_quaternion(bone["rotation"]), _position(bone["position"]))
        parent = bone["parent"]
        if parent < 0:
            roots.append(index)
            globals_[index] = local
        else:
            if not 0 <= parent < len(nodes) or globals_[parent] is None:
                raise CharacterGlbError(
                    f"bone {bone['name']} has unresolved parent {parent}"
                )
            nodes[parent].setdefault("children", []).append(index)
            globals_[index] = globals_[parent] @ local
    inverse = np.asarray(
        [np.linalg.inv(matrix).T.reshape(16) for matrix in globals_], dtype=np.float32
    )
    inverse_accessor = builder.accessor(inverse, FLOAT, "MAT4")
    skin: dict[str, Any] = {
        "inverseBindMatrices": inverse_accessor,
        "joints": list(range(len(model.bones))),
    }
    if len(roots) == 1:
        skin["skeleton"] = roots[0]

    for attachment in model.attachments:
        bone = int(attachment["bone"])
        if not 0 <= bone < len(model.bones):
            raise CharacterGlbError(
                f"attachment {attachment['name']!r} names bone {bone}"
            )
        node_index = len(nodes)
        nodes.append(
            {
                "name": attachment["name"],
                "translation": list(_position(attachment["pos"])),
                "rotation": list(_quaternion(attachment["quat"])),
                "extensions": {
                    CHARACTER_EXTENSION: {
                        "attachmentIndex": attachment.get("index", node_index),
                        "flags": attachment.get("flags", 0),
                        "bone": bone,
                    }
                },
            }
        )
        nodes[bone].setdefault("children", []).append(node_index)

    for bodypart in model.header.get("bodyParts") or []:
        for body_model in bodypart.get("models") or []:
            for eyeball in body_model.get("eyeballs") or []:
                bone = int(eyeball["bone"])
                if not 0 <= bone < len(model.bones):
                    raise CharacterGlbError(
                        f"eyeball {eyeball.get('name', bone)!r} names bone {bone}"
                    )
                node_index = len(nodes)
                nodes.append(
                    {
                        "name": eyeball.get("name") or f"eye-{eyeball.get('index', node_index)}",
                        "translation": list(_position(eyeball["org"])),
                        "extensions": {
                            CHARACTER_EXTENSION: {
                                "eyeballIndex": eyeball.get("index"),
                                "bone": bone,
                                "radius": eyeball.get("radius"),
                                "irisScale": eyeball.get("iris_scale"),
                            }
                        },
                    }
                )
                nodes[bone].setdefault("children", []).append(node_index)

    scene_roots = roots
    if len(roots) > 1:
        common = len(nodes)
        nodes.append(
            {
                "name": f"{model.asset_id}:root",
                "children": list(roots),
            }
        )
        scene_roots = [common]
    return nodes, scene_roots, skin


def _material_index(model: CharacterModel) -> tuple[list[dict[str, Any]], dict[str, int]]:
    materials = []
    by_id = {}
    for row in model.materials:
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
                "extensions": {
                    MATERIAL_EXTENSION: {"material": identifier}
                },
            }
        )
    return materials, by_id


def _fallback_normals(positions: np.ndarray, triangles: np.ndarray) -> np.ndarray:
    normals = np.zeros_like(positions)
    faces = triangles.reshape(-1, 3)
    geometric = np.cross(
        positions[faces[:, 1]] - positions[faces[:, 0]],
        positions[faces[:, 2]] - positions[faces[:, 0]],
    )
    for corner in range(3):
        np.add.at(normals, faces[:, corner], geometric)
    lengths = np.linalg.norm(normals, axis=1, keepdims=True)
    return normals / np.where(lengths > 1e-12, lengths, 1.0)


def _meshes(
    builder: GlbBuilder,
    model: CharacterModel,
    material_ids: dict[str, int],
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    meshes = []
    lod_rows = []
    morph_names = [row["name"] for row in model.facial.get("morphTargets", [])]
    for lod in model.lods:
        primitives = []
        for source in lod["primitives"]:
            positions = np.asarray([_position(value) for value in source["positions"]], np.float32)
            normals = np.asarray([_direction(value) for value in source["normals"]], np.float32)
            tangents = np.asarray(
                [(*_direction(value[:3]), float(value[3])) for value in source["tangents"]],
                np.float32,
            ) if source.get("tangents") else None
            uvs = np.asarray(source["uvs"], np.float32)
            joints = np.asarray(source["joints"], np.uint16)
            weights = np.asarray(source["weights"], np.float32)
            triangles = np.asarray(source["triangles"], np.uint32).reshape(-1)
            authored_lengths = np.linalg.norm(normals, axis=1, keepdims=True)
            fallback = _fallback_normals(positions, triangles)
            normals = np.where(authored_lengths > 1e-12, normals / np.where(
                authored_lengths > 1e-12, authored_lengths, 1.0), fallback)
            skin_reference = source["skinReference"]
            if not model.skin_families or not 0 <= skin_reference < len(model.skin_families[0]):
                raise CharacterGlbError(
                    f"LOD {lod['index']} primitive names skin reference {skin_reference}"
                )
            material_id = model.skin_families[0][skin_reference]
            if material_id not in material_ids:
                raise CharacterGlbError(f"primitive material {material_id!r} is not declared")
            primitive: dict[str, Any] = {
                "attributes": {
                    "POSITION": builder.accessor(
                        positions, FLOAT, "VEC3", target=ARRAY_BUFFER, bounds=True
                    ),
                    "NORMAL": builder.accessor(
                        normals.astype(np.float32), FLOAT, "VEC3", target=ARRAY_BUFFER
                    ),
                    "TEXCOORD_0": builder.accessor(
                        uvs, FLOAT, "VEC2", target=ARRAY_BUFFER
                    ),
                    "JOINTS_0": builder.accessor(
                        joints, U16, "VEC4", target=ARRAY_BUFFER
                    ),
                    "WEIGHTS_0": builder.accessor(
                        weights, FLOAT, "VEC4", target=ARRAY_BUFFER
                    ),
                },
                "indices": builder.accessor(
                    triangles,
                    U32,
                    "SCALAR",
                    target=ELEMENT_ARRAY_BUFFER,
                ),
                "material": material_ids[material_id],
                "extensions": {
                    MATERIAL_EXTENSION: {"material": material_id},
                    CHARACTER_EXTENSION: {
                        "bodyPart": source["bodyPart"],
                        "model": source["model"],
                        "mesh": source["mesh"],
                        "skinReference": skin_reference,
                        "sourceVertices": source["sourceVertices"],
                        "stripGroups": source.get("stripGroups", []),
                    },
                },
            }
            if tangents is not None:
                primitive["attributes"]["TANGENT"] = builder.accessor(
                    tangents, FLOAT, "VEC4", target=ARRAY_BUFFER
                )
            morphs = source.get("morphTargets")
            if morphs:
                targets = []
                for target in morphs:
                    position = np.asarray(
                        [_position(value) for value in target["position"]], np.float32
                    )
                    normal = np.asarray(
                        [_direction(value) for value in target["normal"]], np.float32
                    )
                    targets.append(
                        {
                            "POSITION": builder.accessor(position, FLOAT, "VEC3"),
                            "NORMAL": builder.accessor(normal, FLOAT, "VEC3"),
                        }
                    )
                primitive["targets"] = targets
            primitives.append(primitive)
        mesh: dict[str, Any] = {
            "name": f"{model.asset_id}:lod{lod['index']}",
            "primitives": primitives,
        }
        if lod["index"] == 0 and morph_names:
            mesh["weights"] = [0.0] * len(morph_names)
            mesh["extras"] = {"targetNames": morph_names}
        mesh_index = len(meshes)
        meshes.append(mesh)
        lod_rows.append(
            {
                "index": lod["index"],
                "mesh": mesh_index,
                "switchPoints": lod["switchPoints"],
                "primitiveCount": len(primitives),
            }
        )
    return meshes, lod_rows


def _animations(
    builder: GlbBuilder,
    model: CharacterModel,
    mdl_data: bytes,
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    if not model.local_animations:
        return [], []
    bone_records = mdl_skel.read_bones(mdl_data)
    animations = []
    metadata = []
    for row in model.local_animations:
        frames = mdl_skel.read_anim(
            mdl_data,
            bone_records,
            row["sourceOffset"],
            row["frameCount"],
        )
        frame_count = row["frameCount"]
        times = np.arange(frame_count, dtype=np.float32) / float(row["fps"] or 30.0)
        time_accessor = builder.accessor(times, FLOAT, "SCALAR", bounds=True)
        channels = []
        samplers = []
        for bone in bone_records:
            if row["boneWeights"][bone.index] == 0.0:
                continue
            translations = np.asarray(
                [_position(frames[frame][bone.index][0]) for frame in range(frame_count)],
                np.float32,
            )
            rotations = np.asarray(
                [_quaternion(frames[frame][bone.index][1]) for frame in range(frame_count)],
                np.float32,
            )
            samplers.append(
                {
                    "input": time_accessor,
                    "output": builder.accessor(translations, FLOAT, "VEC3"),
                    "interpolation": "LINEAR",
                }
            )
            channels.append(
                {
                    "sampler": len(samplers) - 1,
                    "target": {"node": bone.index, "path": "translation"},
                }
            )
            samplers.append(
                {
                    "input": time_accessor,
                    "output": builder.accessor(rotations, FLOAT, "VEC4"),
                    "interpolation": "LINEAR",
                }
            )
            channels.append(
                {
                    "sampler": len(samplers) - 1,
                    "target": {"node": bone.index, "path": "rotation"},
                }
            )
        gltf_index = len(animations)
        animations.append(
            {"name": f"{row['index']}:{row['name']}", "channels": channels, "samplers": samplers}
        )
        metadata.append({**row, "animation": gltf_index})
    return animations, metadata


def _physics_accessors(builder: GlbBuilder, physics: dict[str, Any] | None):
    if physics is None:
        return {}
    result = plain(physics)
    for solid in result["solids"]:
        for hull in solid["hulls"]:
            vertices = np.asarray(hull.pop("vertices"), np.float32)
            triangles = np.asarray(hull.pop("triangles"), np.uint32).reshape(-1)
            hull["positions"] = builder.accessor(
                vertices, FLOAT, "VEC3", bounds=True
            )
            hull["indices"] = builder.accessor(triangles, U32, "SCALAR")
    return result


def build_document(
    model: CharacterModel,
    mdl_data: bytes,
) -> tuple[dict[str, Any], bytes]:
    """Build a complete glTF JSON document and BIN chunk from the semantic model."""
    builder = GlbBuilder()
    nodes, roots, skin = _skeleton(builder, model)
    materials, material_ids = _material_index(model)
    meshes, lod_rows = _meshes(builder, model, material_ids)
    animations, animation_rows = _animations(builder, model, mdl_data)
    if not meshes:
        raise CharacterGlbError("character carries no decoded mesh LOD")
    mesh_node = len(nodes)
    nodes.append({"name": model.asset_id, "mesh": 0, "skin": 0})
    physics = _physics_accessors(builder, model.physics)
    source_resolution = [
        {
            "role": source.role,
            "path": source.path,
            "origin": source.origin,
            "byteLength": source.byte_length,
            "sha256": source.sha256,
        }
        for source in model.sources
    ]
    vtx_rows = []
    for source in model.sources:
        if source.role.startswith("vtx-"):
            vtx_rows.append(
                {
                    "role": source.role,
                    "path": source.path,
                    "sha256": source.sha256,
                    "byteLength": source.byte_length,
                }
            )
    extension = {
        "schemaVersion": SCHEMA_VERSION,
        "identity": {
            "asset": model.asset_id,
            "modelPath": model.model_path,
            "sourcePolicy": "up-first",
        },
        "sourceResolution": {"policy": "up-first", "members": source_resolution},
        "coordinateTransform": COORDINATE_TRANSFORM,
        "mdl": {
            "header": model.header,
            "bones": model.bones,
            "localAnimations": animation_rows,
            "sequences": model.sequences,
            "poseParameters": model.pose_parameters,
            "attachments": model.attachments,
            "hitboxSets": model.hitbox_sets,
            "ikChains": model.ik_chains,
        },
        "vtx": {
            "variants": vtx_rows,
            "comparison": model.variant_comparison,
            "lods": lod_rows,
        },
        "physics": physics,
        "materialBindings": {
            "slots": model.materials,
            "skinFamilies": model.skin_families,
        },
        "facial": model.facial,
        "procedural": model.procedural,
        "secondaryMotion": model.secondary_motion,
        "cloth": model.cloth,
        "dependencies": model.dependencies,
        "coverage": {
            "mapped": [
                "identity", "sourceResolution", "coordinateTransform", "mdl", "vtx",
                "physics", "materialBindings", "facial", "procedural",
                "secondaryMotion", "cloth", "dependencies",
            ],
            "typedUnidentified": model.typed_unidentified,
            "omittedProven": model.omitted_proven,
            "byteLedger": model.byte_coverage,
            "unresolved": model.unresolved,
            "unsupported": model.unsupported,
        },
    }
    document = {
        "asset": {"version": "2.0", "generator": "Elysium Character GLB Exporter"},
        "extensionsUsed": [MATERIAL_EXTENSION, CHARACTER_EXTENSION],
        "extensionsRequired": [MATERIAL_EXTENSION, CHARACTER_EXTENSION],
        "scene": 0,
        "scenes": [{"nodes": [*roots, mesh_node]}],
        "nodes": nodes,
        "skins": [skin],
        "meshes": meshes,
        "materials": materials,
        "extensions": {CHARACTER_EXTENSION: plain(extension)},
        "accessors": builder.accessors,
        "bufferViews": builder.buffer_views,
        "buffers": [{"byteLength": len(builder.binary)}],
    }
    if animations:
        document["animations"] = animations
    return document, bytes(builder.binary)


def write_glb(document: dict[str, Any], binary: bytes, destination: Path) -> None:
    json_data = json.dumps(
        plain(document), separators=(",", ":"), ensure_ascii=False, allow_nan=False
    ).encode("utf-8")
    json_data += b" " * (-len(json_data) % 4)
    binary += b"\0" * (-len(binary) % 4)
    total = 12 + 8 + len(json_data) + 8 + len(binary)
    payload = bytearray(struct.pack("<III", 0x46546C67, 2, total))
    payload.extend(struct.pack("<II", len(json_data), 0x4E4F534A))
    payload.extend(json_data)
    payload.extend(struct.pack("<II", len(binary), 0x004E4942))
    payload.extend(binary)
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix(destination.suffix + ".tmp")
    temporary.write_bytes(payload)
    os.replace(temporary, destination)


def export(
    index: dict,
    model_path: str,
    output_root: Path,
    *,
    read_bytes=None,
    anorms=None,
) -> Path:
    """Resolve, fully decode, and write one character GLB below ``output_root``."""
    closure = load_source_closure(index, model_path, read_bytes=read_bytes)
    if anorms is None:
        anorms = mdl_skel.load_anorms()
    model = decode_character(
        closure,
        index,
        read_bytes=read_bytes,
        anorms=anorms,
    )
    document, binary = build_document(model, closure.mdl.data)
    from elysium_pipeline.validation import character_glb as validation

    validation.validate_document(document, binary, source_members=closure.members())
    destination = output_root / Path(*output_relative_path(closure.model_path).parts)
    write_glb(document, binary, destination)
    return destination
