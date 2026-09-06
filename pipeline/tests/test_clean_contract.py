from __future__ import annotations

import json
from pathlib import Path
import tempfile

import pytest

from elysium_pipeline import clean
from elysium_pipeline.workspace_lock import LOCK_FILE, OWNER_FILE


def _layout(root: Path) -> tuple[Path, Path, Path, Path, Path]:
    repo = root / "repo"
    game = root / "game"
    work = root / "work"
    export = work / "exports"
    export_v2 = work / "exports_v2"
    for path in (repo, game, export, export_v2):
        path.mkdir(parents=True)
    return repo, game, work, export, export_v2


def test_clean_deletes_only_generated_targets_and_marks_corpus_incomplete() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        repo, game, work, export, export_v2 = _layout(Path(temporary))
        (export / "sp_tutorial_1").mkdir()
        (export / "sp_tutorial_1" / "map.obj").write_text("derived")
        (export_v2 / "materials").mkdir()
        (export_v2 / "materials" / "brick.glb").write_text("derived")
        (export / LOCK_FILE).write_bytes(b"\0")
        (export / OWNER_FILE).write_text("{}", encoding="utf-8")
        (repo / "Content" / "Fonts").mkdir(parents=True)
        (repo / "Content" / "Fonts" / "source.ttf").write_text("licensed")
        (repo / "Content" / "InputPrompts" / "Kenney").mkdir(parents=True)
        (repo / "Content" / "InputPrompts" / "Kenney" / "glyph.png").write_text("licensed")
        (repo / "Content" / "ElysiumGenerated" / "Input").mkdir(parents=True)
        (repo / "Content" / "ElysiumGenerated" / "Input" / "generated.uasset").write_text(
            "derived")
        (repo / "Content" / "ElysiumGenerated" / "Boot.umap").write_text("derived")
        baked = repo / "Plugins" / "ElysiumBaked" / "Content"
        baked.mkdir(parents=True)
        (baked / "generated.umap").write_text("derived")
        corpus = repo / "Content" / "ElysiumCorpus" / "vdata" / "system"
        corpus.mkdir(parents=True)
        (corpus / "feats.txt").write_text("derived")
        external = repo / "Plugins" / "External" / "Cog"
        external.mkdir(parents=True)
        (external / "source.cpp").write_text("managed dependency")

        targets = clean.validate_clean_targets(
            repo_root=repo,
            game_root=game,
            work_root=work,
            export_root=export,
            export_v2_root=export_v2,
        )
        incomplete = clean.clean_generated(targets)

        assert not (repo / "Content" / "ElysiumGenerated").exists()
        assert not baked.exists()
        assert not (repo / "Content" / "ElysiumCorpus").exists()
        assert (repo / "Content" / "Fonts" / "source.ttf").is_file()
        assert (repo / "Content" / "InputPrompts" / "Kenney" / "glyph.png").is_file()
        assert (external / "source.cpp").is_file()
        assert not (export_v2 / "materials").exists()
        assert (export_v2 / clean.OWNERSHIP_FILE).is_file()
        assert (export / clean.OWNERSHIP_FILE).is_file()
        assert (export / LOCK_FILE).is_file()
        assert (export / OWNER_FILE).is_file()
        assert incomplete.resolve() == (export / clean.INCOMPLETE_FILE).resolve()
        assert incomplete.is_file()

        # Every domain is gated after a clean, and each clears on its own.
        assert clean.incomplete_domains(export) == clean.DOMAINS

        clean.mark_complete(export, ("audio",))
        assert "audio" not in clean.incomplete_domains(export)
        assert "maps" in clean.incomplete_domains(export)
        # The aggregate outlives any single domain.
        assert incomplete.is_file()

        clean.mark_complete(export)
        assert clean.incomplete_domains(export) == ()
        assert not incomplete.exists()


def test_profile_clean_keeps_the_glb_corpus_and_the_canonical_kind_roots() -> None:
    # R8: a profile export reads the V2 corpus and the static native lanes; it regenerates the
    # loose export root, the policy packages, the corpus bake and the map bakes, and only those.
    with tempfile.TemporaryDirectory() as temporary:
        repo, game, work, export, export_v2 = _layout(Path(temporary))
        (export / "sp_tutorial_1").mkdir()
        (export / "sp_tutorial_1" / "map.obj").write_text("derived")
        (export_v2 / "models" / "character").mkdir(parents=True)
        (export_v2 / "models" / "character" / "rat.glb").write_text("published")
        (repo / "Content" / "ElysiumGenerated").mkdir(parents=True)
        (repo / "Content" / "ElysiumGenerated" / "Boot.umap").write_text("derived")
        corpus = repo / "Content" / "ElysiumCorpus" / "vdata"
        corpus.mkdir(parents=True)
        (corpus / "feats.txt").write_text("deployed by import vdata")
        baked = repo / "Plugins" / "ElysiumBaked" / "Content"
        for folder in ("Models/character", "Materials", "Textures", "SurfaceProperties",
                       "ExpressionTables", "Shared", "Characters", "Props", "Items", "Meshes",
                       "Sky", "Sprites", "Lookdev", "sp_tutorial_1"):
            (baked / folder).mkdir(parents=True)
            (baked / folder / "asset.uasset").write_text("x")
        (baked / "Elysium.upluginmanifest").write_text("keep")

        targets = clean.validate_clean_targets(
            repo_root=repo, game_root=game, work_root=work,
            export_root=export, export_v2_root=export_v2,
        )
        emptied = {path.name for path in clean.profile_clean_targets(targets)}
        assert emptied == {"Shared", "Characters", "Props", "Items", "Meshes", "Sky", "Sprites",
                           "Lookdev", "sp_tutorial_1"}

        incomplete = clean.clean_profile_outputs(targets)

        assert not (export / "sp_tutorial_1").exists()
        assert (export / clean.OWNERSHIP_FILE).is_file()
        assert incomplete.is_file()
        assert (export_v2 / "models" / "character" / "rat.glb").is_file()
        assert (corpus / "feats.txt").is_file()
        assert not (repo / "Content" / "ElysiumGenerated").exists()
        for folder in ("Models/character", "Materials", "Textures", "SurfaceProperties",
                       "ExpressionTables"):
            assert (baked / folder / "asset.uasset").is_file(), folder
        for folder in ("Shared", "Characters", "Props", "Items", "Meshes", "Sky", "Sprites",
                       "Lookdev", "sp_tutorial_1"):
            assert not (baked / folder).exists(), folder
        assert (baked / "Elysium.upluginmanifest").is_file()


def test_a_custom_export_v2_root_requires_its_own_ownership_marker() -> None:
    # `exports_v2` is adopted by name exactly as `exports` is; anything else the operator
    # points the seams at must be marked by hand before a clean may empty it.
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        work = root / "work"
        custom = work / "authored-glb"
        custom.mkdir(parents=True)
        (custom / "valuable.glb").write_text("authored", encoding="utf-8")

        with pytest.raises(clean.UnsafeClean):
            clean.ensure_export_ownership(custom, work, standard_name="exports_v2")

        assert not (custom / clean.OWNERSHIP_FILE).exists()
        assert (custom / "valuable.glb").is_file()

        standard = work / "exports_v2"
        marker = clean.ensure_export_ownership(standard, work, standard_name="exports_v2")
        assert marker.is_file()


def test_clean_refuses_one_root_pointed_at_both_export_trees() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        repo, game, work, export, _ = _layout(Path(temporary))
        with pytest.raises(clean.UnsafeClean):
            clean.validate_clean_targets(
                repo_root=repo,
                game_root=game,
                work_root=work,
                export_root=export,
                export_v2_root=export,
            )


def test_unknown_domain_is_rejected() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        with pytest.raises(ValueError):
            clean.domain_marker(Path(temporary), "not-a-domain")


def test_custom_export_root_requires_existing_ownership_marker() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        work = root / "work"
        custom = work / "authored-tools"
        custom.mkdir(parents=True)
        (custom / "valuable.py").write_text("print('keep me')", encoding="utf-8")

        with pytest.raises(clean.UnsafeClean):
            clean.adopt_export_root(custom, work)

        assert not (custom / clean.OWNERSHIP_FILE).exists()
        assert (custom / "valuable.py").is_file()


def test_malformed_ownership_marker_is_rejected_as_unsafe() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        work = root / "work"
        export = work / "exports"
        export.mkdir(parents=True)
        (export / clean.OWNERSHIP_FILE).write_text("{not json", encoding="utf-8")

        with pytest.raises(clean.UnsafeClean):
            clean.ensure_export_ownership(export, work)


def test_marker_for_another_root_is_rejected() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        work = root / "work"
        export = work / "exports"
        export.mkdir(parents=True)
        (export / clean.OWNERSHIP_FILE).write_text(
            json.dumps(
                {
                    "schema": 1,
                    "owner": "elysium",
                    "root": str(work / "somewhere-else"),
                }
            ),
            encoding="utf-8",
        )

        with pytest.raises(clean.UnsafeClean):
            clean.ensure_export_ownership(export, work)
