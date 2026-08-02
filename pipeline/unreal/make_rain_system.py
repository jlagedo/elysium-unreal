"""Generate the one tunable outdoor-rain material and Niagara system."""

from pathlib import Path

import unreal

from elysium_pipeline.paths import export_root
from pipeline.unreal import _bootstrap  # noqa: F401
from pipeline.unreal import mat_fog


PKG = "/Game/VtMB/Particles"
SYSTEM = PKG + "/NS_ElysiumRain"
MATERIAL = PKG + "/M_ElysiumRain"
MPC_PATH = "/Game/VtMB/Materials/MPC_ElysiumEnvironment.MPC_ElysiumEnvironment"
SPRITES = {
    "RainDroplet": "dropletfast.png",
    "RainImpact": "fortituderings.png",
    "RainStain": "d_targetblob.png",
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
    """Binary expression with explicit source pins (notably TextureSample.A)."""
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
            unreal.EditorAssetLibrary.delete_asset(asset)
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", str(source))
        task.set_editor_property("destination_path", PKG)
        task.set_editor_property("destination_name", name)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", False)
        tasks.append((parameter, asset, task))
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
    return imported


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


def make_material(sprites):
    if unreal.EditorAssetLibrary.does_asset_exist(MATERIAL):
        unreal.EditorAssetLibrary.delete_asset(MATERIAL)
    mat = tools.create_asset(
        "M_ElysiumRain", PKG, unreal.Material, unreal.MaterialFactoryNew())
    if not mat:
        raise SystemExit("[make_rain_system] could not create rain material")
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("used_with_niagara_sprites", True)
    mat.set_editor_property(
        "translucency_lighting_mode",
        unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING,
    )

    uv = mel.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -1500, -300)
    samples = {
        name: texture_parameter(mat, name + "Sprite", texture, -1260, y)
        for (name, texture), y in zip(sprites.items(), (-520, -300, -80, 140))
    }
    for sample in samples.values():
        connect(uv, "", sample, "UVs")
    # One graph serves all three renderers. Runtime transient instances set RainLayer to 0, .5,
    # or 1; no duplicate material assets or faithful/enhanced paths are generated.
    layer = scalar(mat, "RainLayer", 0.0, -1260, 400)
    selector_one = constant(mat, 1.0, -1040, 600)
    impact_or_mist = step(mat, layer, 0.25, -1040, 360)
    mist = step(mat, layer, 0.75, -1040, 520)
    streak = binary(
        mat, unreal.MaterialExpressionSubtract, selector_one, impact_or_mist, -820, 360)
    impact = binary(
        mat, unreal.MaterialExpressionMultiply, impact_or_mist,
        binary(mat, unreal.MaterialExpressionSubtract, selector_one, mist, -820, 600),
        -600, 440)

    impact_max = mel.create_material_expression(mat, unreal.MaterialExpressionMax, -980, -180)
    connect(samples["RainImpact"], "R", impact_max, "A")
    connect(samples["RainStain"], "R", impact_max, "B")
    streak_alpha = binary_outputs(
        mat, unreal.MaterialExpressionMultiply,
        samples["RainDroplet"], "R", streak, "", -760, -460)
    streak_alpha = binary(
        mat, unreal.MaterialExpressionMultiply, streak_alpha,
        constant(mat, 1.0, -760, -380), -560, -460)
    impact_alpha = binary(mat, unreal.MaterialExpressionMultiply, impact_max, impact, -760, -180)
    impact_alpha = binary(
        mat, unreal.MaterialExpressionMultiply, impact_alpha,
        constant(mat, 0.25, -760, -100), -560, -180)
    mist_alpha = binary_outputs(
        mat, unreal.MaterialExpressionMultiply,
        samples["RainMist"], "R", mist, "", -760, 100)
    mist_alpha = binary(
        mat, unreal.MaterialExpressionMultiply, mist_alpha,
        constant(mat, 0.03, -760, 180), -560, 100)
    selected_a = binary(mat, unreal.MaterialExpressionAdd, streak_alpha, impact_alpha, -320, -260)
    selected_b = binary(mat, unreal.MaterialExpressionAdd, selected_a, mist_alpha, -300, -180)

    # The per-map R16 maximum-height map shares the exact decode contract with weather.py.
    world = mel.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -1500, 820)
    world_x = channel(mat, world, "r", -1300, 760)
    world_y = channel(mat, world, "g", -1300, 840)
    world_z = channel(mat, world, "b", -1300, 920)
    min_x = scalar(mat, "RainBoundsMinX", 0.0, -1500, 1040)
    min_y = scalar(mat, "RainBoundsMinY", 0.0, -1500, 1120)
    size_x = scalar(mat, "RainBoundsSizeX", 1.0, -1500, 1200)
    size_y = scalar(mat, "RainBoundsSizeY", 1.0, -1500, 1280)
    ux = binary(
        mat, unreal.MaterialExpressionDivide,
        binary(mat, unreal.MaterialExpressionSubtract, world_x, min_x, -1080, 760),
        size_x, -860, 760)
    uy = binary(
        mat, unreal.MaterialExpressionDivide,
        binary(mat, unreal.MaterialExpressionSubtract, world_y, min_y, -1080, 840),
        size_y, -860, 840)
    height_uv = mel.create_material_expression(mat, unreal.MaterialExpressionAppendVector, -640, 800)
    connect(ux, "", height_uv, "A")
    # Unreal's texture V origin matches the saved raster's row-zero convention. Keep Y verbatim;
    # flipping it selects the opposite map column and turns known uncovered locations into cover.
    connect(uy, "", height_uv, "B")
    height = texture_parameter(
        mat, "RainHeightTexture",
        unreal.load_asset(
            "/Game/VtMB/Particles/T_RainHeightPlaceholder.T_RainHeightPlaceholder"),
        -420, 800, grayscale=True)
    connect(height_uv, "", height, "UVs")
    sample_r = channel(mat, height, "r", -200, 800)
    u16 = binary(mat, unreal.MaterialExpressionMultiply, sample_r,
                 constant(mat, 65535.0, -200, 980), 20, 800)
    index = binary(mat, unreal.MaterialExpressionSubtract, u16,
                   constant(mat, 1.0, 20, 980), 240, 800)
    z_scale = scalar(mat, "RainHeightZScale", 1.0, 240, 1040)
    min_z = scalar(mat, "RainHeightMinZ", 0.0, 240, 1120)
    decoded = binary(
        mat, unreal.MaterialExpressionAdd,
        binary(mat, unreal.MaterialExpressionMultiply, index, z_scale, 460, 800),
        min_z, 680, 800)
    # UMaterialExpressionStep compiles as step(Y, X): X is the tested value and Y is
    # the threshold.  A non-sentinel texel means the column has a rain-blocking surface;
    # a falling particle remains visible only while it is above that maximum height.
    cover = step(mat, sample_r, 0.000001, 680, 1000)
    exposed = mel.create_material_expression(mat, unreal.MaterialExpressionStep, 900, 800)
    connect(binary(mat, unreal.MaterialExpressionAdd, decoded,
                   constant(mat, 5.0, 680, 1160), 900, 920), "", exposed, "Y")
    connect(world_z, "", exposed, "X")
    one = constant(mat, 1.0, 900, 1080)
    no_cover = binary(mat, unreal.MaterialExpressionSubtract, one, cover, 1120, 1000)
    cover_exposed = binary(mat, unreal.MaterialExpressionMultiply, cover, exposed, 1120, 800)
    falling_visible = binary(
        mat, unreal.MaterialExpressionAdd, no_cover, cover_exposed, 1340, 900)
    falling_selector = binary(mat, unreal.MaterialExpressionAdd, streak, mist, 1120, 600)
    falling_opacity = binary(
        mat, unreal.MaterialExpressionMultiply, falling_selector, falling_visible, 1560, 720)
    impact_opacity = binary(mat, unreal.MaterialExpressionMultiply, impact, cover, 1340, 560)
    visibility = binary(
        mat, unreal.MaterialExpressionAdd, falling_opacity, impact_opacity, 1780, 640)
    opacity = binary(mat, unreal.MaterialExpressionMultiply, selected_b, visibility, 200, -100)
    # Keep this connection as a single assignment: it is the last gate between Niagara's
    # simulated sprites and the translucent pass, and the content validator checks that the
    # material compiles after the generated graph is saved.
    if not mel.connect_material_property(streak_alpha, "", unreal.MaterialProperty.MP_OPACITY):
        raise SystemExit("[make_rain_system] could not connect opacity")

    # Impact/stain particles are projected onto the decoded crossing height; streak pixels below
    # that same height are masked above, so the two views of the collision stay coherent.
    impact_delta = binary(mat, unreal.MaterialExpressionSubtract, decoded, world_z, 1120, 1240)
    impact_delta = binary(mat, unreal.MaterialExpressionMultiply, impact_delta, impact, 1340, 1240)
    impact_delta = binary(mat, unreal.MaterialExpressionMultiply, impact_delta, cover, 1560, 1240)
    zero = constant(mat, 0.0, 1560, 1400)
    xy = mel.create_material_expression(mat, unreal.MaterialExpressionAppendVector, 1760, 1360)
    connect(zero, "", xy, "A")
    connect(zero, "", xy, "B")
    wpo = mel.create_material_expression(mat, unreal.MaterialExpressionAppendVector, 1960, 1320)
    connect(xy, "", wpo, "A")
    connect(impact_delta, "", wpo, "B")
    if not mel.connect_material_property(wpo, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET):
        raise SystemExit("[make_rain_system] could not connect impact projection")

    tint = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, 200, 180)
    tint.set_editor_property("constant", unreal.LinearColor(0.46, 0.55, 0.62, 1.0))
    collection = unreal.load_asset(MPC_PATH)
    response = mel.create_material_expression(mat, unreal.MaterialExpressionCollectionParameter, 200, 360)
    response.set_editor_property("collection", collection)
    response.set_editor_property("parameter_name", "RainLightResponse")
    lit = binary(mat, unreal.MaterialExpressionMultiply, tint, response, 440, 180)
    inverse = mel.create_material_expression(mat, unreal.MaterialExpressionOneMinus, 440, 360)
    connect(response, "", inverse, "")
    emissive = binary(mat, unreal.MaterialExpressionMultiply, tint, inverse, 660, 300)
    if not mel.connect_material_property(lit, "", unreal.MaterialProperty.MP_BASE_COLOR):
        raise SystemExit("[make_rain_system] could not connect base color")
    if not mel.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        raise SystemExit("[make_rain_system] could not connect emissive")
    mel.connect_material_property(constant(mat, 0.2, 660, 440), "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(constant(mat, 0.5, 660, 520), "", unreal.MaterialProperty.MP_SPECULAR)
    mel.recompile_material(mat)
    if not unreal.EditorAssetLibrary.save_asset(MATERIAL, only_if_is_dirty=False):
        raise SystemExit("[make_rain_system] could not save rain material")
    return mat


def main():
    # Delete the owner first so reruns can replace its referenced material and sprites unattended.
    if unreal.EditorAssetLibrary.does_asset_exist(SYSTEM):
        unreal.EditorAssetLibrary.delete_asset(SYSTEM)
    sprites = import_sprites()
    make_material(sprites)
    template = unreal.load_asset("/Niagara/DefaultAssets/Templates/Emitters/Fountain.Fountain")
    system = unreal.ElysiumRainAssetBuilder.build_rain_system(
        "NS_ElysiumRain", PKG, template)
    if not system or not unreal.EditorAssetLibrary.save_asset(SYSTEM, only_if_is_dirty=False):
        raise SystemExit("[make_rain_system] could not build/save NS_ElysiumRain")
    unreal.log("[make_rain_system] saved one material + one three-emitter Niagara system")


main()
