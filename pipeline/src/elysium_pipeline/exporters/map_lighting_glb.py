"""The Map-lighting GLB product writer: one map's bake, light sources and lighting tables.

The unit is scene-less. Its three accessors are lumps 8, 32 and 34 byte for byte, reached only
through the extension, and everything else it publishes -- the face rows, the world lights, the
displacement runs and the detail-prop lighting table -- says where those bytes belong.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any, Callable

import elysium_pipeline.formats.map_lighting_glb.coverage as lighting_coverage
from elysium_pipeline.formats.map_lighting_glb import (
    KIND_TITLE,
    MAP_LIGHTING_EXTENSION,
    SCHEMA_VERSION,
    decode_map_lighting,
    load_source_closure,
    output_relative_path,
)
from elysium_pipeline.formats.map_lighting_glb import source as lighting_source
from elysium_pipeline.formats.unit_contract import (
    asset_block,
    extension_root,
    identity_block,
    plain,
    source_resolution,
    write_glb,
)

#: What the unit contract calls the transform, restated per domain this seam carries.
COORDINATE_TRANSFORM = {
    "source": "Source inches, Z-up, right-handed",
    "destination": "glTF metres, Y-up, right-handed",
    "scale": 0.0254,
    "position": "(x, y, z)_gltf = (x, z, -y)_source * 0.0254",
    "direction": "(x, y, z)_gltf = (x, z, -y)_source",
    "quaternion": "(x, y, z, w)_gltf = (x, z, -y, w)_source",
    "domains": {
        "worldLights.origin": "position",
        "worldLights.normal": "direction",
        "worldLights.radius": "source inches, restated in metres beside them",
        "worldLights.intensity": "linear RGB, unbounded, carried as the compiler wrote it",
        "samples": "lightmap luxels, kept in the source's own units",
        "faces.lightmapMins/lightmapSize": "lightmap luxels, kept as authored",
        "dispAlphas/dispSamplePositions": "per-luxel bytes, kept in the source's own units",
        "detailPropLighting": "colour only; the prop's placement belongs to the map root",
    },
}


def source_keys(index: dict) -> list[str]:
    """Every map key the install index resolves, for the plural command."""

    return lighting_source.source_keys(index)


def build_document(model) -> tuple[dict[str, Any], bytes]:
    """The glTF document and BIN payload for one decoded lighting unit."""

    extension = extension_root(
        schema_version=SCHEMA_VERSION,
        # The unit is cut from one file, so the identity names that file; the spans it owns are
        # the member table's business, one `#lump` member each.
        identity=identity_block(model.asset_id, model.source_path, map=model.key),
        source_resolution=source_resolution(model.source.members()),
        dependencies=model.dependencies,
        coverage=lighting_coverage.coverage(model),
        coordinateTransform=COORDINATE_TRANSFORM,
        map=model.map,
        samples=model.samples,
        faces=[face.to_json() for face in model.faces],
        styleCensus=model.style_census,
        worldLights=[light.to_json() for light in model.world_lights],
        lightTypeCensus=model.light_type_census,
        dispAlphas=model.disp_alphas,
        dispSamplePositions=model.disp_sample_positions,
        displacements=[row.to_json() for row in model.displacements],
        detailPropLighting=[row.to_json() for row in model.detail_prop_lighting],
        anomalies=model.anomalies,
        omissions=model.omissions,
    )
    document: dict[str, Any] = {
        "asset": asset_block(KIND_TITLE),
        "extensionsUsed": [MAP_LIGHTING_EXTENSION],
        "extensionsRequired": [MAP_LIGHTING_EXTENSION],
        "accessors": model.accessors,
        "bufferViews": model.buffer_views,
        "buffers": [{"byteLength": len(model.binary)}],
        "extensions": {MAP_LIGHTING_EXTENSION: plain(extension)},
    }
    if not model.binary:
        for key in ("accessors", "bufferViews", "buffers"):
            document.pop(key)
    return document, model.binary


def export(
    index: dict,
    key: str,
    output_root: Path,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
) -> Path:
    """Decode, validate against the selected spans, then write the unit atomically."""

    closure = load_source_closure(index, key, read_bytes=read_bytes)
    model = decode_map_lighting(closure)
    document, binary = build_document(model)
    from elysium_pipeline.validation import map_lighting_glb as validation

    validation.validate_document(
        document, binary, source_members=closure.members(), map_bytes=closure.data
    )
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
    "build_document",
    "export",
    "export_all",
    "source_keys",
]
