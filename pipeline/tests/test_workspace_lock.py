from __future__ import annotations

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

import psutil

import pytest

from elysium_pipeline import workspace_lock
from elysium_pipeline.workspace_lock import (
    WorkspaceBusy,
    WorkspaceLease,
    active_lease,
    active_unreal_processes,
    assert_project_idle,
)


def test_lease_is_exclusive_and_process_release_clears_the_owner() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary) / "exports"
        worktree = Path(temporary) / "repo"
        worktree.mkdir()

        with WorkspaceLease(root, "export map", worktree, metadata={"lane": "qa"}):
            owner = active_lease(root)
            assert owner is not None
            assert owner["command"] == "export map"
            assert owner["lane"] == "qa"
            with pytest.raises(WorkspaceBusy):
                with WorkspaceLease(root, "run play", worktree):
                    pytest.fail("a second lease must not be acquired")

        assert active_lease(root) is None


def test_terminated_owner_releases_the_os_lock() -> None:
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
            assert process.stdout.readline().strip() == "ready"
            with pytest.raises(WorkspaceBusy):
                with WorkspaceLease(root, "parent play", worktree):
                    pytest.fail("the child owns the lane")
        finally:
            process.terminate()
            process.wait(timeout=10)
            if process.stdout is not None:
                process.stdout.close()
            if process.stderr is not None:
                process.stderr.close()
        assert active_lease(root) is None


class _FakeProcess:
    """A ``psutil.process_iter`` row whose command-line fetch is observable."""

    def __init__(
        self,
        pid: int,
        name: str,
        cmdline: tuple[str, ...] = (),
        error: Exception | None = None,
    ) -> None:
        self.pid = pid
        self.info = {"pid": pid, "name": name}
        self._cmdline = cmdline
        self._error = error
        self.cmdline_calls = 0

    def cmdline(self) -> tuple[str, ...]:
        self.cmdline_calls += 1
        if self._error is not None:
            raise self._error
        return self._cmdline


def test_a_project_no_editor_holds_is_idle() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        project = Path(temporary) / "ElysiumUE.uproject"
        project.write_text("{}\n", encoding="utf-8")
        # Matching is per project path, so one checkout never sees another's editors.
        assert active_unreal_processes(project) == ()
        assert_project_idle(project)


def test_cmdline_is_fetched_only_for_unreal_process_names() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        project = Path(temporary) / "ElysiumUE.uproject"
        project.write_text("{}\n", encoding="utf-8")
        bystander = _FakeProcess(101, "chrome.exe", cmdline=("chrome.exe",))
        editor = _FakeProcess(
            202,
            "UnrealEditor.exe",
            cmdline=("UnrealEditor.exe", str(project.resolve())),
        )
        other_project = _FakeProcess(
            303,
            "UnrealEditor-Cmd.exe",
            cmdline=("UnrealEditor-Cmd.exe", "C:/elsewhere/Other.uproject"),
        )
        vanished = _FakeProcess(
            404, "LiveCodingConsole.exe", error=psutil.NoSuchProcess(404)
        )
        with mock.patch.object(
            workspace_lock.psutil,
            "process_iter",
            return_value=iter((bystander, editor, other_project, vanished)),
        ):
            found = active_unreal_processes(project)

    assert found == ({"pid": 202, "name": "UnrealEditor.exe"},)
    # A name outside the Unreal set never pays for a command-line fetch.
    assert bystander.cmdline_calls == 0
    assert editor.cmdline_calls == 1
    assert other_project.cmdline_calls == 1
    # A process that vanishes mid-iteration is tolerated, not raised.
    assert vanished.cmdline_calls == 1
