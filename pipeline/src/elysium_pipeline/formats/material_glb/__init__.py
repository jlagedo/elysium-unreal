"""Isolated lossless Material GLB format aggregation."""

from elysium_pipeline.formats.material_glb.decode import (
    ENVIRONMENT_SYMBOL,
    TEXTURE_PARAMETERS,
    MaterialDecodeError,
    decode_material,
)
from elysium_pipeline.formats.material_glb.model import (
    MATERIAL_EXTENSION,
    SCHEMA_VERSION,
    MaterialModel,
    Parameter,
    ProxyRecord,
    asset_id,
    normalize_material_path,
    output_relative_path,
    surface_property_asset_id,
    texture_asset_id,
)
from elysium_pipeline.formats.material_glb.source import (
    MaterialSourceClosure,
    MaterialSourceError,
    SourceMember,
    load_source_closure,
)

__all__ = [
    "ENVIRONMENT_SYMBOL",
    "MATERIAL_EXTENSION",
    "SCHEMA_VERSION",
    "TEXTURE_PARAMETERS",
    "MaterialDecodeError",
    "MaterialModel",
    "MaterialSourceClosure",
    "MaterialSourceError",
    "Parameter",
    "ProxyRecord",
    "SourceMember",
    "asset_id",
    "decode_material",
    "load_source_closure",
    "normalize_material_path",
    "output_relative_path",
    "surface_property_asset_id",
    "texture_asset_id",
]
