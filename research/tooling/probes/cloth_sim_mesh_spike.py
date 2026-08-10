"""Can VtMB's cloth payload be rebuilt as a simulation mesh? (Jeanette spike.)

Usage:
    uv run elysium research cloth_sim_mesh_spike [MODEL] [--material NAME]

Route A of the Unreal port converts the authored payload into a Chaos cloth
asset rather than reimplementing the solver. That conversion needs three things
the payload does not state directly, and this decides whether all three are
recoverable before any Unreal code is written:

    rest positions  A cloth asset needs a position per simulation vertex. The
                    payload stores none. It stores *indices*: the anchored
                    prefix names model-global render vertices, and `StudioMesh`
                    +52 names, per render vertex, the particle that supplies its
                    position. Inverting the second and reading the first should
                    place every particle.

    topology        The collision-triangle array indexes particles, so it is the
                    candidate simulation topology. Chaos wants a surface, so the
                    question is whether those triangles form one -- every edge
                    shared by at most two faces, no degenerates, no orphaned
                    particles.

    binding         Anchored particles are skinned through the bone palette, so
                    their render vertices' own skin weights are the simulation
                    mesh's weights. Those are read from the same `read_skin` the
                    shipped exporter builds its glb joints from.

The check that decides it is **not** any of those three on its own. It is the
authored `rest_length_squared` on every constraint against the squared distance
between the two reconstructed positions. Rest lengths were authored from the
rest pose, so agreement confirms the positions, the particle indexing and the
constraint record layout simultaneously, and disagreement localises which of the
three is wrong. Nothing else in the payload is self-checking in that way.

This is a probe, not an exporter: every length and position stays in the model's
own Source frame. The `UE_`-prefixed sidecar exporter owns the conversion.
"""

from __future__ import annotations

import argparse
import json
import math
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any

from elysium_pipeline.formats import install, mdl_skel
from elysium_pipeline.paths import research_root

import cloth_payload_audit as payload

REPORT_NAME = "cloth-sim-mesh-spike.json"

DEFAULT_MODEL = "models/character/npc/unique/santa_monica/jeanette/jeanette.mdl"


def _dist2(a, b) -> float:
    return (a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2


def build(model_key: str, material: str | None) -> dict[str, Any]:
    idx = install.build_index()
    key = model_key.replace("\\", "/").lower()
    data = install.read(idx, key)
    if not data:
        raise SystemExit(f"not in the engine-resolved install: {key}")

    audit = payload.audit_one(idx, key)
    bone_names = mdl_skel.bone_names(data)

    mesh_map: list[dict[str, Any]] = []
    surfaces = mdl_skel.decode_skinned(data, install.read(idx, key[:-4] + ".dx80.vtx"), mesh_map)

    # One model-global vertex table, so an anchor index and a per-mesh map index
    # resolve through the same door.
    global_pos: dict[int, tuple] = {}
    global_skin: dict[int, list] = {}
    for rec in mesh_map:
        surf = surfaces[rec["material"]]
        for gvid, si in rec["remap"].items():
            if si < len(surf["pos"]):
                global_pos[gvid] = surf["pos"][si]
                global_skin[gvid] = [
                    (bone_names[j] if j < len(bone_names) else str(j), w)
                    for j, w in zip(surf["joints"][si], surf["weights"][si])
                    if w > 0
                ]

    # The two collision tables are counted and located but their record layouts
    # are undecoded, and the exporter cannot emit a collider without them. Both
    # strides factor cleanly against a bone index plus points and a radius, so
    # the bytes are shown under that reading and under a raw one rather than
    # asserted: 20 = int + float3 + float, 36 = 2 int + 2 float3 + float.
    primitives = []
    for sm in audit["models"]:
        base = None
        for bp in range(payload.i32(data, 320)):
            bpr = payload.i32(data, 324) + bp * 16
            for m in range(payload.i32(data, bpr + 4)):
                mb = bpr + payload.i32(data, bpr + 12) + m * payload.MODEL_STRIDE
                if payload.cstr(data, mb) == sm["name"]:
                    base = mb
        if base is None:
            continue
        for label, count_off, table_off, stride in (
            ("capsule", 208, 212, 36), ("sphere", 216, 220, 20)
        ):
            count = payload.i32(data, base + count_off)
            if not count:
                continue
            table = base + payload.i32(data, base + table_off)
            for k in range(count):
                rec = table + k * stride
                chunk = data[rec:rec + stride]
                primitives.append(
                    {
                        "kind": label,
                        "index": k,
                        "as_i32": [payload.i32(data, rec + o) for o in range(0, stride, 4)],
                        "as_f32": [round(payload.f32(data, rec + o), 5)
                                   for o in range(0, stride, 4)],
                        "bone_guess": (
                            bone_names[payload.i32(data, rec)]
                            if 0 <= payload.i32(data, rec) < len(bone_names) else None
                        ),
                        "hex": chunk.hex(),
                    }
                )

    results = []
    for sm in audit["models"]:
        for di, defn in enumerate(sm["definitions"]):
            n = defn["particles"]
            anchored = defn["anchored_particles"]

            # Every render vertex that names a particle, per mapped mesh.
            samples: dict[int, list[tuple]] = defaultdict(list)
            mapped_meshes = []
            for rec in mesh_map:
                mb = rec["model_base"] + payload.i32(data, rec["model_base"] + 140) \
                    + rec["mesh_index"] * payload.MESH_STRIDE
                sel_rel = payload.i32(data, mb + 48)
                pos_rel = payload.i32(data, mb + 52)
                if not (sel_rel and pos_rel):
                    continue
                if material and rec["material"].lower() != material.lower():
                    continue
                nv = payload.i32(data, mb + 8)
                sel_base = mb + sel_rel
                pos_base = mb + pos_rel
                hits = 0
                for local in range(nv):
                    selector = data[sel_base + local]
                    if selector == 0xFF or selector != di:
                        continue
                    particle = payload.u16(data, pos_base + local * 2) & 0x7FFF
                    gvid = rec["vertex_offset"] + local
                    p = global_pos.get(gvid)
                    if p is not None:
                        samples[particle].append(p)
                        hits += 1
                mapped_meshes.append({"material": rec["material"], "resolved_vertices": hits})

            # Anchors are model-global render vertices, in particle order.
            anchor_pos: dict[int, tuple] = {}
            for k, gvid in enumerate(defn["anchor_vertex_indices"]):
                p = global_pos.get(gvid)
                if p is not None:
                    anchor_pos[k] = p

            rest: dict[int, tuple] = {}
            spread = []
            for particle, pts in samples.items():
                # A particle named by several render vertices must be named at
                # one place; the spread is how far that fails.
                cx = sum(p[0] for p in pts) / len(pts)
                cy = sum(p[1] for p in pts) / len(pts)
                cz = sum(p[2] for p in pts) / len(pts)
                rest[particle] = (cx, cy, cz)
                if len(pts) > 1:
                    spread.append(max(math.sqrt(_dist2(p, (cx, cy, cz))) for p in pts))
            # The anchored prefix is authoritative where both name a particle.
            rest.update(anchor_pos)

            placed = sorted(rest)
            missing = [p for p in range(n) if p not in rest]

            # The decisive test, split by constraint kind: the two sets are
            # solved differently, so they are checked separately rather than
            # pooled into one statistic that hides a disagreement in either.
            by_kind: dict[str, list[float]] = {"distance": [], "compression_only": []}
            ratios: dict[str, list[float]] = {"distance": [], "compression_only": []}
            checks = 0
            for c in defn["constraints"]:
                a, b = c["a"], c["b"]
                if a not in rest or b not in rest:
                    continue
                checks += 1
                actual = _dist2(rest[a], rest[b])
                authored = c["rest_length_squared"]
                by_kind[c["kind"]].append(abs(actual - authored) / max(authored, 1e-9))
                if authored > 1e-9:
                    ratios[c["kind"]].append(actual / authored)
            for v in by_kind.values():
                v.sort()
            for v in ratios.values():
                v.sort()

            def pct_of(seq, q):
                return seq[min(len(seq) - 1, int(q * len(seq)))] if seq else None

            errors = sorted(by_kind["distance"] + by_kind["compression_only"])

            def pct(q):
                return pct_of(errors, q)

            # A second topology candidate. The payload's collision array may be a
            # collision proxy rather than a surface, but the render mesh IS a
            # surface and +52 maps each of its vertices to a particle -- so its
            # own triangulation, pushed through that map, induces a particle
            # surface. If the authored array is not manifold and this is, the
            # conversion has a topology source without inventing one.
            induced: set = set()
            induced_degenerate = 0
            for rec in mesh_map:
                if material and rec["material"].lower() != material.lower():
                    continue
                mb = rec["model_base"] + payload.i32(data, rec["model_base"] + 140) \
                    + rec["mesh_index"] * payload.MESH_STRIDE
                sel_rel = payload.i32(data, mb + 48)
                pos_rel = payload.i32(data, mb + 52)
                if not (sel_rel and pos_rel):
                    continue
                surf = surfaces[rec["material"]]
                si_to_particle: dict[int, int] = {}
                for local in range(payload.i32(data, mb + 8)):
                    if data[mb + sel_rel + local] != di:
                        continue
                    si = rec["remap"].get(rec["vertex_offset"] + local)
                    if si is not None:
                        si_to_particle[si] = payload.u16(data, mb + pos_rel + local * 2) & 0x7FFF
                for tri in surf["tris"]:
                    mapped = [si_to_particle.get(v) for v in tri]
                    if any(m is None for m in mapped):
                        continue
                    if len(set(mapped)) < 3:
                        induced_degenerate += 1
                        continue
                    induced.add(tuple(sorted(mapped)))

            induced_edges: Counter = Counter()
            for t in induced:
                for e in ((t[0], t[1]), (t[1], t[2]), (t[0], t[2])):
                    induced_edges[tuple(sorted(e))] += 1
            induced_used = {v for t in induced for v in t}

            # Topology: is the collision-triangle set a surface?
            tris = defn["collision_triangles"]
            edges: Counter = Counter()
            degenerate = 0
            for t in tris:
                if len({*t}) < 3:
                    degenerate += 1
                    continue
                for e in ((t[0], t[1]), (t[1], t[2]), (t[2], t[0])):
                    edges[tuple(sorted(e))] += 1
            used = {v for t in tris for v in t}

            results.append(
                {
                    "definition": di,
                    "particles": n,
                    "anchored": anchored,
                    "dynamic": defn["dynamic_particles"],
                    "gravity_scale": defn["gravity_scale"],
                    "mapped_meshes": mapped_meshes,
                    "rest_positions": {
                        "placed": len(placed),
                        "from_anchor_indices": len(anchor_pos),
                        "from_position_map": len(samples),
                        "missing_particles": missing[:24],
                        "missing_count": len(missing),
                        "max_multi_sample_spread": max(spread) if spread else 0.0,
                        "multi_sampled_particles": len(spread),
                    },
                    "rest_length_check": {
                        "constraints_checked": checks,
                        "of": defn["total_constraint_count"],
                        "relative_error_median": pct(0.5),
                        "relative_error_p95": pct(0.95),
                        "relative_error_max": errors[-1] if errors else None,
                        "within_1_percent": (
                            sum(1 for e in errors if e <= 0.01) / len(errors) if errors else None
                        ),
                        "by_kind": {
                            kind: {
                                "count": len(seq),
                                "relative_error_median": pct_of(seq, 0.5),
                                "relative_error_p95": pct_of(seq, 0.95),
                                "within_1_percent": (
                                    sum(1 for e in seq if e <= 0.01) / len(seq) if seq else None
                                ),
                                "actual_over_authored_median": pct_of(ratios[kind], 0.5),
                                "actual_over_authored_p05": pct_of(ratios[kind], 0.05),
                                "actual_over_authored_p95": pct_of(ratios[kind], 0.95),
                            }
                            for kind, seq in by_kind.items()
                        },
                    },
                    "induced_topology": {
                        "triangles": len(induced),
                        "degenerate_dropped": induced_degenerate,
                        "particles_referenced": len(induced_used),
                        "edges": len(induced_edges),
                        "boundary_edges_used_once": sum(
                            1 for v in induced_edges.values() if v == 1),
                        "manifold_edges_used_twice": sum(
                            1 for v in induced_edges.values() if v == 2),
                        "nonmanifold_edges_used_more": sum(
                            1 for v in induced_edges.values() if v > 2),
                    },
                    "topology": {
                        "triangles": len(tris),
                        "degenerate": degenerate,
                        "particles_referenced": len(used),
                        "particles_unreferenced": n - len(used),
                        "edges": len(edges),
                        "boundary_edges_used_once": sum(1 for v in edges.values() if v == 1),
                        "manifold_edges_used_twice": sum(1 for v in edges.values() if v == 2),
                        "nonmanifold_edges_used_more": sum(1 for v in edges.values() if v > 2),
                        "index_max": max(used) if used else None,
                    },
                    "anchor_binding": {
                        "bone_share": [
                            {"bone": b, "share": round(w / max(tot, 1e-9), 4)}
                            for b, w, tot in _anchor_bones(defn, global_skin)
                        ],
                    },
                }
            )
    return {"model": key, "header_flags": hex(audit["header_flags"]),
            "collision_primitives": primitives, "definitions": results}


def _anchor_bones(defn, global_skin):
    tally: Counter = Counter()
    for gvid in defn["anchor_vertex_indices"]:
        for name, w in global_skin.get(gvid, []):
            tally[name] += w
    total = sum(tally.values())
    return [(b, w, total) for b, w in tally.most_common(6)]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("model", nargs="?", default=DEFAULT_MODEL)
    parser.add_argument("--material")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()

    report = build(args.model, args.material)
    destination = args.report or (research_root() / REPORT_NAME)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    print(f"{report['model']}  flags {report['header_flags']}")
    if report["collision_primitives"]:
        print("\ncollision primitives (undecoded layout, shown both ways):")
        for p in report["collision_primitives"]:
            print(f"  {p['kind']}[{p['index']}] bone-guess {p['bone_guess']}")
            print(f"    i32 {p['as_i32']}")
            print(f"    f32 {p['as_f32']}")
    for d in report["definitions"]:
        print(f"\ndefinition {d['definition']}: {d['particles']} particles "
              f"({d['anchored']} anchored + {d['dynamic']} dynamic), "
              f"gravity scale {d['gravity_scale']:g}")
        for m in d["mapped_meshes"]:
            print(f"  mesh {m['material']}: {m['resolved_vertices']:,} resolved vertices")
        r = d["rest_positions"]
        print(f"  rest positions: {r['placed']}/{d['particles']} placed "
              f"({r['from_anchor_indices']} from anchors, "
              f"{r['from_position_map']} from the position map); "
              f"missing {r['missing_count']}")
        print(f"    multi-sampled {r['multi_sampled_particles']}, "
              f"max spread {r['max_multi_sample_spread']:.4g}")
        c = d["rest_length_check"]
        if c["constraints_checked"]:
            print(f"  rest-length check: {c['constraints_checked']:,}/{c['of']:,} constraints"
                  f"   median {c['relative_error_median']:.3g}"
                  f"   p95 {c['relative_error_p95']:.3g}"
                  f"   max {c['relative_error_max']:.3g}"
                  f"   within 1%: {c['within_1_percent']:.1%}")
        for kind, k in c["by_kind"].items():
            if not k["count"]:
                continue
            print(f"    {kind:<17s} {k['count']:5,}  median err {k['relative_error_median']:.3g}"
                  f"  within 1%: {k['within_1_percent']:.1%}"
                  f"  actual/authored p05..median..p95 "
                  f"{k['actual_over_authored_p05']:.4g} .. "
                  f"{k['actual_over_authored_median']:.4g} .. "
                  f"{k['actual_over_authored_p95']:.4g}")
        t = d["topology"]
        print(f"  authored collision triangles: {t['triangles']}, {t['degenerate']} degenerate, "
              f"{t['particles_referenced']}/{d['particles']} particles referenced "
              f"(max index {t['index_max']})")
        print(f"    edges {t['edges']}: {t['manifold_edges_used_twice']} interior, "
              f"{t['boundary_edges_used_once']} boundary, "
              f"{t['nonmanifold_edges_used_more']} non-manifold")
        it = d["induced_topology"]
        print(f"  induced from render triangles: {it['triangles']} triangles, "
              f"{it['particles_referenced']}/{d['particles']} particles referenced, "
              f"{it['degenerate_dropped']} degenerate dropped")
        print(f"    edges {it['edges']}: {it['manifold_edges_used_twice']} interior, "
              f"{it['boundary_edges_used_once']} boundary, "
              f"{it['nonmanifold_edges_used_more']} non-manifold")
        share = "  ".join(f"{r['bone']} {r['share']:.1%}" for r in d["anchor_binding"]["bone_share"])
        print(f"  anchor binding: {share}")
    print(f"\n  report: {destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
