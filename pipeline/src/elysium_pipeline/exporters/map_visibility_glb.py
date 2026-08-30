"""Isolated one-map Map-visibility GLB product writer.

The unit is scene-less: a cluster visibility set is a bitset, not something a general consumer can
draw or play. Core glTF still carries the bitsets themselves -- one `SCALAR`/`UNSIGNED_BYTE`
accessor per recorded row -- so a reader that ignores the extension can still take the decompressed
sets, and the extension states only what glTF cannot: which cluster a row belongs to, where its
compressed span sits, which clusters share one row, and the portal lumps whose meaning is open.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any, Callable

import elysium_pipeline.formats.map_visibility_glb.coverage as vis_coverage
from elysium_pipeline.formats.map_visibility_glb import (
    KIND_TITLE,
    MAP_VISIBILITY_EXTENSION,
    SCHEMA_VERSION,
    decode_map_visibility,
    load_source_closure,
    normalize_key,
    output_relative_path,
)
from elysium_pipeline.formats.map_visibility_glb import source as vis_source
from elysium_pipeline.formats.map_visibility_glb.model import MapVisibilityModel
from elysium_pipeline.formats.unit_contract import (
    asset_block,
    extension_root,
    identity_block,
    plain,
    source_resolution,
    write_glb,
)


def source_keys(index: dict) -> list[str]:
    """Every map key the install index resolves, for the plural command."""

    return vis_source.source_keys(index)


def build_document(model: MapVisibilityModel) -> tuple[dict[str, Any], bytes]:
    """The glTF document and BIN payload for one decoded map-visibility unit."""

    extension = extension_root(
        schema_version=SCHEMA_VERSION,
        identity=identity_block(model.asset_id, model.source_path, map=model.key),
        source_resolution=source_resolution(model.members),
        dependencies=model.dependencies,
        coverage=vis_coverage.coverage(model),
        map=model.map_block,
        numClusters=model.num_clusters,
        rowByteLength=model.row_byte_length,
        clusters=[cluster.to_json() for cluster in model.clusters],
        unclusteredLeaves=list(model.unclustered_leaves),
        portals=model.portals,
        anomalies=list(model.anomalies),
        omissions=list(model.omissions),
    )
    document: dict[str, Any] = {
        "asset": asset_block(KIND_TITLE),
        "extensionsUsed": [MAP_VISIBILITY_EXTENSION],
        "extensionsRequired": [MAP_VISIBILITY_EXTENSION],
        "extensions": {MAP_VISIBILITY_EXTENSION: plain(extension)},
    }
    # The accessor table is what names the rows, so it is what decides whether the unit carries a
    # BIN chunk at all: a map that recorded no row publishes JSON alone.
    if not model.accessors:
        return document, b""
    document["accessors"] = model.accessors
    document["bufferViews"] = model.buffer_views
    document["buffers"] = [{"byteLength": len(model.binary)}]
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
    model = decode_map_visibility(closure)
    document, binary = build_document(model)

    from elysium_pipeline.validation import map_visibility_glb as validation

    validation.validate_document(
        document,
        binary,
        source_members=closure.members(),
        leaf_table=closure.leaf_bytes(),
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
    "build_document",
    "export",
    "export_all",
    "normalize_key",
    "source_keys",
]
