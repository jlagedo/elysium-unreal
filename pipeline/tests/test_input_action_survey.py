import unittest

from research.tooling.probes.input_action_survey import count_pattern


class _TextImage:
    def __init__(self, data):
        self.data = data

    def section_bytes(self, name):
        if name != ".text":
            raise ValueError(name)
        return 0, self.data


class InputActionSurveyTests(unittest.TestCase):
    def test_count_pattern_counts_overlapping_surface(self):
        image = _TextImage(b"ABABA")
        self.assertEqual(count_pattern(image, b"ABA"), 2)
        self.assertEqual(count_pattern(image, b"XYZ"), 0)


if __name__ == "__main__":
    unittest.main()
