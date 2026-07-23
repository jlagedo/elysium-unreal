"""Batch-export VtMB maps to out/<name>/ via bsp_to_scene.

Runs the full scene export (geometry, textures + DDS, lightmap, collision, sky,
water, static props) for each map, isolating failures so one bad map doesn't abort
the run.

Every run also rebuilds the committed Content/ assets (the sky material, world material and
boot map) via content.bat, so an offline asset generator can never be forgotten and go stale
-- pass --no-content to skip that (fast geometry-only iteration).

Usage:
  python tools/export_all.py                 # the test bench (maps.ini [test])
  python tools/export_all.py --all           # every .bsp in the install (~101 maps)
  python tools/export_all.py ch_hub_1 la_hub_1   # only the named maps
  python tools/export_all.py --skip-existing     # skip maps already exported
  python tools/export_all.py --no-content        # skip the committed-asset rebuild
  python tools/export_all.py --no-scripts        # skip the script/dialogue copy
  python tools/export_all.py --no-signs          # skip the sign definition/background copy
  python tools/export_all.py --npc               # also export the P8 8.2 test NPC glb(s)
"""
import os, sys, glob, time, subprocess, traceback
import UE_bsp_to_scene as B
import UE_extract_sounds as S
import UE_extract_scripts as SC
import UE_extract_signs as SG
import install

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

MAPS = os.path.join(install.GAME, "maps")
OUT = "out"
MAPS_INI = os.path.join(os.path.dirname(os.path.abspath(__file__)), "maps.ini")

# P8 8.2 glTFRuntime spike: the reproducible test NPC(s), each (model-path-in-VPK, anim-name),
# exported to out/npc/<stem>.glb by mdl_gltf.py for the runtime skeletal path. gangmember_male_2
# is a self-contained biped (69 bones) with the clip embedded in the model -- no shared-library
# include resolution (that is PL4). Batch NPC export is PL4; this list is just the spike asset.
TEST_NPCS = [
    ("models/character/npc/common/gangmember_male_2/gangmember_male_2.mdl", "patron_barstand"),
]


def load_test_maps(ini_path=MAPS_INI):
    """The [test] bench map names from maps.ini (one per line; `;`/`#` comments)."""
    maps = []
    with open(ini_path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line[0] in "#;[":
                continue
            name = line.split("#", 1)[0].split(";", 1)[0].split()[0]
            if name:
                maps.append(name)
    return maps


def build_content():
    """Rebuild the committed Content/ assets by running content.bat (a headless editor
    session driving tools/build_content.py). Kept out of the per-map loop -- these assets
    are map-independent, so they are rebuilt once, after the maps."""
    bat = os.path.join(REPO, "content.bat")
    if sys.platform != "win32" or not os.path.exists(bat):
        print(f"\n[content] skipped (need Windows + {bat})", flush=True)
        return
    print("\n[content] rebuilding committed assets (sky/world material, boot map) ...", flush=True)
    t0 = time.time()
    rc = subprocess.run(["cmd", "/c", bat]).returncode
    status = "ok" if rc == 0 else f"FAILED (exit {rc})"
    print(f"[content] {status}  ({time.time()-t0:.0f}s)", flush=True)


def main():
    args = sys.argv[1:]
    skip_existing = "--skip-existing" in args
    skip_content = "--no-content" in args
    skip_sound = "--no-sound" in args
    skip_scripts = "--no-scripts" in args
    skip_signs = "--no-signs" in args
    export_all = "--all" in args
    only = [a for a in args if not a.startswith("--")]

    all_bsps = sorted(glob.glob(os.path.join(MAPS, "*.bsp")))
    if only:
        wanted, scope = set(only), "named maps"
    elif export_all:
        wanted, scope = None, f"all {len(all_bsps)} maps"
    else:
        wanted, scope = set(load_test_maps()), "test bench (maps.ini)"

    bsps = all_bsps if wanted is None else \
        [b for b in all_bsps if os.path.splitext(os.path.basename(b))[0] in wanted]

    # Surface any requested map that has no .bsp in the install (typo / not installed).
    if wanted is not None:
        found = {os.path.splitext(os.path.basename(b))[0] for b in bsps}
        for miss in sorted(wanted - found):
            print(f"  ! {miss}: no .bsp in {MAPS} - skipped")

    print(f"exporting {len(bsps)} maps ({scope}) -> {OUT}/<name>/", flush=True)
    results = []
    t_start = time.time()
    for i, bsp in enumerate(bsps, 1):
        name = os.path.splitext(os.path.basename(bsp))[0]
        out_dir = os.path.join(OUT, name)
        if skip_existing and os.path.exists(os.path.join(out_dir, name + ".obj")):
            print(f"[{i}/{len(bsps)}] {name}: skip (exists)", flush=True)
            results.append((name, "skip", 0.0)); continue
        print(f"\n[{i}/{len(bsps)}] {name} ...", flush=True)
        t0 = time.time()
        try:
            os.makedirs(out_dir, exist_ok=True)
            B.main(bsp, out_dir)
            results.append((name, "ok", time.time() - t0))
        except Exception as e:
            results.append((name, f"FAIL: {e}", time.time() - t0))
            print(f"  !! {name} FAILED: {e}")
            traceback.print_exc()

    ok = sum(1 for _, s, _ in results if s == "ok")
    fail = sum(1 for _, s, _ in results if s.startswith("FAIL"))
    skip = sum(1 for _, s, _ in results if s == "skip")
    print(f"\n==== summary ({time.time()-t_start:.0f}s total) ====")
    for name, status, dt in results:
        print(f"  {name:26} {status[:48]:48} {dt:6.1f}s")
    print(f"\nok={ok}  fail={fail}  skip={skip}  of {len(results)}")

    # Extract the sounds (WAV/MP3) the exported maps reference (verbatim copy, no transcode)
    # into out/sound/. Runs on the maps that produced a .ents this run (ok + skip), so a
    # geometry-only re-export still fills audio. Map-independent, so once after the loop.
    if not skip_sound:
        done = [name for name, status, _ in results if status in ("ok", "skip")]
        if done:
            print("\n[sound] extracting referenced sounds (WAV/MP3) ...", flush=True)
            try:
                S.main(done)
            except Exception as e:
                print(f"[sound] FAILED: {e}", flush=True)
                traceback.print_exc()

    # Copy the loose Python level scripts + dialogue verbatim into out/scripts and
    # out/dlg. Whole-game (not map-scoped), so once after the loop, independent of which
    # maps ran. Runs even on a zero-map invocation -- the mirror is map-independent.
    if not skip_scripts:
        print("\n[scripts] copying level scripts + dialogue ...", flush=True)
        try:
            SC.main()
        except Exception as e:
            print(f"[scripts] FAILED: {e}", flush=True)
            traceback.print_exc()

    # Copy the sign/popup definitions (vdata/Signs) + decode their background art into
    # out/signs. Whole-game like the script mirror, so it runs once after the loop and on a
    # zero-map invocation too.
    if not skip_signs:
        print("\n[signs] copying sign definitions + backgrounds ...", flush=True)
        try:
            SG.main()
        except Exception as e:
            print(f"[signs] FAILED: {e}", flush=True)
            traceback.print_exc()

    # Export the P8 8.2 test NPC(s) to out/npc as glTF 2.0 (.glb: mesh + StudioBone skeleton + one
    # animation) for the runtime glTFRuntime skeletal path. Opt-in (--npc): batch NPC export is PL4;
    # this is just the spike's reproducible test asset. mdl_gltf writes standard glTF (self-describing
    # space), loaded via glTFRuntime's default config, so no UE_-style pre-conversion is needed.
    if "--npc" in args:
        print("\n[npc] exporting test NPC glb(s) -> out/npc ...", flush=True)
        try:
            import mdl_gltf
            for model, anim in TEST_NPCS:
                mdl_gltf.export(model, anim, os.path.join(OUT, "npc"))
        except Exception as e:
            print(f"[npc] FAILED: {e}", flush=True)
            traceback.print_exc()

    if not skip_content:
        build_content()


if __name__ == "__main__":
    main()
