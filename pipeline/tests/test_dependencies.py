from __future__ import annotations

import hashlib
import json
from pathlib import Path
import tempfile
import unittest

import pytest

from elysium_pipeline import dependencies


def test_managed_content_hash_excludes_marker_and_build_products() -> None:
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
        assert dependencies._content_hash(root) == expected

        (root / dependencies.MANAGED_MARKER).write_text("{}", encoding="utf-8")
        (root / "Binaries").mkdir()
        (root / "Binaries" / "generated.dll").write_bytes(b"generated")
        (root / "Intermediate").mkdir()
        (root / "Intermediate" / "generated.obj").write_bytes(b"generated")
        assert dependencies._content_hash(root) == expected


def test_managed_tree_cleanup_handles_read_only_git_objects() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        staging = Path(temporary) / "Cog.__fetch"
        git_object = staging / ".git" / "objects" / "ab" / "object"
        git_object.parent.mkdir(parents=True)
        git_object.write_bytes(b"git object")
        git_object.chmod(0o444)

        dependencies._remove_tree(staging)

        assert not staging.exists()


def test_plugin_destination_cannot_escape_managed_root() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        repo = Path(temporary)
        (repo / "Plugins" / "External").mkdir(parents=True)
        accepted = dependencies._managed_plugin_destination(
            repo, "Plugins/External/Cog"
        )
        assert accepted == (repo / "Plugins/External/Cog").resolve()
        with pytest.raises(dependencies.DependencyError):
            dependencies._managed_plugin_destination(repo, "Content/Cog")


def test_artifact_marker_detects_local_modification() -> None:
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
        assert dependencies._artifact_matches(artifact, destination)
        library.write_bytes(b"modified")
        assert not dependencies._artifact_matches(artifact, destination)
