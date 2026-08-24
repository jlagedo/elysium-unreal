from __future__ import annotations

import io
from pathlib import Path
import sys
import tempfile
import unittest

from elysium_pipeline.process import run_process


class _CountingLog(io.StringIO):
    """A log mirror that counts its flushes."""

    def __init__(self) -> None:
        super().__init__()
        self.flush_count = 0

    def flush(self) -> None:
        self.flush_count += 1
        super().flush()


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

    def test_tail_lines_bounds_retention_but_not_the_log(self) -> None:
        log = io.StringIO()
        result = run_process(
            [
                sys.executable,
                "-c",
                "for i in range(50): print(f'line-{i:02d}', flush=True)",
            ],
            cwd=Path.cwd(),
            log=log,
            tail_lines=10,
        )

        self.assertEqual(result.returncode, 0)
        self.assertEqual(
            result.output.splitlines(), [f"line-{i:02d}" for i in range(40, 50)]
        )
        self.assertEqual(
            log.getvalue().splitlines(), [f"line-{i:02d}" for i in range(50)]
        )

    def test_tail_lines_must_be_positive(self) -> None:
        with self.assertRaises(ValueError):
            run_process(
                [sys.executable, "-c", "pass"],
                cwd=Path.cwd(),
                tail_lines=0,
            )

    def test_log_flushes_on_an_interval_not_per_line(self) -> None:
        log = _CountingLog()
        line_count = 400
        result = run_process(
            [
                sys.executable,
                "-c",
                f"for i in range({line_count}): print(f'line-{{i}}', flush=True)",
            ],
            cwd=Path.cwd(),
            log=log,
        )

        self.assertEqual(result.returncode, 0)
        self.assertEqual(len(log.getvalue().splitlines()), line_count)
        self.assertGreaterEqual(log.flush_count, 1)
        self.assertLess(log.flush_count, line_count // 4)

    def test_mirror_log_reaches_disk_before_run_process_returns(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            log_path = Path(temporary) / "run.log"
            with log_path.open("w", encoding="utf-8", newline="\n") as handle:
                result = run_process(
                    [sys.executable, "-c", "print('tail-marker')"],
                    cwd=Path.cwd(),
                    log=handle,
                )
                self.assertEqual(result.returncode, 0)
                # The exit flush lands before the caller closes the handle, so a
                # short final burst never sits in the file buffer.
                self.assertIn("tail-marker", log_path.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
