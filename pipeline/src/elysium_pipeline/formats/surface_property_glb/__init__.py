"""Isolated lossless Surface-property GLB format aggregation."""

from elysium_pipeline.formats.surface_property_glb.decode import (
    DAMAGE_OUTCOMES,
    IMPACT_KEYS,
    WEAPON_CLASSES,
    SurfacePropertyDecodeError,
    decode_surface_property,
)
from elysium_pipeline.formats.surface_property_glb.model import (
    SCHEMA_VERSION,
    SURFACE_PROPERTY_EXTENSION,
    TABLE_PATH,
    Parameter,
    SurfacePropertyModel,
    asset_id,
    normalize_name,
    output_relative_path,
    sound_asset_id,
    sound_script_asset_id,
)
from elysium_pipeline.formats.surface_property_glb.source import (
    SOUND_SCRIPT_PATH,
    SourceMember,
    SurfacePropertyClosure,
    SurfacePropertySourceError,
    SurfacePropertyTable,
    load_sound_script_names,
    load_source_closure,
    load_table,
)

__all__ = [
    "DAMAGE_OUTCOMES",
    "IMPACT_KEYS",
    "SCHEMA_VERSION",
    "SOUND_SCRIPT_PATH",
    "SURFACE_PROPERTY_EXTENSION",
    "TABLE_PATH",
    "WEAPON_CLASSES",
    "Parameter",
    "SourceMember",
    "SurfacePropertyClosure",
    "SurfacePropertyDecodeError",
    "SurfacePropertyModel",
    "SurfacePropertySourceError",
    "SurfacePropertyTable",
    "asset_id",
    "decode_surface_property",
    "load_sound_script_names",
    "load_source_closure",
    "load_table",
    "normalize_name",
    "output_relative_path",
    "sound_asset_id",
    "sound_script_asset_id",
]
