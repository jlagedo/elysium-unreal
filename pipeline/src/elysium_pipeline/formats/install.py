"""The game install as the engine sees it: one search path, patch before retail.

The engine resolves loose search paths before the VPKs, and the Unofficial Patch
installs as one. Reading the VPKs alone reads assets the install does not run:
2507 patch files shadow the VPKs, and the two diverge in geometry, not just look
(patched `sp_tutorial_1.bsp` is 19 MB / 15,610 faces against retail's 5 MB /
7,580, with brushwork and `info_player_start` moved).

`build_index` merges the install into one table keyed by lowercase,
forward-slashed, install-relative paths -- the shape `vpk.index_all` produces --
with loose entries shadowing VPK ones. Every read goes through it:

    idx = install.build_index()
    data = install.read(idx, "materials/building/chinabldg04.vmt")

Values are tagged `("loose", path)` or `("vpk", entry)` so `read` knows where the
bytes live.
"""
import os
import threading

from elysium_pipeline.formats import vpk
from elysium_pipeline.paths import vtmb_root

GAME_ROOT = os.fspath(vtmb_root())
GAME = os.path.join(GAME_ROOT, "Vampire")            # retail: loose files + the VPKs
PATCH = os.path.join(GAME_ROOT, "Unofficial_Patch")  # the loose search path

# Additional loose search paths in engine order. These shadow the retail loose tree, which in turn
# shadows the retail VPKs. GAME remains separate because callers use LOOSE_ROOTS specifically for
# patch/add-on roots.
LOOSE_ROOTS = [PATCH]

# The trees the converters read from, so the walk stays cheap. The patch also
# ships cfg/save/sound/python/dlg; nothing indexes those through here yet.
ASSET_DIRS = ("materials", "models", "maps", "resource", "particles", "scripts", "vdata")
#: Directories below `maps/` the retail engine writes at run time; never pipeline inputs.
RUNTIME_CACHE_DIRS = frozenset({"graphs", "soundcache"})


# One process-lifetime index per (roots, dirs) key. The install is read-only while the
# pipeline runs, so a rebuild of the same key walks the same trees and pack directories
# for the same answer; callers share one index object instead.
_INDEX_LOCK = threading.Lock()
_INDEX_CACHE = {}


def build_index(dirs=ASSET_DIRS, verbose=True):
    """Index the install the way the engine searches it: loose files shadow VPKs.

    Memoized for the process, keyed on the search roots and `dirs`: a repeat call with
    the same key returns the same index object. `verbose` only controls whether the
    summary line prints -- it never forks or defeats the cache. `invalidate_index_cache`
    drops the memo for a caller that knows the install changed.
    """
    key = (GAME, tuple(LOOSE_ROOTS), tuple(dirs))
    with _INDEX_LOCK:
        built = _INDEX_CACHE.get(key)
        if built is None:
            built = _INDEX_CACHE[key] = _build_index(dirs)
        idx, summary = built
    if verbose:
        print(summary)
    return idx


def _build_index(dirs):
    """One full walk over the VPKs and loose trees -> (index, summary line)."""
    idx = {k: ("vpk", v) for k, v in vpk.index_all(GAME).items()}
    shadowed = added = 0
    # Assignment into idx is last-writer-wins, so walk the engine's loose search path from lowest
    # to highest precedence: retail loose first, then patch/add-on roots in reverse search order.
    for root in reversed(LOOSE_ROOTS + [GAME]):
        for sub in dirs:
            for dirpath, subdirs, files in os.walk(os.path.join(root, sub)):
                # The retail engine writes its own caches below `maps/` while it runs -- AI node
                # graphs and sound caches -- so a play session would otherwise re-stamp the
                # index and invalidate every export receipt. Nothing here reads them.
                if os.path.relpath(dirpath, root).replace("\\", "/").lower() == "maps":
                    subdirs[:] = [d for d in subdirs if d.lower() not in RUNTIME_CACHE_DIRS]
                for fn in files:
                    p = os.path.join(dirpath, fn)
                    rel = os.path.relpath(p, root).replace("\\", "/").lower()
                    if rel in idx:
                        shadowed += 1
                    else:
                        added += 1
                    idx[rel] = ("loose", p)
    summary = (f"  install: {len(idx)} files "
               f"({shadowed} loose overrides shadow the VPKs, {added} loose-only)")
    return idx, summary


def invalidate_index_cache():
    """Forget every memoized index; the next `build_index` call rebuilds."""
    with _INDEX_LOCK:
        _INDEX_CACHE.clear()


def read(idx, key):
    """Bytes for an install-relative path, or None if the install lacks it."""
    e = idx.get(key.lower().replace("\\", "/"))
    if not e:
        return None
    kind, v = e
    if kind == "loose":
        with open(v, "rb") as f:
            return f.read()
    return vpk.extract(v)


def map_path(name):
    """Resolve a map name (or a path) to the .bsp the engine would load.

    A caller-supplied path is honoured as-is; a bare name resolves patch-first, so
    `sp_tutorial_1` is the patch's 19 MB map, not retail's 5 MB one.
    """
    if os.path.sep in name or "/" in name or os.path.exists(name):
        return name
    stem = name[:-4] if name.lower().endswith(".bsp") else name
    for root in LOOSE_ROOTS + [GAME]:
        p = os.path.join(root, "maps", stem + ".bsp")
        if os.path.exists(p):
            return p
    raise FileNotFoundError(f"no map '{stem}' in the install")


def all_map_names():
    """Every map stem the engine could load — the patch-first union of `<root>/maps/*.bsp`
    across the search path, deduped by stem. Patch-only maps are included; a name here
    resolves patch-first through `map_path` (the patch `.bsp` shadows retail's). Returns the
    names, not paths, so callers export what the engine runs, not what the retail tree holds."""
    names = set()
    for root in LOOSE_ROOTS + [GAME]:
        d = os.path.join(root, "maps")
        if not os.path.isdir(d):
            continue
        for fn in os.listdir(d):
            if fn.lower().endswith(".bsp"):
                names.add(fn[:-4])
    return sorted(names)


if __name__ == "__main__":
    import sys
    idx = build_index()
    for key in sys.argv[1:]:
        e = idx.get(key.lower())
        print(f"  {key}: {'missing' if not e else e[0] + ' ' + str(e[1])}")
