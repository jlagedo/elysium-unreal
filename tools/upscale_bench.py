"""
Texture upscale bench — compare/tune super-resolution models on VtMB textures.

This is a *tuning* tool, separate from the game pipeline: point it at a folder of
extracted textures and one or more model files, and it emits per-model upscales
plus side-by-side contact sheets so you can eyeball which model/settings win
before committing to a bulk run.

Why a bespoke tool instead of just chaiNNer:
  * VtMB PNGs carry REAL rgb under transparent pixels + a smooth alpha mask
    (specular / self-illum / cutout). Compositing rgb*alpha throws data away, so
    we split, upscale each independently, and recombine.
  * Source textures were shipped DXT1/DXT5 (block) compressed, so a clean-source
    photo model rings on the block edges. Prefer a compression-aware model.
  * ~half the set tiles. Naive upscaling darkens/halos the seam. --seamless pads
    by wrapping the image, upscales, then crops, so tiles still butt cleanly.

Model loading uses spandrel (the same loader chaiNNer uses), so any architecture
-- ESRGAN / RealPLKSR / DAT / Compact / HAT / SPAN -- loads from one .pth or
.safetensors with no per-arch code here.

Usage:
    pip install spandrel torch --index-url https://download.pytorch.org/whl/cu128
    python upscale_bench.py --in out/ch_hub_1/tex --models models --out out/bench --sheet
    python upscale_bench.py --in out/ch_hub_1/tex/brick_chinawlla.png --models models --seamless

    # tune on a representative handful first:
    python upscale_bench.py --in out/ch_hub_1/tex --models models --sheet \
        --only brick_chinawlla,building_chinabldg08,signs_lotus,concrete_*,metal_*

Notes:
  * cu128 wheels are what an RTX 50-series (Blackwell / sm_120) needs. If torch
    reports "no kernel image for sm_120", your torch is too old for the card.
  * 16 GB handles 512->2048 without tiling; --tile is only insurance for big DAT
    models on 1024+ inputs.
"""
import argparse
import fnmatch
import sys
import time
from pathlib import Path

import numpy as np
from PIL import Image

# torch/spandrel are imported lazily inside load_model so the pure-image helpers
# (and --list) work in an environment without them.


# --------------------------------------------------------------------------- #
# alpha-aware texture processing
# --------------------------------------------------------------------------- #

def split_rgba(img: Image.Image):
    """Return (rgb uint8 HxWx3, alpha uint8 HxW or None)."""
    if img.mode == "RGBA":
        arr = np.asarray(img)
        a = arr[:, :, 3]
        # Treat a fully-opaque alpha as "no alpha" so we skip the extra pass.
        return arr[:, :, :3].copy(), (None if a.min() >= 255 else a.copy())
    return np.asarray(img.convert("RGB")).copy(), None


def merge_rgba(rgb: np.ndarray, alpha: np.ndarray | None) -> Image.Image:
    if alpha is None:
        return Image.fromarray(rgb, "RGB")
    return Image.fromarray(np.dstack([rgb, alpha]), "RGBA")


def wrap_pad(arr: np.ndarray, margin: int) -> np.ndarray:
    """Tile-aware pad: wrap edges so seam neighbourhoods are real, not mirrored."""
    if arr.ndim == 2:
        return np.pad(arr, ((margin, margin), (margin, margin)), mode="wrap")
    return np.pad(arr, ((margin, margin), (margin, margin), (0, 0)), mode="wrap")


# --------------------------------------------------------------------------- #
# model runner (spandrel)
# --------------------------------------------------------------------------- #

class Model:
    def __init__(self, path: Path, device: str, fp16: bool):
        import torch
        from spandrel import ImageModelDescriptor, ModelLoader

        self.path = path
        self.name = path.stem
        self._torch = torch
        desc = ModelLoader().load_from_file(str(path))
        if not isinstance(desc, ImageModelDescriptor):
            raise ValueError(f"{path.name}: not a single-image SR model")
        self.scale = desc.scale
        self.fp16 = fp16 and device.startswith("cuda") and desc.supports_half
        self.model = desc.model.eval().to(device)
        if self.fp16:
            self.model = self.model.half()
        self.device = device

    def _run(self, rgb: np.ndarray) -> np.ndarray:
        return self._run_batch([rgb])[0]

    def _run_batch(self, rgbs: list[np.ndarray]) -> list[np.ndarray]:
        """Run equal-sized tiles as one GPU batch to improve device occupancy."""
        torch = self._torch
        arr = np.stack(rgbs)
        t = torch.from_numpy(arr).permute(0, 3, 1, 2).float() / 255.0
        t = t.to(self.device)
        if self.fp16:
            t = t.half()
        with torch.inference_mode():
            out = self.model(t)
        out = out.clamp(0, 1).permute(0, 2, 3, 1).float().cpu().numpy()
        return list((out * 255.0 + 0.5).astype(np.uint8))

    def upscale_rgb(self, rgb: np.ndarray, tile: int, seamless: bool,
                    margin: int = 24, tile_batch: int = 1) -> np.ndarray:
        if seamless:
            padded = wrap_pad(rgb, margin)
            big = self._run_tiled(padded, tile, tile_batch)
            m = margin * self.scale
            return big[m:-m, m:-m]
        return self._run_tiled(rgb, tile, tile_batch)

    def upscale_alpha(self, alpha: np.ndarray, tile: int, seamless: bool,
                      method: str, margin: int = 24, tile_batch: int = 1) -> np.ndarray:
        if method == "lanczos":
            h, w = alpha.shape
            big = Image.fromarray(alpha, "L").resize(
                (w * self.scale, h * self.scale), Image.LANCZOS)
            return np.asarray(big)
        # 'model': replicate to 3 channels, run, take luma-ish mean.
        rep = np.dstack([alpha, alpha, alpha])
        out = self.upscale_rgb(rep, tile, seamless, margin, tile_batch)
        return out.mean(axis=2).round().astype(np.uint8)

    def _run_tiled(self, rgb: np.ndarray, tile: int, tile_batch: int = 1) -> np.ndarray:
        if tile <= 0 or (rgb.shape[0] <= tile and rgb.shape[1] <= tile):
            return self._run(rgb)
        # Overlap-blend tiles to hide seams from the tiling itself.
        s, ov = self.scale, 16
        h, w, _ = rgb.shape
        out = np.zeros((h * s, w * s, 3), np.float32)
        acc = np.zeros((h * s, w * s, 1), np.float32)
        pieces: dict[tuple[int, int], list[tuple[int, int, int, int, np.ndarray]]] = {}
        for y in range(0, h, tile):
            for x in range(0, w, tile):
                y0, x0 = max(0, y - ov), max(0, x - ov)
                y1, x1 = min(h, y + tile + ov), min(w, x + tile + ov)
                piece = rgb[y0:y1, x0:x1]
                pieces.setdefault(piece.shape[:2], []).append((y0, y1, x0, x1, piece))
        batch_size = max(1, tile_batch)
        for records in pieces.values():
            for start in range(0, len(records), batch_size):
                batch = records[start:start + batch_size]
                results = self._run_batch([record[4] for record in batch])
                for (y0, y1, x0, x1, _), piece in zip(batch, results):
                    out[y0 * s:y1 * s, x0 * s:x1 * s] += piece
                    acc[y0 * s:y1 * s, x0 * s:x1 * s] += 1
        return (out / np.maximum(acc, 1)).round().astype(np.uint8)


def process_texture(model: Model, img: Image.Image, args) -> Image.Image:
    rgb, alpha = split_rgba(img)
    big_rgb = model.upscale_rgb(
        rgb,
        args.tile,
        args.seamless,
        tile_batch=args.tile_batch,
    )
    big_a = None
    if alpha is not None:
        big_a = model.upscale_alpha(
            alpha,
            args.tile,
            args.seamless,
            args.alpha,
            tile_batch=args.tile_batch,
        )
    return merge_rgba(big_rgb, big_a)


# --------------------------------------------------------------------------- #
# contact sheet (original nearest-scaled vs each model)
# --------------------------------------------------------------------------- #

def contact_sheet(original: Image.Image, results: list[tuple[str, Image.Image]],
                  cell: int = 512) -> Image.Image:
    """One row: original (nearest-upscaled to match) then each model, on a checker
    background so alpha reads correctly."""
    def on_checker(im: Image.Image) -> Image.Image:
        im = im.convert("RGBA")
        c = Image.new("RGBA", im.size, (0, 0, 0, 0))
        block = 16
        px = c.load()
        for j in range(im.height):
            for i in range(im.width):
                px[i, j] = (150, 150, 150, 255) if ((i // block + j // block) & 1) \
                    else (90, 90, 90, 255)
        return Image.alpha_composite(c, im)

    def fit(im: Image.Image) -> Image.Image:
        im = on_checker(im)
        r = min(cell / im.width, cell / im.height)
        im = im.resize((max(1, int(im.width * r)), max(1, int(im.height * r))),
                       Image.NEAREST)
        canvas = Image.new("RGBA", (cell, cell + 22), (24, 24, 24, 255))
        canvas.paste(im, ((cell - im.width) // 2, (cell - im.height) // 2 + 22), im)
        return canvas

    tiles = [("original (NN)", fit(original))]
    tiles += [(name, fit(im)) for name, im in results]

    from PIL import ImageDraw
    W = cell * len(tiles)
    sheet = Image.new("RGBA", (W, cell + 22), (24, 24, 24, 255))
    draw = ImageDraw.Draw(sheet)
    for i, (label, t) in enumerate(tiles):
        sheet.paste(t, (i * cell, 0))
        draw.text((i * cell + 6, 4), label, fill=(230, 230, 120, 255))
    return sheet.convert("RGB")


# --------------------------------------------------------------------------- #
# driver
# --------------------------------------------------------------------------- #

def gather_inputs(in_path: Path, only: list[str]) -> list[Path]:
    files = [in_path] if in_path.is_file() else sorted(in_path.glob("*.png"))
    if only:
        files = [f for f in files if any(
            fnmatch.fnmatch(f.stem, pat) or f.stem == pat for pat in only)]
    return files


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--in", dest="inp", required=True,
                    help="texture file or folder of .png")
    ap.add_argument("--models", default="models",
                    help="folder of .pth/.safetensors models (default: models/)")
    ap.add_argument("--out", default="out/bench", help="output folder")
    ap.add_argument("--only", default="",
                    help="comma list of stem globs, e.g. brick_*,signs_lotus")
    ap.add_argument("--seamless", action="store_true",
                    help="wrap-pad for tiling textures (removes seam halos)")
    ap.add_argument("--alpha", choices=["model", "lanczos"], default="model",
                    help="how to upscale the alpha mask (default: model)")
    ap.add_argument("--tile", type=int, default=0,
                    help="tile size for low-VRAM inference (0 = whole image)")
    ap.add_argument("--tile-batch", type=int, default=1,
                    help="equal-sized tiles per GPU pass (default: 1)")
    ap.add_argument("--sheet", action="store_true",
                    help="also write a side-by-side comparison sheet per texture")
    ap.add_argument("--cpu", action="store_true", help="force CPU")
    ap.add_argument("--no-fp16", action="store_true", help="disable half precision")
    ap.add_argument("--list", action="store_true",
                    help="just list discovered models and inputs, then exit")
    args = ap.parse_args()

    only = [s.strip() for s in args.only.split(",") if s.strip()]
    in_path = Path(args.inp)
    model_dir = Path(args.models)
    out_dir = Path(args.out)

    inputs = gather_inputs(in_path, only)
    model_files = sorted(p for p in model_dir.glob("*")
                         if p.suffix.lower() in (".pth", ".safetensors")) \
        if model_dir.exists() else []

    print(f"inputs : {len(inputs)} texture(s)")
    print(f"models : {len(model_files)} -> {[p.stem for p in model_files]}")
    if args.list:
        for f in inputs:
            print("  in ", f.name)
        return
    if not inputs:
        sys.exit("no inputs matched")
    if not model_files:
        sys.exit(f"no models in {model_dir}/ (put .pth/.safetensors there)")

    device = "cpu" if args.cpu else _pick_device()
    print(f"device : {device}")
    models = []
    for mf in model_files:
        try:
            m = Model(mf, device, fp16=not args.no_fp16)
            models.append(m)
            print(f"  loaded {mf.stem}  (x{m.scale}, fp16={m.fp16})")
        except Exception as e:
            print(f"  SKIP {mf.stem}: {e}")
    if not models:
        sys.exit("no usable models")

    out_dir.mkdir(parents=True, exist_ok=True)
    for f in inputs:
        img = Image.open(f)
        results = []
        for m in models:
            t0 = time.perf_counter()
            big = process_texture(m, img, args)
            dt = time.perf_counter() - t0
            sub = out_dir / m.name
            sub.mkdir(exist_ok=True)
            big.save(sub / f.name)
            results.append((m.name, big))
            print(f"  {f.stem:34s} {m.name:22s} {img.size}->{big.size}  {dt:5.2f}s")
        if args.sheet and results:
            sheet = contact_sheet(img, results)
            (out_dir / "_sheets").mkdir(exist_ok=True)
            sheet.save(out_dir / "_sheets" / f"{f.stem}.png")


def _pick_device() -> str:
    try:
        import torch
        if torch.cuda.is_available():
            return "cuda"
    except Exception:
        pass
    return "cpu"


if __name__ == "__main__":
    main()
