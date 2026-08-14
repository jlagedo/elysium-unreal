"""Mutable detached worktrees for isolated agent-owned development tasks."""

from __future__ import annotations

from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import shutil
import tempfile
from typing import Any

from elysium_pipeline.clean import adopt_export_root, mark_incomplete
from elysium_pipeline.lanes import assert_project_idle
from elysium_pipeline.process import ProcessRunner
from elysium_pipeline.workspace_lock import WorkspaceLease, active_lease


TASK_RECORD_FILE = ".elysium-task-worktree.json"
TASK_MARKER_FILE = ".elysium-task-worktree.json"
TASK_SCHEMA = 1
_TASK_NAME = re.compile(r"^[a-z][a-z0-9-]{0,31}$")
_TASK_RUNTIME_PATHS = (
    "pipeline/src/elysium_pipeline/task_worktrees.py",
    "pipeline/src/elysium_pipeline/workspace_lock.py",
)


class TaskWorktreeError(RuntimeError):
    """A mutable task-worktree ownership or lifecycle contract was violated."""


def _now() -> str:
    return datetime.now(timezone.utc).isoformat()


def _same(left: Path, right: Path) -> bool:
    return os.path.normcase(str(left.resolve())) == os.path.normcase(str(right.resolve()))


def _within(path: Path, root: Path) -> bool:
    try:
        path.resolve().relative_to(root.resolve())
        return True
    except ValueError:
        return False


def _write_json_atomic(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = json.dumps(value, indent=2, sort_keys=True) + "\n"
    with tempfile.NamedTemporaryFile(
        "w", encoding="utf-8", newline="\n", delete=False, dir=path.parent
    ) as handle:
        handle.write(payload)
        temporary = Path(handle.name)
    temporary.replace(path)


def _git(
    runner: ProcessRunner,
    repository: Path,
    *arguments: str | os.PathLike[str],
) -> str:
    result = runner.run(["git", "-C", repository, *arguments], cwd=repository)
    if result.returncode:
        detail = result.output.strip().splitlines()
        raise TaskWorktreeError(
            f"git {' '.join(map(os.fspath, arguments))} failed"
            + (f": {detail[-1]}" if detail else "")
        )
    return result.output.strip()


def _common_git_dir(runner: ProcessRunner, repository: Path) -> Path:
    raw = Path(_git(runner, repository, "rev-parse", "--git-common-dir"))
    return (raw if raw.is_absolute() else repository / raw).resolve()


def _commit(runner: ProcessRunner, repository: Path, ref: str) -> str:
    return _git(runner, repository, "rev-parse", "--verify", f"{ref}^{{commit}}")


def _dirty_rows(runner: ProcessRunner, repository: Path) -> tuple[str, ...]:
    output = _git(
        runner,
        repository,
        "status",
        "--porcelain",
        "--untracked-files=all",
    )
    return tuple(line for line in output.splitlines() if line)


def _quoted_environment_value(path: Path) -> str:
    value = str(path.resolve())
    if '"' in value:
        raise TaskWorktreeError(
            f"path cannot be written to the local environment file: {value}"
        )
    return f'"{value}"'


def _assert_task_runtime(
    runner: ProcessRunner, repository: Path, commit: str
) -> None:
    for relative in _TASK_RUNTIME_PATHS:
        try:
            _git(runner, repository, "cat-file", "-e", f"{commit}:{relative}")
        except TaskWorktreeError as exc:
            raise TaskWorktreeError(
                f"candidate {commit[:12]} predates the task-worktree runtime ({relative}); "
                "commit the worktree tooling before creating a task worktree"
            ) from exc


def validate_task_name(name: str) -> str:
    if not _TASK_NAME.fullmatch(name):
        raise TaskWorktreeError(
            "task name must start with a lowercase letter and contain only "
            "lowercase letters, digits, or hyphens (32 characters maximum)"
        )
    if name == "main":
        raise TaskWorktreeError("'main' is the integration checkout, not a task name")
    return name


@dataclass(slots=True)
class TaskWorktreeRecord:
    name: str
    source_repo: Path
    source_work_root: Path
    worktree: Path
    work_root: Path
    export_root: Path
    common_git_dir: Path
    base_commit: str
    created_at: str
    schema: int = field(default=TASK_SCHEMA, init=False)

    @property
    def path(self) -> Path:
        return self.work_root / TASK_RECORD_FILE

    def to_dict(self) -> dict[str, Any]:
        value = asdict(self)
        for name in (
            "source_repo",
            "source_work_root",
            "worktree",
            "work_root",
            "export_root",
            "common_git_dir",
        ):
            value[name] = str(value[name])
        return value

    def save(self) -> None:
        self.validate()
        _write_json_atomic(self.path, self.to_dict())

    @classmethod
    def load(cls, path: Path) -> "TaskWorktreeRecord":
        try:
            value = json.loads(path.read_text(encoding="utf-8-sig"))
        except (OSError, ValueError) as exc:
            raise TaskWorktreeError(f"cannot read task-worktree metadata: {path}") from exc
        if value.get("schema") != TASK_SCHEMA:
            raise TaskWorktreeError(f"unsupported task-worktree metadata schema: {path}")
        try:
            record = cls(
                name=value["name"],
                source_repo=Path(value["source_repo"]).resolve(),
                source_work_root=Path(value["source_work_root"]).resolve(),
                worktree=Path(value["worktree"]).resolve(),
                work_root=Path(value["work_root"]).resolve(),
                export_root=Path(value["export_root"]).resolve(),
                common_git_dir=Path(value["common_git_dir"]).resolve(),
                base_commit=value["base_commit"],
                created_at=value["created_at"],
            )
        except (KeyError, TypeError, ValueError) as exc:
            raise TaskWorktreeError(f"invalid task-worktree metadata: {path}") from exc
        record.validate()
        if not _same(path, record.path):
            raise TaskWorktreeError(
                f"task-worktree metadata path disagrees with its work root: {path}"
            )
        return record

    def validate(self) -> None:
        validate_task_name(self.name)
        expected_root = self.source_work_root / "worktrees" / self.name
        if not _same(self.work_root, expected_root):
            raise TaskWorktreeError(
                f"task work root must be {expected_root}: {self.work_root}"
            )
        if not _same(self.export_root, self.work_root / "exports"):
            raise TaskWorktreeError(
                f"task export root must be work/exports: {self.export_root}"
            )
        for protected in (self.source_repo, self.source_work_root):
            if _same(self.worktree, protected) or _within(self.worktree, protected):
                raise TaskWorktreeError(
                    f"task worktree overlaps a protected root: {self.worktree}"
                )


@dataclass(frozen=True, slots=True)
class TaskWorktreeCreation:
    record: TaskWorktreeRecord
    source_dirty: bool


def _marker_record_path(worktree: Path) -> Path | None:
    marker = worktree.resolve() / TASK_MARKER_FILE
    if not marker.is_file():
        return None
    try:
        value = json.loads(marker.read_text(encoding="utf-8-sig"))
        path = value["record"]
    except (OSError, ValueError, KeyError, TypeError) as exc:
        raise TaskWorktreeError(f"cannot read task-worktree marker: {marker}") from exc
    return Path(path).resolve()


def current_task_worktree(
    repo_root: Path, work_root: Path | None = None
) -> TaskWorktreeRecord | None:
    record_path = _marker_record_path(repo_root)
    if record_path is None:
        return None
    record = TaskWorktreeRecord.load(record_path)
    if not _same(record.worktree, repo_root):
        raise TaskWorktreeError(
            f"task marker belongs to {record.worktree}, not this checkout {repo_root.resolve()}"
        )
    if work_root is not None and not _same(record.work_root, work_root):
        raise TaskWorktreeError(
            f"task {record.name!r} owns work root {record.work_root}, but configuration "
            f"resolved {work_root.resolve()}"
        )
    return record


def task_worktree_records(
    repo_root: Path, work_root: Path, name: str | None = None
) -> tuple[TaskWorktreeRecord, ...]:
    current = current_task_worktree(repo_root, work_root)
    if current is not None:
        if name is not None and current.name != name:
            raise TaskWorktreeError(
                f"this checkout is task {current.name!r}, not {name!r}"
            )
        return (current,)
    root = work_root.resolve() / "worktrees"
    if name is not None:
        path = root / validate_task_name(name) / TASK_RECORD_FILE
        if not path.is_file():
            raise TaskWorktreeError(f"task {name!r} does not exist below {root}")
        return (TaskWorktreeRecord.load(path),)
    if not root.is_dir():
        return ()
    return tuple(
        TaskWorktreeRecord.load(path)
        for path in sorted(root.glob(f"*/{TASK_RECORD_FILE}"))
    )


def create_task_worktree(
    *,
    source_repo: Path,
    source_work_root: Path,
    game_root: Path,
    ue_root: Path,
    runner: ProcessRunner,
    name: str,
    ref: str = "HEAD",
    worktree_path: Path | None = None,
) -> TaskWorktreeCreation:
    name = validate_task_name(name)
    source = source_repo.resolve()
    source_work = source_work_root.resolve()
    if _marker_record_path(source) is not None:
        raise TaskWorktreeError("create task worktrees from the primary main checkout")
    branch = _git(runner, source, "symbolic-ref", "--quiet", "--short", "HEAD")
    if branch != "main":
        raise TaskWorktreeError(
            f"create task worktrees from main; current branch is {branch or 'detached HEAD'}"
        )

    task_root = (source_work / "worktrees" / name).resolve()
    export_root = task_root / "exports"
    worktree = (
        worktree_path.resolve()
        if worktree_path is not None
        else (source.parent / f"{source.name}-task-{name}").resolve()
    )
    for protected, label in (
        (source, "source checkout"),
        (source_work, "work root"),
        (game_root.resolve(), "VtMB installation"),
        (ue_root.resolve(), "Unreal installation"),
    ):
        if _same(worktree, protected) or _within(worktree, protected):
            raise TaskWorktreeError(
                f"task worktree cannot be inside the {label}: {worktree}"
            )
    if worktree.exists():
        raise TaskWorktreeError(f"task worktree path already exists: {worktree}")
    if not worktree.parent.is_dir():
        raise TaskWorktreeError(
            f"task worktree parent does not exist: {worktree.parent}"
        )
    if task_root.exists():
        raise TaskWorktreeError(f"task work root already exists: {task_root}")

    common = _common_git_dir(runner, source)
    candidate = _commit(runner, source, ref)
    _assert_task_runtime(runner, source, candidate)
    source_dirty = bool(_dirty_rows(runner, source))
    created_worktree = False
    created_task_root = False
    try:
        _git(runner, source, "worktree", "add", "--detach", worktree, candidate)
        created_worktree = True
        task_root.mkdir(parents=True)
        created_task_root = True
        adopt_export_root(export_root, task_root)
        mark_incomplete(export_root)
        record = TaskWorktreeRecord(
            name=name,
            source_repo=source,
            source_work_root=source_work,
            worktree=worktree,
            work_root=task_root,
            export_root=export_root,
            common_git_dir=common,
            base_commit=candidate,
            created_at=_now(),
        )
        record.save()
        _write_json_atomic(
            worktree / TASK_MARKER_FILE,
            {"schema": TASK_SCHEMA, "record": str(record.path)},
        )
        (worktree / ".elysium.local.env").write_text(
            "\n".join(
                (
                    "# Generated by `uv run elysium worktree create`; this checkout owns one task.",
                    f"ELYSIUM_TASK_WORKTREE={name}",
                    f"ELYSIUM_UE_ROOT={_quoted_environment_value(ue_root)}",
                    f"ELYSIUM_VTMB_ROOT={_quoted_environment_value(game_root)}",
                    f"ELYSIUM_WORK_ROOT={_quoted_environment_value(task_root)}",
                    f"ELYSIUM_EXPORT_ROOT={_quoted_environment_value(export_root)}",
                    "",
                )
            ),
            encoding="utf-8",
            newline="\n",
        )
        return TaskWorktreeCreation(record=record, source_dirty=source_dirty)
    except Exception as exc:
        cleanup_errors: list[str] = []
        if created_worktree:
            try:
                _git(runner, source, "worktree", "remove", "--force", worktree)
            except Exception as cleanup_exc:
                cleanup_errors.append(str(cleanup_exc))
        if created_task_root and task_root.is_dir() and _within(
            task_root, source_work / "worktrees"
        ):
            try:
                shutil.rmtree(task_root)
            except OSError as cleanup_exc:
                cleanup_errors.append(str(cleanup_exc))
        if cleanup_errors:
            raise TaskWorktreeError(
                f"task-worktree creation failed and cleanup also failed: "
                f"{'; '.join(cleanup_errors)}"
            ) from exc
        raise


def assert_task_worktree_runnable(
    record: TaskWorktreeRecord, runner: ProcessRunner
) -> str:
    if not record.worktree.is_dir():
        raise TaskWorktreeError(f"task worktree is missing: {record.worktree}")
    common = _common_git_dir(runner, record.worktree)
    if not _same(common, record.common_git_dir):
        raise TaskWorktreeError(
            f"task worktree belongs to a different Git repository: {record.worktree}"
        )
    return _commit(runner, record.worktree, "HEAD")


def assert_primary_operation(
    repo_root: Path, work_root: Path | None, operation: str
) -> None:
    record = current_task_worktree(repo_root, work_root)
    if record is None:
        return
    raise TaskWorktreeError(
        f"{operation} is primary-checkout-only; task worktree {record.name!r} may build "
        "and run focused tests, but live game, editor, debug, export, bake, and authored "
        f"asset operations run from {record.source_repo}"
    )


def _unlanded_commits(
    record: TaskWorktreeRecord, runner: ProcessRunner, head: str
) -> tuple[str, ...]:
    ancestor = runner.run(
        [
            "git",
            "-C",
            record.worktree,
            "merge-base",
            "--is-ancestor",
            record.base_commit,
            head,
        ],
        cwd=record.worktree,
    )
    if ancestor.returncode:
        raise TaskWorktreeError(
            f"task {record.name!r} HEAD is not descended from its base "
            f"{record.base_commit[:12]}"
        )
    merges = _git(
        runner,
        record.worktree,
        "rev-list",
        "--merges",
        f"{record.base_commit}..{head}",
    )
    if merges:
        raise TaskWorktreeError(
            f"task {record.name!r} contains merge commits; land it manually before cleanup"
        )
    output = _git(
        runner,
        record.source_repo,
        "cherry",
        "refs/heads/main",
        head,
        record.base_commit,
    )
    return tuple(
        line.split(maxsplit=2)[1]
        for line in output.splitlines()
        if line.startswith("+") and len(line.split(maxsplit=2)) >= 2
    )


def task_worktree_status(
    record: TaskWorktreeRecord, runner: ProcessRunner
) -> dict[str, Any]:
    if not record.worktree.is_dir():
        return {
            "name": record.name,
            "worktree": str(record.worktree),
            "work_root": str(record.work_root),
            "base_commit": record.base_commit,
            "missing": True,
            "dirty": [],
            "head": None,
            "unlanded_commits": [],
            "active": active_lease(record.export_root),
        }
    head = assert_task_worktree_runnable(record, runner)
    dirty = _dirty_rows(runner, record.worktree)
    unlanded = _unlanded_commits(record, runner, head)
    return {
        "name": record.name,
        "worktree": str(record.worktree),
        "work_root": str(record.work_root),
        "export_root": str(record.export_root),
        "base_commit": record.base_commit,
        "head": head,
        "missing": False,
        "dirty": list(dirty),
        "unlanded_commits": list(unlanded),
        "active": active_lease(record.export_root),
    }


def close_task_worktree(
    record: TaskWorktreeRecord, runner: ProcessRunner
) -> None:
    with WorkspaceLease(
        record.export_root,
        "worktree close",
        record.worktree,
        metadata={"task_worktree": record.name},
    ):
        assert_project_idle(record.worktree / "ElysiumUE.uproject")
        head = assert_task_worktree_runnable(record, runner)
        dirty = _dirty_rows(runner, record.worktree)
        if dirty:
            raise TaskWorktreeError(
                f"task {record.name!r} has uncommitted changes; first entry: {dirty[0]}"
            )
        unlanded = _unlanded_commits(record, runner, head)
        if unlanded:
            raise TaskWorktreeError(
                f"task {record.name!r} has {len(unlanded)} commit(s) not present on main; "
                f"first commit: {unlanded[0][:12]}"
            )
        _git(runner, record.source_repo, "worktree", "remove", record.worktree)

    expected = record.source_work_root / "worktrees" / record.name
    if not _same(record.work_root, expected) or not _within(
        record.work_root, record.source_work_root / "worktrees"
    ):
        raise TaskWorktreeError(
            f"refusing to remove unexpected task work root: {record.work_root}"
        )
    try:
        shutil.rmtree(record.work_root)
    except OSError as exc:
        raise TaskWorktreeError(
            f"task checkout closed but generated work root could not be removed: "
            f"{record.work_root}: {exc}"
        ) from exc
