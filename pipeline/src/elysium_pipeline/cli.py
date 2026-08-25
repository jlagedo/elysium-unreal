"""One public command surface for the Elysium project."""

from __future__ import annotations

from contextlib import nullcontext
from dataclasses import dataclass
from datetime import datetime, timezone
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import threading
import time
from typing import Any, Callable

import typer
from rich.console import Console

from elysium_pipeline import task_worktrees
from elysium_pipeline.config import (
    ConfigError,
    ProjectConfig,
    default_build_parallelism,
    validate_engine_root,
)
from elysium_pipeline.dependencies import (
    DependencyError,
    check_dependencies,
    load_project_lock,
    sync_dependencies,
)
from elysium_pipeline.process import ProcessFailure, ProcessRunner
from elysium_pipeline.reporting import ExitCode, RunReport
from elysium_pipeline.workspace_lock import WorkspaceLease, assert_project_idle


console = Console()
PASSTHROUGH = {"allow_extra_args": True, "ignore_unknown_options": True}
app = typer.Typer(
    name="elysium",
    help="Build, reconstruct, export, validate, and run Elysium-Unreal.",
    no_args_is_help=True,
    pretty_exceptions_show_locals=False,
)
deps_app = typer.Typer(help="Restore and verify locked project dependencies.")
export_app = typer.Typer(help="Export VtMB sources and generate Unreal packages.")
verify_app = typer.Typer(help="Check baked packages against what the export declares.")
run_app = typer.Typer(help="Launch the Unreal editor or standalone game.")
debug_app = typer.Typer(help="Run development and acceptance harnesses.")
ide_app = typer.Typer(help="Configure supported development environments.")
worktree_app = typer.Typer(help="Manage mutable, isolated development-task worktrees.")
app.add_typer(deps_app, name="deps")
app.add_typer(export_app, name="export")
app.add_typer(verify_app, name="verify")
app.add_typer(run_app, name="run")
app.add_typer(debug_app, name="debug")
app.add_typer(ide_app, name="ide")
app.add_typer(worktree_app, name="worktree")


@dataclass(slots=True)
class CliState:
    game: Path | None
    work: Path | None
    ue: Path | None

    def resolve(
        self,
        *,
        require_game: bool = False,
        require_work: bool = False,
        require_ue: bool = False,
    ) -> ProjectConfig:
        config = ProjectConfig.resolve(
            self.game,
            self.work,
            self.ue,
            require_game=require_game,
            require_work=require_work,
            require_ue=require_ue,
        )
        config.apply_environment()
        return config


@app.callback()
def root(
    ctx: typer.Context,
    game: Path | None = typer.Option(
        None, "--game", help="Read-only VtMB installation root."
    ),
    work: Path | None = typer.Option(
        None, "--work", help="External generated-output and cache root."
    ),
    ue: Path | None = typer.Option(
        None, "--ue", help="Unreal Engine 5.8 installation root."
    ),
) -> None:
    ctx.obj = CliState(game=game, work=work, ue=ue)


def _state(ctx: typer.Context) -> CliState:
    if not isinstance(ctx.obj, CliState):
        raise RuntimeError("CLI state was not initialized")
    return ctx.obj


#: What an export/verify command's child processes may say on the console. Editor
#: commandlets stream thousands of engine lines; the run log keeps every one, the
#: console keeps the signal -- our own script output (LogPython) and any engine
#: warning or error.
_CHILD_SIGNAL = re.compile(
    r"LogPython|Fatal error|Assertion failed|: Error:|: Warning:|^Error:")

#: ``_CHILD_SIGNAL``'s literal alternatives as plain substrings; ``^Error:``
#: anchors to the line start, so ``startswith`` carries it below. Ordinary
#: engine lines fail every substring test and never reach the regex.
_CHILD_SIGNAL_LITERALS = (
    "LogPython", "Fatal error", "Assertion failed", ": Error:", ": Warning:")


def _child_signal(line: str) -> bool:
    """Whether a child line is console signal; substring checks gate the regex."""
    if not line.startswith("Error:") and not any(
            literal in line for literal in _CHILD_SIGNAL_LITERALS):
        return False
    return _CHILD_SIGNAL.search(line) is not None


class _ChildEcho:
    """The filtered console echo for export/verify children, with a live status.

    Signal lines (our scripts' LogPython output, engine warnings/errors) print
    normally. Everything else feeds a single rewritten status line -- elapsed
    time, how many log lines have streamed, and the most recent one -- redrawn
    by a ticker so a quiet editor phase never looks stalled. Terminal only;
    redirected output gets the signal lines and nothing else."""

    def __init__(self):
        self._tty = bool(getattr(sys.stdout, "isatty", lambda: False)())
        self._lock = threading.Lock()
        self._started = time.monotonic()
        self._count = 0
        self._last = ""
        self._status_len = 0
        self._stop = threading.Event()
        if self._tty:
            threading.Thread(target=self._tick, daemon=True).start()

    def __call__(self, line: str) -> None:
        if _child_signal(line):
            with self._lock:
                self._clear()
                console.print(line, markup=False)
                self._draw()
            return
        with self._lock:
            self._count += 1
            self._last = line.strip()

    def _tick(self) -> None:
        while not self._stop.wait(1.0):
            with self._lock:
                self._draw()

    def _draw(self) -> None:
        if not self._tty:
            return
        minutes, seconds = divmod(int(time.monotonic() - self._started), 60)
        text = f"\u00bb editor {minutes:02d}:{seconds:02d} \u00b7 {self._count} log lines"
        if self._last:
            text += " \u00b7 " + self._last
        width = shutil.get_terminal_size(fallback=(120, 25)).columns - 1
        text = text[:width]
        pad = max(0, self._status_len - len(text))
        sys.stdout.write("\r" + text + " " * pad + "\r" + text)
        sys.stdout.flush()
        self._status_len = len(text)

    def _clear(self) -> None:
        if self._status_len:
            sys.stdout.write("\r" + " " * self._status_len + "\r")
            sys.stdout.flush()
            self._status_len = 0

    def close(self) -> None:
        self._stop.set()
        with self._lock:
            self._clear()
            self._tty = False


def _execute(
    state: CliState,
    name: str,
    category: ExitCode,
    action: Callable[[ProjectConfig, ProcessRunner], Any],
    *,
    require_game: bool = False,
    require_work: bool = True,
    require_ue: bool = False,
    quiet_report: bool = False,
    activity: bool = False,
    primary_only: bool = False,
) -> Any:
    report = RunReport(command=name, arguments=sys.argv[1:])
    config: ProjectConfig | None = None
    log_handle = None
    child_echo: _ChildEcho | None = None
    report_path: Path | None = None
    task: task_worktrees.TaskWorktreeRecord | None = None
    started = datetime.now(timezone.utc).isoformat()
    before = time.monotonic()
    try:
        config = state.resolve(
            require_game=require_game,
            require_work=require_work,
            require_ue=require_ue,
        )
        if config.log_root is not None:
            config.log_root.mkdir(parents=True, exist_ok=True)
            stamp = datetime.fromisoformat(report.started_at).strftime(
                "%Y%m%dT%H%M%S.%fZ"
            )
            slug = name.replace(" ", "-").replace("/", "-")
            log_handle = (config.log_root / f"{stamp}-{slug}.log").open(
                "w", encoding="utf-8", newline="\n"
            )
        runner = ProcessRunner(
            cwd=config.repo_root,
            environment=os.environ.copy(),
            log=log_handle,
            output_sink=(
                None
                if log_handle is None
                else (child_echo := _ChildEcho())
                if name.split(" ", 1)[0] in {"export", "verify"}
                else lambda line: console.print(line, markup=False)
            ),
        )
        task = task_worktrees.current_task_worktree(
            config.repo_root, config.work_root
        )
        if task is not None:
            head = task_worktrees.assert_task_worktree_runnable(task, runner)
            report.metadata.update(
                {
                    "task_worktree": task.name,
                    "task_base_commit": task.base_commit,
                    "task_head": head,
                    "worktree": str(task.worktree),
                    "export_root": str(task.export_root),
                }
            )
        if primary_only:
            task_worktrees.assert_primary_operation(
                config.repo_root, config.work_root, name
            )
        lease = nullcontext()
        if activity:
            assert_project_idle(config.project)
            if config.export_root is not None:
                lease = WorkspaceLease(
                    config.export_root,
                    name,
                    config.repo_root,
                    metadata=({"task_worktree": task.name} if task else None),
                )
        with lease:
            result = action(config, runner)
        report.add_task(
            name,
            status="ok",
            started_at=started,
            duration_seconds=time.monotonic() - before,
        )
        report.finish()
        if config.log_root is not None:
            report_path = report.write(config.log_root)
        if report_path and not quiet_report:
            console.print(f"[dim]run report: {report_path}[/dim]")
        return result
    except ConfigError as exc:
        code = int(ExitCode.USAGE_OR_CONFIG)
        detail = str(exc)
    except ProcessFailure as exc:
        code = int(exc.category)
        detail = str(exc)
    except DependencyError as exc:
        code = int(ExitCode.DEPENDENCY_OR_TOOLCHAIN)
        detail = str(exc)
    except typer.Exit:
        raise
    except Exception as exc:
        code = int(getattr(exc, "exit_code", category))
        detail = str(exc) or type(exc).__name__
    finally:
        if child_echo is not None:
            child_echo.close()
        if log_handle is not None:
            log_handle.close()

    report.add_task(
        name,
        status="failed",
        started_at=started,
        duration_seconds=time.monotonic() - before,
        detail=detail,
    )
    report.finish(exit_code=code)
    if config is not None and config.log_root is not None:
        report_path = report.write(config.log_root)
    if not quiet_report:
        console.print("[red]error:[/red] ", end="")
        console.print(detail, markup=False)
    if report_path and not quiet_report:
        console.print(f"[dim]run report: {report_path}[/dim]")
    raise typer.Exit(code)


def _shared_cache_root(config: ProjectConfig) -> Path | None:
    """Downloaded dependency archives belong to the machine, not to one checkout.

    A task worktree owns its own generated state, but a locked archive is byte-identical for
    every checkout and is already addressed by its own hash, so giving each worktree a private
    cache only buys a re-download. The primary work root holds the one copy.
    """

    task = task_worktrees.current_task_worktree(config.repo_root, config.work_root)
    root = task.source_work_root if task is not None else config.work_root
    return None if root is None else root / "cache"


def _claimed_engines(config: ProjectConfig) -> dict[str, str]:
    """Every engine copy a live checkout already builds against, keyed by normalised path."""

    claims: dict[str, str] = {}
    if config.ue_root is not None:
        claims[os.path.normcase(str(config.ue_root))] = "the primary checkout"
    if config.work_root is None:
        return claims
    for task in task_worktrees.task_worktree_records(config.repo_root, config.work_root):
        if task.ue_root is not None and task.worktree.is_dir():
            claims.setdefault(
                os.path.normcase(str(task.ue_root)), f"task worktree {task.name!r}"
            )
    return claims


def _assign_engine(config: ProjectConfig, override: Path | None, owner: str) -> Path:
    """Choose the engine copy a new checkout builds against.

    UnrealBuildTool's single-instance mutex is keyed on its own assembly path, so two
    checkouts pointed at one engine copy wait for each other even though every other piece
    of their state is separate. A dedicated copy is what makes their builds concurrent.
    """

    if config.ue_root is None:
        raise RuntimeError(f"{owner} needs ELYSIUM_UE_ROOT")
    if override is None:
        console.print(
            f"[yellow]warning:[/yellow] no --ue-root given, so {owner} shares "
            f"{config.ue_root} with the primary checkout; their builds serialize on "
            "UnrealBuildTool's global mutex"
        )
        return config.ue_root
    engine = validate_engine_root(override)
    owner_of_engine = _claimed_engines(config).get(os.path.normcase(str(engine)))
    if owner_of_engine is not None:
        raise RuntimeError(
            f"{engine} is already the engine copy for {owner_of_engine}; give "
            f"{owner} its own copy or its builds will serialize on UnrealBuildTool's "
            "global mutex"
        )
    return engine


def _resolve_build_jobs(build_jobs: int | None) -> int | None:
    """The UBT action cap written into a new checkout, or None to leave UBT's heuristic."""

    if build_jobs == 0:
        return None
    return default_build_parallelism() if build_jobs is None else build_jobs


@worktree_app.command("create")
def worktree_create(
    ctx: typer.Context,
    name: str = typer.Argument(..., help="Task name, such as inventory-fix."),
    at: str = typer.Option("HEAD", "--at", help="Commit or ref to detach at."),
    path: Path | None = typer.Option(
        None,
        "--path",
        help="Worktree path; defaults to a sibling named <repo>-task-<name>.",
    ),
    ue_root: Path | None = typer.Option(
        None,
        "--ue-root",
        help="Dedicated engine copy for this task; required for builds that run "
        "concurrently with the primary checkout.",
    ),
    build_jobs: int | None = typer.Option(
        None,
        "--build-jobs",
        min=0,
        help="UnrealBuildTool actions this task may run at once; 0 leaves UBT's own "
        "heuristic in place. Defaults to one share of the machine.",
    ),
) -> None:
    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        if config.game_root is None or config.work_root is None or config.ue_root is None:
            raise RuntimeError("worktree creation needs the game, work, and UE roots")
        engine = _assign_engine(config, ue_root, f"task worktree {name!r}")
        jobs = _resolve_build_jobs(build_jobs)
        creation = task_worktrees.create_task_worktree(
            source_repo=config.repo_root,
            source_work_root=config.work_root,
            game_root=config.game_root,
            ue_root=engine,
            runner=runner,
            name=name,
            ref=at,
            worktree_path=path,
            max_parallel_actions=jobs,
        )
        record = creation.record
        console.print(
            f"task worktree {record.name} created at {record.worktree} "
            f"({record.base_commit[:12]})"
        )
        console.print(f"task work root: {record.work_root}")
        console.print(f"task engine: {record.ue_root}")
        console.print(
            "task build parallelism: "
            + ("UnrealBuildTool default" if jobs is None else f"{jobs} action(s)")
        )
        # A slot that cannot build yet is not a slot. Materializing the locked dependencies
        # here leaves the checkout ready for `build` on its own, and the archives come from
        # the shared cache rather than the network.
        ready = sync_dependencies(record.worktree, cache_root=_shared_cache_root(config))
        console.print(f"task dependencies: {', '.join(ready) or 'none locked'}")
        if creation.source_dirty:
            console.print(
                "[yellow]warning:[/yellow] main has uncommitted changes; "
                "the task worktree contains only the named commit"
            )
        console.print(f"assign the task agent to: {record.worktree}")
        console.print("allowed: incremental build and focused tests")
        console.print("main only: export, bake, editor, play, debug, gr, and mcp")

    _execute(
        _state(ctx),
        "worktree create",
        ExitCode.VALIDATION,
        action,
        require_game=True,
        require_ue=True,
    )


def _print_task_worktree_status(value: dict[str, Any]) -> None:
    active = value["active"]
    activity = "idle" if active is None else (
        f"{active.get('command', 'busy')} (pid {active.get('pid', '?')})"
    )
    console.print(f"{value['name']}: {value['worktree']}")
    if value["missing"] or value["error"]:
        detail = "checkout missing" if value["missing"] else value["error"]
        console.print(f"  [red]{detail}[/red]")
        console.print(f"  work root {value['work_root']}; close to prune it")
        return
    cleanliness = "clean" if not value["dirty"] else f"dirty ({len(value['dirty'])})"
    unlanded = value["unlanded_commits"]
    landing = "landed" if not unlanded else f"{len(unlanded)} commit(s) to land"
    console.print(
        f"  source {value['head'][:12]} from {value['base_commit'][:12]} "
        f"({cleanliness}, {landing})"
    )
    console.print(f"  activity {activity}; work root {value['work_root']}")
    console.print(f"  engine {value['ue_root'] or 'inherited from the primary checkout'}")


@worktree_app.command("status")
def worktree_status_command(
    ctx: typer.Context,
    name: str | None = typer.Argument(None, help="One task; omit to list every task."),
    json_output: bool = typer.Option(False, "--json"),
) -> None:
    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        if config.work_root is None:
            raise RuntimeError("worktree status needs ELYSIUM_WORK_ROOT")
        records = task_worktrees.task_worktree_records(
            config.repo_root, config.work_root, name
        )
        values = [
            task_worktrees.task_worktree_status(record, runner)
            for record in records
        ]
        if json_output:
            typer.echo(json.dumps(values, indent=2, sort_keys=True))
            return
        if not values:
            console.print("no task worktrees")
            return
        for index, value in enumerate(values):
            if index:
                console.print()
            _print_task_worktree_status(value)

    _execute(
        _state(ctx),
        "worktree status",
        ExitCode.VALIDATION,
        action,
        quiet_report=True,
    )


@worktree_app.command("close")
def worktree_close(
    ctx: typer.Context,
    name: str = typer.Argument(..., help="Landed task worktree to remove."),
) -> None:
    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        if config.work_root is None:
            raise RuntimeError("worktree close needs ELYSIUM_WORK_ROOT")
        if task_worktrees.current_task_worktree(config.repo_root, config.work_root):
            raise task_worktrees.TaskWorktreeError(
                "close task worktrees from the primary main checkout"
            )
        records = task_worktrees.task_worktree_records(
            config.repo_root, config.work_root, name
        )
        task_worktrees.close_task_worktree(records[0], runner)
        console.print(f"task worktree {name} closed")

    _execute(
        _state(ctx),
        "worktree close",
        ExitCode.VALIDATION,
        action,
    )




@deps_app.command("sync")
def deps_sync(ctx: typer.Context) -> None:
    def action(config: ProjectConfig, _runner: ProcessRunner) -> None:
        ready = sync_dependencies(config.repo_root, cache_root=_shared_cache_root(config))
        console.print("dependencies ready: " + ", ".join(ready))

    _execute(
        _state(ctx),
        "deps sync",
        ExitCode.DEPENDENCY_OR_TOOLCHAIN,
        action,
        require_work=False,
        activity=True,
    )


@deps_app.command("check")
def deps_check(ctx: typer.Context) -> None:
    def action(config: ProjectConfig, _runner: ProcessRunner) -> None:
        issues = check_dependencies(config.repo_root, load_project_lock(config.repo_root))
        if issues:
            raise DependencyError(
                "; ".join(f"{issue.name}: {issue.detail}" for issue in issues)
            )
        console.print("locked dependencies match")

    _execute(
        _state(ctx),
        "deps check",
        ExitCode.DEPENDENCY_OR_TOOLCHAIN,
        action,
        require_work=False,
    )


def _load_policy(config: ProjectConfig):
    path = config.repo_root / "dev" / "check_repo_policy.py"
    spec = importlib.util.spec_from_file_location("elysium_repo_policy", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load repository policy: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@app.command("doctor")
def doctor(
    ctx: typer.Context,
    repo_only: bool = typer.Option(False, "--repo-only"),
    json_output: bool = typer.Option(False, "--json"),
    history: bool = typer.Option(False, "--history"),
) -> None:
    def action(config: ProjectConfig, _runner: ProcessRunner) -> None:
        policy = _load_policy(config)
        errors, warnings = policy.audit(history=history, repo_only=repo_only)
        if json_output:
            typer.echo(
                json.dumps(
                    {"ok": not errors, "errors": errors, "warnings": warnings},
                    indent=2,
                    sort_keys=True,
                )
            )
        else:
            for warning in warnings:
                console.print(f"[yellow]warning:[/yellow] {warning}")
            for error in errors:
                console.print(f"[red]error:[/red] {error}")
            if not errors:
                console.print(f"repository policy passed ({len(warnings)} warning(s))")
        if errors:
            error = RuntimeError(f"repository policy failed: {len(errors)} error(s)")
            error.exit_code = int(ExitCode.VALIDATION)
            raise error

    _execute(
        _state(ctx),
        "doctor",
        ExitCode.VALIDATION,
        action,
        require_work=not repo_only,
        quiet_report=json_output,
    )


@app.command(
    "build",
    context_settings={"allow_extra_args": True, "ignore_unknown_options": True},
)
def build_command(
    ctx: typer.Context,
    rebuild: bool = typer.Option(False, "--rebuild"),
    clean: bool = typer.Option(False, "--clean"),
    analyze: bool = typer.Option(False, "--analyze"),
) -> None:
    selected = [
        name for name, enabled in (
            ("rebuild", rebuild),
            ("clean", clean),
            ("analyze", analyze),
        ) if enabled
    ]
    if len(selected) > 1:
        raise typer.BadParameter("--rebuild, --clean, and --analyze are mutually exclusive")
    mode = selected[0] if selected else ""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import unreal

        unreal.build(config, runner, mode, ctx.args)

    _execute(
        _state(ctx),
        "build",
        ExitCode.BUILD,
        action,
        require_ue=True,
        activity=True,
    )


#: `--particles` on every command that bakes a map.
PARTICLE_PASS_HELP = (
    "Author each map's Niagara systems during the bake. Off by default: the pass force-deletes Niagara packages the asset compiler may still own, which crashes the editor. A bake without it leaves the mount's existing particle packages alone."
)


def _export_profile_command(
    ctx: typer.Context,
    profile: str,
    *,
    clean: bool,
    force: bool,
    jobs: int | None,
    particles: bool,
    verify: bool,
) -> None:
    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        maps = export_manager.export_profile(
            config,
            runner,
            profile,
            clean=clean,
            force=force,
            jobs=jobs,
            particles=particles,
            verify=verify,
        )
        console.print(f"{profile} export complete: {len(maps)} map(s)")

    _execute(
        _state(ctx),
        f"export {profile}",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        require_ue=True,
        activity=True,
        primary_only=True,
    )


@export_app.command("grid")
def export_grid(
    ctx: typer.Context,
    clean: bool = typer.Option(False, "--clean"),
    force: bool = typer.Option(False, "--force"),
    jobs: int | None = typer.Option(None, "--jobs", min=1),
    particles: bool = typer.Option(False, "--particles", help=PARTICLE_PASS_HELP),
    verify: bool = typer.Option(
        False,
        "--verify",
        help=(
            "Read the baked mount back after the bake (acceptance check). Off by "
            "default: a bake that exits clean and writes its run report is trusted."
        ),
    ),
) -> None:
    _export_profile_command(
        ctx, "grid", clean=clean, force=force, jobs=jobs, particles=particles, verify=verify
    )


@export_app.command("all")
def export_all_command(
    ctx: typer.Context,
    clean: bool = typer.Option(False, "--clean"),
    force: bool = typer.Option(False, "--force"),
    jobs: int | None = typer.Option(None, "--jobs", min=1),
    particles: bool = typer.Option(False, "--particles", help=PARTICLE_PASS_HELP),
    verify: bool = typer.Option(
        False,
        "--verify",
        help=(
            "Read the baked mount back after the bake (acceptance check). Off by "
            "default: a bake that exits clean and writes its run report is trusted."
        ),
    ),
) -> None:
    _export_profile_command(
        ctx, "all", clean=clean, force=force, jobs=jobs, particles=particles, verify=verify
    )


@export_app.command("map")
def export_map(
    ctx: typer.Context,
    maps: list[str] = typer.Argument(...),
    force: bool = typer.Option(False, "--force"),
    intermediate_only: bool = typer.Option(False, "--intermediate-only"),
    particles: bool = typer.Option(False, "--particles", help=PARTICLE_PASS_HELP),
    verify: bool = typer.Option(
        False,
        "--verify",
        help=(
            "Run the deep bake verification commandlet after the bake (acceptance check; "
            "iteration trusts a clean bake exit)."
        ),
    ),
) -> None:
    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        names = export_manager.export_targeted_maps(
            config,
            runner,
            maps,
            force=force,
            intermediate_only=intermediate_only,
            particles=particles,
            verify=verify,
        )
        console.print("map export complete: " + ", ".join(names))

    _execute(
        _state(ctx),
        "export map",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        require_ue=not intermediate_only,
        activity=True,
        primary_only=True,
    )


@export_app.command("characters")
def export_characters(
    ctx: typer.Context,
    models: list[str] = typer.Argument(
        None,
        help="Models to bake: a stem, family:<name> (an alias for that stem), or "
             "bank:<name> (every model that plays the bank). "
             "Omit for the whole cast, which is what the game needs.",
    ),
    force: bool = typer.Option(False, "--force", help="Rewrite every .eskm container."),
    no_sweep: bool = typer.Option(
        False, "--no-sweep", help="Leave assets the declared partition no longer produces."
    ),
    force_sweep: bool = typer.Option(
        False, "--force-sweep", help="Sweep past the safety guard on how much may be removed."
    ),
    verify: bool = typer.Option(
        False,
        "--verify",
        help=(
            "Read the baked mount back after the bake (acceptance check). Off by "
            "default: a bake that exits clean and writes its run report is trusted."
        ),
    ),
) -> None:
    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        stems = export_manager.export_characters(
            config, runner, models, force=force,
            sweep=not no_sweep, force_sweep=force_sweep, verify=verify,
        )
        console.print(f"character bake complete: {len(stems)} model(s)")

    _execute(
        _state(ctx),
        "export characters",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        require_ue=True,
        activity=True,
        primary_only=True,
    )


@export_app.command("wield")
def export_wield(
    ctx: typer.Context,
    stems: list[str] = typer.Argument(
        None,
        help="Wield model stems to bake, e.g. w_m_katana. Omit for the whole corpus.",
    ),
) -> None:
    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        # No export_manager wrapper exists for this bake yet -- `unreal.bake_wield` is called
        # directly, same shape as `unreal.bake_characters`, whose offline pre-decode and partition
        # steps a wield bake has no equivalent of: its manifest is written by `export bundle items`.
        from elysium_pipeline import unreal

        unreal.bake_wield(config, runner, stems or ())
        console.print(
            "wield bake complete: " + (", ".join(stems) if stems else "whole corpus"))

    _execute(
        _state(ctx),
        "export wield",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        require_ue=True,
        activity=True,
        primary_only=True,
    )


@verify_app.command("characters")
def verify_characters(
    ctx: typer.Context,
    models: list[str] = typer.Argument(
        None,
        help="Models to check: a stem, family:<name> (an alias for that stem), or "
             "bank:<name> (every model that plays the bank). Omit for the whole cast.",
    ),
) -> None:
    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        stems = export_manager.verify_characters(config, runner, models)
        console.print(f"character verify complete: {len(stems)} model(s)")

    _execute(
        _state(ctx),
        "verify characters",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        require_ue=True,
        activity=True,
    )


@verify_app.command("maps")
def verify_maps(
    ctx: typer.Context,
    maps: list[str] = typer.Argument(None, help="Maps to check. Omit for every baked level."),
) -> None:
    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        names = export_manager.verify_maps(config, runner, maps)
        console.print(f"map verify complete: {len(names)} map(s)")

    _execute(
        _state(ctx),
        "verify maps",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        require_ue=True,
        activity=True,
    )


@export_app.command("model")
def export_model(
    ctx: typer.Context,
    model: str = typer.Argument(...),
    animation: str | None = typer.Option(None, "--animation"),
    integrate: bool = typer.Option(False, "--integrate"),
) -> None:
    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destination = export_manager.export_model(
            config,
            runner,
            model,
            animation=animation,
            integrate=integrate,
        )
        console.print(f"model export complete: {destination}")

    _execute(
        _state(ctx),
        "export model",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        require_ue=integrate,
        activity=True,
        primary_only=True,
    )


def _corpus_unit(ctx: typer.Context, label: str, **selectors) -> None:
    """Re-decode and re-bake one shared-corpus unit. No map is exported and no `.umap` changes."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        done = export_manager.export_corpus_unit(config, runner, **selectors)
        console.print(
            f"{label} complete: "
            + ", ".join(filter(None, [
                ", ".join(done["models"]),
                ", ".join(done["materials"]),
            ]))
            + " (no map re-baked)"
        )

    _execute(
        _state(ctx),
        label,
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        require_ue=True,
        activity=True,
        primary_only=True,
    )


@export_app.command("prop")
def export_prop(
    ctx: typer.Context,
    model: str = typer.Argument(..., help="Install model path, for example models/scenery/x.mdl."),
    force: bool = typer.Option(False, "--force", help="Rebake even when the recipe is unchanged."),
) -> None:
    """Re-decode one static model and rebake its mesh, materials and textures."""
    _corpus_unit(ctx, "export prop", models=[model], force=force)


@export_app.command("material")
def export_material(
    ctx: typer.Context,
    material: str = typer.Argument(..., help="Install material path, without materials/ or .vmt."),
    force: bool = typer.Option(False, "--force", help="Rebake even when the recipe is unchanged."),
) -> None:
    """Re-resolve one material and rebake its instance and every texture it draws."""
    _corpus_unit(ctx, "export material", materials=[material], force=force)


@export_app.command("texture")
def export_texture(
    ctx: typer.Context,
    texture: str = typer.Argument(..., help="Install texture path, without materials/ or .tth."),
    force: bool = typer.Option(False, "--force", help="Rebake even when the recipe is unchanged."),
) -> None:
    """Re-decode one texture, through every corpus material that draws it."""
    _corpus_unit(ctx, "export texture", textures=[texture], force=force)


@export_app.command("placed-model")
def export_placed_model(
    ctx: typer.Context,
    map_name: str = typer.Argument(..., help="Owning exported map, for example sp_tutorial_1."),
    model: str = typer.Argument(..., help="Placed MDL path used by that map."),
    force: bool = typer.Option(False, "--force", help="Rewrite and rebake this selected model."),
) -> None:
    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        stems = export_manager.export_placed_models(
            config, runner, [map_name], [model], force=force)
        console.print("placed-model export complete: " + ", ".join(stems))

    _execute(
        _state(ctx),
        "export placed-model",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        require_ue=True,
        activity=True,
        primary_only=True,
    )


@export_app.command("bundle")
def export_bundle(
    ctx: typer.Context,
    bundle: str = typer.Argument(...),
    force: bool = typer.Option(False, "--force"),
) -> None:
    allowed = {
        "audio", "corpus", "particles", "scripts", "signs", "vdata", "items", "cfg", "scenes",
        "ui", "use-icons", "npc", "policy",
    }
    if bundle not in allowed:
        raise typer.BadParameter("bundle must be one of: " + ", ".join(sorted(allowed)))
    # `corpus` decodes offline AND bakes the shared /ElysiumBaked/Shared packages, so it needs
    # both the install and an editor; `policy` needs only the editor.
    needs_editor = bundle in {"policy", "corpus"}

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        export_manager.export_bundle(config, runner, bundle, force=force)
        console.print(f"bundle export complete: {bundle}")

    _execute(
        _state(ctx),
        "export bundle",
        ExitCode.UNREAL_OR_BAKE if bundle == "policy" else ExitCode.OFFLINE_EXPORT,
        action,
        require_game=bundle != "policy",
        require_ue=needs_editor,
        activity=True,
        primary_only=True,
    )


@app.command("reconstruct")
def reconstruct(
    ctx: typer.Context,
    clean: bool = typer.Option(False, "--clean"),
    rebuild: bool = typer.Option(False, "--rebuild"),
) -> None:
    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager, unreal

        ready = sync_dependencies(config.repo_root, cache_root=_shared_cache_root(config))
        console.print("dependencies ready: " + ", ".join(ready))
        unreal.build(config, runner, "rebuild" if rebuild else "")
        maps = export_manager.export_profile(
            config,
            runner,
            "all",
            clean=clean,
            force=clean,
        )
        for tier in ("Substrate", "Policy", "Content"):
            unreal.run_tests(config, runner, tier)
        console.print(f"reconstruction complete: {len(maps)} map(s)")

    _execute(
        _state(ctx),
        "reconstruct",
        ExitCode.VALIDATION,
        action,
        require_game=True,
        require_ue=True,
        activity=True,
        primary_only=True,
    )


@app.command("test")
def test_command(
    ctx: typer.Context,
    filter_name: str = typer.Argument("Elysium."),
    stems: list[str] = typer.Option(
        None, "--stem", help="Widen the per-model parity slice (repeatable)."
    ),
) -> None:
    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import unreal

        summary = unreal.run_tests(config, runner, filter_name, parity_stems=stems or ())
        console.print(f"automation report: {summary['report_path']}")
        # A test that declines to run still reports Success, so the executed count is the only
        # honest measure of what a green tier covered.
        console.print(
            f"{summary['executed']} of {summary['total']} test(s) executed"
            f" in {summary['seconds']:.1f}s"
            + (f"; {summary['abstained']} abstained (prerequisite unavailable)"
               if summary["abstained"] else "")
        )
        for name in summary["abstentions"][:8]:
            console.print(f"  abstained: {name}")
        if len(summary["abstentions"]) > 8:
            console.print(f"  ... and {len(summary['abstentions']) - 8} more")

    _execute(
        _state(ctx),
        "test",
        ExitCode.VALIDATION,
        action,
        require_ue=True,
        activity=True,
    )


@run_app.command("editor", context_settings=PASSTHROUGH)
def run_editor(ctx: typer.Context, extra: list[str] = typer.Argument(None)) -> None:
    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import unreal

        unreal.run_editor(config, runner, [*(extra or ()), *ctx.args])

    _execute(
        _state(ctx),
        "run editor",
        ExitCode.UNREAL_OR_BAKE,
        action,
        require_ue=True,
        activity=True,
        primary_only=True,
    )


@run_app.command("play", context_settings=PASSTHROUGH)
def run_play(
    ctx: typer.Context,
    map_name: str | None = typer.Argument(None),
    extra: list[str] = typer.Argument(None),
) -> None:
    """Launch the game with Unreal's live log console and retained `Saved/Logs` session log.

    `play <map>` boots straight into that map; no argument goes to the menu.

    `play gr <model> <clip>` is the one target that is not a map: it boots into the green room's
    stage world with Cog up and the green-room window docked down the left edge, the same room
    `elysium.gr` opens from a running session. The model and the clip are optional.
    """

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import unreal

        unreal.run_play(config, runner, map_name, [*(extra or ()), *ctx.args])

    _execute(
        _state(ctx),
        "run play",
        ExitCode.UNREAL_OR_BAKE,
        action,
        require_ue=True,
        activity=True,
        primary_only=True,
    )


@debug_app.command("profile", context_settings=PASSTHROUGH)
def debug_profile(ctx: typer.Context, args: list[str] = typer.Argument(None)) -> None:
    _debug(ctx, "profile", [*(args or ()), *ctx.args])


@debug_app.command("probe", context_settings=PASSTHROUGH)
def debug_probe(ctx: typer.Context, args: list[str] = typer.Argument(None)) -> None:
    _debug(ctx, "probe", [*(args or ()), *ctx.args])


@debug_app.command("shots", context_settings=PASSTHROUGH)
def debug_shots(ctx: typer.Context, args: list[str] = typer.Argument(None)) -> None:
    _debug(ctx, "shots", [*(args or ()), *ctx.args])


@debug_app.command("move", context_settings=PASSTHROUGH)
def debug_move(ctx: typer.Context, args: list[str] = typer.Argument(None)) -> None:
    """Replay the movement courses and diff them against their baselines.

    Usage: `debug move <course> <hz>`, both optional. Two hosts run by default -- the generated
    gym, which brackets where a threshold is, and the sited courses on `sp_tutorial_1`, which
    answer whether we match retail. `--gym` or `--sited` runs one of them; `--promote` makes what
    the run just recorded the new baseline.
    """
    _debug(ctx, "move", [*(args or ()), *ctx.args])


@debug_app.command("cast", context_settings=PASSTHROUGH)
def debug_cast(ctx: typer.Context, args: list[str] = typer.Argument(None)) -> None:
    """Record the cast's locomotion courses and diff them against their baselines.

    Usage: `debug cast <course> <hz>`, both optional. The other producer of the same body trace
    `debug move` records for the player: a cast body is driven by an authored travel order -- a
    patrol route, a scripted beat -- while its own driver's selection is written through the same
    channels, and every course judges its own no-slide predicate before anything is compared.

    Two hosts. By default the body stands on the arena in the stage world, which is the regression
    instrument; `--sited` drives the priority map's own cast down the map's own authored route,
    armed and unarmed, which is the only host an acceptance claim can be made on. `-CastBody=<stem>`
    picks the body the arena stands; `--promote` makes what the run just recorded the new baseline.
    """
    _debug(ctx, "cast", [*(args or ()), *ctx.args])


@debug_app.command("compose", context_settings=PASSTHROUGH)
def debug_compose(ctx: typer.Context, args: list[str] = typer.Argument(None)) -> None:
    """Record the pose the animation graph composes, headless, and score it against the capture.

    Usage: `debug compose <weapon> <hz>`, both optional (default `item_w_ithaca_m_37` at 60 Hz).
    Drives a body on a real map through the real input router -- strafe, then strafe with the
    trigger held, then a reload -- and writes the player body's own component-space bone transforms
    per frame beside the four overlay slot rows that produced them.

    This is the half `Elysium.Content.RigCompose` cannot reach: that test evaluates baked sequences
    directly and never stands a graph up, so a slot published at the wrong weight, a bone mask
    resolved against the wrong skeleton or a chain composed in the wrong order is invisible to it.
    `-ElysiumMap=<name>` picks the host map.

    **Both bodies by default**, one launch each: the harness seats a body and drives it, so a second
    body in the same process would inherit the first one's motion. They are the two the retail
    capture recorded -- `malkavian_female_armor_0` and `malkavian_male_armor_0` -- and both reports
    go to one scoring call, whose closing summary puts their arm scalars beside each other.
    `--body <stem>` runs one; it is repeatable and takes a comma list.
    """
    _debug(ctx, "compose", [*(args or ()), *ctx.args])


@debug_app.command("greenroom", context_settings=PASSTHROUGH)
def debug_greenroom(ctx: typer.Context, args: list[str] = typer.Argument(None)) -> None:
    _debug(ctx, "greenroom", [*(args or ()), *ctx.args])


@debug_app.command("modelroom", context_settings=PASSTHROUGH)
def debug_modelroom(ctx: typer.Context, args: list[str] = typer.Argument(None)) -> None:
    _debug(ctx, "modelroom", [*(args or ()), *ctx.args])


@app.command("gr", context_settings=PASSTHROUGH)
def green_room(ctx: typer.Context, args: list[str] = typer.Argument(None)) -> None:
    """Open the interactive green room: one body, live, with cloth tuning.

    Usage: `gr <model> <clip> <map>`, and all three are optional -- with no model the stage
    comes up empty and the window picks one. Square brackets are Rich markup in a Typer
    help string, so the optional arguments are written in angle brackets here.

    `--drive` opens the second mode: the named body on the player pawn, standing on
    the generated movement gym, walked with WASD and framed by the shipping camera, with
    the sample, the selection record and the evaluated pose read back in the window. F1
    hands the keyboard between the game and the window.

    `--arena` opens the third: the same driven body in a clean square room -- flat floor,
    four walls, one cover block, interesting-place anchors -- with a built navmesh over it,
    which is what lets spawned characters path. The AI window stands the cast up, arms the
    player, and reads back each character's mind, conditions, schedule and enemy.

    Top level rather than under `debug` because it is driven by hand rather than run as a
    check, and it captures nothing.
    """
    _debug(ctx, "gr", [*(args or ()), *ctx.args])


def _debug(ctx: typer.Context, kind: str, args: list[str]) -> None:
    # The rendering is already offscreen; this is only about what happens to the sheet afterwards.
    # `--no-open` leaves it on disk and reports the path, which is what an unattended or scripted
    # run wants — handing a file to the shell's image viewer is a side effect nothing asked for.
    open_sheet = "--no-open" not in args
    args = [value for value in args if value != "--no-open"]

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import unreal

        output = unreal.run_harness(config, runner, kind, args)
        if output is not None:
            console.print(f"review sheet: {output}")
            if open_sheet and os.name == "nt" and output.is_file():
                os.startfile(output)  # type: ignore[attr-defined]

    _execute(
        _state(ctx),
        f"debug {kind}",
        ExitCode.VALIDATION,
        action,
        require_ue=True,
        activity=True,
        primary_only=True,
    )


@app.command("research", context_settings=PASSTHROUGH)
def research(
    ctx: typer.Context,
    case: str = typer.Argument(...),
    args: list[str] = typer.Argument(None),
) -> None:
    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        case_root = config.repo_root / "research" / "cases" / case
        values = [*(args or ()), *ctx.args]
        if not case_root.is_dir():
            matches = sorted(
                path
                for path in (config.repo_root / "research" / "tooling").rglob(f"{case}.py")
                if "__pycache__" not in path.parts
            )
            if len(matches) != 1:
                detail = (
                    f"unknown research case or tool: {case}"
                    if not matches
                    else f"ambiguous research tool {case}: {', '.join(map(str, matches))}"
                )
                raise ValueError(detail)
            result = runner.run([sys.executable, str(matches[0]), *values])
            if result.returncode:
                raise ProcessFailure(result, int(ExitCode.VALIDATION))
            return
        if values and values[0].endswith(".json"):
            spec = Path(values.pop(0))
            if not spec.is_absolute():
                spec = config.repo_root / spec
        else:
            specs = sorted((case_root / "specs").glob("*.json"))
            if not specs:
                raise ValueError(f"research case has no specs: {case}")
            spec = specs[0]
        result = runner.run(
            [
                sys.executable,
                str(config.repo_root / "research/tooling/ghidra/driver/ghidra_context.py"),
                str(spec),
                *values,
            ]
        )
        if result.returncode:
            raise ProcessFailure(result, int(ExitCode.VALIDATION))

    _execute(
        _state(ctx),
        "research",
        ExitCode.VALIDATION,
        action,
        require_game=False,
    )


@ide_app.command("vscode", context_settings=PASSTHROUGH)
def ide_vscode(ctx: typer.Context, args: list[str] = typer.Argument(None)) -> None:
    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        result = runner.run(
            [
                sys.executable,
                "-m",
                "elysium_pipeline.devtools.setup_vscode",
                *(args or ()),
                *ctx.args,
            ]
        )
        if result.returncode:
            raise ProcessFailure(result, int(ExitCode.DEPENDENCY_OR_TOOLCHAIN))

    _execute(
        _state(ctx),
        "ide vscode",
        ExitCode.DEPENDENCY_OR_TOOLCHAIN,
        action,
        require_ue=True,
        activity=True,
    )


@app.command(
    "mcp",
    context_settings=PASSTHROUGH,
)
def mcp(ctx: typer.Context) -> None:
    config = _state(ctx).resolve(require_work=False)
    try:
        task_worktrees.assert_primary_operation(
            config.repo_root, config.work_root, "mcp"
        )
    except task_worktrees.TaskWorktreeError as exc:
        console.print("[red]error:[/red] ", end="")
        console.print(str(exc), markup=False)
        raise typer.Exit(int(ExitCode.USAGE_OR_CONFIG)) from exc
    result = subprocess.run(
        [sys.executable, "-m", "elysium_pipeline.devtools.mcp_proxy", *ctx.args],
        cwd=config.repo_root,
        env=os.environ.copy(),
    )
    raise typer.Exit(result.returncode)
