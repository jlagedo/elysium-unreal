# The single umbrella that (re)builds EVERY committed Content/ asset from its generator, in
# one headless editor session. A UMaterial shading graph and a .umap can only be compiled by
# the engine (not by the standalone export), so these few assets are the accepted exception to
# "build everything in code" -- they are authored offline here and committed. Wiring them all
# through one driver (invoked by the export, see export_all.py) means none can be forgotten and
# silently go stale, the way M_Sky did.
#
# Registering a NEW committed-asset transform = add its script filename to GENERATORS below.
# Each generator is a standalone `-script` (also runnable on its own); this just runs them all.
#
# Run headless (normally invoked by content.bat / export_all.py):
#   UnrealEditor-Cmd.exe ElysiumUE.uproject -run=pythonscript -script="tools/build_content.py" -unattended -nosplash -nopause
import os, runpy, traceback
import unreal

HERE = os.path.dirname(os.path.abspath(__file__))

# Ordered by dependency: the master world material first (base + its two additive edits),
# then the sky master material, then the empty boot level. Every generator is idempotent --
# re-running reproduces the same committed asset.
GENERATORS = [
    "add_world_emissive.py",        # M_VtMB_World.uasset: $selfillum emissive path
    "set_world_material_usage.py",  # M_VtMB_World.uasset: "Used with Instanced Static Meshes"
    "make_sky_material.py",         # M_Sky.uasset: the 2D-skybox cube master material
    "make_gizmo_material.py",       # M_Gizmo{,_XRay}.uasset: the P2.4 entity-gizmo ISM materials
    "make_boot_map.py",             # Elysium.umap: the empty boot persistent level
]


def run_one(script):
    """Run a generator by filename; return (script, status). SystemExit(0) counts as ok
    (the idempotent 'already applied -- nothing to do' path)."""
    unreal.log("[build_content] --- %s ---" % script)
    try:
        runpy.run_path(os.path.join(HERE, script), run_name="__main__")
        return (script, "ok")
    except SystemExit as e:
        code = e.code if e.code is not None else 0
        return (script, "ok" if code == 0 else "FAIL: exit %s" % code)
    except Exception as e:
        unreal.log_error("[build_content] %s raised: %s" % (script, e))
        traceback.print_exc()
        return (script, "FAIL: %s" % e)


def main():
    results = [run_one(s) for s in GENERATORS]

    unreal.log("[build_content] ==== summary ====")
    failed = 0
    for script, status in results:
        unreal.log("[build_content]   %-30s %s" % (script, status))
        if status.startswith("FAIL"):
            failed += 1

    if failed:
        unreal.log_error("[build_content] %d of %d generators FAILED" % (failed, len(results)))
        raise SystemExit(1)
    unreal.log("[build_content] all %d committed assets rebuilt ok" % len(results))


main()
