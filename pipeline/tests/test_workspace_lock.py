from __future__ import annotations

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from elysium_pipeline.workspace_lock import (
    WorkspaceBusy,
    WorkspaceLease,
    active_lease,
    active_unreal_processes,
    assert_project_idle,
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

class ProjectIdleTests(unittest.TestCase):
    def test_a_project_no_editor_holds_is_idle(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            project = Path(temporary) / "ElysiumUE.uproject"
            project.write_text("{}\n", encoding="utf-8")
            # Matching is per project path, so one checkout never sees another's editors.
            self.assertEqual(active_unreal_processes(project), ())
            assert_project_idle(project)


if __name__ == "__main__":
    unittest.main()
