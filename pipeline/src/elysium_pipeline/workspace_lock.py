"""Cross-process ownership of one checkout's mutable generated content."""

from __future__ import annotations

from collections.abc import Iterator
from contextlib import contextmanager
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import tempfile
from typing import Any, BinaryIO
from uuid import uuid4

import psutil


LOCK_FILE = ".elysium-activity.lock"
OWNER_FILE = ".elysium-activity.json"
_UNREAL_PROCESSES = {
    "livecodingconsole.exe",
    "unrealeditor.exe",
    "unrealeditor-cmd.exe",
}


class WorkspaceBusy(RuntimeError):
    """Another command owns the mutable state rooted at this export corpus."""


class ProjectBusy(RuntimeError):
    """Unreal still holds this checkout's project open."""


def active_unreal_processes(project: Path) -> tuple[dict[str, Any], ...]:
    """Every Unreal process holding this exact project, matched on its command line.

    The match is per project path, so a checkout only ever sees its own editors and never
    the ones belonging to another checkout building or running beside it.
    """

    needle = os.path.normcase(str(project.resolve()))
    found: list[dict[str, Any]] = []
    for process in psutil.process_iter(("pid", "name")):
        try:
            name = str(process.info.get("name") or "").lower()
            if name not in _UNREAL_PROCESSES:
                continue
            # A command line is fetched per process (on Windows it opens the
            # process), so only the name-matched few pay for it.
            command = " ".join(process.cmdline() or ())
            if needle not in os.path.normcase(command):
                continue
            found.append({"pid": process.pid, "name": process.info.get("name") or name})
        except (psutil.AccessDenied, psutil.NoSuchProcess, OSError):
            continue
    return tuple(found)


def assert_project_idle(project: Path) -> None:
    processes = active_unreal_processes(project)
    if not processes:
        return
    detail = ", ".join(f"{item['name']} pid {item['pid']}" for item in processes)
    raise ProjectBusy(f"Unreal still has this project open: {detail}")


def _lock(handle: BinaryIO, *, blocking: bool) -> None:
    handle.seek(0)
    if os.name == "nt":
        import msvcrt

        mode = msvcrt.LK_LOCK if blocking else msvcrt.LK_NBLCK
        msvcrt.locking(handle.fileno(), mode, 1)
        return

    import fcntl

    operation = fcntl.LOCK_EX
    if not blocking:
        operation |= fcntl.LOCK_NB
    fcntl.flock(handle.fileno(), operation)


def _unlock(handle: BinaryIO) -> None:
    handle.seek(0)
    if os.name == "nt":
        import msvcrt

        msvcrt.locking(handle.fileno(), msvcrt.LK_UNLCK, 1)
        return

    import fcntl

    fcntl.flock(handle.fileno(), fcntl.LOCK_UN)


def _open_lock(path: Path) -> BinaryIO:
    path.parent.mkdir(parents=True, exist_ok=True)
    handle = path.open("a+b")
    handle.seek(0, os.SEEK_END)
    if handle.tell() == 0:
        handle.write(b"\0")
        handle.flush()
    handle.seek(0)
    return handle


def _write_json_atomic(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = json.dumps(value, indent=2, sort_keys=True) + "\n"
    with tempfile.NamedTemporaryFile(
        "w", encoding="utf-8", newline="\n", delete=False, dir=path.parent
    ) as handle:
        handle.write(payload)
        temporary = Path(handle.name)
    temporary.replace(path)


def _read_owner(path: Path) -> dict[str, Any] | None:
    try:
        value = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, ValueError):
        return None
    return value if isinstance(value, dict) else None


@contextmanager
def exclusive_path_lock(path: Path) -> Iterator[None]:
    """Hold an OS lock on `path` for the duration of the block.

    `WorkspaceLease` is keyed on one checkout's export root, so two checkouts never
    exclude each other -- which is the point for generated content each owns alone. This
    guards the opposite case: state every checkout on the machine shares, such as the
    engine tree they all build against. The lock is released by the OS when the process
    dies, so a killed command leaves nothing to clean up.
    """

    handle = _open_lock(path)
    try:
        _lock(handle, blocking=True)
        try:
            yield
        finally:
            _unlock(handle)
    finally:
        handle.close()


class WorkspaceLease:
    """An OS-released lease held for a build, bake, test, editor, or play process."""

    def __init__(
        self,
        export_root: Path,
        command: str,
        worktree: Path,
        *,
        metadata: dict[str, Any] | None = None,
    ) -> None:
        self.export_root = export_root.resolve()
        self.command = command
        self.worktree = worktree.resolve()
        self.metadata = dict(metadata or {})
        self.token = uuid4().hex
        self._handle: BinaryIO | None = None

    @property
    def lock_path(self) -> Path:
        return self.export_root / LOCK_FILE

    @property
    def owner_path(self) -> Path:
        return self.export_root / OWNER_FILE

    def __enter__(self) -> "WorkspaceLease":
        handle = _open_lock(self.lock_path)
        try:
            _lock(handle, blocking=False)
        except OSError as exc:
            handle.close()
            owner = _read_owner(self.owner_path) or {}
            command = owner.get("command", "another command")
            pid = owner.get("pid")
            suffix = f" (pid {pid})" if pid else ""
            raise WorkspaceBusy(
                f"generated-state lane is busy: {command}{suffix}; "
                f"export root {self.export_root}"
            ) from exc

        self._handle = handle
        try:
            _write_json_atomic(
                self.owner_path,
                {
                    "schema": 1,
                    "token": self.token,
                    "command": self.command,
                    "pid": os.getpid(),
                    "started_at": datetime.now(timezone.utc).isoformat(),
                    "worktree": str(self.worktree),
                    "export_root": str(self.export_root),
                    **self.metadata,
                },
            )
        except Exception:
            _unlock(handle)
            handle.close()
            self._handle = None
            raise
        return self

    def __exit__(self, _type: object, _value: object, _traceback: object) -> None:
        handle = self._handle
        if handle is None:
            return
        try:
            owner = _read_owner(self.owner_path)
            if owner is not None and owner.get("token") == self.token:
                self.owner_path.unlink(missing_ok=True)
        finally:
            _unlock(handle)
            handle.close()
            self._handle = None


def active_lease(export_root: Path) -> dict[str, Any] | None:
    """Return the current owner, ignoring stale metadata left by a terminated process."""

    root = export_root.resolve()
    lock_path = root / LOCK_FILE
    if not lock_path.is_file():
        return None
    handle = _open_lock(lock_path)
    try:
        try:
            _lock(handle, blocking=False)
        except OSError:
            return _read_owner(root / OWNER_FILE) or {"command": "another command"}
        _unlock(handle)
        return None
    finally:
        handle.close()
