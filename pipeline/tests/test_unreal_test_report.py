import json
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
import unittest
from unittest import mock

from elysium_pipeline import unreal
from elysium_pipeline.unreal import TEST_ABSTENTION_TOKEN, summarize_test_report


class UnrealTestReportTests(unittest.TestCase):
    def summarize(self, tests: list[dict]) -> dict:
        with TemporaryDirectory() as temp:
            report = Path(temp)
            (report / "index.json").write_text(
                json.dumps({"tests": tests, "failed": 0, "totalDuration": 1.25}),
                encoding="utf-8",
            )
            return summarize_test_report(report)

    @staticmethod
    def make_entry(name: str, message: str = "") -> dict:
        entries = [] if not message else [{"event": {"message": message}}]
        return {"fullTestPath": name, "entries": entries}

    def test_explicit_abstention_is_not_counted_as_execution(self) -> None:
        summary = self.summarize([
            self.make_entry("Elysium.Content.Corpus", f"{TEST_ABSTENTION_TOKEN}: npc missing"),
            self.make_entry("Elysium.Content.Pure"),
        ])

        self.assertEqual(summary["total"], 2)
        self.assertEqual(summary["executed"], 1)
        self.assertEqual(summary["abstained"], 1)
        self.assertEqual(summary["abstentions"], ["Elysium.Content.Corpus"])

    def test_legacy_incomplete_marker_remains_visible(self) -> None:
        summary = self.summarize([
            self.make_entry("Elysium.Content.Legacy", "npc export domain is marked incomplete")
        ])

        self.assertEqual(summary["executed"], 0)
        self.assertEqual(summary["abstained"], 1)

    def test_unrelated_skip_word_does_not_abstain_the_test(self) -> None:
        summary = self.summarize([
            self.make_entry("Elysium.Content.Partial", "one optional comparison was skipped")
        ])

        self.assertEqual(summary["executed"], 1)
        self.assertEqual(summary["abstained"], 0)

    def test_each_run_retains_its_report_below_the_work_root(self) -> None:
        with TemporaryDirectory() as temp:
            root = Path(temp)
            config = SimpleNamespace(
                work_root=root / "work",
                export_root=root / "exports",
                project=root / "repo" / "ElysiumUE.uproject",
            )

            def write_report(_config, _runner, _executable, arguments) -> None:
                switch = next(
                    value for value in arguments if str(value).startswith("-ReportExportPath=")
                )
                report = Path(str(switch).split("=", 1)[1])
                report.mkdir(parents=True)
                (report / "index.json").write_text(
                    json.dumps({"tests": [], "failed": 0, "totalDuration": 0}),
                    encoding="utf-8",
                )

            with (
                mock.patch.object(unreal, "editor_executable", return_value=Path("editor")),
                mock.patch.object(unreal, "_run", side_effect=write_report),
            ):
                first = unreal.run_tests(config, None, "Substrate")
                second = unreal.run_tests(config, None, "Substrate")

            first_path = Path(first["report_path"])
            second_path = Path(second["report_path"])
            report_root = (config.work_root / "reports" / "tests").resolve()
            self.assertTrue(first_path.is_relative_to(report_root))
            self.assertTrue(second_path.is_relative_to(report_root))
            self.assertNotEqual(first_path, second_path)
            self.assertFalse(first_path.is_relative_to(config.export_root.resolve()))


if __name__ == "__main__":
    unittest.main()
