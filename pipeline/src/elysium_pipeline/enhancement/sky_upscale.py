"""
Skybox-aware upscale (option 1: ring composite -> upscale -> re-slice).

Independently upscaling the six cube faces makes the model invent different detail
on each side of a shared cube edge -> visible seams on a continuous sky. Instead we
stitch the four horizon faces (ft/rt/bk/lf) into ONE 360-degree strip so every
face-to-face boundary is a real adjacency the model sees across, wrap-pad the strip
horizontally (closes the loop seam) and reflect-pad vertically, upscale the whole
strip once, then re-slice. up/dn are upscaled on their own (up = smooth sky,
dn = near-black ground -> low seam risk).

The ring order is the RE'd constant `bk, rt, ft, lf`, no face flipped: the decoded faces
are already canonically oriented, so the unfolded cross reads left-to-right and cyclically
in that order with `up` on rt's top edge and `dn` on its bottom (`docs/vtmb/sky-ambience.md`
-> "K1 ... (settled)").

Nothing is written until every face passes an **absolute-texel** check against its source.
VtMB's sky transfer is the identity -- a sky pixel is the decoded texel, unscaled and unfogged
(`docs/vtmb/sky-ambience.md` -> "K7 ... (settled)") -- so a model that shifts the mean shifts the
sky's brightness one-for-one, and one that reshapes the histogram changes its contrast. A
resolution change may not smuggle in a grade.

Internal enhancement experiment; not a public project-tooling entrypoint. It writes
`$ELYSIUM_EXPORT_ROOT/shared/tex_hi/<sky><face>.png` -- the set the runtime reads when
`elysium.EnhancedTextures` is on -- and `$ELYSIUM_EXPORT_ROOT/<sky>_sky_compare.png`.

A sky belongs to the corpus, not to a map: the same six faces serve every map whose `.sky`
names them, so they are upscaled once under their own name.
"""
import argparse
from pathlib import Path

import numpy as np
from PIL import Image

from elysium_pipeline import shared_corpus as SC
from elysium_pipeline.enhancement.upscale_bench import Model  # reuse the spandrel runner

RING = ["bk", "rt", "ft", "lf"]           # the canonical angular order (K1)
MARGIN = 32                               # bleed context in source px


def load_face(texdir: Path, sky: str, name: str) -> np.ndarray:
    return np.asarray(Image.open(texdir / SC.sky_face_file(sky, name)).convert("RGB"))


def seam_err(a_right: np.ndarray, b_left: np.ndarray) -> float:
    return float(np.abs(a_right.astype(int) - b_left.astype(int)).mean())


def ring_seam_err(faces: dict[str, np.ndarray]) -> float:
    """Mean pixel error across the four RING seams, as a sanity read on the input faces.

    Not a solver: the order is fixed. A large value means the decoded faces are not what
    the contract says they are, which is worth printing before an hour of upscaling.
    """
    return sum(seam_err(faces[RING[i]][:, -1], faces[RING[(i + 1) % len(RING)]][:, 0])
               for i in range(len(RING))) / len(RING)


def upscale_strip(model: Model, rgb: np.ndarray) -> np.ndarray:
    """Upscale with horizontal-wrap + vertical-reflect padding, then crop."""
    m = MARGIN
    p = np.pad(rgb, ((0, 0), (m, m), (0, 0)), mode="wrap")     # ring loop
    p = np.pad(p, ((m, m), (0, 0), (0, 0)), mode="reflect")    # zenith/nadir
    big = model._run_tiled(p, tile=512)
    s = model.scale
    return big[m * s:-m * s, m * s:-m * s]


def upscale_face(model: Model, rgb: np.ndarray) -> np.ndarray:
    m = MARGIN
    p = np.pad(rgb, ((m, m), (m, m), (0, 0)), mode="reflect")
    big = model._run_tiled(p, tile=512)
    s = model.scale
    return big[m * s:-m * s, m * s:-m * s]


def texel_fidelity(src: np.ndarray, hi: np.ndarray) -> dict:
    """How far an upscaled face drifts from the source in ABSOLUTE texel value.

    VtMB's sky transfer is the identity -- a sky pixel IS the decoded texel, unscaled and
    unfogged (`docs/vtmb/sky-ambience.md` -> "K7 ... (settled)") -- so a super-resolver that shifts
    the mean shifts the sky's brightness one-for-one, and one that reshapes the histogram
    changes the sky's contrast. Structural similarity is not the acceptance test here;
    preserving the numbers is.

    Returns per-channel mean drift, the worst decile drift, and the largest percentile gap
    (a cheap histogram distance) -- all in 0..255 units, so the numbers read as texel values.
    """
    a = src.astype(np.float64)
    b = hi.astype(np.float64)
    qs = [1, 5, 10, 25, 50, 75, 90, 95, 99]
    pa = np.percentile(a.reshape(-1, a.shape[2]), qs, axis=0)
    pb = np.percentile(b.reshape(-1, b.shape[2]), qs, axis=0)
    return {
        "mean": (b.mean((0, 1)) - a.mean((0, 1))),          # per-channel mean drift
        "mean_abs": float(np.abs(b.mean((0, 1)) - a.mean((0, 1))).max()),
        "hist_max": float(np.abs(pb - pa).max()),           # worst percentile gap
        "hist_at": qs[int(np.abs(pb - pa).max(axis=1).argmax())],
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sky", required=True,
                    help="sky name as the map's .sky sidecar states it, for example sky_day01")
    ap.add_argument("--model", default="models/RealESRGAN_x4plus.pth")
    ap.add_argument("--out-root", default="out")
    ap.add_argument("--out-sub", default=SC.TEX_HI,
                    help="subdir under $ELYSIUM_EXPORT_ROOT/shared/ for the upscaled faces")
    ap.add_argument("--cpu", action="store_true")
    ap.add_argument("--max-mean-shift", type=float, default=1.0,
                    help="reject a face whose per-channel mean drifts more than this many "
                         "0..255 units from the source (absolute-texel preservation, RE-A9)")
    ap.add_argument("--max-hist-shift", type=float, default=6.0,
                    help="reject a face whose worst percentile gap exceeds this, in 0..255 units")
    ap.add_argument("--allow-drift", action="store_true",
                    help="write the faces even when the fidelity check fails (still reported)")
    args = ap.parse_args()

    root = Path(args.out_root)
    texdir = SC.tex_dir(root)
    hidir = SC.corpus_dir(root) / args.out_sub
    hidir.mkdir(parents=True, exist_ok=True)

    device = "cpu" if args.cpu else _pick_device()
    print(f"device : {device}")
    model = Model(Path(args.model), device, fp16=True)
    print(f"model  : {model.name} (x{model.scale}, fp16={model.fp16})")

    faces = {n: load_face(texdir, args.sky, n) for n in RING}
    print(f"ring   : {'-'.join(RING)} (canonical)  seam-err={ring_seam_err(faces):.2f}")

    # Build the continuous strip and record each face's slot for re-slicing.
    w = faces["bk"].shape[1]
    strip = np.concatenate([faces[n] for n in RING], axis=1)
    print(f"strip  : {strip.shape[1]}x{strip.shape[0]} -> upscaling")
    big = upscale_strip(model, strip)
    s = model.scale

    hi = {n: big[:, i * w * s:(i + 1) * w * s] for i, n in enumerate(RING)}

    for n in ("up", "dn"):
        faces[n] = load_face(texdir, args.sky, n)
        hi[n] = upscale_face(model, faces[n])

    # Absolute-texel acceptance (RE-A9): the sky's displayed brightness IS its texel value, so a
    # face whose mean or histogram has moved is a brightness/contrast change wearing a
    # resolution change's clothes. Checked before anything is written.
    print("fidelity (0..255 units, vs the source face):")
    failed = []
    for n in ("bk", "rt", "ft", "lf", "up", "dn"):
        f = texel_fidelity(faces[n], hi[n])
        bad = f["mean_abs"] > args.max_mean_shift or f["hist_max"] > args.max_hist_shift
        print("  %-3s mean %+6.2f %+6.2f %+6.2f  worst-percentile %5.2f (p%d)  %s"
              % (n, *f["mean"], f["hist_max"], f["hist_at"], "FAIL" if bad else "ok"))
        if bad:
            failed.append(n)
    if failed:
        print("  ! %d face(s) drift beyond the accept thresholds: %s"
              % (len(failed), ", ".join(failed)))
        if not args.allow_drift:
            print("  ! nothing written. Re-run with --allow-drift to keep them anyway, or use a "
                  "model that preserves absolute values.")
            raise SystemExit(1)

    for n, arr in hi.items():
        Image.fromarray(arr).save(hidir / SC.sky_face_file(args.sky, n))
    print(f"wrote  : {hidir}/{args.sky}*.png  ({hi['bk'].shape[1]}x{hi['bk'].shape[0]})")

    _compare_sheet(faces, hi, root / f"{args.sky}_sky_compare.png")


def _compare_sheet(faces, hi, path):
    """Ring strip: original (nearest-upscaled) over upscaled, same pixel size."""
    orig = np.concatenate([faces[n] for n in RING], axis=1)
    up = np.concatenate([hi[n] for n in RING], axis=1)
    orig_big = np.asarray(Image.fromarray(orig).resize(
        (up.shape[1], up.shape[0]), Image.NEAREST))
    gap = np.full((16, up.shape[1], 3), 30, np.uint8)
    sheet = np.concatenate([orig_big, gap, up], axis=0)
    Image.fromarray(sheet).save(path)
    print(f"compare: {path}  (top=original NN, bottom=upscaled)")


def _pick_device():
    try:
        import torch
        if torch.cuda.is_available():
            return "cuda"
    except Exception:
        pass
    return "cpu"


if __name__ == "__main__":
    main()
