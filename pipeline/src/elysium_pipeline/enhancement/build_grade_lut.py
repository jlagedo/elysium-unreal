"""Derive a per-map color-grade 3D LUT that matches Elysium's real-time render
toward a Source-game reference screenshot.

The real-time LightRig produces its own palette (higher contrast, more saturated,
a harder warm/cool split) than VtMB's unified hazy look. This tool measures the
gap statistically and bakes a correction into a `.cube` 3D LUT the viewer loads
into `Environment.AdjustmentColorCorrection` (see GameScene.ApplyEnvironment).

Method: linear Monge-Kantorovich (MKL) color transfer (Pitie & Kokaram 2007).
Given the Elysium capture's pixel distribution X and the Source reference's Y,
it fits the affine map that matches BOTH the mean and the full 3x3 covariance
(so contrast, saturation, and channel cross-talk / split-tone are all corrected
at once), then bakes that map over a lattice into a `.cube`.

Usage:
  python pipeline/src/elysium_pipeline/enhancement/build_grade_lut.py <map> --shot <elysium.png> [--shot ...] \
      [--ref <source.png>] [--space srgb|linear] [--size 33] [--strength 1.0] \
      [--crop-ref L,T,R,B]

  <map>        exported map name (e.g. sp_tutorial_1)
  --ref        Source reference PNG; default $ELYSIUM_EXPORT_ROOT/<map>/grade_ref.png
  --shot       Elysium capture PNG (repeatable; pixels are pooled). A clean
               --capture frame with no HUD.
  --space      color space the transfer + LUT operate in; must match the space
               Godot samples the LUT in (settled empirically). Default srgb.
  --size       LUT lattice size per axis (default 33).
  --strength   blend the correction toward identity, 0..1 (default 1.0).
  --crop-ref   fractional margins cropped off the reference to drop the HUD /
               title bar, "left,top,right,bottom" (default 0.06,0.03,0.06,0.12).

  Writes $ELYSIUM_EXPORT_ROOT/<map>/<map>.cube.
"""
import argparse
import os
import sys

import numpy as np
from PIL import Image
from elysium_pipeline.paths import export_root

OUT = os.fspath(export_root())


def srgb_to_linear(c):
    c = np.asarray(c, np.float64)
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def linear_to_srgb(c):
    c = np.clip(np.asarray(c, np.float64), 0.0, 1.0)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055)


def load_pixels(path, crop):
    img = Image.open(path).convert("RGB")
    w, h = img.size
    l, t, r, b = crop
    box = (int(w * l), int(h * t), int(w * (1 - r)), int(h * (1 - b)))
    img = img.crop(box)
    return np.asarray(img, np.float64).reshape(-1, 3) / 255.0


def psd_sqrt(m, inv=False):
    """Matrix square root (or inverse sqrt) of a symmetric PSD matrix via eigh."""
    w, v = np.linalg.eigh(m)
    w = np.clip(w, 1e-12, None)
    d = 1.0 / np.sqrt(w) if inv else np.sqrt(w)
    return (v * d) @ v.T


def mkl_transform(X, Y):
    """Affine map f(x) = A(x - mean_x) + mean_y matching mean + covariance of X to Y."""
    mx, my = X.mean(0), Y.mean(0)
    cx = np.cov(X, rowvar=False) + np.eye(3) * 1e-6
    cy = np.cov(Y, rowvar=False) + np.eye(3) * 1e-6
    cx_half = psd_sqrt(cx)
    cx_ihalf = psd_sqrt(cx, inv=True)
    inner = psd_sqrt(cx_half @ cy @ cx_half)
    A = cx_ihalf @ inner @ cx_ihalf
    return A, mx, my


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("map")
    ap.add_argument("--ref")
    ap.add_argument("--shot", action="append", required=True)
    ap.add_argument("--space", choices=["srgb", "linear"], default="srgb")
    ap.add_argument("--size", type=int, default=33)
    ap.add_argument("--strength", type=float, default=1.0)
    ap.add_argument("--crop-ref", default="0.06,0.03,0.06,0.12")
    args = ap.parse_args()

    map_dir = os.path.join(OUT, args.map)
    ref = args.ref or os.path.join(map_dir, "grade_ref.png")
    crop = tuple(float(x) for x in args.crop_ref.split(","))

    if not os.path.exists(ref):
        sys.exit(f"reference not found: {ref}")

    Y = load_pixels(ref, crop)                      # Source (target)
    X = np.concatenate([load_pixels(s, (0, 0, 0, 0)) for s in args.shot])  # Elysium

    if args.space == "linear":
        X, Y = srgb_to_linear(X), srgb_to_linear(Y)

    A, mx, my = mkl_transform(X, Y)

    S = args.size
    axis = np.linspace(0.0, 1.0, S)
    # .cube ordering: red varies fastest. Build the lattice as (b,g,r).
    b, g, r = np.meshgrid(axis, axis, axis, indexing="ij")
    grid = np.stack([r.ravel(), g.ravel(), b.ravel()], axis=1)  # sample = input space

    src = srgb_to_linear(grid) if args.space == "linear" else grid.copy()
    out = (src - mx) @ A.T + my
    out = src + args.strength * (out - src)         # blend toward identity
    if args.space == "linear":
        out = linear_to_srgb(out)
    out = np.clip(out, 0.0, 1.0)

    cube = os.path.join(map_dir, f"{args.map}.cube")
    with open(cube, "w") as f:
        f.write(f'TITLE "elysium {args.map} grade"\n')
        f.write(f"LUT_3D_SIZE {S}\n")
        f.write("DOMAIN_MIN 0.0 0.0 0.0\n")
        f.write("DOMAIN_MAX 1.0 1.0 1.0\n")
        for px in out:
            f.write(f"{px[0]:.6f} {px[1]:.6f} {px[2]:.6f}\n")

    # Report the fit: mean shift and per-channel std ratio it is correcting.
    sx, sy = X.std(0), Y.std(0)
    print(f"wrote {cube}  (size {S}, space {args.space}, strength {args.strength})")
    print(f"  mean  elysium {np.round(mx,3)} -> source {np.round(my,3)}")
    print(f"  std   elysium {np.round(sx,3)} -> source {np.round(sy,3)}")


if __name__ == "__main__":
    main()
