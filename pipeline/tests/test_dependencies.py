from __future__ import annotations

from dataclasses import asdict, replace
import hashlib
import io
import json
from pathlib import Path
import tarfile
import tempfile
import tomllib
import unittest
from unittest import mock

from elysium_pipeline import dependencies


class DependencyLockTests(unittest.TestCase):
    def _fixture_package(
        self,
        repo: Path,
        *,
        include_license: bool = True,
    ) -> tuple[dependencies.SourcePackageLock, Path]:
        source = repo / "fixture-source" / "fixture-rev"
        (source / "include").mkdir(parents=True)
        (source / "include" / "fixture.h").write_text(
            "#define FIXTURE_VALUE 7\n",
            encoding="utf-8",
        )
        if include_license:
            (source / "LICENSE.txt").write_text("fixture license\n", encoding="utf-8")

        archive_path = repo / "fixture.tar.gz"
        with tarfile.open(archive_path, "w:gz") as archive:
            archive.add(source, arcname="fixture-rev")
        package = dependencies.SourcePackageLock(
            name="FixtureSource",
            repository="https://example.invalid/fixture.git",
            revision="fixture-rev",
            archive_url=archive_path.resolve().as_uri(),
            archive_sha256=hashlib.sha256(archive_path.read_bytes()).hexdigest(),
            destination=(
                "research/tooling/capture/native/third_party/FixtureSource"
            ),
            source_subdirectory="fixture-rev",
            license_file="LICENSE.txt",
            content_tree_sha256=dependencies._source_tree_hash(source),
        )
        return package, archive_path

    @staticmethod
    def _source_lock(
        package: dependencies.SourcePackageLock,
    ) -> dependencies.DependencyLock:
        return dependencies.DependencyLock(
            schema=2,
            plugins=(),
            artifacts=(),
            source_packages=(package,),
        )

    @staticmethod
    def _write_source_lock(
        repo: Path,
        package: dependencies.SourcePackageLock,
    ) -> None:
        lock_path = repo / "dev" / "dependencies.lock.json"
        lock_path.parent.mkdir(parents=True)
        lock_path.write_text(
            json.dumps(
                {
                    "schema": 2,
                    "plugins": [],
                    "artifacts": [],
                    "source_packages": [asdict(package)],
                },
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )

    def test_project_lock_preserves_pinned_contracts(self) -> None:
        repo = Path(__file__).resolve().parents[2]
        lock = dependencies.load_project_lock(repo)
        plugins = {plugin.name: plugin for plugin in lock.plugins}
        artifacts = {artifact.name: artifact for artifact in lock.artifacts}
        source_packages = {package.name: package for package in lock.source_packages}

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
        self.assertEqual(lock.schema, 2)
        self.assertEqual(
            source_packages["FlatBuffers"].revision,
            "7e163021e59cca4f8e1e35a7c828b5c6b7915953",
        )
        self.assertEqual(
            source_packages["FlatBuffers"].archive_sha256,
            "4236c5d22309abeac2384d3800febcc8d60423e8cb2f573abfa8f798352c4536",
        )
        self.assertEqual(source_packages["FlatBuffers"].license_file, "LICENSE")

    def test_flatbuffers_python_runtime_is_exactly_locked(self) -> None:
        repo = Path(__file__).resolve().parents[2]
        project = tomllib.loads((repo / "pyproject.toml").read_text(encoding="utf-8"))
        self.assertIn(
            "flatbuffers==25.12.19",
            project["project"]["dependencies"],
        )
        lock = tomllib.loads((repo / "uv.lock").read_text(encoding="utf-8"))
        package = next(
            item for item in lock["package"] if item["name"] == "flatbuffers"
        )
        self.assertEqual(package["version"], "25.12.19")

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

    def test_source_package_sync_and_check_use_local_archive_without_network(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            package, _archive_path = self._fixture_package(repo)
            self._write_source_lock(repo, package)
            original_urlopen = dependencies.urllib.request.urlopen

            def local_only_urlopen(url: str, *args: object, **kwargs: object):
                self.assertTrue(str(url).startswith("file:"), url)
                return original_urlopen(url, *args, **kwargs)

            with mock.patch.object(
                dependencies.urllib.request,
                "urlopen",
                side_effect=local_only_urlopen,
            ):
                ready = dependencies.sync_dependencies(repo)

            self.assertEqual(ready, ["FixtureSource"])
            lock = dependencies.load_project_lock(repo)
            destination = repo / package.destination
            self.assertEqual(
                (destination / "include" / "fixture.h").read_text(encoding="utf-8"),
                "#define FIXTURE_VALUE 7\n",
            )
            self.assertTrue((destination / "LICENSE.txt").is_file())
            self.assertTrue((destination / dependencies.MANAGED_MARKER).is_file())
            self.assertEqual(dependencies.check_dependencies(repo, lock), [])

    def test_source_package_check_detects_tamper(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            package, _archive_path = self._fixture_package(repo)
            lock = self._source_lock(package)
            dependencies.sync_source_packages(repo, lock)
            destination = repo / package.destination
            (destination / "include" / "fixture.h").write_text(
                "tampered\n",
                encoding="utf-8",
            )

            self.assertEqual(
                dependencies.check_dependencies(repo, lock),
                [
                    dependencies.DependencyIssue(
                        "FixtureSource",
                        "managed source package is stale or modified",
                    )
                ],
            )

    def test_source_package_bad_archive_hash_does_not_replace_destination(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            package, _archive_path = self._fixture_package(repo)
            lock = self._source_lock(package)
            dependencies.sync_source_packages(repo, lock)
            destination = repo / package.destination
            header = destination / "include" / "fixture.h"

            bad_package = replace(package, archive_sha256="0" * 64)
            with self.assertRaisesRegex(dependencies.DependencyError, "SHA-256 mismatch"):
                dependencies.sync_source_packages(
                    repo,
                    self._source_lock(bad_package),
                )
            self.assertEqual(
                header.read_text(encoding="utf-8"),
                "#define FIXTURE_VALUE 7\n",
            )

    def test_source_package_bad_content_tree_does_not_replace_destination(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            package, _archive_path = self._fixture_package(repo)
            lock = self._source_lock(package)
            dependencies.sync_source_packages(repo, lock)
            destination = repo / package.destination
            header = destination / "include" / "fixture.h"

            bad_package = replace(package, content_tree_sha256="0" * 64)
            with self.assertRaisesRegex(dependencies.DependencyError, "content tree mismatch"):
                dependencies.sync_source_packages(
                    repo,
                    self._source_lock(bad_package),
                )
            self.assertEqual(
                header.read_text(encoding="utf-8"),
                "#define FIXTURE_VALUE 7\n",
            )

    def test_source_package_rejects_unsafe_destination_and_archive_member(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            package, _archive_path = self._fixture_package(repo)
            unsafe_destination = replace(package, destination="research/escaped")
            with self.assertRaisesRegex(
                dependencies.DependencyError,
                "outside.*third_party",
            ):
                dependencies.sync_source_packages(
                    repo,
                    self._source_lock(unsafe_destination),
                )

            unsafe_archive = repo / "unsafe.tar.gz"
            payload = b"escaped"
            with tarfile.open(unsafe_archive, "w:gz") as archive:
                member = tarfile.TarInfo("../escape.txt")
                member.size = len(payload)
                archive.addfile(member, io.BytesIO(payload))
            unsafe_package = replace(
                package,
                archive_url=unsafe_archive.resolve().as_uri(),
                archive_sha256=hashlib.sha256(unsafe_archive.read_bytes()).hexdigest(),
            )
            with self.assertRaisesRegex(
                dependencies.DependencyError,
                "escapes extraction root",
            ):
                dependencies.sync_source_packages(
                    repo,
                    self._source_lock(unsafe_package),
                )
            self.assertFalse((repo / "escape.txt").exists())

    def test_source_archive_materializes_internal_links_and_rejects_escape(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive_path = root / "links.tar.gz"
            payload = b"shared source\n"
            with tarfile.open(archive_path, "w:gz") as archive:
                target = tarfile.TarInfo("package/shared.txt")
                target.size = len(payload)
                archive.addfile(target, io.BytesIO(payload))
                link = tarfile.TarInfo("package/materialized.txt")
                link.type = tarfile.SYMTYPE
                link.linkname = "shared.txt"
                archive.addfile(link)

            extraction = root / "extract"
            extraction.mkdir()
            dependencies._safe_extract_source_archive(archive_path, extraction)
            materialized = extraction / "package" / "materialized.txt"
            self.assertFalse(materialized.is_symlink())
            self.assertEqual(materialized.read_bytes(), payload)

            unsafe_path = root / "unsafe-link.tar.gz"
            with tarfile.open(unsafe_path, "w:gz") as archive:
                link = tarfile.TarInfo("package/escape.txt")
                link.type = tarfile.SYMTYPE
                link.linkname = "../../escape.txt"
                archive.addfile(link)
            with self.assertRaisesRegex(
                dependencies.DependencyError,
                "link escapes extraction root",
            ):
                dependencies._safe_extract_source_archive(
                    unsafe_path,
                    root / "unsafe-extract",
                )

    def test_source_package_requires_and_checks_license(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            package, _archive_path = self._fixture_package(repo)
            lock = self._source_lock(package)
            dependencies.sync_source_packages(repo, lock)
            (repo / package.destination / "LICENSE.txt").unlink()
            self.assertEqual(
                dependencies.check_dependencies(repo, lock),
                [
                    dependencies.DependencyIssue(
                        "FixtureSource",
                        "managed source package license is missing",
                    )
                ],
            )

        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            unlicensed, _archive_path = self._fixture_package(
                repo,
                include_license=False,
            )
            with self.assertRaisesRegex(
                dependencies.DependencyError,
                "missing license file",
            ):
                dependencies.sync_source_packages(
                    repo,
                    self._source_lock(unlicensed),
                )


if __name__ == "__main__":
    unittest.main()
