"""Ownership checks and exact deletion rules for reproducible exports."""

from __future__ import annotations

from dataclasses import dataclass
import json
import os
from collections.abc import Sequence
from pathlib import Path
import shutil

from elysium_pipeline.workspace_lock import LOCK_FILE, OWNER_FILE


OWNERSHIP_FILE = ".elysium-owned.json"
INCOMPLETE_FILE = ".elysium-incomplete"
MANIFEST_FILE = ".elysium-manifest.json"

# The corpus is incomplete per DOMAIN, not as a whole: `export bundle audio` finishes the audio
# catalogue and nothing else, and a content test that reads only that has no reason to abstain
# because the scripts are still missing. (`npc` and `items` retired with their exporters in R8;
# characters and wield are native import lanes with their own receipts.)  One marker per domain, named for the bundle that
# clears it (`maps` for the map exports), plus INCOMPLETE_FILE as the aggregate the repository
# policy check and the human-facing message read.  The aggregate survives while any domain does.
DOMAINS = (
    "maps",
    "audio",
    "cfg",
    # The shared static corpus: every texture, material and static model in the install, decoded
    # once. Every map export and every bake resolves against it, so it is its own domain.
    "corpus",
    "particles",
    # `policy` is not an export bundle in the exporters' sense: it is the generated
    # /Game/ElysiumGenerated packages `ensure_policy_content` writes, cleared by every profile
    # export and by `export bundle policy`.
    "policy",
    "scenes",
    "scripts",
    "signs",
    "ui",
    "vdata",
)


class UnsafeClean(RuntimeError):
    pass


def _same(left: Path, right: Path) -> bool:
    return os.path.normcase(str(left.resolve())) == os.path.normcase(str(right.resolve()))


def _is_within(path: Path, root: Path) -> bool:
    try:
        path.resolve().relative_to(root.resolve())
        return True
    except ValueError:
        return False


def _dangerous_roots(repo: Path, game: Path, work: Path) -> tuple[Path, ...]:
    home = Path.home().resolve()
    anchor = Path(repo.resolve().anchor)
    return anchor, home, repo.resolve(), game.resolve(), work.resolve()


def ensure_export_ownership(
    export_root: Path,
    work_root: Path,
    *,
    standard_name: str = "exports",
    adopt_standard: bool = True,
) -> Path:
    export_root = export_root.resolve()
    work_root = work_root.resolve()
    standard = (work_root / standard_name).resolve()
    if not _is_within(export_root, work_root) or _same(export_root, work_root):
        raise UnsafeClean(f"export root must be a child of the configured work root: {export_root}")
    marker = export_root / OWNERSHIP_FILE
    if marker.is_file():
        try:
            data = json.loads(marker.read_text(encoding="utf-8"))
            recorded_root = Path(data["root"])
        except (OSError, ValueError, KeyError, TypeError) as exc:
            raise UnsafeClean(f"invalid export ownership marker: {marker}") from exc
        if (
            data.get("schema") != 1
            or data.get("owner") != "elysium"
            or not _same(recorded_root, export_root)
        ):
            raise UnsafeClean(f"invalid export ownership marker: {marker}")
        return marker
    if not adopt_standard or not _same(export_root, standard):
        raise UnsafeClean(
            f"custom export root has no ownership marker: {marker}; "
            "create a schema-1 marker only after verifying this directory is dedicated "
            "to Elysium generated output"
        )
    export_root.mkdir(parents=True, exist_ok=True)
    marker.write_text(
        json.dumps({"schema": 1, "owner": "elysium", "root": str(export_root)}, indent=2)
        + "\n",
        encoding="utf-8",
    )
    return marker


def adopt_export_root(export_root: Path, work_root: Path) -> Path:
    """Validate ownership, adopting only the standard ``work/exports`` root."""
    return ensure_export_ownership(export_root, work_root, adopt_standard=True)


@dataclass(frozen=True)
class CleanTargets:
    export_root: Path
    export_v2_root: Path
    generated_content: Path
    baked_content: Path
    corpus_content: Path


def validate_clean_targets(
    *,
    repo_root: Path,
    game_root: Path,
    work_root: Path,
    export_root: Path,
    export_v2_root: Path,
) -> CleanTargets:
    repo = repo_root.resolve()
    game = game_root.resolve()
    work = work_root.resolve()
    export = export_root.resolve()
    export_v2 = export_v2_root.resolve()
    ensure_export_ownership(export, work)
    ensure_export_ownership(export_v2, work, standard_name="exports_v2")

    if _same(export, export_v2):
        raise UnsafeClean(f"export and export_v2 roots must differ: {export}")
    for dangerous in _dangerous_roots(repo, game, work):
        if _same(export, dangerous):
            raise UnsafeClean(f"refusing dangerous export root: {export}")
        if _same(export_v2, dangerous):
            raise UnsafeClean(f"refusing dangerous export_v2 root: {export_v2}")
    generated_content = (repo / "Content" / "ElysiumGenerated").resolve()
    baked_content = (repo / "Plugins" / "ElysiumBaked" / "Content").resolve()
    corpus_content = (repo / "Content" / "ElysiumCorpus").resolve()
    expected = (
        repo / "Content" / "ElysiumGenerated",
        repo / "Plugins" / "ElysiumBaked" / "Content",
        repo / "Content" / "ElysiumCorpus",
    )
    actual = (generated_content, baked_content, corpus_content)
    if any(not _same(left, right) for left, right in zip(expected, actual, strict=True)):
        raise UnsafeClean("generated Unreal targets did not resolve to the exact project paths")
    return CleanTargets(export, export_v2, generated_content, baked_content, corpus_content)


def _empty_owned_root(root: Path) -> None:
    """Delete everything under one adopted root except the files that mark it as ours."""
    controls = {
        (root / OWNERSHIP_FILE).resolve(),
        (root / LOCK_FILE).resolve(),
        (root / OWNER_FILE).resolve(),
    }
    for child in root.iterdir():
        if child.resolve() in controls:
            continue
        if child.is_dir():
            shutil.rmtree(child)
        else:
            child.unlink()


def clean_generated(targets: CleanTargets) -> Path:
    """Empty every generated root, the published GLB corpus and its native lanes included.

    This is the whole-mount clean. Nothing on the public command surface runs it since R8:
    a profile export regenerates only its own outputs (`clean_profile_outputs`), because the
    GLB corpus and the static native lanes it consumes are published by separate commands.
    """
    _empty_owned_root(targets.export_root)
    _empty_owned_root(targets.export_v2_root)
    if targets.generated_content.exists():
        shutil.rmtree(targets.generated_content)
    if targets.baked_content.exists():
        shutil.rmtree(targets.baked_content)
    if targets.corpus_content.exists():
        shutil.rmtree(targets.corpus_content)
    return mark_incomplete(targets.export_root)


def profile_clean_targets(targets: CleanTargets) -> tuple[Path, ...]:
    """The mount folders a profile export owns and may empty before regenerating.

    Everything under the baked mount that is not a canonical kind root: the legacy roots
    (`Shared/`, `Characters/`, `Props/`, `Items/`), the pre-standard folders (`Meshes/`,
    `Sky/`, `Sprites/`, `Lookdev/`) and every deployed `<map>/` bundle. The kind roots
    (`Models/`, `Materials/`, `Textures/`, `SurfaceProperties/`, `ExpressionTables/`, ...)
    are producer-owned: their lanes stamp and prune their own assets and are re-run with
    `--force` by the profile rather than deleted from under their receipts.
    """
    from elysium_pipeline.asset_paths import KIND_ROOTS

    keep = {name.casefold() for name in KIND_ROOTS.values()}
    if not targets.baked_content.is_dir():
        return ()
    return tuple(sorted(
        child for child in targets.baked_content.iterdir()
        if child.is_dir() and child.name.casefold() not in keep
    ))


def clean_profile_outputs(targets: CleanTargets) -> Path:
    """Empty what a profile export regenerates; keep the corpus the profile only reads.

    Kept: the `exports_v2` GLB corpus (published by `export_v2 *-glb`), the canonical kind
    roots on the mount (published by `import textures/materials/models/...`) and the deployed
    vdata corpus (`import vdata`). Emptied: the loose export root, `Content/ElysiumGenerated`
    (policy generators) and the legacy/map folders on the mount (corpus bake, map bakes).
    """
    _empty_owned_root(targets.export_root)
    if targets.generated_content.exists():
        shutil.rmtree(targets.generated_content)
    for folder in profile_clean_targets(targets):
        shutil.rmtree(folder)
    return mark_incomplete(targets.export_root)


def domain_marker(export_root: Path, domain: str) -> Path:
    if domain not in DOMAINS:
        raise ValueError(f"unknown export domain: {domain!r}")
    return export_root / f"{INCOMPLETE_FILE}.{domain}"


def incomplete_domains(export_root: Path) -> tuple[str, ...]:
    return tuple(
        domain for domain in DOMAINS if domain_marker(export_root, domain).is_file()
    )


def mark_incomplete(export_root: Path, domains: Sequence[str] = DOMAINS) -> Path:
    for domain in domains:
        domain_marker(export_root, domain).write_text(
            f"The {domain} half of the generated corpus is incomplete.\n",
            encoding="utf-8",
        )
    incomplete = export_root / INCOMPLETE_FILE
    incomplete.write_text(
        "The generated corpus is incomplete. Run `uv run elysium export grid`, "
        "`uv run elysium export all`, or `uv run elysium reconstruct`.\n",
        encoding="utf-8",
    )
    return incomplete


def mark_complete(export_root: Path, domains: Sequence[str] | None = None) -> None:
    for domain in DOMAINS if domains is None else domains:
        marker = domain_marker(export_root, domain)
        if marker.is_file():
            marker.unlink()
    # The aggregate outlives the domains: it says only that SOMETHING is missing, which the
    # repository policy check and `reconstruct` still turn on.
    if incomplete_domains(export_root):
        return
    incomplete = export_root / INCOMPLETE_FILE
    if incomplete.exists():
        incomplete.unlink()
