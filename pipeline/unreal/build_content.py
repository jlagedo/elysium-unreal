# The single umbrella that (re)builds every generated local Content/ package from its generator, in
# one headless editor session. A UMaterial shading graph and a .umap can only be compiled by
# the engine (not by the standalone export), so these few assets are the accepted exception to
# "build everything in code" -- they are authored offline here, generated locally, and ignored.
# Wiring them all through one driver lets the project tooling reproduce the complete policy
# package set without any generated Unreal package entering Git.
#
# Registering a new local package transform = add its script filename to GENERATORS below.
# Each generator is a standalone `-script` (also runnable on its own); this just runs them all.
#
# Run headless as the commandlet policy-content phase coordinated by ``elysium``:
#   UnrealEditor-Cmd.exe ElysiumUE.uproject -run=pythonscript -script="pipeline/unreal/build_content.py" -unattended -nosplash -nopause
import os, runpy, traceback
import unreal
from pipeline.unreal import _bootstrap  # noqa: F401, E402

HERE = os.path.dirname(os.path.abspath(__file__))

# Ordered by dependency: the world master-material set first, then the sky master material,
# then the empty boot level. Every generator is idempotent -- re-running reproduces the same
# local generated package.
GENERATORS = [
    "make_world_materials.py",      # M_World_{Opaque,Masked,Translucent,Glass} + M_Refract/M_Additive
    "make_rain_system.py",         # one tunable sm_hub_1 rain material + Niagara system
    "make_player_body_material.py", # M_PlayerBody.uasset: masked/dithered glTF player body
    "make_sky_material.py",         # M_Sky.uasset: the 2D-skybox cube master material
    "make_gizmo_material.py",       # M_Gizmo{,_XRay}.uasset: the P2.4 entity-gizmo ISM materials
    "make_decal_material.py",       # M_Decal.uasset: the 7.2 deferred-decal master material
    "make_audio_routing.py",        # semantic classes/submixes/concurrency/attenuation templates
    "make_boot_map.py",             # Elysium.umap: the empty boot persistent level
]

# NOT in the umbrella: `make_ui_fonts.py`. Importing a `UFontFace` flushes Slate's font cache,
# and `FSlateApplication::Get()` asserts in a commandlet -- `-run=pythonscript` never creates a
# Slate application, with or without `-AllowCommandletRendering`. It therefore remains the
# separate Slate-enabled policy-content phase coordinated by ``elysium``.


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
    unreal.log("[build_content] all %d local packages rebuilt ok" % len(results))


main()
