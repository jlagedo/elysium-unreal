"""Small deterministic task graph and export manifest primitives."""

from __future__ import annotations

from collections.abc import Callable, Iterable, Mapping
from concurrent.futures import FIRST_COMPLETED, Future, ThreadPoolExecutor, wait
from dataclasses import dataclass, field
from datetime import datetime, timezone
import hashlib
from importlib.metadata import PackageNotFoundError, version
import json
from pathlib import Path
import tempfile
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


class Manifest:
    """Atomic task fingerprint store rooted in the generated export corpus."""

    SCHEMA = 1

    def __init__(self, path: Path):
        self.path = path
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
        self.write()

    def can_skip(self, task: Task, fingerprint: str | None) -> bool:
        if fingerprint is None:
            return False
        saved = self.data.get("tasks", {}).get(task.name, {})
        if saved.get("status") != "complete" or saved.get("fingerprint") != fingerprint:
            return False
        return all(path.exists() for path in task.outputs)

    def record(self, result: TaskResult) -> None:
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
        self.write()

    def write(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        payload = json.dumps(self.data, indent=2, sort_keys=True) + "\n"
        with tempfile.NamedTemporaryFile(
            "w", encoding="utf-8", newline="\n", delete=False, dir=self.path.parent
        ) as handle:
            handle.write(payload)
            temporary = Path(handle.name)
        temporary.replace(self.path)


class TaskGraph:
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
    ) -> dict[str, TaskResult]:
        jobs = max(1, jobs)
        pending = set(self.tasks)
        running: dict[Future[TaskResult], str] = {}
        results: dict[str, TaskResult] = {}

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

        with ThreadPoolExecutor(max_workers=jobs) as executor:
            while pending or running:
                made_progress = False
                for name in sorted(tuple(pending)):
                    task = self.tasks[name]
                    dependency_results = [results.get(dep) for dep in task.dependencies]
                    if any(result is not None and not result.succeeded for result in dependency_results):
                        result = TaskResult(
                            name,
                            "blocked",
                            0.0,
                            error="dependency failed",
                            dependencies=list(task.dependencies),
                        )
                        results[name] = result
                        if manifest:
                            manifest.record(result)
                        pending.remove(name)
                        made_progress = True
                        continue
                    if all(dep in results for dep in task.dependencies) and len(running) < jobs:
                        running[executor.submit(execute, task)] = name
                        pending.remove(name)
                        made_progress = True
                if not running:
                    if pending and not made_progress:
                        raise RuntimeError("task graph stalled")
                    continue
                done, _ = wait(tuple(running), return_when=FIRST_COMPLETED)
                for future in done:
                    name = running.pop(future)
                    result = future.result()
                    results[name] = result
                    if manifest:
                        manifest.record(result)
                    if fail_fast and not result.succeeded:
                        for queued in running:
                            queued.cancel()
                        raise TaskFailure(results)

        if any(not result.succeeded for result in results.values()):
            raise TaskFailure(results)
        return results


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
            for child in sorted(
                (entry for entry in path.rglob("*") if entry.is_file()),
                key=lambda item: str(item).lower(),
            ):
                stat = child.stat()
                digest.update(str(child.relative_to(path)).encode("utf-8"))
                digest.update(f"{stat.st_size}:{stat.st_mtime_ns}".encode("ascii"))
        else:
            digest.update(b"missing")
        digest.update(b"\0")
    return digest.hexdigest()


def _tool_version() -> str:
    try:
        return version("elysium-pipeline")
    except PackageNotFoundError:
        return "unknown"
