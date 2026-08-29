from __future__ import annotations

from pathlib import Path
import tempfile
import unittest

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
