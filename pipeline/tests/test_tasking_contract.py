from __future__ import annotations

from pathlib import Path
import sys
import tempfile

import pytest

from elysium_pipeline.tasking import Task, TaskFailure, TaskGraph


def test_dependency_order_and_deduplication_are_deterministic() -> None:
    calls: list[str] = []
    graph = TaskGraph(
        [
            Task("index", lambda: calls.append("index")),
            Task(
                "map:a",
                lambda: calls.append("map:a"),
                dependencies=("index",),
            ),
            Task(
                "map:b",
                lambda: calls.append("map:b"),
                dependencies=("index",),
            ),
            Task(
                "bundle",
                lambda: calls.append("bundle"),
                dependencies=("map:a", "map:b"),
            ),
        ]
    )

    results = graph.run(jobs=1)

    assert calls == ["index", "map:a", "map:b", "bundle"]
    assert set(results) == {"index", "map:a", "map:b", "bundle"}
    assert calls.count("index") == 1


def test_duplicate_task_names_are_rejected_instead_of_silently_overwritten() -> None:
    with pytest.raises(ValueError, match="unique"):
        TaskGraph([Task("same", lambda: None), Task("same", lambda: None)])


def test_failure_blocks_dependents_and_is_propagated() -> None:
    dependent_ran = False

    def fail() -> None:
        raise RuntimeError("synthetic failure")

    def dependent() -> None:
        nonlocal dependent_ran
        dependent_ran = True

    graph = TaskGraph(
        [
            Task("fail", fail),
            Task("dependent", dependent, dependencies=("fail",)),
        ]
    )

    with pytest.raises(TaskFailure) as caught:
        graph.run(fail_fast=False)

    assert not dependent_ran
    assert caught.value.results["fail"].status == "failed"
    assert caught.value.results["dependent"].status == "blocked"


def test_missing_outputs_turn_successful_action_into_failure() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        missing = Path(temporary) / "not-created"
        graph = TaskGraph([Task("producer", lambda: None, outputs=(missing,))])
        with pytest.raises(TaskFailure) as caught:
            graph.run()
        assert "expected outputs missing" in caught.value.results["producer"].error


class _PlainStdout:
    """A stdout that filters like the CLI's tee does: only `!` lines survive `write`.

    Everything written lands in `log` either way, which is what the run log does. This one
    has no curated door, standing in for a standalone run.
    """

    def __init__(self):
        self.console: list[str] = []
        self.log: list[str] = []

    def write(self, text: str) -> int:
        for line in text.splitlines():
            self.log.append(line)
            if line.lstrip().startswith("!"):
                self.console.append(line)
        return len(text)

    def flush(self) -> None:
        pass

    def isatty(self) -> bool:
        return False


class _CuratedStdout(_PlainStdout):
    """The same, plus the CLI's "already curated" door."""

    def write_signal(self, line: str) -> None:
        self.log.append(line)
        self.console.append(line)


def _run_one(stdout, tmp_path, status: str, *, per_task: bool = True) -> None:
    """Emit one task through a TaskProgress bound to `stdout`."""
    from elysium_pipeline.tasking import TaskProgress

    saved_out, saved_err = sys.stdout, sys.stderr
    sys.stdout = sys.stderr = stdout
    try:
        with TaskProgress(1, tmp_path / "tasks.log", per_task=per_task) as progress:
            progress.emit(
                "la_hub_1", status, 12.4,
                "\n".join(f"body line {index}" for index in range(40)),
                error="the decoder gave up" if status == "failed" else None,
            )
    finally:
        sys.stdout, sys.stderr = saved_out, saved_err


def test_a_failed_tasks_output_tail_survives_the_cli_filter(tmp_path) -> None:
    # TaskProgress has already filtered, capped and elided by the time it writes. Sending its
    # output through the CLI's filter as well drops all of it -- including the 30-line tail
    # that is the only thing a failed task says about why.
    tee = _CuratedStdout()
    _run_one(tee, tmp_path, "failed")
    console = "\n".join(tee.console)
    assert "la_hub_1" in console and "FAILED" in console
    assert "body line 39" in console, "the failure tail must reach the console"
    assert "the decoder gave up" in console
    assert "task log:" in console, "the closing tally names where the rest is"


def test_without_the_curated_door_the_reporter_still_works(tmp_path) -> None:
    # Standalone, or under any stdout that is not the CLI's tee: plain writes, no import of
    # cli, nothing lost.
    plain = _PlainStdout()
    assert not hasattr(plain, "write_signal")
    _run_one(plain, tmp_path, "failed")
    assert any("body line 39" in line for line in plain.log)
    assert any("FAILED" in line for line in plain.log)


def test_per_task_false_keeps_a_clean_task_off_the_console(tmp_path) -> None:
    # The lever that makes a lean `export all` print only what went wrong.
    quiet = _CuratedStdout()
    _run_one(quiet, tmp_path, "ok", per_task=False)
    assert not any("la_hub_1" in line for line in quiet.console)
    assert any("task log:" in line for line in quiet.console), "the tally still closes the run"

    loud = _CuratedStdout()
    _run_one(loud, tmp_path, "ok", per_task=True)
    assert any("la_hub_1" in line for line in loud.console)
