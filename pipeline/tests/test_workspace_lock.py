from __future__ import annotations

from datetime import datetime, timezone
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
from unittest import mock

import psutil

import pytest

from elysium_pipeline import workspace_lock
from elysium_pipeline.workspace_lock import (
    WorkspaceBusy,
    WorkspaceLease,
    active_checkout_lease,
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
        # Windows unlocks a terminated process's byte-range lock asynchronously ("the time it takes
        # ... depends upon available system resources", `LockFileEx`); on a loaded `-n auto` run it
        # can trail the exit by a moment, so the release is given a bounded few seconds to land.
        deadline = time.monotonic() + 5.0
        while active_lease(root) is not None and time.monotonic() < deadline:
            time.sleep(0.05)
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


# ---------------------------------------------------------------- the checkout lease and the wait

def test_each_checkout_holds_its_own_lease() -> None:
    # One lease across five checkouts refused 1,659 builds in 12 days; the checkout's lease lives
    # in the checkout, so an idle sibling never blocks this one and the same checkout still does.
    with tempfile.TemporaryDirectory() as temporary:
        first, second = Path(temporary) / "first", Path(temporary) / "second"
        first.mkdir()
        second.mkdir()
        with WorkspaceLease.for_checkout(first, "build"):
            assert active_checkout_lease(first)["command"] == "build"
            assert active_checkout_lease(second) is None
            with WorkspaceLease.for_checkout(second, "test"):
                assert active_checkout_lease(second)["command"] == "test"
            with pytest.raises(WorkspaceBusy):
                with WorkspaceLease.for_checkout(first, "test"):
                    pytest.fail("a second command on one checkout must not be admitted")
        assert active_checkout_lease(first) is None


def test_the_checkout_lease_lives_inside_the_checkout_and_apart_from_the_export_root() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        checkout = Path(temporary) / "repo"
        exports = Path(temporary) / "exports"
        checkout.mkdir()
        with WorkspaceLease.for_checkout(checkout, "build") as lease:
            assert lease.lock_path.is_relative_to(checkout.resolve())
            assert lease.lock_path.parent == workspace_lock.checkout_lease_root(checkout)
            # The export root's lease is a different file: a build does not take it, and a bake
            # holding it does not stop a build on a checkout that is not baking.
            with WorkspaceLease(exports, "export map", checkout):
                assert active_lease(exports)["command"] == "export map"
                assert active_checkout_lease(checkout)["command"] == "build"
        assert active_lease(exports) is None


def _hold_then_release(lease: WorkspaceLease, seconds: float) -> threading.Thread:
    def work() -> None:
        time.sleep(seconds)
        lease.__exit__(None, None, None)

    thread = threading.Thread(target=work)
    thread.start()
    return thread


def test_a_held_lease_is_waited_for_and_the_holder_is_named_once() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        checkout = Path(temporary) / "repo"
        checkout.mkdir()
        holder = WorkspaceLease.for_checkout(checkout, "run play").__enter__()
        releaser = _hold_then_release(holder, 0.4)
        said: list[str] = []
        waiter = WorkspaceLease.for_checkout(
            checkout, "build", wait_seconds=10.0, poll_seconds=0.05, on_wait=said.append)
        with waiter:
            assert waiter.waited >= 0.3
            assert active_checkout_lease(checkout)["command"] == "build"
        releaser.join()
        # One line when the wait begins, naming who is waited for and how to refuse instead.
        assert len(said) == 1
        assert "run play" in said[0] and f"pid {os.getpid()}" in said[0]
        assert "--no-wait" in said[0]


def test_no_wait_refuses_at_once_and_exits_with_the_busy_code() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        checkout = Path(temporary) / "repo"
        checkout.mkdir()
        said: list[str] = []
        with WorkspaceLease.for_checkout(checkout, "run play"):
            began = time.monotonic()
            with pytest.raises(WorkspaceBusy) as refused:
                with WorkspaceLease.for_checkout(checkout, "build", on_wait=said.append):
                    pytest.fail("--no-wait must not be admitted")
            assert time.monotonic() - began < 2.0
        assert refused.value.exit_code == workspace_lock.EXIT_BUSY == 8
        assert "run play" in str(refused.value) and "--no-wait" in str(refused.value)
        assert said == [], "a refusal at once has nothing to announce"


def test_the_wait_is_bounded_and_the_timeout_names_the_holder() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        checkout = Path(temporary) / "repo"
        checkout.mkdir()
        said: list[str] = []
        with WorkspaceLease.for_checkout(checkout, "test"):
            began = time.monotonic()
            with pytest.raises(WorkspaceBusy, match="still busy after waiting") as gave_up:
                with WorkspaceLease.for_checkout(
                        checkout, "build", wait_seconds=0.3, poll_seconds=0.05,
                        on_wait=said.append):
                    pytest.fail("the holder never released")
            assert 0.25 <= time.monotonic() - began < 5.0
        assert "test" in str(gave_up.value)
        assert gave_up.value.exit_code == 8
        assert len(said) == 1


def test_a_stale_owner_file_with_no_lock_behind_it_is_not_a_holder() -> None:
    # The OS drops a dead process's lock, so what a killed command leaves behind is a name and no
    # claim: nothing waits on it and nothing has to clear it.
    with tempfile.TemporaryDirectory() as temporary:
        checkout = Path(temporary) / "repo"
        root = workspace_lock.checkout_lease_root(checkout)
        root.mkdir(parents=True)
        (root / workspace_lock.CHECKOUT_OWNER_FILE).write_text(
            '{"command": "build", "pid": 99999999}', encoding="utf-8")
        (root / workspace_lock.CHECKOUT_LOCK_FILE).write_bytes(b"\0")
        assert active_checkout_lease(checkout) is None
        with WorkspaceLease.for_checkout(checkout, "test") as lease:
            assert active_checkout_lease(checkout)["command"] == "test"
            assert lease.waited < 1.0


def test_an_owner_is_described_by_command_pid_checkout_and_age() -> None:
    now = datetime(2026, 10, 3, 12, 5, 30, tzinfo=timezone.utc)
    owner = {"command": "run play", "pid": 4242, "worktree": "E:/dev/elysium-unreal",
             "started_at": "2026-10-03T12:03:00+00:00"}
    assert workspace_lock.describe_owner(owner, now=now) == (
        "run play (pid 4242, E:/dev/elysium-unreal, 2m30s ago)")
    assert workspace_lock.describe_owner({}, now=now) == "another command"
