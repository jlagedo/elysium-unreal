import tempfile
import unittest
from pathlib import Path

from PIL import Image

from elysium_pipeline.exporters.UE_extract_particles import normalise_rain_sprite


class ParticleMirrorTests(unittest.TestCase):
    def test_rain_tga_normalization_preserves_rgba(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source.tga"
            destination = root / "normalized.png"
            expected = Image.new("RGBA", (5, 3), (17, 34, 51, 68))
            expected.putpixel((2, 1), (200, 150, 100, 50))
            expected.save(source)

            normalise_rain_sprite(source.read_bytes(), destination)

            with Image.open(destination) as result:
                self.assertEqual(result.mode, "RGBA")
                self.assertEqual(result.size, (5, 3))
                self.assertEqual(result.getpixel((0, 0)), (68, 68, 68, 255))
                self.assertEqual(result.getpixel((2, 1)), (50, 50, 50, 255))


if __name__ == "__main__":
    unittest.main()
