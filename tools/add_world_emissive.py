# Adds the $selfillum emissive path to the committed master world material
# Content/VtMB/Materials/M_VtMB_World.uasset. VtMB's lit surfaces (bulbs, lit windows,
# neon signs) carry `$selfillum` -- the base texture's alpha is an emission mask. The
# exporter bakes that into a `*_ke.png` and writes `map_Ke` in the MTL; the runtime binds
# it onto the `Emissive` texture parameter and turns `EmissiveScale` on (the factory).
#
# This is a purely additive edit: it wires  Emissive.rgb * EmissiveScale -> Emissive Color
# and leaves the BaseColor / OpacityMask graph untouched. EmissiveScale defaults to 0, so
# surfaces without a bound emissive map never glow. Idempotent (skips if already present).
#
# Normally rebuilt by the umbrella (content.bat -> tools/build_content.py, which the export
# runs); also runnable standalone:
#   UnrealEditor-Cmd.exe ElysiumUE.uproject -run=pythonscript -script="tools/add_world_emissive.py" -unattended -nosplash -nopause
import unreal

ASSET = "/Game/VtMB/Materials/M_VtMB_World"
DEFAULT_TEX = "/Engine/EngineResources/DefaultTexture.DefaultTexture"

mel = unreal.MaterialEditingLibrary

mat = unreal.load_asset(ASSET)
if not mat:
    unreal.log_error("[add_world_emissive] load failed: %s" % ASSET)
    raise SystemExit(1)

# Idempotency: if EmissiveScale already exists, this edit has already been applied.
try:
    names = [str(n) for n in mel.get_scalar_parameter_names(mat)]
except Exception:
    names = []
if "EmissiveScale" in names:
    unreal.log("[add_world_emissive] EmissiveScale already present -- nothing to do")
    raise SystemExit(0)

# Emissive: the per-surface alpha-masked emission map, bound at runtime via a MID. A default
# engine texture lets the graph compile; EmissiveScale's 0 default keeps it black until bound.
emis = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -520, 360)
emis.set_editor_property("parameter_name", "Emissive")
emis.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
default_tex = unreal.EditorAssetLibrary.load_asset(DEFAULT_TEX)
if default_tex:
    emis.set_editor_property("texture", default_tex)

# EmissiveScale: self-illum brightness, off (0) by default. The material factory sets it to
# elysium.EmissiveScale only for surfaces that carry a `map_Ke` emission map.
scale = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -520, 560)
scale.set_editor_property("parameter_name", "EmissiveScale")
scale.set_editor_property("default_value", 0.0)

mul = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -250, 400)
mel.connect_material_expressions(emis, "RGB", mul, "A")
mel.connect_material_expressions(scale, "", mul, "B")
mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

mel.recompile_material(mat)
if unreal.EditorAssetLibrary.save_asset(ASSET, only_if_is_dirty=False):
    unreal.log("[add_world_emissive] saved %s (Emissive + EmissiveScale added)" % ASSET)
else:
    unreal.log_error("[add_world_emissive] save failed")
    raise SystemExit(1)
