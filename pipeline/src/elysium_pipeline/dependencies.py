"""Lock-driven external plugin and embedded-runtime dependency management."""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import shutil
import tarfile
import tempfile
from typing import Any, Iterable
import urllib.request
import zipfile

from elysium_pipeline.process import run_process
from elysium_pipeline.reporting import ExitCode


MANAGED_MARKER = ".elysium-managed.json"
CPYTHON_GITIGNORE = """\
# The CPython 2.7 SDK is open source, fetched, and regenerable with:
#   uv run elysium deps sync
# Keep only the provenance text and this policy file tracked.
/include/
/libs/
/bin/
/PythonHome/
/_extract/
/.elysium-managed.json
!VERSION.txt
!.gitignore
"""


class DependencyError(RuntimeError):
    """A lock, download, patch, or managed-tree contract failed."""


@dataclass(frozen=True, slots=True)
class PluginLock:
    name: str
    repository: str
    revision: str
    destination: str
    source_subdirectory: str
    patch: str | None
    post_patch_tree: str | None


@dataclass(frozen=True, slots=True)
class ArtifactLock:
    name: str
    version: str
    url: str
    sha256: str
    destination: str


@dataclass(frozen=True, slots=True)
class SourcePackageLock:
    name: str
    repository: str
    revision: str
    archive_url: str
    archive_sha256: str
    destination: str
    source_subdirectory: str
    license_file: str
    content_tree_sha256: str


@dataclass(frozen=True, slots=True)
class DependencyLock:
    schema: int
    plugins: tuple[PluginLock, ...]
    artifacts: tuple[ArtifactLock, ...]
    source_packages: tuple[SourcePackageLock, ...]

    @classmethod
    def load(cls, path: Path) -> "DependencyLock":
        try:
            raw = json.loads(path.read_text(encoding="utf-8-sig"))
        except (OSError, ValueError) as error:
            raise DependencyError(f"cannot read dependency lock {path}: {error}") from error
        if raw.get("schema") != 2:
            raise DependencyError(
                f"unsupported dependency lock schema: {raw.get('schema')!r}"
            )
        try:
            plugins = tuple(
                PluginLock(
                    name=item["name"],
                    repository=item["repository"],
                    revision=item["revision"],
                    destination=item["destination"],
                    source_subdirectory=item.get("source_subdirectory", "."),
                    patch=item.get("patch"),
                    post_patch_tree=item.get("post_patch_tree"),
                )
                for item in raw.get("plugins", [])
            )
            artifacts = tuple(ArtifactLock(**item) for item in raw.get("artifacts", []))
            source_packages = tuple(
                SourcePackageLock(**item) for item in raw.get("source_packages", [])
            )
        except (KeyError, TypeError) as error:
            raise DependencyError(f"invalid dependency lock {path}: {error}") from error
        for package in source_packages:
            _validate_source_package_lock(package)
        names = [package.name.casefold() for package in source_packages]
        destinations = [package.destination.casefold() for package in source_packages]
        if len(names) != len(set(names)):
            raise DependencyError(f"duplicate source-package name in dependency lock {path}")
        if len(destinations) != len(set(destinations)):
            raise DependencyError(
                f"duplicate source-package destination in dependency lock {path}"
            )
        return cls(
            schema=2,
            plugins=plugins,
            artifacts=artifacts,
            source_packages=source_packages,
        )


@dataclass(frozen=True, slots=True)
class DependencyIssue:
    name: str
    detail: str


def _inside(path: Path, root: Path) -> bool:
    try:
        path.resolve().relative_to(root.resolve())
    except ValueError:
        return False
    return path.resolve() != root.resolve()


def _managed_plugin_destination(repo_root: Path, relative: str) -> Path:
    managed_root = (repo_root / "Plugins" / "External").resolve()
    destination = (repo_root / Path(*Path(relative).parts)).resolve()
    if not _inside(destination, managed_root):
        raise DependencyError(
            f"refusing to manage plugin outside {managed_root}: {destination}"
        )
    return destination


def _managed_artifact_destination(repo_root: Path, relative: str) -> Path:
    destination = (repo_root / Path(*Path(relative).parts)).resolve()
    if not _inside(destination, repo_root):
        raise DependencyError(
            f"refusing to manage artifact outside {repo_root}: {destination}"
        )
    return destination


def _managed_source_package_destination(repo_root: Path, relative: str) -> Path:
    managed_root = (
        repo_root / "research" / "tooling" / "capture" / "native" / "third_party"
    ).resolve()
    destination = (repo_root / Path(*Path(relative).parts)).resolve()
    if not _inside(destination, managed_root):
        raise DependencyError(
            f"refusing to manage source package outside {managed_root}: {destination}"
        )
    return destination


def _content_hash(directory: Path) -> str:
    lines: list[str] = []
    for path in directory.rglob("*"):
        if not path.is_file() or path.name == MANAGED_MARKER:
            continue
        relative = path.relative_to(directory)
        if relative.parts and relative.parts[0] in {"Binaries", "Intermediate"}:
            continue
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        lines.append(f"{relative.as_posix()}={digest}")
    lines.sort()
    return hashlib.sha256(("\n".join(lines) + "\n").encode("utf-8")).hexdigest()


def _source_tree_hash(directory: Path) -> str:
    lines: list[str] = []
    for path in directory.rglob("*"):
        relative = path.relative_to(directory)
        if relative == Path(MANAGED_MARKER):
            continue
        if path.is_symlink() or (hasattr(path, "is_junction") and path.is_junction()):
            lines.append(f"{relative.as_posix()}=<link>")
        elif path.is_dir():
            lines.append(f"{relative.as_posix()}/")
        elif path.is_file():
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            lines.append(f"{relative.as_posix()}={digest}")
    lines.sort()
    return hashlib.sha256(("\n".join(lines) + "\n").encode("utf-8")).hexdigest()


def _read_marker(path: Path) -> dict[str, Any] | None:
    try:
        value = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, ValueError):
        return None
    return value if isinstance(value, dict) else None


def _write_marker(destination: Path, data: dict[str, Any]) -> None:
    (destination / MANAGED_MARKER).write_text(
        json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


def _git(repo_root: Path, working_directory: Path, *arguments: str) -> str:
    result = run_process(
        ["git", "-C", working_directory, *arguments],
        cwd=repo_root,
        output_sink=lambda _line: None,
        category=int(ExitCode.DEPENDENCY_OR_TOOLCHAIN),
    )
    return result.output.strip()


def _plugin_matches(plugin: PluginLock, destination: Path) -> bool:
    marker = _read_marker(destination / MANAGED_MARKER)
    if marker is None:
        return False
    if marker.get("revision") != plugin.revision:
        return False
    if plugin.post_patch_tree and marker.get("post_patch_tree") != plugin.post_patch_tree:
        return False
    expected_hash = marker.get("content_hash")
    return bool(expected_hash) and expected_hash == _content_hash(destination)


def sync_plugins(repo_root: Path, lock: DependencyLock) -> list[str]:
    """Restore each patched plugin at its exact locked post-patch tree."""

    ready: list[str] = []
    (repo_root / "Plugins" / "External").mkdir(parents=True, exist_ok=True)
    for plugin in lock.plugins:
        destination = _managed_plugin_destination(repo_root, plugin.destination)
        staging = destination.with_name(destination.name + ".__fetch")
        _managed_plugin_destination(
            repo_root, staging.relative_to(repo_root.resolve()).as_posix()
        )
        if destination.is_dir() and _plugin_matches(plugin, destination):
            ready.append(plugin.name)
            continue
        if staging.exists():
            shutil.rmtree(staging)
        staging.mkdir(parents=True)
        try:
            _git(repo_root, staging, "init", "--quiet")
            _git(repo_root, staging, "remote", "add", "origin", plugin.repository)
            _git(
                repo_root,
                staging,
                "fetch",
                "--quiet",
                "--depth",
                "1",
                "origin",
                plugin.revision,
            )
            _git(repo_root, staging, "checkout", "--quiet", "--detach", "FETCH_HEAD")
            if plugin.patch:
                patch = (repo_root / plugin.patch).resolve()
                if not patch.is_file() or not _inside(patch, repo_root):
                    raise DependencyError(
                        f"{plugin.name} patch is missing or outside the repository: {patch}"
                    )
                _git(repo_root, staging, "apply", "--whitespace=nowarn", os.fspath(patch))
            _git(repo_root, staging, "add", "-A")
            root_tree = _git(repo_root, staging, "write-tree")
            subtree = plugin.source_subdirectory or "."
            tree = (
                root_tree
                if subtree == "."
                else _git(repo_root, staging, "rev-parse", f"{root_tree}:{subtree}")
            )
            if plugin.post_patch_tree and tree != plugin.post_patch_tree:
                raise DependencyError(
                    f"{plugin.name} post-patch tree mismatch: got {tree}, "
                    f"expected {plugin.post_patch_tree}"
                )
            source = staging if subtree == "." else staging / Path(*Path(subtree).parts)
            if not source.is_dir():
                raise DependencyError(
                    f"{plugin.name} source subdirectory is missing: {subtree}"
                )
            if subtree == ".":
                shutil.rmtree(staging / ".git")
            if destination.exists():
                shutil.rmtree(destination)
            if subtree == ".":
                staging.rename(destination)
            else:
                source.rename(destination)
                shutil.rmtree(staging)
            content_hash = _content_hash(destination)
            _write_marker(
                destination,
                {
                    "name": plugin.name,
                    "repository": plugin.repository,
                    "revision": plugin.revision,
                    "post_patch_tree": tree,
                    "content_hash": content_hash,
                },
            )
            ready.append(plugin.name)
        except Exception:
            if staging.exists():
                shutil.rmtree(staging)
            raise
    return ready


def _safe_extract(archive: tarfile.TarFile, destination: Path) -> None:
    destination = destination.resolve()
    members = archive.getmembers()
    ordinary_members: list[tarfile.TarInfo] = []
    links: list[tuple[tarfile.TarInfo, Path, Path]] = []
    for member in members:
        target = (destination / member.name).resolve()
        if target != destination and not _inside(target, destination):
            raise DependencyError(f"archive member escapes extraction root: {member.name}")
        if member.issym() or member.islnk():
            if "\\" in member.linkname:
                raise DependencyError(
                    f"archive link uses an unsafe path separator: {member.name}"
                )
            link_target = (
                (target.parent / member.linkname).resolve()
                if member.issym()
                else (destination / member.linkname).resolve()
            )
            if link_target != destination and not _inside(link_target, destination):
                raise DependencyError(
                    f"archive link escapes extraction root: {member.name}"
                )
            links.append((member, target, link_target))
        else:
            ordinary_members.append(member)
    archive.extractall(destination, members=ordinary_members, filter="data")
    for member, target, link_target in links:
        if not link_target.exists():
            raise DependencyError(
                f"archive link target is missing: {member.name} -> {member.linkname}"
            )
        if target.exists():
            raise DependencyError(f"archive link target path already exists: {member.name}")
        target.parent.mkdir(parents=True, exist_ok=True)
        if link_target.is_dir():
            shutil.copytree(link_target, target)
        elif link_target.is_file():
            shutil.copy2(link_target, target)
        else:
            raise DependencyError(
                f"archive link target is unsupported: {member.name} -> {member.linkname}"
            )


def _safe_extract_zip(archive: zipfile.ZipFile, destination: Path) -> None:
    destination = destination.resolve()
    for member in archive.infolist():
        if "\\" in member.filename:
            raise DependencyError(
                f"archive member uses an unsafe path separator: {member.filename}"
            )
        target = (destination / member.filename).resolve()
        if target != destination and not _inside(target, destination):
            raise DependencyError(
                f"archive member escapes extraction root: {member.filename}"
            )
        unix_mode = member.external_attr >> 16
        if (unix_mode & 0o170000) == 0o120000:
            raise DependencyError(f"archive links are not accepted: {member.filename}")
        if member.is_dir():
            target.mkdir(parents=True, exist_ok=True)
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        with archive.open(member, "r") as source, target.open("wb") as output:
            shutil.copyfileobj(source, output)


def _safe_extract_source_archive(archive_path: Path, destination: Path) -> None:
    if tarfile.is_tarfile(archive_path):
        with tarfile.open(archive_path, mode="r:*") as archive:
            _safe_extract(archive, destination)
        return
    if zipfile.is_zipfile(archive_path):
        with zipfile.ZipFile(archive_path) as archive:
            _safe_extract_zip(archive, destination)
        return
    raise DependencyError(f"unsupported source-package archive: {archive_path}")


def _safe_package_relative_path(value: str, field: str, *, allow_root: bool) -> Path:
    if not value or "\\" in value:
        raise DependencyError(f"source package {field} must be a POSIX relative path")
    if allow_root and value == ".":
        return Path(".")
    parts = value.split("/")
    if any(part in {"", ".", ".."} or ":" in part for part in parts):
        raise DependencyError(f"source package {field} is unsafe: {value}")
    return Path(*parts)


def _validate_source_package_lock(package: SourcePackageLock) -> None:
    string_fields = {
        "name": package.name,
        "repository": package.repository,
        "revision": package.revision,
        "archive_url": package.archive_url,
        "destination": package.destination,
        "source_subdirectory": package.source_subdirectory,
        "license_file": package.license_file,
    }
    for field, value in string_fields.items():
        if not isinstance(value, str) or not value:
            raise DependencyError(f"source package {field} must be a non-empty string")
    if any(character in package.name for character in "/\\:") or package.name in {
        ".",
        "..",
    }:
        raise DependencyError(f"source package name is unsafe: {package.name}")
    for field, value in {
        "archive_sha256": package.archive_sha256,
        "content_tree_sha256": package.content_tree_sha256,
    }.items():
        if (
            not isinstance(value, str)
            or len(value) != 64
            or value != value.lower()
            or any(character not in "0123456789abcdef" for character in value)
        ):
            raise DependencyError(
                f"source package {field} must be a lowercase SHA-256 digest"
            )
    _safe_package_relative_path(
        package.source_subdirectory,
        "source_subdirectory",
        allow_root=True,
    )
    _safe_package_relative_path(
        package.license_file,
        "license_file",
        allow_root=False,
    )


def _copy_path(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    if source.is_dir():
        shutil.copytree(source, destination)
    else:
        shutil.copy2(source, destination)


def _artifact_matches(artifact: ArtifactLock, destination: Path) -> bool:
    marker = _read_marker(destination / MANAGED_MARKER)
    if marker is None:
        return False
    expected = {
        "name": artifact.name,
        "version": artifact.version,
        "url": artifact.url,
        "sha256": artifact.sha256,
    }
    if any(marker.get(key) != value for key, value in expected.items()):
        return False
    content_hash = marker.get("content_hash")
    return bool(content_hash) and content_hash == _content_hash(destination)


def _download_artifact(artifact: ArtifactLock, cache_root: Path | None) -> Path:
    cache_path = None
    if cache_root is not None:
        cache_root.mkdir(parents=True, exist_ok=True)
        cache_path = cache_root / f"{artifact.name}-{artifact.sha256}.tar.gz"
        if cache_path.is_file():
            if hashlib.sha256(cache_path.read_bytes()).hexdigest() == artifact.sha256:
                return cache_path
            cache_path.unlink()
    handle = tempfile.NamedTemporaryFile(delete=False, suffix=".tar.gz")
    temporary = Path(handle.name)
    digest = hashlib.sha256()
    try:
        with handle, urllib.request.urlopen(artifact.url, timeout=60) as response:
            while chunk := response.read(1024 * 1024):
                handle.write(chunk)
                digest.update(chunk)
        if digest.hexdigest() != artifact.sha256:
            raise DependencyError(
                f"{artifact.name} SHA-256 mismatch: got {digest.hexdigest()}, "
                f"expected {artifact.sha256}"
            )
        if cache_path is not None:
            shutil.move(temporary, cache_path)
            return cache_path
        return temporary
    except Exception:
        temporary.unlink(missing_ok=True)
        raise


def _install_cpython27(
    repo_root: Path,
    artifact: ArtifactLock,
    destination: Path,
    archive_path: Path,
) -> None:
    staging = destination.with_name(destination.name + ".__fetch")
    _managed_artifact_destination(
        repo_root, staging.relative_to(repo_root.resolve()).as_posix()
    )
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir(parents=True)
    try:
        with tempfile.TemporaryDirectory() as temporary:
            extraction = Path(temporary)
            with tarfile.open(archive_path, mode="r:gz") as archive:
                _safe_extract(archive, extraction)
            source = extraction / "python"
            layout = {
                source / "include": staging / "include",
                source / "libs" / "python27.lib": staging / "libs" / "python27.lib",
                source / "python27.dll": staging / "bin" / "python27.dll",
                source / "Lib": staging / "PythonHome" / "Lib",
            }
            for source_path, destination_path in layout.items():
                if not source_path.exists():
                    raise DependencyError(
                        f"{artifact.name} archive is missing {source_path.relative_to(extraction)}"
                    )
                _copy_path(source_path, destination_path)
        (staging / "VERSION.txt").write_text(
            f"cpython {artifact.version} (qnox/python-2.7), "
            "x86_64-pc-windows-msvc\n"
            f"sha256 {artifact.sha256}\n{artifact.url}\n",
            encoding="utf-8",
        )
        (staging / ".gitignore").write_text(CPYTHON_GITIGNORE, encoding="utf-8")
        content_hash = _content_hash(staging)
        _write_marker(
            staging,
            {
                "name": artifact.name,
                "version": artifact.version,
                "url": artifact.url,
                "sha256": artifact.sha256,
                "content_hash": content_hash,
            },
        )
        if destination.exists():
            shutil.rmtree(destination)
        staging.rename(destination)
    except Exception:
        if staging.exists():
            shutil.rmtree(staging)
        raise


def sync_artifacts(
    repo_root: Path,
    lock: DependencyLock,
    *,
    cache_root: Path | None = None,
) -> list[str]:
    ready: list[str] = []
    artifact_cache = cache_root / "dependencies" if cache_root else None
    for artifact in lock.artifacts:
        destination = _managed_artifact_destination(repo_root, artifact.destination)
        if destination.is_dir() and _artifact_matches(artifact, destination):
            ready.append(artifact.name)
            continue
        archive_path = _download_artifact(artifact, artifact_cache)
        remove_archive = artifact_cache is None
        try:
            if artifact.name != "CPython27":
                raise DependencyError(f"unsupported artifact layout: {artifact.name}")
            _install_cpython27(repo_root, artifact, destination, archive_path)
        finally:
            if remove_archive:
                archive_path.unlink(missing_ok=True)
        ready.append(artifact.name)
    return ready


def _source_package_matches(
    package: SourcePackageLock,
    destination: Path,
) -> bool:
    marker = _read_marker(destination / MANAGED_MARKER)
    if marker is None:
        return False
    expected = {
        "name": package.name,
        "repository": package.repository,
        "revision": package.revision,
        "archive_url": package.archive_url,
        "archive_sha256": package.archive_sha256,
        "source_subdirectory": package.source_subdirectory,
        "license_file": package.license_file,
        "content_tree_sha256": package.content_tree_sha256,
    }
    if any(marker.get(key) != value for key, value in expected.items()):
        return False
    try:
        license_path = _safe_package_relative_path(
            package.license_file,
            "license_file",
            allow_root=False,
        )
    except DependencyError:
        return False
    if not (destination / license_path).is_file():
        return False
    return _source_tree_hash(destination) == package.content_tree_sha256


def _download_source_package(
    package: SourcePackageLock,
    cache_root: Path | None,
) -> tuple[Path, bool]:
    cache_path = None
    if cache_root is not None:
        cache_root.mkdir(parents=True, exist_ok=True)
        cache_path = cache_root / f"{package.name}-{package.archive_sha256}.archive"
        if cache_path.is_file():
            if hashlib.sha256(cache_path.read_bytes()).hexdigest() == package.archive_sha256:
                return cache_path, False
            cache_path.unlink()

    handle = tempfile.NamedTemporaryFile(delete=False, suffix=".archive")
    temporary = Path(handle.name)
    digest = hashlib.sha256()
    try:
        with handle, urllib.request.urlopen(package.archive_url, timeout=60) as response:
            while chunk := response.read(1024 * 1024):
                handle.write(chunk)
                digest.update(chunk)
        actual = digest.hexdigest()
        if actual != package.archive_sha256:
            raise DependencyError(
                f"{package.name} archive SHA-256 mismatch: got {actual}, "
                f"expected {package.archive_sha256}"
            )
        if cache_path is not None:
            shutil.move(temporary, cache_path)
            return cache_path, False
        return temporary, True
    except Exception:
        temporary.unlink(missing_ok=True)
        raise


def _remove_managed_path(path: Path) -> None:
    if path.is_symlink() or (hasattr(path, "is_junction") and path.is_junction()):
        path.unlink(missing_ok=True)
    elif path.is_dir():
        shutil.rmtree(path)
    else:
        path.unlink(missing_ok=True)


def _install_source_package(
    repo_root: Path,
    package: SourcePackageLock,
    destination: Path,
    archive_path: Path,
) -> None:
    staging = destination.with_name(destination.name + ".__fetch")
    previous = destination.with_name(destination.name + ".__previous")
    _managed_source_package_destination(
        repo_root, staging.relative_to(repo_root.resolve()).as_posix()
    )
    _managed_source_package_destination(
        repo_root, previous.relative_to(repo_root.resolve()).as_posix()
    )
    source_subdirectory = _safe_package_relative_path(
        package.source_subdirectory,
        "source_subdirectory",
        allow_root=True,
    )
    license_file = _safe_package_relative_path(
        package.license_file,
        "license_file",
        allow_root=False,
    )
    _remove_managed_path(staging)
    destination.parent.mkdir(parents=True, exist_ok=True)
    try:
        with tempfile.TemporaryDirectory() as temporary:
            extraction = Path(temporary)
            _safe_extract_source_archive(archive_path, extraction)
            source = (
                extraction
                if source_subdirectory == Path(".")
                else extraction / source_subdirectory
            )
            if not source.is_dir():
                raise DependencyError(
                    f"{package.name} archive is missing source subdirectory: "
                    f"{package.source_subdirectory}"
                )
            shutil.copytree(source, staging)

        installed_license = staging / license_file
        if not installed_license.is_file():
            raise DependencyError(
                f"{package.name} source package is missing license file: "
                f"{package.license_file}"
            )
        content_tree = _source_tree_hash(staging)
        if content_tree != package.content_tree_sha256:
            raise DependencyError(
                f"{package.name} content tree mismatch: got {content_tree}, "
                f"expected {package.content_tree_sha256}"
            )
        _write_marker(
            staging,
            {
                "name": package.name,
                "repository": package.repository,
                "revision": package.revision,
                "archive_url": package.archive_url,
                "archive_sha256": package.archive_sha256,
                "source_subdirectory": package.source_subdirectory,
                "license_file": package.license_file,
                "content_tree_sha256": package.content_tree_sha256,
            },
        )

        _remove_managed_path(previous)
        replaced = destination.exists()
        if replaced:
            destination.rename(previous)
        try:
            staging.rename(destination)
        except Exception:
            if replaced and previous.exists() and not destination.exists():
                previous.rename(destination)
            raise
        _remove_managed_path(previous)
    except Exception:
        _remove_managed_path(staging)
        raise


def sync_source_packages(
    repo_root: Path,
    lock: DependencyLock,
    *,
    cache_root: Path | None = None,
) -> list[str]:
    ready: list[str] = []
    package_cache = (
        cache_root / "dependencies" / "source-packages" if cache_root else None
    )
    for package in lock.source_packages:
        _validate_source_package_lock(package)
        destination = _managed_source_package_destination(
            repo_root, package.destination
        )
        if destination.is_dir() and _source_package_matches(package, destination):
            ready.append(package.name)
            continue
        archive_path, remove_archive = _download_source_package(package, package_cache)
        try:
            _install_source_package(repo_root, package, destination, archive_path)
        finally:
            if remove_archive:
                archive_path.unlink(missing_ok=True)
        ready.append(package.name)
    return ready


def check_dependencies(repo_root: Path, lock: DependencyLock) -> list[DependencyIssue]:
    issues: list[DependencyIssue] = []
    for plugin in lock.plugins:
        destination = _managed_plugin_destination(repo_root, plugin.destination)
        if not destination.is_dir():
            issues.append(DependencyIssue(plugin.name, "managed plugin is missing"))
        elif not _plugin_matches(plugin, destination):
            issues.append(
                DependencyIssue(plugin.name, "managed plugin is stale or modified")
            )
    for artifact in lock.artifacts:
        destination = _managed_artifact_destination(repo_root, artifact.destination)
        if not destination.is_dir():
            issues.append(DependencyIssue(artifact.name, "managed artifact is missing"))
        elif not _artifact_matches(artifact, destination):
            issues.append(
                DependencyIssue(artifact.name, "managed artifact is stale or modified")
            )
    for package in lock.source_packages:
        destination = _managed_source_package_destination(
            repo_root, package.destination
        )
        if not destination.is_dir():
            issues.append(
                DependencyIssue(package.name, "managed source package is missing")
            )
            continue
        try:
            license_file = _safe_package_relative_path(
                package.license_file,
                "license_file",
                allow_root=False,
            )
        except DependencyError:
            issues.append(
                DependencyIssue(package.name, "managed source package license is unsafe")
            )
            continue
        if not (destination / license_file).is_file():
            issues.append(
                DependencyIssue(package.name, "managed source package license is missing")
            )
        elif not _source_package_matches(package, destination):
            issues.append(
                DependencyIssue(
                    package.name,
                    "managed source package is stale or modified",
                )
            )
    return issues


def load_project_lock(repo_root: Path) -> DependencyLock:
    return DependencyLock.load(repo_root / "dev" / "dependencies.lock.json")


def sync_dependencies(
    repo_root: Path,
    *,
    cache_root: Path | None = None,
) -> list[str]:
    lock = load_project_lock(repo_root)
    return (
        sync_plugins(repo_root, lock)
        + sync_source_packages(repo_root, lock, cache_root=cache_root)
        + sync_artifacts(repo_root, lock, cache_root=cache_root)
    )
