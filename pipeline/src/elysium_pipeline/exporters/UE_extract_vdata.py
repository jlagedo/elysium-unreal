"""Copy VtMB's plain-text data tables (`vdata/`) verbatim into $ELYSIUM_EXPORT_ROOT/vdata.

Asset-delivery step for the RPG/rules layer (roadmap PL5b, broadened). `vdata/` is VtMB's
entire rulebook in Valve-KeyValues text: the character sheet (stats/feats/traiteffects),
the rules constants, dice tables, clans + chargen, disciplines, quests, items + weapons +
vendors, stealth, NPC disposition/reactions, per-category sound schemes, UI strings, camera
shots, and the hacking-terminal content. The engine (`vampire.dll`) loads these by name at
runtime; the offline seam mirrors them so the C++ runtime reads them from disk as its
consumers are built (per `docs/vtmb/vdata-catalog.md`, which maps each table to its system + task).

Copied **verbatim** -- no parse, no transcode -- same bring-your-own-game posture as the
script/dialogue/sign mirrors: output lives under $ELYSIUM_EXPORT_ROOT/ (gitignored, regenerable), resolved
patch-first (patch loose > retail loose > VPK), the engine's own search order.

Two subtrees are deliberately excluded:
  * `vdata/signs/` -- owned by `UE_extract_signs.py` (it also decodes the sign background art
    into $ELYSIUM_EXPORT_ROOT/signs), so mirroring it here would duplicate.
  * `*.xls` -- `stealth.xls` is a design-source spreadsheet, not an engine-loaded table.

The hunter/vampire trait split is left intact: `<name>.txt` is byte-identical to
`<name> - vampire.txt` (what the shipped game loads); `<name> - hunter.txt` is the cut
companion-mode variant. All are copied verbatim; consumers read the base (unsuffixed) file.

Usage:
  python pipeline/src/elysium_pipeline/exporters/UE_extract_vdata.py            # copy vdata tables
  python pipeline/src/elysium_pipeline/exporters/UE_extract_vdata.py --force    # re-copy even files already present
"""
import os
import sys

from elysium_pipeline.formats import install, vpk
from elysium_pipeline.paths import export_root

OUT = os.fspath(export_root())

ROOT = "vdata"
EXCLUDE_PREFIXES = ("vdata/signs/",)   # owned by UE_extract_signs.py
EXTS = (".txt",)                        # skip stealth.xls (design source, not engine data)


def collect():
    """Merged {install-rel-key -> (dest_rel, kind, ref)} for every `vdata/**` text table,
    resolved patch-first (patch loose > retail loose > VPK). `dest_rel` keeps the subtree
    under `vdata/` (e.g. `system/feats.txt`), so the mirror preserves the group layout.
    The `signs/` subtree and non-`.txt` files are dropped."""
    prefix = ROOT + "/"
    picked = {}
    # Lowest precedence: the VPKs (keys already lowercased by vpk.index_all).
    for key, entry in vpk.index_all(install.GAME).items():
        if key.startswith(prefix) and key.endswith(EXTS):
            picked[key] = (key[len(prefix):], "vpk", entry)
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
    return {k: v for k, v in picked.items()
            if not k.startswith(EXCLUDE_PREFIXES)}


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


def main(force=False):
    picked = collect()
    dest_root = os.path.join(OUT, "vdata")
    written, cached = extract(picked, dest_root, force=force)
    print(f"[vdata] {len(picked)} files ({written} copied, {cached} already present) "
          f"-> {os.path.relpath(dest_root, OUT)}/", flush=True)


if __name__ == "__main__":
    main(force="--force" in sys.argv[1:])
