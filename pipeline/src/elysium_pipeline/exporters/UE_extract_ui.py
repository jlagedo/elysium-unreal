"""Mirror the VtMB UI source -- layouts, schemes, strings and art -- into `$ELYSIUM_EXPORT_ROOT/ui/`.

Roadmap **PL8**. The UI has no classic mode (`docs/project/remaster-direction.md` axis 1), so this
is an extractor of **design intent + source art**, not of a runtime layout engine: the
`.res` trees and the two schemes are copied verbatim as the record of what each screen
contains and how it is grouped, and the art trees are decoded to PNG so the modern
re-skin can reuse the pieces that still carry the game's tone (title lockup, clan
iconography, HUD frames, the menu particle scene).

Two schemes are live and they are **not** interchangeable (RE, `docs/vtmb/vtmb-ui.md`):
`VampireScheme.res` is what `client.dll`'s own UI loads -- the main menu, the character
sheet, the HUD -- and carries the gold `V*` palette; `TrackerScheme.res` skins
`GameUI.dll`'s dialogs. Both are mirrored.

The `.fnt` bitmap atlases are deliberately **not** extracted: they are not the runtime
type. Vector faces live in `Content/Fonts` (`pipeline/src/elysium_pipeline/devtools/fetch_ui_fonts.py`).

Whole-game, not map-scoped -- one mirror, like the script (PL2) and sign (PL5c) copies.
Resolution is patch-first, matching the engine's search order. Output is map-independent
and carries no coordinate space (2D UI art), which is why this is a `UE_` writer despite
touching no geometry.

Produces (under $ELYSIUM_EXPORT_ROOT/ui/):
  resource/<name>.res     every scheme + menu + dialog layout, byte-for-byte
  strings.json            the localized token table (UCS-2 source -> UTF-8 JSON)
  menu/title.png          the title lockup (interface/mainmenu/vtm_title)
  menu/particles/*.txt    mainmenuparticles.txt + every emitter/particle it references
  menu/sprites/*.png      every sprite the particle graph draws
  menu/skybox/*.png       the six MM_Skybox faces
  art/<tree>/<name>.png   the HUD + interface art trees, decoded RGBA
  manifest.json           index: trees, per-file pixel size, and what was missing

Source bytes stay the user's install; the output is gitignored and regenerable.

Usage:
  uv run elysium export bundle ui            # layouts, strings, menu scene, HUD + screen art
  uv run elysium export bundle ui --force    # redo files already present

Inventory-icon inclusion is selected by the grid and all export profiles.
"""
import argparse
import json
import os
import re
import sys

from elysium_pipeline.formats import install, tex_to_png, vmt
from elysium_pipeline.formats.install import read
from elysium_pipeline.paths import export_root

OUT = os.path.join(os.fspath(export_root()), "ui")

# Every `.res` the UI loads. Both schemes are here on purpose -- see the module docstring.
# Multiplayer layouts are included because they exist in the install and the inventory of
# intent should record that they were shipped-but-suppressed, not silently drop them.
RES_FILES = [
    "vampirescheme", "trackerscheme", "trackerscheme-uhd", "vampirece2scheme",
    "gamemenu",
    "newgamedialog", "loadgamedialog", "savegamedialog",
    "dialogoptionsingame", "confirmdialog", "notifydialog", "textentrydialog",
    "contentcontroldialog",
    "optionssubvideo", "optionssubaudio", "optionssubmouse", "optionssubkeyboard",
    "optionssubgameplay", "optionssubvoice", "optionssubvisual", "optionssubadvanced",
    "createmultiplayergamegameplaypage", "createmultiplayergameserverpage",
    "multiplayeradvanceddialog", "multiplayercustomizedialog",
]

STRINGS = "resource/gameui_english.txt"
TITLE = "interface/mainmenu/vtm_title"
FEED_VISION_MASK = "effects/spotlight"
SCENE = "resource/mainmenuparticles.txt"
SKYBOX_FACES = ("ft", "bk", "up", "dn", "lf", "rt")

# Art trees decoded to PNG. `hud/context_icons` is owned by UE_use_icons.py (it packs the
# 72-entry enum into one atlas), so it is excluded here rather than decoded twice.
ART_TREES = [
    "materials/hud",
    "materials/hud/new_ui",
    "materials/hud/new_ui/bloodbar",
    "materials/hud/new_ui/healthbar",
    "materials/hud/nos_indicator",
    "materials/hud/crosshairs",
    "materials/hud/infobar_icons",
    "materials/hud/area_icons",
    "materials/hud/catagory_icons",
    "materials/hud/disciplines",
    "materials/hud/signs",
    "materials/interface/mainmenu",
    "materials/interface/charactermaintenance",
    "materials/interface/pop_ups",
    "materials/interface/tipinfoscreen",
    "materials/interface/widescreen",
    "materials/interface/worldmap",
    "materials/interface/sewermap",
]

INVENTORY_TREES = ["materials/hud/inventory_images", "materials/hud/barter_loot"]

# A particle file names what it spawns; the scene names its emitters. Comment-only lines
# never match, so a commented-out block is skipped exactly as the runtime skips it.
PART_REF = re.compile(r'^\s*(?:particle|emitter)\s+"([^"]+)"', re.I | re.M)
SPRITE_REF = re.compile(r'^\s*sprite\s+"([^"]+)"', re.I | re.M)
# The scene's `default_skybox` names the cubemap stem (materials/skybox/<stem><face>).
SKYBOX_REF = re.compile(r'^\s*default_skybox\s+"([^"]+)"', re.I | re.M)


def safe(name):
    """`interface/Pop_Ups/general` -> `interface_pop_ups_general`."""
    return re.sub(r"[^a-z0-9]+", "_", name.replace("\\", "/").lower()).strip("_")


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


def decode_material(idx, material, resolve_inc):
    """Material path (install-relative, no extension) -> RGBA image, or None."""
    data = read(idx, material + ".vmt")
    base = None
    if data:
        base = vmt.parse(data.decode("latin-1"), resolve_inc).get("basetexture")
    tex = "materials/" + base.replace("\\", "/").lower() if base else material
    tth, ttz = read(idx, tex + ".tth"), read(idx, tex + ".ttz")
    if not tth:
        # Some entries name the texture with no VMT beside it.
        tth, ttz = read(idx, material + ".tth"), read(idx, material + ".ttz")
        if not tth:
            return None
    try:
        return tex_to_png.decode(tth, ttz)
    except Exception:
        return None


def decode_sprite(idx, name, resolve_inc):
    """The menu particle sprites are loose **TGA** under `particles/`, not the `.tth/.ttz`
    pair the rest of the UI art uses -- the particle system reads them directly. Try that
    first, then fall back to the ordinary material path."""
    import io as _io

    from PIL import Image
    key = "particles/" + name.replace("\\", "/").lower()
    for cand in (key + ".tga", key + ".png"):
        data = read(idx, cand)
        if data:
            try:
                return Image.open(_io.BytesIO(data)).convert("RGBA")
            except Exception:
                pass
    return decode_material(idx, "materials/" + name.replace("\\", "/").lower(), resolve_inc)


def write_bytes(path, data, force):
    if not force and os.path.exists(path):
        return False
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)
    return True


def parse_strings(raw):
    """`gameui_english.txt` is a UCS-2 KeyValues file: `"Tokens" { "Key" "Value" ... }`."""
    for enc in ("utf-16", "utf-16-le", "utf-8-sig", "latin-1"):
        try:
            text = raw.decode(enc)
            if text.count('"') > 20:
                break
        except UnicodeDecodeError:
            continue
    else:
        return {}
    out = {}
    for k, v in re.findall(r'"([^"\n]+)"\s+"([^"]*)"', text):
        if k.lower() in ("lang", "language", "tokens"):
            continue
        out[k] = v
    return out


def main(inventory=False, force=False):
    args = argparse.Namespace(inventory=inventory, force=force)

    os.makedirs(OUT, exist_ok=True)
    print("indexing install...")
    idx = install.build_index(dirs=("materials", "resource", "particles"))
    resolve_inc = resolve_include(idx)
    manifest = {"resource": [], "strings": 0, "menu": {}, "art": {}, "missing": []}

    # --- layouts + schemes, verbatim -------------------------------------------------
    wrote = 0
    for stem in RES_FILES:
        data = read(idx, f"resource/{stem}.res")
        if data is None:
            manifest["missing"].append(f"resource/{stem}.res")
            continue
        wrote += write_bytes(os.path.join(OUT, "resource", stem + ".res"), data, args.force)
        manifest["resource"].append(stem)
    print(f"[ui] {len(manifest['resource'])} .res layouts ({wrote} written)")

    # --- localized strings -----------------------------------------------------------
    raw = read(idx, STRINGS)
    if raw:
        table = parse_strings(raw)
        with open(os.path.join(OUT, "strings.json"), "w", encoding="utf-8") as f:
            json.dump(table, f, indent=1, ensure_ascii=False, sort_keys=True)
        manifest["strings"] = len(table)
        print(f"[ui] {len(table)} localized strings")
    else:
        manifest["missing"].append(STRINGS)

    # --- title lockup ----------------------------------------------------------------
    img = decode_material(idx, "materials/" + TITLE, resolve_inc)
    if img:
        os.makedirs(os.path.join(OUT, "menu"), exist_ok=True)
        img.save(os.path.join(OUT, "menu", "title.png"))
        manifest["menu"]["title"] = {"png": "menu/title.png", "w": img.width, "h": img.height}
        print(f"[ui] title lockup {img.width}x{img.height}")
    else:
        manifest["missing"].append(TITLE)

    # --- renderer presentation art ---------------------------------------------------
    # CViewRender::DrawFeedingView samples this exact radial mask. It stays in the generated UI
    # mirror because it is global 2D presentation art, not a map texture and not a runtime layout.
    img = decode_material(idx, "materials/" + FEED_VISION_MASK, resolve_inc)
    if img:
        relative = "effects/feed_spotlight.png"
        destination = os.path.join(OUT, *relative.split("/"))
        os.makedirs(os.path.dirname(destination), exist_ok=True)
        img.save(destination)
        manifest["feed_vision"] = {"png": relative, "w": img.width, "h": img.height}
        print(f"[ui] feed-vision mask {img.width}x{img.height}")
    else:
        manifest["missing"].append(FEED_VISION_MASK)

    # --- the menu particle scene, followed transitively -------------------------------
    scene = read(idx, SCENE)
    sprites, skybox_stem = set(), None
    if scene:
        pending, seen = [SCENE], set()
        while pending:
            key = pending.pop()
            if key in seen:
                continue
            seen.add(key)
            data = read(idx, key)
            if data is None:
                manifest["missing"].append(key)
                continue
            write_bytes(os.path.join(OUT, "menu", "particles", os.path.basename(key)),
                        data, args.force)
            text = data.decode("latin-1")
            for ref in PART_REF.findall(text):
                pending.append(f"particles/{ref.lower()}.txt")
            sprites.update(s.strip() for s in SPRITE_REF.findall(text) if s.strip())
            m = SKYBOX_REF.search(text)
            if m:
                skybox_stem = m.group(1).strip().lower()
        manifest["menu"]["particles"] = sorted(os.path.basename(k) for k in seen)
        print(f"[ui] particle scene: {len(seen)} scripts, {len(sprites)} sprites")

        got = {}
        for sprite in sorted(sprites):
            im = decode_sprite(idx, sprite, resolve_inc)
            if im is None:
                manifest["missing"].append(sprite)
                continue
            stem = safe(sprite) + ".png"
            path = os.path.join(OUT, "menu", "sprites", stem)
            if args.force or not os.path.exists(path):
                os.makedirs(os.path.dirname(path), exist_ok=True)
                im.save(path)
            got[sprite.lower()] = {"png": f"menu/sprites/{stem}", "w": im.width, "h": im.height}
        manifest["menu"]["sprites"] = got
    else:
        manifest["missing"].append(SCENE)

    # --- the menu skybox --------------------------------------------------------------
    if skybox_stem:
        faces = {}
        for face in SKYBOX_FACES:
            im = decode_material(idx, f"materials/skybox/{skybox_stem}{face}", resolve_inc)
            if im is None:
                manifest["missing"].append(f"skybox/{skybox_stem}{face}")
                continue
            path = os.path.join(OUT, "menu", "skybox", f"{face}.png")
            if args.force or not os.path.exists(path):
                os.makedirs(os.path.dirname(path), exist_ok=True)
                im.save(path)
            faces[face] = {"png": f"menu/skybox/{face}.png", "w": im.width, "h": im.height}
        manifest["menu"]["skybox"] = {"stem": skybox_stem, "faces": faces}
        print(f"[ui] menu skybox '{skybox_stem}': {len(faces)}/6 faces")

    # --- art trees ---------------------------------------------------------------------
    trees = ART_TREES + (INVENTORY_TREES if args.inventory else [])
    for tree in trees:
        names = sorted(k for k in idx
                       if k.endswith(".vmt") and os.path.dirname(k) == tree)
        if not names:
            continue
        label = tree[len("materials/"):]
        got = {}
        for key in names:
            material = key[:-4]
            stem = os.path.basename(material) + ".png"
            path = os.path.join(OUT, "art", label, stem)
            if not args.force and os.path.exists(path):
                from PIL import Image
                with Image.open(path) as im:
                    got[os.path.basename(material)] = {"w": im.width, "h": im.height}
                continue
            im = decode_material(idx, material, resolve_inc)
            if im is None:
                manifest["missing"].append(material)
                continue
            os.makedirs(os.path.dirname(path), exist_ok=True)
            im.save(path)
            got[os.path.basename(material)] = {"w": im.width, "h": im.height}
        manifest["art"][label] = got
        print(f"[ui] art/{label}: {len(got)}/{len(names)}")

    with open(os.path.join(OUT, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=1, sort_keys=True)
    if manifest["missing"]:
        print(f"[ui] {len(manifest['missing'])} entries the install did not resolve "
              f"(listed in manifest.json)")
    print(f"  -> {OUT}")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--inventory", action="store_true", help="also decode the item/armor icons")
    ap.add_argument("--force", action="store_true", help="redo files already present")
    ns = ap.parse_args()
    main(inventory=ns.inventory, force=ns.force)
