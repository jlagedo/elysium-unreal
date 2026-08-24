"""Patch-first mirror of VtMB particle definitions and sprites.

The mirror is intentionally raw and complete; individual map exporters compile
only the definitions they actually reference into their versioned weather
sidecars.  Output is generated below ``$ELYSIUM_EXPORT_ROOT/particles`` and is
therefore bring-your-own-game, ignored, and regenerable.
"""

from __future__ import annotations

import json
from io import BytesIO
from pathlib import Path

from PIL import Image

from elysium_pipeline.paths import export_root


# The raw mirror remains complete.  Only this resolved sm_hub_1 dependency closure gets a
# normalized derivative for UE import; UE 5.8's Interchange TGA path accepts these small Source
# sprites but produces texture resources that sample black at runtime.
RAIN_SLICE_SPRITES = (
    "d_targetblob.tga",
    "dropletfast.tga",
    "fortituderings.tga",
    "furball.tga",
)


def normalise_rain_sprite(data: bytes, destination: Path) -> None:
    with Image.open(BytesIO(data)) as source:
        rgba = source.convert("RGBA")
        if rgba.width <= 0 or rgba.height <= 0:
            raise ValueError(f"particle sprite has invalid dimensions: {destination}")
        # UE 5.8 Interchange accepts these legacy RGBA files but the imported alpha resource
        # samples as zero. The rain material only needs the authored mask, so encode the exact
        # source alpha into RGB and make the derivative opaque. The raw patch-first TGA remains
        # beside it for provenance and future decoder work.
        alpha = rgba.getchannel("A")
        rgba = Image.merge("RGBA", (alpha, alpha, alpha, Image.new("L", rgba.size, 255)))
        destination.parent.mkdir(parents=True, exist_ok=True)
        # zlib level 1: gitignored intermediates, so encode speed outranks disk size.
        rgba.save(destination, format="PNG", compress_level=1)


def normalise_sprite(data: bytes, destination: Path) -> None:
    """Straight TGA -> PNG for every other sprite, colour and alpha intact.

    Same reason as the rain derivative — UE 5.8's Interchange TGA path imports these legacy files
    but samples their alpha as zero — but not the same transform: a general sprite is tinted
    (blood, embers, glass), so folding alpha into RGB the way the mask-only rain material wants
    would throw its colour away.
    """

    with Image.open(BytesIO(data)) as source:
        rgba = source.convert("RGBA")
        if rgba.width <= 0 or rgba.height <= 0:
            raise ValueError(f"particle sprite has invalid dimensions: {destination}")
        destination.parent.mkdir(parents=True, exist_ok=True)
        # zlib level 1: gitignored intermediates, so encode speed outranks disk size.
        rgba.save(destination, format="PNG", compress_level=1)


def main(*, force: bool = False, index=None) -> None:
    from elysium_pipeline.formats import install

    idx = index if index is not None else install.build_index(dirs=("particles",))
    out = export_root() / "particles"
    out.mkdir(parents=True, exist_ok=True)
    selected = sorted(
        key for key in idx
        if key.startswith("particles/") and key.lower().endswith((".txt", ".tga"))
    )
    copied = cached = 0
    manifest = {}
    normalized = {}
    sprites = {}
    for key in selected:
        relative = Path(key).relative_to("particles")
        destination = out / relative
        data = install.read(idx, key)
        if data is None:
            raise FileNotFoundError(key)
        destination.parent.mkdir(parents=True, exist_ok=True)
        fresh = force or not (destination.is_file() and destination.read_bytes() == data)
        if fresh:
            destination.write_bytes(data)
            copied += 1
        else:
            cached += 1
        manifest[relative.as_posix()] = {"bytes": len(data), "source": idx[key][0]}
        posix = relative.as_posix()
        if posix.lower().endswith(".tga"):
            png_relative = relative.with_suffix(".png")
            png_destination = out / png_relative
            # The PNG is derived from the .tga, so a source that was (re)copied this run
            # re-normalises; an unchanged source keeps the PNG it already has.
            if posix.lower() in RAIN_SLICE_SPRITES:
                if fresh or not png_destination.is_file():
                    normalise_rain_sprite(data, png_destination)
                normalized[posix] = png_relative.as_posix()
            else:
                if fresh or not png_destination.is_file():
                    normalise_sprite(data, png_destination)
                sprites[posix] = png_relative.as_posix()
    manifest_text = json.dumps({
        "schema": "elysium.particle-mirror",
        "version": 3,
        "files": manifest,
        "normalized_rain_closure": normalized,
        "normalized_sprites": sprites,
    }, indent=2, sort_keys=True) + "\n"
    manifest_path = out / "manifest.json"
    # Skip a byte-identical rewrite: an unchanged manifest keeps its mtime, so downstream
    # freshness checks stay warm (same pattern as `UE_extract_corpus._write`).
    try:
        unchanged = manifest_path.read_text(encoding="utf-8") == manifest_text
    except OSError:
        unchanged = False
    if not unchanged:
        manifest_path.write_text(manifest_text, encoding="utf-8")
    print(f"[particles] {len(selected)} files ({copied} copied, {cached} unchanged) -> {out}")


if __name__ == "__main__":
    main()
