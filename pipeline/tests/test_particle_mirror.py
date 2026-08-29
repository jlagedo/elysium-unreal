import tempfile
import unittest
from pathlib import Path
from unittest import mock

from PIL import Image

from elysium_pipeline.exporters import UE_extract_particles
from elysium_pipeline.exporters.UE_extract_particles import normalise_rain_sprite


REPO = Path(__file__).resolve().parents[2]


def test_rain_tga_normalization_preserves_rgba():
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        source = root / "source.tga"
        destination = root / "normalized.png"
        expected = Image.new("RGBA", (5, 3), (17, 34, 51, 68))
        expected.putpixel((2, 1), (200, 150, 100, 50))
        expected.save(source)

        normalise_rain_sprite(source.read_bytes(), destination)

        with Image.open(destination) as result:
            assert result.mode == "RGBA"
            assert result.size == (5, 3)
            assert result.getpixel((0, 0)) == (68, 68, 68, 255)
            assert result.getpixel((2, 1)) == (50, 50, 50, 255)


def _synthetic_mirror(root: Path):
    """A two-sprite loose index (one rain-closure slice, one ordinary sprite)."""
    source_dir = root / "install"
    source_dir.mkdir()
    rain = source_dir / "dropletfast.tga"
    blood = source_dir / "blood.tga"
    Image.new("RGBA", (2, 2), (10, 20, 30, 40)).save(rain)
    Image.new("RGBA", (2, 2), (200, 0, 0, 128)).save(blood)
    return {
        "particles/dropletfast.tga": ("loose", str(rain)),
        "particles/blood.tga": ("loose", str(blood)),
    }


def test_second_run_skips_sprite_encodes_and_manifest_rewrite():
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        idx = _synthetic_mirror(root)
        with mock.patch.object(UE_extract_particles, "export_root",
                               return_value=root / "export"):
            UE_extract_particles.main(index=idx)
            out = root / "export" / "particles"
            assert (out / "dropletfast.png").is_file()
            assert (out / "blood.png").is_file()
            manifest = out / "manifest.json"
            before_bytes = manifest.read_bytes()
            before_mtime = manifest.stat().st_mtime_ns

            # An unchanged mirror re-encodes nothing and leaves the manifest untouched.
            with (mock.patch.object(
                    UE_extract_particles, "normalise_rain_sprite",
                    side_effect=AssertionError("re-encoded a rain sprite")),
                  mock.patch.object(
                    UE_extract_particles, "normalise_sprite",
                    side_effect=AssertionError("re-encoded a sprite"))):
                UE_extract_particles.main(index=idx)
            assert manifest.read_bytes() == before_bytes
            assert manifest.stat().st_mtime_ns == before_mtime


def test_force_re_encodes_both_sprite_kinds():
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        idx = _synthetic_mirror(root)
        with mock.patch.object(UE_extract_particles, "export_root",
                               return_value=root / "export"):
            UE_extract_particles.main(index=idx)
            with (mock.patch.object(
                    UE_extract_particles, "normalise_rain_sprite",
                    side_effect=UE_extract_particles.normalise_rain_sprite) as rain,
                  mock.patch.object(
                    UE_extract_particles, "normalise_sprite",
                    side_effect=UE_extract_particles.normalise_sprite) as sprite):
                UE_extract_particles.main(index=idx, force=True)
            assert rain.call_count == 1
            assert sprite.call_count == 1
