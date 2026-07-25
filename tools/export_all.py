"""Batch-export VtMB maps to out/<name>/ via bsp_to_scene.

Runs the full scene export (geometry, textures + DDS, lightmap, collision, sky,
water, static props) for each map, isolating failures so one bad map doesn't abort
the run.

Every run also rebuilds the committed Content/ assets (the sky material, world material and
boot map) via content.bat, so an offline asset generator can never be forgotten and go stale
-- pass --no-content to skip that (fast geometry-only iteration) -- and then bakes each
re-exported map's Lumen card sidecar via cards.bat (--no-cards to skip; ~35 s per map, the
long tail of a full export).

Usage:
  python tools/export_all.py                 # the test bench (maps.ini [test])
  python tools/export_all.py --all           # every map in the install, patch-first (108)
  python tools/export_all.py ch_hub_1 la_hub_1   # only the named maps
  python tools/export_all.py --skip-existing     # skip maps already exported
  python tools/export_all.py --no-content        # skip the committed-asset rebuild
  python tools/export_all.py --no-cards          # skip the Lumen card bake
  python tools/export_all.py --no-scripts        # skip the script/dialogue copy
  python tools/export_all.py --no-signs          # skip the sign definition/background copy
  python tools/export_all.py --no-vdata          # skip the vdata rulebook copy
  python tools/export_all.py --no-cfg            # skip the console cfg copy
  python tools/export_all.py --npc               # also batch-export NPC glbs + shared banks
"""
import os, sys, time, subprocess, traceback
import UE_bsp_to_scene as B
import UE_extract_sounds as S
import UE_extract_scripts as SC
import UE_extract_signs as SG
import UE_extract_vdata as VD
import UE_extract_cfg as CF
import install

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

OUT = "out"
MAPS_INI = os.path.join(os.path.dirname(os.path.abspath(__file__)), "maps.ini")

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


def bake_cards(maps):
    """Bake each exported map's Lumen card sidecar by running cards.bat (a headless engine
    session driving the -ElysiumCards harness). Per map, unlike content.bat: the cards are
    fitted to that map's own geometry, so a re-export invalidates exactly the maps it touched.

    This is an engine step rather than a tools/ decoder because the card builder
    (IMeshUtilities::GenerateCardRepresentationData) ray-traces through Embree and exists only
    in the editor -- baking here is what keeps Embree out of the shipping build. It stays an
    offline step: the runtime only ever reads out/<map>/<map>.cards. See
    docs/lumen-coverage-spike.md.

    Roughly 35 s per map, so this is the long tail of a full export -- --no-cards skips it,
    at the cost of every re-exported map falling back to bounds cards until it is re-run."""
    bat = os.path.join(REPO, "cards.bat")
    if sys.platform != "win32" or not os.path.exists(bat):
        print(f"\n[cards] skipped (need Windows + {bat})", flush=True)
        return
    if not maps:
        print("\n[cards] skipped (no maps exported this run)", flush=True)
        return
    print(f"\n[cards] baking Lumen cards for {len(maps)} map(s) ...", flush=True)
    t0 = time.time()
    failed = []
    for i, name in enumerate(maps, 1):
        t1 = time.time()
        rc = subprocess.run(["cmd", "/c", bat, name]).returncode
        status = "ok" if rc == 0 else f"FAILED (exit {rc})"
        if rc != 0:
            failed.append(name)
        print(f"[cards] [{i}/{len(maps)}] {name:26} {status}  ({time.time()-t1:.0f}s)", flush=True)
    tail = f", {len(failed)} failed: {', '.join(failed)}" if failed else ""
    print(f"[cards] done ({time.time()-t0:.0f}s{tail})", flush=True)


def main():
    args = sys.argv[1:]
    skip_existing = "--skip-existing" in args
    skip_content = "--no-content" in args
    skip_cards = "--no-cards" in args
    skip_sound = "--no-sound" in args
    skip_scripts = "--no-scripts" in args
    skip_signs = "--no-signs" in args
    skip_vdata = "--no-vdata" in args
    skip_cfg = "--no-cfg" in args
    export_all = "--all" in args
    only = [a for a in args if not a.startswith("--")]

    # Enumerate map NAMES from the patch-first union of the search path (all 108, patch-only
    # maps included), not a glob of the retail tree. Each name resolves patch-first through
    # install.map_path in B.main, so we export the .bsp the engine actually loads — passing a
    # full retail path here would be "honoured as-is" and silently export retail instead.
    all_names = install.all_map_names()
    if only:
        wanted, scope = set(only), "named maps"
    elif export_all:
        wanted, scope = None, f"all {len(all_names)} maps"
    else:
        wanted, scope = set(load_test_maps()), "test bench (maps.ini)"

    names = all_names if wanted is None else [n for n in all_names if n in wanted]

    # Surface any requested map that has no .bsp anywhere in the install (typo / not installed).
    if wanted is not None:
        for miss in sorted(wanted - set(names)):
            print(f"  ! {miss}: no .bsp in the install - skipped")

    print(f"exporting {len(names)} maps ({scope}) -> {OUT}/<name>/", flush=True)
    results = []
    t_start = time.time()
    for i, name in enumerate(names, 1):
        out_dir = os.path.join(OUT, name)
        if skip_existing and os.path.exists(os.path.join(out_dir, name + ".obj")):
            print(f"[{i}/{len(names)}] {name}: skip (exists)", flush=True)
            results.append((name, "skip", 0.0)); continue
        print(f"\n[{i}/{len(names)}] {name} ...", flush=True)
        t0 = time.time()
        try:
            os.makedirs(out_dir, exist_ok=True)
            B.main(install.map_path(name), out_dir)   # patch-first .bsp for this name
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

    # Copy the vdata rulebook (stats/feats/rules/dice/clans/quests/items/weapons/vendors/
    # stealth/disposition/sound-schemes/strings/camera/hacking) verbatim into out/vdata.
    # Whole-game like the script/sign mirrors, so it runs once after the loop and on a
    # zero-map invocation too. Consumers per docs/vdata-catalog.md.
    if not skip_vdata:
        print("\n[vdata] copying data tables ...", flush=True)
        try:
            VD.main()
        except Exception as e:
            print(f"[vdata] FAILED: {e}", flush=True)
            traceback.print_exc()

    # Copy the console config files (cfg/*.cfg -- the alias/cvar tables, incl. user.cfg's
    # Basic/Plus `patchtype` alias) verbatim into out/cfg. Whole-game like the other mirrors,
    # so once after the loop and on a zero-map invocation too. The runtime console bridge
    # (roadmap 9.3b) seeds its alias/cvar store from this; consumer per docs/python_bridge.md.
    if not skip_cfg:
        print("\n[cfg] copying console config files ...", flush=True)
        try:
            CF.main()
        except Exception as e:
            print(f"[cfg] FAILED: {e}", flush=True)
            traceback.print_exc()

    # Batch-export the NPCs the exported maps reference (PL4): per-NPC mesh glbs (mesh + skeleton +
    # own clips), the shared animation banks each once, and out/npc/npc_manifest.json. Opt-in (--npc):
    # the include-tree decode is the heaviest offline pass. Standard glTF (self-describing space),
    # loaded via glTFRuntime, retargeted onto NPC skeletons by bone name at runtime (roadmap 8.5).
    # Scans out/*/*.ents, so it runs after the map loop.
    if "--npc" in args:
        print("\n[npc] batch NPC export (mesh glbs + shared banks + manifest) ...", flush=True)
        try:
            import npc_export
            npc_export.main()
        except Exception as e:
            print(f"[npc] FAILED: {e}", flush=True)
            traceback.print_exc()

    if not skip_content:
        build_content()

    # Last: the Lumen card bake. It loads each map in the engine, so it needs the committed
    # Content/ assets (the master materials the map build binds) to be current -- hence after
    # build_content. Only the maps that actually re-exported this run need re-baking.
    if not skip_cards:
        bake_cards([name for name, status, _ in results if status == "ok"])


if __name__ == "__main__":
    main()
