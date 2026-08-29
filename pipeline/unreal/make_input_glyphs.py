"""Import the tracked Kenney input prompts as generated CommonInput textures.

The CC0 PNG files are the reviewable source.  Unreal textures are local policy packages under
``/Game/ElysiumGenerated/Input/Glyphs/Kenney`` and remain ignored like the generated Enhanced Input assets beside
them.
"""

from pathlib import Path
import struct

import unreal

from pipeline.unreal import _bootstrap  # noqa: F401, E402
from elysium_pipeline import mounts

SOURCE_ROOT = Path(unreal.Paths.project_content_dir()) / "InputPrompts" / "Kenney"
PACKAGE_ROOT = mounts.INPUT + "/Glyphs/Kenney"
GLYPHS = {
    "Keyboard": (
        "keyboard_e.png",
        "keyboard_enter.png",
        "keyboard_escape.png",
    ),
    "PlayStation": (
        "playstation_button_cross.png",
        "playstation_button_circle.png",
        "playstation_button_square.png",
        "playstation_button_triangle.png",
        "playstation_trigger_l1.png",
        "playstation_trigger_r1.png",
        "playstation_trigger_l2.png",
        "playstation_trigger_r2.png",
        "playstation_button_l3.png",
        "playstation_button_r3.png",
        "playstation_stick_l.png",
        "playstation_stick_r.png",
        "playstation_dpad_up.png",
        "playstation_dpad_down.png",
        "playstation_dpad_left.png",
        "playstation_dpad_right.png",
        "playstation5_touchpad_press.png",
        "playstation5_button_options.png",
        "playstation5_button_create.png",
        "playstation5_button_mute.png",
    ),
    "Xbox": (
        "xbox_button_a.png",
        "xbox_button_b.png",
        "xbox_button_x.png",
        "xbox_button_y.png",
        "xbox_lb.png",
        "xbox_rb.png",
        "xbox_lt.png",
        "xbox_rt.png",
        "xbox_ls.png",
        "xbox_rs.png",
        "xbox_stick_l.png",
        "xbox_stick_r.png",
        "xbox_dpad_up.png",
        "xbox_dpad_down.png",
        "xbox_dpad_left.png",
        "xbox_dpad_right.png",
        "xbox_button_view.png",
        "xbox_button_menu.png",
    ),
}


def asset_name(filename):
    return "T_Kenney_" + Path(filename).stem


def import_glyph(family, filename):
    source = SOURCE_ROOT / family / filename
    if not source.is_file():
        raise RuntimeError("tracked Kenney glyph is missing: %s" % source)
    header = source.read_bytes()[:24]
    if len(header) != 24 or header[:8] != b"\x89PNG\r\n\x1a\n":
        raise RuntimeError("tracked Kenney glyph is not a PNG: %s" % source)
    width, height = struct.unpack(">II", header[16:24])
    if (width, height) != (128, 128):
        raise RuntimeError(
            "%s is %dx%d; the selected Kenney Double glyphs must be 128x128"
            % (source, width, height))

    package = "%s/%s" % (PACKAGE_ROOT, family)
    name = asset_name(filename)
    asset_path = "%s/%s" % (package, name)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", str(source))
    task.set_editor_property("destination_path", package)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    texture = unreal.load_asset(asset_path)
    if texture is None:
        raise RuntimeError("Kenney glyph import produced no texture: %s" % asset_path)
    texture.set_editor_property("srgb", True)
    texture.set_editor_property(
        "compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    texture.set_editor_property(
        "mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property("filter", unreal.TextureFilter.TF_BILINEAR)
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    texture.set_editor_property("never_stream", True)
    unreal.EditorAssetLibrary.save_loaded_asset(texture, only_if_is_dirty=False)
    return asset_path


def main():
    imported = []
    for family, filenames in GLYPHS.items():
        unreal.EditorAssetLibrary.make_directory("%s/%s" % (PACKAGE_ROOT, family))
        imported.extend(import_glyph(family, filename) for filename in filenames)
    unreal.log("[input-glyphs] generated %d Kenney CommonInput textures" % len(imported))


main()
