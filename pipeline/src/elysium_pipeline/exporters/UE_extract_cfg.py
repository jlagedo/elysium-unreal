"""Copy VtMB's console config files (`cfg/*.cfg`) verbatim into $ELYSIUM_EXPORT_ROOT/cfg.

Asset-delivery step for the console/scripting surface (roadmap PL5d). VtMB's `cfg/` tree is
the engine's alias + cvar + keybind table in Valve console syntax. It is the fifth scripting
surface (`docs/vtmb/python_bridge.md`): a level script runs a console command by attribute-assigning
on `__main__.ccmd` (`c.patchtype = ""` executes the alias `patchtype`), and an unrecognised
console command falls through to Python. The **Unofficial Patch's Basic/Plus switch rides on
exactly this** -- its installer writes one of two `user.cfg` files differing only in
`alias patchtype "setBasic()"` vs `"setPlus()"`, so `setPlus`/`setBasic` are named nowhere in
the `.py`/`.ents`/`.dlg`/`.bsp` trees; the sole reference is that one `.cfg` line. The copied file
stays verbatim as install provenance. After seeding its alias/cvar store from this mirror, the
runtime console bridge (roadmap 9.3b) replaces only `patchtype` with Elysium's Plus selector, so
the source install's personal installer choice does not change the game's content profile.

Copied **verbatim** -- no parse, no transcode -- same bring-your-own-game posture as the
script/dialogue/sign/vdata mirrors: output lives under $ELYSIUM_EXPORT_ROOT/ (gitignored, regenerable),
resolved patch-first (patch loose > retail loose > VPK), the engine's own search order.

Whole-game, not map-scoped, so `export_all.py` runs it once at the end of a run
(`--no-cfg` to skip).

Usage:
  uv run elysium export bundle cfg            # copy cfg files
  uv run elysium export bundle cfg --force    # re-copy even files already present
"""
import os
import sys

from elysium_pipeline.formats import install, vpk
from elysium_pipeline.paths import export_root

OUT = os.fspath(export_root())

ROOT = "cfg"
EXTS = (".cfg",)


def collect(index=None):
    """Merged {install-rel-key -> (dest_rel, kind, ref)} for every `cfg/*.cfg`, resolved
    patch-first (patch loose > retail loose > VPK). `dest_rel` keeps the subtree under
    `cfg/` (flat in practice), so the mirror preserves the layout.

    ``index`` is a shared `install.build_index()` table used as the base layer instead of
    re-indexing the VPKs. Its tagged loose entries are re-walked below at their correct
    precedence, so seeding from it yields the identical merge."""
    prefix = ROOT + "/"
    picked = {}
    # Lowest precedence: the VPKs (keys already lowercased by vpk.index_all).
    base = (index.items() if index is not None
            else ((k, ("vpk", e)) for k, e in vpk.index_all(install.GAME).items()))
    for key, (kind, ref) in base:
        if kind != "vpk":
            continue    # loose entries enter through the re-walk below
        if key.startswith(prefix) and key.endswith(EXTS):
            picked[key] = (key[len(prefix):], kind, ref)
    # Then retail loose, then patch loose -- each root shadows the one before it.
    for base_root in (install.GAME, install.PATCH):
        base = os.path.join(base_root, ROOT)
        for dirpath, _, files in os.walk(base):
            for fn in files:
                if not fn.lower().endswith(EXTS):
                    continue
                p = os.path.join(dirpath, fn)
                rel = os.path.relpath(p, base).replace("\\", "/")
                picked[(prefix + rel).lower()] = (rel, "loose", p)
    return picked


def extract(picked, dest_root, force=False):
    """Copy each collected file verbatim into `dest_root/<dest_rel>`. Returns
    (written, cached). A file already on disk is left untouched unless `force`."""
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


def main(force=False, index=None):
    picked = collect(index=index)
    dest_root = os.path.join(OUT, "cfg")
    written, cached = extract(picked, dest_root, force=force)
    print(f"[cfg] {len(picked)} files ({written} copied, {cached} already present) "
          f"-> {os.path.relpath(dest_root, OUT)}/", flush=True)


if __name__ == "__main__":
    main(force="--force" in sys.argv[1:])
