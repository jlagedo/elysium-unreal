from __future__ import annotations

from pathlib import Path
import tempfile
import unittest

from elysium_pipeline.tasking import Task, TaskFailure, TaskGraph


class TaskGraphContractTests(unittest.TestCase):
    def test_dependency_order_and_deduplication_are_deterministic(self) -> None:
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

        self.assertEqual(calls, ["index", "map:a", "map:b", "bundle"])
        self.assertEqual(set(results), {"index", "map:a", "map:b", "bundle"})
        self.assertEqual(calls.count("index"), 1)

    def test_duplicate_task_names_are_rejected_instead_of_silently_overwritten(self) -> None:
        with self.assertRaisesRegex(ValueError, "unique"):
            TaskGraph([Task("same", lambda: None), Task("same", lambda: None)])

    def test_failure_blocks_dependents_and_is_propagated(self) -> None:
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

        with self.assertRaises(TaskFailure) as caught:
            graph.run(fail_fast=False)

        self.assertFalse(dependent_ran)
        self.assertEqual(caught.exception.results["fail"].status, "failed")
        self.assertEqual(caught.exception.results["dependent"].status, "blocked")

    def test_missing_outputs_turn_successful_action_into_failure(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            missing = Path(temporary) / "not-created"
            graph = TaskGraph([Task("producer", lambda: None, outputs=(missing,))])
            with self.assertRaises(TaskFailure) as caught:
                graph.run()
            self.assertIn("expected outputs missing", caught.exception.results["producer"].error)


if __name__ == "__main__":
    unittest.main()
