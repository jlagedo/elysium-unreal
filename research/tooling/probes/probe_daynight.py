"""RE-A4: what selects VtMB's day vs night lightmap bake (K4)?

`dface_t` v17 reserves three 8-byte lightstyle arrays where modern Source has one:
`styles[8]` @48, `day[8]` @56, `night[8]` @64 (docs/vtmb/sky-ambience.md, pipeline/CLAUDE.md).
The open question is which of the two extra sets the shipped data uses and what picks
between them -- because whichever one lump 8 actually holds is our calibration target.

This is the data half of RE-A4 (the decompile half is `engine.dll`'s `Mod_LoadFaces`,
0x200b73d0). It scans every map in the install and reports four independent things:

  1. the exact byte histogram of `styles[]`, `day[]` and `night[]` over every face --
     if a day/night bake were authored, those arrays carry lightstyle indices;
  2. **byte closure** of lump 8 against a *single* bake: per lit face, the span to the
     next face's `lightofs` divided by one luxel grid gives the number of stored luxel
     sets. `sets == nstyles` is one plain bake, `sets == 4*nstyles` is a bumped bake
     (NUM_BUMP_VECTS+1). Anything at `2*` or `8*` would be two bakes sharing the lump;
  3. every `worldspawn` key in the game, so a day/night selector keyvalue cannot hide;
  4. any entity key or value anywhere matching day/night, and the lightstyle-carrying
     `light*` entities' `style`/`pattern` keys.

Usage:
    python research/tooling/probes/probe_daynight.py              # every map in the install (108)
    python research/tooling/probes/probe_daynight.py sp_tutorial_1 sm_hub_1
    python research/tooling/probes/probe_daynight.py --verbose    # per-map lines, not just the rollup
"""
import collections
import re
import struct
import sys
from pathlib import Path

from elysium_pipeline.formats import bsp, install

FS = bsp.FACE_SIZE          # 104
OFS_STYLES, OFS_DAY, OFS_NIGHT = 48, 56, 64

DAYNIGHT_RE = re.compile(r"day|night|dusk|dawn|sunset|sunrise|time.?of.?day", re.I)


def scan_faces(data):
    """Byte histograms for the three lightstyle arrays + per-face set counts."""
    faces = bsp.read_lump(data, 7)
    lighting = bsp.read_lump(data, 8)
    nf = len(faces) // FS

    hist = {"styles": collections.Counter(), "day": collections.Counter(),
            "night": collections.Counter()}
    # A face "uses" an array if any of its 8 bytes is neither 0 nor 0xFF: 0 is
    # lightstyle 0 (the always-on baked style) and 0xFF is the unused sentinel,
    # so a live day/night set has to show some other index somewhere.
    live = {"styles": 0, "day": 0, "night": 0}
    nonzero = {"styles": 0, "day": 0, "night": 0}

    lit = []
    for fi in range(nf):
        b = fi * FS
        for name, ofs in (("styles", OFS_STYLES), ("day", OFS_DAY),
                          ("night", OFS_NIGHT)):
            arr = faces[b + ofs:b + ofs + 8]
            hist[name].update(arr)
            if any(x != 0 for x in arr):
                nonzero[name] += 1
            if any(x not in (0, 0xFF) for x in arr):
                live[name] += 1

        lo = struct.unpack_from("<i", faces, b + bsp.FACE_LIGHTOFS)[0]
        if lo == -1:
            continue
        sx, sy = struct.unpack_from("<ii", faces, b + bsp.FACE_LM_SIZE)
        one = (sx + 1) * (sy + 1) * 4          # bytes for one RGBE luxel grid
        ns = sum(1 for x in faces[b + OFS_STYLES:b + OFS_STYLES + 8] if x != 0xFF)
        lit.append((lo, one, ns))

    # Per-face closure: the span from a face's `lightofs` to the next face's is
    # how many bytes lump 8 spends on it. Summing the spans proves nothing --
    # they telescope to `len(lighting) - first lightofs` whatever the contents.
    # The test that bites is per face: divide the span by the face's style count
    # and measure it in whole luxel grids. A plain bake stores 1 grid per style;
    # a bumped bake stores NUM_BUMP_VECTS+1 = 4, preceded by one 4-byte average
    # colour. A *second* full bake sharing the lump would read as 2 and 8.
    lit.sort()
    pairs = collections.Counter()
    ragged = 0
    for k, (lo, one, ns) in enumerate(lit):
        nxt = lit[k + 1][0] if k + 1 < len(lit) else len(lighting)
        if nxt == lo:                          # faces sharing one grid
            continue
        if not one or not ns or (nxt - lo) % ns:
            ragged += 1
            continue
        per_style = (nxt - lo) // ns
        pairs[(per_style // one, per_style % one)] += 1

    return {"faces": nf, "hist": hist, "live": live, "nonzero": nonzero,
            "pairs": pairs, "lump8": len(lighting), "ragged": ragged,
            "prefix": lit[0][0] if lit else 0, "lit": len(lit)}


def scan_entities(data):
    """worldspawn keys, day/night-shaped keys or values, and lightstyle keys."""
    text = bsp.read_lump(data, 0).split(b"\x00")[0].decode("latin-1")
    blocks = [re.findall(r'"([^"]*)"\s+"([^"]*)"', b)
              for b in re.findall(r"\{([^{}]*)\}", text, re.S)]

    ws_keys, hits, styles = collections.Counter(), [], collections.Counter()
    for kvs in blocks:
        d = dict(kvs)
        cls = d.get("classname", "")
        if cls == "worldspawn":
            ws_keys.update(k for k, _ in kvs)
        for k, v in kvs:
            # ignore targetnames/models/scripts -- only lighting-relevant hits
            if k in ("classname", "model", "targetname", "message"):
                continue
            if DAYNIGHT_RE.search(k) or (DAYNIGHT_RE.search(v) and cls.startswith("light")):
                hits.append((cls, k, v))
        if cls.startswith("light"):
            if "style" in d:
                styles[("style", d["style"])] += 1
            if "pattern" in d:
                styles[("pattern", d["pattern"])] += 1
    return ws_keys, hits, styles


def main(argv):
    verbose = "--verbose" in argv
    names = [a for a in argv if not a.startswith("-")]
    idx = install.build_index()
    if not names:
        names = install.all_map_names()

    agg = {"styles": collections.Counter(), "day": collections.Counter(),
           "night": collections.Counter()}
    agg_live = collections.Counter()
    agg_nonzero = collections.Counter()
    agg_pairs = collections.Counter()
    ws_keys = collections.Counter()
    ws_maps = collections.Counter()
    all_hits, style_keys = [], collections.Counter()
    tot_faces = tot_lit = 0
    residuals = []

    for nm in names:
        try:
            data = install.read(idx, "maps/%s.bsp" % nm)
        except Exception as e:                              # noqa: BLE001
            print("  ! %s: %s" % (nm, e))
            continue
        f = scan_faces(data)
        wk, hits, sk = scan_entities(data)

        for a in agg:
            agg[a].update(f["hist"][a])
            agg_live[a] += f["live"][a]
            agg_nonzero[a] += f["nonzero"][a]
        agg_pairs.update(f["pairs"])
        ws_keys.update(wk)
        ws_maps.update(set(wk))
        style_keys.update(sk)
        all_hits += [(nm, *h) for h in hits]
        tot_faces += f["faces"]
        tot_lit += f["lit"]
        residuals.append((f["ragged"], nm, f["prefix"], f["lump8"]))

        if verbose:
            print("%-20s faces=%-6d lit=%-6d  live day/night=%d/%d  "
                  "lump8=%-9d ragged=%d prefix=%d"
                  % (nm, f["faces"], f["lit"], f["live"]["day"],
                     f["live"]["night"], f["lump8"], f["ragged"], f["prefix"]))

    n = len(names)
    print("\n=== RE-A4: day/night bake selection — %d maps, %d faces (%d lit) ==="
          % (n, tot_faces, tot_lit))

    print("\n-- dface_t lightstyle arrays --")
    for a in ("styles", "day", "night"):
        top = ", ".join("0x%02x:%d" % (k, v) for k, v in sorted(agg[a].items()))
        print("  %-6s @%-2d  faces with any non-zero byte: %-7d  with a live index: %d"
              % (a, {"styles": 48, "day": 56, "night": 64}[a],
                 agg_nonzero[a], agg_live[a]))
        print("         byte histogram: %s" % (top if len(top) < 400 else top[:400] + " ..."))

    print("\n-- lump 8 per-face closure: bytes per lightstyle --")
    print("  (grids = whole luxel grids stored per style; extra = bytes left over)")
    for (grids, extra), c in sorted(agg_pairs.items(), key=lambda kv: -kv[1])[:12]:
        print("  grids=%-3d extra=%-5d  %d faces" % (grids, extra, c))
    residuals.sort(reverse=True)
    print("  faces whose span is not divisible by the style count: %d (worst: %s)"
          % (sum(r for r, _, _, _ in residuals),
             ", ".join("%s %d" % (m, r) for r, m, _, _ in residuals[:4])))
    print("  bytes before the first face's lightofs: %s"
          % ", ".join(sorted({str(p) for _, _, p, _ in residuals})))

    print("\n-- worldspawn keys (key: maps carrying it, of %d) --" % n)
    for k, c in ws_maps.most_common():
        print("  %-24s %d" % (k, c))

    print("\n-- day/night-shaped entity keys/values --")
    print("  %s" % (all_hits if all_hits else "none"))

    print("\n-- light* entity style/pattern keys --")
    for (k, v), c in style_keys.most_common(20):
        print("  %-8s %-24s %d" % (k, repr(v)[:24], c))


if __name__ == "__main__":
    main(sys.argv[1:])
