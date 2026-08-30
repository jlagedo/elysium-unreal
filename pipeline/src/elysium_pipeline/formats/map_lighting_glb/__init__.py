"""Isolated lossless Map-lighting GLB format aggregation.

The lighting unit is cut from lumps 8, 15, 32 and 34 of one BSP plus the `dplt` game-lump
payload; `formats/map_glb/partition.py` is the proof that says so, and the root unit owns it.
"""

from elysium_pipeline.formats.map_lighting_glb.decode import (
    MapLightingDecodeError,
    decode_map_lighting,
)
from elysium_pipeline.formats.map_lighting_glb.model import (
    DETAIL_PROP_LIGHTING_ID,
    FAMILY_DIR,
    KIND,
    KIND_TITLE,
    LIGHT_TYPE_NAMES,
    MAP_LIGHTING_EXTENSION,
    MAPPED_FIELDS,
    OWNED_LUMPS,
    SCHEMA_VERSION,
    SURF_BUMPLIGHT,
    DetailPropLight,
    DisplacementLighting,
    FaceLighting,
    MapKeyError,
    MapLightingModel,
    SampleSpan,
    WorldLight,
    asset_id,
    game_lump_member_path,
    map_asset_id,
    map_entities_asset_id,
    member_path,
    normalize_key,
    output_relative_path,
    source_path,
)
from elysium_pipeline.formats.map_lighting_glb.source import (
    MapLightingSourceClosure,
    MapLightingSourceError,
    load_source_closure,
    source_keys,
)

__all__ = [
    "DETAIL_PROP_LIGHTING_ID",
    "FAMILY_DIR",
    "KIND",
    "KIND_TITLE",
    "LIGHT_TYPE_NAMES",
    "MAP_LIGHTING_EXTENSION",
    "MAPPED_FIELDS",
    "OWNED_LUMPS",
    "SCHEMA_VERSION",
    "SURF_BUMPLIGHT",
    "DetailPropLight",
    "DisplacementLighting",
    "FaceLighting",
    "MapKeyError",
    "MapLightingDecodeError",
    "MapLightingModel",
    "MapLightingSourceClosure",
    "MapLightingSourceError",
    "SampleSpan",
    "WorldLight",
    "asset_id",
    "decode_map_lighting",
    "game_lump_member_path",
    "load_source_closure",
    "map_asset_id",
    "map_entities_asset_id",
    "member_path",
    "normalize_key",
    "output_relative_path",
    "source_keys",
    "source_path",
]
