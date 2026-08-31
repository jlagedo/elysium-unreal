"""Cubemap face order for the texture import lane, and the seam check that accepts it.

A texture unit stores a cubemap's six faces in glTF order and orientation
(`seam_map_texture.md` → "faces"): the exporter permuted Source's faces by
`SOURCE_TO_GLTF_FACE_ORDER` and rotated each by `SOURCE_TO_GLTF_FACE_TRANSFORMS`, and it recorded
both per face in `faces[]`. Unreal samples a `UTextureCube` through the plain D3D face table, which
is also the layout VtMB's env cubemaps were authored for -- the legacy bake imports them in VTF
order with no rotation (`tex_to_png.cubemap_dds`) and the material owns the one handedness
correction. So the lane's whole job is to *undo* the glTF step: face `s` of the DDS is the KTX2
face whose row names `sourceFace == s`, rotated by the inverse of that row's `transform`.

`seam_error` is the reference-free acceptance: across a cube's twelve edges the texels on either
side must continue into each other. The emitted orientation is right when it scores lower than
every other per-face rotation; a permutation or rotation slip breaks continuity sharply.
"""

from __future__ import annotations

import numpy as np

from elysium_pipeline.formats.texture_glb.decode import (
    FACE_NAMES,
    FORMATS,
    SOURCE_TO_GLTF_FACE_ORDER,
    SOURCE_TO_GLTF_FACE_TRANSFORMS,
    transform_cubemap_face,
)
from elysium_pipeline.importers.texture_dds import Ktx2, TextureDdsError

INVERSE_TRANSFORM = {
    "identity": "identity",
    "rotate-cw": "rotate-ccw",
    "rotate-ccw": "rotate-cw",
    "rotate-180": "rotate-180",
}

#: Source/VTF face index -> the D3D axis it is, which is the DDS slot it lands in.
SOURCE_FACE_NAMES = ("+X", "-X", "+Y", "-Y", "+Z", "-Z")


def format_info(vk_format: int):
    for info in FORMATS.values():
        if info.vk_format == vk_format:
            return info
    raise TextureDdsError(f"vkFormat {vk_format} has no source format")


def face_rows(extension: dict) -> list[dict]:
    """The unit's `faces[]`, or the exporter's constants when a unit omits them."""

    rows = extension.get("faces") or []
    if len(rows) == 6 and all(isinstance(row, dict) for row in rows):
        return rows
    return [
        {"ktxFace": index, "name": FACE_NAMES[index],
         "sourceFace": SOURCE_TO_GLTF_FACE_ORDER[index],
         "transform": SOURCE_TO_GLTF_FACE_TRANSFORMS[index]}
        for index in range(6)
    ]


def unreal_faces(ktx: Ktx2, rows: list[dict]) -> tuple[list[list[bytes]], list[dict]]:
    """`(faces, mapping)`: six per-level image lists in DDS order, and the mapping applied.

    `faces[s][level]` is the source face `s` in its source orientation. `mapping` is one row per
    DDS face: `unrealFace`, `ktxFace`, `sourceFace`, `transform` (the inverse rotation applied).
    """

    if ktx.faces != 6:
        raise TextureDdsError("not a cubemap")
    info = format_info(ktx.vk_format)
    by_source: dict[int, dict] = {}
    for row in rows:
        source = row.get("sourceFace")
        if not isinstance(source, int) or not 0 <= source < 6 or source in by_source:
            raise TextureDdsError(f"faces[] does not name each source face once: {rows}")
        by_source[source] = row
    if len(by_source) != 6:
        raise TextureDdsError("faces[] does not cover six source faces")
    faces: list[list[bytes]] = []
    mapping: list[dict] = []
    for source in range(6):
        row = by_source[source]
        ktx_face = row.get("ktxFace")
        transform = row.get("transform")
        if not isinstance(ktx_face, int) or not 0 <= ktx_face < 6 or transform not in INVERSE_TRANSFORM:
            raise TextureDdsError(f"invalid faces[] row {row}")
        inverse = INVERSE_TRANSFORM[transform]
        chain = []
        for level in range(len(ktx.levels)):
            width, height = max(1, ktx.width >> level), max(1, ktx.height >> level)
            chain.append(transform_cubemap_face(ktx.image(level, 0, ktx_face), width, height,
                                                info, inverse))
        faces.append(chain)
        mapping.append({"unrealFace": SOURCE_FACE_NAMES[source], "ktxFace": ktx_face,
                        "sourceFace": source, "transform": inverse})
    return faces, mapping


# --- D3D cube geometry and the seam check --------------------------------------------------------


def face_direction(face: int, u: np.ndarray, v: np.ndarray) -> np.ndarray:
    """World directions `(..., 3)` for texel coordinates `u, v` in [0, 1] on a D3D cube face."""

    s, t = 2.0 * u - 1.0, 2.0 * v - 1.0
    one = np.ones_like(s)
    table = (
        (one, -t, -s),     # +X
        (-one, -t, s),     # -X
        (s, one, t),       # +Y
        (s, -one, -t),     # -Y
        (s, -t, one),      # +Z
        (-s, -t, -one),    # -Z
    )
    x, y, z = table[face]
    return np.stack([x, y, z], axis=-1)


def direction_face_uv(direction: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """The D3D face each direction `(..., 3)` falls on, and its `u, v` in [0, 1]."""

    x, y, z = direction[..., 0], direction[..., 1], direction[..., 2]
    ax, ay, az = np.abs(x), np.abs(y), np.abs(z)
    face = np.where(
        (ax >= ay) & (ax >= az), np.where(x >= 0, 0, 1),
        np.where(ay >= az, np.where(y >= 0, 2, 3), np.where(z >= 0, 4, 5)),
    )
    ma = np.maximum(np.maximum(ax, ay), az)
    sc = np.select([face == 0, face == 1, face == 2, face == 3, face == 4, face == 5],
                   [-z, z, x, x, x, -x])
    tc = np.select([face == 0, face == 1, face == 2, face == 3, face == 4, face == 5],
                   [-y, -y, z, -z, -y, -y])
    return face, (sc / ma + 1.0) / 2.0, (tc / ma + 1.0) / 2.0


def rasterize_cube(size: int, shade) -> list[np.ndarray]:
    """Six `(size, size, 4)` uint8 faces of the function `shade(directions) -> (..., 4)` floats."""

    centres = (np.arange(size) + 0.5) / size
    v, u = np.meshgrid(centres, centres, indexing="ij")
    faces = []
    for face in range(6):
        direction = face_direction(face, u, v)
        direction = direction / np.linalg.norm(direction, axis=-1, keepdims=True)
        faces.append(np.clip(np.rint(shade(direction) * 255.0), 0, 255).astype(np.uint8))
    return faces


def seam_error(faces: list[np.ndarray]) -> float:
    """Mean absolute texel step across the twelve cube edges, for six `(n, n, 4)` faces.

    Each border texel is pushed a third of a texel outward past its edge; where that lands on the
    neighbouring face is where continuity says the same colour should be.
    """

    size = faces[0].shape[0]
    centres = (np.arange(size) + 0.5) / size
    step = 1.0 / size
    total = 0.0
    count = 0
    for face in range(6):
        image = faces[face].astype(np.int32)
        for edge in range(4):
            if edge == 0:      # top row, pushed up (v < 0)
                u, v, du, dv = centres, np.full(size, 0.5 / size), 0.0, -step
                texels = image[0, :, :]
            elif edge == 1:    # bottom row
                u, v, du, dv = centres, np.full(size, 1 - 0.5 / size), 0.0, step
                texels = image[size - 1, :, :]
            elif edge == 2:    # left column
                u, v, du, dv = np.full(size, 0.5 / size), centres, -step, 0.0
                texels = image[:, 0, :]
            else:              # right column
                u, v, du, dv = np.full(size, 1 - 0.5 / size), centres, step, 0.0
                texels = image[:, size - 1, :]
            direction = face_direction(face, u + du, v + dv)
            other, ou, ov = direction_face_uv(direction)
            col = np.clip((ou * size).astype(int), 0, size - 1)
            row = np.clip((ov * size).astype(int), 0, size - 1)
            neighbour = np.stack([faces[int(f)][r, c] for f, r, c in zip(other, row, col)]).astype(np.int32)
            total += float(np.abs(neighbour - texels).mean())
            count += 1
    return total / count
