# Enables the master world material's "Used with Instanced Static Meshes" usage flag and
# re-saves the committed .uasset. Static props render through UInstancedStaticMeshComponents,
# and UE only compiles an ISM shader permutation for a master material that carries this flag
# — without it, every prop falls back to the default (grey) material (and a packaged build,
# with no shader compiler, cannot recover it at runtime). One-time asset edit.
#
# Normally rebuilt by the umbrella (content.bat -> tools/build_content.py, which the export
# runs); also runnable standalone:
#   UnrealEditor-Cmd.exe ElysiumUE.uproject -run=pythonscript -script="tools/set_world_material_usage.py" -unattended -nosplash -nopause
import unreal

ASSET = "/Game/VtMB/Materials/M_VtMB_World"

mat = unreal.load_asset(ASSET)
if not mat:
    unreal.log_error("[set_world_material_usage] load failed: %s" % ASSET)
    raise SystemExit(1)

mat.set_editor_property("used_with_instanced_static_meshes", True)
unreal.MaterialEditingLibrary.recompile_material(mat)

if unreal.EditorAssetLibrary.save_asset(ASSET, only_if_is_dirty=False):
    unreal.log("[set_world_material_usage] saved %s (ISM usage enabled)" % ASSET)
else:
    unreal.log_error("[set_world_material_usage] save failed")
    raise SystemExit(1)
