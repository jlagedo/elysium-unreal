"""One public command surface for the Elysium project."""

from __future__ import annotations

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

from elysium_pipeline.config import ConfigError, ProjectConfig
from elysium_pipeline.dependencies import (
    DependencyError,
    check_dependencies,
    load_project_lock,
    sync_dependencies,
)
from elysium_pipeline.process import ProcessFailure, ProcessRunner
from elysium_pipeline.reporting import ExitCode, RunReport


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
run_app = typer.Typer(help="Launch the Unreal editor or standalone game.")
debug_app = typer.Typer(help="Run development and acceptance harnesses.")
ide_app = typer.Typer(help="Configure supported development environments.")
app.add_typer(deps_app, name="deps")
app.add_typer(export_app, name="export")
app.add_typer(run_app, name="run")
app.add_typer(debug_app, name="debug")
app.add_typer(ide_app, name="ide")


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
) -> Any:
    report = RunReport(command=name, arguments=sys.argv[1:])
    config: ProjectConfig | None = None
    log_handle = None
    report_path: Path | None = None
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
            stamp = report.started_at[:19].replace(":", "").replace("-", "")
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
) -> None:
    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        names = export_manager.export_targeted_maps(
            config,
            runner,
            maps,
            force=force,
            intermediate_only=intermediate_only,
        )
        console.print("map export complete: " + ", ".join(names))

    _execute(
        _state(ctx),
        "export map",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        require_ue=not intermediate_only,
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
    )


@export_app.command("bundle")
def export_bundle(
    ctx: typer.Context,
    bundle: str = typer.Argument(...),
    force: bool = typer.Option(False, "--force"),
) -> None:
    allowed = {
        "audio", "particles", "scripts", "signs", "vdata", "cfg", "scenes",
        "ui", "use-icons", "npc", "policy",
    }
    if bundle not in allowed:
        raise typer.BadParameter("bundle must be one of: " + ", ".join(sorted(allowed)))

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
        require_ue=bundle == "policy",
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
    )


@app.command("test")
def test_command(
    ctx: typer.Context,
    filter_name: str = typer.Argument("Elysium."),
) -> None:
    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import unreal

        unreal.run_tests(config, runner, filter_name)

    _execute(
        _state(ctx),
        "test",
        ExitCode.VALIDATION,
        action,
        require_ue=True,
    )


@run_app.command("editor", context_settings=PASSTHROUGH)
def run_editor(ctx: typer.Context, extra: list[str] = typer.Argument(None)) -> None:
    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import unreal

        unreal.run_editor(config, runner, [*(extra or ()), *ctx.args])

    _execute(_state(ctx), "run editor", ExitCode.UNREAL_OR_BAKE, action, require_ue=True)


@run_app.command("play", context_settings=PASSTHROUGH)
def run_play(
    ctx: typer.Context,
    map_name: str | None = typer.Argument(None),
    extra: list[str] = typer.Argument(None),
) -> None:
    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import unreal

        unreal.run_play(config, runner, map_name, [*(extra or ()), *ctx.args])

    _execute(_state(ctx), "run play", ExitCode.UNREAL_OR_BAKE, action, require_ue=True)


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

    Top level rather than under `debug` because it is driven by hand rather than run as a
    check, and it captures nothing.
    """
    _debug(ctx, "gr", [*(args or ()), *ctx.args])


def _debug(ctx: typer.Context, kind: str, args: list[str]) -> None:
    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import unreal

        output = unreal.run_harness(config, runner, kind, args)
        if output is not None:
            console.print(f"review sheet: {output}")
            if os.name == "nt" and output.is_file():
                os.startfile(output)  # type: ignore[attr-defined]

    _execute(
        _state(ctx),
        f"debug {kind}",
        ExitCode.VALIDATION,
        action,
        require_ue=True,
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
    )


@app.command(
    "mcp",
    context_settings=PASSTHROUGH,
)
def mcp(ctx: typer.Context) -> None:
    config = _state(ctx).resolve(require_work=False)
    result = subprocess.run(
        [sys.executable, "-m", "elysium_pipeline.devtools.mcp_proxy", *ctx.args],
        cwd=config.repo_root,
        env=os.environ.copy(),
    )
    raise typer.Exit(result.returncode)
