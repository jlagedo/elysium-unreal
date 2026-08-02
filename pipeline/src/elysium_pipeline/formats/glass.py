"""Shared semantic-glass classification and authored-ripple normal generation.

Bloodlines does not name a dedicated glass shader.  Its lit glass is authored as a
translucent LightmappedGeneric/VertexLitGeneric surface with an envmap, while effects,
nets, curtains and glow panes use other combinations.  Exporters classify that intent
here so every downstream consumer receives an explicit, engine-neutral ``glass 1`` flag.
"""
from __future__ import annotations

import re

import numpy as np
from PIL import Image, ImageFilter


_LIT_GLASS_SHADERS = frozenset(("lightmappedgeneric", "vertexlitgeneric"))


def is_glass(info: dict, material_path: str | None) -> bool:
    """Return whether one parsed VMT describes lit reflective glass.

    The word check is intentionally against both the resolved VMT path and its
    basetexture.  Props often call the material ``glass`` while world materials keep
    the semantic in their ``glass/...`` path.  Requiring the complete authored shader
    combination prevents unrelated alpha-blended effects from entering this path.
    """
    haystack = "%s %s" % (material_path or "", info.get("basetexture") or "")
    return bool(
        info.get("translucent")
        and info.get("envmap")
        and not info.get("additive")
        and not info.get("decal")
        and not info.get("water")
        and info.get("shader") in _LIT_GLASS_SHADERS
        and re.search(r"glass", haystack, re.IGNORECASE)
    )


def _erode_one(mask: np.ndarray) -> np.ndarray:
    """One-pixel 3x3 binary erosion with a false border, using NumPy only."""
    padded = np.pad(mask.astype(bool), 1, mode="constant", constant_values=False)
    out = np.ones(mask.shape, dtype=bool)
    height, width = mask.shape
    for y in range(3):
        for x in range(3):
            out &= padded[y:y + height, x:x + width]
    return out


def derive_normal(
    source: Image.Image,
    env_mask: Image.Image | None = None,
    *,
    blur_radius: float = 1.0,
    slope_scale: float = 2.0,
) -> Image.Image:
    """Derive a tangent-space normal from the authored dirty/uneven glass image.

    The source luminance supplies the ripple height.  An envmap mask confines the
    distortion to reflective panes (and is eroded one pixel to keep frame edges flat);
    where no mask exists, any texel with authored alpha participates.  The result is an
    RGB normal texture with neutral (128, 128, 255) pixels outside that region.
    """
    rgba = source.convert("RGBA")
    rgb = np.asarray(rgba, dtype=np.float32)[:, :, :3] / 255.0
    luma = rgb[:, :, 0] * 0.2126 + rgb[:, :, 1] * 0.7152 + rgb[:, :, 2] * 0.0722
    height_img = Image.fromarray(np.rint(luma * 255.0).astype(np.uint8), "L")
    if blur_radius > 0.0:
        height_img = height_img.filter(ImageFilter.GaussianBlur(radius=blur_radius))
    height = np.asarray(height_img, dtype=np.float32) / 255.0

    # Sobel derivatives, divided by the kernel's weight so slope_scale has stable
    # meaning across images.  Edge padding makes an otherwise uniform image flat.
    p = np.pad(height, 1, mode="edge")
    dx = (
        p[:-2, 2:] + 2.0 * p[1:-1, 2:] + p[2:, 2:]
        - p[:-2, :-2] - 2.0 * p[1:-1, :-2] - p[2:, :-2]
    ) / 8.0
    dy = (
        p[2:, :-2] + 2.0 * p[2:, 1:-1] + p[2:, 2:]
        - p[:-2, :-2] - 2.0 * p[:-2, 1:-1] - p[:-2, 2:]
    ) / 8.0

    if env_mask is not None:
        active = np.asarray(env_mask.convert("L"), dtype=np.uint8) >= 128
    else:
        active = np.asarray(rgba.getchannel("A"), dtype=np.uint8) > 0
    active = _erode_one(active).astype(np.float32)

    nx = -dx * slope_scale * active
    ny = -dy * slope_scale * active
    nz = np.ones_like(nx)
    length = np.sqrt(nx * nx + ny * ny + nz * nz)
    normal = np.stack((nx / length, ny / length, nz / length), axis=2)
    encoded = np.rint((normal * 0.5 + 0.5) * 255.0).clip(0, 255).astype(np.uint8)
    return Image.fromarray(encoded, "RGB")
