"""Copy the game's loose Python level scripts + dialogue into $ELYSIUM_EXPORT_ROOT/scripts and $ELYSIUM_EXPORT_ROOT/dlg.

P5 5.1 (PL2) asset-delivery step. VtMB runs its story on **loose plain-text** files:
Python 2.1 level scripts (`python/**/*.py`) and `dlgexpr` dialogue (`dlg/**/*.dlg`). This
step copies them **verbatim** -- no parse, no transcode -- into a shared, game-global
mirror (`$ELYSIUM_EXPORT_ROOT/scripts/<rel>`, `$ELYSIUM_EXPORT_ROOT/dlg/<rel>`), so the runtime scripting host (5.2+) reads
them from disk. Same bring-your-own-game posture as the maps/textures/sounds: output lives
under $ELYSIUM_EXPORT_ROOT/ (gitignored, regenerable).

Resolution is patch-first, matching the engine's search order -- the Unofficial Patch's
loose copy shadows the retail loose copy, which shadows the VPK. Two format facts drive it:

  * Scripts: only the **loose `.py`** run. The 24 VPK `.pyc` are stale + unreachable
    (CPython 2.1 predates `zipimport`, and can't read a VPK), so `.pyc` are ignored
    entirely; retail (`Vampire/python/`) and patch (`Unofficial_Patch/python/`) each ship a
    loose tree, and neither is a strict superset of the other -- both are merged.
  * Dialogue: retail `.dlg` ship inside the VPKs; the patch overlays a loose `dlg/` tree.

Scripts/dialogue are whole-game, not map-scoped, so one mirror is written (not per-map).

Usage:
  python pipeline/src/elysium_pipeline/exporters/UE_extract_scripts.py            # copy scripts + dialogue
  python pipeline/src/elysium_pipeline/exporters/UE_extract_scripts.py --force    # re-copy even files already present
"""
import os
import sys

from elysium_pipeline.formats import install, vpk
from elysium_pipeline.paths import export_root

OUT = os.fspath(export_root())


def collect(subdir, exts):
    """Merged {install-rel-key -> (dest_rel, kind, ref)} for `<subdir>/`, filtered to
    `exts`, resolved patch-first (patch loose > retail loose > VPK -- each shadows the
    prior). `dest_rel` is the path under the mirror root (the `<subdir>/` prefix stripped);
    loose sources keep their authored case, VPK sources use the lowercased index key."""
    exts = tuple(e.lower() for e in exts)
    prefix = subdir + "/"
    picked = {}
    # Lowest precedence: the VPKs (keys already lowercased by vpk.index_all).
    for key, entry in vpk.index_all(install.GAME).items():
        if key.startswith(prefix) and key.endswith(exts):
            picked[key] = (key[len(prefix):], "vpk", entry)
    # Then retail loose, then patch loose -- each root shadows the one before it.
    for root in (install.GAME, install.PATCH):
        base = os.path.join(root, subdir)
        for dirpath, _, files in os.walk(base):
            for fn in files:
                if not fn.lower().endswith(exts):
                    continue
                p = os.path.join(dirpath, fn)
                rel = os.path.relpath(p, base).replace("\\", "/")
                picked[(prefix + rel).lower()] = (rel, "loose", p)
    return picked


def extract(picked, dest_root, force=False):
    """Copy each collected file verbatim into `dest_root/<dest_rel>`. Returns
    (written, cached). A file already on disk is left untouched unless `force` (cheap
    re-runs -- like the sound mirror)."""
    written = cached = 0
    for key in sorted(picked):
        dest_rel, kind, ref = picked[key]
        dest = os.path.join(dest_root, *dest_rel.split("/"))
        if not force and os.path.exists(dest):
            cached += 1
            continue
        data = vpk.extract(ref) if kind == "vpk" else open(ref, "rb").read()
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        with open(dest, "wb") as f:
            f.write(data)
        written += 1
    return written, cached


def main(force=False):
    for subdir, exts, out_name in (("python", (".py",), "scripts"),
                                   ("dlg", (".dlg",), "dlg")):
        picked = collect(subdir, exts)
        dest_root = os.path.join(OUT, out_name)
        written, cached = extract(picked, dest_root, force=force)
        print(f"[{out_name}] {len(picked)} files ({written} copied, {cached} already present) "
              f"-> {os.path.relpath(dest_root, OUT)}/", flush=True)


if __name__ == "__main__":
    main(force="--force" in sys.argv[1:])
