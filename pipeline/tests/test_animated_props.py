"""Animated-prop export contract: the RLE clamp, pose selection, and index projection.

Every buffer here is synthesised in-code, so nothing depends on the user's game install.
"""

from __future__ import annotations

import struct
import json
import tempfile
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest import mock

from elysium_pipeline.exporters import npc_export
from elysium_pipeline.exporters import UE_mdl_skeletal as UEK
from elysium_pipeline.formats import mdl_skel
from elysium_pipeline import placed_models
from elysium_pipeline.exporters import source_warnings
from elysium_pipeline import export_manager


def _run(valid_keys, total, stored=None):
    """One `mstudioanimvalue_t` run: byte valid, byte total, then the int16 keys.

    `stored` truncates how many keys are actually written, which is how a shipped model whose
    final run overshoots the end of the file is reproduced.
    """
    keys = valid_keys if stored is None else valid_keys[:stored]
    return bytes([len(valid_keys), total]) + struct.pack(f"<{len(keys)}h", *keys)


class RleChannelTests(unittest.TestCase):
    def test_explicit_keys_decode_exactly(self) -> None:
        self.assertEqual(mdl_skel._rle_channel(_run([10, 20, 30], 3), 0, 3), [10, 20, 30])

    def test_frames_past_valid_hold_the_last_key(self) -> None:
        # Frames [0,valid) take the explicit keys; [valid,total) clamp to the last one.
        self.assertEqual(mdl_skel._rle_channel(_run([7, 9], 5), 0, 5), [7, 9, 9, 9, 9])

    def test_two_runs_are_concatenated(self) -> None:
        buffer = _run([1, 2], 2) + _run([5], 2)
        self.assertEqual(mdl_skel._rle_channel(buffer, 0, 4), [1, 2, 5, 5])

    def test_a_run_claiming_more_keys_than_the_file_stores_is_clamped(self) -> None:
        # `stage_light.mdl` is 11,400 bytes and a literal walk asks for 11,419. Retail's
        # ExtractAnimValue stops at the frame it wants and never notices; decoding every frame
        # eagerly has to clamp rather than raise.
        buffer = _run([3, 4, 5, 6], 4, stored=2)
        self.assertEqual(mdl_skel._rle_channel(buffer, 0, 4), [3, 4, 4, 4])

    def test_a_channel_that_ends_early_pads_to_numframes(self) -> None:
        self.assertEqual(mdl_skel._rle_channel(_run([2], 1), 0, 4), [2, 2, 2, 2])

    def test_a_truncated_header_terminates(self) -> None:
        self.assertEqual(mdl_skel._rle_channel(b"\x02", 0, 3), [0, 0, 0])

    def test_zero_valid_does_not_index_an_empty_key_list(self) -> None:
        self.assertEqual(mdl_skel._rle_channel(_run([], 3), 0, 3), [0, 0, 0])

    def test_a_zero_length_run_cannot_loop_forever(self) -> None:
        self.assertEqual(mdl_skel._rle_channel(_run([1], 0), 0, 2), [0, 0])


class _Seq:
    def __init__(self, frames):
        self.frames = frames


class HasAnimationTests(unittest.TestCase):
    def test_a_single_frame_sequence_is_an_authored_pose(self) -> None:
        # `stage_light`, `lampfloor`, `glassa`, `junkyardcraneb`, `bottleb` and `bottlec` each
        # declare exactly one 1-frame `idle` while authoring LoopSequence.
        with mock.patch.object(mdl_skel, "local_sequences", return_value=[_Seq(1)]):
            self.assertTrue(npc_export.has_animation(b""))

    def test_any_multi_frame_sequence_qualifies(self) -> None:
        # `clamp`'s `idle` is one frame beside its real 45-frame open/close, so the test is "any",
        # not "every".
        with mock.patch.object(mdl_skel, "local_sequences", return_value=[_Seq(1), _Seq(45)]):
            self.assertTrue(npc_export.has_animation(b""))

    def test_a_model_with_no_sequences_is_not_animation(self) -> None:
        with mock.patch.object(mdl_skel, "local_sequences", return_value=[]):
            self.assertFalse(npc_export.has_animation(b""))

    def test_an_undecodable_model_is_not_animation(self) -> None:
        with mock.patch.object(mdl_skel, "local_sequences", side_effect=struct.error("truncated")):
            self.assertFalse(npc_export.has_animation(b""))


class PlacedModelPolicyTests(unittest.TestCase):
    MODEL = "models/scenery/structural/doorknoba/drknobantique.mdl"

    @staticmethod
    def seq(label, activity="", weight=0, index=0):
        return SimpleNamespace(label=label, activity=activity, actweight=weight, index=index)

    def test_fnv_seed_has_cross_language_golden_vectors(self) -> None:
        self.assertEqual(placed_models.fnv1a_32(self.MODEL, 0), 2282856472)
        self.assertEqual(placed_models.fnv1a_32(self.MODEL, 42), 2895120338)

    def test_act_idle_candidates_preserve_declaration_order_and_weight(self) -> None:
        clips = [self.seq("open"), self.seq("idle_a", "ACT_IDLE", 1),
                 self.seq("idle_b", "ACT_IDLE", 9)]
        self.assertEqual([c.label for c in placed_models.rest_candidates(clips)],
                         ["idle_a", "idle_b"])
        picks = [placed_models.select_rest_sequence(self.MODEL, clips, token).label
                 for token in range(64)]
        self.assertIn("idle_a", picks)
        self.assertGreater(picks.count("idle_b"), picks.count("idle_a"))

    def test_sequence_zero_is_the_fallback(self) -> None:
        clips = [self.seq("declared_first"), self.seq("alphabetically_first")]
        self.assertEqual(placed_models.select_rest_sequence(self.MODEL, clips, 12).label,
                         "declared_first")

    def test_serialized_selection_matches_the_sequence_policy(self) -> None:
        clips = [self.seq("idle_a", "ACT_IDLE", 2), self.seq("idle_b", "ACT_IDLE", 7)]
        row = {"rest_candidates": ["idle_a", "idle_b"],
               "clips": [{"name": "idle_a", "weight": 2},
                         {"name": "idle_b", "weight": 7}]}
        for token in range(20):
            self.assertEqual(placed_models.select_rest_label(self.MODEL, row, token),
                             placed_models.select_rest_sequence(self.MODEL, clips, token).label)

    def test_discovery_covers_entities_and_new_game_lump_rows(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            map_dir = root / "sp_test"
            map_dir.mkdir()
            (map_dir / "sp_test.ents").write_text(json.dumps({"entities": [{
                "classname": "prop_switch", "targetname": "switch",
                "model_mesh": "models_scenery_structural_doorknoba_drknobantique",
                "keys": {"model": self.MODEL},
                "outputs": [{"target": "switch", "input": "SetAnimation"}],
            }]}), encoding="utf-8")
            second = "models/scenery/props/palm.mdl"
            (map_dir / "sp_test.props").write_text(
                "models_scenery_props_palm 0 0 0 0 0 0 1 0 0 0 %s\n" % second,
                encoding="utf-8")
            uses = placed_models.discover(str(root))
        self.assertEqual({use.model for use in uses}, {self.MODEL, second})
        switch = next(use for use in uses if use.model == self.MODEL)
        self.assertTrue(switch.full_clips)
        self.assertEqual(switch.static_stem,
                         "models_scenery_structural_doorknoba_drknobantique")
        self.assertEqual(switch.required_clips,
                         ("activate", "deactivate", "idle_off", "idle_on"))
        self.assertEqual(next(use for use in uses if use.model == second).static_stem,
                         "models_scenery_props_palm")

    def test_an_untargeted_switch_still_declares_its_intrinsic_clip_vocabulary(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            map_dir = root / "sp_test"
            map_dir.mkdir()
            (map_dir / "sp_test.ents").write_text(json.dumps({"entities": [{
                "classname": "prop_switch",
                "model_mesh": "models_scenery_switch",
                "keys": {"model": "models/scenery/switch.mdl"},
            }]}), encoding="utf-8")
            use = placed_models.discover(str(root))[0]
        self.assertFalse(use.full_clips)
        self.assertEqual(use.required_clips,
                         ("activate", "deactivate", "idle_off", "idle_on"))

    def test_conflicting_static_stems_fail_the_export_contract(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name, stem in (("a", "models_switch_a"), ("b", "models_switch_b")):
                map_dir = root / name
                map_dir.mkdir()
                (map_dir / f"{name}.ents").write_text(json.dumps({"entities": [{
                    "classname": "prop_dynamic", "model_mesh": stem,
                    "keys": {"model": "models/switch.mdl"},
                }]}), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "conflicting static stems"):
                placed_models.discover(str(root))

    def test_map_slice_reads_only_the_selected_maps(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name, model in (("a", "models/a.mdl"), ("b", "models/b.mdl")):
                map_dir = root / name
                map_dir.mkdir()
                (map_dir / f"{name}.ents").write_text(json.dumps({"entities": [{
                    "classname": "prop_dynamic",
                    "model_mesh": f"models_{name}",
                    "keys": {"model": model},
                }]}), encoding="utf-8")
                (map_dir / f"{name}.props").write_text("", encoding="utf-8")
            uses = placed_models.discover(str(root), map_names=["a"])
        self.assertEqual([use.model for use in uses], ["models/a.mdl"])
        self.assertEqual(uses[0].static_stem, "models_a")

    def test_missing_switch_clips_warn_for_any_model(self) -> None:
        missing = ["activate", "deactivate", "idle_off", "idle_on"]
        warning = source_warnings.missing_intrinsic_prop_clips_warning(
            "models/scenery/structural/warrens/warr_02_container_door.mdl", missing)
        self.assertEqual(warning["fallback"], "authored static rest pose")
        partial = source_warnings.missing_intrinsic_prop_clips_warning(
            "models/scenery/misc/curcuitbreaker/curcuitbreaker.mdl",
            ["deactivate"], declared=3)
        self.assertEqual(partial["fallback"], "the declared clip subset")
        self.assertIn("deactivate", partial["detail"])

    def test_legacy_game_lump_stem_joins_with_its_models_prefix(self) -> None:
        model = "models/scenery/street/payphone/payphone_pair.mdl"
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            map_dir = root / "sp_test"
            map_dir.mkdir()
            (map_dir / "sp_test.props").write_text(
                "models_scenery_street_payphone_payphone_pair 0 0 0 0 0 0 1 0 0 0\n",
                encoding="utf-8")
            uses = placed_models.discover(str(root), {model: object()})
        self.assertEqual([use.model for use in uses], [model])
        self.assertEqual(uses[0].static_stem,
                         "models_scenery_street_payphone_payphone_pair")

    def test_static_equivalence_accepts_identity_and_rejects_quarter_turn(self) -> None:
        bone = mdl_skel.Bone(index=0, name="root", parent=-1, flags=0,
                             pos=(0.0, 0.0, 0.0), quat=(0.0, 0.0, 0.0, 1.0),
                             pose_to_bone=(1.0, 0.0, 0.0, 0.0,
                                           0.0, 1.0, 0.0, 0.0,
                                           0.0, 0.0, 1.0, 0.0))
        surface = {"pos": [(1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 0.0)],
                   "nrm": [(0.0, 0.0, 1.0)] * 3,
                   "joints": [[0, 0, 0, 0]] * 3,
                   "weights": [[1.0, 0.0, 0.0, 0.0]] * 3,
                   "tris": [(0, 1, 2)]}
        sequence = SimpleNamespace(base=0, frames=1)
        identity = [[((0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0))]]
        quarter = [[((0.0, 0.0, 0.0), (0.0, 0.0, 2**-0.5, 2**-0.5))]]
        with (mock.patch.object(mdl_skel, "read_bones", return_value=[bone]),
              mock.patch.object(mdl_skel, "decode_skinned", return_value={"m": surface}),
              mock.patch.object(UEK, "_split_rotation_tracks", return_value={}),
              mock.patch.object(mdl_skel, "read_anim", return_value=identity)):
            self.assertTrue(placed_models.rest_pose_static_equivalent(b"", b"", [sequence]))
        with (mock.patch.object(mdl_skel, "read_bones", return_value=[bone]),
              mock.patch.object(mdl_skel, "decode_skinned", return_value={"m": surface}),
              mock.patch.object(UEK, "_split_rotation_tracks", return_value={}),
              mock.patch.object(mdl_skel, "read_anim", return_value=quarter)):
            self.assertFalse(placed_models.rest_pose_static_equivalent(b"", b"", [sequence]))


class PlacedModelClipEmissionTests(unittest.TestCase):
    def _write(self, labels, ensure=()):
        clips = [SimpleNamespace(label="idle"), SimpleNamespace(label="open")]
        seen = []
        ensured = []

        def anim(_data, _bones, emitted, _bone_map, _count, _masks, ensure_labels=(),
                 reparented=None):
            seen.extend(clip.label for clip in emitted)
            ensured.extend(ensure_labels)
            return b"", len(emitted)

        with (tempfile.TemporaryDirectory() as temporary,
                mock.patch.object(UEK.mdl, "load", return_value=(b"mdl", b"vtx")),
                mock.patch.object(mdl_skel, "read_bones", return_value=[]),
                mock.patch.object(mdl_skel, "decode_skinned", return_value={}),
                mock.patch.object(UEK.mdl, "search_paths", return_value=[]),
                mock.patch.object(UEK, "unreal_bones", return_value=([], {}, {})),
                mock.patch.object(UEK, "_mesh_section", return_value=(b"", {})),
                mock.patch.object(mdl_skel, "local_sequences", return_value=clips),
                mock.patch.object(mdl_skel, "blend_clip_plan", return_value=([], {})),
                mock.patch.object(UEK, "_attachment_section", return_value=b""),
                mock.patch.object(UEK, "_anim_section", side_effect=anim)):
            UEK.write_model({}, "models/test/prop.mdl", temporary, clip_labels=labels,
                            ensure_labels=ensure)
        return seen, ensured

    def test_rest_only_container_emits_only_selected_candidates(self) -> None:
        self.assertEqual(self._write(("idle",), ("idle",)), (["idle"], ["idle"]))

    def test_full_container_emits_the_complete_sequence_inventory(self) -> None:
        self.assertEqual(self._write(None), (["idle", "open"], []))

    def test_clip_policy_and_inventory_invalidate_the_container_fingerprint(self) -> None:
        rest = {"clip_mode": "rest", "clips": {"idle": {}},
                "rest_candidates": ["idle"]}
        full = {"clip_mode": "full", "clips": {"idle": {}, "open": {}}}
        first = export_manager.placed_model_source_fingerprint(
            "prop", rest, "code", "source")
        self.assertNotEqual(first, export_manager.placed_model_source_fingerprint(
            "prop", full, "code", "source"))

    def test_ensured_rest_forces_complete_bind_local_tracks(self) -> None:
        clip = mdl_skel.Seq(label="idle", base=0, frames=1, fps=30.0,
                            activity="", actweight=0, flags=0)
        forced = []

        def payload(*_args, **kwargs):
            forced.append(kwargs.get("forced_channels"))
            return b"clip"

        with (mock.patch.object(UEK, "_derived_bindings", return_value=[]),
              mock.patch.object(UEK, "_cell_names", return_value={}),
              mock.patch.object(UEK, "_clip_payload", side_effect=payload)):
            _blob, count = UEK._anim_section(
                b"", [SimpleNamespace()], [clip], [0], 1, {}, ensure_labels=("idle",))
        self.assertEqual(count, 1)
        self.assertEqual(forced, [[(True, True)]])

class CompactRigidSkinTests(unittest.TestCase):
    def test_a_compact_one_bone_model_binds_every_vertex_to_its_bone(self) -> None:
        data = bytearray(244)
        struct.pack_into("<i", data, 240, 1)
        self.assertEqual(mdl_skel.read_skin(bytes(data), 0, 0, 3, vlist=2),
                         [([0], [1.0]), ([0], [1.0]), ([0], [1.0])])

    def test_a_compact_multi_bone_model_is_refused_as_ambiguous(self) -> None:
        data = bytearray(244)
        struct.pack_into("<i", data, 240, 2)
        with self.assertRaisesRegex(ValueError, "rigid binding is ambiguous"):
            mdl_skel.read_skin(bytes(data), 0, 0, 1, vlist=1)


class CompleteOwnedPoseTests(unittest.TestCase):
    def setUp(self) -> None:
        self.bones = [mdl_skel.Bone(index=0, name="root", parent=-1, flags=0,
                                    pos=(1.0, 2.0, 3.0),
                                    quat=(0.0, 0.0, 0.0, 1.0))]
        self.clip = mdl_skel.Seq(label="idle", base=0, frames=1, fps=30.0,
                                 activity="", actweight=0, flags=0)
        self.pose = [[((1.0, 2.0, 3.0), (0.0, 0.0, 0.0, 1.0))]]

    def test_an_owned_bind_only_frame_writes_both_local_tracks(self) -> None:
        with (mock.patch.object(UEK, "_authored_channels", return_value=[(False, False)]),
              mock.patch.object(UEK, "_owned_bones", return_value={0}),
              mock.patch.object(UEK, "_bone_mask", return_value=None),
              mock.patch.object(mdl_skel, "read_anim", return_value=self.pose)):
            payload = UEK._clip_payload(
                b"", self.bones, self.clip, [0], 1, {})
        self.assertIsNotNone(payload)
        offset = 0
        for _ in range(2):
            length = struct.unpack_from("<I", payload, offset)[0]
            offset += 4 + length
        _frames, _fps, _flags, _mask, tracks = struct.unpack_from("<IfIiI", payload, offset)
        offset += struct.calcsize("<IfIiI")
        bone, has_translation, has_rotation = struct.unpack_from("<I2B", payload, offset)
        self.assertEqual((tracks, bone, has_translation, has_rotation), (1, 0, 1, 1))

    def test_a_zero_weight_bone_does_not_become_a_track(self) -> None:
        with (mock.patch.object(UEK, "_authored_channels", return_value=[(False, False)]),
              mock.patch.object(UEK, "_owned_bones", return_value=set()),
              mock.patch.object(mdl_skel, "read_anim", return_value=self.pose)):
            self.assertIsNone(UEK._clip_payload(
                b"", self.bones, self.clip, [0], 1, {}))

    def test_an_additive_forces_its_owned_bind_only_bones_onto_the_host(self) -> None:
        host = self.clip._replace(label="host")
        layer = self.clip._replace(label="delta", flags=UEK.DELTA_SEQUENCE)
        with (mock.patch.object(UEK, "_derived_bindings",
                               return_value=[(layer, host, None)]),
              mock.patch.object(UEK, "_owned_channels",
                               side_effect=[[(True, True)], [(False, False)]]),
              mock.patch.object(UEK, "_composed_frames", return_value=self.pose),
              mock.patch.object(UEK, "_clip_payload", return_value=b"clip") as payload):
            UEK._anim_section(b"", self.bones, [host, layer], [0], 1, {})

        host_call = next(call for call in payload.call_args_list
                         if call.args[2].label == "host")
        self.assertEqual(host_call.kwargs["forced_channels"], [(True, True)])

    def test_a_forced_host_track_uses_donor_bind_not_zero_weight_sentinels(self) -> None:
        masked_frame = [[((0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 0.0))]]
        with (mock.patch.object(UEK, "_authored_channels", return_value=[(False, False)]),
              mock.patch.object(UEK, "_owned_bones", return_value=set()),
              mock.patch.object(UEK, "_bone_mask", return_value=None),
              mock.patch.object(mdl_skel, "read_anim", return_value=masked_frame)):
            payload = UEK._clip_payload(
                b"", self.bones, self.clip, [0], 1, {},
                forced_channels=[(True, True)])

        self.assertIsNotNone(payload)
        offset = 0
        for _ in range(2):
            length = struct.unpack_from("<I", payload, offset)[0]
            offset += 4 + length
        offset += struct.calcsize("<IfIiI")
        bone, has_translation, has_rotation = struct.unpack_from("<I2B", payload, offset)
        offset += struct.calcsize("<I2B")
        position = struct.unpack_from("<3f", payload, offset)
        offset += struct.calcsize("<3f")
        rotation = struct.unpack_from("<4f", payload, offset)
        self.assertEqual((bone, has_translation, has_rotation), (0, 1, 1))
        for actual, expected in zip(position, UEK._conv_pos(self.bones[0].pos)):
            self.assertAlmostEqual(actual, expected, places=5)
        for actual, expected in zip(rotation, UEK._conv_quat(self.bones[0].quat)):
            self.assertAlmostEqual(actual, expected, places=6)


class AnimatedPropIndexRowTests(unittest.TestCase):
    # `drknobantique`'s real shape: declaration order puts `idle` first, alphabetical order would
    # put `handle_locked` first, and retail's rest pose is sequence index 0.
    RECORD = {
        "eskm": "animated_props/drknobantique.eskm",
        "model": "models/scenery/doorknoba/drknobantique.mdl",
        "bones": 2,
        "clips": {
            "idle": {"activity": "", "weight": 0, "flags": 1, "frames": 16, "fps": 15.0},
            "handle_locked": {"activity": "", "weight": 0, "flags": 0, "frames": 16, "fps": 15.0},
            "handle_unlocked": {"activity": "", "weight": 0, "flags": 0, "frames": 16, "fps": 15.0},
        },
    }

    def test_declaration_order_is_preserved(self) -> None:
        row = npc_export.animated_prop_index_row(self.RECORD)
        self.assertEqual([c["name"] for c in row["clips"]],
                         ["idle", "handle_locked", "handle_unlocked"])

    def test_each_clip_carries_its_ordinal_and_selection_keys(self) -> None:
        row = npc_export.animated_prop_index_row(self.RECORD)
        self.assertEqual([c["index"] for c in row["clips"]], [0, 1, 2])
        first = row["clips"][0]
        self.assertEqual(first["flags"], 1)          # STUDIO_LOOPING
        self.assertEqual(first["activity"], "")
        self.assertEqual(first["frames"], 16)
        self.assertEqual(first["fps"], 15.0)

    def test_optional_sidecars_are_omitted_when_absent(self) -> None:
        row = npc_export.animated_prop_index_row(self.RECORD)
        self.assertNotIn("procedural", row)
        self.assertNotIn("blends", row)
        self.assertEqual(row["split_bones"], [])

    def test_a_clipless_record_projects_an_empty_list(self) -> None:
        row = npc_export.animated_prop_index_row({"eskm": "e", "model": "m"})
        self.assertEqual(row["clips"], [])

    def test_a_bounds_radius_reaches_the_runtime_row(self) -> None:
        record = dict(self.RECORD, clips={
            "idle": dict(self.RECORD["clips"]["idle"], bounds_radius_m=22.388)})
        row = npc_export.animated_prop_index_row(record)
        self.assertEqual(row["clips"][0]["bounds_radius_m"], 22.388)


def _seq(label, bbmin, bbmax):
    return mdl_skel.Seq(label=label, base=0, frames=16, fps=30.0, activity="",
                        actweight=0, flags=0, bbmin=bbmin, bbmax=bbmax)


class ClipBoundsRadiusTests(unittest.TestCase):
    """What a clip declares about its own reach, reconciled with what baked.

    `cin_sheriff_sword`'s `scene` is the shape these guard: a 2 m mesh whose sequence bbox
    spans 881 Source units because the rig carries the sword across the courtroom.
    """

    SWORD = _seq("scene", (-881.4, -162.6, -6.7), (0.0, 89.3, 166.2))

    def test_the_authored_box_reduces_to_its_largest_coordinate(self) -> None:
        self.assertAlmostEqual(npc_export.authored_radius_m(self.SWORD), 881.4 * 0.0254, places=6)

    def test_the_authored_radius_wins_when_it_covers_the_bake(self) -> None:
        # The real case: studiomdl's box sits a little outside the extent it was computed from.
        self.assertAlmostEqual(npc_export.clip_bounds_radius_m(self.SWORD, 21.833),
                               881.4 * 0.0254, places=6)

    def test_a_zeroed_descriptor_falls_back_to_the_bake(self) -> None:
        # Every model's header ViewBBMin/ViewBBMax is (0,0,0) in this corpus, so a sequence box
        # that was never filled in is a shape the export has to survive rather than trust.
        blank = _seq("scene", (0.0, 0.0, 0.0), (0.0, 0.0, 0.0))
        self.assertEqual(npc_export.authored_radius_m(blank), 0.0)
        self.assertAlmostEqual(npc_export.clip_bounds_radius_m(blank, 9.99), 9.99, places=6)

    def test_an_authored_radius_short_of_the_bake_loses(self) -> None:
        short = _seq("scene", (-1.0, 0.0, 0.0), (1.0, 0.0, 0.0))
        self.assertAlmostEqual(npc_export.clip_bounds_radius_m(short, 4.0), 4.0, places=6)

    def test_the_key_is_absent_until_it_has_been_reconciled(self) -> None:
        # Presence is the promise that the number covers the geometry; a bank or NPC clip, which
        # nothing measures, must not look like it carries one.
        self.assertNotIn("bounds_radius_m", npc_export._clip_meta(self.SWORD))
        self.assertEqual(npc_export._clip_meta(self.SWORD, 22.38812)["bounds_radius_m"], 22.3881)


class ClipExtentTests(unittest.TestCase):
    """The measured half of the same question: how far a bone chain actually reaches."""

    def test_a_rotated_parent_carries_its_child_out(self) -> None:
        bones = [mdl_skel.Bone(index=0, parent=-1), mdl_skel.Bone(index=1, parent=0)]
        # Frame 0 rests; frame 1 turns the root a quarter turn about Z, which swings the child's
        # 100-unit local offset onto +Y. Either way the reach is 100 units = 2.54 m.
        rest = [((0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0)), ((100.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0))]
        turn = [((0.0, 0.0, 0.0), (0.0, 0.0, 0.7071067811865476, 0.7071067811865476)),
                ((100.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0))]
        with mock.patch.object(mdl_skel, "read_anim", return_value=[rest, turn]):
            self.assertAlmostEqual(mdl_skel.clip_extent(b"", bones, 0, 2), 100.0 * 0.0254, places=6)

    def test_a_clip_with_no_bones_or_no_frames_claims_nothing(self) -> None:
        self.assertEqual(mdl_skel.clip_extent(b"", [], 0, 16), 0.0)
        self.assertEqual(mdl_skel.clip_extent(b"", [mdl_skel.Bone(index=0, parent=-1)], 0, 0), 0.0)

    def test_the_vectorised_rotation_matches_the_scalar_one(self) -> None:
        import numpy as np
        quats = np.array([[0.0, 0.0, 0.0, 1.0],
                          [0.5, -0.5, 0.5, 0.5],
                          [0.0, 0.0, 0.7071067811865476, 0.7071067811865476]])
        stacked = mdl_skel.rot_matrices(quats)
        for i, q in enumerate(quats):
            np.testing.assert_allclose(stacked[i], mdl_skel.rot_matrix(q), atol=1e-12)


if __name__ == "__main__":
    unittest.main()
