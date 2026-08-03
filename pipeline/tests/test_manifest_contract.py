from __future__ import annotations

from pathlib import Path
import json
import tempfile
import unittest

from elysium_pipeline.tasking import (
    ContentDigestCache,
    Manifest,
    Task,
    TaskFailure,
    TaskGraph,
    fingerprint_content,
)


class ManifestContractTests(unittest.TestCase):
    def test_content_fingerprint_ignores_identical_rewrite_and_detects_new_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            product = root / "product"
            cache_path = root / "digests.json"
            product.write_bytes(b"first")
            cache = ContentDigestCache(cache_path)
            initial = fingerprint_content([product], cache=cache)
            self.assertEqual(fingerprint_content([product]), initial)
            cache.write()

            product.write_bytes(b"first")
            cache = ContentDigestCache(cache_path)
            self.assertEqual(fingerprint_content([product], cache=cache), initial)
            product.write_bytes(b"other")
            self.assertNotEqual(fingerprint_content([product], cache=cache), initial)

    def test_manifest_records_run_context_tool_version_and_task_dependencies(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / "product"
            manifest = Manifest(root / ".elysium-manifest.json")
            manifest.set_context(
                profile="grid",
                source_fingerprint="source",
                dependency_fingerprint="dependencies",
            )
            graph = TaskGraph(
                [
                    Task("map:test", lambda: None),
                    Task(
                        "bundle:audio",
                        lambda: output.write_text("ready", encoding="utf-8"),
                        dependencies=("map:test",),
                        fingerprint=lambda: "audio-inputs",
                        outputs=(output,),
                    ),
                ]
            )

            graph.run(manifest=manifest)
            saved = json.loads(manifest.path.read_text(encoding="utf-8"))

            self.assertEqual(saved["profile"], "grid")
            self.assertEqual(saved["source_fingerprint"], "source")
            self.assertEqual(saved["dependency_fingerprint"], "dependencies")
            self.assertTrue(saved["tool_version"])
            self.assertEqual(
                saved["tasks"]["bundle:audio"]["dependencies"],
                ["map:test"],
            )

    def test_matching_task_is_skipped_and_force_reexecutes_it(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / "product"
            manifest = Manifest(root / ".elysium-manifest.json")
            calls = 0

            def action() -> None:
                nonlocal calls
                calls += 1
                output.write_text(str(calls), encoding="utf-8")

            task = Task(
                "map:test",
                action,
                fingerprint=lambda: "same-inputs",
                outputs=(output,),
            )

            first = TaskGraph([task]).run(manifest=manifest)
            second = TaskGraph([task]).run(manifest=manifest)
            forced = TaskGraph([task]).run(manifest=manifest, force=True)

            self.assertEqual(first["map:test"].status, "ok")
            self.assertEqual(second["map:test"].status, "skipped")
            self.assertEqual(forced["map:test"].status, "ok")
            self.assertEqual(calls, 2)
            self.assertEqual(output.read_text(encoding="utf-8"), "2")

    def test_changed_fingerprint_invalidates_saved_task(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / "product"
            manifest = Manifest(root / ".elysium-manifest.json")
            fingerprint = ["one"]
            calls = 0

            def action() -> None:
                nonlocal calls
                calls += 1
                output.write_text(str(calls), encoding="utf-8")

            task = Task(
                "bundle:ui",
                action,
                fingerprint=lambda: fingerprint[0],
                outputs=(output,),
            )
            TaskGraph([task]).run(manifest=manifest)
            fingerprint[0] = "two"
            result = TaskGraph([task]).run(manifest=manifest)

            self.assertEqual(result["bundle:ui"].status, "ok")
            self.assertEqual(calls, 2)

    def test_missing_output_invalidates_saved_task(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / "product"
            manifest = Manifest(root / ".elysium-manifest.json")
            calls = 0

            def action() -> None:
                nonlocal calls
                calls += 1
                output.write_text("ready", encoding="utf-8")

            task = Task(
                "bundle:audio",
                action,
                fingerprint=lambda: "catalog",
                outputs=(output,),
            )
            TaskGraph([task]).run(manifest=manifest)
            output.unlink()
            result = TaskGraph([task]).run(manifest=manifest)

            self.assertEqual(result["bundle:audio"].status, "ok")
            self.assertEqual(calls, 2)

    def test_changed_output_inventory_invalidates_saved_task(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            first_output = root / "first"
            added_output = root / "added"
            first_output.write_text("one", encoding="utf-8")
            added_output.write_text("two", encoding="utf-8")
            manifest = Manifest(root / ".elysium-manifest.json")
            calls = 0

            def action() -> None:
                nonlocal calls
                calls += 1

            initial = Task(
                "bake:test:textures",
                action,
                fingerprint=lambda: "same-inputs",
                outputs=(first_output,),
            )
            TaskGraph([initial]).run(manifest=manifest)
            expanded = Task(
                "bake:test:textures",
                action,
                fingerprint=lambda: "same-inputs",
                outputs=(first_output, added_output),
            )
            result = TaskGraph([expanded]).run(manifest=manifest)

            self.assertEqual(result["bake:test:textures"].status, "ok")
            self.assertEqual(calls, 2)

    def test_failed_task_is_never_a_skip_candidate(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / "product"
            manifest = Manifest(root / ".elysium-manifest.json")

            task = Task(
                "map:broken",
                lambda: (_ for _ in ()).throw(RuntimeError("broken")),
                fingerprint=lambda: "inputs",
                outputs=(output,),
            )
            with self.assertRaises(TaskFailure):
                TaskGraph([task]).run(manifest=manifest)

            self.assertFalse(manifest.can_skip(task, "inputs"))


if __name__ == "__main__":
    unittest.main()
