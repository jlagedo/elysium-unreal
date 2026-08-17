from __future__ import annotations

import copy
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock

from elysium_pipeline import character_inventory as ci, character_partition as cp
from elysium_pipeline.formats import eskm


DELTA = ci.DELTA_SEQUENCE

TREE = {"root": "", "spine": "root"}
BANK_A_TREE = {"root": "", "spine": "root", "tail": "spine"}
BANK_B_TREE = {"root": "", "spine": "root", "tail": "root"}


def _string(text):
    raw = text.encode("utf-8")
    return struct.pack("<I", len(raw)) + raw


def skel_section(tree):
    """The `SKEL` payload for {bone: parent name}, with a neutral bind pose."""
    names = list(tree)
    out = struct.pack("<I", len(names))
    for name in names:
        parent = tree[name]
        out += _string(name)
        out += struct.pack("<i", names.index(parent) if parent else -1)
        out += struct.pack("<3f", 0.0, 0.0, 0.0)
        out += struct.pack("<4f", 0.0, 0.0, 0.0, 1.0)
    return out


def anim_section(clips):
    """The `ANIM` payload for [(name, base, frames, flags, [(bone, has_t, has_r)])].

    Track data is written as zeroes at its declared width: nothing here decodes a pose, and the
    point of the fixture is that the reader steps over exactly the right number of bytes.
    """
    out = struct.pack("<I", len(clips))
    for name, base, frames, flags, tracks in clips:
        out += _string(name) + _string(base)
        out += struct.pack("<IfIiI", frames, 30.0, flags, -1, len(tracks))
        for bone, has_translation, has_rotation in tracks:
            out += struct.pack("<IBB", bone, has_translation, has_rotation)
            if has_translation:
                out += b"\0" * (12 * frames)
            if has_rotation:
                out += b"\0" * (16 * frames)
    return out


def container(sections):
    """An `.eskm` blob from [(tag, payload)], laid out as `UE_mdl_skeletal._assemble` writes it."""
    header = struct.pack("<4sIII", eskm.MAGIC, eskm.VERSION, len(sections), 0)
    offset = 16 + 20 * len(sections)
    directory = b""
    payload = b""
    for tag, data in sections:
        directory += struct.pack("<4sQQ", tag, offset, len(data))
        offset += len(data)
        payload += data
    return header + directory + payload


ONE_TRACK = [(0, 1, 1)]

# bank_a stands `walk`, and binds the two-cell `aim_layer` grid to it: both cells own the split
# bone, so they ship only as `<cell>@walk`. `recoil_delta` ships twice -- raw, which nothing can
# build, and composed onto the host that declares it.
BANK_A_CLIPS = [
    ("walk", "", 4, 0, ONE_TRACK),
    ("aim_layer@walk", "walk", 4, 0, ONE_TRACK),
    ("aim_layer_up@walk", "walk", 4, 0, ONE_TRACK),
    ("recoil_delta", "", 4, DELTA, ONE_TRACK),
    ("recoil_delta@walk", "walk", 4, DELTA, ONE_TRACK),
]

# bank_b stands an ordinary two-cell fan no host declares, so it ships under its plain label.
BANK_B_CLIPS = [
    ("run_0", "", 6, 0, ONE_TRACK),
    ("run_1", "", 6, 0, ONE_TRACK),
    ("move_yaw", "", 6, 0, ONE_TRACK),
]

MANIFEST = {
    "npcs": {
        "amy": {"clips": {"walk": "bank_a"}},
        "bob": {"clips": {"move_yaw": "bank_b"}},
    },
    "banks": {
        "bank_a": {
            "blends": "blends/bank_a.json",
            "clips": {
                "walk": {"flags": 0},
                "aim_layer": {"flags": 0},
                "aim_layer_up": {"flags": 0},
                "recoil_delta": {"flags": DELTA},
            },
        },
        "bank_b": {
            "blends": "blends/bank_b.json",
            "clips": {
                "run_0": {"flags": 0},
                "run_1": {"flags": 0},
                "move_yaw": {"flags": 0},
            },
        },
    },
}

BLENDS_A = {
    "grids": {
        "aim_layer": {
            "cells": [{"axis": [0, 0], "clip": "aim_layer"},
                      {"axis": [1, 0], "clip": "aim_layer_up"}],
        },
    },
    "autolayers": {"walk": ["aim_layer"]},
}

BLENDS_B = {
    "grids": {
        "move_yaw": {
            "cells": [{"axis": [0, 0], "clip": "run_0"}, {"axis": [1, 0], "clip": "run_1"}],
        },
        "lonely": {"cells": [{"axis": [0, 0], "clip": "run_0"}]},
    },
}


def bank_package(bank, name):
    return f"{ci.character_cache.BANKS}/{bank}/{name}"


class InventoryFixture(unittest.TestCase):
    """A two-bank cast on disk: one bank with derived forms and a bound grid, one plain."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.npc = self.root / "npc"
        (self.npc / "banks").mkdir(parents=True)
        (self.npc / "blends").mkdir(parents=True)
        self.write_bank("bank_a", BANK_A_TREE, BANK_A_CLIPS)
        self.write_bank("bank_b", BANK_B_TREE, BANK_B_CLIPS)
        self.write_blends("bank_a", BLENDS_A)
        self.write_blends("bank_b", BLENDS_B)
        self.manifest = copy.deepcopy(MANIFEST)
        self.partition = cp.build_partition(
            {"amy": TREE, "bob": TREE}, {"bank_a": BANK_A_TREE, "bank_b": BANK_B_TREE})

    def tearDown(self):
        self.tmp.cleanup()

    def write_bank(self, stem, tree, clips):
        (self.npc / "banks" / f"{stem}.eskm").write_bytes(
            container([(b"SKEL", skel_section(tree)), (b"ANIM", anim_section(clips))]))

    def write_blends(self, stem, document):
        (self.npc / "blends" / f"{stem}.json").write_text(
            json.dumps(document), encoding="utf-8")

    def prove(self, banks=("bank_a", "bank_b")):
        return ci.assert_cardinality(self.npc, self.partition, self.manifest, banks)


class ProjectionTests(InventoryFixture):
    def test_projects_the_sequences_the_bake_writes(self):
        inventory = ci.project_bank(self.npc, self.manifest, "bank_a")
        self.assertEqual(
            sorted(inventory.sequences),
            sorted(bank_package("bank_a", name) for name in (
                "A_aim_layer_up_walk", "A_aim_layer_walk", "A_recoil_delta_walk", "A_walk")),
        )

    def test_a_delta_no_host_declares_is_not_built(self):
        # It has nothing to be a difference FROM, so the bake writes no asset for it -- and the
        # source clip is still accounted for, because its composed form shipped.
        inventory = ci.project_bank(self.npc, self.manifest, "bank_a")
        self.assertEqual(inventory.unbound_additives, ["recoil_delta"])
        self.assertIn("recoil_delta", inventory.derived_only)
        self.assertEqual(self.prove()["unbound_additives"], 1)

    def test_a_bound_grid_ships_one_blend_space_per_declaring_host(self):
        inventory = ci.project_bank(self.npc, self.manifest, "bank_a")
        self.assertEqual(list(inventory.spaces), [bank_package("bank_a", "BS_aim_layer_walk")])

    def test_an_unbound_grid_ships_under_its_plain_label(self):
        inventory = ci.project_bank(self.npc, self.manifest, "bank_b")
        self.assertEqual(list(inventory.spaces), [bank_package("bank_b", "BS_move_yaw")])

    def test_a_grid_with_one_resolved_cell_is_not_a_blend_space(self):
        # `lonely` names one cell. One sample is a clip, and the bake writes no space for it.
        inventory = ci.project_bank(self.npc, self.manifest, "bank_b")
        self.assertNotIn(bank_package("bank_b", "BS_lonely"), inventory.spaces)

    def test_an_absent_container_is_reported_rather_than_raised(self):
        (self.npc / "banks" / "bank_b.eskm").unlink()
        summary = self.prove()
        self.assertEqual(summary["absent"], ["bank_b"])
        self.assertEqual(summary["banks"], 1)
        self.assertIn("were not proved", ci.summary_line(summary))


class InvariantTests(InventoryFixture):
    def test_a_clean_corpus_proves(self):
        summary = self.prove()
        self.assertEqual(summary["sequences"], 7)
        self.assertEqual(summary["blend_spaces"], 2)
        self.assertEqual(summary["packages"], 9)

    def test_adding_a_body_family_moves_no_bank_package(self):
        # The invariant itself: a bank is addressed by owner alone, so the projection cannot take
        # the model partition as an input and the count cannot follow it.
        before = self.prove()
        widened = copy.deepcopy(self.partition)
        widened["models"]["synthetic"] = {
            "members": ["synthetic_body"], "skeleton": "SKEL_Elysium_synthetic",
            "tree_fingerprint": "0" * 64, "bones": len(TREE),
        }
        widened["model_family_of"]["synthetic_body"] = "synthetic"
        self.partition = widened
        self.assertEqual(self.prove(), before)

    def test_a_cross_product_of_bank_clips_and_bodies_is_refused(self):
        # What the guard is for: a package addressed by anything but its bank's own namespace.
        # Reached by making the object path a function of a body family, which is precisely the
        # regression that once wrote 95 GB.
        original = ci.character_cache.bank_clips_object_path

        def below_a_body(bank):
            return original(bank).replace("/_banks/", "/amy/")

        with mock.patch.object(ci.character_cache, "bank_clips_object_path", below_a_body):
            with self.assertRaisesRegex(ValueError, "outside the shared bank namespace"):
                self.prove()

    def test_two_labels_folding_onto_one_package_are_named(self):
        self.write_bank("bank_b", BANK_B_TREE, [
            *BANK_B_CLIPS,
            ("run 0", "", 6, 0, ONE_TRACK),
        ])
        self.manifest["banks"]["bank_b"]["clips"]["run 0"] = {"flags": 0}
        with self.assertRaisesRegex(ValueError, "fold onto one package"):
            self.prove()

    def test_a_payload_with_no_frame_is_named(self):
        # The bake writes nothing for one and logs nothing about it, so a hole here is a hole on
        # the mount. It is not the deliberate absence a hostless delta is, and is not excused as
        # one even when it carries the same flag.
        self.write_bank("bank_b", BANK_B_TREE, [
            *BANK_B_CLIPS,
            ("sprint", "", 0, DELTA, ONE_TRACK),
        ])
        self.manifest["banks"]["bank_b"]["clips"]["sprint"] = {"flags": DELTA}
        with self.assertRaisesRegex(ValueError, "no frame or no track.*'sprint'"):
            self.prove()

    def test_a_source_clip_that_reaches_no_package_is_named(self):
        self.manifest["banks"]["bank_b"]["clips"]["sprint"] = {"flags": 0}
        with self.assertRaisesRegex(ValueError, "reach no package.*'sprint'"):
            self.prove()

    def test_a_packaged_clip_the_manifest_does_not_carry_is_named(self):
        self.manifest["banks"]["bank_a"]["clips"].pop("aim_layer_up")
        with self.assertRaisesRegex(ValueError, "does not account for"):
            self.prove()

    def test_a_derived_form_whose_host_is_unknown_is_named(self):
        self.manifest["banks"]["bank_a"]["clips"].pop("walk")
        with self.assertRaisesRegex(ValueError, "host 'walk'"):
            self.prove()

    def test_a_bank_outside_the_declared_partition_is_refused(self):
        with self.assertRaisesRegex(ValueError, "partition does not name"):
            ci.assert_cardinality(self.npc, self.partition, self.manifest, ["bank_c"])

    def test_an_unreadable_blend_sidecar_is_named(self):
        (self.npc / "blends" / "bank_a.json").write_text("{not json", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "blends/bank_a.json"):
            self.prove()


class ClipPayloadReaderTests(unittest.TestCase):
    def test_headers_step_over_track_data_of_every_width(self):
        clips = [
            ("first", "", 3, 0, [(0, 1, 1), (1, 0, 1), (2, 1, 0)]),
            ("second@first", "first", 12, DELTA, [(0, 0, 1)]),
            ("third", "", 1, 0, []),
        ]
        blob = container([(b"ANIM", anim_section(clips))])
        self.assertEqual(
            eskm.clip_payloads(blob),
            [eskm.ClipPayload("first", "", 3, 0, 3),
             eskm.ClipPayload("second@first", "first", 12, DELTA, 1),
             eskm.ClipPayload("third", "", 1, 0, 0)],
        )

    def test_a_container_with_no_clips_reads_empty(self):
        blob = container([(b"SKEL", skel_section(TREE))])
        self.assertEqual(eskm.clip_payloads(blob), [])


if __name__ == "__main__":
    unittest.main()
