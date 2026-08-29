from __future__ import annotations

from pathlib import Path
import hashlib
import json
import os
import tempfile
import time
import unittest
from unittest import mock

from elysium_pipeline.tasking import (
    ContentDigestCache,
    Manifest,
    Task,
    TaskFailure,
    TaskGraph,
    fingerprint_content,
)
import pytest


def test_content_fingerprint_ignores_identical_rewrite_and_detects_new_bytes() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        product = root / "product"
        cache_path = root / "digests.json"
        product.write_bytes(b"first")
        cache = ContentDigestCache(cache_path)
        initial = fingerprint_content([product], cache=cache)
        assert fingerprint_content([product]) == initial
        cache.write()

        product.write_bytes(b"first")
        cache = ContentDigestCache(cache_path)
        assert fingerprint_content([product], cache=cache) == initial
        product.write_bytes(b"other")
        assert fingerprint_content([product], cache=cache) != initial


def test_manifest_records_run_context_tool_version_and_task_dependencies() -> None:
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

        assert saved["profile"] == "grid"
        assert saved["source_fingerprint"] == "source"
        assert saved["dependency_fingerprint"] == "dependencies"
        assert saved["tool_version"]
        assert saved["tasks"]["bundle:audio"]["dependencies"] == ["map:test"]


def test_matching_task_is_skipped_and_force_reexecutes_it() -> None:
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

        assert first["map:test"].status == "ok"
        assert second["map:test"].status == "skipped"
        assert forced["map:test"].status == "ok"
        assert calls == 2
        assert output.read_text(encoding="utf-8") == "2"


def test_changed_fingerprint_invalidates_saved_task() -> None:
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

        assert result["bundle:ui"].status == "ok"
        assert calls == 2


def test_missing_output_invalidates_saved_task() -> None:
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

        assert result["bundle:audio"].status == "ok"
        assert calls == 2


def test_changed_output_inventory_invalidates_saved_task() -> None:
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

        assert result["bake:test:textures"].status == "ok"
        assert calls == 2


def test_failed_task_is_never_a_skip_candidate() -> None:
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
        with pytest.raises(TaskFailure):
            TaskGraph([task]).run(manifest=manifest)

        assert not manifest.can_skip(task, "inputs")


def test_task_failure_still_persists_the_records_of_completed_tasks() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        output = root / "product"
        manifest = Manifest(root / ".elysium-manifest.json")

        def fail() -> None:
            raise RuntimeError("synthetic failure")

        graph = TaskGraph(
            [
                Task(
                    "map:good",
                    lambda: output.write_text("ready", encoding="utf-8"),
                    fingerprint=lambda: "inputs",
                    outputs=(output,),
                ),
                Task("zz:fail", fail, dependencies=("map:good",)),
            ]
        )
        with pytest.raises(TaskFailure):
            graph.run(manifest=manifest)

        saved = json.loads(manifest.path.read_text(encoding="utf-8"))
        assert saved["tasks"]["map:good"]["status"] == "complete"
        assert saved["tasks"]["zz:fail"]["status"] == "failed"


def test_graph_run_batches_receipts_into_a_single_flush() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        manifest = Manifest(root / ".elysium-manifest.json")
        graph = TaskGraph([Task(f"map:{name}", lambda: None) for name in "abc"])

        with mock.patch.object(manifest, "write", wraps=manifest.write) as writes:
            graph.run(manifest=manifest)

        assert writes.call_count == 1
        saved = json.loads(manifest.path.read_text(encoding="utf-8"))
        assert set(saved["tasks"]) == {"map:a", "map:b", "map:c"}


def test_skip_run_still_records_and_persists_the_receipt() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        output = root / "product"
        manifest_path = root / ".elysium-manifest.json"
        task = Task(
            "map:test",
            lambda: output.write_text("ready", encoding="utf-8"),
            fingerprint=lambda: "same-inputs",
            outputs=(output,),
        )
        TaskGraph([task]).run(manifest=Manifest(manifest_path))

        # A stale marker planted in the receipt proves the skip run rewrites it.
        saved = json.loads(manifest_path.read_text(encoding="utf-8"))
        saved["tasks"]["map:test"]["error"] = "stale-marker"
        manifest_path.write_text(json.dumps(saved), encoding="utf-8")

        result = TaskGraph([task]).run(manifest=Manifest(manifest_path))

        assert result["map:test"].status == "skipped"
        rewritten = json.loads(manifest_path.read_text(encoding="utf-8"))
        assert rewritten["tasks"]["map:test"]["error"] is None


def test_digest_cache_tolerates_entries_with_extra_or_malformed_metadata() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        product = root / "product"
        product.write_bytes(b"payload")
        expected = hashlib.sha256(b"payload").hexdigest()
        settled = time.time_ns() - 2 * ContentDigestCache.RACY_WINDOW_NS
        os.utime(product, ns=(settled, settled))
        stat = product.stat()
        key = str(product.resolve())
        cache_path = root / "digests.json"
        cache_path.write_text(json.dumps({
            "schema": 1,
            "entries": {
                key: {
                    "size": stat.st_size,
                    "mtime_ns": stat.st_mtime_ns,
                    "ctime_ns": 123,
                    "sha256": expected,
                },
            },
        }), encoding="utf-8")

        # An entry carrying metadata beyond the identity still hits without a re-hash.
        cache = ContentDigestCache(cache_path)
        assert cache.digest(product) == expected
        assert not cache.dirty

        # An entry that is not a mapping is a plain miss that re-hashes once.
        cache.entries[key] = ["not", "a", "mapping"]
        assert cache.digest(product) == expected
        assert cache.dirty
        assert "ctime_ns" not in cache.entries[key]


def test_digest_cache_write_keeps_entries_another_instance_added() -> None:
    # The parent process and an editor commandlet hash into one store; the later
    # writer overlays its entries rather than replacing the document.
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        settled = time.time_ns() - 2 * ContentDigestCache.RACY_WINDOW_NS
        first, second = root / "first", root / "second"
        for product in (first, second):
            product.write_bytes(product.name.encode())
            os.utime(product, ns=(settled, settled))
        store = root / "digests.json"
        parent = ContentDigestCache(store)
        parent.digest(first)
        child = ContentDigestCache(store)
        child.digest(second)
        child.write()
        parent.write()
        reloaded = ContentDigestCache(store)
        assert set(reloaded.entries) == {str(first.resolve()), str(second.resolve())}


def test_digest_cache_hashes_a_racy_file_and_leaves_it_unrecorded() -> None:
    # A file inside the racy window can be rewritten at the same size within one
    # timestamp tick, so its digest is computed every time and only settles into the
    # store once the window has passed.
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        product = root / "product"
        product.write_bytes(b"first!")
        cache = ContentDigestCache(root / "digests.json")
        assert cache.digest(product) == hashlib.sha256(b"first!").hexdigest()
        assert not cache.dirty
        product.write_bytes(b"second")
        assert cache.digest(product) == hashlib.sha256(b"second").hexdigest()
        assert not cache.dirty

        settled = time.time_ns() - 2 * ContentDigestCache.RACY_WINDOW_NS
        os.utime(product, ns=(settled, settled))
        assert cache.digest(product) == hashlib.sha256(b"second").hexdigest()
        assert cache.dirty
        assert cache.entries[str(product.resolve())]["sha256"] == hashlib.sha256(b"second").hexdigest()
