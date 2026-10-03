"""The command runner's promises to a caller: it waits for a held lease and says who holds it,
it refuses with a code of its own, it ends a build or a test with one summary and a meaningful
exit, and a research query is warned at 10 s and stopped at 60.

None of it needs an editor: the leases are real files in a temporary checkout, the actions are
stand-ins, and the research tools are scripts that sleep.
"""

from __future__ import annotations

import os
from pathlib import Path
import threading
import time
from types import SimpleNamespace

import pytest
import typer
from typer.testing import CliRunner

from elysium_pipeline import cli, unreal
from elysium_pipeline.process import ProcessFailure, ProcessRunner, ProcessTimeout
from elysium_pipeline.reporting import ExitCode
from elysium_pipeline.workspace_lock import (
    WorkspaceLease,
    active_checkout_lease,
    active_lease,
)


RUNNER = CliRunner()


def _config(tmp_path: Path, name: str = "repo") -> SimpleNamespace:
    repo = tmp_path / name
    repo.mkdir(exist_ok=True)
    return SimpleNamespace(
        repo_root=repo,
        project=repo / "ElysiumUE.uproject",
        export_root=tmp_path / "exports",
        work_root=tmp_path / "work",
        log_root=tmp_path / "logs",
    )


def _state(config: SimpleNamespace, **kwargs) -> cli.CliState:
    class _Stub(cli.CliState):
        def resolve(self, **_ignored):
            return config

    return _Stub(game=None, work=None, ue=None, **kwargs)


@pytest.fixture(autouse=True)
def _no_editor_probe(monkeypatch):
    """Nothing here launches Unreal, so the process scan for a live editor has nothing to find."""

    monkeypatch.setattr(cli, "assert_project_idle", lambda project: None)


# ---------------------------------------------------------------- which lease a command holds

def test_a_build_holds_its_checkouts_lease_and_not_the_export_roots(tmp_path) -> None:
    config = _config(tmp_path)
    seen: dict = {}

    def action(config, _runner):
        seen["checkout"] = active_checkout_lease(config.repo_root)
        seen["export"] = active_lease(config.export_root)

    cli._execute(_state(config), "build", ExitCode.BUILD, action,
                 require_ue=True, checkout=True)

    assert seen["checkout"]["command"] == "build"
    assert seen["export"] is None, "a build writes nothing the other checkouts read"
    assert active_checkout_lease(config.repo_root) is None


def test_a_bake_holds_both_and_an_offline_export_only_the_export_roots(tmp_path) -> None:
    config = _config(tmp_path)
    seen: dict = {}

    def probe(label):
        def action(config, _runner):
            seen[label] = (active_checkout_lease(config.repo_root) is not None,
                           active_lease(config.export_root) is not None)
        return action

    # It writes bakes and launches the editor, so a build must not start under it.
    cli._execute(_state(config), "bake map", ExitCode.UNREAL_OR_BAKE, probe("bake"),
                 require_ue=True, activity=True)
    # It writes exports and never touches this checkout's binaries.
    cli._execute(_state(config), "export_v2 map-glb", ExitCode.OFFLINE_EXPORT, probe("offline"),
                 activity=True)

    assert seen == {"bake": (True, True), "offline": (False, True)}


def test_a_second_command_on_one_checkout_is_refused_with_the_busy_code(tmp_path) -> None:
    config = _config(tmp_path)
    outcome: dict = {}

    def holder(config, _runner):
        try:
            cli._execute(_state(config, no_wait=True), "test", ExitCode.VALIDATION,
                         lambda *_: pytest.fail("a refused command must not run"),
                         require_ue=True, checkout=True)
        except typer.Exit as refused:
            outcome["code"] = refused.exit_code

    cli._execute(_state(config), "run play", ExitCode.UNREAL_OR_BAKE, holder,
                 require_ue=True, checkout=True)

    # 8: "did not run", distinct from the 4 of a failed build and the 7 of a failed test.
    assert outcome["code"] == 8


def test_an_idle_sibling_checkout_never_blocks_this_one(tmp_path) -> None:
    here, sibling = _config(tmp_path, "here"), _config(tmp_path, "sibling")
    ran: list[str] = []

    def holder(_config, _runner):
        cli._execute(_state(sibling, no_wait=True), "build", ExitCode.BUILD,
                     lambda *_: ran.append("sibling"), require_ue=True, checkout=True)

    cli._execute(_state(here), "run play", ExitCode.UNREAL_OR_BAKE, holder,
                 require_ue=True, checkout=True)
    assert ran == ["sibling"]


def test_a_held_lease_is_waited_for_and_the_wait_is_announced_once(
        tmp_path, monkeypatch, capsys) -> None:
    monkeypatch.setattr(cli, "LEASE_POLL_SECONDS", 0.05)
    config = _config(tmp_path)
    holder = WorkspaceLease.for_checkout(config.repo_root, "run play").__enter__()
    threading.Timer(0.3, lambda: holder.__exit__(None, None, None)).start()
    ran: list[int] = []

    cli._execute(_state(config), "build", ExitCode.BUILD, lambda *_: ran.append(1),
                 require_ue=True, checkout=True)

    shown = capsys.readouterr().out
    assert ran == [1], "the command ran once the holder let go"
    assert shown.count("waiting for run play") == 1
    assert "--no-wait" in shown


def test_no_wait_is_a_flag_and_an_environment_switch(monkeypatch) -> None:
    monkeypatch.delenv("ELYSIUM_NO_WAIT", raising=False)
    state = cli.CliState(game=None, work=None, ue=None)
    assert cli._wait_seconds(state) == cli.DEFAULT_WAIT_SECONDS == 30 * 60
    state.no_wait = True
    assert cli._wait_seconds(state) == 0.0
    state.no_wait = False
    monkeypatch.setenv("ELYSIUM_NO_WAIT", "1")
    assert cli._wait_seconds(state) == 0.0


def test_the_editor_probe_runs_after_the_lease_is_held(tmp_path, monkeypatch) -> None:
    # A holder that just released took its editor down with it; what is still standing after
    # that is something this checkout did not start, which is the only thing worth refusing on.
    config = _config(tmp_path)
    held_when_probed: list[bool] = []
    monkeypatch.setattr(
        cli, "assert_project_idle",
        lambda project: held_when_probed.append(active_checkout_lease(config.repo_root) is not None))

    cli._execute(_state(config), "test", ExitCode.VALIDATION, lambda *_: None,
                 require_ue=True, checkout=True)
    assert held_when_probed == [True]


# ---------------------------------------------------------------- the end-of-run verdict

def test_a_verdict_names_the_result_the_time_and_the_exit_code() -> None:
    assert cli._verdict_line("build", False, 0, 46.2, {}) == "build ok in 46.2s"
    assert cli._verdict_line("test", True, 7, 192.0, {"errors": 2, "warnings": 5}) == (
        "test FAILED (exit 7) in 3m12s -- 2 error(s), 5 warning(s)")


def test_an_opted_in_command_ends_with_its_verdict(tmp_path, capsys) -> None:
    config = _config(tmp_path)
    cli._execute(_state(config), "build", ExitCode.BUILD, lambda *_: None, verdict=True)
    assert capsys.readouterr().out.rstrip().splitlines()[-1].startswith("build ok in ")

    def failing(_config, _runner):
        raise unreal.UnrealFailure("the build broke", exit_code=int(ExitCode.BUILD))

    with pytest.raises(typer.Exit) as failed:
        cli._execute(_state(config), "build", ExitCode.BUILD, failing, verdict=True)
    assert failed.value.exit_code == 4
    last = capsys.readouterr().out.rstrip().splitlines()[-1]
    assert last.startswith("build FAILED (exit 4) in ")


# ---------------------------------------------------------------- `elysium test A B C`

THREE = ["Elysium.Substrate.Npc.", "Elysium.Substrate.Hint.", "Elysium.Content.Hints."]


def _row(prefix, total, failed=0, seconds=0.5, failures=()):
    return {"prefix": prefix, "total": total, "executed": total, "abstained": 0,
            "failed": failed, "seconds": seconds, "failures": list(failures)}


def _summary(rows, failures=()):
    return {"total": sum(row["total"] for row in rows), "executed": sum(row["total"] for row in rows),
            "abstained": 0, "failed": len(failures), "seconds": 1.5, "abstentions": [],
            "failures": list(failures), "prefixes": rows,
            "report_path": "E:/work/reports/tests/x/index.json"}


FIRE = {"name": "Elysium.Substrate.Npc.Fire", "message": "Expected 3, but it was 4.",
        "where": "NpcTests.cpp:412"}


def test_the_summary_is_one_line_per_prefix_then_every_failure_by_name() -> None:
    summary = _summary([_row(THREE[0], 2, 1, failures=[FIRE]), _row(THREE[1], 1), _row(THREE[2], 0)],
                       failures=[FIRE])
    lines = cli._test_summary_lines(summary)

    assert lines[0].lstrip().startswith("FAIL") and THREE[0] in lines[0] and "2 test(s), 1 failed" in lines[0]
    assert lines[1].lstrip().startswith("ok") and THREE[1] in lines[1]
    assert lines[2].lstrip().startswith("EMPTY") and "matched no test" in lines[2]
    assert any("FAILED Elysium.Substrate.Npc.Fire: Expected 3, but it was 4. (NpcTests.cpp:412)" in line
               for line in lines)
    assert lines[-1].strip() == "automation report: E:/work/reports/tests/x/index.json"


def test_a_run_with_many_failures_cannot_scroll_its_verdict_away() -> None:
    failures = [{"name": f"Elysium.X.T{n}", "message": "bad", "where": ""} for n in range(40)]
    lines = cli._test_summary_lines(_summary([_row("Elysium.X.", 40, 40, failures=failures)], failures))
    assert sum("FAILED Elysium.X." in line for line in lines) == 12
    assert any("and 28 more failed" in line for line in lines)
    assert len(lines) < 20


def _patch_the_test_run(monkeypatch, config, outcome):
    """Run `elysium test` for real, minus the editor: `run_tests` answers (or raises) `outcome`."""

    calls: list = []

    def run_tests(_config, _runner, filters, parity_stems=()):
        calls.append(list(filters))
        if isinstance(outcome, Exception):
            raise outcome
        return outcome

    monkeypatch.setattr(cli.CliState, "resolve", lambda self, **_ignored: config)
    monkeypatch.setattr(unreal, "run_tests", run_tests)
    return calls


def test_several_prefixes_reach_one_run_and_a_green_run_ends_ok(tmp_path, monkeypatch) -> None:
    config = _config(tmp_path)
    summary = _summary([_row(prefix, 1) for prefix in THREE])
    calls = _patch_the_test_run(monkeypatch, config, summary)

    result = RUNNER.invoke(app := cli.app, ["test", *THREE])

    assert result.exit_code == 0, result.output
    assert calls == [THREE], "every prefix in the same call, so the same boot"
    for prefix in THREE:
        assert prefix in result.output
    assert result.output.rstrip().splitlines()[-1].startswith("test ok in ")

    # With none named it is the default tier: the explicit list of default groups, in one boot.
    calls.clear()
    assert RUNNER.invoke(app, ["test"]).exit_code == 0
    assert calls == [list(unreal.DEFAULT_TEST_FILTER)]
    assert all(prefix.startswith("Elysium.") and prefix.endswith(".") for prefix in calls[0])
    assert not any(prefix.startswith(f"Elysium.{group}.")
                   for prefix in calls[0] for group in unreal.OPT_IN_TEST_GROUPS)

    # `--all` is the whole suite, and a prefix beside it is a usage error that never boots the editor.
    calls.clear()
    assert RUNNER.invoke(app, ["test", "--all"]).exit_code == 0
    assert calls == [["Elysium."]]
    calls.clear()
    assert RUNNER.invoke(app, ["test", "--all", THREE[0]]).exit_code == 2
    assert calls == []


def test_the_summary_names_its_tier(tmp_path, monkeypatch) -> None:
    config = _config(tmp_path)
    summary = _summary([_row("Elysium.Arm.", 3)])
    summary["tier"] = "arm"
    _patch_the_test_run(monkeypatch, config, summary)

    result = RUNNER.invoke(cli.app, ["test", "arm"])

    assert result.exit_code == 0, result.output
    assert "  tier: arm" in result.output
    assert "arm tier: 3 of 3 test(s) executed" in result.output


def test_a_failed_test_run_names_the_test_and_exits_7(tmp_path, monkeypatch) -> None:
    config = _config(tmp_path)
    summary = _summary([_row(THREE[0], 2, 1, failures=[FIRE]), _row(THREE[1], 1)], failures=[FIRE])
    _patch_the_test_run(monkeypatch, config, unreal.UnrealFailure(
        "1 of 3 test(s) failed: Elysium.Substrate.Npc.Fire (report: x)",
        exit_code=int(ExitCode.VALIDATION), summary=summary))

    result = RUNNER.invoke(cli.app, ["test", THREE[0], THREE[1], "--json"])

    assert result.exit_code == 7
    # `--json` keeps stdout to the one envelope, and the envelope carries the failures by name.
    import json
    envelope = json.loads(result.stdout)
    assert envelope["status"] == "failed" and envelope["exit_code"] == 7
    assert envelope["failures"][0]["name"] == "Elysium.Substrate.Npc.Fire"
    assert [row["prefix"] for row in envelope["prefixes"]] == THREE[:2]

    human = RUNNER.invoke(cli.app, ["test", THREE[0], THREE[1]])
    assert human.exit_code == 7
    assert "FAILED Elysium.Substrate.Npc.Fire" in human.output
    assert human.output.rstrip().splitlines()[-1].startswith("test FAILED (exit 7) in ")


def test_no_wait_on_the_command_line_refuses_a_held_checkout_with_exit_8(
        tmp_path, monkeypatch) -> None:
    config = _config(tmp_path)
    calls = _patch_the_test_run(monkeypatch, config, _summary([_row("Elysium.", 1)]))

    with WorkspaceLease.for_checkout(config.repo_root, "run play"):
        refused = RUNNER.invoke(cli.app, ["test", "--no-wait"])

    assert refused.exit_code == 8, refused.output
    assert "run play" in refused.output and "not waiting" in refused.output
    assert calls == [], "a refused test never boots the editor"


# ---------------------------------------------------------------- the research watchdog

def _tool(tmp_path: Path, name: str, body: str) -> Path:
    path = tmp_path / "research" / "tooling" / f"{name}.py"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(body, encoding="utf-8")
    return path


def _watched(tmp_path, monkeypatch, *, warn=0.2, stop=1.0):
    monkeypatch.setattr(cli, "RESEARCH_WARN_SECONDS", warn)
    monkeypatch.setattr(cli, "RESEARCH_STOP_SECONDS", stop)
    said: list[str] = []
    runner = ProcessRunner(cwd=tmp_path, environment=os.environ.copy(), log=None,
                           output_sink=said.append)
    return _config(tmp_path), runner, said


def _slow_log(config) -> list[list[str]]:
    path = config.log_root / cli.SLOW_QUERY_LOG
    if not path.is_file():
        return []
    return [line.split("\t") for line in path.read_text(encoding="utf-8").splitlines()]


def test_a_query_past_the_stop_is_killed_told_to_optimize_and_logged(tmp_path, monkeypatch) -> None:
    config, runner, said = _watched(tmp_path, monkeypatch)
    tool = _tool(tmp_path, "slow", "import time\ntime.sleep(30)\n")
    began = time.monotonic()

    with pytest.raises(ProcessTimeout) as stopped:
        cli._run_research_tool(config, runner, tool, "slow", ["--map", "sp_tutorial_1"])

    assert time.monotonic() - began < 15, "killed at the stop, not left to run"
    assert "stopped at 1 s" in str(stopped.value) and "optimize the tool" in str(stopped.value)
    assert stopped.value.category == int(ExitCode.VALIDATION)
    assert any(line.startswith("warning: research slow is past") for line in said)
    header, row = _slow_log(config)
    assert header == ["time", "source", "query", "seconds"]
    assert row[1:3] == ["research", "slow --map sp_tutorial_1"]
    assert float(row[3]) >= 1.0


def test_a_query_between_the_warning_and_the_stop_finishes_and_is_logged(
        tmp_path, monkeypatch) -> None:
    config, runner, said = _watched(tmp_path, monkeypatch, warn=0.2, stop=20.0)
    tool = _tool(tmp_path, "medium", "import time\ntime.sleep(0.7)\n")

    cli._run_research_tool(config, runner, tool, "medium", [])

    warnings = [line for line in said if line.startswith("warning: research medium")]
    assert len(warnings) == 1, "one warning line, not one per second"
    (_header, row) = _slow_log(config)
    assert row[2] == "medium" and 0.6 <= float(row[3]) < 20


def test_a_fast_query_leaves_no_trace(tmp_path, monkeypatch) -> None:
    config, runner, said = _watched(tmp_path, monkeypatch, warn=5.0, stop=20.0)
    tool = _tool(tmp_path, "fast", "print('answer')\n")

    cli._run_research_tool(config, runner, tool, "fast", [])

    assert said == ["answer"]
    assert _slow_log(config) == []


def test_a_tool_that_declares_itself_a_run_is_neither_warned_nor_stopped(
        tmp_path, monkeypatch) -> None:
    config, runner, said = _watched(tmp_path, monkeypatch, warn=0.2, stop=0.5)
    tool = _tool(tmp_path, "packet",
                 'RESEARCH_NOT_A_QUERY = "launches agent workers"\n'
                 "import time\ntime.sleep(1.2)\nprint('done')\n")

    cli._run_research_tool(config, runner, tool, "packet", [])

    assert said == ["done"]
    assert _slow_log(config) == []


def test_the_exemption_is_a_reason_in_the_tool_not_a_flag_a_caller_adds(
        tmp_path, monkeypatch) -> None:
    # A bare `True` gives no reason; a mention in a comment or a function is not a declaration;
    # and a `--flag` on the command line is just an argument handed to the tool.
    for name, text in {
        "bare": "RESEARCH_NOT_A_QUERY = True\n",
        "empty": 'RESEARCH_NOT_A_QUERY = "  "\n',
        "comment": "# RESEARCH_NOT_A_QUERY = 'x'\nx = 1\n",
        "nested": "def f():\n    RESEARCH_NOT_A_QUERY = 'x'\n",
    }.items():
        assert cli._research_exemption(_tool(tmp_path, name, text)) is None, name
    assert cli._research_exemption(
        _tool(tmp_path, "typed", 'RESEARCH_NOT_A_QUERY: str = "builds a database"\n')
    ) == "builds a database"

    # A tool of runs and queries exempts its runs by subcommand, and only them.
    mixed = _tool(tmp_path, "mixed",
                  'RESEARCH_NOT_A_QUERY = {"dump": "drives Ghidra", "build": "  "}\n')
    assert cli._research_exemption(mixed, ["dump", "vampire.dll"]) == "drives Ghidra"
    assert cli._research_exemption(mixed, ["code", "0x1028a380"]) is None, "a query is not"
    assert cli._research_exemption(mixed, ["build"]) is None, "an empty reason is no reason"
    assert cli._research_exemption(mixed, []) is None
    assert cli._research_exemption(mixed, ["code", "dump"]) is None, "only the first is a subcommand"
    # A run mode chosen by a switch is exempt wherever the switch stands, and only with it.
    flagged = _tool(tmp_path, "flagged", 'RESEARCH_NOT_A_QUERY = {"--attach": "injects the hook"}\n')
    assert cli._research_exemption(flagged, ["--scenario", "x", "--attach"]) == "injects the hook"
    assert cli._research_exemption(flagged, ["--status"]) is None

    config, runner, _said = _watched(tmp_path, monkeypatch, warn=0.2, stop=0.5)
    slow = _tool(tmp_path, "slowwithflag", "import time\ntime.sleep(30)\n")
    with pytest.raises(ProcessTimeout):
        cli._run_research_tool(config, runner, slow, "slowwithflag", ["--no-timeout", "--exempt"])


def test_a_failing_tool_still_exits_with_the_validation_code(tmp_path, monkeypatch) -> None:
    config, runner, _said = _watched(tmp_path, monkeypatch, warn=5.0, stop=20.0)
    tool = _tool(tmp_path, "broken", "raise SystemExit(3)\n")
    with pytest.raises(ProcessFailure) as failed:
        cli._run_research_tool(config, runner, tool, "broken", [])
    assert failed.value.category == int(ExitCode.VALIDATION)


def test_the_research_command_runs_a_tool_under_the_budget(tmp_path, monkeypatch) -> None:
    config = _config(tmp_path)
    monkeypatch.setattr(cli.CliState, "resolve", lambda self, **_ignored: config)
    monkeypatch.setattr(cli, "RESEARCH_WARN_SECONDS", 0.2)
    monkeypatch.setattr(cli, "RESEARCH_STOP_SECONDS", 1.0)
    _tool(tmp_path.joinpath("repo"), "sleepy", "import time\ntime.sleep(30)\n")

    stopped = RUNNER.invoke(cli.app, ["research", "sleepy", "--probe"])

    assert stopped.exit_code == int(ExitCode.VALIDATION), stopped.output
    assert "stopped at 1 s" in stopped.output
    assert len(_slow_log(config)) == 2


def test_a_research_tools_answer_reaches_the_console_without_verbose(
        tmp_path, monkeypatch) -> None:
    # A lookup's output is its answer: the console filter, tuned for engine chatter, used to
    # withhold every line of it ("33 lines suppressed") unless the caller knew to add --verbose.
    config = _config(tmp_path)
    monkeypatch.setattr(cli.CliState, "resolve", lambda self, **_ignored: config)
    _tool(tmp_path.joinpath("repo"), "lookup",
          "print('0x1028a380  CAI_BaseNPC::SelectSchedule')\n"
          "print('  docs/vtmb/npc-ai/schedules.md:12  ## SelectSchedule')\n")

    answered = RUNNER.invoke(cli.app, ["research", "lookup", "0x1028a380"])

    assert answered.exit_code == 0, answered.output
    assert "0x1028a380  CAI_BaseNPC::SelectSchedule" in answered.output
    assert "docs/vtmb/npc-ai/schedules.md:12" in answered.output
    assert "suppressed" not in answered.output
    log = (config.log_root).glob("*-research.log")
    assert any("CAI_BaseNPC::SelectSchedule" in path.read_text(encoding="utf-8") for path in log), (
        "the run log still keeps every line")
