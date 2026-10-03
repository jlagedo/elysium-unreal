import json
from pathlib import Path
import re
from tempfile import TemporaryDirectory
from types import SimpleNamespace
from unittest import mock

import pytest

from elysium_pipeline import unreal
from elysium_pipeline.process import ProcessIdle, ProcessResult, ProcessTimeout
from elysium_pipeline.reporting import ExitCode
from elysium_pipeline.unreal import TEST_ABSTENTION_TOKEN, summarize_test_report


def summarize(tests: list[dict]) -> dict:
    with TemporaryDirectory() as temp:
        report = Path(temp)
        (report / "index.json").write_text(
            json.dumps({"tests": tests, "failed": 0, "totalDuration": 1.25}),
            encoding="utf-8",
        )
        return summarize_test_report(report)


def make_entry(name: str, message: str = "") -> dict:
    entries = [] if not message else [{"event": {"message": message}}]
    return {"fullTestPath": name, "entries": entries}


def test_explicit_abstention_is_not_counted_as_execution() -> None:
    summary = summarize([
        make_entry("Elysium.Content.Corpus", f"{TEST_ABSTENTION_TOKEN}: npc missing"),
        make_entry("Elysium.Content.Pure"),
    ])

    assert summary["total"] == 2
    assert summary["executed"] == 1
    assert summary["abstained"] == 1
    assert summary["abstentions"] == ["Elysium.Content.Corpus"]


def test_legacy_incomplete_marker_remains_visible() -> None:
    summary = summarize([
        make_entry("Elysium.Content.Legacy", "npc export domain is marked incomplete")
    ])

    assert summary["executed"] == 0
    assert summary["abstained"] == 1


def test_unrelated_skip_word_does_not_abstain_the_test() -> None:
    summary = summarize([
        make_entry("Elysium.Content.Partial", "one optional comparison was skipped")
    ])

    assert summary["executed"] == 1
    assert summary["abstained"] == 0


def stub_config(root: Path) -> SimpleNamespace:
    return SimpleNamespace(
        work_root=root / "work",
        export_root=root / "exports",
        project=root / "repo" / "ElysiumUE.uproject",
    )


def reporter(tests: list[dict], failed: int = 0):
    """A `_run` stand-in that writes the report the real commandlet would have written."""

    def write_report(_config, _runner, _executable, arguments, **_kwargs) -> None:
        switch = next(
            value for value in arguments if str(value).startswith("-ReportExportPath=")
        )
        report = Path(str(switch).split("=", 1)[1])
        report.mkdir(parents=True)
        (report / "index.json").write_text(
            json.dumps({"tests": tests, "failed": failed, "totalDuration": 0}),
            encoding="utf-8",
        )

    return write_report


def run_tests_against(config, tests: list[dict], failed: int = 0,
                      filter_name: str = "Substrate") -> dict:
    with (
        mock.patch.object(unreal, "editor_executable", return_value=Path("editor")),
        mock.patch.object(unreal, "_run", side_effect=reporter(tests, failed)),
    ):
        return unreal.run_tests(config, None, filter_name)


def test_each_run_retains_its_report_below_the_work_root() -> None:
    with TemporaryDirectory() as temp:
        root = Path(temp)
        config = stub_config(root)
        executed = [make_entry("Elysium.Substrate.Case")]

        first = run_tests_against(config, executed)
        second = run_tests_against(config, executed)

        first_path = Path(first["report_path"])
        second_path = Path(second["report_path"])
        report_root = (config.work_root / "reports" / "tests").resolve()
        assert first_path.is_relative_to(report_root)
        assert second_path.is_relative_to(report_root)
        assert first_path != second_path
        assert not first_path.is_relative_to(config.export_root.resolve())


def test_a_selection_that_matched_nothing_is_a_failure() -> None:
    # `Automation RunTest` reports success for a filter that matched no test, so a mistyped
    # tier used to be indistinguishable from a clean run.
    with TemporaryDirectory() as temp:
        config = stub_config(Path(temp))
        with pytest.raises(unreal.UnrealFailure, match="matched no test"):
            run_tests_against(config, [])


def test_an_unknown_tier_name_is_refused_before_launching() -> None:
    with TemporaryDirectory() as temp:
        config = stub_config(Path(temp))
        with pytest.raises(unreal.UnrealFailure, match="unknown test tier 'Substate'"):
            run_tests_against(config, [], filter_name="Substate")


def test_a_fully_qualified_filter_is_passed_through() -> None:
    with TemporaryDirectory() as temp:
        config = stub_config(Path(temp))
        summary = run_tests_against(
            config,
            [make_entry("Elysium.Substrate.Knockback.Rule")],
            filter_name="Elysium.Substrate.Knockback.",
        )
        assert summary["executed"] == 1


def test_a_reported_failure_raises_even_when_the_commandlet_exits_clean() -> None:
    with TemporaryDirectory() as temp:
        config = stub_config(Path(temp))
        with pytest.raises(unreal.UnrealFailure, match=r"1 of 1 test\(s\) failed"):
            run_tests_against(config, [make_entry("Elysium.Substrate.Case")],
                                   failed=1)


def test_a_wholly_abstained_tier_is_vacuous_and_raises() -> None:
    with TemporaryDirectory() as temp:
        config = stub_config(Path(temp))
        with pytest.raises(unreal.UnrealFailure, match="abstained"):
            run_tests_against(config, [
                make_entry("Elysium.Content.A", f"{TEST_ABSTENTION_TOKEN}: no corpus"),
                make_entry("Elysium.Content.B", f"{TEST_ABSTENTION_TOKEN}: no corpus"),
            ])


def test_partial_abstention_still_passes() -> None:
    with TemporaryDirectory() as temp:
        config = stub_config(Path(temp))
        summary = run_tests_against(config, [
            make_entry("Elysium.Content.A", f"{TEST_ABSTENTION_TOKEN}: no corpus"),
            make_entry("Elysium.Content.B"),
        ])
        assert summary["executed"] == 1
        assert summary["abstained"] == 1


def _killed_result(output: str) -> ProcessResult:
    return ProcessResult(
        argv=("UnrealEditor-Cmd.exe",),
        cwd=Path.cwd(),
        returncode=1,
        started_at="2026-09-28T09:15:14+00:00",
        duration_seconds=121.0,
        output=output,
    )


_HUNG_OUTPUT = "\n".join([
    "LogAutomationController: Display: Test Started. Name={Activate} "
    "Path={Elysium.Substrate.NpcKernelLifecycle.Activate}",
    "LogAutomationController: Display: Test Completed. Result={Success} Name={Activate} "
    "Path={Elysium.Substrate.NpcKernelLifecycle.Activate}",
    "LogAutomationController: Display: Test Started. Name={Dormancy} "
    "Path={Elysium.Substrate.NpcKernelLifecycle.Dormancy}",
    "LogElysiumNpcEnt: Warning: Occluded target reaction percentages do not add up to 100%",
]) + "\n"


def test_the_automation_launch_carries_an_idle_bound_and_the_deadline() -> None:
    seen: dict = {}

    def recording_run(config, runner, executable, arguments, **kwargs):
        seen.update(kwargs)
        reporter([make_entry("Elysium.Substrate.Case")])(config, runner, executable,
                                                         arguments)

    with TemporaryDirectory() as temp:
        config = stub_config(Path(temp))
        with (
            mock.patch.object(unreal, "editor_executable", return_value=Path("editor")),
            mock.patch.object(unreal, "_run", side_effect=recording_run),
        ):
            unreal.run_tests(config, None, "Substrate")
    # A killed editor is "the run failed" (6), not the runner's default "toolchain" (3).
    assert seen == {"timeout": unreal.TEST_TIMEOUT_SECONDS,
                    "idle_timeout": unreal.TEST_IDLE_TIMEOUT_SECONDS,
                    "category": ExitCode.UNREAL_OR_BAKE}
    assert unreal.TEST_IDLE_TIMEOUT_SECONDS == 120.0
    assert unreal.TEST_TIMEOUT_SECONDS == 900.0


def test_an_idle_kill_names_the_hung_test_and_the_report() -> None:
    # The last line the editor printed is a warning the hung test logged; the test in flight
    # comes from the controller's start line with no completion after it.
    def hanging_run(_config, _runner, _executable, arguments, **_kwargs):
        switch = next(value for value in arguments
                      if str(value).startswith("-ReportExportPath="))
        Path(str(switch).split("=", 1)[1]).mkdir(parents=True)
        raise ProcessIdle(_killed_result(_HUNG_OUTPUT), 3, 120.0,
                          _HUNG_OUTPUT.splitlines()[-1], 900.0)

    with TemporaryDirectory() as temp:
        config = stub_config(Path(temp))
        with (
            mock.patch.object(unreal, "editor_executable", return_value=Path("editor")),
            mock.patch.object(unreal, "_run", side_effect=hanging_run),
            pytest.raises(ProcessIdle) as caught,
        ):
            unreal.run_tests(config, None, "Substrate")
        message = str(caught.value)
        report_root = (config.work_root / "reports" / "tests").resolve()
        assert "printed nothing for 120s" in message
        assert "Occluded target reaction percentages" in message
        assert "test in flight: Elysium.Substrate.NpcKernelLifecycle.Dormancy" in message
        assert f"automation report: {report_root}" in message
        assert "no index.json" in message
        # The type and category survive, so the CLI still prints the retained tail.
        assert caught.value.category == 3
        assert caught.value.result.output == _HUNG_OUTPUT


def test_a_deadline_kill_is_annotated_the_same_way() -> None:
    def slow_run(_config, _runner, _executable, arguments, **_kwargs):
        switch = next(value for value in arguments
                      if str(value).startswith("-ReportExportPath="))
        report = Path(str(switch).split("=", 1)[1])
        report.mkdir(parents=True)
        (report / "index.json").write_text("{}", encoding="utf-8")
        raise ProcessTimeout(_killed_result(_HUNG_OUTPUT), 3, 900.0)

    with TemporaryDirectory() as temp:
        config = stub_config(Path(temp))
        with (
            mock.patch.object(unreal, "editor_executable", return_value=Path("editor")),
            mock.patch.object(unreal, "_run", side_effect=slow_run),
            pytest.raises(ProcessTimeout, match="900s deadline") as caught,
        ):
            unreal.run_tests(config, None, "Substrate")
        assert "Elysium.Substrate.NpcKernelLifecycle.Dormancy" in str(caught.value)
        assert "partial index.json written" in str(caught.value)


def test_a_completed_test_is_not_in_flight() -> None:
    finished = "\n".join(_HUNG_OUTPUT.splitlines()[:2])
    assert unreal.automation_test_in_flight(finished) is None
    assert unreal.automation_test_in_flight(_HUNG_OUTPUT) == \
        "Elysium.Substrate.NpcKernelLifecycle.Dormancy"
    assert unreal.automation_test_in_flight("no automation lines at all") is None


def test_only_the_newest_reports_survive() -> None:
    with TemporaryDirectory() as temp:
        reports = Path(temp) / "tests"
        reports.mkdir()
        for stamp in range(6):
            (reports / f"2026082{stamp}T000000.0Z-elysium-substrate").mkdir()

        removed = unreal.prune_test_reports(reports, keep=2)

        assert removed == 4
        assert sorted(child.name for child in reports.iterdir()) == ["20260824T000000.0Z-elysium-substrate",
            "20260825T000000.0Z-elysium-substrate"]


def test_a_missing_directory_is_not_an_error() -> None:
    with TemporaryDirectory() as temp:
        assert unreal.prune_test_reports(Path(temp) / "absent") == 0


# ---------------------------------------------------------------- several prefixes, one boot

def timed(name: str, seconds: float = 0.5, *, failed: str | None = None) -> dict:
    """A report row with a duration, and the error a failed test logs."""

    entry = {"fullTestPath": name, "duration": seconds, "state": "Success", "entries": []}
    if failed is not None:
        entry["state"] = "Fail"
        entry["errors"] = 1
        entry["entries"] = [{
            "event": {"type": "Error", "message": failed},
            "filename": "E:\\dev\\elysium-unreal\\Source\\ElysiumUE\\Private\\Tests\\NpcTests.cpp",
            "lineNumber": 412,
        }]
    return entry


def capturing_run(tests: list[dict], failed: int = 0):
    """A `_run` stand-in that records every launch and writes the report it would have."""

    launches: list[list[str]] = []
    write = reporter(tests, failed)

    def run(config, runner, executable, arguments, **kwargs):
        launches.append([str(value) for value in arguments])
        write(config, runner, executable, arguments, **kwargs)

    return run, launches


def run_prefixes(config, filters, tests: list[dict], failed: int = 0):
    run, launches = capturing_run(tests, failed)
    with (
        mock.patch.object(unreal, "editor_executable", return_value=Path("editor")),
        mock.patch.object(unreal, "_run", side_effect=run),
    ):
        try:
            return unreal.run_tests(config, None, filters), launches
        except unreal.UnrealFailure as exc:
            return exc, launches


THREE = ["Elysium.Substrate.Npc.", "Elysium.Substrate.Hint.", "Elysium.Content.Hints."]


def test_every_prefix_runs_in_one_boot_joined_with_a_plus() -> None:
    with TemporaryDirectory() as temp:
        config = stub_config(Path(temp))
        tests = [timed("Elysium.Substrate.Npc.Think"), timed("Elysium.Substrate.Hint.Claim"),
                 timed("Elysium.Content.Hints.Baked")]
        summary, launches = run_prefixes(config, THREE, tests)

        assert len(launches) == 1, "one editor boot for the whole selection"
        assert ("-ExecCmds=Automation RunTest "
                "Elysium.Substrate.Npc.+Elysium.Substrate.Hint.+Elysium.Content.Hints.;Quit"
                ) in launches[0]
        assert [row["prefix"] for row in summary["prefixes"]] == THREE
        assert [row["total"] for row in summary["prefixes"]] == [1, 1, 1]
        assert summary["executed"] == 3 and summary["failed"] == 0


def test_a_single_argument_may_join_the_prefixes_itself() -> None:
    assert unreal.resolve_test_filters("Elysium.A.+Elysium.B.") == ["Elysium.A.", "Elysium.B."]
    # Tier words resolve, repeats collapse, and an empty request is the whole suite.
    assert unreal.resolve_test_filters(["substrate", "Elysium.Substrate.", "policy"]) == [
        "Elysium.Substrate.", "Elysium.Policy."]
    assert unreal.resolve_test_filters([]) == ["Elysium."]


def test_each_prefix_is_summarised_and_a_failure_is_named_with_its_error() -> None:
    with TemporaryDirectory() as temp:
        config = stub_config(Path(temp))
        tests = [
            timed("Elysium.Substrate.Npc.Think", 0.25),
            timed("Elysium.Substrate.Npc.Fire", 0.5, failed="Expected 'a' to be 3, but it was 4."),
            timed("Elysium.Substrate.Hint.Claim", 1.0),
            timed("Elysium.Content.Hints.Baked", 2.0),
        ]
        failure, _launches = run_prefixes(config, THREE, tests, failed=1)

        assert isinstance(failure, unreal.UnrealFailure)
        # A verdict is 7, so a caller can tell "a test failed" from "the editor did not finish" (6).
        assert failure.exit_code == int(ExitCode.VALIDATION)
        assert "1 of 4 test(s) failed: Elysium.Substrate.Npc.Fire" in str(failure)
        summary = failure.summary
        npc, hint, content = summary["prefixes"]
        assert (npc["total"], npc["failed"], npc["seconds"]) == (2, 1, 0.75)
        assert (hint["total"], hint["failed"]) == (1, 0)
        assert (content["total"], content["failed"]) == (1, 0)
        assert summary["failures"] == [{
            "name": "Elysium.Substrate.Npc.Fire",
            "message": "Expected 'a' to be 3, but it was 4.",
            "where": "NpcTests.cpp:412",
        }]
        assert npc["failures"] == summary["failures"]


def test_a_prefix_that_matched_nothing_fails_even_though_the_others_ran() -> None:
    with TemporaryDirectory() as temp:
        config = stub_config(Path(temp))
        tests = [timed("Elysium.Substrate.Npc.Think"), timed("Elysium.Substrate.Hint.Claim")]
        failure, _launches = run_prefixes(config, THREE, tests)

        assert isinstance(failure, unreal.UnrealFailure)
        assert failure.exit_code == int(ExitCode.VALIDATION)
        assert "1 of 3 prefix(es) matched no test: Elysium.Content.Hints." in str(failure)
        assert [row["total"] for row in failure.summary["prefixes"]] == [1, 1, 0]


def test_a_lone_prefix_the_report_cannot_attribute_does_not_read_as_empty() -> None:
    # The overall total is the single-prefix answer; per-prefix rows that would all say "empty"
    # because the report names its tests some other way are dropped rather than shown.
    with TemporaryDirectory() as temp:
        config = stub_config(Path(temp))
        summary, _launches = run_prefixes(config, "substrate", [timed("Elysium.Content.A")])
        assert summary["executed"] == 1 and summary["prefixes"] == []


def test_a_long_filter_gets_a_capped_slug_and_a_digest_of_the_whole_filter() -> None:
    long_filter = "+".join(f"Elysium.Substrate.NpcKernelSelect{n}." for n in range(12))
    assert len(re.sub(r"[^a-z0-9]+", "-", long_filter.lower())) > 250

    slug = unreal.report_slug(long_filter)
    assert len(slug) <= unreal.TEST_REPORT_SLUG_CHARS + 1 + 8
    assert re.fullmatch(r"[a-z0-9-]{1,40}-[0-9a-f]{8}", slug)
    # The digest is of the whole filter, so two that share their first 40 characters differ.
    assert unreal.report_slug(long_filter + "+Elysium.Extra.") != slug
    # A filter that fits keeps the plain slug the retained reports have always had.
    assert unreal.report_slug("Elysium.Substrate.") == "elysium-substrate"


def test_the_report_directory_of_a_long_filter_stays_short_enough_to_open() -> None:
    with TemporaryDirectory() as temp:
        config = stub_config(Path(temp))
        filters = [f"Elysium.Substrate.NpcKernelSelect{n}." for n in range(12)]
        summary, launches = run_prefixes(
            config, filters, [timed(f"Elysium.Substrate.NpcKernelSelect{n}.A") for n in range(12)])
        report = Path(summary["report_path"])
        assert len(report.name) < 75, report.name
        assert (report / "index.json").is_file()


def test_a_killed_launch_maps_to_the_run_failed_code_not_the_toolchain_one() -> None:
    class Dying:
        def __init__(self, returncode):
            self.returncode = returncode
            self.calls = []

        def run(self, argv, **kwargs):
            self.calls.append(kwargs)
            return SimpleNamespace(returncode=self.returncode)

    config = SimpleNamespace(repo_root=Path("."), project=Path("x.uproject"))
    runner = Dying(3)
    with pytest.raises(unreal.UnrealFailure) as caught:
        unreal._run(config, runner, Path("Build.bat"), [], category=ExitCode.BUILD)
    assert caught.value.exit_code == int(ExitCode.BUILD)
    assert runner.calls == [{"cwd": Path("."), "tail_lines": None, "timeout": None,
                             "category": ExitCode.BUILD}]
    # Without a named category nothing is added, so a stand-in written for the older signature
    # keeps working and a failure takes its command's own code.
    runner = Dying(3)
    with pytest.raises(unreal.UnrealFailure) as caught:
        unreal._run(config, runner, Path("Build.bat"), [])
    assert not hasattr(caught.value, "exit_code")
    assert "category" not in runner.calls[0]
