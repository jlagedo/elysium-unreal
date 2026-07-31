"""Copy the game's sign/popup definitions + decode their background art into $ELYSIUM_EXPORT_ROOT/signs.

P4 4.10 (PL5c) asset-delivery step. A `game_sign` / `prop_sign` entity names a
`definition_file` -- `vdata/Signs/<name>.txt`, a Source KeyValues panel rooted `SignData`
that the engine draws as a full-screen window (`CSignUI` in `client.dll`). This step
mirrors those definitions **verbatim** (no parse, no transcode) and decodes every
`BackgroundImage` material they reference into a PNG the runtime draws with Canvas.

Whole-game, not map-scoped -- one mirror, like the script/dialogue copy (PL2). Resolution
is patch-first (patch loose > retail loose > VPK), matching the engine's search order.

Output is map-independent, so it lands in the global `$ELYSIUM_EXPORT_ROOT/signs/` folder (next to the
per-map `$ELYSIUM_EXPORT_ROOT/<map>/` dirs), read 1:1 by the runtime. No coordinate space is involved (2D
HUD art), which is why this is a `UE_` writer despite touching no geometry.

Definition names are written **lowercased** (the install index is case-folded, and a
`definition_file` keyvalue's authored case varies); the runtime lowercases its lookup to
match, so a packaged case-sensitive filesystem behaves like Windows.

Produces (under $ELYSIUM_EXPORT_ROOT/signs/):
  <name>.txt          each sign definition, byte-for-byte
  tex/<safe>.png      each referenced BackgroundImage material, RGBA
  backgrounds.json    manifest: material name -> png filename + pixel size, so the
                      runtime never has to replicate the safe-name rule

Source bytes stay the user's install; the output is gitignored and regenerable.

Usage:
  uv run elysium export bundle signs            # copy definitions + decode backgrounds
  uv run elysium export bundle signs --force    # redo files already present
"""
import json
import os
import re
import sys

from elysium_pipeline.formats import install, tex_to_png, vmt
from elysium_pipeline.formats.install import read
from elysium_pipeline.paths import export_root

OUT = os.path.join(os.fspath(export_root()), "signs")

SIGN_DIR = "vdata/signs/"

# `"Name" "<material>"` inside a BackgroundImage block. The definitions are small and
# regular, so a targeted scan beats standing up the whole KeyValues parser offline -- the
# runtime does the real parse.
NAME_KEY = re.compile(r'"Name"\s+"([^"]*)"', re.I)


def safe_name(material):
    """`interface/Pop_Ups/general` -> `interface_pop_ups_general` (a flat, portable stem)."""
    return re.sub(r"[^a-z0-9]+", "_", material.replace("\\", "/").lower()).strip("_")


def resolve_include(idx):
    """A `fn(include_path)->vmt_text` for vmt.parse's patch-shader follow."""
    def f(inc):
        key = inc.replace("\\", "/").lower()
        if not key.endswith(".vmt"):
            key += ".vmt"
        if not key.startswith("materials/"):
            key = "materials/" + key
        data = read(idx, key)
        return data.decode("latin-1") if data else None
    return f


def collect_definitions(idx):
    """{dest filename (lowercased) -> install key} for every `vdata/Signs/*.txt`."""
    picked = {}
    for key in idx:
        if key.startswith(SIGN_DIR) and key.endswith(".txt"):
            picked[key[len(SIGN_DIR):]] = key
    return picked


def decode_background(idx, material, resolve_inc):
    """Material name -> RGBA image, or None when the install lacks it. The VMT is an
    UnlitGeneric whose $basetexture names the .tth/.ttz pair; a few definitions name the
    texture directly, so fall back to the material name itself."""
    key = "materials/" + material.replace("\\", "/").lower()
    data = read(idx, key + ".vmt")
    base = None
    if data:
        base = vmt.parse(data.decode("latin-1"), resolve_inc).get("basetexture")
    tex = "materials/" + (base if base else material.replace("\\", "/").lower())
    tth, ttz = read(idx, tex + ".tth"), read(idx, tex + ".ttz")
    if not tth:
        return None
    # Recompiled art stores every mip inline in the .tth with no .ttz sibling.
    return tex_to_png.decode(tth, ttz)


def main(force=False):
    os.makedirs(os.path.join(OUT, "tex"), exist_ok=True)
    print("indexing install...")
    idx = install.build_index(dirs=("materials", "vdata"))
    resolve_inc = resolve_include(idx)

    # --- definitions, verbatim -------------------------------------------------------
    picked = collect_definitions(idx)
    written = cached = 0
    materials = set()
    for name in sorted(picked):
        data = read(idx, picked[name])
        if data is None:
            continue
        for m in NAME_KEY.finditer(data.decode("latin-1")):
            if m.group(1).strip():
                materials.add(m.group(1).strip())
        dest = os.path.join(OUT, name)
        if not force and os.path.exists(dest):
            cached += 1
            continue
        with open(dest, "wb") as f:
            f.write(data)
        written += 1
    print(f"[signs] {len(picked)} definitions ({written} copied, {cached} already present)")

    # --- background art --------------------------------------------------------------
    manifest, missing = {}, []
    for material in sorted(materials):
        stem = safe_name(material)
        png = os.path.join(OUT, "tex", stem + ".png")
        if not force and os.path.exists(png):
            from PIL import Image
            with Image.open(png) as im:
                manifest[material.lower()] = {"png": stem + ".png", "w": im.width, "h": im.height}
            continue
        img = decode_background(idx, material, resolve_inc)
        if img is None:
            missing.append(material)
            continue
        img.save(png)
        manifest[material.lower()] = {"png": stem + ".png", "w": img.width, "h": img.height}

    with open(os.path.join(OUT, "backgrounds.json"), "w", encoding="utf-8") as f:
        json.dump({"backgrounds": manifest}, f, indent=1, sort_keys=True)

    print(f"[signs] {len(manifest)} background materials decoded -> tex/")
    if missing:
        print(f"  ! {len(missing)} unresolved: {sorted(missing)}")
    print(f"wrote signs -> {os.path.normpath(OUT)}")


if __name__ == "__main__":
    main(force="--force" in sys.argv[1:])
