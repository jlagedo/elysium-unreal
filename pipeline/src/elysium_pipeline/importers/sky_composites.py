"""D1 sky projection from six published texture GLBs, owned by the texture stage.

The six 2D units keep their own addresses. This derived cube has a `_Sky` role and
private staging namespace; a genuine cube unit such as `skybox/hav` remains TC_hav.
No install, PNG, Unreal asset, or map-bake reader participates in this projection.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import struct

import numpy as np

from elysium_pipeline.formats.texture_glb.model import TEXTURE_EXTENSION
from elysium_pipeline.formats.unit_contract.container import decode_glb
from elysium_pipeline.importers.texture_cube import face_direction
from elysium_pipeline.importers.texture_dds import (
    Ktx2, VK_FORMATS, build_dds, decode_image, parse_ktx2,
)
from elysium_pipeline.importers.sky_paths import (
    SkyCompositeError, normalize_sky_name, sky_cube_path, sky_material_path,
)

VERSION = "elysium-sky-composite-v1"
STORAGE_PREFIX = "skybox/_composites/"
# K1 x K2: retail R_DrawSkyBox -> Source Y reflection -> D3D cube slices.
# The integer is numpy.rot90's counter-clockwise quarter turns of image content.
SLICES = (("rt", 1), ("lf", -1), ("ft", 2), ("bk", 0), ("up", 1), ("dn", 1))
AXES = ("+X", "-X", "+Y", "-Y", "+Z", "-Z")
MEAN_METHOD = "mip0-upper-z-solid-angle-rec709-pow2.2-v1"


def upper_hemisphere_mean(faces: list[np.ndarray]) -> float:
    """R5.2's actual pow(2.2) mean, retained explicitly rather than changed to IEC sRGB.

    Integer D3D slices retain the old gamma-2.2 mean; native floating slices are
    already linear. Includes only world Z > 0 and ignores alpha as before.
    """
    size = faces[0].shape[0]
    centres = (np.arange(size, dtype=np.float64) + 0.5) / size
    v, u = np.meshgrid(centres, centres, indexing="ij")
    weights = (1.0 + (2*u - 1)**2 + (2*v - 1)**2)**-1.5
    total = weight = 0.0
    for index, pixels in enumerate(faces):
        above = face_direction(index, u, v)[..., 2] > 0
        rgb = pixels[..., :3].astype(np.float64)
        if pixels.dtype == np.uint8:
            rgb = (rgb / 255.0)**2.2
        luminance = rgb @ [0.2126, 0.7152, 0.0722]
        total += float((luminance[above] * weights[above]).sum())
        weight += float(weights[above].sum())
    return total / weight if weight else 0.0


def linear_rgba(pixels: np.ndarray) -> np.ndarray:
    """One-to-one LDR sRGB transfer; native HDR stays linear and is never clamped."""
    if pixels.dtype != np.uint8:
        if not np.isfinite(pixels).all():
            raise SkyCompositeError("sky HDR source contains non-finite samples")
        return pixels.astype("<f4")
    value = pixels.astype(np.float64) / 255.0
    rgb = value[..., :3]
    value[..., :3] = np.where(rgb <= 0.04045, rgb / 12.92, ((rgb + 0.055) / 1.055)**2.4)
    return value.astype("<f4")


def plan_sky(sky: str, sources: dict[str, bytes], *, existing_sidecar=None,
             existing_dds_sha256=None):
    """Return the texture lane's normal DDS plan, with complete composite provenance.

    Every face must have the same square extent and admitted mip chain. Animated
    faces, a cube passed as a 2D face, or signed data are refused, never flattened.
    Mixed BC/uncompressed inputs are legal: all are decoded by the existing lane.
    """
    from elysium_pipeline.importers.textures import _Plan

    sky = normalize_sky_name(sky)
    if set(sources) != {suffix for suffix, _ in SLICES}:
        raise SkyCompositeError(f"{sky}: requires exactly rt, lf, ft, bk, up, dn GLB faces")
    inputs, mapping, chains, native_top = [], [], [], []
    shape = None
    for index, (suffix, turns) in enumerate(SLICES):
        blob = sources[suffix]
        document, binary = decode_glb(blob, f"skybox/{sky}{suffix}")
        extension = document.get("extensions", {}).get(TEXTURE_EXTENSION)
        key = f"skybox/{sky}{suffix}"
        if not isinstance(extension, dict) or extension.get("identity", {}).get("asset") != f"vtmb:texture:{key}":
            raise SkyCompositeError(f"{key}: GLB identity does not match its face path")
        ktx = parse_ktx2(binary)
        dims = extension.get("dimensions", {})
        if (ktx.faces != 1 or ktx.layers > 1 or dims.get("frames", 1) != 1
                or ktx.width != ktx.height or ktx.vk_format == 41):
            raise SkyCompositeError(f"{key}: sky face must be one square colour image, not an array/cube/data field")
        current_shape = (ktx.width, ktx.height, len(ktx.levels))
        if shape is not None and shape != current_shape:
            raise SkyCompositeError(f"{key}: extent/mip chain {current_shape} differs from {shape}; no resampling or mip loss allowed")
        shape = current_shape
        chain = []
        for level in range(len(ktx.levels)):
            size = max(1, ktx.width >> level)
            pixels = decode_image(VK_FORMATS[ktx.vk_format][4], ktx.image(level, 0, 0), size, size)
            rotated = np.ascontiguousarray(np.rot90(pixels, turns))
            if level == 0:
                native_top.append(rotated)
            chain.append(linear_rgba(rotated))
        chains.append(chain)
        inputs.append({"assetId": f"vtmb:texture:{key}", "unitGlb": f"textures/{key}.glb",
                       "unitSha256": hashlib.sha256(blob).hexdigest(),
                       # Carry the entire extension: flags, mip mapping, byte ledger and all
                       # source members survive without inventing a single-unit identity.
                       "texture": extension})
        mapping.append({"unrealFace": AXES[index], "sourceFace": suffix,
                        "quarterTurnsCCW": turns, "assetId": f"vtmb:texture:{key}"})

    size, _, mip_count = shape
    levels = [b"".join(chain[level].tobytes() for chain in chains)
              for level in range(mip_count)]
    dds = build_dds(Ktx2(109, size, size, 0, 6, levels))
    dds_hash = hashlib.sha256(dds).hexdigest()
    recipe = {"settingsVersion": VERSION, "sourceUnits": [row["unitSha256"] for row in inputs],
              "faceMapping": mapping, "meanMethod": MEAN_METHOD,
              "transfer": "uint8-iec-srgb-to-linear; native-float-linear; alpha-linear",
              "compression": "hdr-f32", "srgb": False, "ddsSha256": dds_hash}
    recipe_hash = hashlib.sha256(json.dumps(recipe, sort_keys=True).encode()).hexdigest()
    key = STORAGE_PREFIX + sky
    path = sky_cube_path(sky)
    provenance = {"schemaVersion": "1.0.0", "product": "sky-composite", "skyName": sky,
                  "assetPath": path, "materialPath": sky_material_path(sky),
                  "settingsVersion": VERSION, "recipeSha256": recipe_hash,
                  "sourceUnits": inputs, "faceMapping": mapping,
                  "width": size, "height": size, "faces": 6, "frames": 1,
                  "mipCount": mip_count, "stagedFormat": "rgba32f", "ddsSha256": dds_hash,
                  "sourceMipMd5": [hashlib.md5(level).hexdigest() for level in levels],
                  "upperHemisphereMean": upper_hemisphere_mean(native_top),
                  "meanMethod": MEAN_METHOD, "srgb": False, "compression": "hdr-f32",
                  "transfer": recipe["transfer"],
                  "alphaPolicy": "preserved-in-source; sky-radiance-ignores-alpha"}
    entry = {"assetPath": path, "class": "TextureCube", "product": "sky-composite",
             "skyName": sky, "dds": key + ".dds", "provenance": key + ".provenance.json",
             "unit": f"elysium:sky-composite:{sky}", "twinOf": None, "role": "sky",
             "stagedFormat": "rgba32f", "srgb": False, "compression": "hdr-f32",
             "mipGen": "leave-existing" if mip_count > 1 else "no-mipmaps",
             "filter": "default", "neverStream": True,
             "expected": {"width": size, "height": size, "faces": 6, "slices": 1, "mips": mip_count},
             "recipe": recipe}
    current = (existing_sidecar == provenance and existing_dds_sha256 == dds_hash)
    return _Plan(key, entry["dds"], None if current else dds, [entry],
                 {entry["provenance"]: provenance})


def plan_composites(export_root: Path, stage_root: Path, select: str | None):
    """Discover face sets from the GLB corpus; return plans and named failures.

    The entire skybox directory is the composite selection boundary. A selection
    outside it never rebuilds/prunes skies. Partial face sets are named failures.
    """
    from elysium_pipeline.importers.textures import in_selection, _load_json, _sha256_of

    if not in_selection("skybox", select):
        return [], []
    sky_root = Path(export_root) / "textures" / "skybox"
    names = sorted({p.stem[:-2] for p in sky_root.glob("*.glb")
                    if p.stem[-2:] in {suffix for suffix, _ in SLICES} and len(p.stem) > 2})
    plans, failures = [], []
    for sky in names:
        key = STORAGE_PREFIX + sky
        try:
            sources = {suffix: (sky_root / f"{sky}{suffix}.glb").read_bytes() for suffix, _ in SLICES}
            plans.append(plan_sky(sky, sources,
                                 existing_sidecar=_load_json(Path(stage_root) / (key + ".provenance.json")),
                                 existing_dds_sha256=_sha256_of(Path(stage_root) / (key + ".dds"))))
        except (OSError, ValueError, KeyError, TypeError, struct.error) as error:
            failures.append((key, str(error)))
    return plans, failures
