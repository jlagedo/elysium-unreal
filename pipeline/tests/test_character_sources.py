"""Character-source orchestration: the process-pool worker seam, the prefix-bucketed
install-detail fingerprint, the section-read partition pass, the single-parse manifest
plumbing, and the sweep receipt.

Every container here is synthesised in-code from the documented layouts; nothing depends on
the user's game install.
"""
from __future__ import annotations

import json
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace
from unittest import mock

import pytest

from elysium_pipeline import character_sweep, export_manager, workers
from elysium_pipeline.formats import eskm, install
from elysium_pipeline.tasking import Manifest

from pipeline.tests.test_eskm_sections import container_bytes


def _string(value: str) -> bytes:
    encoded = value.encode("utf-8")
    return struct.pack("<I", len(encoded)) + encoded


def skel_payload(bones) -> bytes:
    """One SKEL section: count, then (name, parent, translation, rotation) per bone."""
    out = struct.pack("<I", len(bones))
    for name, parent in bones:
        out += _string(name) + struct.pack("<i", parent) + b"\0" * (12 + 16)
    return out


def matl_payload(materials) -> bytes:
    out = struct.pack("<I", len(materials))
    for name, albedo in materials.items():
        out += _string(name) + _string(albedo)
    return out


def test_worker_captures_output_and_forwards_the_write() -> None:
    from elysium_pipeline.exporters import UE_mdl_skeletal

    with tempfile.TemporaryDirectory() as temporary:
        index = {"models/amy.mdl": ("vpk", ("entry", 1))}

        def write(_index, model_rel, out_dir, **_kwargs):
            print(f"wrote {model_rel}")

        with (
            mock.patch.object(install, "build_index", return_value=index),
            mock.patch.object(UE_mdl_skeletal, "write_model", side_effect=write),
            mock.patch.object(workers, "_ANORMS", [None]),
        ):
            out = workers.character_source_worker(
                "model", "amy", "models/amy.mdl", temporary)
        assert "wrote models/amy.mdl" in out


def test_worker_refuses_an_unknown_kind_loudly() -> None:
    with pytest.raises(RuntimeError, match="unknown character source kind"):
        workers.character_source_worker("nope", "amy", "models/amy.mdl", "out")


def test_worker_round_trips_a_spawned_process() -> None:
    # Windows spawns rather than forks, so the worker, its arguments and its raised
    # errors must all cross a real process boundary. The unknown-kind error is raised
    # before any install read, which keeps this game-independent.
    from concurrent.futures import ProcessPoolExecutor

    with ProcessPoolExecutor(max_workers=1) as pool:
        future = pool.submit(
            workers.character_source_worker, "nope", "amy", "models/amy.mdl", "out")
        with pytest.raises(RuntimeError, match="unknown character source kind"):
            future.result(timeout=120)


def test_worker_failure_carries_the_captured_tail() -> None:
    from elysium_pipeline.exporters import UE_mdl_skeletal

    def explode(*_args, **_kwargs):
        print("decoding amy")
        raise ValueError("bad sequence block")

    with (
        mock.patch.object(install, "build_index", return_value={}),
        mock.patch.object(UE_mdl_skeletal, "write_bank", side_effect=explode),
    ):
        with pytest.raises(RuntimeError, match="decoding amy"):
            workers.character_source_worker("bank", "bank_a", "models/bank_a.mdl", "out")


INDEX = {
    "models/character/x.mdl": ("vpk", 1),
    "models/character/x.dx80.vtx": ("vpk", 2),
    "models/character/x.vvd": ("loose", "E:/nowhere/x.vvd"),
    "models/character/xy.mdl": ("vpk", 4),
    "models/dir.v2/x.mdl": ("vpk", 5),
    "models/dir.v2/x.vvd": ("vpk", 6),
    "models/character/x.foo.mdl": ("vpk", 7),
    "models/character/x.foo.vvd": ("vpk", 8),
    "models/character/noext": ("vpk", 9),
}


def test_sibling_files_move_the_detail_and_foreign_stems_do_not() -> None:
    buckets = export_manager._index_prefix_buckets(INDEX)
    base = export_manager._character_source_detail(
        INDEX, "models/character/x.mdl", buckets)
    grown = dict(INDEX)
    grown["models/character/x.ani"] = ("vpk", 10)
    assert base != export_manager._character_source_detail(
        grown, "models/character/x.mdl",
        export_manager._index_prefix_buckets(grown))
    foreign = dict(INDEX)
    foreign["models/character/xz.mdl"] = ("vpk", 11)
    assert base == export_manager._character_source_detail(
        foreign, "models/character/x.mdl",
        export_manager._index_prefix_buckets(foreign))


MANIFEST = {
    "npcs": {"amy": {"model": "models/amy.mdl", "clips": {"idle": "amy"}}},
    "banks": {},
    "cinematics": {},
    "placed_models": {},
}


def _config(temporary: str) -> SimpleNamespace:
    root = Path(temporary)
    (root / "exports" / "npc").mkdir(parents=True)
    return SimpleNamespace(
        repo_root=root / "repo",
        export_root=root / "exports",
        log_root=root / "logs",
    )


def test_inline_dispatch_writes_once_and_a_warm_second_run_skips() -> None:
    from elysium_pipeline.exporters import UE_mdl_skeletal

    with tempfile.TemporaryDirectory() as temporary:
        config = _config(temporary)
        npc_dir = config.export_root / "npc"
        index = {"models/amy.mdl": ("vpk", ("entry", 1))}

        def write(_index, _model_rel, out_dir, *, stem, **_kwargs):
            (Path(out_dir) / f"{stem}.eskm").write_bytes(b"container")

        with (
            mock.patch.object(install, "build_index", return_value=index),
            mock.patch.object(UE_mdl_skeletal, "write_model", side_effect=write) as writer,
            mock.patch.object(workers, "_ANORMS", [None]),
        ):
            manifest = Manifest(config.export_root / ".elysium-manifest.json")
            bodies, banks = export_manager.write_character_sources(
                config, npc_dir, manifest=manifest, jobs=1,
                npc_manifest=MANIFEST)
            assert (bodies, banks) == (["amy"], [])
            assert writer.call_count == 1

            manifest = Manifest(config.export_root / ".elysium-manifest.json")
            export_manager.write_character_sources(
                config, npc_dir, manifest=manifest, jobs=1,
                npc_manifest=MANIFEST)
            assert writer.call_count == 1


def test_force_rewrites_the_container() -> None:
    from elysium_pipeline.exporters import UE_mdl_skeletal

    with tempfile.TemporaryDirectory() as temporary:
        config = _config(temporary)
        npc_dir = config.export_root / "npc"
        index = {"models/amy.mdl": ("vpk", ("entry", 1))}

        def write(_index, _model_rel, out_dir, *, stem, **_kwargs):
            (Path(out_dir) / f"{stem}.eskm").write_bytes(b"container")

        with (
            mock.patch.object(install, "build_index", return_value=index),
            mock.patch.object(UE_mdl_skeletal, "write_model", side_effect=write) as writer,
            mock.patch.object(workers, "_ANORMS", [None]),
        ):
            manifest = Manifest(config.export_root / ".elysium-manifest.json")
            export_manager.write_character_sources(
                config, npc_dir, manifest=manifest, jobs=1,
                npc_manifest=MANIFEST)
            export_manager.write_character_sources(
                config, npc_dir, manifest=manifest, jobs=1, force=True,
                npc_manifest=MANIFEST)
            assert writer.call_count == 2


PARTITION_SECTION_READ_MANIFEST = {
    "npcs": {"amy": {"model": "models/amy.mdl",
                     "clips": {"idle": "amy", "walk": "bank_a"}}},
    "banks": {"bank_a": {"model": "models/bank_a.mdl"}},
    "cinematics": {},
    "placed_models": {"switch": {"model": "models/switch.mdl"}},
}


def _npc_dir(temporary: str) -> Path:
    npc_dir = Path(temporary) / "npc"
    (npc_dir / "banks").mkdir(parents=True)
    (npc_dir / "placed_models").mkdir()
    (npc_dir / "amy.eskm").write_bytes(container_bytes({
        b"SKEL": skel_payload((("root", -1), ("spine", 0))),
        b"MATL": matl_payload({"skin": "tex/amy_body.png"}),
        b"ANIM": b"\x01" * 512,
    }))
    (npc_dir / "banks" / "bank_a.eskm").write_bytes(container_bytes({
        b"SKEL": skel_payload((("root", -1), ("spine", 0))),
        b"ANIM": b"\x02" * 512,
    }))
    (npc_dir / "placed_models" / "switch.eskm").write_bytes(container_bytes({
        b"SKEL": skel_payload((("gear", -1),)),
    }))
    return npc_dir


def test_partition_and_texture_table_come_off_the_section_reads() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        npc_dir = _npc_dir(temporary)
        partition = export_manager.write_character_partition(
            npc_dir, npc_manifest=PARTITION_SECTION_READ_MANIFEST)
        assert sorted(partition["model_family_of"]) == ["amy"]
        assert sorted(partition["bank_family_of"]) == ["bank_a"]
        with (npc_dir / "textures.json").open(encoding="utf-8") as handle:
            textures = json.load(handle)
        (name, entry), = textures["textures"].items()
        assert entry == {"uri": "tex/amy_body.png", "used_by": ["amy"]}
        assert textures["bindings"] == {"amy": {"skin": name}}


def test_a_corrupt_prop_container_fails_loudly() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        npc_dir = _npc_dir(temporary)
        (npc_dir / "placed_models" / "switch.eskm").write_bytes(b"NOPE not a container")
        with pytest.raises(export_manager.OfflineExportFailure,
                                    match="switch.eskm"):
            export_manager.write_character_partition(
                npc_dir, npc_manifest=PARTITION_SECTION_READ_MANIFEST)


def test_a_stale_model_container_fails_loudly() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        npc_dir = _npc_dir(temporary)
        (npc_dir / "amy.eskm").write_bytes(container_bytes(
            {b"SKEL": skel_payload((("root", -1),))}, version=eskm.VERSION + 1))
        with pytest.raises(export_manager.OfflineExportFailure, match="amy.eskm"):
            export_manager.write_character_partition(
                npc_dir, npc_manifest=PARTITION_SECTION_READ_MANIFEST)


def test_a_supplied_manifest_needs_no_file_on_disk() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        npc_dir = Path(temporary)  # deliberately carries no npc_manifest.json
        manifest = {
            "npcs": {"amy": {"model": "models/amy.mdl", "clips": {"idle": "amy"}}},
            "banks": {},
            "cinematics": {},
            "placed_models": {},
        }
        models, banks, cinematics, props = export_manager.character_source_plan(
            npc_dir, manifest)
        assert models == {"amy": "models/amy.mdl"}
        assert (banks, cinematics, props) == ({}, {}, {})


def _sweep_receipt_config(temporary: str) -> SimpleNamespace:
    root = Path(temporary)
    (root / "exports" / "npc").mkdir(parents=True)
    return SimpleNamespace(repo_root=root / "repo", export_root=root / "exports")

CLEAN = {"orphan_assets": [], "orphan_dirs": [], "total": 0,
         "removed": 0, "refused": ""}


def test_unchanged_inputs_skip_the_mount_scan_and_force_defeats_it() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        config = _sweep_receipt_config(temporary)
        npc_dir = config.export_root / "npc"
        (npc_dir / "families.json").write_text("{}", encoding="utf-8")
        (npc_dir / "textures.json").write_text("{}", encoding="utf-8")
        with mock.patch.object(character_sweep, "sweep",
                               return_value=dict(CLEAN)) as swept:
            first = export_manager.sweep_characters(
                config, {}, skip_unchanged=True)
            second = export_manager.sweep_characters(
                config, {}, skip_unchanged=True)
            third = export_manager.sweep_characters(
                config, {}, skip_unchanged=True, force=True)
        assert swept.call_count == 2
        assert "skipped" not in first
        assert second["skipped"]
        assert "skipped" not in third


def test_a_moved_partition_reopens_the_scan() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        config = _sweep_receipt_config(temporary)
        npc_dir = config.export_root / "npc"
        (npc_dir / "families.json").write_text("{}", encoding="utf-8")
        (npc_dir / "textures.json").write_text("{}", encoding="utf-8")
        with mock.patch.object(character_sweep, "sweep",
                               return_value=dict(CLEAN)) as swept:
            export_manager.sweep_characters(config, {}, skip_unchanged=True)
            (npc_dir / "families.json").write_text(
                '{"models": {}}', encoding="utf-8")
            export_manager.sweep_characters(config, {}, skip_unchanged=True)
        assert swept.call_count == 2


def test_a_refused_sweep_writes_no_receipt() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        config = _sweep_receipt_config(temporary)
        npc_dir = config.export_root / "npc"
        (npc_dir / "families.json").write_text("{}", encoding="utf-8")
        (npc_dir / "textures.json").write_text("{}", encoding="utf-8")
        refused = dict(CLEAN, refused="66% look orphaned")
        with mock.patch.object(character_sweep, "sweep", return_value=refused):
            export_manager.sweep_characters(config, {}, skip_unchanged=True)
        assert not (config.export_root / export_manager.SWEEP_RECEIPT_FILE).is_file()


def _cloth_gate_config(temporary: str) -> SimpleNamespace:
    root = Path(temporary)
    (root / "exports" / "npc" / "garment").mkdir(parents=True)
    return SimpleNamespace(repo_root=root / "repo", export_root=root / "exports")


def test_only_stale_garments_earn_a_launch() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        config = _cloth_gate_config(temporary)
        garment_dir = config.export_root / "npc" / "garment"
        (garment_dir / "amy.json").write_text("{}", encoding="utf-8")
        # amy authors a garment with no generated asset yet -> stale; bob authors none.
        assert export_manager._cloth_bake_stems(config, ["amy", "bob"]) == ["amy"]
        cloth = config.repo_root / "Plugins" / "ElysiumBaked" / "Content" / "Characters" / "Cloth"
        cloth.mkdir(parents=True)
        (cloth / "CLOTH_amy.uasset").write_bytes(b"cloth")
        future = (garment_dir / "amy.json").stat().st_mtime + 60
        import os

        os.utime(cloth / "CLOTH_amy.uasset", (future, future))
        assert export_manager._cloth_bake_stems(config, ["amy", "bob"]) == []
        # --force launches for every authored garment, and never for unauthored stems.
        assert export_manager._cloth_bake_stems(config, ["amy", "bob"], force=True) == ["amy"]
