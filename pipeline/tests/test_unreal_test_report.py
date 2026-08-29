import json
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
import unittest
from unittest import mock

import pytest

from elysium_pipeline import unreal
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
