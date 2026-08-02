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

from elysium_pipeline.formats import install
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
        rgba.save(destination, format="PNG")


def main(*, force: bool = False, index=None) -> None:
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
    for key in selected:
        relative = Path(key).relative_to("particles")
        destination = out / relative
        data = install.read(idx, key)
        if data is None:
            raise FileNotFoundError(key)
        destination.parent.mkdir(parents=True, exist_ok=True)
        if not force and destination.is_file() and destination.read_bytes() == data:
            cached += 1
        else:
            destination.write_bytes(data)
            copied += 1
        manifest[relative.as_posix()] = {"bytes": len(data), "source": idx[key][0]}
        if relative.as_posix().lower() in RAIN_SLICE_SPRITES:
            png_relative = relative.with_suffix(".png")
            png_destination = out / png_relative
            normalise_rain_sprite(data, png_destination)
            normalized[relative.as_posix()] = png_relative.as_posix()
    (out / "manifest.json").write_text(
        json.dumps({
            "schema": "elysium.particle-mirror",
            "version": 2,
            "files": manifest,
            "normalized_rain_closure": normalized,
        }, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"[particles] {len(selected)} files ({copied} copied, {cached} unchanged) -> {out}")


if __name__ == "__main__":
    main()
