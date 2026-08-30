"""Complete TGA/BMP decode into the Image GLB semantic model."""

from __future__ import annotations

from elysium_pipeline.formats.image_glb.bmp import BmpDecodeError, decode_bmp
from elysium_pipeline.formats.image_glb.model import ImageModel
from elysium_pipeline.formats.image_glb.tga import TgaDecodeError, decode_tga


class ImageDecodeError(ValueError):
    """The selected image member cannot be read as a TGA or BMP image."""


def decode_image(closure) -> ImageModel:
    """Dispatch on the closure's own container, so the writer and the validator agree on it
    without either one sniffing the bytes a second way."""

    if closure.container == "tga":
        try:
            return decode_tga(closure)
        except TgaDecodeError as error:
            raise ImageDecodeError(str(error)) from error
    if closure.container == "bmp":
        try:
            return decode_bmp(closure)
        except BmpDecodeError as error:
            raise ImageDecodeError(str(error)) from error
    raise ImageDecodeError(f"unsupported image container {closure.container!r}")
