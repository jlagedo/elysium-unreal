from __future__ import annotations

import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from elysium_pipeline.config import UNREAL_BUILD_CONFIG, ConfigError
from elysium_pipeline.process import ProcessRunner
from elysium_pipeline import task_worktrees


def make_engine(root: Path, name: str = "ue") -> Path:
    """The two files `validate_engine_root` demands of an engine copy."""

    engine = root / name
    for relative in (
        Path("Engine/Build/BatchFiles/Build.bat"),
        Path("Engine/Binaries/Win64/UnrealEditor.exe"),
    ):
        path = engine / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("stub\n", encoding="utf-8")
    return engine


class TaskWorktreeLifecycleTests(unittest.TestCase):
    def git(self, repository: Path, *arguments: str) -> str:
        result = subprocess.run(
            ["git", "-C", repository, *arguments],
            check=True,
            capture_output=True,
            text=True,
            encoding="utf-8",
        )
        return result.stdout.strip()

    def make_repository(self, root: Path) -> Path:
        repository = root / "repo"
        repository.mkdir()
        self.git(repository, "init", "--quiet", "--initial-branch=main")
        self.git(repository, "config", "user.name", "Task Worktree Test")
        self.git(repository, "config", "user.email", "worktree@example.invalid")
        (repository / ".gitignore").write_text(
            ".elysium.local.env\n.elysium-task-worktree.json\nBinaries/\nSaved/\n",
            encoding="utf-8",
        )
        (repository / "ElysiumUE.uproject").write_text("{}\n", encoding="utf-8")
        (repository / "tracked.txt").write_text("one\n", encoding="utf-8")
        runtime = repository / "pipeline" / "src" / "elysium_pipeline"
        runtime.mkdir(parents=True)
        (runtime / "task_worktrees.py").write_text(
            "# task-worktree runtime\n", encoding="utf-8"
        )
        (runtime / "workspace_lock.py").write_text(
            "# generated-state lock\n", encoding="utf-8"
        )
        self.git(repository, "add", ".")
        self.git(repository, "commit", "--quiet", "-m", "initial")
        return repository

    def test_create_status_land_and_close(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = self.make_repository(root)
            game = root / "game"
            work = root / "work"
            for path in (game, work):
                path.mkdir()
            ue = make_engine(root)
            checkout = root / "task-checkout"
            runner = ProcessRunner(cwd=source, output_sink=lambda _line: None)

            creation = task_worktrees.create_task_worktree(
                source_repo=source,
                source_work_root=work,
                game_root=game,
                ue_root=ue,
                runner=runner,
                name="inventory-fix",
                worktree_path=checkout,
                max_parallel_actions=5,
            )
            record = creation.record

            self.assertFalse(creation.source_dirty)
            self.assertTrue((checkout / ".git").is_file())
            self.assertEqual(self.git(checkout, "rev-parse", "--abbrev-ref", "HEAD"), "HEAD")
            self.assertTrue((checkout / task_worktrees.TASK_MARKER_FILE).is_file())
            self.assertEqual(
                task_worktrees.current_task_worktree(checkout, record.work_root),
                record,
            )
            environment = (checkout / ".elysium.local.env").read_text(
                encoding="utf-8"
            )
            self.assertIn("ELYSIUM_TASK_WORKTREE=inventory-fix", environment)
            self.assertIn(str(record.work_root), environment)
            self.assertIn(f'ELYSIUM_UE_ROOT="{ue.resolve()}"', environment)
            self.assertEqual(record.ue_root, ue.resolve())
            self.assertEqual(
                task_worktrees.TaskWorktreeRecord.load(record.path).ue_root,
                ue.resolve(),
            )
            self.assertIn(
                "<MaxParallelActions>5</MaxParallelActions>",
                (checkout / UNREAL_BUILD_CONFIG).read_text(encoding="utf-8"),
            )

            initial = task_worktrees.task_worktree_status(record, runner)
            self.assertEqual(initial["ue_root"], str(ue.resolve()))
            self.assertEqual(initial["head"], record.base_commit)
            self.assertEqual(initial["unlanded_commits"], [])

            (checkout / "tracked.txt").write_text("two\n", encoding="utf-8")
            self.git(checkout, "add", "tracked.txt")
            self.git(checkout, "commit", "--quiet", "-m", "change task")
            candidate = self.git(checkout, "rev-parse", "HEAD")
            changed = task_worktrees.task_worktree_status(record, runner)
            self.assertEqual(changed["unlanded_commits"], [candidate])

            with self.assertRaisesRegex(
                task_worktrees.TaskWorktreeError, "not present on main"
            ):
                task_worktrees.close_task_worktree(record, runner)

            self.git(source, "cherry-pick", "--quiet", candidate)
            landed = task_worktrees.task_worktree_status(record, runner)
            self.assertEqual(landed["unlanded_commits"], [])

            task_worktrees.close_task_worktree(record, runner)
            self.assertFalse(checkout.exists())
            self.assertFalse(record.work_root.exists())

    def test_task_marker_enforces_its_roots_and_primary_only_commands(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = self.make_repository(root)
            game = root / "game"
            work = root / "work"
            for path in (game, work):
                path.mkdir()
            ue = make_engine(root)
            checkout = root / "task-checkout"
            runner = ProcessRunner(cwd=source, output_sink=lambda _line: None)
            record = task_worktrees.create_task_worktree(
                source_repo=source,
                source_work_root=work,
                game_root=game,
                ue_root=ue,
                runner=runner,
                name="camera-fix",
                worktree_path=checkout,
            ).record

            with self.assertRaisesRegex(
                task_worktrees.TaskWorktreeError, "owns work root"
            ):
                task_worktrees.current_task_worktree(checkout, root / "wrong-work")
            with self.assertRaisesRegex(
                task_worktrees.TaskWorktreeError, "primary-checkout-only"
            ):
                task_worktrees.assert_primary_operation(
                    checkout, record.work_root, "run play"
                )
            task_worktrees.assert_primary_operation(source, work, "run play")

    def test_two_tasks_hold_distinct_engines_and_legacy_records_still_load(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = self.make_repository(root)
            game = root / "game"
            work = root / "work"
            for path in (game, work):
                path.mkdir()
            runner = ProcessRunner(cwd=source, output_sink=lambda _line: None)

            engines = {}
            for index, name in enumerate(("first-task", "second-task"), start=1):
                engine = make_engine(root, f"UE_5.8_agent{index}")
                engines[name] = engine.resolve()
                task_worktrees.create_task_worktree(
                    source_repo=source,
                    source_work_root=work,
                    game_root=game,
                    ue_root=engine,
                    runner=runner,
                    name=name,
                    worktree_path=root / f"checkout-{name}",
                )

            records = task_worktrees.task_worktree_records(source, work)
            self.assertEqual(
                {record.name: record.ue_root for record in records}, engines
            )
            self.assertEqual(len({record.ue_root for record in records}), 2)

            # A record written before tasks carried their own engine keeps loading; its
            # checkout simply inherits whatever the primary configuration resolves.
            legacy = records[0].path
            value = json.loads(legacy.read_text(encoding="utf-8-sig"))
            del value["ue_root"]
            legacy.write_text(json.dumps(value), encoding="utf-8")
            self.assertIsNone(task_worktrees.TaskWorktreeRecord.load(legacy).ue_root)

    def test_an_engine_root_without_an_installation_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = self.make_repository(root)
            game = root / "game"
            work = root / "work"
            empty = root / "not-an-engine"
            for path in (game, work, empty):
                path.mkdir()
            runner = ProcessRunner(cwd=source, output_sink=lambda _line: None)
            with self.assertRaisesRegex(ConfigError, "not an Unreal installation"):
                task_worktrees.create_task_worktree(
                    source_repo=source,
                    source_work_root=work,
                    game_root=game,
                    ue_root=empty,
                    runner=runner,
                    name="no-engine",
                    worktree_path=root / "checkout",
                )
            self.assertFalse((root / "checkout").exists())
            self.assertFalse((work / "worktrees" / "no-engine").exists())

    def test_an_orphaned_checkout_reports_and_closes_instead_of_raising(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = self.make_repository(root)
            game = root / "game"
            work = root / "work"
            for path in (game, work):
                path.mkdir()
            ue = make_engine(root)
            runner = ProcessRunner(cwd=source, output_sink=lambda _line: None)

            healthy = task_worktrees.create_task_worktree(
                source_repo=source, source_work_root=work, game_root=game, ue_root=ue,
                runner=runner, name="healthy", worktree_path=root / "healthy",
            ).record
            orphan = task_worktrees.create_task_worktree(
                source_repo=source, source_work_root=work, game_root=game, ue_root=ue,
                runner=runner, name="orphan", worktree_path=root / "orphan",
            ).record

            # A checkout removed behind Git's back: the directory survives without the
            # .git file that made it a worktree.
            (orphan.worktree / ".git").unlink()

            broken = task_worktrees.task_worktree_status(orphan, runner)
            self.assertFalse(broken["missing"])
            self.assertIsNotNone(broken["error"])
            self.assertIsNone(broken["head"])

            # The healthy task beside it still reports in full.
            intact = task_worktrees.task_worktree_status(healthy, runner)
            self.assertIsNone(intact["error"])
            self.assertEqual(intact["head"], healthy.base_commit)

            # Listing every task must not fail because one of them is broken.
            names = {
                task_worktrees.task_worktree_status(record, runner)["name"]
                for record in task_worktrees.task_worktree_records(source, work)
            }
            self.assertEqual(names, {"healthy", "orphan"})

            task_worktrees.close_task_worktree(orphan, runner)
            self.assertFalse(orphan.work_root.exists())
            self.assertEqual(
                [r.name for r in task_worktrees.task_worktree_records(source, work)],
                ["healthy"],
            )

    def test_task_names_are_strict(self) -> None:
        for value in ("Task", "../task", "two tasks", "main", ""):
            with self.subTest(value=value), self.assertRaises(
                task_worktrees.TaskWorktreeError
            ):
                task_worktrees.validate_task_name(value)
        self.assertEqual(
            task_worktrees.validate_task_name("inventory-fix"), "inventory-fix"
        )


if __name__ == "__main__":
    unittest.main()
