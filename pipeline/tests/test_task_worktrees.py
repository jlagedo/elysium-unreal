from __future__ import annotations

from pathlib import Path
import subprocess
import tempfile
import unittest

from elysium_pipeline.process import ProcessRunner
from elysium_pipeline import task_worktrees


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
            ".elysium.local.env\n.elysium-task-worktree.json\nBinaries/\n",
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
            ue = root / "ue"
            for path in (game, work, ue):
                path.mkdir()
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

            initial = task_worktrees.task_worktree_status(record, runner)
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
            ue = root / "ue"
            for path in (game, work, ue):
                path.mkdir()
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
