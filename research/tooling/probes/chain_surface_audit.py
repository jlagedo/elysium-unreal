"""Which surfaces ride the MDLHeader +396/+400 bone chains, and what parents them.

Usage:
    uv run elysium research chain_surface_audit [--limit N] [--report PATH]

This probe runs two complementary audits over the engine-resolved install so the
bone-chain population is classified without depending on how a material happens
to be named. Its negative garment result bounds only the bone-chain mechanism;
renderer cloth is the separate `StudioModel`/`StudioMesh` path decoded by
`cloth_payload_audit.py` and `docs/vtmb/secondary_motion.md`.

    audit A  Every record's first bone and its parent, all of them. The document
             reports `Bip01 Head` as the parent on 320 of 600 records and
             characterises the rest as "dominated by" hair, mane, ponytail and
             breast rigs. The other 280 parents are never enumerated, and two of
             its own capture rows are glossed rather than identified -- `VV`'s
             "two 25 degree body records" and `Damsel`'s "two 30 degree roots".
             This tallies every record's parent by name and leaves nothing in a
             residual bucket.

    audit B  The bone-chain/surface intersection, run from the 600 chain records
             rather than from material names. It closes each chain over its
             descendants and reports every surface carrying a vertex weighted to
             those bones. Naming is an annotation rather than the filter, so a
             generically named body material cannot vanish from the result.

Both audits read the installed bytes only. There is no capture, no game run, and
no dependency on an export having been produced.

A chain is closed over descendants rather than over retail's single child walk.
The constructor follows one ordered parent chain from the first moving bone, but
what audit B needs is which VERTICES a solved chain can move, and a bone hanging
off a solved bone is moved by it whether or not the record's own walk names it.
Both are reported per record -- `chain` is retail's walk, `closure` is what the
weight test uses -- so a reader can see where the two differ instead of taking
the wider one on trust.

Weights come from the same `read_skin` the shipped exporter builds its glb joints
from, so a vertex counted here is a vertex the bake skins.
"""

from __future__ import annotations

import argparse
import json
from collections import Counter
from pathlib import Path
from typing import Any

from elysium_pipeline.formats import install, mdl, mdl_skel
from elysium_pipeline.paths import research_root

REPORT_NAME = "chain-surface-audit.json"

#: `studiohdr.version`. Every claim in the owning document is scoped to this
#: revision, and a later-format decoder mislabels this table's slot.
MDL_VERSION = 2531

#: The count/index pair and the 28-byte record layout
#: (`docs/vtmb/secondary_motion.md`). `+8` is read and reported unnamed: the
#: located retail constructor never passes it, so this probe carries the value
#: and declines to call it anything.
H_NUM_CHAINS, H_CHAIN_INDEX = 396, 400
CHAIN_STRIDE = 28

#: The tokens the published audit searched material names for. Kept only to mark
#: which findings that method could already have reached; nothing filters on them.
GARMENT_TOKENS = ("skirt", "dress", "coat", "robe", "cloth", "cape", "cloak")

#: Families the document names for the chain corpus. A first-bone or parent name
#: matching none of these is what audit A exists to surface, so the residual is
#: reported per name rather than as a count.
KNOWN_FAMILY_TOKENS = ("hair", "pony", "tail", "mane", "breast", "tit", "boob")


def _f32(d: bytes, o: int) -> float:
    import struct

    return struct.unpack_from("<f", d, o)[0]


def _i32(d: bytes, o: int) -> int:
    import struct

    return struct.unpack_from("<i", d, o)[0]


def model_keys(idx: dict) -> list[str]:
    """Every `.mdl` the engine would resolve, install-relative and patch-first."""
    return sorted(
        key
        for key in idx
        if key.startswith("models/") and key.endswith(".mdl")
    )


def read_chain_records(d: bytes) -> list[dict[str, Any]] | None:
    """The +396/+400 array, or None where the model declares none.

    Returns None rather than an empty list for a model with no table, so "does
    not carry one" and "carries an empty one" stay distinguishable; no shipped
    model does the latter, and a probe that could not tell would never report it.
    """
    count = _i32(d, H_NUM_CHAINS)
    index = _i32(d, H_CHAIN_INDEX)
    if count <= 0:
        return None
    if index <= 0 or index + count * CHAIN_STRIDE > len(d):
        return []
    records = []
    for i in range(count):
        base = index + i * CHAIN_STRIDE
        records.append(
            {
                "first_bone": _i32(d, base + 0),
                "terminal_bone": _i32(d, base + 4),
                "unnamed_field_8": _f32(d, base + 8),
                "gravity": _f32(d, base + 12),
                "damping": _f32(d, base + 16),
                "spring_exponent": _f32(d, base + 20),
                "max_angle_degrees": _f32(d, base + 24),
            }
        )
    return records


def child_walk(bones: list, first: int) -> list[int]:
    """Retail's own chain: from the first moving bone, down one child each step.

    The constructor follows the ordered `StudioBone.parent` chain, so where a
    bone has several children the first in header order is the successor. A bone
    with none ends the chain.
    """
    children: dict[int, list[int]] = {}
    for bone in bones:
        children.setdefault(bone.parent, []).append(bone.index)
    walk = [first]
    while True:
        kids = children.get(walk[-1], [])
        if not kids:
            return walk
        walk.append(kids[0])


def descendant_closure(bones: list, first: int) -> set[int]:
    """Every bone at or under `first`, which is the set a solve can move."""
    children: dict[int, list[int]] = {}
    for bone in bones:
        children.setdefault(bone.parent, []).append(bone.index)
    out: set[int] = set()
    stack = [first]
    while stack:
        node = stack.pop()
        if node in out:
            continue
        out.add(node)
        stack.extend(children.get(node, ()))
    return out


def named_family(name: str) -> str:
    lowered = name.lower()
    for token in KNOWN_FAMILY_TOKENS:
        if token in lowered:
            return token
    return ""


def garment_named(name: str) -> str:
    lowered = name.lower()
    for token in GARMENT_TOKENS:
        if token in lowered:
            return token
    return ""


def audit(limit: int | None = None) -> dict[str, Any]:
    idx = install.build_index()
    keys = model_keys(idx)
    if limit is not None:
        keys = keys[:limit]

    counts = {
        "models_seen": 0,
        "models_unreadable": 0,
        "models_wrong_version": 0,
        "models_carrying_a_table": 0,
        "records": 0,
        "records_whose_first_bone_is_out_of_range": 0,
        "records_whose_table_does_not_fit_the_image": 0,
        "models_whose_geometry_could_not_be_read": 0,
    }
    records_out: list[dict[str, Any]] = []
    parent_tally: dict[str, int] = {}
    first_tally: dict[str, int] = {}
    surfaces_out: list[dict[str, Any]] = []
    carriers: list[str] = []

    for key in keys:
        data = install.read(idx, key)
        if not data or len(data) < 404:
            counts["models_unreadable"] += 1
            continue
        counts["models_seen"] += 1
        if _i32(data, 4) != MDL_VERSION:
            counts["models_wrong_version"] += 1
            continue
        records = read_chain_records(data)
        if records is None:
            continue
        if not records:
            counts["records_whose_table_does_not_fit_the_image"] += 1
            continue
        counts["models_carrying_a_table"] += 1
        carriers.append(key)

        bones = mdl_skel.read_bones(data)
        chain_bones: set[int] = set()
        for record in records:
            counts["records"] += 1
            first = record["first_bone"]
            if not 0 <= first < len(bones):
                counts["records_whose_first_bone_is_out_of_range"] += 1
                continue
            parent = bones[first].parent
            parent_name = bones[parent].name if 0 <= parent < len(bones) else ""
            walk = child_walk(bones, first)
            closure = descendant_closure(bones, first)
            chain_bones |= closure
            parent_tally[parent_name] = parent_tally.get(parent_name, 0) + 1
            first_tally[bones[first].name] = first_tally.get(bones[first].name, 0) + 1
            records_out.append(
                {
                    "model": key,
                    **record,
                    "first_bone_name": bones[first].name,
                    "parent_name": parent_name,
                    "first_bone_family": named_family(bones[first].name),
                    "parent_family": named_family(parent_name),
                    "chain": [bones[i].name for i in walk],
                    "closure_size": len(closure),
                }
            )

        # --- audit B: which surfaces carry a vertex on any of those bones ---
        pair = mdl.load(idx, key)
        if pair is None:
            counts["models_whose_geometry_could_not_be_read"] += 1
            continue
        try:
            surfaces = mdl_skel.decode_skinned(*pair)
        except Exception as error:  # a malformed .vtx is a corpus fact, not a stop
            counts["models_whose_geometry_could_not_be_read"] += 1
            records_out[-1].setdefault("geometry_error", str(error))
            continue
        for material, surface in surfaces.items():
            on_chain = 0
            dominant = 0
            for joints, weights in zip(surface["joints"], surface["weights"]):
                share = sum(
                    weight
                    for bone, weight in zip(joints, weights)
                    if weight > 0.0 and bone in chain_bones
                )
                if share > 0.0:
                    on_chain += 1
                if share > 0.5:
                    dominant += 1
            if on_chain == 0:
                continue
            surfaces_out.append(
                {
                    "model": key,
                    "material": material,
                    "vertices": len(surface["pos"]),
                    "vertices_on_a_chain": on_chain,
                    "vertices_mostly_on_a_chain": dominant,
                    "share_on_a_chain": round(on_chain / max(len(surface["pos"]), 1), 6),
                    "garment_token": garment_named(material),
                    "family_token": named_family(material),
                }
            )

    records_out.sort(key=lambda row: (row["model"], row["first_bone_name"]))
    surfaces_out.sort(key=lambda row: -row["vertices_on_a_chain"])

    # The whole point of audit A: what is left once the named families are taken
    # out. Reported by name and count, never as a bucket.
    unnamed_parents = {
        name: total
        for name, total in parent_tally.items()
        if not named_family(name)
    }
    unnamed_firsts = {
        name: total
        for name, total in first_tally.items()
        if not named_family(name)
    }
    # A surface on a chain whose material name the published search could not
    # have matched. This is the population that method was structurally unable
    # to find, so it is the finding audit B exists to produce.
    unnamed_surfaces = [
        row
        for row in surfaces_out
        if not row["garment_token"] and not row["family_token"]
    ]

    return {
        "counts": counts,
        "carriers": carriers,
        "records": records_out,
        "parents_by_name": dict(
            sorted(parent_tally.items(), key=lambda kv: -kv[1])
        ),
        "first_bones_by_name": dict(
            sorted(first_tally.items(), key=lambda kv: -kv[1])
        ),
        "parents_outside_the_named_families": dict(
            sorted(unnamed_parents.items(), key=lambda kv: -kv[1])
        ),
        "first_bones_outside_the_named_families": dict(
            sorted(unnamed_firsts.items(), key=lambda kv: -kv[1])
        ),
        "surfaces_on_a_chain": surfaces_out,
        "surfaces_a_name_search_could_not_reach": unnamed_surfaces,
    }


def summarize(report: dict[str, Any]) -> str:
    counts = report["counts"]
    lines = [
        f"models {counts['models_seen']:,} seen, "
        f"{counts['models_carrying_a_table']:,} carry a chain table, "
        f"{counts['records']:,} records",
    ]

    lines.append("")
    lines.append("audit A -- every record's parent, by name:")
    for name, total in list(report["parents_by_name"].items())[:25]:
        mark = "" if named_family(name) else "   <- outside the named families"
        lines.append(f"    {total:5,}  {name or '(rootless)'}{mark}")
    residual = report["parents_outside_the_named_families"]
    lines.append(
        f"  parents outside hair/pony/tail/mane/breast: "
        f"{sum(residual.values()):,} records over {len(residual)} names"
    )

    lines.append("")
    lines.append("audit B -- surfaces carrying a vertex on a chain bone:")
    for row in report["surfaces_on_a_chain"][:30]:
        lines.append(
            f"    {row['model'].split('/')[-1]:34s} {row['material']:26s} "
            f"{row['vertices_on_a_chain']:5,}/{row['vertices']:<5,} "
            f"({row['share_on_a_chain']:.0%}, {row['vertices_mostly_on_a_chain']:,} "
            f"dominant)"
            + (f"  [{row['garment_token']}]" if row["garment_token"] else "")
        )
    lines.append(
        f"  surfaces on a chain: {len(report['surfaces_on_a_chain']):,}; "
        f"of those, {len(report['surfaces_a_name_search_could_not_reach']):,} "
        f"carry a material name the published search could not have matched"
    )
    return "\n".join(lines)


def surface_weights(model_key: str) -> str:
    """What every surface of one model is actually skinned to, chain or not.

    Audit B answers which surfaces a chain moves. This answers the complement for
    a single model -- what the surfaces it does NOT move are bound to instead --
    which is the reading a rigid-looking garment needs: a skirt on the pelvis
    alone cannot deform at all, while one on both thighs deforms and may still
    look stiff. Same `read_skin` as everything above.
    """
    idx = install.build_index(verbose=False)
    pair = mdl.load(idx, model_key)
    if pair is None:
        return f"  {model_key}: no mdl/dx80.vtx pair in the install"
    d, v = pair
    bones = mdl_skel.read_bones(d)
    records = read_chain_records(d) or []
    chain_bones: set[int] = set()
    for record in records:
        first = record["first_bone"]
        if 0 <= first < len(bones):
            chain_bones |= descendant_closure(bones, first)

    lines = [f"  {model_key}"]
    for material, surface in sorted(mdl_skel.decode_skinned(d, v).items()):
        tally: dict[str, float] = {}
        single = 0
        for joints, weights in zip(surface["joints"], surface["weights"]):
            live = [(b, w) for b, w in zip(joints, weights) if w > 0.0]
            if not live:
                continue
            if len(live) == 1 or max(w for _, w in live) > 0.99:
                single += 1
            for bone, weight in live:
                name = bones[bone].name if 0 <= bone < len(bones) else f"#{bone}"
                tally[name] = tally.get(name, 0.0) + weight
        on_chain = any(
            0 <= bone < len(bones) and bone in chain_bones
            for joints, weights in zip(surface["joints"], surface["weights"])
            for bone, weight in zip(joints, weights)
            if weight > 0.0
        )
        total = len(surface["pos"])
        # The authored silhouette, in Source axes: X/Y are the horizontal plane
        # and Z is up. A garment that hangs reads as a tall narrow shell; one
        # authored flared reads as a wide flat one. Stated as spans rather than
        # a ratio so a reader can see which axis carries it.
        xs = [p[0] for p in surface["pos"]]
        ys = [p[1] for p in surface["pos"]]
        zs = [p[2] for p in surface["pos"]]
        span_x, span_y, span_z = (
            max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs)
        )
        lines.append(
            f"    {material:26s} {total:5,} verts, {single:5,} effectively "
            f"single-weight{'   [on a chain]' if on_chain else ''}"
        )
        lines.append(
            f"        bind span  X {span_x:7.2f}  Y {span_y:7.2f}  Z {span_z:7.2f}"
            f"   (widest horizontal {max(span_x, span_y):.2f} vs drop {span_z:.2f})"
        )
        for name, weight in sorted(tally.items(), key=lambda kv: -kv[1]):
            lines.append(f"        {weight:9.1f}  {name}")
    return "\n".join(lines)


def chain_vertices(model_key: str) -> str:
    """Per chain record, WHERE on the body the vertices it drives actually sit.

    Audit B answers which materials a chain touches. On a model whose garment is
    authored into a generically named body material that is not enough: `Bone01`
    off `Bip01 Spine1` is a breast rig or a skirt panel depending only on where
    its vertices are, and the record cannot say which. Height does say. A breast
    chain's vertices sit high on the torso in a tight cluster; a skirt's sit low
    and wide. Reported as the driven vertices' bounding box in Source axes, with
    the model's own vertical extent beside it so "low" is relative to this body
    rather than to an absolute number.
    """
    idx = install.build_index(verbose=False)
    pair = mdl.load(idx, model_key)
    if pair is None:
        return f"  {model_key}: no mdl/dx80.vtx pair in the install"
    d, v = pair
    bones = mdl_skel.read_bones(d)
    records = read_chain_records(d) or []
    surfaces = mdl_skel.decode_skinned(d, v)

    every_z = [p[2] for s in surfaces.values() for p in s["pos"]]
    lines = [
        f"  {model_key}",
        f"    model Z extent {min(every_z):.2f} .. {max(every_z):.2f}",
    ]
    for record in records:
        first = record["first_bone"]
        if not 0 <= first < len(bones):
            continue
        closure = descendant_closure(bones, first)
        driven: list[tuple[float, float, float]] = []
        materials: Counter = Counter()
        for material, surface in surfaces.items():
            for position, joints, weights in zip(
                surface["pos"], surface["joints"], surface["weights"]
            ):
                if any(
                    w > 0.0 and b in closure for b, w in zip(joints, weights)
                ):
                    driven.append(position)
                    materials[material] += 1
        parent = bones[first].parent
        parent_name = bones[parent].name if 0 <= parent < len(bones) else "(root)"
        head = (
            f"    {bones[first].name:16s} parent {parent_name:16s} "
            f"max {record['max_angle_degrees']:5.1f}"
        )
        if not driven:
            lines.append(f"{head}  -- drives NO vertex")
            continue
        zs = [p[2] for p in driven]
        xs = [p[0] for p in driven]
        lines.append(
            f"{head}  {len(driven):4,} verts  "
            f"Z {min(zs):7.2f}..{max(zs):7.2f}  X width {max(xs) - min(xs):6.2f}  "
            f"{dict(materials.most_common(2))}"
        )
    return "\n".join(lines)


def chain_heights(limit: int | None = None) -> str:
    """Every record in the corpus, ranked by how low on the body it acts.

    The complete form of the garment question, gated on nothing. A chain's
    clientele is decided by where its vertices are, so normalising each record's
    driven band by its own model's height makes 600 records on 107 differently
    scaled bodies directly comparable. Hair and hats land near 1.0, breasts
    around 0.7, and a skirt or coat hem cannot: it has to land low. Sorted
    ascending, so if a garment chain exists anywhere it is the first row.
    """
    idx = install.build_index(verbose=False)
    keys = sorted(
        key for key in idx if key.startswith("models/") and key.endswith(".mdl")
    )
    if limit is not None:
        keys = keys[:limit]

    rows = []
    skipped = 0
    for key in keys:
        data = install.read(idx, key)
        if not data or len(data) < 404 or _i32(data, 4) != MDL_VERSION:
            continue
        records = read_chain_records(data)
        if not records:
            continue
        pair = mdl.load(idx, key)
        if pair is None:
            skipped += 1
            continue
        try:
            surfaces = mdl_skel.decode_skinned(*pair)
        except Exception:
            skipped += 1
            continue
        bones = mdl_skel.read_bones(data)
        every_z = [p[2] for s in surfaces.values() for p in s["pos"]]
        if not every_z:
            continue
        floor, ceiling = min(every_z), max(every_z)
        height = max(ceiling - floor, 1e-6)
        for record in records:
            first = record["first_bone"]
            if not 0 <= first < len(bones):
                continue
            closure = descendant_closure(bones, first)
            zs = [
                position[2]
                for surface in surfaces.values()
                for position, joints, weights in zip(
                    surface["pos"], surface["joints"], surface["weights"]
                )
                if any(w > 0.0 and b in closure for b, w in zip(joints, weights))
            ]
            if not zs:
                continue
            rows.append(
                {
                    "model": key.split("/")[-1],
                    "bone": bones[first].name,
                    "verts": len(zs),
                    "low": (min(zs) - floor) / height,
                    "high": (max(zs) - floor) / height,
                }
            )
    rows.sort(key=lambda row: row["low"])
    lines = [
        f"  {len(rows):,} records located on {len({r['model'] for r in rows})} "
        f"models ({skipped} models' geometry unreadable)",
        "  lowest-acting records first; 0.0 = feet, 1.0 = top of head",
    ]
    for row in rows[:30]:
        lines.append(
            f"    {row['low']:.3f}..{row['high']:.3f}  {row['verts']:5,} verts  "
            f"{row['model']:34s} {row['bone']}"
        )
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--heights",
        action="store_true",
        help="Rank every chain record in the corpus by how low on the body it acts.",
    )
    parser.add_argument("--limit", type=int, help="Bound the model walk for a smoke run.")
    parser.add_argument(
        "--chains",
        help="Skip the audits; for one model, report where each chain record's "
             "driven vertices sit on the body.",
    )
    parser.add_argument("--report", type=Path)
    parser.add_argument(
        "--weights",
        help="Skip the audits; print every surface's bone weights for one "
             "install-relative model path.",
    )
    args = parser.parse_args()

    if args.weights:
        print(surface_weights(args.weights))
        return 0

    if args.chains:
        print(chain_vertices(args.chains))
        return 0

    if args.heights:
        print(chain_heights(limit=args.limit))
        return 0

    report = audit(limit=args.limit)
    destination = args.report or (research_root() / REPORT_NAME)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(summarize(report))
    print(f"\n  report: {destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
