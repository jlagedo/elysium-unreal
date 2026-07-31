from __future__ import annotations

import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from elysium_pipeline import dependencies


class DependencyLockTests(unittest.TestCase):
    def test_project_lock_preserves_pinned_contracts(self) -> None:
        repo = Path(__file__).resolve().parents[2]
        lock = dependencies.load_project_lock(repo)
        plugins = {plugin.name: plugin for plugin in lock.plugins}
        artifacts = {artifact.name: artifact for artifact in lock.artifacts}

        self.assertEqual(
            plugins["Cog"].revision,
            "cb1b435f3bb5aca41ff559863927a151b80537ac",
        )
        self.assertEqual(
            plugins["Cog"].post_patch_tree,
            "ec2512ba56301ed266c641eb9e70687fc9e0c3e6",
        )
        self.assertEqual(
            plugins["glTFRuntime"].post_patch_tree,
            "0fb2e26f5b311fc76bbbf504c341a38e24f0ce81",
        )
        self.assertEqual(
            artifacts["CPython27"].sha256,
            "0eab8de590076f74a17802d8b613ad74ec050b0081ccd0e2c7a47432c9b169b8",
        )

    def test_managed_content_hash_excludes_marker_and_build_products(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "Source").mkdir()
            (root / "Source" / "authored.cpp").write_text("one", encoding="utf-8")
            expected_line = (
                "Source/authored.cpp="
                + hashlib.sha256(b"one").hexdigest()
                + "\n"
            )
            expected = hashlib.sha256(expected_line.encode()).hexdigest()
            self.assertEqual(dependencies._content_hash(root), expected)

            (root / dependencies.MANAGED_MARKER).write_text("{}", encoding="utf-8")
            (root / "Binaries").mkdir()
            (root / "Binaries" / "generated.dll").write_bytes(b"generated")
            (root / "Intermediate").mkdir()
            (root / "Intermediate" / "generated.obj").write_bytes(b"generated")
            self.assertEqual(dependencies._content_hash(root), expected)

    def test_plugin_destination_cannot_escape_managed_root(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            (repo / "Plugins" / "External").mkdir(parents=True)
            accepted = dependencies._managed_plugin_destination(
                repo, "Plugins/External/Cog"
            )
            self.assertEqual(accepted, (repo / "Plugins/External/Cog").resolve())
            with self.assertRaises(dependencies.DependencyError):
                dependencies._managed_plugin_destination(repo, "Content/Cog")

    def test_artifact_marker_detects_local_modification(self) -> None:
        artifact = dependencies.ArtifactLock(
            name="CPython27",
            version="test",
            url="https://example.invalid/python.tar.gz",
            sha256="abc",
            destination="Source/ThirdParty/CPython27",
        )
        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary)
            (destination / "bin").mkdir()
            library = destination / "bin" / "python27.dll"
            library.write_bytes(b"initial")
            content_hash = dependencies._content_hash(destination)
            (destination / dependencies.MANAGED_MARKER).write_text(
                json.dumps(
                    {
                        "name": artifact.name,
                        "version": artifact.version,
                        "url": artifact.url,
                        "sha256": artifact.sha256,
                        "content_hash": content_hash,
                    }
                ),
                encoding="utf-8",
            )
            self.assertTrue(dependencies._artifact_matches(artifact, destination))
            library.write_bytes(b"modified")
            self.assertFalse(dependencies._artifact_matches(artifact, destination))


if __name__ == "__main__":
    unittest.main()
