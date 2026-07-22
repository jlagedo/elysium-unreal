"""
Skybox-aware upscale (option 1: ring composite -> upscale -> re-slice).

Independently upscaling the six cube faces makes the model invent different detail
on each side of a shared cube edge -> visible seams on a continuous sky. Instead we
stitch the four horizon faces (ft/rt/bk/lf) into ONE 360-degree strip so every
face-to-face boundary is a real adjacency the model sees across, wrap-pad the strip
horizontally (closes the loop seam) and reflect-pad vertically, upscale the whole
strip once, then re-slice. up/dn are upscaled on their own (up = smooth sky,
dn = near-black ground -> low seam risk).

The cyclic order + per-face horizontal flip of the ring is auto-detected by
minimising the pixel difference across each seam, so we don't have to hard-code the
decode's face-orientation convention.

Usage:
    python sky_upscale.py --map la_hub_1 --model models/RealESRGAN_x4plus.pth
    # writes out/<map>/tex_hi/sky_*.png and out/<map>_sky_compare.png
"""
import argparse
from itertools import permutations
from pathlib import Path

import numpy as np
from PIL import Image

from upscale_bench import Model  # reuse the spandrel runner

RING = ["ft", "rt", "bk", "lf"]          # nominal angular order (auto-refined)
MARGIN = 32                               # bleed context in source px


def load_face(texdir: Path, name: str) -> np.ndarray:
    return np.asarray(Image.open(texdir / f"sky_{name}.png").convert("RGB"))


def seam_err(a_right: np.ndarray, b_left: np.ndarray) -> float:
    return float(np.abs(a_right.astype(int) - b_left.astype(int)).mean())


def best_ring(faces: dict[str, np.ndarray]):
    """Find (ordered names, flip flags) minimising total seam error around the loop.

    Faces are assumed upright (horizon level); only left-right order and optional
    horizontal flip per face vary. ft is pinned first, unflipped, to fix the frame.
    """
    names = RING
    others = [n for n in names if n != "ft"]
    best = None
    for perm in permutations(others):
        order = ["ft", *perm]
        for bits in range(1 << len(order)):
            flips = [(bits >> i) & 1 for i in range(len(order))]
            if flips[0]:                      # keep ft unflipped (canonical frame)
                continue
            arr = [faces[n][:, ::-1] if f else faces[n]
                   for n, f in zip(order, flips)]
            total = 0.0
            for i in range(len(arr)):
                a = arr[i][:, -1]
                b = arr[(i + 1) % len(arr)][:, 0]
                total += seam_err(a, b)
            if best is None or total < best[0]:
                best = (total, order, flips)
    return best


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
    err, order, flips = best_ring(faces)
    print(f"ring   : {list(zip(order, flips))}  seam-err={err:.2f}")

    # Build the continuous strip and record each face's slot for re-slicing.
    w = faces["ft"].shape[1]
    strip = np.concatenate(
        [faces[n][:, ::-1] if f else faces[n] for n, f in zip(order, flips)], axis=1)
    print(f"strip  : {strip.shape[1]}x{strip.shape[0]} -> upscaling")
    big = upscale_strip(model, strip)
    s = model.scale

    hi = {}
    for i, (n, f) in enumerate(zip(order, flips)):
        seg = big[:, i * w * s:(i + 1) * w * s]
        if f:
            seg = seg[:, ::-1]                 # undo the flip -> original orientation
        hi[n] = seg

    for n in ("up", "dn"):
        hi[n] = upscale_face(model, load_face(texdir, n))

    for n, arr in hi.items():
        Image.fromarray(arr).save(hidir / f"sky_{n}.png")
    print(f"wrote  : {hidir}/sky_*.png  ({hi['ft'].shape[1]}x{hi['ft'].shape[0]})")

    _compare_sheet(faces, hi, order, flips, s, root / f"{args.map}_sky_compare.png")


def _compare_sheet(faces, hi, order, flips, s, path):
    """Ring strip: original (nearest-upscaled) over upscaled, same pixel size."""
    w = faces["ft"].shape[1]
    orig = np.concatenate(
        [faces[n][:, ::-1] if f else faces[n] for n, f in zip(order, flips)], axis=1)
    up = np.concatenate(
        [(hi[n][:, ::-1] if f else hi[n]) for n, f in zip(order, flips)], axis=1)
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
