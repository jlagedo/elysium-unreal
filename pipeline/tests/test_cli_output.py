"""What the console owes an agent: the signal, the counts, and where the rest went."""

from __future__ import annotations

import io
import json
from pathlib import Path
import sys
from typing import Any

import pytest
import typer
from rich.console import Console
from typer.testing import CliRunner

from elysium_pipeline.cli import (
    _ECHO_DISTINCT_LIMIT,
    _ChildEcho,
    _StdoutTee,
    _console_signal,
    _echo_category,
    _echo_key,
    _echo_severity,
    _local_signal,
    app,
)
from elysium_pipeline.reporting import RunReport


RUNNER = CliRunner()

STAMP = "[2026.09.11-23.35.41:233][555]"


def _echo() -> tuple[_ChildEcho, io.StringIO]:
    """An echo writing to a captured, unstyled console of fixed width."""

    buffer = io.StringIO()
    console = Console(file=buffer, width=200, no_color=True, highlight=False, soft_wrap=True)
    return _ChildEcho(console), buffer


def _stub_state(tmp_path, **kwargs):
    """A `CliState` whose config points at a temporary log root.

    `_execute` otherwise resolves the real one, so these tests would need a configured
    checkout to pass and would drop a log and a report into the shared work tree on every
    run. `conftest` promises a rootless checkout still collects.
    """
    from types import SimpleNamespace

    from elysium_pipeline import cli

    class _Stub(cli.CliState):
        def resolve(self, **_ignored):
            return SimpleNamespace(
                log_root=tmp_path, repo_root=Path.cwd(), project=None, export_root=None
            )

    return _Stub(game=None, work=None, ue=None, **kwargs)


def test_the_automation_mirror_normalises_onto_its_own_original() -> None:
    # Under automation Unreal says every warning twice. The two spellings differ in three
    # ways -- the mirrored category, the moved severity, the trailing marker -- and all
    # three have to be undone or the console doubles.
    original = f"{STAMP}LogElysiumNpcEnt: Warning: #0 guard(npc) TaskFail 0x15: Bad activity"
    mirror = (
        f"{STAMP}LogAutomationController: Warning: "
        "LogElysiumNpcEnt: #0 guard(npc) TaskFail 0x15: Bad activity [log] "
    )
    assert _echo_key(original) == _echo_key(mirror)
    assert _echo_category(_echo_key(mirror)) == "LogElysiumNpcEnt"


def test_a_mirrored_warning_prints_once_and_counts_once() -> None:
    # The mirror is the same event said twice. Counting it twice would make the `--json`
    # warning count match neither the distinct events nor the raw lines.
    echo, buffer = _echo()
    echo("LogElysiumWeapon: Warning: wield presentation failed")
    echo("LogAutomationController: Warning: LogElysiumWeapon: wield presentation failed [log]")
    echo.close()
    printed = buffer.getvalue()
    assert printed.count("wield presentation failed") == 1
    assert "1 repeats collapsed" in printed
    assert echo.counts()["warnings"] == 1


def test_the_same_text_at_two_severities_is_two_events() -> None:
    # Deleting the severity from the dedupe key would file an escalation as a repeat of the
    # warning that preceded it, and it would never be printed.
    echo, buffer = _echo()
    echo("LogElysiumNpcEnt: Warning: nav mesh missing for map la_hub_1")
    echo("LogElysiumNpcEnt: Error: nav mesh missing for map la_hub_1")
    echo.close()
    assert buffer.getvalue().count("nav mesh missing for map la_hub_1") == 2
    assert echo.counts() == {"warnings": 1, "errors": 1, "suppressed_lines": 0}


def test_compiler_diagnostics_are_signal() -> None:
    # `build` runs a compiler, not the engine: none of its diagnostics carry a log category,
    # and a filter that knows only `Category: Severity:` shows a failed build nothing at all.
    for line in (
        r"E:\dev\x\Foo.cpp(95,28): error C2259: cannot instantiate abstract class",
        r"E:\dev\x\Foo.cpp(12,3): warning C4996: deprecated",
        "LINK : fatal error LNK1181: cannot open input file 'x.lib'",
        "/home/x/foo.cpp:95:28: error: no member named 'y'",
    ):
        assert _console_signal(line), line
    assert _echo_severity(r"Foo.cpp(95,28): error C2259: nope") == "error"
    assert _echo_severity(r"Foo.cpp(12,3): warning C4996: old") == "warning"
    assert _echo_severity("LINK : fatal error LNK1181: nope") == "error"
    # Ordinary build chatter stays chatter.
    assert not _console_signal("  Building 412 actions with 24 processes...")
    assert not _console_signal("[3/412] Compile Module.ElysiumUE.1.cpp")


def test_errors_are_never_crowded_out_by_a_warning_storm() -> None:
    # Measured on this project's failing runs the first error ranks ~165th among distinct
    # signal lines. On one shared budget a failing run shows everything except why it failed.
    echo, buffer = _echo()
    for index in range(300):
        echo(f"LogElysiumWeapon: Warning: distinct warning {index}")
    echo("LogOutputDevice: Error: Ensure condition failed: the thing that broke")
    echo.close()
    printed = buffer.getvalue()
    assert "the thing that broke" in printed, "the error must be quoted, not withheld"
    assert any("the thing that broke" in line for line in echo.evidence())


def test_evidence_puts_errors_first_and_in_full() -> None:
    echo, _buffer = _echo()
    echo("LogFoo: Error: the first thing that broke")
    for index in range(300):
        echo(f"LogElysiumWeapon: Warning: distinct warning {index}")
    echo("LogFoo: Error: the last thing that broke")
    echo.close()
    evidence = echo.evidence()
    assert len(evidence) <= 30
    assert "the first thing that broke" in evidence[0]
    assert "the last thing that broke" in evidence[1]
    assert any("distinct warning 299" in line for line in evidence), "warnings fill the rest"


def test_ordinary_engine_chatter_never_reaches_the_console() -> None:
    echo, buffer = _echo()
    for index in range(500):
        echo(f"LogElysiumWorld: spawned actor {index}")
    echo.close()
    printed = buffer.getvalue()
    assert "spawned actor" not in printed
    assert "500 lines suppressed" in printed
    assert "--verbose for all" in printed


def test_engine_python_boot_chatter_is_not_treated_as_our_script_output() -> None:
    # `LogPython` is in the vocabulary for our own editor scripts, which speak as
    # `LogPython: [tag] ...`. The engine narrates every plugin's init_unreal.py under the
    # same category at Display verbosity -- about thirty lines in front of every editor run,
    # enough to push the real failure out of the evidence tail.
    echo, buffer = _echo()
    for index in range(30):
        echo(f"{STAMP}LogPython: Display: Running start-up script plugin{index}.py... started...")
    echo(f"{STAMP}LogPython: [import-characters] 4821 to import, 12030 reused")
    echo(f"{STAMP}LogPython: Error: the thing that actually broke")
    echo.close()
    printed = buffer.getvalue()
    assert "Running start-up script" not in printed
    assert "4821 to import" in printed
    assert "the thing that actually broke" in printed
    assert [line for line in echo.evidence() if "start-up script" in line] == []


def test_the_footer_names_the_escape_hatch_whenever_anything_was_withheld() -> None:
    # Silent truncation is the failure mode this whole path exists to avoid: an agent that
    # cannot tell output was dropped has no reason to go looking for the rest.
    echo, buffer = _echo()
    echo("LogElysiumWorld: ordinary chatter")
    echo.close()
    assert "--verbose for all" in buffer.getvalue()


def test_a_quiet_child_gets_no_footer_at_all() -> None:
    echo, buffer = _echo()
    echo("LogPython: [bake] world: 182344 verts")
    echo.close()
    printed = buffer.getvalue()
    assert "182344 verts" in printed
    assert "suppressed" not in printed
    assert "--verbose" not in printed


def test_distinct_signal_lines_stop_being_quoted_and_start_being_tallied() -> None:
    echo, buffer = _echo()
    for index in range(_ECHO_DISTINCT_LIMIT + 25):
        echo(f"LogElysiumNpcEnt: Warning: distinct failure {index}")
    echo.close()
    printed = buffer.getvalue()
    assert f"distinct failure {_ECHO_DISTINCT_LIMIT - 1}" in printed
    assert f"distinct failure {_ECHO_DISTINCT_LIMIT}" not in printed
    assert "25 distinct not shown" in printed
    assert "LogElysiumNpcEnt: 65 line(s), 65 distinct" in printed


def test_the_category_tally_truncates_with_the_established_phrasing() -> None:
    echo, buffer = _echo()
    for category in range(12):
        for index in range(_ECHO_DISTINCT_LIMIT):
            echo(f"LogCategory{category:02d}: Warning: failure {index}")
    echo.close()
    assert "  ... and 4 more" in buffer.getvalue()


def test_evidence_holds_distinct_lines_not_a_raw_tail() -> None:
    # A tail of raw lines would be 400 copies of one warning. The deque keeps first
    # occurrences, so 400 repeats cost one slot rather than all of them.
    echo, _buffer = _echo()
    echo("LogElysiumWorld: Warning: the interesting failure")
    for _ in range(400):
        echo("LogElysiumNpcEnt: Warning: the boring repeat")
    echo.close()
    evidence = echo.evidence()
    assert any("the interesting failure" in line for line in evidence)
    assert sum("the boring repeat" in line for line in evidence) == 1


def test_the_real_test_log_collapses_to_something_an_agent_can_read() -> None:
    # The measurement this change exists for, run against the shapes a real automation log
    # holds: an interleaved storm of mirrored warnings inside ordinary engine chatter.
    echo, buffer = _echo()
    lines = 0
    for index in range(2000):
        echo(f"LogAsyncCompilation: Display: compiling shader {index}")
        lines += 1
        if index % 4 == 0:
            warning = f"LogElysiumNpcEnt: Warning: #{index % 3} guard(npc) TaskFail 0x15"
            echo(warning)
            echo(f"LogAutomationController: Warning: {warning.replace(': Warning:', '')} [log]")
            lines += 2
    echo.close()
    printed = buffer.getvalue()
    assert lines > 2500
    assert len(printed.splitlines()) < 40, printed
    assert "repeats collapsed" in printed


def test_the_offline_vocabulary_is_the_bang_and_the_lowercase_warning() -> None:
    # The GLB seams have no log category and no severity token; `!` in front of a line is how
    # they have always marked a warning, and `warning: <item>: <detail>` is the per-item form.
    assert _local_signal("! texture GLB corpus: 7 textures published with warnings")
    assert _local_signal("  ! model not in the install: models/foo.mdl")
    assert _local_signal("  warning: models/character/eyes/demen: not recoverable")
    assert not _local_signal("texture [512/12886]: foo -> bar.glb (94.1% of source bytes)")
    assert not _local_signal("faces: 18234  materials: 88  skipped TOOLS: 12")
    # An Unreal child line must not be caught by the offline rules by accident.
    assert not _local_signal(f"{STAMP}LogElysiumWorld: Warning: something")


def test_offline_warnings_are_tallied_by_their_clause_not_by_the_word_warning() -> None:
    # `fonts-glb` emits sixty thousand `warning: <item>: <detail>` lines. Splitting on the
    # first colon files every one of them under a label called `warning`, which tells a
    # reader nothing; the clause is what distinguishes them.
    assert _echo_category(
        _echo_key("  warning: marlett_17_000_008: typed but unidentified: header.words[3]")
    ) == "warning: typed but unidentified"
    assert _echo_category(
        _echo_key("  warning: models/eyes/demen: the primary image is not recoverable here")
    ) == "warning: the primary image is not recoverable"
    # An Unreal line still groups under its own category.
    assert _echo_category(_echo_key(f"{STAMP}LogElysiumWeapon: Warning: x")) == "LogElysiumWeapon"


def test_in_process_prints_reach_the_log_and_the_filter() -> None:
    # Bare `print()` from the GLB seams used to reach neither: it went to the terminal and
    # was unrecoverable afterwards at any verbosity.
    log = io.StringIO()
    echo, buffer = _echo()
    tee = _StdoutTee(log, echo)
    for index in range(3000):
        tee.write(f"texture [{index}/3000]: unit{index} -> out{index}.glb\n")
    tee.write("  warning: models/eyes/demen: the primary image is not recoverable\n")
    tee.write("! texture GLB corpus: 1 texture published with warnings\n")
    tee.finish()
    echo.close()
    printed = buffer.getvalue()
    assert log.getvalue().count("\n") == 3002, "every line belongs in the run log"
    assert "texture [1500/3000]" not in printed, "progress rows are not console signal"
    assert "the primary image is not recoverable" in printed
    assert "1 texture published with warnings" in printed
    assert "3,000 lines suppressed" in printed


def test_the_tee_reassembles_a_line_print_split_across_calls() -> None:
    # `print("x")` writes the text and the newline as two calls; a naive tee turns one line
    # into two, and the second is an empty string that nothing can classify.
    log = io.StringIO()
    seen: list[str] = []
    tee = _StdoutTee(log, seen.append)
    tee.write("! partial")
    tee.write(" line")
    tee.write("\n")
    tee.write("a trailing line with no newline")
    tee.finish()
    assert seen == ["! partial line", "a trailing line with no newline"]
    assert log.getvalue() == "! partial line\na trailing line with no newline\n"


def test_the_tee_does_not_swallow_the_commands_own_summary() -> None:
    # `_teed_stdout` pins the rich console to the real stream, because rich resolves
    # `sys.stdout` at write time and would otherwise route the verdict into the filter.
    import sys

    from elysium_pipeline.cli import _teed_stdout, console

    real = io.StringIO()
    filtered: list[str] = []
    saved_pin = console._file
    saved_stdout, sys.stdout = sys.stdout, real
    try:
        # The context pins the console to whatever the real stream is on entry, which is
        # what a command's `console.print` has to keep reaching.
        with _teed_stdout(None, filtered.append):
            print("texture [1/2]: progress nobody asked for")
            console.print("texture GLB corpus export complete: 12886 textures")
    finally:
        sys.stdout = saved_stdout
    assert "12886 textures" in real.getvalue()
    assert filtered == ["texture [1/2]: progress nobody asked for"]
    # And the pin is undone, or every later command would print into a closed buffer.
    assert console._file is saved_pin, "the pin is restored, not merely cleared"


def test_the_run_log_and_the_run_report_share_one_stem(tmp_path) -> None:
    # They used to be spelled two different ways, so a command whose name held an underscore
    # wrote its log and its report under names that did not match. This asserts against the
    # file `write()` actually produces, not just against the helper.
    report = RunReport(command="export_v2 map-glb", arguments=[])
    report.finish()
    written = report.write(tmp_path)
    assert written.name == f"{report.artifact_stem()}.json"
    assert "_" not in written.stem.split("Z-", 1)[1]


def test_verbose_after_the_subcommand_is_refused_rather_than_forwarded() -> None:
    # `build` and `run play` forward what they do not recognise to Unreal, so a trailing
    # `--verbose` would silently become a child argument and the escape hatch the console
    # advertises would do nothing. It has to say so.
    from elysium_pipeline import cli

    assert "--verbose" in RUNNER.invoke(app, ["--help"]).output

    class _Ctx:
        def __init__(self, state, args=(), params=None):
            self.obj, self.args, self.params = state, list(args), params or {}

    plain = cli.CliState(game=None, work=None, ue=None)
    # A PASSTHROUGH command collects it in `ctx.args` -- `build`, `debug shots`.
    with pytest.raises(Exception, match="root option"):
        cli._state(_Ctx(plain, args=["--verbose"]))
    # A command ending in a variadic Argument swallows it as a positional -- `run play`.
    with pytest.raises(Exception, match="root option"):
        cli._state(_Ctx(plain, params={"map_name": None, "extra": ["-v"]}))
    # The correct spelling passes through untouched. Asserted here rather than by invoking a
    # command, because every command that reaches this guard launches Unreal.
    verbose = cli.CliState(game=None, work=None, ue=None, verbose=True)
    assert cli._state(_Ctx(verbose, args=["--verbose"])) is verbose
    assert cli._state(_Ctx(plain, args=["--rebuild"], params={"maps": ["la_hub_1"]})) is plain


def test_the_agent_facing_commands_all_offer_json() -> None:
    for command in (
        ["test", "--help"],
        ["build", "--help"],
        ["doctor", "--help"],
        ["verify", "characters", "--help"],
        ["verify", "maps", "--help"],
        ["verify", "model-catalogues", "--help"],
        ["verify", "expression-tables", "--help"],
    ):
        result = RUNNER.invoke(app, command)
        assert result.exit_code == 0, result.output
        assert "--json" in result.output, command


def test_a_failed_command_reports_evidence_and_a_structured_envelope(tmp_path) -> None:
    # The path every one of these findings lived on, and the one nothing covered: a command
    # that fails must still say why, and `--json` must still produce exactly one object.
    from elysium_pipeline import cli

    state = _stub_state(tmp_path, json_output=True)

    def action(config, runner):
        print("! the offline decoder complained")
        print("texture [1/2]: progress nobody needs")
        raise RuntimeError("the thing that broke")

    captured = io.StringIO()
    saved, sys.stdout = sys.stdout, captured
    try:
        with pytest.raises(typer.Exit):
            cli._execute(state, "test", cli.ExitCode.VALIDATION, action, require_work=False)
    finally:
        sys.stdout = saved
    payload = json.loads(captured.getvalue())
    assert payload["status"] == "failed"
    assert payload["exit_code"] == int(cli.ExitCode.VALIDATION)
    assert payload["detail"] == "the thing that broke"
    assert any("the offline decoder complained" in line for line in payload["evidence"])
    assert not any("progress nobody needs" in line for line in payload["evidence"])


def test_doctor_json_is_one_object_on_stdout_and_keeps_its_original_keys() -> None:
    result = RUNNER.invoke(app, ["doctor", "--repo-only", "--json"])
    assert result.exit_code == 0, result.output
    payload = json.loads(result.stdout)
    # The envelope adds context; the three keys the command has always published survive.
    assert set(payload) >= {"ok", "errors", "warnings", "command", "status", "exit_code"}
    assert payload["command"] == "doctor"
    assert payload["status"] == "succeeded"


def test_a_curated_line_bypasses_the_filter_but_not_the_log() -> None:
    # The seam's whole contract, in one place: a producer that has already decided a line is
    # worth showing gets it shown, while raw chatter beside it is still classified.
    from elysium_pipeline.cli import _StdoutTee

    log = io.StringIO()
    echo, buffer = _echo()
    tee = _StdoutTee(log, echo, echo.passthrough)
    tee.write("texture [1/2]: progress nobody asked for\n")
    tee.write_signal("[  1/120] la_hub_1                    FAILED  12.4s")
    tee.write_signal("      the tail of the task that failed")
    tee.finish()
    echo.close()
    printed = buffer.getvalue()
    assert "FAILED" in printed
    assert "the tail of the task that failed" in printed
    assert "progress nobody asked for" not in printed
    # Both kinds are in the log; only the classified one was suppressed from the console.
    assert log.getvalue().count("\n") == 3
    assert "1 lines suppressed" in printed


def test_a_curated_line_becomes_evidence_when_the_command_fails() -> None:
    from elysium_pipeline.cli import _StdoutTee

    echo, _buffer = _echo()
    tee = _StdoutTee(io.StringIO(), echo, echo.passthrough)
    tee.write_signal("      the tail of the task that failed")
    tee.finish()
    echo.close()
    assert any("the tail of the task that failed" in line for line in echo.evidence())


def test_the_tee_refuses_fileno_and_buffer_with_an_explanation() -> None:
    # No caller reaches either today. Delegating them would hand a child a handle that writes
    # past the tee unlogged, or let a binary write land inside another line, so they refuse --
    # and the refusal has to say why, because the next person meets it cold.
    from elysium_pipeline.cli import _StdoutTee

    tee = _StdoutTee(io.StringIO(), lambda line: None)
    with pytest.raises(io.UnsupportedOperation, match="ProcessRunner"):
        tee.fileno()
    with pytest.raises(io.UnsupportedOperation, match="line reassembly"):
        tee.buffer


def _shard_that_talks(chunk):
    print(f"  ! prop decode failed for {chunk[0]}: no such model")
    return [item * 2 for item in chunk]


def test_a_worker_shards_diagnostics_reach_the_parent_at_every_job_count() -> None:
    # A worker process inherits the OS stdout handle, so its prints used to go past the tee
    # and past the log. The bug was not only that they vanished: they vanished only above one
    # job, because map_chunks runs a single shard inline.
    from elysium_pipeline import workers

    seen = {}
    for jobs in (1, 2):
        captured = io.StringIO()
        saved, sys.stdout = sys.stdout, captured
        try:
            results = workers.map_chunks(
                _shard_that_talks, [1, 2, 3, 4], jobs=jobs, label="probe"
            )
        finally:
            sys.stdout = saved
        assert sorted(item for shard in results for item in shard) == [2, 4, 6, 8]
        seen[jobs] = [
            line for line in captured.getvalue().splitlines() if "prop decode failed" in line
        ]
    assert seen[1], "the inline path always reached the console"
    assert seen[2], "the pooled path must now reach it too"
    assert len(seen[2]) == 2, "one line per shard, replayed in the parent"


def test_the_verdict_and_the_failure_land_in_the_run_log(tmp_path) -> None:
    # The console pin keeps a command's own summary out of the filter; without a log sink it
    # kept it out of the record too, so the one line missing from a run log was the answer.
    from elysium_pipeline import cli

    state = _stub_state(tmp_path, json_output=True)

    def action(config, runner):
        cli._summary(state, "610 of 610 test(s) executed in 92.4s", executed=610)
        print("! the offline decoder complained")
        raise RuntimeError("3 of 254 test(s) failed")

    captured = io.StringIO()
    saved, sys.stdout = sys.stdout, captured
    try:
        with pytest.raises(typer.Exit):
            cli._execute(state, "test", cli.ExitCode.VALIDATION, action, require_work=False)
    finally:
        sys.stdout = saved
    payload = json.loads(captured.getvalue())
    written = Path(payload["log"]).read_text(encoding="utf-8")
    assert "610 of 610 test(s) executed in 92.4s" in written, "the verdict belongs in the log"
    assert "error: 3 of 254 test(s) failed" in written, "so does the failure"
    assert "! the offline decoder complained" in written
    # And the handle is released, with the sink taken back down after the run.
    assert state.log_sink is None


def test_an_action_that_exits_by_itself_still_releases_the_log(tmp_path) -> None:
    # `typer.Exit` is re-raised rather than handled, so it leaves without reaching the trailer
    # that closes the log on every other path.
    from elysium_pipeline import cli

    state = _stub_state(tmp_path)
    handles: list[Any] = []

    def action(config, runner):
        handles.append(state.log_sink)
        raise typer.Exit(0)

    with pytest.raises(typer.Exit):
        cli._execute(state, "test", cli.ExitCode.VALIDATION, action, require_work=False)
    assert len(handles) == 1 and handles[0] is not None, "the sink was live during the action"
    assert state.log_sink is None, "and taken back down on the way out"


def test_the_seam_holds_end_to_end_from_the_task_graph_to_the_echo(tmp_path) -> None:
    # The two halves of the curated-line seam find each other by name across a module
    # boundary: `tasking` looks up `write_signal` with getattr, `cli` defines it. Both sides
    # are covered apart, which means renaming either one leaves every other test green while
    # the feature silently reverts. This is the test that notices.
    from elysium_pipeline.cli import _StdoutTee
    from elysium_pipeline.tasking import TaskProgress

    log = io.StringIO()
    echo, buffer = _echo()
    tee = _StdoutTee(log, echo, echo.passthrough)
    saved_out, saved_err = sys.stdout, sys.stderr
    sys.stdout = sys.stderr = tee
    try:
        with TaskProgress(1, tmp_path / "tasks.log") as progress:
            progress.emit(
                "la_hub_1", "failed", 12.4,
                "\n".join(f"body line {index}" for index in range(40)),
                error="the decoder gave up",
            )
    finally:
        sys.stdout, sys.stderr = saved_out, saved_err
    tee.finish()
    echo.close()
    printed = buffer.getvalue()
    assert "la_hub_1" in printed and "FAILED" in printed
    assert "body line 39" in printed, "the failure tail must survive both filters"
    assert "the decoder gave up" in printed
    assert "task log:" in printed
    # And what a failure quotes back leads with the line naming the task, not with body text.
    evidence = echo.evidence()
    assert "la_hub_1" in evidence[0] and "FAILED" in evidence[0]
