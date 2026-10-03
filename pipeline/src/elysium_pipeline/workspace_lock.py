"""Cross-process ownership of one checkout's mutable generated content."""

from __future__ import annotations

from collections.abc import Callable, Iterator
from contextlib import contextmanager
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import tempfile
import time
from typing import Any, BinaryIO
from uuid import uuid4

import psutil


LOCK_FILE = ".elysium-activity.lock"
OWNER_FILE = ".elysium-activity.json"

#: The CLI exit code of a command that was refused, or gave up waiting, because another one holds
#: the lease or an editor holds the project. Distinct from every failure the command itself can
#: have (a build, an export, a test verdict), so a caller can tell "did not run" from "ran and failed".
EXIT_BUSY = 8

#: Where one checkout keeps its own lease: inside the checkout, in the ignored `Saved/` tree, so the
#: location is the key and a sibling checkout -- which has its own -- is never asked.
CHECKOUT_LEASE_DIR = ("Saved", "Elysium", "leases")
CHECKOUT_LOCK_FILE = "checkout.lock"
CHECKOUT_OWNER_FILE = "checkout.json"

#: How long a command waits for a held lease before giving up, and how often it looks again.
DEFAULT_WAIT_SECONDS = 30 * 60.0
POLL_SECONDS = 0.5
_UNREAL_PROCESSES = {
    "livecodingconsole.exe",
    "unrealeditor.exe",
    "unrealeditor-cmd.exe",
}


class WorkspaceBusy(RuntimeError):
    """Another command owns the lease: refused at once (`--no-wait`) or still held after the wait."""

    exit_code = EXIT_BUSY


class ProjectBusy(RuntimeError):
    """Unreal still holds this checkout's project open."""

    exit_code = EXIT_BUSY


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


def describe_owner(owner: dict[str, Any] | None, *, now: datetime | None = None) -> str:
    """One phrase naming a lease holder: the command, its pid, its checkout and how long it has run."""

    owner = owner or {}
    parts = [str(owner.get("command") or "another command")]
    detail = []
    if owner.get("pid"):
        detail.append(f"pid {owner['pid']}")
    if owner.get("worktree"):
        detail.append(str(owner["worktree"]))
    try:
        started = datetime.fromisoformat(str(owner["started_at"]))
        seconds = max(0, int(((now or datetime.now(timezone.utc)) - started).total_seconds()))
        detail.append(f"{seconds // 60}m{seconds % 60:02d}s ago" if seconds >= 60 else f"{seconds}s ago")
    except (KeyError, ValueError, TypeError):
        pass
    if detail:
        parts.append("(" + ", ".join(detail) + ")")
    return " ".join(parts)


def checkout_lease_root(worktree: Path) -> Path:
    """The directory one checkout's own lease lives in."""

    return worktree.resolve().joinpath(*CHECKOUT_LEASE_DIR)


class WorkspaceLease:
    """An OS-released lease held for a build, bake, test, editor, or play process.

    Two scopes share this class. The export-root lease (`WorkspaceLease(export_root, ...)`) guards
    exports and bakes, which every checkout on the machine writes into one place. The checkout
    lease (`WorkspaceLease.for_checkout(...)`) guards what one checkout alone holds -- its built
    binaries and its running editor -- and lives inside that checkout, so an idle sibling never
    blocks it.

    `wait_seconds` is how long to wait for a held lease: `0` refuses at once (`--no-wait`), a
    positive bound polls every `poll_seconds` and calls `on_wait` once, with a phrase naming the
    holder, when the wait begins. The OS releases a dead owner's lock, so a stale lease never
    needs clearing; the metadata it leaves is ignored.
    """

    def __init__(
        self,
        export_root: Path,
        command: str,
        worktree: Path,
        *,
        metadata: dict[str, Any] | None = None,
        lock_name: str = LOCK_FILE,
        owner_name: str = OWNER_FILE,
        scope: str = "generated-state lane",
        wait_seconds: float = 0.0,
        poll_seconds: float = POLL_SECONDS,
        on_wait: Callable[[str], None] | None = None,
    ) -> None:
        self.export_root = export_root.resolve()
        self.command = command
        self.worktree = worktree.resolve()
        self.metadata = dict(metadata or {})
        self.token = uuid4().hex
        self.lock_name = lock_name
        self.owner_name = owner_name
        self.scope = scope
        self.wait_seconds = wait_seconds
        self.poll_seconds = poll_seconds
        self.on_wait = on_wait
        #: Seconds the last `__enter__` spent waiting, so a caller can say so.
        self.waited = 0.0
        self._handle: BinaryIO | None = None

    @classmethod
    def for_checkout(cls, worktree: Path, command: str, **options: Any) -> "WorkspaceLease":
        """The lease on one checkout's binaries and editor, keyed by the checkout itself."""

        return cls(
            checkout_lease_root(worktree),
            command,
            worktree,
            lock_name=CHECKOUT_LOCK_FILE,
            owner_name=CHECKOUT_OWNER_FILE,
            scope="checkout",
            **options,
        )

    @property
    def lock_path(self) -> Path:
        return self.export_root / self.lock_name

    @property
    def owner_path(self) -> Path:
        return self.export_root / self.owner_name

    def _busy(self, owner: dict[str, Any], *, waited: float) -> WorkspaceBusy:
        holder = describe_owner(owner)
        if waited > 0:
            return WorkspaceBusy(
                f"{self.scope} is still busy after waiting {waited:.0f}s: {holder}; "
                f"lease {self.lock_path}"
            )
        return WorkspaceBusy(
            f"{self.scope} is busy: {holder}; not waiting (--no-wait); lease {self.lock_path}"
        )

    def __enter__(self) -> "WorkspaceLease":
        handle = _open_lock(self.lock_path)
        began = time.monotonic()
        announced = False
        while True:
            try:
                _lock(handle, blocking=False)
                break
            except OSError as exc:
                waited = time.monotonic() - began
                owner = _read_owner(self.owner_path) or {}
                if waited >= self.wait_seconds:
                    handle.close()
                    raise self._busy(owner, waited=waited if announced else 0.0) from exc
                if not announced:
                    announced = True
                    if self.on_wait is not None:
                        self.on_wait(
                            f"waiting for {describe_owner(owner)} to release the {self.scope} "
                            f"lease (up to {self.wait_seconds / 60:.0f} min; --no-wait refuses instead)"
                        )
                time.sleep(min(self.poll_seconds, max(0.0, self.wait_seconds - waited)))
        self.waited = time.monotonic() - began

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


def active_lease(
    export_root: Path,
    *,
    lock_name: str = LOCK_FILE,
    owner_name: str = OWNER_FILE,
) -> dict[str, Any] | None:
    """Return the current owner, ignoring stale metadata left by a terminated process."""

    root = export_root.resolve()
    lock_path = root / lock_name
    if not lock_path.is_file():
        return None
    handle = _open_lock(lock_path)
    try:
        try:
            _lock(handle, blocking=False)
        except OSError:
            return _read_owner(root / owner_name) or {"command": "another command"}
        _unlock(handle)
        return None
    finally:
        handle.close()


def active_checkout_lease(worktree: Path) -> dict[str, Any] | None:
    """The command holding this checkout's lease, or `None`."""

    return active_lease(
        checkout_lease_root(worktree),
        lock_name=CHECKOUT_LOCK_FILE,
        owner_name=CHECKOUT_OWNER_FILE,
    )
