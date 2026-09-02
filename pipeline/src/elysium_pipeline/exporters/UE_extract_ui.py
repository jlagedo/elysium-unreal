"""Mirror the VtMB UI source -- layouts, schemes and strings -- into `$ELYSIUM_EXPORT_ROOT/ui/`.

Roadmap **PL8**. The UI has no classic mode (`docs/project/reconstruction-direction.md` axis 1), so this
is an extractor of **design intent**, not of a runtime layout engine: the `.res` trees and the
two schemes are copied verbatim as the record of what each screen contains and how it is
grouped, and the localized string table is the one thing the runtime reads.

Two schemes are live and they are **not** interchangeable (RE, `docs/vtmb/vtmb-ui.md`):
`VampireScheme.res` is what `client.dll`'s own UI loads -- the main menu, the character
sheet, the HUD -- and carries the gold `V*` palette; `TrackerScheme.res` skins
`GameUI.dll`'s dialogs. Both are mirrored.

The `.fnt` bitmap atlases are deliberately **not** extracted: they are not the runtime
type. Vector faces live in `Content/Fonts` (`pipeline/src/elysium_pipeline/devtools/fetch_ui_fonts.py`).

**No art** (R6.6, `docs/architecture/ui-architecture.md` -> "9. Art from assets"). The HUD and
interface trees, the title lockup, the feed-vision mask, the menu particle sprites and the
`MM_Skybox` faces this exporter used to decode to PNG are the texture lane's `T_` assets
(`uv run elysium import textures`), and every screen draws those; nothing reads `ui/art`,
`ui/menu` or `ui/effects` any more, so they are no longer written.

Whole-game, not map-scoped -- one mirror, like the script (PL2) and sign (PL5c) copies.
Resolution is patch-first, matching the engine's search order.

Produces (under $ELYSIUM_EXPORT_ROOT/ui/):
  resource/<name>.res     every scheme + menu + dialog layout, byte-for-byte
  strings.json            the localized token table (UCS-2 source -> UTF-8 JSON)
  manifest.json           index: the layouts written, the string count, and what was missing

Source bytes stay the user's install; the output is gitignored and regenerable.

Usage:
  uv run elysium export bundle ui            # layouts + strings
  uv run elysium export bundle ui --force    # redo files already present
"""
import argparse
import json
import os
import re
import sys

from elysium_pipeline.formats import install
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


def main(force=False, index=None):
    args = argparse.Namespace(force=force)

    os.makedirs(OUT, exist_ok=True)
    # A shared full install index is a superset whose extra loose dirs fall outside every
    # prefix this exporter filters on, so it resolves identically to the scoped build.
    if index is None:
        print("indexing install...")
        index = install.build_index(dirs=("resource",))
    idx = index
    manifest = {"resource": [], "strings": 0, "missing": []}

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

    with open(os.path.join(OUT, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=1, sort_keys=True)
    if manifest["missing"]:
        print(f"[ui] {len(manifest['missing'])} entries the install did not resolve "
              f"(listed in manifest.json)")
    print(f"wrote ui -> {os.path.normpath(OUT)}")


if __name__ == "__main__":
    main(force="--force" in sys.argv[1:])
