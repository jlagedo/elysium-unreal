"""Isolated lossless Image GLB format aggregation.

An image is a raw `.tga` or `.bmp` install member -- particle sprites, loose art below
`materials/`, `screenshots/`, and the Faceposer icons below `gfx/hlfaceposer/` -- as distinct
from a VTF-wrapped texture (`formats/texture_glb`). `docs/architecture/seam_map_image.md` owns
the format; `docs/architecture/seam_map_unit_contract.md` owns the shared unit rules this package
builds on through `formats/unit_contract`.
"""

from elysium_pipeline.formats.image_glb.bmp import BmpDecodeError, decode_bmp
from elysium_pipeline.formats.image_glb.decode import ImageDecodeError, decode_image
from elysium_pipeline.formats.image_glb.model import (
    IMAGE_EXTENSION,
    IMAGE_KIND,
    SCHEMA_VERSION,
    ImageModel,
    ImageModelError,
    ImageSourceClosure,
    asset_id,
    container_of,
    normalize_image_path,
    output_relative_path,
)
from elysium_pipeline.formats.image_glb.source import ImageSourceError, load_source_closure
from elysium_pipeline.formats.image_glb.tga import TgaDecodeError, decode_tga

__all__ = [
    "IMAGE_EXTENSION",
    "IMAGE_KIND",
    "SCHEMA_VERSION",
    "BmpDecodeError",
    "ImageDecodeError",
    "ImageModel",
    "ImageModelError",
    "ImageSourceClosure",
    "ImageSourceError",
    "TgaDecodeError",
    "asset_id",
    "container_of",
    "decode_bmp",
    "decode_image",
    "decode_tga",
    "load_source_closure",
    "normalize_image_path",
    "output_relative_path",
]
