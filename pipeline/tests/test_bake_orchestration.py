from __future__ import annotations

import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
from unittest import mock

import pytest

from elysium_pipeline import export_manager, shared_corpus
from elysium_pipeline.placed_models import PlacedModelUse
from elysium_pipeline.tasking import Manifest, TaskFailure, TaskResult


def _config(temporary: str):
    repo = Path(temporary) / "repo"
    export = Path(temporary) / "exports"
    baked = repo / "Plugins" / "ElysiumBaked" / "Content" / "test_map"
    baked.mkdir(parents=True)
    (baked / "test_map.umap").write_bytes(b"level")
    export.mkdir(parents=True)
    return SimpleNamespace(repo_root=repo, export_root=export)


def _write_character_materials(material_root):
    v2 = material_root / "V2"
    v2.mkdir(parents=True, exist_ok=True)
    for name in ("M_V2_LitSkinned", "M_V2_LitSkinnedTranslucent", "M_V2_Eyes",
                 "MI_V2_Missing", "T_V2_MissingChecker"):
        (v2 / f"{name}.uasset").write_bytes(b"asset")


def _record_verification(config) -> None:
    task = export_manager._verification_task(config, "test_map")
    Manifest(config.export_root / ".elysium-manifest.json").record(
        TaskResult(
            name=task.name,
            status="ok",
            duration_seconds=0.0,
            fingerprint=task.fingerprint(),
            outputs=[str(path) for path in task.outputs],
        )
    )


def test_bake_launches_once_per_batch_and_trusts_the_exit() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        config = _config(temporary)
        with (
            mock.patch.object(export_manager.unreal, "bake_maps") as bake,
            mock.patch.object(export_manager.unreal, "verify_bakes") as verify,
        ):
            export_manager.bake_and_verify(config, object(), ["test_map", "test_map"])
        bake.assert_called_once_with(config, mock.ANY, ["test_map"], force=False,
                                     particles=False,
                                     batch_size=export_manager.unreal.MAP_BAKE_BATCH)
        verify.assert_not_called()


def test_missing_baked_package_is_a_loud_failure() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        config = _config(temporary)
        with (
            mock.patch.object(export_manager.unreal, "bake_maps"),
            mock.patch.object(export_manager.unreal, "verify_bakes") as verify,
        ):
            with pytest.raises(export_manager.ExportBakeFailure) as caught:
                export_manager.bake_and_verify(config, object(), ["absent_map"])
        assert "absent_map" in str(caught.value)
        verify.assert_not_called()


def test_verify_is_an_explicit_opt_in() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        config = _config(temporary)
        with (
            mock.patch.object(export_manager.unreal, "bake_maps"),
            mock.patch.object(export_manager.unreal, "verify_bakes") as verify,
        ):
            export_manager.bake_and_verify(
                config, object(), ["test_map"], verify=True
            )
        verify.assert_called_once_with(config, mock.ANY, ["test_map"])


def test_particle_pass_is_an_explicit_opt_in() -> None:
    """The map bake authors no Niagara system unless the caller asks for one.

    The pass force-deletes packages the asset compiler may still own, which crashes the
    editor, so it stays off the default path and rides `--particles` when wanted.
    """
    recorded: list[list[str]] = []

    class _Runner:
        def run(self, argv, **_kwargs):
            recorded.append([str(item) for item in argv])
            return SimpleNamespace(returncode=0)

    with tempfile.TemporaryDirectory() as temporary:
        config = _config(temporary)
        config.ue_root = Path(temporary) / "ue"
        config.project = config.repo_root / "ElysiumUE.uproject"
        editor = config.ue_root / "Engine" / "Binaries" / "Win64"
        editor.mkdir(parents=True)
        (editor / "UnrealEditor-Cmd.exe").write_bytes(b"")

        export_manager.unreal.bake_maps(config, _Runner(), ["test_map"])
        assert "-BakeParticles=1" not in recorded[-1]

        export_manager.unreal.bake_maps(config, _Runner(), ["test_map"], particles=True)
        assert "-BakeParticles=1" in recorded[-1]


def test_particle_pass_is_part_of_the_profile_recipe() -> None:
    """Turning the pass on or off changes the maps-bake receipt, so a toggled run relaunches."""
    with tempfile.TemporaryDirectory() as temporary:
        config = _config(temporary)
        (config.export_root / "test_map").mkdir(parents=True, exist_ok=True)
        off = export_manager._maps_bake_fingerprint(config, ["test_map"], particles=False)
        on = export_manager._maps_bake_fingerprint(config, ["test_map"], particles=True)
        assert off != on


def test_map_receipt_keeps_deployed_root_and_tracks_native_catalogues(tmp_path) -> None:
    config = _config(str(tmp_path))
    expected = config.repo_root / "Plugins/ElysiumBaked/Content/test_map/test_map.umap"
    assert export_manager._baked_package(config, "test_map") == expected
    before = export_manager._maps_bake_fingerprint(config, ["test_map"])
    catalogue = config.repo_root / "Plugins/ElysiumBaked/Content/Models/_Corpus/DA_PlacedModels.uasset"
    catalogue.parent.mkdir(parents=True)
    catalogue.write_bytes(b"native references")
    assert export_manager._maps_bake_fingerprint(config, ["test_map"]) != before


def test_corpus_bake_gate_skips_a_warm_second_run_and_force_defeats_it() -> None:
    # The launch rides a manifest receipt over the decoded shared corpus; per-asset reuse
    # inside a launch remains the commandlet's own decision, read off each recipe stamp.
    with tempfile.TemporaryDirectory() as temporary:
        config = _config(temporary)
        shared = config.export_root / "shared"
        shared.mkdir()
        (shared / "manifest.json").write_text("{}", encoding="utf-8")
        mount = config.repo_root / "Plugins" / "ElysiumBaked" / "Content" / "Shared"
        mount.mkdir(parents=True)
        with mock.patch.object(export_manager.unreal, "bake_corpus") as bake:
            export_manager.ensure_corpus_bake(config, object())
            export_manager.ensure_corpus_bake(config, object())
            export_manager.ensure_corpus_bake(config, object(), force=True)
        assert [call.kwargs["force"] for call in bake.call_args_list] == [False, True]


def test_corpus_bake_relaunches_when_a_shared_input_moves() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        config = _config(temporary)
        shared = config.export_root / "shared"
        shared.mkdir()
        (shared / "manifest.json").write_text("{}", encoding="utf-8")
        mount = config.repo_root / "Plugins" / "ElysiumBaked" / "Content" / "Shared"
        mount.mkdir(parents=True)
        with mock.patch.object(export_manager.unreal, "bake_corpus") as bake:
            export_manager.ensure_corpus_bake(config, object())
            (shared / "manifest.json").write_text('{"textures": {}}', encoding="utf-8")
            export_manager.ensure_corpus_bake(config, object())
        assert bake.call_count == 2


def test_failed_corpus_bake_records_no_success() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        config = _config(temporary)
        shared = config.export_root / "shared"
        shared.mkdir()
        (shared / "manifest.json").write_text("{}", encoding="utf-8")
        mount = config.repo_root / "Plugins" / "ElysiumBaked" / "Content" / "Shared"
        mount.mkdir(parents=True)
        with mock.patch.object(
            export_manager.unreal, "bake_corpus",
            side_effect=export_manager.unreal.UnrealFailure("editor exited with 1"),
        ):
            with pytest.raises(export_manager.ExportBakeFailure):
                export_manager.ensure_corpus_bake(config, object())
        # The failed launch left no usable receipt, so the next run launches again.
        with mock.patch.object(export_manager.unreal, "bake_corpus") as bake:
            export_manager.ensure_corpus_bake(config, object())
        bake.assert_called_once()


def test_profile_map_bake_gate_skips_warm_and_verify_still_reads_back() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        config = _config(temporary)
        map_dir = config.export_root / "test_map"
        map_dir.mkdir()
        (map_dir / "test_map.obj").write_text("o test\n", encoding="utf-8")
        with (
            mock.patch.object(export_manager.unreal, "bake_maps") as bake,
            mock.patch.object(export_manager.unreal, "verify_bakes") as verify,
        ):
            export_manager._bake_profile_maps(config, object(), ["test_map"])
            export_manager._bake_profile_maps(config, object(), ["test_map"])
            export_manager._bake_profile_maps(config, object(), ["test_map"],
                                              verify=True)
            export_manager._bake_profile_maps(config, object(), ["test_map"],
                                              force=True)
        assert bake.call_count == 2
        # --verify is an explicit read-back request and runs even off a warm receipt.
        assert verify.call_count == 1


def test_profile_map_bake_skip_requires_the_baked_package() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        config = _config(temporary)
        map_dir = config.export_root / "test_map"
        map_dir.mkdir()
        (map_dir / "test_map.obj").write_text("o test\n", encoding="utf-8")
        package = export_manager._baked_package(config, "test_map")

        with mock.patch.object(export_manager.unreal, "bake_maps") as bake:
            export_manager._bake_profile_maps(config, object(), ["test_map"])
            package.unlink()
            bake.side_effect = lambda *_args, **_kwargs: package.write_bytes(b"level")
            export_manager._bake_profile_maps(config, object(), ["test_map"])
        assert bake.call_count == 2


def test_policy_generators_merge_into_one_commandlet_launch() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        config = _config(temporary)
        unreal_root = config.repo_root / "pipeline" / "unreal"
        unreal_root.mkdir(parents=True)
        names = [
            export_manager.WORLD_MATERIAL_GENERATOR,
            *export_manager.CHARACTER_MATERIAL_GENERATORS,
            "make_boot_map.py",
        ]
        (unreal_root / "build_content.py").write_text(
            "GENERATORS = " + repr(names) + "\n", encoding="utf-8")
        for name in names:
            (unreal_root / name).write_text("VALUE = 1\n", encoding="utf-8")
        material_root = config.repo_root / "Content" / "ElysiumGenerated" / "Materials"
        font_root = config.repo_root / "Content" / "ElysiumGenerated" / "UI" / "Fonts"

        def generate(_config, _runner, generators, *, include_auxiliary):
            assert not include_auxiliary
            material_root.mkdir(parents=True, exist_ok=True)
            for master in (
                "M_World_Opaque", "M_World_Masked", "M_World_Translucent",
                "M_World_Glass", "M_Refract", "M_Additive",
            ):
                (material_root / f"{master}.uasset").write_bytes(b"asset")
            _write_character_materials(material_root)

        def auxiliary(_config, _runner):
            font_root.mkdir(parents=True, exist_ok=True)
            for name in export_manager.unreal.FONT_ASSETS:
                (font_root / name).write_bytes(b"font")
            graph = config.repo_root / "Content" / "ElysiumGenerated" / "Animation"
            graph.mkdir(parents=True, exist_ok=True)
            (graph / "ABP_ElysiumBiped.uasset").write_bytes(b"graph")
            (config.repo_root / "Content" / "ElysiumGenerated" / "Boot.umap").write_bytes(
                b"map")

        with (
            mock.patch.object(export_manager.unreal, "generate_policy_content",
                              side_effect=generate) as content,
            mock.patch.object(export_manager.unreal,
                              "generate_auxiliary_policy_content",
                              side_effect=auxiliary) as extras,
        ):
            first = export_manager.ensure_policy_content(
                config, object(), particles_covered=True)
            second = export_manager.ensure_policy_content(
                config, object(), particles_covered=True)
            third = export_manager.ensure_policy_content(
                config, object(), particles_covered=True, force=True)

        # All three receipt sets were stale together, so one commandlet carried the
        # union in the generator list's declared order; the auxiliary pair launched
        # once behind it.
        assert content.call_count == 2
        assert content.call_args_list[0].args[2] == names
        assert extras.call_count == 2
        assert first.status == "ok"
        assert second.status == "skipped"
        assert third.status == "ok"


def test_stale_world_materials_alone_launch_only_that_generator() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        config = _config(temporary)
        unreal_root = config.repo_root / "pipeline" / "unreal"
        unreal_root.mkdir(parents=True)
        names = [
            export_manager.WORLD_MATERIAL_GENERATOR,
            *export_manager.CHARACTER_MATERIAL_GENERATORS,
            "make_boot_map.py",
        ]
        (unreal_root / "build_content.py").write_text(
            "GENERATORS = " + repr(names) + "\n", encoding="utf-8")
        for name in names:
            (unreal_root / name).write_text("VALUE = 1\n", encoding="utf-8")
        material_root = config.repo_root / "Content" / "ElysiumGenerated" / "Materials"
        font_root = config.repo_root / "Content" / "ElysiumGenerated" / "UI" / "Fonts"

        def generate(_config, _runner, generators, *, include_auxiliary):
            material_root.mkdir(parents=True, exist_ok=True)
            for master in (
                "M_World_Opaque", "M_World_Masked", "M_World_Translucent",
                "M_World_Glass", "M_Refract", "M_Additive",
            ):
                (material_root / f"{master}.uasset").write_bytes(b"asset")
            _write_character_materials(material_root)

        def auxiliary(_config, _runner):
            font_root.mkdir(parents=True, exist_ok=True)
            for name in export_manager.unreal.FONT_ASSETS:
                (font_root / name).write_bytes(b"font")
            graph = config.repo_root / "Content" / "ElysiumGenerated" / "Animation"
            graph.mkdir(parents=True, exist_ok=True)
            (graph / "ABP_ElysiumBiped.uasset").write_bytes(b"graph")
            (config.repo_root / "Content" / "ElysiumGenerated" / "Boot.umap").write_bytes(
                b"map")

        with (
            mock.patch.object(export_manager.unreal, "generate_policy_content",
                              side_effect=generate) as content,
            mock.patch.object(export_manager.unreal,
                              "generate_auxiliary_policy_content",
                              side_effect=auxiliary) as extras,
        ):
            export_manager.ensure_policy_content(
                config, object(), particles_covered=True)
            # Invalidate only the world-material set; the merged launch then carries
            # exactly that generator and the auxiliary pair stays down.
            (unreal_root / export_manager.WORLD_MATERIAL_GENERATOR).write_text(
                "VALUE = 2  # moved\n", encoding="utf-8")
            export_manager.ensure_policy_content(
                config, object(), particles_covered=True)

        assert content.call_count == 2
        assert content.call_args_list[1].args[2] == [export_manager.WORLD_MATERIAL_GENERATOR]
        assert extras.call_count == 1


def test_export_profile_sequences_gated_launches_and_covers_particles() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        config = _config(temporary)
        order = []
        with (
            mock.patch.object(export_manager.native_model_pipeline, "require_map_prerequisites"),
            mock.patch.object(
                export_manager, "run_offline_profile",
                side_effect=lambda *_a, **_k: order.append("offline") or (["m1"], {})),
            mock.patch.object(
                export_manager, "ensure_policy_content",
                side_effect=lambda *_a, **k: order.append(
                    ("policy", k.get("particles_covered")))),
            mock.patch.object(
                export_manager, "ensure_corpus_bake",
                side_effect=lambda *_a, **_k: order.append("corpus")),
            mock.patch.object(
                export_manager.native_model_pipeline, "import_map_dependencies",
                side_effect=lambda *_a, **_k: order.append("native models")),
            mock.patch.object(
                export_manager, "_bake_profile_maps",
                side_effect=lambda *_a, **_k: order.append("maps")),
        ):
            maps = export_manager.export_profile(config, object(), "all")
        assert maps == ["m1"]
        # The `all` profile carries the particles bundle, so the policy phase skips its
        # duplicate mirror; the cast and wield mounts precede the map bake.
        assert order == [
            "offline", ("policy", True), "corpus", "native models", "maps"]


def test_particle_mirror_is_gated_and_skipped_when_a_profile_covers_it() -> None:
    from elysium_pipeline.exporters import UE_extract_particles
    from elysium_pipeline.formats import install

    with tempfile.TemporaryDirectory() as temporary:
        config = _config(temporary)
        manifest = Manifest(config.export_root / ".elysium-manifest.json")
        out = config.export_root / "particles"

        def mirror(**_kwargs):
            out.mkdir(parents=True, exist_ok=True)
            (out / "manifest.json").write_text("{}", encoding="utf-8")

        with (
            mock.patch.object(UE_extract_particles, "main", side_effect=mirror) as main,
            mock.patch.object(install, "build_index", return_value={}),
        ):
            export_manager._ensure_particle_mirror(
                config, manifest=manifest, covered=True)
            main.assert_not_called()
            export_manager._ensure_particle_mirror(
                config, manifest=manifest, covered=False)
            export_manager._ensure_particle_mirror(
                config, manifest=manifest, covered=False)
            assert main.call_count == 1
            export_manager._ensure_particle_mirror(
                config, manifest=manifest, covered=False, force=True)
            assert main.call_count == 2


def test_scoped_fingerprint_moves_with_reached_modules_only() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        config = _config(temporary)
        package = config.repo_root / "pipeline" / "src" / "elysium_pipeline"
        (package / "exporters").mkdir(parents=True)
        (package / "exporters" / "reader.py").write_text(
            "from elysium_pipeline import shared_corpus\nVALUE = 1\n",
            encoding="utf-8")
        (package / "exporters" / "other.py").write_text("VALUE = 1\n", encoding="utf-8")
        (package / "shared_corpus.py").write_text("VALUE = 1\n", encoding="utf-8")
        entries = {"elysium_pipeline.exporters.reader"}

        initial = export_manager._scoped_source_fingerprint(config, entries)
        # A module the closure reaches moves the fingerprint...
        (package / "shared_corpus.py").write_text("VALUE = 2\n", encoding="utf-8")
        export_manager._DECODER_CLOSURES.clear()
        moved = export_manager._scoped_source_fingerprint(config, entries)
        assert moved != initial
        # ...and a module outside it does not.
        (package / "exporters" / "other.py").write_text("VALUE = 2\n", encoding="utf-8")
        export_manager._DECODER_CLOSURES.clear()
        assert export_manager._scoped_source_fingerprint(config, entries) == moved


def test_policy_fingerprint_includes_input_prompt_sources() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        config = _config(temporary)
        unreal_root = config.repo_root / "pipeline" / "unreal"
        unreal_root.mkdir(parents=True)
        (unreal_root / "build_content.py").write_text(
            'GENERATORS = ["make_input_glyphs.py"]\n', encoding="utf-8"
        )
        (unreal_root / "make_input_glyphs.py").write_text(
            "VALUE = 1\n", encoding="utf-8"
        )
        source = config.repo_root / "Content" / "InputPrompts" / "Kenney" / "glyph.png"
        source.parent.mkdir(parents=True)
        source.write_bytes(b"first")

        initial = export_manager._policy_fingerprint(config)
        source.write_bytes(b"replacement")
        assert export_manager._policy_fingerprint(config) != initial


def test_focused_world_policy_runs_only_the_world_material_generator() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        config = _config(temporary)
        material_root = config.repo_root / "Content" / "ElysiumGenerated" / "Materials"

        def generate(_config, _runner, generators, *, include_auxiliary):
            assert generators == (export_manager.WORLD_MATERIAL_GENERATOR,)
            assert not include_auxiliary
            material_root.mkdir(parents=True)
            for name in (
                "M_World_Opaque", "M_World_Masked", "M_World_Translucent",
                "M_World_Glass", "M_Refract", "M_Additive",
            ):
                (material_root / f"{name}.uasset").write_bytes(b"asset")

        with mock.patch.object(
            export_manager.unreal, "generate_policy_content", side_effect=generate
        ) as policy:
            result = export_manager.ensure_world_material_content(config, object())

        assert result.status == "ok"
        policy.assert_called_once()


def test_focused_character_policy_runs_only_character_material_generators() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        config = _config(temporary)
        material_root = config.repo_root / "Content" / "ElysiumGenerated" / "Materials"

        def generate(_config, _runner, generators, *, include_auxiliary):
            assert generators == export_manager.CHARACTER_MATERIAL_GENERATORS
            assert not include_auxiliary
            material_root.mkdir(parents=True)
            _write_character_materials(material_root)

        with mock.patch.object(
            export_manager.unreal, "generate_policy_content", side_effect=generate
        ) as policy:
            result = export_manager.ensure_character_material_content(config, object())

        assert result.status == "ok"
        policy.assert_called_once()


def _v2_config(temporary: str, map_name: str = "test_map"):
    config = _config(temporary)
    config.game_root = Path(temporary) / "game"
    config.work_root = Path(temporary) / "work"
    (config.export_root / map_name).mkdir(parents=True, exist_ok=True)
    (config.export_root / map_name / f"{map_name}.env").write_text("sky 0\n", encoding="utf-8")
    return config


def test_bake_v2_maps_runs_only_the_v2_lane() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        config = _v2_config(temporary)
        with (
            mock.patch("elysium_pipeline.map_transport.is_map_on_v2_models", return_value=True),
            mock.patch.object(export_manager.native_model_pipeline, "require_map_prerequisites")
            as prerequisites,
            mock.patch.object(export_manager, "adopt_export_root"),
            mock.patch.object(export_manager, "ensure_world_material_content") as masters,
            mock.patch.object(export_manager, "bake_and_verify") as bake,
        ):
            names = export_manager.bake_v2_maps(
                config, object(), ["test_map", "test_map"], force=True, verify=True)
        assert names == ["test_map"]
        prerequisites.assert_called_once_with(config)
        masters.assert_called_once()
        bake.assert_called_once_with(config, mock.ANY, ["test_map"], force=True,
                                     particles=False, verify=True)


def test_bake_v2_maps_refuses_a_map_off_the_v2_flag() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        config = _v2_config(temporary)
        with (
            mock.patch("elysium_pipeline.map_transport.is_map_on_v2_models", return_value=False),
            mock.patch.object(export_manager, "bake_and_verify") as bake,
        ):
            with pytest.raises(export_manager.ExportBakeFailure) as caught:
                export_manager.bake_v2_maps(config, object(), ["test_map"])
        assert "MapsOnV2Models" in str(caught.value)
        bake.assert_not_called()


def test_bake_v2_maps_refuses_a_map_with_no_export_directory() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        config = _v2_config(temporary)
        with (
            mock.patch("elysium_pipeline.map_transport.is_map_on_v2_models", return_value=True),
            mock.patch.object(export_manager, "bake_and_verify") as bake,
        ):
            with pytest.raises(export_manager.ExportBakeFailure) as caught:
                export_manager.bake_v2_maps(config, object(), ["never_exported"])
        assert "--intermediate-only" in str(caught.value)
        bake.assert_not_called()
