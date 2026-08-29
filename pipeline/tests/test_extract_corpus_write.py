"""`UE_extract_corpus._write` mtime contract: an unchanged document is not rewritten.

Downstream caching fingerprints `shared/manifest.json` and `shared/materials.json` through a
stat-keyed digest cache, so a byte-identical rewrite must not disturb the file's mtime.
"""

from __future__ import annotations

import os
from pathlib import Path
import tempfile
import unittest

from elysium_pipeline.exporters import UE_extract_corpus as corpus


class WriteSkipsUnchangedPayload(unittest.TestCase):
    def test_unchanged_payload_leaves_mtime(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "shared", "manifest.json")
            document = {"b": 2, "a": 1}

            corpus._write(path, document)
            # Windows mtime resolution can be coarse enough that two writes in quick succession
            # land on the same tick even without the skip; back the clock up explicitly so an
            # unchanged mtime is proof the second call skipped the write, not a timing accident.
            older = os.stat(path).st_mtime_ns - 5_000_000_000
            os.utime(path, ns=(older, older))
            before = os.stat(path).st_mtime_ns

            corpus._write(path, document)

            after = os.stat(path).st_mtime_ns
            assert before == after

    def test_changed_payload_rewrites_content(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "shared", "materials.json")

            corpus._write(path, {"a": 1})
            older = os.stat(path).st_mtime_ns - 5_000_000_000
            os.utime(path, ns=(older, older))
            before = os.stat(path).st_mtime_ns

            corpus._write(path, {"a": 2})

            after = os.stat(path).st_mtime_ns
            assert before != after
            assert Path(path).read_text(encoding="utf-8") == '{"a":2}'
