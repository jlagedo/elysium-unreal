# Generates the empty boot level Content/Elysium.umap: a genuinely blank persistent level
# (no template floor / sky / lights). The game boots into this clean world and the runtime
# module spawns everything from source, so nothing has to be stripped at runtime.
#
# Normally rebuilt by the umbrella (content.bat -> tools/build_content.py, which the export
# runs); also runnable standalone:
#   UnrealEditor-Cmd.exe ElysiumUE.uproject -run=pythonscript -script="tools/make_boot_map.py" -unattended -nosplash -nopause
import unreal

MAP = "/Game/Elysium"

world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
if not world:
    unreal.log_error("[make_boot_map] new_blank_map returned null")
else:
    ok = unreal.EditorLoadingAndSavingUtils.save_map(world, MAP)
    unreal.log("[make_boot_map] saved %s: %s" % (MAP, "ok" if ok else "FAILED"))
