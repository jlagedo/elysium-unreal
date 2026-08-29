from __future__ import annotations

import io
from pathlib import Path
import sys
import tempfile
import unittest

from elysium_pipeline.process import ProcessTimeout, run_process
import pytest


class _CountingLog(io.StringIO):
    """A log mirror that counts its flushes."""

    def __init__(self) -> None:
        super().__init__()
        self.flush_count = 0

    def flush(self) -> None:
        self.flush_count += 1
        super().flush()


def test_failed_output_sink_does_not_abandon_the_child() -> None:
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

    assert result.returncode == 0
    assert mirrored_lines == ["first"]
    assert "first\n" in result.output
    assert "second\n" in result.output
    assert "WARNING - child output mirror to output sink failed" in result.output
    assert "first\n" in log.getvalue()
    assert "second\n" in log.getvalue()
    assert "WARNING - child output mirror to output sink failed" in log.getvalue()


def test_tail_lines_bounds_retention_but_not_the_log() -> None:
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

    assert result.returncode == 0
    assert result.output.splitlines() == [f"line-{i:02d}" for i in range(40, 50)]
    assert log.getvalue().splitlines() == [f"line-{i:02d}" for i in range(50)]


def test_tail_lines_must_be_positive() -> None:
    with pytest.raises(ValueError):
        run_process(
            [sys.executable, "-c", "pass"],
            cwd=Path.cwd(),
            tail_lines=0,
        )


def test_log_flushes_on_an_interval_not_per_line() -> None:
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

    assert result.returncode == 0
    assert len(log.getvalue().splitlines()) == line_count
    assert log.flush_count >= 1
    assert log.flush_count < line_count // 4


def test_mirror_log_reaches_disk_before_run_process_returns() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        log_path = Path(temporary) / "run.log"
        with log_path.open("w", encoding="utf-8", newline="\n") as handle:
            result = run_process(
                [sys.executable, "-c", "print('tail-marker')"],
                cwd=Path.cwd(),
                log=handle,
            )
            assert result.returncode == 0
            # The exit flush lands before the caller closes the handle, so a
            # short final burst never sits in the file buffer.
            assert "tail-marker" in log_path.read_text(encoding="utf-8")


def test_a_child_that_outlives_its_deadline_is_killed() -> None:
    # The streaming read is what blocks when a child wedges, so the watchdog has to kill
    # the process rather than wait on it.
    with pytest.raises(ProcessTimeout) as caught:
        run_process(
            [sys.executable, "-c", "import time; time.sleep(30)"],
            cwd=Path.cwd(),
            timeout=0.5,
        )
    assert "deadline" in str(caught.value)
    assert caught.value.result.duration_seconds < 20


def test_a_child_that_finishes_in_time_is_unaffected() -> None:
    result = run_process(
        [sys.executable, "-c", "print('done')"],
        cwd=Path.cwd(),
        timeout=30,
    )
    assert result.returncode == 0
    assert "done" in result.output
