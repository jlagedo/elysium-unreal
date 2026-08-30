"""The Map-entities GLB product writer: one map's ENTITIES lump, as an addressable table.

The unit is scene-less and has no BIN chunk: every datum is a string the source wrote as text or
a number read from one, so there is nothing for a core glTF accessor to hold. It is published
beside the map root, from the same member, over the span the root's partition proof hands it.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any, Callable

from elysium_pipeline.formats.map_entities_glb import (
    KIND_TITLE,
    MAP_ENTITIES_EXTENSION,
    SCHEMA_VERSION,
    decode_map_entities,
    load_source_closure,
    normalize_key,
    output_relative_path,
)
from elysium_pipeline.formats.map_entities_glb import source as entity_source
from elysium_pipeline.formats.map_entities_glb.coverage import build_coverage
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
        "entities[].origin": "position; source is the three C atof components in inches",
        "entities[].angles": "a Source QAngle (pitch, yaw, roll) in degrees, restated as the "
                             "glTF quaternion of the same placement",
    },
}


def source_keys(index: dict) -> list[str]:
    """Every map key the install index resolves, for the plural command."""

    return entity_source.source_keys(index)


def build_document(model) -> tuple[dict[str, Any], bytes]:
    """The glTF document and (absent) BIN payload for one decoded ENTITIES lump."""

    extension = extension_root(
        schema_version=SCHEMA_VERSION,
        identity=identity_block(model.asset_id, model.source_path, map=model.key),
        source_resolution=source_resolution([model.member]),
        dependencies=model.dependencies,
        coverage=build_coverage(model),
        coordinateTransform=COORDINATE_TRANSFORM,
        map=model.map,
        worldspawn=model.worldspawn,
        entities=model.entity_rows(),
        classCensus=model.class_census,
        scriptExpressions=model.script_expressions,
        comments=model.comments,
        anomalies=model.anomalies,
        omissions=model.omissions,
    )
    document: dict[str, Any] = {
        "asset": asset_block(KIND_TITLE),
        "extensionsUsed": [MAP_ENTITIES_EXTENSION],
        "extensionsRequired": [MAP_ENTITIES_EXTENSION],
        "extensions": {MAP_ENTITIES_EXTENSION: plain(extension)},
    }
    return document, b""


def export(
    index: dict,
    key: str,
    output_root: Path,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
    member_exists: Callable[[str], bool] | None = None,
) -> Path:
    """Decode, validate against the selected member, then write the unit atomically."""

    closure = load_source_closure(index, key, read_bytes=read_bytes)
    if member_exists is None:
        def member_exists(path: str) -> bool:
            return path.lower() in index

    model = decode_map_entities(closure, member_exists=member_exists)
    document, binary = build_document(model)
    from elysium_pipeline.validation import map_entities_glb as validation

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
    return [export(index, key, output_root, read_bytes=read_bytes) for key in source_keys(index)]


__all__ = [
    "COORDINATE_TRANSFORM",
    "build_document",
    "export",
    "export_all",
    "normalize_key",
    "source_keys",
]
