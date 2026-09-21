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
    work = Path(temporary) / "work"
    work.mkdir(parents=True)
    units = Path(temporary) / "exports_v2"
    units.mkdir(parents=True)
    return SimpleNamespace(repo_root=repo, export_root=export, work_root=work,
                           export_v2_root=units)


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


def _clean_gate(failed: int = 0):
    """A nav gate that ran and found `failed` findings, without an editor."""
    return mock.patch.object(
        export_manager.nav_gate, "judge",
        return_value=export_manager.nav_gate.NavGateResult(
            maps=1, failed=failed, report_path=Path("report.json")))


def test_bake_launches_once_per_batch_and_trusts_the_exit() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        config = _config(temporary)
        with (
            mock.patch.object(export_manager.unreal, "bake_maps") as bake,
            mock.patch.object(export_manager.unreal, "verify_bakes") as verify,
            _clean_gate(),
        ):
            export_manager.bake_and_verify(config, object(), ["test_map", "test_map"])
        bake.assert_called_once_with(config, mock.ANY, ["test_map"], force=False,
                                     from_stage="",
                                     batch_size=export_manager.unreal.MAP_BAKE_BATCH)
        verify.assert_not_called()


def test_missing_baked_package_is_a_loud_failure() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        config = _config(temporary)
        with (
            mock.patch.object(export_manager.unreal, "bake_maps"),
            mock.patch.object(export_manager.unreal, "verify_bakes") as verify,
            _clean_gate() as gate,
        ):
            with pytest.raises(export_manager.ExportBakeFailure) as caught:
                export_manager.bake_and_verify(config, object(), ["absent_map"])
        assert "absent_map" in str(caught.value)
        verify.assert_not_called()
        # The missing `.umap` is caught before anything is judged: there is nothing to judge.
        gate.assert_not_called()


def test_verify_is_an_explicit_opt_in() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        config = _config(temporary)
        with (
            mock.patch.object(export_manager.unreal, "bake_maps"),
            mock.patch.object(export_manager.unreal, "verify_bakes") as verify,
            _clean_gate(),
        ):
            export_manager.bake_and_verify(
                config, object(), ["test_map"], verify=True
            )
        verify.assert_called_once_with(config, mock.ANY, ["test_map"])


def test_the_nav_gate_runs_on_every_bake_and_can_fail_it() -> None:
    """0018 story 21-2: the lane that builds the meshes judges them, by whichever door it was
    entered -- `bake map` and `export map` both come through here. `--skip-nav-verify` is for
    iterating on the lane itself."""
    with tempfile.TemporaryDirectory() as temporary:
        config = _config(temporary)
        with (
            mock.patch.object(export_manager.unreal, "bake_maps"),
            mock.patch.object(export_manager.unreal, "verify_bakes"),
            _clean_gate() as gate,
        ):
            export_manager.bake_and_verify(config, object(), ["test_map"])
        gate.assert_called_once()

        with (
            mock.patch.object(export_manager.unreal, "bake_maps"),
            mock.patch.object(export_manager.unreal, "verify_bakes"),
            _clean_gate(failed=3),
        ):
            with pytest.raises(export_manager.ExportBakeFailure) as caught:
                export_manager.bake_and_verify(config, object(), ["test_map"])
        assert "3 new finding" in str(caught.value)

        with (
            mock.patch.object(export_manager.unreal, "bake_maps"),
            mock.patch.object(export_manager.unreal, "verify_bakes"),
            _clean_gate(failed=3) as gate,
        ):
            export_manager.bake_and_verify(config, object(), ["test_map"],
                                           skip_nav_verify=True)
        gate.assert_not_called()


#: The three per-map asset manifests `unreal.bake_maps` hands the commandlet (0018 story 21-2) --
#: enough for a test that is about the argv rather than about the staging read.
_STAGED_MANIFESTS = {
    "entities": Path("entities.json"),
    "collision": Path("collision.json"),
    "environment": Path("environment.json"),
}


def test_one_launch_carries_the_three_per_map_asset_manifests() -> None:
    """0018 story 21-2: `bake map` is one editor boot for a loadable level.

    The entity table, the environment and the collision payload used to be three commands with
    three boots of their own, run after the bake, with the level unloadable in between. They ride
    this argv now, and `--from` rides beside them.
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

        with mock.patch.object(export_manager.unreal, "_stage_map_inputs",
                               return_value=_STAGED_MANIFESTS):
            export_manager.unreal.bake_maps(config, _Runner(), ["test_map"])
            argv = recorded[-1]
            assert "-BakeMapEntities=entities.json" in argv
            assert "-BakeMapCollision=collision.json" in argv
            assert "-BakeMapEnvironment=environment.json" in argv
            assert not any(item.startswith("-BakeFrom=") for item in argv)

            export_manager.unreal.bake_maps(config, _Runner(), ["test_map"],
                                            from_stage="collision")
            assert "-BakeFrom=collision" in recorded[-1]


def test_a_map_whose_offline_stage_failed_never_reaches_the_editor() -> None:
    """A level authored without one of the three assets cannot be loaded at all, so the refusal
    is before the launch rather than a line in a report read after it."""
    with tempfile.TemporaryDirectory() as temporary:
        config = _config(temporary)

        def _stage(_config, _maps):
            raise export_manager.unreal.MapStageFailure("collision/test_map: no <map>.hulls")

        with (
            mock.patch.object(export_manager.unreal, "_stage_map_inputs", side_effect=_stage),
            mock.patch.object(export_manager.unreal, "_run") as run,
        ):
            with pytest.raises(export_manager.unreal.MapStageFailure) as caught:
                export_manager.unreal.bake_maps(config, object(), ["test_map"])
        assert "no <map>.hulls" in str(caught.value)
        run.assert_not_called()


def test_map_receipt_keeps_deployed_root_and_tracks_native_catalogues(tmp_path) -> None:
    config = _config(str(tmp_path))
    expected = config.repo_root / "Plugins/ElysiumBaked/Content/test_map/test_map.umap"
    assert export_manager._baked_package(config, "test_map") == expected
    before = export_manager._maps_bake_fingerprint(config, ["test_map"])
    catalogue = config.repo_root / "Plugins/ElysiumBaked/Content/Models/_Corpus/DA_PlacedModels.uasset"
    catalogue.parent.mkdir(parents=True)
    catalogue.write_bytes(b"native references")
    assert export_manager._maps_bake_fingerprint(config, ["test_map"]) != before


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
                export_manager.native_model_pipeline, "import_map_dependencies",
                side_effect=lambda *_a, **_k: order.append("native models")),
            mock.patch.object(
                export_manager, "_bake_profile_maps",
                side_effect=lambda *_a, **_k: order.append("maps")),
        ):
            maps = export_manager.export_profile(config, object(), "all")
        assert maps == ["m1"]
        # The `all` profile carries the particles bundle, so the policy phase skips its
        # duplicate mirror; the cast and wield mounts precede the map bake. 0018 story 21-5
        # removed the `corpus` gate that used to sit between policy and the native models.
        assert order == ["offline", ("policy", True), "native models", "maps"]


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
    # No export directory is written: 0018 story 21-4 removed the `.env` gate that demanded one.
    # A map goes from its published `exports_v2` units to a level in this one command, and the
    # producer runs inside `_stage_map_inputs` to write the sidecars the lane still reads.
    config = _config(temporary)
    config.game_root = Path(temporary) / "game"
    config.work_root = Path(temporary) / "work"
    return config


def test_bake_v2_maps_bakes_each_named_map_once() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        config = _v2_config(temporary)
        with (
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
                                     from_stage="", verify=True,
                                     skip_nav_verify=False, on_line=None)


def test_bake_v2_maps_needs_no_export_directory() -> None:
    """0018 story 21-4: the lane opens nothing under `$ELYSIUM_EXPORT_ROOT/<map>/`, so a map that
    was never legacy-exported reaches the bake. What decides whether it can be baked is whether
    its units are published, and `_stage_map_inputs` says so by name if they are not."""

    with tempfile.TemporaryDirectory() as temporary:
        config = _v2_config(temporary)
        with (
            mock.patch.object(export_manager.native_model_pipeline, "require_map_prerequisites"),
            mock.patch.object(export_manager, "adopt_export_root"),
            mock.patch.object(export_manager, "ensure_world_material_content"),
            mock.patch.object(export_manager, "bake_and_verify") as bake,
        ):
            names = export_manager.bake_v2_maps(config, object(), ["never_exported"])
        assert names == ["never_exported"]
        bake.assert_called_once()
