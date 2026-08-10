"""Detached Git worktrees for isolated bake, validation, and live-QA lanes."""

from __future__ import annotations

from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import tempfile
from typing import Any

import psutil

from elysium_pipeline.clean import adopt_export_root, incomplete_domains, mark_incomplete
from elysium_pipeline.process import ProcessRunner
from elysium_pipeline.workspace_lock import WorkspaceLease, active_lease


LANE_FILE = ".elysium-lane.json"
LANE_SCHEMA = 1
EVIDENCE_STATES = ("pending", "passed", "failed")
_LANE_NAME = re.compile(r"^[a-z][a-z0-9-]{0,31}$")
_UNREAL_PROCESSES = {
    "livecodingconsole.exe",
    "unrealeditor.exe",
    "unrealeditor-cmd.exe",
}
_LANE_RUNTIME_PATHS = (
    "pipeline/src/elysium_pipeline/lanes.py",
    "pipeline/src/elysium_pipeline/workspace_lock.py",
)


class LaneError(RuntimeError):
    """A lane lifecycle or immutable-candidate contract was violated."""


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
        raise LaneError(
            f"git {' '.join(map(os.fspath, arguments))} failed"
            + (f": {detail[-1]}" if detail else "")
        )
    return result.output.strip()


def _common_git_dir(runner: ProcessRunner, repository: Path) -> Path:
    raw = Path(_git(runner, repository, "rev-parse", "--git-common-dir"))
    return (raw if raw.is_absolute() else repository / raw).resolve()


def _commit(runner: ProcessRunner, repository: Path, ref: str) -> str:
    return _git(runner, repository, "rev-parse", "--verify", f"{ref}^{{commit}}")


def _assert_lane_runtime(runner: ProcessRunner, repository: Path, commit: str) -> None:
    for relative in _LANE_RUNTIME_PATHS:
        try:
            _git(runner, repository, "cat-file", "-e", f"{commit}:{relative}")
        except LaneError as exc:
            raise LaneError(
                f"candidate {commit[:12]} predates the lane runtime ({relative}); "
                "commit the lane tooling before creating or dispatching a QA lane"
            ) from exc


def _dirty_rows(runner: ProcessRunner, repository: Path) -> tuple[str, ...]:
    output = _git(
        runner,
        repository,
        "status",
        "--porcelain",
        "--untracked-files=all",
    )
    return tuple(line for line in output.splitlines() if line)


def repository_has_changes(repository: Path, runner: ProcessRunner) -> bool:
    return bool(_dirty_rows(runner, repository.resolve()))


def _quoted_environment_value(path: Path) -> str:
    value = str(path.resolve())
    if '"' in value:
        raise LaneError(f"path cannot be written to the local environment file: {value}")
    return f'"{value}"'


@dataclass(slots=True)
class LaneRecord:
    name: str
    source_repo: Path
    worktree: Path
    work_root: Path
    export_root: Path
    common_git_dir: Path
    candidate_commit: str
    created_at: str
    dispatched_at: str
    automated: str = "pending"
    live: str = "pending"
    build_commit: str | None = None
    note: str = ""
    last_run: dict[str, Any] | None = None
    last_automation_run: dict[str, Any] | None = None
    last_live_run: dict[str, Any] | None = None
    snapshot: dict[str, Any] | None = None
    schema: int = field(default=LANE_SCHEMA, init=False)

    @property
    def path(self) -> Path:
        return self.work_root / LANE_FILE

    def to_dict(self) -> dict[str, Any]:
        value = asdict(self)
        for name in (
            "source_repo",
            "worktree",
            "work_root",
            "export_root",
            "common_git_dir",
        ):
            value[name] = str(value[name])
        return value

    def save(self) -> None:
        _write_json_atomic(self.path, self.to_dict())

    @classmethod
    def load(cls, path: Path) -> "LaneRecord":
        try:
            value = json.loads(path.read_text(encoding="utf-8-sig"))
        except (OSError, ValueError) as exc:
            raise LaneError(f"cannot read lane metadata: {path}") from exc
        if value.get("schema") != LANE_SCHEMA:
            raise LaneError(f"unsupported lane metadata schema: {path}")
        try:
            record = cls(
                name=value["name"],
                source_repo=Path(value["source_repo"]).resolve(),
                worktree=Path(value["worktree"]).resolve(),
                work_root=Path(value["work_root"]).resolve(),
                export_root=Path(value["export_root"]).resolve(),
                common_git_dir=Path(value["common_git_dir"]).resolve(),
                candidate_commit=value["candidate_commit"],
                created_at=value["created_at"],
                dispatched_at=value["dispatched_at"],
                automated=value.get("automated", "pending"),
                live=value.get("live", "pending"),
                build_commit=value.get("build_commit"),
                note=value.get("note", ""),
                last_run=value.get("last_run"),
                last_automation_run=value.get("last_automation_run"),
                last_live_run=value.get("last_live_run"),
                snapshot=value.get("snapshot"),
            )
        except (KeyError, TypeError, ValueError) as exc:
            raise LaneError(f"invalid lane metadata: {path}") from exc
        record.validate()
        if not _same(path, record.path):
            raise LaneError(
                f"lane metadata path disagrees with its declared work root: {path}"
            )
        return record

    def validate(self) -> None:
        validate_lane_name(self.name)
        if not _same(self.export_root, self.work_root / "exports"):
            raise LaneError(f"lane export root must be work/exports: {self.export_root}")
        if self.automated not in EVIDENCE_STATES or self.live not in EVIDENCE_STATES:
            raise LaneError(f"invalid evidence state in lane metadata: {self.path}")


def validate_lane_name(name: str) -> str:
    if not _LANE_NAME.fullmatch(name):
        raise LaneError(
            "lane name must start with a lowercase letter and contain only "
            "lowercase letters, digits, or hyphens (32 characters maximum)"
        )
    if name == "main":
        raise LaneError("'main' is the development checkout, not a QA lane name")
    return name


def current_lane(work_root: Path | None, repo_root: Path) -> LaneRecord | None:
    if work_root is None:
        return None
    path = work_root.resolve() / LANE_FILE
    if not path.is_file():
        return None
    record = LaneRecord.load(path)
    if not _same(record.worktree, repo_root):
        raise LaneError(
            f"lane work root belongs to {record.worktree}, not this checkout {repo_root.resolve()}"
        )
    return record


def lane_records(work_root: Path, name: str | None = None) -> tuple[LaneRecord, ...]:
    root = work_root.resolve()
    own = root / LANE_FILE
    if own.is_file():
        record = LaneRecord.load(own)
        if name is not None and record.name != name:
            raise LaneError(f"this checkout is lane {record.name!r}, not {name!r}")
        return (record,)
    if name is not None:
        path = root / "lanes" / validate_lane_name(name) / LANE_FILE
        if not path.is_file():
            raise LaneError(f"lane {name!r} does not exist below {root / 'lanes'}")
        return (LaneRecord.load(path),)
    lanes_root = root / "lanes"
    if not lanes_root.is_dir():
        return ()
    return tuple(
        LaneRecord.load(path)
        for path in sorted(lanes_root.glob(f"*/{LANE_FILE}"))
    )


@dataclass(frozen=True, slots=True)
class LaneCreation:
    record: LaneRecord
    source_dirty: bool


def create_lane(
    *,
    source_repo: Path,
    source_work_root: Path,
    game_root: Path,
    ue_root: Path,
    runner: ProcessRunner,
    name: str,
    ref: str = "HEAD",
    worktree_path: Path | None = None,
) -> LaneCreation:
    name = validate_lane_name(name)
    source = source_repo.resolve()
    source_work = source_work_root.resolve()
    if (source_work / LANE_FILE).is_file():
        raise LaneError("create lanes from the primary development checkout")
    lane_root = (source_work / "lanes" / name).resolve()
    export_root = lane_root / "exports"
    worktree = (
        worktree_path.resolve()
        if worktree_path is not None
        else (source.parent / f"{source.name}-{name}").resolve()
    )
    for forbidden, label in (
        (source, "source checkout"),
        (source_work, "work root"),
        (game_root.resolve(), "VtMB installation"),
        (ue_root.resolve(), "Unreal installation"),
    ):
        if _same(worktree, forbidden) or _within(worktree, forbidden):
            raise LaneError(f"lane worktree cannot be inside the {label}: {worktree}")
    if worktree.exists():
        raise LaneError(f"lane worktree path already exists: {worktree}")
    if not worktree.parent.is_dir():
        raise LaneError(f"lane worktree parent does not exist: {worktree.parent}")
    if lane_root.exists():
        raise LaneError(f"lane work root already exists: {lane_root}")

    common = _common_git_dir(runner, source)
    candidate = _commit(runner, source, ref)
    _assert_lane_runtime(runner, source, candidate)
    source_dirty = bool(_dirty_rows(runner, source))
    created_worktree = False
    created_lane_root = False
    try:
        _git(runner, source, "worktree", "add", "--detach", worktree, candidate)
        created_worktree = True
        lane_root.mkdir(parents=True)
        created_lane_root = True
        adopt_export_root(export_root, lane_root)
        mark_incomplete(export_root)
        (worktree / ".elysium.local.env").write_text(
            "\n".join(
                (
                    "# Generated by `uv run elysium lane create`; this checkout owns one QA lane.",
                    f"ELYSIUM_LANE={name}",
                    f"ELYSIUM_UE_ROOT={_quoted_environment_value(ue_root)}",
                    f"ELYSIUM_VTMB_ROOT={_quoted_environment_value(game_root)}",
                    f"ELYSIUM_WORK_ROOT={_quoted_environment_value(lane_root)}",
                    f"ELYSIUM_EXPORT_ROOT={_quoted_environment_value(export_root)}",
                    "",
                )
            ),
            encoding="utf-8",
            newline="\n",
        )
        now = _now()
        record = LaneRecord(
            name=name,
            source_repo=source,
            worktree=worktree,
            work_root=lane_root,
            export_root=export_root,
            common_git_dir=common,
            candidate_commit=candidate,
            created_at=now,
            dispatched_at=now,
        )
        record.save()
        return LaneCreation(record=record, source_dirty=source_dirty)
    except Exception:
        if created_worktree:
            try:
                _git(runner, source, "worktree", "remove", "--force", worktree)
            except Exception:
                pass
        if created_lane_root and lane_root.is_dir() and _within(lane_root, source_work / "lanes"):
            shutil.rmtree(lane_root)
        raise


def active_unreal_processes(project: Path) -> tuple[dict[str, Any], ...]:
    needle = os.path.normcase(str(project.resolve()))
    found: list[dict[str, Any]] = []
    for process in psutil.process_iter(("pid", "name", "cmdline")):
        try:
            name = str(process.info.get("name") or "").lower()
            if name not in _UNREAL_PROCESSES:
                continue
            command = " ".join(process.info.get("cmdline") or ())
            if needle not in os.path.normcase(command):
                continue
            found.append({"pid": process.pid, "name": process.info.get("name") or name})
        except (psutil.AccessDenied, psutil.NoSuchProcess, OSError):
            continue
    return tuple(found)


def assert_lane_runnable(record: LaneRecord, runner: ProcessRunner) -> str:
    if not record.worktree.is_dir():
        raise LaneError(f"lane worktree is missing: {record.worktree}")
    common = _common_git_dir(runner, record.worktree)
    if not _same(common, record.common_git_dir):
        raise LaneError(f"lane worktree belongs to a different Git repository: {record.worktree}")
    actual = _commit(runner, record.worktree, "HEAD")
    if actual != record.candidate_commit:
        raise LaneError(
            f"lane {record.name!r} is at {actual[:12]}, but its dispatched candidate is "
            f"{record.candidate_commit[:12]}; run `uv run elysium lane dispatch {record.name}`"
        )
    dirty = _dirty_rows(runner, record.worktree)
    if dirty:
        raise LaneError(
            f"lane {record.name!r} has tracked or untracked source changes; "
            f"first entry: {dirty[0]}"
        )
    return actual


def assert_project_idle(project: Path) -> None:
    processes = active_unreal_processes(project)
    if not processes:
        return
    detail = ", ".join(f"{item['name']} pid {item['pid']}" for item in processes)
    raise LaneError(f"Unreal still has this project open: {detail}")


def dispatch_lane(record: LaneRecord, runner: ProcessRunner, ref: str = "HEAD") -> LaneRecord:
    with WorkspaceLease(
        record.export_root,
        "lane dispatch",
        record.worktree,
        metadata={"lane": record.name},
    ):
        assert_project_idle(record.worktree / "ElysiumUE.uproject")
        if _dirty_rows(runner, record.worktree):
            raise LaneError(f"lane {record.name!r} has source changes; refusing to switch it")
        common = _common_git_dir(runner, record.worktree)
        if not _same(common, record.common_git_dir):
            raise LaneError(
                f"lane worktree belongs to a different Git repository: {record.worktree}"
            )
        candidate = _commit(runner, record.source_repo, ref)
        _assert_lane_runtime(runner, record.source_repo, candidate)
        _git(runner, record.worktree, "switch", "--detach", candidate)
        record.candidate_commit = candidate
        record.dispatched_at = _now()
        record.automated = "pending"
        record.live = "pending"
        record.build_commit = None
        record.note = ""
        record.last_run = None
        record.last_automation_run = None
        record.last_live_run = None
        record.snapshot = None
        record.save()
    return record


def _sha256_file(path: Path) -> str | None:
    if not path.is_file():
        return None
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while chunk := handle.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def _receipt_snapshot(root: Path) -> tuple[int, str | None]:
    if not root.is_dir():
        return 0, None
    paths = sorted(path for path in root.rglob("*.json") if path.is_file())
    digest = hashlib.sha256()
    for path in paths:
        digest.update(path.relative_to(root).as_posix().encode("utf-8"))
        digest.update(b"\0")
        value = _sha256_file(path)
        if value is not None:
            digest.update(value.encode("ascii"))
        digest.update(b"\0")
    return len(paths), digest.hexdigest() if paths else None


def capture_snapshot(record: LaneRecord, runner: ProcessRunner) -> dict[str, Any]:
    commit = assert_lane_runnable(record, runner)
    if record.build_commit != commit:
        raise LaneError(
            f"lane {record.name!r} has no successful disk build for candidate {commit[:12]}"
        )
    receipts, receipt_digest = _receipt_snapshot(
        record.export_root / ".elysium-bake-assets"
    )
    baked = record.worktree / "Plugins" / "ElysiumBaked" / "Content"
    baked_maps = sum(1 for path in baked.rglob("*.umap")) if baked.is_dir() else 0
    build = record.worktree / "Binaries" / "Win64" / "UnrealEditor-ElysiumUE.dll"
    return {
        "captured_at": _now(),
        "commit": commit,
        "export_manifest_sha256": _sha256_file(
            record.export_root / ".elysium-manifest.json"
        ),
        "bake_receipt_count": receipts,
        "bake_receipts_sha256": receipt_digest,
        "baked_map_count": baked_maps,
        "editor_module_sha256": _sha256_file(build),
        "incomplete_domains": list(incomplete_domains(record.export_root)),
    }


def mark_lane(
    record: LaneRecord,
    runner: ProcessRunner,
    *,
    automated: str | None = None,
    live: str | None = None,
    note: str | None = None,
) -> LaneRecord:
    if automated is None and live is None and note is None:
        raise LaneError("name at least one of --automated, --live, or --note")
    for name, value in (("automated", automated), ("live", live)):
        if value is not None and value not in EVIDENCE_STATES:
            raise LaneError(f"{name} must be one of: {', '.join(EVIDENCE_STATES)}")
    if automated == "passed":
        run = record.last_automation_run or {}
        if (
            run.get("status") != "succeeded"
            or run.get("candidate_commit") != record.candidate_commit
        ):
            raise LaneError(
                "automated passed needs a successful test, verification, or reconstruction "
                "run for this candidate"
            )
    if live == "passed":
        run = record.last_live_run or {}
        if (
            run.get("status") != "succeeded"
            or run.get("candidate_commit") != record.candidate_commit
        ):
            raise LaneError(
                "live passed needs a successful managed run or debug session for this candidate"
            )
    with WorkspaceLease(
        record.export_root,
        "lane mark",
        record.worktree,
        metadata={"lane": record.name},
    ):
        assert_project_idle(record.worktree / "ElysiumUE.uproject")
        record.snapshot = capture_snapshot(record, runner)
        if automated is not None:
            record.automated = automated
        if live is not None:
            record.live = live
        if note is not None:
            record.note = note.strip()
        record.save()
    return record


def record_run(
    record: LaneRecord,
    *,
    command: str,
    status: str,
    exit_code: int,
    started_at: str,
    report_path: Path | None,
) -> None:
    current = LaneRecord.load(record.path)
    result = {
        "command": command,
        "status": status,
        "exit_code": exit_code,
        "started_at": started_at,
        "completed_at": _now(),
        "candidate_commit": current.candidate_commit,
        "report": str(report_path.resolve()) if report_path is not None else None,
    }
    current.last_run = result
    if command == "test" or command == "reconstruct" or command.startswith("verify "):
        current.last_automation_run = result
    if command.startswith("run ") or command.startswith("debug "):
        current.last_live_run = result
    build = current.worktree / "Binaries" / "Win64" / "UnrealEditor-ElysiumUE.dll"
    if status == "succeeded" and command in {"build", "reconstruct"} and build.is_file():
        current.build_commit = current.candidate_commit
    current.save()


def begin_activity(record: LaneRecord, command: str) -> None:
    """Invalidate evidence before a command can change what the candidate proves."""

    current = LaneRecord.load(record.path)
    reset_both = (
        command == "deps sync"
        or command == "build"
        or command == "reconstruct"
        or command.startswith("export ")
    )
    reset_automated = command == "test" or command.startswith("verify ")
    reset_live = command.startswith("run ") or command.startswith("debug ")
    if not (reset_both or reset_automated or reset_live):
        return
    if reset_both:
        current.automated = "pending"
        current.live = "pending"
        current.last_automation_run = None
        current.last_live_run = None
    if reset_automated:
        current.automated = "pending"
        current.last_automation_run = None
    if reset_live:
        current.live = "pending"
        current.last_live_run = None
    if command in {"deps sync", "build", "reconstruct"}:
        current.build_commit = None
    current.snapshot = None
    current.save()


def lane_status(record: LaneRecord, runner: ProcessRunner) -> dict[str, Any]:
    head = branch = None
    dirty: tuple[str, ...] = ()
    error = None
    try:
        head = _commit(runner, record.worktree, "HEAD")
        branch = _git(runner, record.worktree, "rev-parse", "--abbrev-ref", "HEAD")
        dirty = _dirty_rows(runner, record.worktree)
    except LaneError as exc:
        error = str(exc)
    build = record.worktree / "Binaries" / "Win64" / "UnrealEditor-ElysiumUE.dll"
    baked = record.worktree / "Plugins" / "ElysiumBaked" / "Content"
    return {
        "name": record.name,
        "worktree": str(record.worktree),
        "work_root": str(record.work_root),
        "export_root": str(record.export_root),
        "candidate_commit": record.candidate_commit,
        "head": head,
        "branch": branch,
        "candidate_matches": head == record.candidate_commit,
        "dirty": list(dirty),
        "activity": active_lease(record.export_root),
        "unreal_processes": list(
            active_unreal_processes(record.worktree / "ElysiumUE.uproject")
        ),
        "build_ready": build.is_file() and record.build_commit == record.candidate_commit,
        "build_commit": record.build_commit,
        "baked_map_count": (
            sum(1 for path in baked.rglob("*.umap")) if baked.is_dir() else 0
        ),
        "incomplete_domains": list(incomplete_domains(record.export_root)),
        "automated": record.automated,
        "live": record.live,
        "note": record.note,
        "last_run": record.last_run,
        "snapshot": record.snapshot,
        "error": error,
    }
