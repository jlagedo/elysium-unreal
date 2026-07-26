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
in that order with `up` on rt's top edge and `dn` on its bottom (`docs/sky-ambience.md`
-> "K1 ... (settled)").

Usage:
    python sky_upscale.py --map la_hub_1 --model models/RealESRGAN_x4plus.pth
    # writes out/<map>/tex_hi/sky_*.png and out/<map>_sky_compare.png
"""
import argparse
from pathlib import Path

import numpy as np
from PIL import Image

from upscale_bench import Model  # reuse the spandrel runner

RING = ["bk", "rt", "ft", "lf"]           # the canonical angular order (K1)
MARGIN = 32                               # bleed context in source px


def load_face(texdir: Path, name: str) -> np.ndarray:
    return np.asarray(Image.open(texdir / f"sky_{name}.png").convert("RGB"))


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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--map", required=True)
    ap.add_argument("--model", default="models/RealESRGAN_x4plus.pth")
    ap.add_argument("--out-root", default="out")
    ap.add_argument("--out-sub", default="tex_hi",
                    help="subdir under out/<map>/ for the upscaled faces")
    ap.add_argument("--cpu", action="store_true")
    args = ap.parse_args()

    root = Path(args.out_root) / args.map
    texdir = root / "tex"
    hidir = root / args.out_sub
    hidir.mkdir(exist_ok=True)

    device = "cpu" if args.cpu else _pick_device()
    print(f"device : {device}")
    model = Model(Path(args.model), device, fp16=True)
    print(f"model  : {model.name} (x{model.scale}, fp16={model.fp16})")

    faces = {n: load_face(texdir, n) for n in RING}
    print(f"ring   : {'-'.join(RING)} (canonical)  seam-err={ring_seam_err(faces):.2f}")

    # Build the continuous strip and record each face's slot for re-slicing.
    w = faces["bk"].shape[1]
    strip = np.concatenate([faces[n] for n in RING], axis=1)
    print(f"strip  : {strip.shape[1]}x{strip.shape[0]} -> upscaling")
    big = upscale_strip(model, strip)
    s = model.scale

    hi = {n: big[:, i * w * s:(i + 1) * w * s] for i, n in enumerate(RING)}

    for n in ("up", "dn"):
        hi[n] = upscale_face(model, load_face(texdir, n))

    for n, arr in hi.items():
        Image.fromarray(arr).save(hidir / f"sky_{n}.png")
    print(f"wrote  : {hidir}/sky_*.png  ({hi['bk'].shape[1]}x{hi['bk'].shape[0]})")

    _compare_sheet(faces, hi, root / f"{args.map}_sky_compare.png")


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
