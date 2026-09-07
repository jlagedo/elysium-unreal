# Imports the UI typeface set (Content/Fonts/*.ttf) into generated local `UFontFace` assets under
# /Game/ElysiumGenerated/UI/Fonts. Roadmap 8.6; the type system is "Nocturne" (docs/architecture/ui-architecture.md):
# Spectral SC for small-caps labels, Spectral for body copy, Inter for data and numerals, plus
# Terminus (TTF) for the computer-terminal console.
#
# Why font *faces* and not one composite UFont: UE 5.8's editor Python exposes `UFontFace`
# fully, but `unreal.Font` carries no `composite_font` property and `Typeface`/`TypefaceEntry`/
# `FontData` are not exposed at all -- a composite UFont cannot be authored from Python. Slate
# takes a `UFontFace` directly in an `FSlateFontInfo`, so the C++ style layer
# (`ElysiumUIStyle.{h,cpp}`) composes role+weight -> face itself. Same engine-native result:
# real assets that cook and stream, no loose TTF read at draw time.
#
# `loading_policy = INLINE` keeps the glyph data in the asset (these are UI faces, always
# resident, and a streamed face pops on first draw). `hinting = DEFAULT` lets FreeType use the
# face's own hinting -- Spectral and Inter are both well hinted for screen sizes.
#
# The licensed .ttf sources are tracked under Content/Fonts.
#
# Importing a `UFontFace` flushes Slate's font cache, so this generator runs as the
# Slate-enabled policy-content pass coordinated by ``elysium``, outside the
# commandlet-only `build_content.py` pass.
import os

import unreal
from pipeline.unreal import _bootstrap  # noqa: E402
from elysium_pipeline import mounts

PKG = mounts.UI_FONTS
SRC = os.path.join(_bootstrap.REPO, "Content", "Fonts")

# (ttf filename, asset name). Asset names are FF_<Family>_<Weight> so the C++ side can build
# the path from a role + weight without a table lookup per face.
FACES = [
    ("SpectralSC-Regular.ttf",  "FF_SpectralSC_Regular"),
    ("SpectralSC-SemiBold.ttf", "FF_SpectralSC_SemiBold"),
    ("Spectral-Regular.ttf",    "FF_Spectral_Regular"),
    ("Spectral-Italic.ttf",     "FF_Spectral_Italic"),
    ("Spectral-SemiBold.ttf",   "FF_Spectral_SemiBold"),
    ("Inter-Regular.ttf",       "FF_Inter_Regular"),
    ("Inter-SemiBold.ttf",      "FF_Inter_SemiBold"),
    # The computer-terminal console face (docs/architecture/computer-terminal-architecture.md 6.4).
    # Pixel-derived outlines: the C++ side raises the SDF ppem on this role so the stair corners
    # survive Slate's distance-field rasterizer.
    ("TerminusTTF-Regular.ttf", "FF_TerminusTTF_Regular"),
    ("TerminusTTF-Bold.ttf",    "FF_TerminusTTF_Bold"),
]


def main():
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    src_dir = os.path.normpath(SRC)
    made = kept = 0
    missing = []
    failed = []

    for ttf, asset in FACES:
        path = os.path.join(src_dir, ttf)
        if not os.path.exists(path):
            missing.append(ttf)
            continue

        obj_path = "%s/%s.%s" % (PKG, asset, asset)
        if unreal.EditorAssetLibrary.does_asset_exist(obj_path):
            # Idempotent: re-point the existing asset at its source and re-save, so a replaced
            # .ttf is picked up without needing the asset deleted by hand.
            face = unreal.EditorAssetLibrary.load_asset(obj_path)
            if face and face.get_editor_property("source_filename") == path:
                kept += 1
                continue

        task = unreal.AssetImportTask()
        task.filename = path
        task.destination_path = PKG
        task.destination_name = asset
        task.automated = True
        task.replace_existing = True
        task.save = True
        task.factory = unreal.FontFileImportFactory()
        tools.import_asset_tasks([task])

        face = unreal.EditorAssetLibrary.load_asset(obj_path)
        if not face:
            unreal.log_error("[make_ui_fonts] import produced no asset: %s" % asset)
            failed.append(asset)
            continue
        face.set_editor_property("loading_policy", unreal.FontLoadingPolicy.INLINE)
        face.set_editor_property("hinting", unreal.FontHinting.DEFAULT)
        unreal.EditorAssetLibrary.save_asset(obj_path)
        made += 1

    if missing:
        unreal.log_error(
            "[make_ui_fonts] %d source .ttf missing from Content/Fonts (%s) -- "
            "run: uv run elysium deps sync"
            % (len(missing), ", ".join(missing)))

    unreal.log("[make_ui_fonts] %d imported, %d already current, %d missing -> %s"
               % (made, kept, len(missing), PKG))
    if missing or failed:
        raise RuntimeError(
            "font generation incomplete: %d missing source(s), %d failed import(s)"
            % (len(missing), len(failed)))


main()
