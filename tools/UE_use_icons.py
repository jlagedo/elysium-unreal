"""Export the VtMB use-icon set (the `use_icon`/`locked_icon` enum) as one HUD atlas
the Elysium-Unreal runtime draws over the reticle.

`use_icon`/`locked_icon` on a usable entity index a **72-entry** engine table of
`hud/context_icons/<name>` materials (pointer array at `client.dll:0x2700b4`, see
`docs/entity_io.md`); `use_icon N` selects entry `N-1`, `use_icon 0` = no icon. This
exporter resolves each of the 72 entries' material -> `$basetexture` -> `.tth/.ttz`,
decodes it (128x128 RGBA, alpha = coverage), plus the two reticle frames
(`context_icon_ring` = the idle reticle, `context_icon_back` = the plate behind an icon),
packs them into one atlas, and writes a manifest mapping each enum slot to its atlas cell.

Output is map-independent, so it lands in the global `out/hud/` folder (next to the
per-map `out/<map>/` dirs), read 1:1 by the runtime -- no coordinate space is involved
(2D HUD art), which is why this is a `UE_` writer despite touching no geometry.

Produces (under tools/out/hud/):
  use_icons.png    the packed atlas (RGBA)
  use_icons.json   manifest: cell size, atlas dims, ring/back frames, and the 72
                   enum slots each -> {name, material, atlas rect, normalized UVs}

Source bytes stay the user's install; the output is gitignored and regenerable.

Usage:  python tools/UE_use_icons.py
"""
import os, sys, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import install, vmt, tex_to_png
from install import read
from PIL import Image

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out", "hud")

# The 72-entry use_icon enum, verbatim from docs/entity_io.md (client.dll:0x2700b4).
# `use_icon N` -> ICONS[N-1]; duplicate art (e.g. phonograph 42/43) is intentional --
# the table has genuine duplicate slots, kept so N indexes ICONS directly.
ICONS = [
    "CarryBody", "Hacking", "Intrusion", "Key", "accesscard", "Lootable",       # 1-6
    "Monitor", "Phone", "PhysicsHand", "Portal", "Stakeable", "Switchable",     # 7-12
    "Sewer", "Talk_Female", "Talk_Male", "Nosferatu_Warning", "Use_Bomb",       # 13-17
    "Note", "stealth_succeed", "stealth_chance", "Button_1", "Button_2",        # 18-22
    "Button_3", "Button_4", "Button_5", "Button_6", "Button_7", "Button_8",     # 23-28
    "Button_G", "Button_Locked", "Button_Up", "Button_Down", "stop", "drop",    # 29-34
    "arrowright", "arrowleft", "reeltoreel", "key", "clipboard",                # 35-39
    "printedpapers", "spotlight", "phonograph", "phonograph", "cashregister",   # 40-44
    "giovanbook", "payphone", "webcam", "button_penthouse", "bustopmap",        # 45-49
    "sewermap", "deadbody", "electroniclock", "electroniclocked", "breakable",  # 50-54
    "breakable", "bustopmap", "sewerlines", "door_playerwanted",                # 55-58
    "door_transition", "sewer_transition", "pedestal", "dance_male",            # 59-62
    "dance_female", "switch", "valve", "malkchaos", "malkkey", "malkmind",      # 63-68
    "malkorder", "malksight", "malktime", "push",                               # 69-72
]

# A few enum symbols name a material whose on-disk basename differs (the table stores
# the engine label, not the file). Map enum name (lowercased) -> actual material basename.
ALIASES = {"stakeable": "stakable", "valve": "valvewheel"}

# The two reticle frames -- not selectable use_icon values, drawn around/behind an icon.
FRAMES = {"ring": "context_icon_ring", "back": "context_icon_back"}

COLUMNS = 8  # atlas grid width in cells


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


def decode_icon(idx, name, resolve_inc):
    """Material name (under hud/context_icons/) -> 128x128 RGBA image, or None if the
    install lacks it. Resolves the VMT's $basetexture, then decodes the .tth/.ttz pair."""
    mat = ALIASES.get(name.lower(), name.lower())
    data = read(idx, f"materials/hud/context_icons/{mat}.vmt")
    if not data:
        return None, mat
    m = vmt.parse(data.decode("latin-1"), resolve_inc)
    bt = m.get("basetexture")
    if not bt:
        return None, mat
    tth, ttz = read(idx, f"materials/{bt}.tth"), read(idx, f"materials/{bt}.ttz")
    if not (tth and ttz):
        return None, mat
    return tex_to_png.decode(tth, ttz), mat


def main():
    os.makedirs(OUT, exist_ok=True)
    print("indexing install...")
    idx = install.build_index(dirs=("materials",))
    resolve_inc = resolve_include(idx)

    # Decode each distinct material once; keep first-seen order for a stable atlas.
    cache, cell_order, missing = {}, [], []

    def want(name):
        """Ensure `name`'s art is decoded and cached; return its cache key (material)."""
        img, mat = decode_icon(idx, name, resolve_inc)
        if mat not in cache:
            if img is None:
                missing.append(name)
                img = Image.new("RGBA", (128, 128), (0, 0, 0, 0))  # transparent placeholder
            cache[mat] = img
            cell_order.append(mat)
        return mat

    frame_mat = {slot: want(m) for slot, m in FRAMES.items()}  # frames pack first
    icon_mat = [want(name) for name in ICONS]

    cell = max(im.width for im in cache.values())  # 128 for VtMB
    n = len(cell_order)
    cols = min(COLUMNS, n)
    rows = (n + cols - 1) // cols
    atlas = Image.new("RGBA", (cols * cell, rows * cell), (0, 0, 0, 0))

    rect = {}  # material -> (col, row, x, y)
    for i, mat in enumerate(cell_order):
        c, r = i % cols, i // cols
        x, y = c * cell, r * cell
        atlas.paste(cache[mat], (x, y))
        rect[mat] = (c, r, x, y)

    aw, ah = atlas.size

    def entry(mat):
        c, r, x, y = rect[mat]
        return {"material": mat, "col": c, "row": r, "x": x, "y": y, "w": cell, "h": cell,
                "u0": x / aw, "v0": y / ah, "u1": (x + cell) / aw, "v1": (y + cell) / ah}

    manifest = {
        "atlas": "use_icons.png", "cell": cell, "columns": cols, "rows": rows,
        "atlas_width": aw, "atlas_height": ah,
        # frames: `ring` is the idle reticle, `back` the plate drawn behind an icon.
        "ring": {**entry(frame_mat["ring"])},
        "back": {**entry(frame_mat["back"])},
        # 72 enum slots; runtime: use_icon N -> icons[N-1], use_icon 0 = no icon.
        "icons": [{"n": i + 1, "name": ICONS[i], **entry(icon_mat[i])} for i in range(len(ICONS))],
    }

    atlas.save(os.path.join(OUT, "use_icons.png"))
    with open(os.path.join(OUT, "use_icons.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=1)

    print(f"  {len(ICONS)} enum slots, {n} unique cells ({cols}x{rows}, {aw}x{ah})")
    if missing:
        print(f"  ! {len(missing)} missing (transparent placeholder): {sorted(set(missing))}")
    print(f"wrote use-icon atlas -> {os.path.normpath(OUT)}")


if __name__ == "__main__":
    main()
