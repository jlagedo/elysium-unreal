"""Texture decoding.

A texture unit carries a complete KTX2 file in a bufferView that no core glTF object
points at, so Blender's importer sees a buffer and no picture. Blender has no KTX2
reader, but it decodes DDS through OpenImageIO, and the block bytes in a KTX2 level are
the block bytes in a DDS surface. Wrapping a level in a header therefore turns Blender's
own decoder into ours, with no dependency and no temporary file.

Only level 0 is ever handed over. The path that would use a supplied mip chain opens the
file by name, which a packed image does not have, so a chain costs bytes and buys
nothing.

That same path logs two warnings per image the first time Blender uploads it to the GPU
(`failed to load data from file`, `falling back to uncompressed`). The fallback is what
we want and the result is correct; re-encoding the buffer to shed the DDS file type only
changes the wording of the warning, so the noise is accepted rather than paid for.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import bpy
import numpy as np

from ..core import dds, glb, ids, ktx2, seams

#: Blender colour space names. 'Linear', 'Raw' and 'XYZ' were all renamed and now raise.
COLOR = "sRGB"
NON_COLOR = "Non-Color"

#: KTX2 stores cubemap faces in this order.
FACE_NAMES = ("px", "nx", "py", "ny", "pz", "nz")


@dataclass
class Decoded:
    """One decoded texture unit."""

    identity: str
    #: Primary image: face +X for a cube, layer 0 for an array, else the whole thing.
    image: bpy.types.Image | None
    #: Every image built, keyed by suffix ('' for a plain 2D texture).
    images: dict
    #: Human-readable account of what was decoded, for the inspector.
    note: str
    #: The unit's own record of what it could not carry.
    omissions: list


class TextureCache:
    """Session-scoped decode cache.

    One texture is referenced by many materials, and decoding is the expensive half of
    building a character, so a unit is decoded once per session and reused.
    """

    def __init__(self, root: Path) -> None:
        self.root = Path(root)
        self._entries: dict[tuple[str, bool], Decoded | None] = {}

    def get(self, identity: str, *, non_color: bool) -> Decoded | None:
        key = (identity, non_color)
        if key not in self._entries:
            self._entries[key] = _decode(identity, self.root, non_color=non_color)
        return self._entries[key]


def _new_image(name: str, width: int, height: int, *, non_color: bool) -> bpy.types.Image:
    image = bpy.data.images.new(name, width, height, alpha=True, is_data=non_color)
    if not non_color:
        image.colorspace_settings.name = COLOR
    return image


def _image_from_blocks(
    name: str, payload: bytes, width: int, height: int, fourcc: bytes, *, non_color: bool
) -> bpy.types.Image:
    """Decode a block-compressed level by handing Blender a DDS of it."""
    envelope = dds.build(payload, width, height, fourcc)
    # The dimensions given here are placeholders; packing decodes and resizes.
    image = bpy.data.images.new(name, 8, 8, alpha=True, is_data=non_color)
    image.pack(data=envelope, data_len=len(envelope))
    image.source = "FILE"
    image.colorspace_settings.name = NON_COLOR if non_color else COLOR
    return image


def _rgba_from_bytes(payload: bytes, texture: ktx2.Ktx2, width: int, height: int) -> np.ndarray:
    """Expand an uncompressed level to float32 RGBA, top row first."""
    raw = np.frombuffer(payload, dtype=np.uint8)

    if texture.vk_format == ktx2.VK_R8G8B8_UNORM:
        # Rows are tightly packed at three bytes per texel, with no padding to four.
        rgb = raw.reshape(height, width, 3)
        alpha = np.full((height, width, 1), 255, np.uint8)
        rgba = np.concatenate((rgb, alpha), axis=2)
    elif texture.vk_format == ktx2.VK_B8G8R8A8_UNORM:
        rgba = raw.reshape(height, width, 4)[..., [2, 1, 0, 3]]
    elif texture.vk_format == ktx2.VK_R8G8B8A8_UINT:
        # Source UVWQ: two's-complement du and dv, with w and q unused. Biasing by 128
        # puts zero displacement on the flat-normal grey a reviewer expects to see.
        uvwq = raw.reshape(height, width, 4)
        opaque = np.full((height, width), 255, np.uint8)
        rgba = np.dstack((uvwq[..., 0] ^ 0x80, uvwq[..., 1] ^ 0x80, opaque, opaque))
    else:
        rgba = raw.reshape(height, width, 4)

    return np.ascontiguousarray(rgba, dtype=np.float32) / 255.0


def _image_from_pixels(
    name: str, payload: bytes, texture: ktx2.Ktx2, width: int, height: int, *, non_color: bool
) -> bpy.types.Image:
    rgba = _rgba_from_bytes(payload, texture, width, height)
    image = _new_image(name, width, height, non_color=non_color)
    # KTX2 row 0 is the top row; Blender's row 0 is the bottom one.
    image.pixels.foreach_set(np.ascontiguousarray(rgba[::-1]).ravel())
    image.update()
    image.pack()
    return image


def _build_image(
    name: str, data: bytes, texture: ktx2.Ktx2, *, layer: int, face: int, non_color: bool
) -> bpy.types.Image:
    level = texture.levels[0]
    payload = ktx2.image_bytes(data, texture, 0, layer=layer, face=face)
    block = texture.format
    if block.compressed:
        return _image_from_blocks(
            name, payload, level.width, level.height, block.fourcc, non_color=non_color
        )
    return _image_from_pixels(
        name, payload, texture, level.width, level.height, non_color=non_color
    )


def _decode(identity: str, root: Path, *, non_color: bool) -> Decoded | None:
    path = ids.resolve(identity, root)
    if path is None or not path.is_file():
        return None

    document, binary = glb.read(path)
    payload = seams.root_extension(document, seams.TEXTURE_EXTENSION)
    if payload is None:
        return None

    view = (payload.get("payload") or {}).get("bufferView", 0)
    data = glb.buffer_view_bytes(document, binary, view)
    texture = ktx2.parse(data)
    if texture.format is None:
        return Decoded(identity, None, {}, "unsupported vkFormat %d" % texture.vk_format,
                       payload.get("omissions") or [])

    stem = identity.split(":", 2)[-1]
    images: dict[str, bpy.types.Image] = {}

    if texture.is_cubemap:
        # Blender has no cubemap datablock, and routing a cube through DDS returns one
        # flipped vertical strip, so the faces are sliced here instead.
        for face, suffix in enumerate(FACE_NAMES):
            images[suffix] = _build_image(
                "%s.%s" % (stem, suffix), data, texture, layer=0, face=face,
                non_color=non_color,
            )
        note = "cubemap, %d faces at %dx%d" % (6, texture.width, texture.height)
    elif texture.is_array:
        # These are animation frames rather than array elements in any Blender sense.
        images[""] = _build_image(
            stem, data, texture, layer=0, face=0, non_color=non_color
        )
        note = "frame 1 of %d at %dx%d" % (texture.layers, texture.width, texture.height)
    else:
        images[""] = _build_image(stem, data, texture, layer=0, face=0, non_color=non_color)
        note = "%dx%d %s, %d mip(s)" % (
            texture.width, texture.height, texture.format.name, max(1, texture.level_count)
        )

    primary = images.get("") or images.get(FACE_NAMES[0])
    for image in images.values():
        image["elysium_texture"] = identity
    return Decoded(identity, primary, images, note, payload.get("omissions") or [])
