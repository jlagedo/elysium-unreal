"""Generate the one tunable outdoor-rain material and Niagara system."""

from pathlib import Path
import struct
import zlib

import unreal

from elysium_pipeline.paths import export_root
from pipeline.unreal import _bootstrap  # noqa: F401
from pipeline.unreal import bake_lib as bl
from pipeline.unreal import mat_fog


PKG = "/Game/VtMB/Particles"
SYSTEM = PKG + "/NS_ElysiumRain"
MATERIAL = PKG + "/M_ElysiumRain"
STREAK_MIC = PKG + "/MI_ElysiumRainStreak"
MIST_MIC = PKG + "/MI_ElysiumRainMist"
SPRITES = {
    "RainDroplet": "dropletfast.png",
    "RainMist": "furball.png",
}

mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
connect = mat_fog.connect


def scalar(mat, name, default, x, y):
    node = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", default)
    return node


def constant(mat, value, x, y):
    node = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, x, y)
    node.set_editor_property("r", value)
    return node


def binary(mat, cls, a, b, x, y):
    node = mel.create_material_expression(mat, cls, x, y)
    connect(a, "", node, "A")
    connect(b, "", node, "B")
    return node


def binary_outputs(mat, cls, a, a_output, b, b_output, x, y):
    node = mel.create_material_expression(mat, cls, x, y)
    connect(a, a_output, node, "A")
    connect(b, b_output, node, "B")
    return node


def channel(mat, source, name, x, y):
    node = mel.create_material_expression(mat, unreal.MaterialExpressionComponentMask, x, y)
    for prop in ("r", "g", "b", "a"):
        node.set_editor_property(prop, prop == name)
    connect(source, "", node, "")
    return node


def step(mat, tested, threshold, x, y):
    """UE's Step expression compiles as step(Y, X): threshold Y, tested value X."""
    node = mel.create_material_expression(mat, unreal.MaterialExpressionStep, x, y)
    node.set_editor_property("const_y", threshold)
    connect(tested, "", node, "X")
    return node


def _png_chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(
        ">I", zlib.crc32(kind + data) & 0xffffffff)


def write_needle_png(path):
    """A thin vertical streak: 8x128, hard core, soft sides, faded tips."""
    width, height = 8, 128
    rows = []
    for y in range(height):
        tip = min(y, height - 1 - y) / 14.0
        tip = 1.0 if tip > 1.0 else tip
        row = [0]
        for x in range(width):
            side = 1.0 - abs((x + 0.5) / width - 0.5) * 2.2
            side = 0.0 if side < 0.0 else side
            value = int(255.0 * (side ** 2) * tip)
            row.append(value)
        rows.append(bytes(row))
    raw = b"".join(rows)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + _png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0))
        + _png_chunk(b"IDAT", zlib.compress(raw, 9))
        + _png_chunk(b"IEND", b"")
    )


def import_sprites():
    source_root = Path(export_root()) / "particles"
    imported = {}
    tasks = []
    for parameter, filename in SPRITES.items():
        source = source_root / filename
        if not source.is_file():
            raise SystemExit(
                "[make_rain_system] missing particle mirror %s; export bundle particles first"
                % source
            )
        name = "T_" + parameter
        asset = PKG + "/" + name
        if unreal.EditorAssetLibrary.does_asset_exist(asset):
            texture = unreal.load_asset(asset)
            if texture:
                imported[parameter] = texture
                continue
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", str(source))
        task.set_editor_property("destination_path", PKG)
        task.set_editor_property("destination_name", name)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", False)
        tasks.append((parameter, asset, task))
    if tasks:
        tools.import_asset_tasks([item[2] for item in tasks])
    for parameter, asset, _task in tasks:
        texture = unreal.load_asset(asset)
        if not texture:
            raise SystemExit("[make_rain_system] sprite import failed: %s" % asset)
        texture.set_editor_property("srgb", False)
        texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        texture.set_editor_property("filter", unreal.TextureFilter.TF_BILINEAR)
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
        unreal.EditorAssetLibrary.save_asset(asset, only_if_is_dirty=False)
        imported[parameter] = texture
    needle = import_needle()
    if needle:
        imported["RainDroplet"] = needle
    return imported


def import_needle():
    asset = PKG + "/T_RainNeedle"
    source = Path(export_root()) / ".policy" / "rain_needle.png"
    write_needle_png(source)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", str(source))
    task.set_editor_property("destination_path", PKG)
    task.set_editor_property("destination_name", "T_RainNeedle")
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    tools.import_asset_tasks([task])
    texture = unreal.load_asset(asset)
    if not texture:
        raise SystemExit("[make_rain_system] needle sprite import failed")
    texture.set_editor_property("srgb", False)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property("filter", unreal.TextureFilter.TF_BILINEAR)
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    unreal.EditorAssetLibrary.save_asset(asset, only_if_is_dirty=False)
    return texture


def texture_parameter(mat, name, texture, x, y, grayscale=False):
    node = mel.create_material_expression(
        mat, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("texture", texture)
    node.set_editor_property(
        "sampler_type",
        unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR
        if grayscale else unreal.MaterialSamplerType.SAMPLERTYPE_MASKS,
    )
    return node


def make_layer_instance(name, parent, layer):
    path = PKG + "/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    mic = bl.make_material_instance(name, PKG, parent)
    if not mic:
        raise SystemExit("[make_rain_system] could not create %s" % path)
    bl.set_scalar_param(mic, "RainLayer", layer)
    if not unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False):
        raise SystemExit("[make_rain_system] could not save %s" % path)
    return mic


def make_material(sprites):
    if unreal.EditorAssetLibrary.does_asset_exist(MATERIAL):
        mat = unreal.load_asset(MATERIAL)
        if not mat:
            raise SystemExit("[make_rain_system] could not load existing rain material")
        mel.delete_all_material_expressions(mat)
    else:
        mat = tools.create_asset(
            "M_ElysiumRain", PKG, unreal.Material, unreal.MaterialFactoryNew())
    if not mat:
        raise SystemExit("[make_rain_system] could not create rain material")
    unreal.log("[make_rain_system] authoring unlit additive rain material")
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("used_with_niagara_sprites", True)

    uv = mel.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -1600, -200)
    droplet = texture_parameter(mat, "RainDropletSprite", sprites["RainDroplet"], -1360, -320)
    mist = texture_parameter(mat, "RainMistSprite", sprites["RainMist"], -1360, -80)
    connect(uv, "", droplet, "UVs")
    connect(uv, "", mist, "UVs")
    layer = scalar(mat, "RainLayer", 0.0, -1360, 200)
    mist_sel = step(mat, layer, 0.5, -1120, 200)
    one = constant(mat, 1.0, -1120, 80)
    streak_sel = binary(mat, unreal.MaterialExpressionSubtract, one, mist_sel, -900, 80)
    streak_tex = binary_outputs(
        mat, unreal.MaterialExpressionMultiply, droplet, "R", streak_sel, "", -680, -280)
    mist_tex = binary_outputs(
        mat, unreal.MaterialExpressionMultiply, mist, "R", mist_sel, "", -680, -40)
    sprite = binary(mat, unreal.MaterialExpressionAdd, streak_tex, mist_tex, -460, -160)

    particle = mel.create_material_expression(
        mat, unreal.MaterialExpressionParticleColor, -1600, 360)
    lit = binary_outputs(
        mat, unreal.MaterialExpressionMultiply, particle, "RGB", sprite, "", -240, 200)
    alpha = binary_outputs(
        mat, unreal.MaterialExpressionMultiply, particle, "A", sprite, "", -240, 360)

    # Height-map clip stays; a zero placeholder means no cover, so every drop stays visible.
    world = mel.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -1600, 820)
    world_x = channel(mat, world, "r", -1400, 760)
    world_y = channel(mat, world, "g", -1400, 840)
    world_z = channel(mat, world, "b", -1400, 920)
    min_x = scalar(mat, "RainBoundsMinX", 0.0, -1600, 1040)
    min_y = scalar(mat, "RainBoundsMinY", 0.0, -1600, 1120)
    size_x = scalar(mat, "RainBoundsSizeX", 1.0, -1600, 1200)
    size_y = scalar(mat, "RainBoundsSizeY", 1.0, -1600, 1280)
    ux = binary(
        mat, unreal.MaterialExpressionDivide,
        binary(mat, unreal.MaterialExpressionSubtract, world_x, min_x, -1180, 760),
        size_x, -960, 760)
    uy = binary(
        mat, unreal.MaterialExpressionDivide,
        binary(mat, unreal.MaterialExpressionSubtract, world_y, min_y, -1180, 840),
        size_y, -960, 840)
    height_uv = mel.create_material_expression(mat, unreal.MaterialExpressionAppendVector, -740, 800)
    connect(ux, "", height_uv, "A")
    connect(uy, "", height_uv, "B")
    height_tex = unreal.load_asset(
        "/Game/VtMB/Particles/T_RainHeightPlaceholder.T_RainHeightPlaceholder")
    if height_tex:
        height = texture_parameter(
            mat, "RainHeightTexture", height_tex, -520, 800, grayscale=True)
        connect(height_uv, "", height, "UVs")
        sample_r = channel(mat, height, "r", -300, 800)
    else:
        # Rain-only generate does not run the world-material placeholder import.
        sample_r = constant(mat, 0.0, -300, 800)
    cover = step(mat, sample_r, 0.000001, 0, 1000)
    u16 = binary(mat, unreal.MaterialExpressionMultiply, sample_r,
                 constant(mat, 65535.0, -300, 980), -80, 800)
    index = binary(mat, unreal.MaterialExpressionSubtract, u16,
                   constant(mat, 1.0, -80, 980), 140, 800)
    z_scale = scalar(mat, "RainHeightZScale", 1.0, 140, 1040)
    min_z = scalar(mat, "RainHeightMinZ", 0.0, 140, 1120)
    decoded = binary(
        mat, unreal.MaterialExpressionAdd,
        binary(mat, unreal.MaterialExpressionMultiply, index, z_scale, 360, 800),
        min_z, 580, 800)
    exposed = mel.create_material_expression(mat, unreal.MaterialExpressionStep, 800, 800)
    connect(binary(mat, unreal.MaterialExpressionAdd, decoded,
                   constant(mat, 5.0, 580, 1160), 800, 920), "", exposed, "Y")
    connect(world_z, "", exposed, "X")
    no_cover = binary(mat, unreal.MaterialExpressionSubtract, one, cover, 1020, 1000)
    cover_exposed = binary(mat, unreal.MaterialExpressionMultiply, cover, exposed, 1020, 800)
    # Height clip stays in the graph for a later pass. Night-rain visibility is the sprite itself.
    emissive = lit
    opacity = alpha
    if not mel.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        raise SystemExit("[make_rain_system] could not connect emissive")
    if not mel.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY):
        raise SystemExit("[make_rain_system] could not connect opacity")
    mel.recompile_material(mat)
    if not unreal.EditorAssetLibrary.save_asset(MATERIAL, only_if_is_dirty=False):
        raise SystemExit("[make_rain_system] could not save rain material")
    return mat


def main():
    sprites = import_sprites()
    mat = make_material(sprites)
    make_layer_instance("MI_ElysiumRainStreak", mat, 0.0)
    make_layer_instance("MI_ElysiumRainMist", mat, 1.0)
    if unreal.EditorAssetLibrary.does_asset_exist(SYSTEM):
        unreal.EditorAssetLibrary.delete_asset(SYSTEM)
    template = unreal.load_asset("/Niagara/DefaultAssets/Templates/Emitters/Fountain.Fountain")
    system = unreal.ElysiumRainAssetBuilder.build_rain_system(
        "NS_ElysiumRain", PKG, template)
    if not system or not unreal.EditorAssetLibrary.save_asset(SYSTEM, only_if_is_dirty=False):
        raise SystemExit("[make_rain_system] could not build/save NS_ElysiumRain")
    unreal.log("[make_rain_system] saved unlit additive rain material + Streaks/Mist Niagara system")


main()
