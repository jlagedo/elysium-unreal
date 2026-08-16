"""One public command surface for the Elysium project."""

from __future__ import annotations

from contextlib import nullcontext
from dataclasses import dataclass
from datetime import datetime, timezone
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import time
from typing import Any, Callable

import typer
from rich.console import Console

from elysium_pipeline import lanes, task_worktrees
from elysium_pipeline.config import ConfigError, ProjectConfig
from elysium_pipeline.dependencies import (
    DependencyError,
    check_dependencies,
    load_project_lock,
    sync_dependencies,
)
from elysium_pipeline.process import ProcessFailure, ProcessRunner
from elysium_pipeline.reporting import ExitCode, RunReport
from elysium_pipeline.workspace_lock import WorkspaceLease


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
lane_app = typer.Typer(help="Manage detached, generated-state-isolated QA worktrees.")
worktree_app = typer.Typer(help="Manage mutable, isolated development-task worktrees.")
app.add_typer(deps_app, name="deps")
app.add_typer(export_app, name="export")
app.add_typer(verify_app, name="verify")
app.add_typer(run_app, name="run")
app.add_typer(debug_app, name="debug")
app.add_typer(ide_app, name="ide")
app.add_typer(lane_app, name="lane")
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
    require_built_lane: bool = False,
    record_lane_run: bool = True,
    primary_only: bool = False,
) -> Any:
    report = RunReport(command=name, arguments=sys.argv[1:])
    config: ProjectConfig | None = None
    log_handle = None
    report_path: Path | None = None
    lane: lanes.LaneRecord | None = None
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
                else lambda line: console.print(line, markup=False)
            ),
        )
        lane = lanes.current_lane(config.work_root, config.repo_root)
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
        if lane is not None and record_lane_run:
            report.metadata.update(
                {
                    "lane": lane.name,
                    "candidate_commit": lane.candidate_commit,
                    "worktree": str(lane.worktree),
                    "export_root": str(lane.export_root),
                }
            )
        if activity:
            lanes.assert_project_idle(config.project)
            if lane is not None:
                candidate = lanes.assert_lane_runnable(lane, runner)
                if require_built_lane and lane.build_commit != candidate:
                    raise lanes.LaneError(
                        f"lane {lane.name!r} needs `uv run elysium build` for candidate "
                        f"{candidate[:12]} before {name}"
                    )
            if config.export_root is not None:
                lease_metadata: dict[str, Any] = {}
                if lane is not None:
                    lease_metadata["lane"] = lane.name
                if task is not None:
                    lease_metadata["task_worktree"] = task.name
                lease = WorkspaceLease(
                    config.export_root,
                    name,
                    config.repo_root,
                    metadata=lease_metadata or None,
                )
        with lease:
            if lane is not None and activity:
                lanes.begin_activity(lane, name)
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
        if lane is not None and record_lane_run:
            lanes.record_run(
                lane,
                command=name,
                status="succeeded",
                exit_code=0,
                started_at=started,
                report_path=report_path,
            )
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
    if lane is not None and record_lane_run:
        lanes.record_run(
            lane,
            command=name,
            status="failed",
            exit_code=code,
            started_at=started,
            report_path=report_path,
        )
    if not quiet_report:
        console.print("[red]error:[/red] ", end="")
        console.print(detail, markup=False)
    if report_path and not quiet_report:
        console.print(f"[dim]run report: {report_path}[/dim]")
    raise typer.Exit(code)


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
) -> None:
    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        if config.game_root is None or config.work_root is None or config.ue_root is None:
            raise RuntimeError("worktree creation needs the game, work, and UE roots")
        creation = task_worktrees.create_task_worktree(
            source_repo=config.repo_root,
            source_work_root=config.work_root,
            game_root=config.game_root,
            ue_root=config.ue_root,
            runner=runner,
            name=name,
            ref=at,
            worktree_path=path,
        )
        record = creation.record
        console.print(
            f"task worktree {record.name} created at {record.worktree} "
            f"({record.base_commit[:12]})"
        )
        console.print(f"task work root: {record.work_root}")
        if creation.source_dirty:
            console.print(
                "[yellow]warning:[/yellow] main has uncommitted changes; "
                "the task worktree contains only the named commit"
            )
        console.print(f"assign the task agent to: {record.worktree}")
        console.print("task agent setup: uv run elysium deps sync")
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
    if value["missing"]:
        console.print("  [red]checkout missing[/red]")
        return
    cleanliness = "clean" if not value["dirty"] else f"dirty ({len(value['dirty'])})"
    unlanded = value["unlanded_commits"]
    landing = "landed" if not unlanded else f"{len(unlanded)} commit(s) to land"
    console.print(
        f"  source {value['head'][:12]} from {value['base_commit'][:12]} "
        f"({cleanliness}, {landing})"
    )
    console.print(f"  activity {activity}; work root {value['work_root']}")


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
        record_lane_run=False,
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


@lane_app.command("create")
def lane_create(
    ctx: typer.Context,
    name: str = typer.Argument(..., help="Lane name, such as qa."),
    at: str = typer.Option("HEAD", "--at", help="Commit or ref to detach at."),
    path: Path | None = typer.Option(
        None, "--path", help="Worktree path; defaults to a sibling named <repo>-<lane>."
    ),
) -> None:
    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        if config.game_root is None or config.work_root is None or config.ue_root is None:
            raise RuntimeError("lane creation needs the game, work, and UE roots")
        creation = lanes.create_lane(
            source_repo=config.repo_root,
            source_work_root=config.work_root,
            game_root=config.game_root,
            ue_root=config.ue_root,
            runner=runner,
            name=name,
            ref=at,
            worktree_path=path,
        )
        record = creation.record
        console.print(
            f"lane {record.name} created at {record.worktree} "
            f"({record.candidate_commit[:12]})"
        )
        console.print(f"lane work root: {record.work_root}")
        if creation.source_dirty:
            console.print(
                "[yellow]warning:[/yellow] the development checkout has changes; "
                "the lane contains only the named commit"
            )
        console.print(f"next: cd {record.worktree}")
        console.print("      uv run elysium deps sync")
        console.print("      uv run elysium build")

    _execute(
        _state(ctx),
        "lane create",
        ExitCode.VALIDATION,
        action,
        require_game=True,
        require_ue=True,
        primary_only=True,
    )


@lane_app.command("dispatch")
def lane_dispatch(
    ctx: typer.Context,
    name: str = typer.Argument(..., help="Lane to advance."),
    at: str = typer.Option("HEAD", "--at", help="Candidate commit or ref."),
) -> None:
    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        if config.work_root is None:
            raise RuntimeError("lane dispatch needs ELYSIUM_WORK_ROOT")
        record = lanes.lane_records(config.work_root, name)[0]
        lanes.dispatch_lane(record, runner, at)
        console.print(
            f"lane {record.name} dispatched to {record.candidate_commit[:12]} at {record.worktree}"
        )
        if lanes.repository_has_changes(record.source_repo, runner):
            console.print(
                "[yellow]warning:[/yellow] the development checkout has changes; "
                "the dispatched lane contains only the named commit"
            )
        console.print("evidence reset: automated pending; live pending")
        console.print(f"next: cd {record.worktree}")
        console.print("      uv run elysium deps sync")
        console.print("      uv run elysium build")

    _execute(
        _state(ctx),
        "lane dispatch",
        ExitCode.VALIDATION,
        action,
    )


def _print_lane_status(value: dict[str, Any]) -> None:
    activity = value["activity"]
    active = "idle" if activity is None else (
        f"{activity.get('command', 'busy')} (pid {activity.get('pid', '?')})"
    )
    source = "candidate" if value["candidate_matches"] else "MISMATCH"
    cleanliness = "clean" if not value["dirty"] else f"dirty ({len(value['dirty'])})"
    incomplete = value["incomplete_domains"]
    corpus = "complete" if not incomplete else "incomplete: " + ", ".join(incomplete)
    console.print(f"{value['name']}: {value['worktree']}")
    console.print(
        f"  source {str(value['head'] or '?')[:12]} ({source}, {cleanliness}, "
        f"{value['branch'] or '?'})"
    )
    console.print(
        f"  activity {active}; build {'ready' if value['build_ready'] else 'required'}; "
        f"{value['baked_map_count']} baked map(s)"
    )
    console.print(f"  corpus {corpus}")
    console.print(
        f"  evidence automated {value['automated']}; live {value['live']}"
        + (f"; {value['note']}" if value["note"] else "")
    )
    if value["last_run"]:
        run = value["last_run"]
        console.print(
            f"  last {run['command']}: {run['status']} ({run['candidate_commit'][:12]})"
        )
    if value["unreal_processes"]:
        processes = ", ".join(
            f"{item['name']} pid {item['pid']}" for item in value["unreal_processes"]
        )
        console.print(f"  Unreal {processes}")
    if value["error"]:
        console.print(f"  [red]error:[/red] {value['error']}")


@lane_app.command("status")
def lane_status_command(
    ctx: typer.Context,
    name: str | None = typer.Argument(None, help="One lane; omit to list every lane."),
    json_output: bool = typer.Option(False, "--json"),
) -> None:
    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        if config.work_root is None:
            raise RuntimeError("lane status needs ELYSIUM_WORK_ROOT")
        records = lanes.lane_records(config.work_root, name)
        values = [lanes.lane_status(record, runner) for record in records]
        if json_output:
            typer.echo(json.dumps(values, indent=2, sort_keys=True))
            return
        if not values:
            console.print("no QA lanes")
            return
        for index, value in enumerate(values):
            if index:
                console.print()
            _print_lane_status(value)

    _execute(
        _state(ctx),
        "lane status",
        ExitCode.VALIDATION,
        action,
        quiet_report=True,
        record_lane_run=False,
    )


@lane_app.command("mark")
def lane_mark(
    ctx: typer.Context,
    name: str | None = typer.Argument(
        None, help="Lane to mark; omit when running inside that lane."
    ),
    automated: str | None = typer.Option(None, "--automated"),
    live: str | None = typer.Option(None, "--live"),
    note: str | None = typer.Option(None, "--note"),
) -> None:
    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        if config.work_root is None:
            raise RuntimeError("lane mark needs ELYSIUM_WORK_ROOT")
        records = lanes.lane_records(config.work_root, name)
        if len(records) != 1:
            raise lanes.LaneError("name one lane to mark")
        record = lanes.mark_lane(
            records[0],
            runner,
            automated=automated,
            live=live,
            note=note,
        )
        console.print(
            f"lane {record.name}: automated {record.automated}; live {record.live}"
        )
        if record.snapshot is not None:
            console.print(
                f"snapshot {record.snapshot['commit'][:12]}: "
                f"{record.snapshot['baked_map_count']} baked map(s), "
                f"{record.snapshot['bake_receipt_count']} receipt file(s)"
            )

    _execute(
        _state(ctx),
        "lane mark",
        ExitCode.VALIDATION,
        action,
    )


@deps_app.command("sync")
def deps_sync(ctx: typer.Context) -> None:
    def action(config: ProjectConfig, _runner: ProcessRunner) -> None:
        ready = sync_dependencies(config.repo_root, cache_root=config.cache_root)
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


def _export_profile_command(
    ctx: typer.Context,
    profile: str,
    *,
    clean: bool,
    force: bool,
    jobs: int | None,
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
        require_built_lane=True,
        primary_only=True,
    )


@export_app.command("grid")
def export_grid(
    ctx: typer.Context,
    clean: bool = typer.Option(False, "--clean"),
    force: bool = typer.Option(False, "--force"),
    jobs: int | None = typer.Option(None, "--jobs", min=1),
) -> None:
    _export_profile_command(ctx, "grid", clean=clean, force=force, jobs=jobs)


@export_app.command("all")
def export_all_command(
    ctx: typer.Context,
    clean: bool = typer.Option(False, "--clean"),
    force: bool = typer.Option(False, "--force"),
    jobs: int | None = typer.Option(None, "--jobs", min=1),
) -> None:
    _export_profile_command(ctx, "all", clean=clean, force=force, jobs=jobs)


@export_app.command("map")
def export_map(
    ctx: typer.Context,
    maps: list[str] = typer.Argument(...),
    force: bool = typer.Option(False, "--force"),
    intermediate_only: bool = typer.Option(False, "--intermediate-only"),
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
        require_built_lane=not intermediate_only,
        primary_only=True,
    )


@export_app.command("characters")
def export_characters(
    ctx: typer.Context,
    models: list[str] = typer.Argument(
        None,
        help="Models to bake: a stem, family:<name>, or bank:<name>. "
             "Omit for the whole cast, which is what the game needs.",
    ),
    force: bool = typer.Option(False, "--force", help="Rewrite every .eskm container."),
    no_sweep: bool = typer.Option(
        False, "--no-sweep", help="Leave assets the declared partition no longer produces."
    ),
    force_sweep: bool = typer.Option(
        False, "--force-sweep", help="Sweep past the safety guard on how much may be removed."
    ),
) -> None:
    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        stems = export_manager.export_characters(
            config, runner, models, force=force,
            sweep=not no_sweep, force_sweep=force_sweep,
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
        require_built_lane=True,
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
        require_built_lane=True,
        primary_only=True,
    )


@verify_app.command("characters")
def verify_characters(
    ctx: typer.Context,
    models: list[str] = typer.Argument(
        None,
        help="Models to check: a stem, family:<name>, or bank:<name>. Omit for the whole cast.",
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
        require_built_lane=True,
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
        require_built_lane=True,
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
        require_built_lane=integrate,
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
        require_built_lane=True,
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
        require_built_lane=True,
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
        require_built_lane=needs_editor,
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

        ready = sync_dependencies(config.repo_root, cache_root=config.cache_root)
        console.print("dependencies ready: " + ", ".join(ready))
        unreal.build(config, runner, "rebuild" if rebuild else "")
        maps = export_manager.export_profile(
            config,
            runner,
            "all",
            clean=clean,
            force=clean,
        )
        unreal.run_tests(config, runner, "Substrate")
        unreal.run_tests(config, runner, "Content")
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
        if not summary["total"]:
            return
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
        require_built_lane=True,
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
        require_built_lane=True,
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
        require_built_lane=True,
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
        require_built_lane=True,
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
