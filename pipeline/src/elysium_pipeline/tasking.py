"""Small deterministic task graph and export manifest primitives."""

from __future__ import annotations

from collections.abc import Callable, Iterable, Mapping
from concurrent.futures import FIRST_COMPLETED, Future, ThreadPoolExecutor, wait
from contextlib import contextmanager
from dataclasses import dataclass, field
from datetime import datetime, timezone
import hashlib
from importlib.metadata import PackageNotFoundError, version
import io
import json
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile
import threading
import time
from typing import Any


Action = Callable[[], None]
Fingerprint = Callable[[], str]


@dataclass(frozen=True)
class Task:
    name: str
    action: Action
    dependencies: tuple[str, ...] = ()
    fingerprint: Fingerprint | None = None
    outputs: tuple[Path, ...] = ()


@dataclass
class TaskResult:
    name: str
    status: str
    duration_seconds: float
    error: str | None = None
    fingerprint: str | None = None
    outputs: list[str] = field(default_factory=list)
    dependencies: list[str] = field(default_factory=list)

    @property
    def succeeded(self) -> bool:
        return self.status in {"ok", "skipped"}


class TaskFailure(RuntimeError):
    def __init__(self, results: Mapping[str, TaskResult]):
        self.results = dict(results)
        failed = [name for name, result in results.items() if not result.succeeded]
        super().__init__("task failure: " + ", ".join(failed))


class _StreamRouter:
    """`sys.stdout`/`sys.stderr` replacement for a progress-reported run.

    A thread that registered a buffer has every write land there; every other
    thread passes through to the real stream. This is what lets concurrent graph
    tasks print freely without interleaving on the console."""

    def __init__(self, base):
        self.base = base
        self._routes: dict[int, io.StringIO] = {}

    def register(self, buffer: io.StringIO) -> None:
        self._routes[threading.get_ident()] = buffer

    def release(self) -> None:
        self._routes.pop(threading.get_ident(), None)

    def write(self, text: str) -> int:
        return self._routes.get(threading.get_ident(), self.base).write(text)

    def flush(self) -> None:
        self._routes.get(threading.get_ident(), self.base).flush()

    def isatty(self) -> bool:
        return bool(getattr(self.base, "isatty", lambda: False)())


#: A captured line whose first non-space characters are ``!`` (the exporters'
#: warning marker) is surfaced on the console; everything else stays in the log.
_WARNING_LINE = re.compile(r"^\s*!+\s")

#: How many of one task's warning lines reach the console before eliding.
_WARNING_LIMIT = 8


class TaskProgress:
    """One console line per task; the complete captured output in a log file.

    Installed as a context manager around a graph run: it replaces the process
    streams with routers, gives every task its own capture buffer, prints
    ``[done/total] name status`` as tasks finish, surfaces ``!``-marked warning
    lines beneath the task that produced them, and dumps a failed task's output
    tail so the error context never has to be dug out of the log."""

    def __init__(self, total: int, log_path: Path, *, per_task: bool = True):
        self.total = total
        self.log_path = log_path
        #: When False, a clean task prints nothing: only tasks with warnings or a
        #: failure reach the console, plus the closing tally. The fit for large
        #: homogeneous graphs (one task per container) where per-task lines are noise.
        self.per_task = per_task
        self._lock = threading.Lock()
        self.done = 0
        self.warnings = 0
        self.counts: dict[str, int] = {}
        self._log = None
        self._stdout = None
        self._stderr = None
        #: Live status line (tty only): what is running right now and for how
        #: long, rewritten in place so a long task never looks stalled.
        self._active: dict[str, float] = {}
        self._status_len = 0
        self._tty = False
        self._ticker: threading.Thread | None = None
        self._stop = threading.Event()

    def __enter__(self) -> "TaskProgress":
        self.log_path.parent.mkdir(parents=True, exist_ok=True)
        self._log = self.log_path.open("w", encoding="utf-8")
        self._stdout, self._stderr = sys.stdout, sys.stderr
        self._router_out = _StreamRouter(self._stdout)
        self._router_err = _StreamRouter(self._stderr)
        sys.stdout, sys.stderr = self._router_out, self._router_err
        self._tty = bool(getattr(self._stdout, "isatty", lambda: False)())
        if self._tty:
            self._ticker = threading.Thread(target=self._tick, daemon=True)
            self._ticker.start()
        return self

    def __exit__(self, *exc) -> None:
        self._stop.set()
        if self._ticker is not None:
            self._ticker.join(timeout=2.0)
        self._tty = False
        sys.stdout, sys.stderr = self._stdout, self._stderr
        with self._lock:
            self._clear_status()
        self._console(self.summary())
        self._log.close()

    @contextmanager
    def capture(self):
        buffer = io.StringIO()
        self._router_out.register(buffer)
        self._router_err.register(buffer)
        try:
            yield buffer
        finally:
            self._router_out.release()
            self._router_err.release()

    @contextmanager
    def stage(self, name: str):
        """Capture one uncounted phase that runs outside the graph (e.g. the
        shared-corpus decode), reported on the same console/log seam."""
        started = time.monotonic()
        self.start(name)
        with self.capture() as buffer:
            try:
                yield
            except BaseException as exc:
                self.emit(name, "failed", time.monotonic() - started,
                          buffer.getvalue(), counted=False,
                          error=f"{type(exc).__name__}: {exc}")
                raise
        self.emit(name, "ok", time.monotonic() - started, buffer.getvalue(),
                  counted=False)

    def start(self, name: str) -> None:
        """Mark a task as running so the live status line can show it."""
        with self._lock:
            self._active[name] = time.monotonic()
            self._draw_status()

    def _tick(self) -> None:
        while not self._stop.wait(1.0):
            with self._lock:
                self._draw_status()

    def _clear_status(self) -> None:
        if self._status_len:
            self._stdout.write("\r" + " " * self._status_len + "\r")
            self._stdout.flush()
            self._status_len = 0

    def _draw_status(self) -> None:
        if not self._tty:
            return
        now = time.monotonic()
        running = sorted(self._active.items(), key=lambda kv: kv[1])
        parts = [f"{name} {now - t:.0f}s" for name, t in running[:4]]
        if len(running) > 4:
            parts.append(f"+{len(running) - 4} more")
        text = f"\u00bb {self.done}/{self.total}"
        if parts:
            text += " \u00b7 running: " + ", ".join(parts)
        width = shutil.get_terminal_size(fallback=(120, 25)).columns - 1
        text = text[:width]
        pad = max(0, self._status_len - len(text))
        self._stdout.write("\r" + text + " " * pad + "\r" + text)
        self._stdout.flush()
        self._status_len = len(text)

    def note(self, line: str) -> None:
        with self._lock:
            self._console(line)
            self._log.write(line + "\n")

    def emit(self, name: str, status: str, seconds: float, text: str,
             *, counted: bool = True, error: str | None = None) -> None:
        with self._lock:
            self._active.pop(name, None)
            self._log.write(f"==== {name} [{status}] {seconds:.1f}s ====\n")
            if text:
                self._log.write(text if text.endswith("\n") else text + "\n")
            if error:
                self._log.write(error + "\n")
            self._log.flush()

            warnings = [line for line in text.splitlines() if _WARNING_LINE.match(line)]
            self.warnings += len(warnings)
            self.counts[status] = self.counts.get(status, 0) + 1
            head = ""
            if counted:
                self.done += 1
                width = len(str(self.total))
                head = f"[{self.done:>{width}}/{self.total}] "
            shown = "reused" if status == "skipped" else status.upper() if status in {
                "failed", "blocked"} else status
            timing = f"  {seconds:.1f}s" if status == "ok" and seconds >= 0.05 else ""
            note = f"  ({len(warnings)} warning{'s' if len(warnings) != 1 else ''})" \
                if warnings else ""
            if not self.per_task and status in {"ok", "skipped"} and not warnings:
                return
            self._console(f"{head}{name:<28} {shown}{timing}{note}")
            if status == "failed":
                # The output tail carries the warnings too; one copy is enough.
                for line in text.splitlines()[-30:]:
                    self._console("      " + line)
                if error:
                    self._console("      " + error)
                return
            for line in warnings[:_WARNING_LIMIT]:
                self._console("      " + line.strip())
            if len(warnings) > _WARNING_LIMIT:
                self._console(f"      … {len(warnings) - _WARNING_LIMIT} more: {self.log_path}")

    def summary(self) -> str:
        parts = []
        for status, label in (("ok", "ok"), ("skipped", "reused"),
                              ("failed", "failed"), ("blocked", "blocked")):
            if self.counts.get(status):
                parts.append(f"{self.counts[status]} {label}")
        left = self.total - self.done
        if left > 0:
            parts.append(f"{left} not run")
        parts.append(f"{self.warnings} warning{'s' if self.warnings != 1 else ''}")
        return " · ".join(parts) + f" — task log: {self.log_path}"

    def _console(self, line: str) -> None:
        self._clear_status()
        # Everything reaching here has already passed this class's own filter: the `!` marker,
        # `_WARNING_LIMIT`, the failure tail, the closing tally. When the CLI is above us its
        # stdout is a filter too, tuned for raw decoder chatter, and running these lines
        # through it a second time drops all of them -- including a failed task's output tail,
        # the one thing a failure has to say. `write_signal` is the CLI's "already curated"
        # door; found by name so this module keeps knowing nothing about that one.
        write_signal = getattr(self._stdout, "write_signal", None)
        if write_signal is not None:
            write_signal(line)
        else:
            self._stdout.write(line + "\n")
            self._stdout.flush()
        self._draw_status()


class Manifest:
    """Atomic task fingerprint store rooted in the generated export corpus."""

    SCHEMA = 1

    def __init__(self, path: Path):
        self.path = path
        self.dirty = False
        self.data: dict[str, Any] = {
            "schema": self.SCHEMA,
            "tool_version": _tool_version(),
            "tasks": {},
        }
        if path.is_file():
            try:
                loaded = json.loads(path.read_text(encoding="utf-8"))
                if loaded.get("schema") == self.SCHEMA:
                    self.data = loaded
            except (OSError, ValueError):
                pass
        self.data["tool_version"] = _tool_version()

    def set_context(self, **values: Any) -> None:
        """Record the inputs that define this export run independently of tasks."""

        self.data.update(values)
        self.dirty = True
        self.write()

    def can_skip(self, task: Task, fingerprint: str | None) -> bool:
        if fingerprint is None:
            return False
        saved = self.data.get("tasks", {}).get(task.name, {})
        if saved.get("status") != "complete" or saved.get("fingerprint") != fingerprint:
            return False
        # A generated package family is an inventory, not merely one sentinel file.  Requiring
        # the current inventory to agree with the recorded one makes a removed or unexpected
        # asset invalidate the task just like a missing declared output does.
        current_outputs = sorted(str(path) for path in task.outputs)
        saved_outputs = sorted(str(path) for path in saved.get("outputs", []))
        if current_outputs != saved_outputs:
            return False
        return all(path.exists() for path in task.outputs)

    def record(self, result: TaskResult, *, flush: bool = True) -> None:
        """Store one task receipt; ``flush=False`` leaves it in memory for a later `write`."""
        tasks = self.data.setdefault("tasks", {})
        tasks[result.name] = {
            "status": "complete" if result.succeeded else "failed",
            "fingerprint": result.fingerprint,
            "outputs": result.outputs,
            "dependencies": result.dependencies,
            "duration_seconds": round(result.duration_seconds, 6),
            "error": result.error,
            "updated_at": datetime.now(timezone.utc).isoformat(),
        }
        self.dirty = True
        if flush:
            self.write()

    #: Windows fails an atomic replace with a sharing violation whenever anything else holds the
    #: destination open for even a moment -- an indexer or a virus scanner reading the file this
    #: process wrote microseconds earlier is enough. A profile records hundreds of receipts, so a
    #: one-in-a-thousand collision is a run that fails for no reason the export can report. The
    #: replace is still atomic; only the attempt is retried.
    WRITE_ATTEMPTS = 8
    WRITE_BACKOFF_SECONDS = 0.05

    def write(self) -> None:
        if not self.dirty:
            return
        self.path.parent.mkdir(parents=True, exist_ok=True)
        payload = json.dumps(self.data, indent=2, sort_keys=True) + "\n"
        with tempfile.NamedTemporaryFile(
            "w", encoding="utf-8", newline="\n", delete=False, dir=self.path.parent
        ) as handle:
            handle.write(payload)
            temporary = Path(handle.name)
        for attempt in range(self.WRITE_ATTEMPTS):
            try:
                temporary.replace(self.path)
                self.dirty = False
                return
            except PermissionError:
                if attempt == self.WRITE_ATTEMPTS - 1:
                    # Do not leave the temporary behind for a future run to trip over.
                    temporary.unlink(missing_ok=True)
                    raise
                time.sleep(self.WRITE_BACKOFF_SECONDS * (attempt + 1))


#: The persistent digest cache the export root carries, shared by every consumer that hashes
#: generated intermediates -- the offline fingerprints and the editor bakes alike.
DIGEST_CACHE_FILE = ".elysium-content-digests.json"


class ContentDigestCache:
    """Persistent SHA-256 cache keyed by stable file metadata."""

    SCHEMA = 1
    RACY_WINDOW_NS = 2_000_000_000

    def __init__(self, path: Path):
        self.path = path
        self.entries: dict[str, dict[str, Any]] = {}
        self.dirty = False
        if path.is_file():
            try:
                loaded = json.loads(path.read_text(encoding="utf-8"))
                if loaded.get("schema") == self.SCHEMA:
                    self.entries = loaded.get("entries", {})
            except (OSError, ValueError):
                pass

    def digest(self, path: Path) -> str:
        resolved = path.resolve()
        key = str(resolved)
        stat = resolved.stat()
        # Size plus mtime_ns is the identity (creation time is not part of it: an atomic
        # temp+replace publish gives even byte-identical output a fresh one). The match reads
        # only the identity keys, so a saved entry carrying extra metadata keys still hits,
        # and a malformed entry is a plain miss that re-hashes once.
        identity = {
            "size": stat.st_size,
            "mtime_ns": stat.st_mtime_ns,
        }
        # A file written inside the last RACY_WINDOW_NS is racy: a same-size rewrite landing
        # within the filesystem's timestamp tick keeps the identity while changing the bytes,
        # so such a file is always hashed and never recorded (the rule git's index applies).
        # It settles into the store on the first check after the window.
        racy = time.time_ns() - stat.st_mtime_ns < self.RACY_WINDOW_NS
        saved = self.entries.get(key)
        if not racy and isinstance(saved, dict) and all(
                saved.get(name) == value for name, value in identity.items()):
            value = saved.get("sha256")
            if isinstance(value, str) and len(value) == 64:
                return value

        digest = hashlib.sha256()
        with resolved.open("rb") as handle:
            while chunk := handle.read(1024 * 1024):
                digest.update(chunk)
        value = digest.hexdigest()
        if not racy:
            self.entries[key] = {**identity, "sha256": value}
            self.dirty = True
        return value

    def write(self) -> None:
        """Persist the store, keeping entries another writer added since this one loaded.

        An editor commandlet hashes into the same file while the parent process holds its
        own instance; the parent's later write overlays its entries on the current on-disk
        document instead of replacing it, so neither writer drops the other's digests."""
        if not self.dirty:
            return
        self.path.parent.mkdir(parents=True, exist_ok=True)
        merged: dict[str, dict[str, Any]] = {}
        if self.path.is_file():
            try:
                current = json.loads(self.path.read_text(encoding="utf-8"))
                if current.get("schema") == self.SCHEMA:
                    merged = dict(current.get("entries", {}))
            except (OSError, ValueError):
                merged = {}
        merged.update(self.entries)
        self.entries = merged
        payload = json.dumps(
            {"schema": self.SCHEMA, "entries": self.entries},
            indent=2,
            sort_keys=True,
        ) + "\n"
        with tempfile.NamedTemporaryFile(
            "w", encoding="utf-8", newline="\n", delete=False, dir=self.path.parent
        ) as handle:
            handle.write(payload)
            temporary = Path(handle.name)
        temporary.replace(self.path)
        self.dirty = False


class TaskGraph:
    #: How long deferred manifest receipts may sit in memory between interval flushes. The
    #: scheduler records every result without flushing and writes on this cadence plus a final
    #: flush in ``run``'s ``finally``, so a run pays a handful of full-document serializations
    #: rather than one per task, and a hard crash loses at most this window of receipts.
    MANIFEST_FLUSH_SECONDS = 5.0

    def __init__(self, tasks: Iterable[Task]):
        task_list = list(tasks)
        self.tasks = {task.name: task for task in task_list}
        if len(self.tasks) != len(task_list):
            raise ValueError("task names must be unique")
        for task in self.tasks.values():
            missing = set(task.dependencies) - self.tasks.keys()
            if missing:
                raise ValueError(f"{task.name} has unknown dependencies: {sorted(missing)}")
        self._assert_acyclic()

    def _assert_acyclic(self) -> None:
        pending = set(self.tasks)
        completed: set[str] = set()
        while pending:
            ready = {
                name
                for name in pending
                if set(self.tasks[name].dependencies).issubset(completed)
            }
            if not ready:
                raise ValueError(f"task dependency cycle: {sorted(pending)}")
            completed.update(ready)
            pending.difference_update(ready)

    def run(
        self,
        *,
        jobs: int = 1,
        force: bool = False,
        manifest: Manifest | None = None,
        fail_fast: bool = True,
        progress: TaskProgress | None = None,
    ) -> dict[str, TaskResult]:
        jobs = max(1, jobs)
        # Sorted once: dispatch order is deterministic by name without a re-sort per pass.
        pending = sorted(self.tasks)
        running: dict[Future[tuple[TaskResult, str]], str] = {}
        results: dict[str, TaskResult] = {}
        manifest_flushed = time.monotonic()

        def record_result(result: TaskResult) -> None:
            # The manifest write serializes the whole document, so the scheduler defers each
            # receipt and flushes on an interval; the run's finally writes whatever remains.
            nonlocal manifest_flushed
            manifest.record(result, flush=False)
            if time.monotonic() - manifest_flushed >= self.MANIFEST_FLUSH_SECONDS:
                manifest.write()
                manifest_flushed = time.monotonic()

        def execute_captured(task: Task) -> tuple[TaskResult, str]:
            if progress is None:
                return execute(task), ""
            with progress.capture() as buffer:
                result = execute(task)
            return result, buffer.getvalue()

        def execute(task: Task) -> TaskResult:
            started = time.monotonic()
            fingerprint = task.fingerprint() if task.fingerprint else None
            if not force and manifest and manifest.can_skip(task, fingerprint):
                return TaskResult(
                    task.name,
                    "skipped",
                    time.monotonic() - started,
                    fingerprint=fingerprint,
                    outputs=[str(path) for path in task.outputs],
                    dependencies=list(task.dependencies),
                )
            try:
                task.action()
                missing = [str(path) for path in task.outputs if not path.exists()]
                if missing:
                    raise RuntimeError("expected outputs missing: " + ", ".join(missing))
                return TaskResult(
                    task.name,
                    "ok",
                    time.monotonic() - started,
                    fingerprint=fingerprint,
                    outputs=[str(path) for path in task.outputs],
                    dependencies=list(task.dependencies),
                )
            except BaseException as exc:
                return TaskResult(
                    task.name,
                    "failed",
                    time.monotonic() - started,
                    error=f"{type(exc).__name__}: {exc}",
                    fingerprint=fingerprint,
                    outputs=[str(path) for path in task.outputs],
                    dependencies=list(task.dependencies),
                )

        def drain_after_interrupt() -> None:
            # First Ctrl+C: nothing new is scheduled and queued work is
            # cancelled, but a running thread cannot be killed -- wait for it
            # in short, interruptible slices. Second Ctrl+C: abort the
            # process without joining (os._exit), because every graceful exit
            # path joins those threads and blocks exactly the same way.
            for queued in running:
                queued.cancel()
            message = (f"interrupted -- waiting for {len(running)} running "
                       f"task(s); Ctrl+C again to abort immediately")
            if progress:
                progress.note(message)
            else:
                print(message, flush=True)
            try:
                while any(not f.done() for f in running):
                    time.sleep(0.2)
            except KeyboardInterrupt:
                os._exit(130)

        executor = ThreadPoolExecutor(max_workers=jobs)
        try:
            # A scheduling pass walks the still-sorted pending list once. It runs only when a
            # completion or a fresh blocked result can change what is ready, never while every
            # worker slot is occupied, and stops early once the slots fill with no failure to
            # propagate -- past that point the rest of the list cannot change state.
            scan_due = True
            have_failures = False
            while pending or running:
                if scan_due and pending and len(running) < jobs:
                    scan_due = False
                    remaining: list[str] = []
                    for index, name in enumerate(pending):
                        if len(running) >= jobs and not have_failures:
                            remaining.extend(pending[index:])
                            break
                        task = self.tasks[name]
                        dependency_results = [results.get(dep) for dep in task.dependencies]
                        if any(result is not None and not result.succeeded
                               for result in dependency_results):
                            result = TaskResult(
                                name,
                                "blocked",
                                0.0,
                                error="dependency failed",
                                dependencies=list(task.dependencies),
                            )
                            results[name] = result
                            if manifest:
                                record_result(result)
                            if progress:
                                progress.emit(name, "blocked", 0.0, "")
                            # A blocked result can block its own dependents on the next pass.
                            scan_due = True
                            continue
                        if len(running) < jobs and all(
                                result is not None for result in dependency_results):
                            if progress:
                                progress.start(name)
                            running[executor.submit(execute_captured, task)] = name
                            continue
                        remaining.append(name)
                    made_progress = len(remaining) != len(pending)
                    pending = remaining
                    if not running:
                        if pending and not made_progress:
                            raise RuntimeError("task graph stalled")
                        continue
                done, _ = wait(running, timeout=0.5, return_when=FIRST_COMPLETED)
                for future in done:
                    name = running.pop(future)
                    result, captured = future.result()
                    results[name] = result
                    if manifest:
                        record_result(result)
                    if progress:
                        progress.emit(result.name, result.status,
                                      result.duration_seconds, captured,
                                      error=result.error)
                    scan_due = True
                    if not result.succeeded:
                        have_failures = True
                    if fail_fast and not result.succeeded:
                        for queued in running:
                            queued.cancel()
                        raise TaskFailure(results)
        except KeyboardInterrupt:
            drain_after_interrupt()
            raise
        finally:
            # Deferred receipts must reach disk however the run ends -- normal return,
            # TaskFailure, or interrupt -- so completed work never re-runs for lack of a record.
            if manifest:
                manifest.write()
            executor.shutdown(wait=True, cancel_futures=True)

        if any(not result.succeeded for result in results.values()):
            raise TaskFailure(results)
        return results


def _fingerprint_directory_files(root: Path) -> list[Path]:
    """A directory's files as fingerprint entries. Bytecode caches are interpreter
    by-products, not task inputs, so they never participate in a fingerprint."""
    return sorted(
        (
            entry
            for entry in root.rglob("*")
            if entry.is_file() and "__pycache__" not in entry.parts
        ),
        key=lambda item: str(item).lower(),
    )


def fingerprint_paths(paths: Iterable[Path], *, extra: Iterable[str] = ()) -> str:
    digest = hashlib.sha256()
    for value in extra:
        digest.update(value.encode("utf-8"))
        digest.update(b"\0")
    for path in sorted((item.resolve() for item in paths), key=lambda item: str(item).lower()):
        digest.update(str(path).encode("utf-8"))
        digest.update(b"\0")
        if path.is_file():
            stat = path.stat()
            digest.update(f"{stat.st_size}:{stat.st_mtime_ns}".encode("ascii"))
        elif path.is_dir():
            for child in _fingerprint_directory_files(path):
                stat = child.stat()
                digest.update(str(child.relative_to(path)).encode("utf-8"))
                digest.update(f"{stat.st_size}:{stat.st_mtime_ns}".encode("ascii"))
        else:
            digest.update(b"missing")
        digest.update(b"\0")
    return digest.hexdigest()


def fingerprint_content(
    paths: Iterable[Path],
    *,
    extra: Iterable[str] = (),
    cache: ContentDigestCache | None = None,
) -> str:
    """Hash path identities and file bytes rather than mtimes.

    Exporters are allowed to replace a generated intermediate with byte-identical output.  An
    mtime fingerprint would turn that harmless rewrite into a costly Unreal bake, while a content
    fingerprint remains stable.  Directory membership is part of the digest, so additions,
    removals, and renames still invalidate it.
    """

    digest = hashlib.sha256()
    for value in extra:
        digest.update(value.encode("utf-8"))
        digest.update(b"\0")

    roots = sorted(
        {item.resolve() for item in paths},
        key=lambda item: str(item).lower(),
    )
    for root in roots:
        digest.update(str(root).encode("utf-8", errors="surrogateescape"))
        digest.update(b"\0")
        if root.is_file():
            entries = ((root, Path(root.name)),)
        elif root.is_dir():
            entries = tuple(
                (child, child.relative_to(root))
                for child in _fingerprint_directory_files(root)
            )
        else:
            digest.update(b"missing\0")
            continue

        for child, relative in entries:
            digest.update(relative.as_posix().encode("utf-8", errors="surrogateescape"))
            digest.update(b"\0")
            try:
                if cache is not None:
                    child_digest = cache.digest(child)
                else:
                    content = hashlib.sha256()
                    with child.open("rb") as handle:
                        while chunk := handle.read(1024 * 1024):
                            content.update(chunk)
                    child_digest = content.hexdigest()
                digest.update(child_digest.encode("ascii"))
            except OSError:
                digest.update(b"missing")
            digest.update(b"\0")
    return digest.hexdigest()


def _tool_version() -> str:
    try:
        return version("elysium-pipeline")
    except PackageNotFoundError:
        return "unknown"
