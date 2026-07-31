"""Probe GAME_LUMP (35) + PAKFILE (40) for a VtMB BSP.

Confirms the Phase-0 unknowns on real maps: game-lump fourCC byte order, that
directory offsets are file-absolute, the `sprp` static-prop version + count +
model dictionary, and the PAKFILE file listing.

Usage: python research/tooling/probes/probe_gamelump.py <bsp> [<bsp> ...]
"""
import struct, sys, os
from elysium_pipeline.formats import bsp as B


def probe_sprp(payload, version):
    """Parse the sprp game-lump payload: model dict + leaf array + prop count.
    Returns (names, leaf_count, prop_count, prop_struct_size)."""
    p = 0
    name_count = struct.unpack_from("<i", payload, p)[0]; p += 4
    names = []
    for _ in range(name_count):
        raw = payload[p:p + 128]; p += 128
        names.append(raw.split(b"\0", 1)[0].decode("ascii", "replace"))
    leaf_count = struct.unpack_from("<i", payload, p)[0]; p += 4
    p += leaf_count * (4 if version >= 12 else 2)
    prop_count = struct.unpack_from("<i", payload, p)[0]; p += 4
    remaining = len(payload) - p
    size = remaining // prop_count if prop_count else 0
    return names, leaf_count, prop_count, size


def main(path):
    data = open(path, "rb").read()
    name = os.path.basename(path)
    ident, ver = struct.unpack_from("<4si", data, 0)
    print(f"\n=== {name}  ident={ident!r} version={ver} ===")

    gl = B.read_game_lump(data)
    print(f"GAME_LUMP: {len(gl)} entries -> {[k for k in gl]}")
    for fourcc, (v, payload) in gl.items():
        print(f"  '{fourcc}'  version={v}  payload={len(payload)}B")
        if fourcc == "sprp" and payload:
            names, nleaf, nprop, size = probe_sprp(payload, v)
            print(f"    sprp v{v}: {nprop} props, {len(names)} models, "
                  f"{nleaf} leaf refs, prop_struct={size}B "
                  f"({'V4' if size == 56 else 'V5' if size == 60 else 'V6' if size == 64 else '?'})")
            for m in names[:6]:
                print(f"      model: {m}")
            if len(names) > 6:
                print(f"      ... (+{len(names) - 6} more)")

    pak = B.read_pakfile(data)
    print(f"PAKFILE: {len(pak)} files")
    for k in list(pak)[:12]:
        print(f"    {k}  ({len(pak[k])}B)")
    if len(pak) > 12:
        print(f"    ... (+{len(pak) - 12} more)")


if __name__ == "__main__":
    from elysium_pipeline.formats import install
    args = sys.argv[1:] or [install.map_path("ch_hub_1")]
    for a in args:
        main(a)
