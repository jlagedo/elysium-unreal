"""
Simulated-garment spike: rebuild two VtMB character meshes with a synthesised cloth
lattice, beside the faithful export rather than over it.

VtMB's garment path is authored StudioRender particle cloth after ordinary skinning,
independent from its custom hair/body chain solver (`docs/vtmb/secondary_motion.md`). This
spike does not decode that payload. `enhancement/cloth.py` segments pelvis-dominant
free-hanging geometry on the two allowlisted models, hangs an approximation bone lattice on
that surface and re-weights it; this module emits the `.glb` and solver sidecar it needs.

Outputs go to `$ELYSIUM_EXPORT_ROOT/npc/cloth/` and **nothing else is written**. The
faithful `npc/<stem>.glb` is never touched, so the whole spike reverts by deleting one
directory, and `elysium.Cloth 0` bypasses it without deleting anything.

Internal enhancement experiment; not a public project-tooling entrypoint. Its environment
is managed by uv:

    uv run python -m elysium_pipeline.enhancement.cloth_spike
"""

from __future__ import annotations

import argparse
import json
import os
from dataclasses import dataclass
from pathlib import Path

from elysium_pipeline.enhancement import cloth
from elysium_pipeline.formats import install, mdl, mdl_gltf, mdl_skel
from elysium_pipeline.paths import export_root


@dataclass(frozen=True)
class Sample:
    stem: str
    model: str

    @property
    def key(self) -> str:
        return self.stem


#: The spike's corpus, frozen in the module the way `upscale_spike.SAMPLES` is. These are
#: the two garments the experiment is about: a floor-length robe on a player body and a
#: full-length coat on a unique NPC. `sheriff_battle` is deliberately absent -- its mesh
#: is the `Sheriff_nocloth_Body` variant and `cloth.plan` rejects it on its own.
SAMPLES = (
    Sample("tremere_female_armor_0",
           "models/character/pc/female/tremere/armor0/tremere_female_armor_0.mdl"),
    Sample("sheriff", "models/character/npc/unique/downtown/sheriff/sheriff.mdl"),
)


def out_dir() -> Path:
    return export_root() / "npc" / "cloth"


def build(sample: Sample, idx, destination: Path, *, rows: int, columns: int) -> dict:
    """Export one enhanced mesh + sidecar. Returns the summary row."""
    row: dict = {"stem": sample.stem, "model": sample.model}

    captured: dict = {}

    def planner(bones, surfaces):
        plan = cloth.plan(bones, surfaces, rows=rows, columns=columns)
        captured["plan"] = plan
        return plan

    info = mdl_gltf.export_npc(idx, sample.model, str(destination), stem=sample.stem,
                               cloth_planner=planner)
    plan = captured.get("plan")
    if plan is None:
        # Not a fault: `cloth.plan` refuses a model with no free-hanging shell, which is
        # the same guard that keeps `sheriff_battle` out of the corpus.
        row["skipped"] = "no garment shell below the hip joint"
        return row

    sidecar = {
        "stem": sample.stem,
        "model": sample.model,
        "note": ("Synthesised garment approximation; this does not consume VtMB's authored "
                 "renderer-cloth payload (docs/vtmb/secondary_motion.md). Lengths are metres "
                 "in this glb's own "
                 "space -- apply glTFRuntime's import scale, as the composition rig does."),
        "root": plan.root,
        "rows": plan.rows,
        "columns": plan.columns,
        **cloth.parameters(plan),
    }
    path = destination / f"{sample.stem}.json"
    with open(path, "w", encoding="utf-8") as f:
        json.dump(sidecar, f, separators=(",", ":"))

    row.update(
        glb=info["glb"],
        model_bones=info["bones"],
        cloth_bones=len(plan.bones),
        chains=len(sidecar["chains"]),
        shell_verts=plan.stats["shell_verts"],
        shell_materials=plan.stats["shell_materials"],
        empty_cells=plan.stats["empty_cells"],
        colliders=len(sidecar["colliders"]),
        drop=plan.stats["drop"],
        hip_z=round(plan.hip_z, 2),
        hem_z=round(plan.hem_z, 2),
        malformed_weights=_malformed(plan),
    )
    return row


def _malformed(plan) -> int:
    """Vertices whose replacement skin is not a convex combination of at most four bones.

    A non-zero count here means the mesh would skin wrong, so it is reported per model
    rather than asserted -- the summary is the artifact worth reading.
    """
    names = {b.name for b in plan.bones}
    bad = 0
    for per_vert in plan.weights.values():
        for terms in per_vert.values():
            total = sum(w for _, w in terms)
            if len(terms) > 4 or abs(total - 1.0) > 1e-5 \
                    or any(n not in names for n, _ in terms):
                bad += 1
    return bad


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    ap.add_argument("--only", help="substring narrowing SAMPLES by stem")
    ap.add_argument("--limit", type=int, help="stop after N models")
    ap.add_argument("--rows", type=int, default=5,
                    help="lattice rows including the rigid anchor row 0 (default 5)")
    ap.add_argument("--columns", type=int, default=10,
                    help="vertical panels around the garment (default 10)")
    ap.add_argument("--out", type=Path, default=None,
                    help=f"output directory (default {out_dir()})")
    args = ap.parse_args(argv)

    samples = [s for s in SAMPLES if not args.only or args.only in s.key]
    if args.limit:
        samples = samples[:args.limit]
    if not samples:
        print("no samples selected")
        return 1

    destination = args.out or out_dir()
    os.makedirs(destination, exist_ok=True)
    idx = install.build_index(verbose=False)

    rows = []
    for sample in samples:
        if mdl.load(idx, sample.model) is None:
            rows.append({"stem": sample.stem, "model": sample.model,
                         "skipped": "model not present in this install"})
            print(f"  ! {sample.stem}: not in this install")
            continue
        rows.append(build(sample, idx, destination, rows=args.rows, columns=args.columns))

    summary = {"rows": args.rows, "columns": args.columns,
               "out": str(destination), "models": rows}
    with open(destination / "summary.json", "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=1)

    print(f"\n[cloth-spike] {len(rows)} model(s) -> {destination}")
    for row in rows:
        if "skipped" in row:
            print(f"  {row['stem']}: skipped - {row['skipped']}")
            continue
        print(f"  {row['stem']}: {row['model_bones']}+{row['cloth_bones']} bones, "
              f"{row['chains']} chains, {row['shell_verts']} shell verts, "
              f"{row['colliders']} colliders, {row['empty_cells']} empty cells, "
              f"{row['malformed_weights']} malformed")
    return 1 if any(r.get("malformed_weights") for r in rows) else 0


if __name__ == "__main__":
    raise SystemExit(main())
