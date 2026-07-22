"""Batch-export every VtMB map to out/<name>/ via bsp_to_scene.

Runs the full scene export (geometry, textures, lightmap, collision, sky, water,
static props) for each map, isolating failures so one bad map doesn't abort the run.

Usage:
  python tools/export_all.py                 # all maps
  python tools/export_all.py ch_hub_1 la_hub_1   # only the named maps
  python tools/export_all.py --skip-existing     # skip maps already exported
"""
import os, sys, glob, time, traceback
import bsp_to_scene as B
import install

MAPS = os.path.join(install.GAME, "maps")
OUT = "out"


def main():
    args = sys.argv[1:]
    skip_existing = "--skip-existing" in args
    only = [a for a in args if not a.startswith("--")]

    bsps = sorted(glob.glob(os.path.join(MAPS, "*.bsp")))
    if only:
        bsps = [b for b in bsps if os.path.splitext(os.path.basename(b))[0] in only]

    print(f"exporting {len(bsps)} maps -> {OUT}/<name>/", flush=True)
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


if __name__ == "__main__":
    main()
