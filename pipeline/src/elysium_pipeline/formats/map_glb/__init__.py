"""Isolated lossless Map GLB format aggregation.

The map root is the entry point to one BSP's four `export_v2` units; `partition` is the shared
proof the three sub-unit seams import.
"""

from elysium_pipeline.formats.map_glb.decode import MapDecodeError, decode_map
from elysium_pipeline.formats.map_glb.model import (
    FAMILY_DIR,
    INCH_TO_METRE,
    KIND,
    KIND_TITLE,
    MAP_EXTENSION,
    MAPS_ROOT,
    SCHEMA_VERSION,
    SUB_UNITS,
    MapKeyError,
    MapModel,
    asset_id,
    material_asset_id,
    model_asset_id,
    normalize_key,
    output_relative_path,
    source_path,
    split_patched_name,
    sub_asset_id,
    sub_output_relative_path,
    surface_property_asset_id,
    texture_asset_id,
)
from elysium_pipeline.formats.map_glb.partition import (
    GameLumpEntry,
    LumpSpan,
    Partition,
    PartitionError,
    Region,
    Span,
)
from elysium_pipeline.formats.map_glb.source import (
    MapSourceClosure,
    MapSourceError,
    load_source_closure,
    source_keys,
)

__all__ = [
    "FAMILY_DIR",
    "INCH_TO_METRE",
    "KIND",
    "KIND_TITLE",
    "MAPS_ROOT",
    "MAP_EXTENSION",
    "SCHEMA_VERSION",
    "SUB_UNITS",
    "GameLumpEntry",
    "LumpSpan",
    "MapDecodeError",
    "MapKeyError",
    "MapModel",
    "MapSourceClosure",
    "MapSourceError",
    "Partition",
    "PartitionError",
    "Region",
    "Span",
    "asset_id",
    "decode_map",
    "load_source_closure",
    "material_asset_id",
    "model_asset_id",
    "normalize_key",
    "output_relative_path",
    "source_keys",
    "source_path",
    "split_patched_name",
    "sub_asset_id",
    "sub_output_relative_path",
    "surface_property_asset_id",
    "texture_asset_id",
]
