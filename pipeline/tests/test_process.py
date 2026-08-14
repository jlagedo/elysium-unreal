from __future__ import annotations

import io
from pathlib import Path
import sys
import unittest

from elysium_pipeline.process import run_process


class ProcessRunnerTests(unittest.TestCase):
    def test_failed_output_sink_does_not_abandon_the_child(self) -> None:
        mirrored_lines: list[str] = []

        def rejected_sink(line: str) -> None:
            mirrored_lines.append(line)
            raise OSError(22, "invalid output handle")

        log = io.StringIO()
        result = run_process(
            [
                sys.executable,
                "-c",
                "print('first', flush=True); print('second', flush=True)",
            ],
            cwd=Path.cwd(),
            log=log,
            output_sink=rejected_sink,
        )

        self.assertEqual(result.returncode, 0)
        self.assertEqual(mirrored_lines, ["first"])
        self.assertIn("first\n", result.output)
        self.assertIn("second\n", result.output)
        self.assertIn("WARNING - child output mirror to output sink failed", result.output)
        self.assertIn("first\n", log.getvalue())
        self.assertIn("second\n", log.getvalue())
        self.assertIn("WARNING - child output mirror to output sink failed", log.getvalue())


if __name__ == "__main__":
    unittest.main()
