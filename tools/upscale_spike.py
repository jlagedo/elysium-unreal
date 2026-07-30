"""
Small, repeatable GPU benchmark for the Elysium texture-upscaling spike.

The corpus deliberately stays small: six representative textures from
sp_tutorial_1 and six from sp_theatre.  It covers repeating world surfaces,
prop atlases, text/posters, and alpha cutouts.  Outputs are written under
tools/out/_upscale_spike/ and therefore remain game-derived and gitignored.

Usage:
    tools\\.venv\\Scripts\\python tools\\upscale_spike.py
    tools\\.venv\\Scripts\\python tools\\upscale_spike.py --limit 2
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import time
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from upscale_bench import Model, merge_rgba, split_rgba


ROOT = Path(__file__).resolve().parent
DEFAULT_MODEL = ROOT / "models" / "4x-PBRify_UpscalerV4.pth"
DEFAULT_OUT = ROOT / "out" / "_upscale_spike"


@dataclass(frozen=True)
class Sample:
    map_name: str
    relative_path: str
    family: str
    seamless: bool = False

    @property
    def source(self) -> Path:
        return ROOT / "out" / self.map_name / self.relative_path

    @property
    def key(self) -> str:
        return f"{self.map_name}__{self.source.stem}"


SAMPLES = (
    Sample("sp_tutorial_1", "tex/brick_theatwlla.png", "repeating brick", True),
    Sample("sp_tutorial_1", "tex/ground_streetb.png", "repeating ground", True),
    Sample("sp_tutorial_1", "tex/concrete_twrconflra.png", "repeating concrete", True),
    Sample("sp_tutorial_1", "tex/wood_fencea.png", "alpha cutout"),
    Sample("sp_tutorial_1", "tex/decals_pictures_aspostersa.png", "poster/text"),
    Sample(
        "sp_tutorial_1",
        "props/tex/models_scenery_vehicles_forklift_forklift.png",
        "prop atlas",
    ),
    Sample("sp_theatre", "tex/brick_chtrima.png", "repeating trim", True),
    Sample("sp_theatre", "tex/plaster_twrbllwlla.png", "repeating plaster", True),
    Sample("sp_theatre", "tex/drapery_ascurtainb_highrez.png", "hero alpha"),
    Sample("sp_theatre", "tex/carpet_ohruga.png", "decorative alpha"),
    Sample("sp_theatre", "tex/decals_signs_asylum.png", "poster/text"),
    Sample(
        "sp_theatre",
        "props/tex/models_scenery_structural_zhaos_boxes.png",
        "prop atlas",
    ),
)


def resize_channels(img: Image.Image, size: tuple[int, int]) -> Image.Image:
    """Resize RGB and alpha separately so transparent RGB is not premultiplied."""
    rgb, alpha = split_rgba(img)
    rgb_img = Image.fromarray(rgb, "RGB").resize(size, Image.Resampling.LANCZOS)
    out_rgb = np.asarray(rgb_img)
    if alpha is None:
        return Image.fromarray(out_rgb, "RGB")
    alpha_img = Image.fromarray(alpha, "L").resize(size, Image.Resampling.LANCZOS)
    return merge_rgba(out_rgb, np.asarray(alpha_img))


def target_scale(img: Image.Image) -> int:
    """Small sources get the native 4x result; 512+ sources stop at 2x."""
    return 4 if max(img.size) <= 256 else 2


def rgb_reconstruction_metrics(source: Image.Image, result: Image.Image) -> tuple[float, float]:
    src = np.asarray(source.convert("RGB"), dtype=np.float32)
    restored = np.asarray(
        result.convert("RGB").resize(source.size, Image.Resampling.LANCZOS),
        dtype=np.float32,
    )
    delta = src - restored
    mae = float(np.abs(delta).mean())
    mse = float(np.square(delta).mean())
    psnr = 99.0 if mse <= 1e-12 else 20.0 * math.log10(255.0 / math.sqrt(mse))
    return mae, psnr


def alpha_reconstruction_mae(source: Image.Image, result: Image.Image) -> float | None:
    _, src_alpha = split_rgba(source)
    _, result_alpha = split_rgba(result)
    if src_alpha is None or result_alpha is None:
        return None
    restored = np.asarray(
        Image.fromarray(result_alpha, "L").resize(source.size, Image.Resampling.LANCZOS),
        dtype=np.float32,
    )
    return float(np.abs(src_alpha.astype(np.float32) - restored).mean())


def edge_error(img: Image.Image) -> float:
    rgb = np.asarray(img.convert("RGB"), dtype=np.float32)
    horizontal = np.abs(rgb[:, 0, :] - rgb[:, -1, :]).mean()
    vertical = np.abs(rgb[0, :, :] - rgb[-1, :, :]).mean()
    return float((horizontal + vertical) * 0.5)


def checker_composite(img: Image.Image, size: tuple[int, int]) -> Image.Image:
    img = img.convert("RGBA")
    checker = Image.new("RGBA", img.size, (0, 0, 0, 255))
    px = checker.load()
    block = max(4, min(img.size) // 32)
    for y in range(img.height):
        for x in range(img.width):
            v = 72 if ((x // block + y // block) & 1) else 112
            px[x, y] = (v, v, v, 255)
    flat = Image.alpha_composite(checker, img).convert("RGB")
    flat.thumbnail(size, Image.Resampling.LANCZOS)
    canvas = Image.new("RGB", size, (24, 24, 24))
    canvas.paste(flat, ((size[0] - flat.width) // 2, (size[1] - flat.height) // 2))
    return canvas


def overview(rows: list[dict], out_path: Path) -> None:
    cell_w, cell_h, label_h = 480, 320, 48
    sheet = Image.new("RGB", (cell_w * 2, (cell_h + label_h) * len(rows)), (20, 20, 20))
    draw = ImageDraw.Draw(sheet)
    for index, row in enumerate(rows):
        y = index * (cell_h + label_h)
        source = Image.open(row["source_path"])
        target = Image.open(row["target_path"])
        sheet.paste(checker_composite(source, (cell_w, cell_h)), (0, y + label_h))
        sheet.paste(checker_composite(target, (cell_w, cell_h)), (cell_w, y + label_h))
        draw.text((8, y + 5), f'{row["sample"]} — original', fill=(235, 235, 160))
        draw.text(
            (cell_w + 8, y + 5),
            f'PBRify V4, {row["target_scale"]}x target — {row["seconds"]:.2f}s',
            fill=(235, 235, 160),
        )
        draw.text(
            (cell_w + 8, y + 24),
            f'RGB MAE {row["rgb_mae"]:.2f}; PSNR {row["rgb_psnr_db"]:.2f} dB',
            fill=(190, 210, 235),
        )
    sheet.save(out_path)


def detail_sheet(rows: list[dict], out_path: Path) -> None:
    """Four 1:1 crops: nearest-expanded source beside the enhanced target."""
    wanted = (
        "sp_tutorial_1__brick_theatwlla",
        "sp_tutorial_1__wood_fencea",
        "sp_theatre__decals_signs_asylum",
        "sp_tutorial_1__models_scenery_vehicles_forklift_forklift",
    )
    by_name = {row["sample"]: row for row in rows}
    if not all(key in by_name for key in wanted):
        return
    selected = [by_name[key] for key in wanted]
    cell, label_h = 512, 42
    sheet = Image.new("RGB", (cell * 2, (cell + label_h) * len(selected)), (20, 20, 20))
    draw = ImageDraw.Draw(sheet)
    for index, row in enumerate(selected):
        source = Image.open(row["source_path"])
        target = Image.open(row["target_path"])
        scale = int(row["target_scale"])
        crop_size = min(source.width, source.height, cell // scale)
        sx = (source.width - crop_size) // 2
        sy = (source.height - crop_size) // 2
        source_crop = source.crop((sx, sy, sx + crop_size, sy + crop_size))
        target_crop = target.crop(
            (sx * scale, sy * scale, (sx + crop_size) * scale, (sy + crop_size) * scale)
        )
        source_big = source_crop.resize((cell, cell), Image.Resampling.NEAREST)
        target_big = target_crop.resize((cell, cell), Image.Resampling.LANCZOS)
        y = index * (cell + label_h)
        sheet.paste(checker_composite(source_big, (cell, cell)), (0, y + label_h))
        sheet.paste(checker_composite(target_big, (cell, cell)), (cell, y + label_h))
        draw.text((8, y + 6), f'{row["sample"]} — original nearest', fill=(235, 235, 160))
        draw.text((cell + 8, y + 6), "PBRify V4 target", fill=(235, 235, 160))
    sheet.save(out_path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", type=Path, default=DEFAULT_MODEL)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--tile", type=int, default=256)
    parser.add_argument("--tile-batch", type=int, default=4)
    parser.add_argument("--limit", type=int, default=0)
    parser.add_argument("--only", default="", help="run samples whose key contains this text")
    parser.add_argument("--cpu", action="store_true")
    parser.add_argument("--no-fp16", action="store_true")
    parser.add_argument(
        "--tf32",
        action="store_true",
        help="allow TensorFloat-32 matmuls (slightly slower for this DAT2 model on RTX 5070 Ti)",
    )
    args = parser.parse_args()

    samples = list(SAMPLES)
    if args.only:
        samples = [sample for sample in samples if args.only.lower() in sample.key.lower()]
    samples = samples[: args.limit or None]
    if not samples:
        raise SystemExit("no spike samples matched")
    missing = [sample.source for sample in samples if not sample.source.is_file()]
    if missing:
        raise SystemExit("missing spike inputs:\n" + "\n".join(str(path) for path in missing))
    if not args.model.is_file():
        raise SystemExit(f"model not found: {args.model}")

    args.out.mkdir(parents=True, exist_ok=True)
    native_dir = args.out / "native_4x"
    target_dir = args.out / "target"
    native_dir.mkdir(exist_ok=True)
    target_dir.mkdir(exist_ok=True)

    import torch

    device = "cpu" if args.cpu else "cuda"
    if device == "cuda":
        torch.set_float32_matmul_precision("high" if args.tf32 else "highest")
    model = Model(args.model, device, fp16=not args.no_fp16)
    if model.scale != 4:
        raise SystemExit(f"expected a 4x model, got {model.scale}x")

    rows: list[dict] = []
    for sample in samples:
        source = Image.open(sample.source)
        rgb, alpha = split_rgba(source)

        if device == "cuda":
            torch.cuda.empty_cache()
            torch.cuda.reset_peak_memory_stats()
            torch.cuda.synchronize()
        started = time.perf_counter()
        big_rgb = model.upscale_rgb(
            rgb,
            args.tile,
            sample.seamless,
            tile_batch=args.tile_batch,
        )
        big_alpha = None
        if alpha is not None:
            # Alpha carries authored masks; keep it deterministic.
            big_alpha = model.upscale_alpha(
                alpha,
                args.tile,
                sample.seamless,
                method="lanczos",
                tile_batch=args.tile_batch,
            )
        if device == "cuda":
            torch.cuda.synchronize()
        elapsed = time.perf_counter() - started
        peak_mb = (
            torch.cuda.max_memory_allocated() / (1024.0 * 1024.0)
            if device == "cuda"
            else 0.0
        )

        native = merge_rgba(big_rgb, big_alpha)
        scale = target_scale(source)
        target = (
            native
            if scale == model.scale
            else resize_channels(native, (source.width * scale, source.height * scale))
        )
        native_path = native_dir / f"{sample.key}.png"
        target_path = target_dir / f"{sample.key}.png"
        native.save(native_path)
        target.save(target_path)

        rgb_mae, rgb_psnr = rgb_reconstruction_metrics(source, target)
        row = {
            "sample": sample.key,
            "map": sample.map_name,
            "family": sample.family,
            "source_path": str(sample.source),
            "native_path": str(native_path),
            "target_path": str(target_path),
            "source_width": source.width,
            "source_height": source.height,
            "target_scale": scale,
            "target_width": target.width,
            "target_height": target.height,
            "seamless": sample.seamless,
            "seconds": elapsed,
            "peak_vram_mb": peak_mb,
            "rgb_mae": rgb_mae,
            "rgb_psnr_db": rgb_psnr,
            "alpha_mae": alpha_reconstruction_mae(source, target),
            "source_edge_error": edge_error(source) if sample.seamless else None,
            "target_edge_error": edge_error(target) if sample.seamless else None,
        }
        rows.append(row)
        print(
            f'{sample.key:64s} {source.size}->{target.size} '
            f'{elapsed:7.2f}s  peak {peak_mb:7.0f} MiB'
        )

    summary = {
        "model": str(args.model),
        "model_name": model.name,
        "model_scale": model.scale,
        "device": device,
        "fp16": model.fp16,
        "tf32": device == "cuda" and args.tf32,
        "tile": args.tile,
        "tile_batch": args.tile_batch,
        "samples": len(rows),
        "total_seconds": sum(row["seconds"] for row in rows),
        "mean_seconds": float(np.mean([row["seconds"] for row in rows])),
        "max_peak_vram_mb": max(row["peak_vram_mb"] for row in rows),
        "mean_rgb_mae": float(np.mean([row["rgb_mae"] for row in rows])),
        "mean_rgb_psnr_db": float(np.mean([row["rgb_psnr_db"] for row in rows])),
        "rows": rows,
    }
    (args.out / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    with (args.out / "summary.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)
    overview(rows, args.out / "overview.png")
    detail_sheet(rows, args.out / "details.png")
    print(json.dumps({key: value for key, value in summary.items() if key != "rows"}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
