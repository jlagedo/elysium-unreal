"""Build the human-review sheet for a rendered skeletal green-room run."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


CELL_WIDTH = 480
IMAGE_HEIGHT = 270
LABEL_HEIGHT = 34
TITLE_HEIGHT = 58


def _font(size: int):
    for candidate in ("C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf"):
        if Path(candidate).is_file():
            return ImageFont.truetype(candidate, size)
    return ImageFont.load_default()


def build_sheet(run_dir: Path) -> Path:
    manifest_path = run_dir / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    shots = manifest.get("shots", [])
    if not shots:
        raise RuntimeError(f"{manifest_path} contains no shots")

    fractions = sorted({float(shot["fraction"]) for shot in shots})
    view_yaws = sorted({float(shot.get("view_yaw", 0.0)) for shot in shots})
    lookup = {
        (float(shot["fraction"]), float(shot.get("view_yaw", 0.0))): shot
        for shot in shots
    }
    rows = len(fractions)
    columns = len(view_yaws)
    canvas = Image.new(
        "RGB",
        (columns * CELL_WIDTH, TITLE_HEIGHT + rows * (IMAGE_HEIGHT + LABEL_HEIGHT)),
        (12, 14, 18),
    )
    draw = ImageDraw.Draw(canvas)
    title_font = _font(25)
    label_font = _font(18)
    small_font = _font(15)
    stem = manifest.get("review_stem", "<unknown model>")
    clip = manifest.get("review_clip", "<unknown clip>")
    anim_set = manifest.get("review_anim_set", "")
    subtitle = f"model: {stem}    clip: {clip}"
    if anim_set:
        subtitle += f"    cinematic: {anim_set} / {manifest.get('review_bone_root', '')}"
    draw.text((16, 10), subtitle, fill=(235, 238, 243), font=title_font)

    for row, fraction in enumerate(fractions):
        for column, view_yaw in enumerate(view_yaws):
            x = column * CELL_WIDTH
            y = TITLE_HEIGHT + row * (IMAGE_HEIGHT + LABEL_HEIGHT)
            shot = lookup.get((fraction, view_yaw))
            if shot is None:
                draw.rectangle(
                    (x + 1, y + 1, x + CELL_WIDTH - 2, y + IMAGE_HEIGHT - 2),
                    outline=(150, 50, 50),
                    width=3,
                )
                draw.text((x + 16, y + 16), "missing capture", fill=(255, 100, 100), font=label_font)
                continue
            image_path = run_dir / shot["file"]
            with Image.open(image_path) as source:
                image = source.convert("RGB")
                image.thumbnail((CELL_WIDTH, IMAGE_HEIGHT), Image.Resampling.LANCZOS)
                paste_x = x + (CELL_WIDTH - image.width) // 2
                paste_y = y + (IMAGE_HEIGHT - image.height) // 2
                canvas.paste(image, (paste_x, paste_y))

            bodies = shot.get("bodies", [])
            seconds = float(bodies[0].get("time", 0.0)) if bodies else 0.0
            draw.rectangle(
                (x, y + IMAGE_HEIGHT, x + CELL_WIDTH, y + IMAGE_HEIGHT + LABEL_HEIGHT),
                fill=(22, 25, 31),
            )
            draw.text(
                (x + 12, y + IMAGE_HEIGHT + 6),
                f"{fraction * 100:5.1f}%  {seconds:7.3f}s",
                fill=(230, 232, 237),
                font=label_font,
            )
            draw.text(
                (x + CELL_WIDTH - 128, y + IMAGE_HEIGHT + 8),
                f"view {view_yaw:3.0f}°",
                fill=(165, 193, 225),
                font=small_font,
            )

    output = run_dir / "review_sheet.png"
    canvas.save(output)
    return output


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("run_dir", type=Path)
    args = parser.parse_args()
    output = build_sheet(args.run_dir.resolve())
    print(f"[modelroom] review sheet: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
