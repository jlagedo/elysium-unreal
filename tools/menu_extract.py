"""Extract the VtMB VGUI menu resources from the user's install into
`game/content/ui/` (bring-your-own; gitignored). The Godot runtime parses these
at load time — the game's own scheme, fonts, `.res` layouts, and particle scene —
so the menu is a faithful port of the original VGUI2 UI, not a hand-composited
look-alike.

Produces (under game/content/ui/):
  resource/<name>.res  scheme + menu + dialog layouts, copied verbatim (text)
  fonts/<stem>.fnt     bitmap-font metrics, copied verbatim
  fonts/<stem>-pageN.png   decoded glyph atlas (RGBA: white RGB + alpha coverage)
  strings.json         localized #GameUI_* tokens from gameui_english.txt
  skybox/mm_skybox<f>.png  the 6 MM_Skybox faces (menu 3D-scene cubemap)
  sprites/<name>.png   every sprite the particle graph references
  particles/<name>.txt the particle scene and everything it references, verbatim
  title.png            the game title logo (interface/mainmenu/vtm_title)
  theme.mp3            the menu theme (loose install file, not in the VPKs)
  manifest.json        thin index (music filename)

All source bytes stay the user's; nothing here is committed.
"""
import sys, os, io, re, json, shutil
sys.path.insert(0, os.path.dirname(__file__))
import install, tex_to_png, kv
from PIL import Image
from install import read

GAME = install.GAME
OUT = os.path.join(os.path.dirname(__file__), "..", "game", "content", "ui")

# Only the trees the menu reads from, so the walk stays cheap. The search order
# itself (patch before the VPKs) lives in install.py: reading the VPKs alone
# builds a menu the install does not run — the patch shadows trackerscheme.res
# (the scheme that drives every font and colour), the vamp_mainfont pages, the
# title art, and several of the menu's own particle scripts.
ASSET_DIRS = ("materials", "resource", "particles")

# The menu theme is a loose file under the install, not packed in the VPKs.
MUSIC_SRC = os.path.join(GAME, "sound", "music", "vampire_theme.mp3")

# .res layouts copied verbatim: the scheme, the menu, and every dialog the loader
# renders (most inert in M0, but all faithful).
RES_FILES = [
    "trackerscheme", "gamemenu",
    "newgamedialog", "loadgamedialog", "savegamedialog",
    "dialogoptionsingame",
    "optionssubvideo", "optionssubaudio", "optionssubmouse", "optionssubkeyboard",
    "optionssubgameplay", "optionssubvoice", "optionssubvisual", "optionssubadvanced",
    "confirmdialog", "notifydialog", "textentrydialog", "contentcontroldialog",
]

# Font faces the scheme aliases reference (menu + dialogs). Every size variant of
# each face is extracted so any alias resolves.
FONT_FACES = ("vamp_mainfont", "vamp_dialog_base", "vamp_small", "tahoma", "marlett")

SKYBOX_FACES = ("ft", "bk", "up", "dn", "lf", "rt")

# A particle file names the particles it spawns and the sprite it draws; the scene
# names its emitters. Comment-only lines never match, so a commented-out block is
# skipped exactly as the runtime skips it.
PART_REF = re.compile(r'^\s*(?:particle|emitter)\s+"([^"]+)"', re.I | re.M)
SPRITE_REF = re.compile(r'^\s*sprite\s+"([^"]+)"', re.I | re.M)


def build_index():
    return install.build_index(dirs=ASSET_DIRS)


def decode_tex(idx, mat):
    return tex_to_png.decode(read(idx, f"{mat}.tth"), read(idx, f"{mat}.ttz"))


def decode_tga(idx, path):
    return Image.open(io.BytesIO(read(idx, path))).convert("RGBA")


def copy_res(idx):
    d = os.path.join(OUT, "resource"); os.makedirs(d, exist_ok=True)
    for name in RES_FILES:
        key = f"resource/{name}.res"
        if key not in idx:
            print(f"  ! missing {key}"); continue
        data = read(idx, key)
        try:                                   # sanity-check it parses
            kv.parse(data.decode("latin-1"))
        except Exception as e:
            print(f"  ! {name}.res failed to parse: {e}")
        with open(os.path.join(d, f"{name}.res"), "wb") as f:
            f.write(data)
    print(f"  {len(RES_FILES)} .res copied")


def copy_fonts(idx):
    d = os.path.join(OUT, "fonts"); os.makedirs(d, exist_ok=True)
    stems = sorted(
        k[len("materials/fonts/"):-4] for k in idx
        if k.startswith("materials/fonts/") and k.endswith(".fnt")
        and k[len("materials/fonts/"):-4].rsplit("_", 3)[0] in FONT_FACES
    )
    n_png = 0
    for stem in stems:
        with open(os.path.join(d, f"{stem}.fnt"), "wb") as f:
            f.write(read(idx, f"materials/fonts/{stem}.fnt"))
        p = 0
        while f"materials/fonts/{stem}-page{p}.tth" in idx:
            decode_tex(idx, f"materials/fonts/{stem}-page{p}").save(
                os.path.join(d, f"{stem}-page{p}.png"))
            n_png += 1; p += 1
    print(f"  {len(stems)} fonts, {n_png} page atlases")


def write_strings(idx):
    # gameui_english.txt is a flat "KEY" "VALUE" token list (UCS-2). Values carry
    # apostrophes and literal \n, which desync a stateful KeyValues tokenizer, so
    # pull the pairs with a regex over the Tokens block (dropping comment lines).
    txt = read(idx, "resource/gameui_english.txt").decode("utf-16")
    m = re.search(r'"Tokens"\s*\{(.*)\}', txt, re.S | re.I)
    body = m.group(1) if m else txt
    body = "\n".join(l for l in body.splitlines() if not l.lstrip().startswith("//"))
    tokens = dict(re.findall(r'"([^"]+)"\s+"([^"]*)"', body))
    with open(os.path.join(OUT, "strings.json"), "w", encoding="utf-8") as f:
        json.dump(tokens, f, ensure_ascii=False, indent=0)
    print(f"  {len(tokens)} strings")


def copy_skybox(idx):
    d = os.path.join(OUT, "skybox"); os.makedirs(d, exist_ok=True)
    for face in SKYBOX_FACES:
        decode_tex(idx, f"materials/skybox/mm_skybox{face}").save(
            os.path.join(d, f"mm_skybox{face}.png"))
    print(f"  {len(SKYBOX_FACES)} skybox faces")


def copy_sprites(idx, names):
    d = os.path.join(OUT, "sprites"); os.makedirs(d, exist_ok=True)
    n = 0
    for name in sorted(names):
        key = f"particles/{name}.tga"
        if key not in idx:
            print(f"  ! missing sprite {key}"); continue
        decode_tga(idx, key).save(os.path.join(d, f"{name}.png")); n += 1
    print(f"  {n} sprites")


def copy_particles(idx):
    """Copy the menu scene and every particle it transitively references.

    Walk the graph the way the runtime does — the scene names emitters, each
    emitter's spawn blocks name more particles, each particle names a sprite.
    Globbing `m_*.txt` instead misses whatever an override adds under another
    name: the Unofficial Patch's m_clans_emmiter.txt spawns `bloodlinestemp` and
    `vtm_glowtemp`, the floating BLOODLINES wordmark and glow.

    Returns the set of sprite names the graph reaches.
    """
    d = os.path.join(OUT, "particles"); os.makedirs(d, exist_ok=True)
    seen, sprites, queue = {}, set(), ["mainmenuparticles"]
    while queue:
        name = queue.pop().lower()
        if name in seen:
            continue
        key = next((k for k in (f"particles/{name}.txt", f"resource/{name}.txt")
                    if k in idx), None)
        if key is None:
            print(f"  ! missing particles/{name}.txt"); seen[name] = None; continue
        data = read(idx, key)
        seen[name] = data
        text = data.decode("latin-1")
        queue += PART_REF.findall(text)
        sprites.update(s.lower() for s in SPRITE_REF.findall(text))

    for name, data in seen.items():
        if data is None:
            continue
        with open(os.path.join(d, f"{name}.txt"), "wb") as f:
            f.write(data)
    print(f"  {sum(v is not None for v in seen.values())} particle files, "
          f"{len(sprites)} sprites referenced")
    return sprites


def main():
    os.makedirs(OUT, exist_ok=True)
    print("indexing install…")
    idx = build_index()

    print("resources…");  copy_res(idx)
    print("fonts…");      copy_fonts(idx)
    print("strings…");    write_strings(idx)
    print("skybox…");     copy_skybox(idx)
    print("particles…");  sprites = copy_particles(idx)
    print("sprites…");    copy_sprites(idx, sprites)

    # The menu draws one title image. The wordmark is not composited on top of it:
    # on a patched install vtm_title already contains it, and BloodLines2 is a
    # *particle* sprite (bloodlinestemp.txt floats it through the backdrop), which
    # the particle graph above already pulls into sprites/.
    print("title art…")
    decode_tex(idx, "materials/interface/mainmenu/vtm_title").save(os.path.join(OUT, "title.png"))

    have_music = os.path.exists(MUSIC_SRC)
    if have_music:
        shutil.copyfile(MUSIC_SRC, os.path.join(OUT, "theme.mp3"))

    with open(os.path.join(OUT, "manifest.json"), "w") as f:
        json.dump({"music": "theme.mp3" if have_music else None}, f, indent=2)
    print(f"wrote menu assets -> {os.path.normpath(OUT)}")


if __name__ == "__main__":
    main()
