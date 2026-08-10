import json
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

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


if __name__ == "__main__":
    unittest.main()
