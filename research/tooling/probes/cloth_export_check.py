"""Acceptance check for `formats.mdl_cloth.build`, against the payload's own numbers.

Usage:
    uv run elysium research cloth_export_check [MODEL ...]

The parser reconstructs quantities the file does not state -- rest positions, a
simulation topology, collider frames -- so it is only trustworthy if what it
produces still agrees with what the file *does* state. This re-derives the one
self-checking invariant against the parser's own output rather than against a
second decode: every distance constraint's authored rest length against the
squared distance between the two rest positions the parser placed.

It also reports what the writer beside it has to carry, so a sidecar that grows
a field silently is visible here first.
"""

from __future__ import annotations

import argparse

from elysium_pipeline.formats import install, mdl_cloth

DEFAULT = ("models/character/npc/unique/santa_monica/jeanette/jeanette.mdl",)


def check(idx, key):
    d = install.read(idx, key)
    if not d:
        raise SystemExit(f"not in the engine-resolved install: {key}")
    v = install.read(idx, key[:-4] + ".dx80.vtx")
    garments = mdl_cloth.build(d, v)
    print(f"\n{key}: {len(garments)} garment(s)")
    for g in garments:
        pos = g["rest_positions"]
        # Each set is scored against ITS OWN measured ratio, so the check asks
        # "is this scale consistent across the set" rather than assuming one
        # scale the corpus does not actually share.
        errs_d, errs_c = [], []
        for c in g["constraints"]:
            a, b = pos[c["a"]], pos[c["b"]]
            actual = sum((a[i] - b[i]) ** 2 for i in range(3))
            ratio = (g["compression_rest_ratio"] if c["compression_only"]
                     else g["distance_rest_ratio"]) or 1.0
            expected = c["rest_length_squared"] * ratio
            e = abs(actual - expected) / max(expected, 1e-9)
            (errs_c if c["compression_only"] else errs_d).append(e)
        edges = {}
        for t in g["triangles"]:
            for e in ((t[0], t[1]), (t[1], t[2]), (t[0], t[2])):
                key_e = tuple(sorted(e))
                edges[key_e] = edges.get(key_e, 0) + 1
        mapped = sum(sum(1 for r in m["vertices"] if r) for m in g["render_maps"])
        flips = sum(sum(1 for r in m["vertices"] if r and r["flip_normal"])
                    for m in g["render_maps"])

        print(f"  {g['model_name']} def{g['definition']}: "
              f"{g['particle_count']} particles ({g['anchored_count']} anchored), "
              f"gravity scale {g['gravity_scale']:g}")
        print(f"    triangles {len(g['triangles'])}, edges {len(edges)}, "
              f"non-manifold {sum(1 for c in edges.values() if c > 2)}")
        print(f"    constraints {g['distance_constraints']} distance + "
              f"{g['compression_constraints']} compression   "
              f"measured rest ratio: distance {g['distance_rest_ratio']:.4g}, "
              f"compression {g['compression_rest_ratio']:.4g}")
        for label, seq in (("distance", errs_d), ("compression", errs_c)):
            if seq:
                seq.sort()
                print(f"      {label:<12s} median rel err {seq[len(seq)//2]:.3g}  "
                      f"max {seq[-1]:.3g}  within 1%: "
                      f"{sum(1 for e in seq if e <= 0.01)/len(seq):.1%}")
        print(f"    colliders: {len(g['capsules'])} capsules, {len(g['spheres'])} spheres")
        for c in g["capsules"]:
            print(f"      capsule {c['bone_a_name']} r={c['radius']:.3f} "
                  f"local a={[round(x, 2) for x in c['a_local']]} "
                  f"b={[round(x, 2) for x in c['b_local']]}")
        for s in g["spheres"]:
            print(f"      sphere  {s['bone_name']} r={s['radius']:.3f} "
                  f"local c={[round(x, 2) for x in s['centre_local']]}")
        print(f"    render map: {mapped} substituted vertices ({flips} normal-flipped) "
              f"over {len(g['render_maps'])} surface(s)")
        print(f"    anchor skin entries: {len(g['anchor_skin'])}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("models", nargs="*", default=list(DEFAULT))
    parser.add_argument("--write", metavar="STEM",
                        help="also run the exporter's own sidecar writer under this stem, "
                             "which is what proves the basis conversion and the JSON shape "
                             "rather than only the decode")
    args = parser.parse_args()
    idx = install.build_index()
    for key in args.models:
        key = key.replace("\\", "/").lower()
        check(idx, key)
        if args.write:
            from elysium_pipeline.exporters import npc_export

            fields = npc_export.write_garment(args.write, key, idx, key)
            print(f"    sidecar: {fields or 'not written (model carries no cloth)'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
