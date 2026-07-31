"""Emit .dds siblings for a map's exported albedo PNGs, preserving the original
DXT blocks and mip chain from the game's .tth/.ttz textures (no re-encode).

The exported tex/<flat>.png files (world surfaces) and props/tex/<flat>.png files
(static props) decompress the game's DXT data to RGBA and drop the mip chain; engines
that can sample DXT directly (Unreal loads these at runtime) get smaller uploads, real
mips, and bit-identical texels by reading the original blocks instead. PNGs stay as the
reference/fallback (and cover generated textures with no .tth source: _ke self-illum,
_envmask, normal maps).

  py retex_dds.py [map ...]      default: every exported map under $ELYSIUM_EXPORT_ROOT
"""
import glob
import os
import struct
import sys
import zlib

from elysium_pipeline.formats import install
from elysium_pipeline.formats import tex_to_png as t2p
from elysium_pipeline.paths import export_root

OUT = os.fspath(export_root())

DDSD_CAPS, DDSD_HEIGHT, DDSD_WIDTH, DDSD_PIXELFORMAT = 0x1, 0x2, 0x4, 0x1000
DDSD_MIPMAPCOUNT, DDSD_LINEARSIZE = 0x20000, 0x80000
DDSCAPS_COMPLEX, DDSCAPS_TEXTURE, DDSCAPS_MIPMAP = 0x8, 0x1000, 0x400000


def make_dds_mipchain(mips, w, h, fourcc):
    """A DDS with a full mip chain: `mips` is [(bytes, w, h)] largest-first."""
    header = bytearray(128)
    flags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | DDSD_LINEARSIZE
    caps = DDSCAPS_TEXTURE
    if len(mips) > 1:
        flags |= DDSD_MIPMAPCOUNT
        caps |= DDSCAPS_COMPLEX | DDSCAPS_MIPMAP
    struct.pack_into("<4sIIIIIII", header, 0,
                     b"DDS ", 124, flags, h, w, len(mips[0][0]), 0, len(mips))
    struct.pack_into("<II4s", header, 76, 32, 0x4, fourcc)   # DDPF_FOURCC
    struct.pack_into("<I", header, 108, caps)
    return bytes(header) + b"".join(m[0] for m in mips)


def extract_mipchain(tth, ttz):
    """The DXT mip chain from a texture pair, largest-first, or None if not DXT.

    The .ttz holds the pyramid smallest->largest (full-res LAST); some chains keep
    their smallest mips in the .tth instead, so the chain is walked backward from
    the tail for as many whole mips as the data actually contains."""
    w, h, fmt, mip_count = t2p.parse_tth(tth)
    if fmt not in t2p.DXT_FOURCC:
        return None, None, None
    data = zlib.decompress(ttz)

    mips = []
    end = len(data)
    for i in range(mip_count):
        mw, mh = max(1, w >> i), max(1, h >> i)
        size = t2p.mip_byte_size(mw, mh, fmt)
        if size > end:
            break
        mips.append((data[end - size:end], mw, mh))
        end -= size
    return mips, (w, h), t2p.DXT_FOURCC[fmt]


def flat_index(idx):
    """materials/<path>.tth keyed by the exporter's flattened name ('/' -> '_').
    Colliding flat names are dropped (ambiguous source) and reported."""
    flat, dropped = {}, set()
    for key in idx:
        if not (key.startswith("materials/") and key.endswith(".tth")):
            continue
        name = key[len("materials/"):-len(".tth")].replace("/", "_")
        if name in flat:
            dropped.add(name)
        else:
            flat[name] = key
    for name in dropped:
        flat.pop(name, None)
    return flat, dropped


def albedo_pngs(mtl_path):
    """The map_Kd tex/*.png references of an exported .mtl (albedo only: other
    channels are generated PNGs with no direct .tth source)."""
    refs = set()
    with open(mtl_path, encoding="utf-8") as f:
        for line in f:
            parts = line.split()
            if len(parts) >= 2 and parts[0] == "map_Kd" and parts[1].startswith("tex/"):
                refs.add(parts[1])
    return sorted(refs)


def emit_refs(idx, flat, dropped, refs, tex_dir):
    """Emit a .dds sibling into tex_dir for each 'tex/<stem>.png' albedo ref,
    reading the original DXT blocks from that stem's .tth/.ttz. Returns
    (done, skipped_non_dxt, missing)."""
    done = skipped = missing = 0
    for rel in refs:
        stem = os.path.splitext(os.path.basename(rel))[0]
        dds_path = os.path.join(tex_dir, stem + ".dds")
        if os.path.isfile(dds_path):
            done += 1
            continue
        key = flat.get(stem)
        if key is None:
            missing += 1
            if stem in dropped:
                print(f"    ambiguous source for {stem}, skipped")
            continue
        tth = install.read(idx, key)
        ttz = install.read(idx, key[:-4] + ".ttz")
        if ttz is None:
            missing += 1
            continue
        mips, _, fourcc = extract_mipchain(tth, ttz)
        if not mips:
            skipped += 1          # non-DXT (BGR888 etc.): PNG fallback covers it
            continue
        with open(dds_path, "wb") as f:
            f.write(make_dds_mipchain(mips, mips[0][1], mips[0][2], fourcc))
        done += 1
    return done, skipped, missing


def emit_dir(idx, flat, dropped, map_dir, base):
    """Emit DDS siblings for one exported map directory: world <base>.mtl -> tex/
    and every props/*.mtl -> props/tex/. Callable from the exporter (which passes
    the install index it already built) or from this module's map-name CLI."""
    mtl = os.path.join(map_dir, base + ".mtl")
    if not os.path.isfile(mtl):
        print(f"  {base}: no .mtl, skipped")
        return

    done, skipped, missing = emit_refs(
        idx, flat, dropped, albedo_pngs(mtl), os.path.join(map_dir, "tex"))
    print(f"  {base}: {done} dds, {skipped} non-dxt, {missing} unsourced")

    # Props share one props/tex/ across their per-model .mtl files (many models
    # reference the same texture, so the refs are deduped before emitting).
    prop_mtls = sorted(glob.glob(os.path.join(map_dir, "props", "*.mtl")))
    if prop_mtls:
        refs = sorted({r for m in prop_mtls for r in albedo_pngs(m)})
        pdone, pskipped, pmissing = emit_refs(
            idx, flat, dropped, refs, os.path.join(map_dir, "props", "tex"))
        print(f"    props: {pdone} dds, {pskipped} non-dxt, {pmissing} unsourced")


def emit_map(idx, flat, dropped, map_name):
    emit_dir(idx, flat, dropped, os.path.join(OUT, map_name), map_name)


def main():
    maps = sys.argv[1:]
    if not maps:
        maps = sorted(d for d in os.listdir(OUT)
                      if os.path.isfile(os.path.join(OUT, d, d + ".obj")))
    idx = install.build_index()
    flat, dropped = flat_index(idx)
    print(f"emitting dds for: {', '.join(maps)}")
    for m in maps:
        emit_map(idx, flat, dropped, m)


if __name__ == "__main__":
    main()
