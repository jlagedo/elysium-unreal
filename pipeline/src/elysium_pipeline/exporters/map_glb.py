"""The Map GLB product writer: one BSP's root unit, scenes and extension.

The unit is the entry point to a map's four `export_v2` units. It publishes the header, the lump
directory, the partition proof and every lump the entities, lighting and visibility sub-units do
not claim, with the world, brush models, displacements and placements as ordinary glTF scenes.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any, Callable

import elysium_pipeline.formats.map_glb.coverage as map_coverage
from elysium_pipeline.formats.map_glb import (
    KIND_TITLE,
    MAP_EXTENSION,
    SCHEMA_VERSION,
    decode_map,
    load_source_closure,
    normalize_key,
    output_relative_path,
)
from elysium_pipeline.formats.map_glb import source as map_source
from elysium_pipeline.formats.unit_contract import (
    MATERIAL_REFERENCE,
    MODEL_REFERENCE,
    TEXTURE_REFERENCE,
    asset_block,
    extension_root,
    identity_block,
    plain,
    source_resolution,
    write_glb,
)

#: The cross-reference extensions a map can bind, in the order they are declared.
REFERENCE_EXTENSIONS = (MATERIAL_REFERENCE, MODEL_REFERENCE, TEXTURE_REFERENCE)

#: What the unit contract calls the transform, restated per domain this seam carries.
COORDINATE_TRANSFORM = {
    "source": "Source inches, Z-up, right-handed",
    "destination": "glTF metres, Y-up, right-handed",
    "scale": 0.0254,
    "position": "(x, y, z)_gltf = (x, z, -y)_source * 0.0254",
    "direction": "(x, y, z)_gltf = (x, z, -y)_source",
    "quaternion": "(x, y, z, w)_gltf = (x, z, -y, w)_source",
    "domains": {
        "vertexes": "position",
        "vertexNormals": "direction",
        "planes": "direction for the normal, position scale for the distance",
        "models": "position",
        "displacements": "position",
        "staticProps": "position and quaternion",
        "detailProps": "position and quaternion",
        "cubemaps": "the node translation is a position; cubemaps[].origin keeps the "
                    "source integers the probe's file name is built from",
        "water": "surface and minimum heights scaled with position",
        "occluders": "position",
        "primitives.verts": "position",
        "bsp.clipPortalVerts": "position",
        "displacements.verts": "direction for the offset, position scale for its length",
        "faces.area": "source square inches, kept as the compiler wrote it",
        "physics": "IVP metres, axis-only: (x, y, z)_gltf = (x, -y, -z)_ivp, no scale",
        "TEXCOORD_1": "lightmap luxels, kept in the source's own units",
        "TEXCOORD_0": "normalized by the TEXDATA width and height",
        "bsp.nodes/leafs": "source integer bounds, kept as authored",
    },
}


def source_keys(index: dict) -> list[str]:
    """Every map key the install index resolves, for the plural command."""

    return map_source.source_keys(index)


def bound_extensions(scenes: dict[str, Any]) -> set[str]:
    """The cross-reference extensions some object of this unit actually binds.

    The unit contract declares a reference extension where it is used, so a map with no static
    props declares no `ELYSIUM_model_reference`.
    """

    return {
        name
        for objects in (scenes["materials"], scenes["nodes"], scenes["meshes"])
        for entry in objects
        for name in (entry.get("extensions") or {})
    }


def build_document(model) -> tuple[dict[str, Any], bytes]:
    """The glTF document and BIN payload for one decoded map."""

    extension = extension_root(
        schema_version=SCHEMA_VERSION,
        identity=identity_block(model.asset_id, model.source_path, map=model.key),
        source_resolution=source_resolution([model.source]),
        dependencies=model.dependencies,
        coverage=map_coverage.coverage(model),
        coordinateTransform=COORDINATE_TRANSFORM,
        header=model.header,
        partition=model.partition.rows(),
        partitionOwners=model.partition.owners(),
        subUnits=model.sub_units,
        census=model.census,
        planes=model.planes,
        textures=model.textures,
        textureStrings=model.texture_strings,
        texinfos=model.texinfos,
        faces=model.faces,
        originalFaces=model.original_faces,
        edges=model.edges,
        surfEdges=model.surf_edges,
        normalIndices=model.normal_indices,
        models=model.models,
        bsp=model.bsp,
        collision=model.collision,
        displacements=model.displacements,
        displacementTriangleTags=model.displacement_triangle_tags,
        primitives=model.primitives,
        physics=model.physics,
        water=model.water,
        cubemaps=model.cubemaps,
        occluders=model.occluders,
        staticProps=model.static_props,
        detailProps=model.detail_props,
        pakfile=model.pakfile,
        trailer=model.trailer,
        anomalies=model.anomalies,
        omissions=model.omissions,
    )
    used = [name for name in REFERENCE_EXTENSIONS if name in bound_extensions(model.scenes)]
    used.append(MAP_EXTENSION)
    document: dict[str, Any] = {
        "asset": asset_block(KIND_TITLE),
        "extensionsUsed": used,
        "extensionsRequired": [MAP_EXTENSION],
        "scene": 0,
        "scenes": model.scenes["scenes"],
        "nodes": model.scenes["nodes"],
        "meshes": model.scenes["meshes"],
        "materials": model.scenes["materials"],
        "accessors": model.scenes["accessors"],
        "bufferViews": model.scenes["bufferViews"],
        "buffers": [{"byteLength": len(model.binary)}],
        "extensions": {MAP_EXTENSION: plain(extension)},
    }
    if not model.binary:
        for key in ("accessors", "bufferViews", "buffers"):
            document.pop(key, None)
    return document, model.binary


def export(
    index: dict,
    key: str,
    output_root: Path,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
) -> Path:
    """Decode, validate against the selected member, then write the unit atomically."""

    closure = load_source_closure(index, key, read_bytes=read_bytes)
    model = decode_map(closure, member_exists=lambda path: path.lower() in index)
    document, binary = build_document(model)
    from elysium_pipeline.validation import map_glb as validation

    validation.validate_document(document, binary, source_members=closure.members())
    destination = Path(output_root) / Path(*output_relative_path(closure.key).parts)
    write_glb(document, binary, destination)
    return destination


def export_all(
    index: dict,
    output_root: Path,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
) -> list[Path]:
    return [
        export(index, key, output_root, read_bytes=read_bytes) for key in source_keys(index)
    ]


__all__ = [
    "COORDINATE_TRANSFORM",
    "REFERENCE_EXTENSIONS",
    "bound_extensions",
    "build_document",
    "export",
    "export_all",
    "normalize_key",
    "source_keys",
]
