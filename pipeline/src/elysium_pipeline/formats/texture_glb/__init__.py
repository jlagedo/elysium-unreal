"""Isolated lossless Texture GLB format aggregation."""

from elysium_pipeline.formats.texture_glb.decode import TextureDecodeError, decode_texture
from elysium_pipeline.formats.texture_glb.model import (
    SCHEMA_VERSION,
    TEXTURE_EXTENSION,
    TextureModel,
    asset_id,
    normalize_texture_path,
    output_relative_path,
)
from elysium_pipeline.formats.texture_glb.source import (
    SourceMember,
    TextureSourceClosure,
    TextureSourceError,
    load_source_closure,
    source_keys,
)

__all__ = [
    "SCHEMA_VERSION",
    "TEXTURE_EXTENSION",
    "SourceMember",
    "TextureDecodeError",
    "TextureModel",
    "TextureSourceClosure",
    "TextureSourceError",
    "asset_id",
    "decode_texture",
    "load_source_closure",
    "normalize_texture_path",
    "output_relative_path",
    "source_keys",
]
