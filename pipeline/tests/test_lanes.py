from __future__ import annotations

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from elysium_pipeline import lanes
from elysium_pipeline.process import ProcessRunner
from elysium_pipeline.workspace_lock import (
    WorkspaceBusy,
    WorkspaceLease,
    active_lease,
)


class WorkspaceLeaseTests(unittest.TestCase):
    def test_lease_is_exclusive_and_process_release_clears_the_owner(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "exports"
            worktree = Path(temporary) / "repo"
            worktree.mkdir()

            with WorkspaceLease(root, "export map", worktree, metadata={"lane": "qa"}):
                owner = active_lease(root)
                self.assertIsNotNone(owner)
                self.assertEqual(owner["command"], "export map")
                self.assertEqual(owner["lane"], "qa")
                with self.assertRaises(WorkspaceBusy):
                    with WorkspaceLease(root, "run play", worktree):
                        self.fail("a second lease must not be acquired")

            self.assertIsNone(active_lease(root))

    def test_terminated_owner_releases_the_os_lock(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "exports"
            worktree = Path(temporary) / "repo"
            worktree.mkdir()
            code = (
                "from pathlib import Path; import time; "
                "from elysium_pipeline.workspace_lock import WorkspaceLease; "
                f"root=Path({str(root)!r}); repo=Path({str(worktree)!r}); "
                "lease=WorkspaceLease(root, 'child bake', repo); "
                "lease.__enter__(); print('ready', flush=True); time.sleep(30)"
            )
            process = subprocess.Popen(
                [sys.executable, "-c", code],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                encoding="utf-8",
            )
            try:
                assert process.stdout is not None
                self.assertEqual(process.stdout.readline().strip(), "ready")
                with self.assertRaises(WorkspaceBusy):
                    with WorkspaceLease(root, "parent play", worktree):
                        self.fail("the child owns the lane")
            finally:
                process.terminate()
                process.wait(timeout=10)
                if process.stdout is not None:
                    process.stdout.close()
                if process.stderr is not None:
                    process.stderr.close()
            self.assertIsNone(active_lease(root))


class LaneLifecycleTests(unittest.TestCase):
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
        self.git(repository, "init", "--quiet")
        self.git(repository, "config", "user.name", "Lane Test")
        self.git(repository, "config", "user.email", "lane@example.invalid")
        (repository / ".gitignore").write_text(
            ".elysium.local.env\nBinaries/\nPlugins/ElysiumBaked/Content/\n",
            encoding="utf-8",
        )
        (repository / "ElysiumUE.uproject").write_text("{}\n", encoding="utf-8")
        (repository / "tracked.txt").write_text("one\n", encoding="utf-8")
        runtime = repository / "pipeline" / "src" / "elysium_pipeline"
        runtime.mkdir(parents=True)
        (runtime / "lanes.py").write_text("# lane runtime\n", encoding="utf-8")
        (runtime / "workspace_lock.py").write_text("# lane lock\n", encoding="utf-8")
        self.git(repository, "add", ".")
        self.git(repository, "commit", "--quiet", "-m", "initial")
        return repository

    def test_create_dispatch_build_gate_mark_and_status(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = self.make_repository(root)
            game = root / "game"
            work = root / "work"
            ue = root / "ue"
            for path in (game, work, ue):
                path.mkdir()
            worktree = root / "qa-checkout"
            runner = ProcessRunner(cwd=source, output_sink=lambda _line: None)

            creation = lanes.create_lane(
                source_repo=source,
                source_work_root=work,
                game_root=game,
                ue_root=ue,
                runner=runner,
                name="qa",
                worktree_path=worktree,
            )
            record = creation.record

            self.assertFalse(creation.source_dirty)
            self.assertTrue((worktree / ".git").is_file())
            self.assertEqual(self.git(worktree, "rev-parse", "--abbrev-ref", "HEAD"), "HEAD")
            self.assertTrue((record.export_root / ".elysium-owned.json").is_file())
            self.assertIn("maps", lanes.incomplete_domains(record.export_root))
            environment = (worktree / ".elysium.local.env").read_text(encoding="utf-8")
            self.assertIn("ELYSIUM_LANE=qa", environment)
            self.assertIn(str(record.work_root), environment)

            (source / "tracked.txt").write_text("two\n", encoding="utf-8")
            self.git(source, "add", "tracked.txt")
            self.git(source, "commit", "--quiet", "-m", "candidate")
            candidate = self.git(source, "rev-parse", "HEAD")
            lanes.dispatch_lane(record, runner, candidate)
            self.assertEqual(self.git(worktree, "rev-parse", "HEAD"), candidate)
            self.assertEqual(record.candidate_commit, candidate)
            self.assertEqual(record.automated, "pending")
            self.assertIsNone(record.build_commit)

            (worktree / "untracked.txt").write_text("dirty\n", encoding="utf-8")
            with self.assertRaises(lanes.LaneError):
                lanes.dispatch_lane(record, runner, candidate)
            (worktree / "untracked.txt").unlink()

            with self.assertRaises(lanes.LaneError):
                lanes.mark_lane(record, runner, automated="passed")

            module = worktree / "Binaries" / "Win64" / "UnrealEditor-ElysiumUE.dll"
            module.parent.mkdir(parents=True)
            module.write_bytes(b"candidate module")
            lanes.record_run(
                record,
                command="build",
                status="succeeded",
                exit_code=0,
                started_at="2026-08-10T00:00:00+00:00",
                report_path=None,
            )
            record = lanes.LaneRecord.load(record.path)
            lanes.record_run(
                record,
                command="test",
                status="succeeded",
                exit_code=0,
                started_at="2026-08-10T00:01:00+00:00",
                report_path=record.work_root / "reports" / "tests" / "focused",
            )
            record = lanes.LaneRecord.load(record.path)
            lanes.mark_lane(
                record,
                runner,
                automated="passed",
                live="pending",
                note="focused automation green",
            )
            status = lanes.lane_status(record, runner)

            self.assertTrue(status["candidate_matches"])
            self.assertTrue(status["build_ready"])
            self.assertEqual(status["automated"], "passed")
            self.assertEqual(status["live"], "pending")
            self.assertEqual(status["snapshot"]["commit"], candidate)
            self.assertEqual(status["snapshot"]["editor_module_sha256"],
                             lanes._sha256_file(module))

            with self.assertRaises(lanes.LaneError):
                lanes.mark_lane(record, runner, live="passed")
            lanes.begin_activity(record, "run play")
            lanes.record_run(
                record,
                command="run play",
                status="succeeded",
                exit_code=0,
                started_at="2026-08-10T00:02:00+00:00",
                report_path=None,
            )
            record = lanes.LaneRecord.load(record.path)
            lanes.mark_lane(record, runner, live="passed")

            lanes.begin_activity(record, "export map")
            invalidated = lanes.LaneRecord.load(record.path)
            self.assertEqual(invalidated.automated, "pending")
            self.assertEqual(invalidated.live, "pending")
            self.assertEqual(invalidated.build_commit, candidate)
            self.assertIsNone(invalidated.snapshot)
            self.assertIsNone(invalidated.last_automation_run)

            self.git(
                source,
                "rm",
                "pipeline/src/elysium_pipeline/workspace_lock.py",
            )
            self.git(source, "commit", "--quiet", "-m", "remove lane runtime")
            with self.assertRaises(lanes.LaneError):
                lanes.dispatch_lane(invalidated, runner, "HEAD")
            self.assertEqual(self.git(worktree, "rev-parse", "HEAD"), candidate)

    def test_lane_names_and_registry_boundaries_are_strict(self) -> None:
        for value in ("QA", "../qa", "two lanes", "main", ""):
            with self.subTest(value=value), self.assertRaises(lanes.LaneError):
                lanes.validate_lane_name(value)
        self.assertEqual(lanes.validate_lane_name("qa-2"), "qa-2")


if __name__ == "__main__":
    unittest.main()
