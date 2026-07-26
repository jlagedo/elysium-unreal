"""Copy VtMB's choreo scenes, phoneme files and expression tables verbatim into out/.

Asset-delivery step for the choreography + lipsync track (roadmap PL9, PL10). All three
trees are plain text needing no transcode:

  * **`sound/**/*.vcd`** -> `out/scenes/` -- Faceposer choreo scenes, the unit of a cinematic
    *and* of a spoken line (a `logic_choreographed_scene`'s `SceneFile`, or the per-line scene
    the engine plays through `CInstancedSceneEntity`). Uniform word-list/brace grammar, no
    binary decode, no coordinate space. Format + event semantics:
    `docs/choreographed_scenes.md` (RE19).
  * **`sound/**/*.lip`** -> `out/lip/` -- the phoneme sidecar beside a line's audio
    (`<line>.wav` -> `<line>.lip`): which phoneme is on screen when.
  * **`expressions/*.txt`** -> `out/expressions/` -- the phoneme -> flex-controller weight
    tables, chosen by the actor's model basename (`lacroix.mdl` ->
    `lacroix_phonemes.txt`). The shipped `.vfe` is Faceposer's compiled form of the same
    data, so only the readable `.txt` is mirrored.

The last two are two thirds of 12.5's per-line join (the third is the model's own
`mstudiomouth_t`, which rides in the NPC export); both formats are RE20, and only the bytes
are delivered here.

Copied **verbatim** -- no parse, no transcode -- same bring-your-own-game posture as the
script/dialogue/sign/vdata/cfg mirrors: output lives under out/ (gitignored, regenerable),
resolved patch-first (patch loose > retail loose > VPK), the engine's own search order. Each
tree's own root prefix is stripped, so a mirror keeps the subtree the engine addresses:
`SceneFile "sound/CINEMATIC/tutorial/jack_VS_sabbat.vcd"` ->
`out/scenes/CINEMATIC/tutorial/jack_VS_sabbat.vcd`, matching how `out/sound/` is laid out.
The two `sound/` extensions get separate mirrors because they are separate consumers -- 12.1
reads the scenes, 12.5 the phonemes -- and their sub-paths otherwise interleave file-for-file.

Whole-game, not map-scoped, so `export_all.py` runs it once at the end of a run
(`--no-scenes` to skip).

Usage:
  python tools/UE_extract_scenes.py            # copy scenes + phonemes + expressions
  python tools/UE_extract_scenes.py --force    # re-copy even files already present
"""
import glob
import json
import os
import sys

import install
import vpk

TOOLS = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(TOOLS, "out")

SCENE_ROOT = "sound"
#: (install root, extension, mirror name under out/). Roots are walked once each.
TREES = ((SCENE_ROOT, ".vcd", "scenes"), (SCENE_ROOT, ".lip", "lip"),
         ("expressions", ".txt", "expressions"))


def collect():
    """Merged {(root, ext): {install-rel-key -> (dest_rel, kind, ref)}} for every tree,
    resolved patch-first (patch loose > retail loose > VPK). `dest_rel` is the path under the
    mirror root (the install root prefix stripped), so each mirror preserves the engine's own
    subtree; loose sources keep their authored case, VPK sources use the lowercased index
    key."""
    picked = {(r, e): {} for r, e, _ in TREES}
    roots = {r for r, _, _ in TREES}
    # Lowest precedence: the VPKs (keys already lowercased by vpk.index_all).
    for key, entry in vpk.index_all(install.GAME).items():
        root = key.split("/", 1)[0]
        ext = os.path.splitext(key)[1]
        if (root, ext) in picked and key.startswith(root + "/"):
            picked[(root, ext)][key] = (key[len(root) + 1:], "vpk", entry)
    # Then retail loose, then patch loose -- each root shadows the one before it.
    for base_root in (install.GAME, install.PATCH):
        for root in sorted(roots):
            base = os.path.join(base_root, root)
            for dirpath, _, files in os.walk(base):
                for fn in files:
                    ext = os.path.splitext(fn)[1].lower()
                    if (root, ext) not in picked:
                        continue
                    p = os.path.join(dirpath, fn)
                    rel = os.path.relpath(p, base).replace("\\", "/")
                    picked[(root, ext)][f"{root}/{rel}".lower()] = (rel, "loose", p)
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


def check_referenced(scenes):
    """Cross-check the `SceneFile` of every `logic_choreographed_scene` in the already-exported
    maps against the mirror, and report the ones the install does not ship.

    A `SceneFile` naming no file is map data outliving its assets (RE19 counts eight, on three
    maps) -- breakage worth surfacing once, not a format question. Silent when no map is
    exported yet: the mirror itself is whole-game and does not depend on the map export."""
    referenced = {}
    for p in sorted(glob.glob(os.path.join(OUT, "*", "*.ents"))):
        try:
            with open(p, encoding="utf-8") as f:
                doc = json.load(f)
        except (OSError, ValueError):
            continue
        for ent in doc.get("entities", []):
            if (ent.get("classname") or "").lower() != "logic_choreographed_scene":
                continue
            sf = ent.get("keys", {}).get("SceneFile")
            if isinstance(sf, str) and sf.strip():
                referenced.setdefault(sf.replace("\\", "/").lower(),
                                      set()).add(os.path.basename(p)[:-len(".ents")])
    if not referenced:
        return
    prefix = SCENE_ROOT + "/"
    missing = {k: v for k, v in referenced.items()
               if not (k.startswith(prefix) and k[len(prefix):] in scenes)}
    print(f"[scenes] {len(referenced)} SceneFile value(s) named by the exported maps, "
          f"{len(referenced) - len(missing)} resolve", flush=True)
    for key in sorted(missing):
        print(f"  ! not in the install: {key}  <- {', '.join(sorted(missing[key]))}", flush=True)


def main(force=False):
    picked = collect()
    scenes = set()
    for root, ext, out_name in TREES:
        files = picked[(root, ext)]
        dest_root = os.path.join(OUT, out_name)
        written, cached = extract(files, dest_root, force=force)
        print(f"[{out_name}] {len(files)} files ({written} copied, {cached} already present) "
              f"-> {os.path.relpath(dest_root, TOOLS)}/", flush=True)
        if ext == ".vcd":
            scenes = {dest_rel.lower() for dest_rel, _, _ in files.values()}
    check_referenced(scenes)


if __name__ == "__main__":
    main(force="--force" in sys.argv[1:])
