from __future__ import annotations

import copy
import json
from pathlib import Path
import tempfile
import unittest

from elysium_pipeline import character_census as cc, character_inventory as ci


# Two bodies over two weapon banks that ship the SAME label, plus a bank of layers and grids.
#
# `amy`'s tree reaches `bank_onehand` first, so its `kick` wins and `bank_twohand`'s copy is a
# clip no body ever resolves there -- the shadowing the census exists to measure. `bank_layers`
# stands `walk`, binds `aim_layer` to it, fans `move_yaw` over two cells no body names on its own,
# and carries a `dead_fan` grid whose own label resolves nowhere.
MANIFEST = {
    "npcs": {
        "amy": {"clips": {"walk": "bank_layers", "move_yaw": "bank_layers",
                          "kick": "bank_onehand"}},
    },
    "banks": {
        "bank_layers": {
            "blends": "blends/bank_layers.json",
            "clips": {"walk": {}, "aim_layer": {}, "move_yaw": {},
                      "run_0": {}, "run_1": {}, "dead_0": {}, "dead_1": {}, "stray": {}},
        },
        "bank_onehand": {"clips": {"kick": {}}},
        "bank_twohand": {"clips": {"kick": {}}},
        "scene_bank": {"clips": {"performance": {}}},
    },
    "cinematics": {
        "models/cinematic/party.mdl": {"stem": "party", "roots": [{"root": "Bip01",
                                                                   "bank": "scene_bank"}]},
    },
}

BLENDS = {
    "grids": {
        "move_yaw": {"cells": [{"axis": [0, 0], "clip": "run_0"},
                               {"axis": [1, 0], "clip": "run_1"}]},
        "dead_fan": {"cells": [{"axis": [0, 0], "clip": "dead_0"},
                               {"axis": [1, 0], "clip": "dead_1"}]},
    },
    "autolayers": {"walk": ["aim_layer"]},
}

PARTITION = {"model_family_of": {"amy": "amy"}}

BANKS = ("bank_layers", "bank_onehand", "bank_twohand", "scene_bank")


def spaces(*labels):
    """A projected inventory that built a blend space for each of `labels`."""
    return ci.BankInventory(bank="bank_layers",
                            spaces={f"BS_{label}": label for label in labels})


class CensusFixture(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.npc = Path(self.tmp.name) / "npc"
        (self.npc / "blends").mkdir(parents=True)
        self.manifest = copy.deepcopy(MANIFEST)
        self.partition = copy.deepcopy(PARTITION)
        self.write_blends(BLENDS)

    def tearDown(self):
        self.tmp.cleanup()

    def write_blends(self, document):
        (self.npc / "blends" / "bank_layers.json").write_text(
            json.dumps(document), encoding="utf-8")

    def take(self):
        return cc.census(self.npc, self.manifest, BANKS)

    def prove(self, inventories=None):
        return cc.assert_reachable(
            self.npc, self.partition, self.manifest, BANKS,
            inventories if inventories is not None else {"bank_layers": spaces("move_yaw")})


class RouteTests(CensusFixture):
    def test_a_body_resolving_the_label_reaches_it(self):
        self.assertEqual(self.take()["bank_layers"].clips["walk"], cc.BY_LABEL)

    def test_a_layer_is_reached_through_the_host_that_declares_it(self):
        # `aim_layer` ships as `aim_layer@walk` and never under its plain label, so the host
        # binding is the route even though nothing else names the layer.
        self.assertEqual(self.take()["bank_layers"].clips["aim_layer"], cc.BY_HOST)

    def test_a_cell_of_a_grid_a_body_reaches_is_reached_content(self):
        result = self.take()["bank_layers"]
        self.assertEqual(result.clips["run_0"], cc.BY_GRID)
        self.assertEqual(result.clips["run_1"], cc.BY_GRID)
        self.assertEqual(result.grids["move_yaw"], cc.BY_LABEL)

    def test_a_cell_of_a_grid_no_body_reaches_is_distinguished_from_one_that_is(self):
        # The whole point of measuring over the DAG: `dead_0` and `run_0` are the same thing to a
        # reading of this container alone -- a plain-label clip no host declares.
        result = self.take()["bank_layers"]
        self.assertEqual(result.grids["dead_fan"], cc.UNREACHED)
        self.assertEqual(result.clips["dead_0"], cc.BY_DEAD_GRID)
        self.assertEqual(result.clips["dead_1"], cc.BY_DEAD_GRID)

    def test_a_clip_with_no_route_at_all_is_named(self):
        self.assertEqual(self.take()["bank_layers"].clips["stray"], cc.UNREACHED)

    def test_a_cell_shared_with_a_live_grid_is_reached_however_it_is_declared(self):
        document = copy.deepcopy(BLENDS)
        document["grids"]["dead_fan"]["cells"].append({"axis": [2, 0], "clip": "run_0"})
        self.write_blends(document)
        self.assertEqual(self.take()["bank_layers"].clips["run_0"], cc.BY_GRID)

    def test_a_grid_a_host_declares_is_reached_through_it(self):
        document = copy.deepcopy(BLENDS)
        document["autolayers"]["walk"] = ["aim_layer", "dead_fan"]
        self.write_blends(document)
        self.assertEqual(self.take()["bank_layers"].grids["dead_fan"], cc.BY_HOST)


class ShadowingTests(CensusFixture):
    def test_the_bank_a_body_resolves_the_label_to_owns_it(self):
        self.assertEqual(self.take()["bank_onehand"].clips["kick"], cc.BY_LABEL)

    def test_the_same_label_in_a_bank_no_tree_resolves_reaches_nobody(self):
        self.assertEqual(self.take()["bank_twohand"].clips["kick"], cc.UNREACHED)

    def test_a_cinematic_bank_is_not_censused(self):
        # Reached by a scene's anim-set root rather than by any body's clip map, so an include
        # DAG has nothing to say about it and a census would call the performance orphaned.
        self.assertNotIn("scene_bank", self.take())


class InvariantTests(CensusFixture):
    def test_a_clean_corpus_proves(self):
        summary = self.prove()
        self.assertEqual(summary["clips"][cc.BY_LABEL], 3)
        self.assertEqual(summary["clips"][cc.BY_HOST], 1)
        self.assertEqual(summary["clips"][cc.BY_GRID], 2)
        self.assertEqual(summary["clips"][cc.BY_DEAD_GRID], 2)
        self.assertEqual(summary["clips"][cc.UNREACHED], 2)
        self.assertEqual(summary["banks"], 3)
        self.assertIn("3 by label", cc.summary_line(summary))

    def test_a_host_no_body_reaches_is_refused(self):
        # The layer ships only as `aim_layer@walk`; a `walk` no tree resolves to this bank leaves
        # that asset addressed through a pose no body stands on, with no plain form to fall to.
        self.manifest["npcs"]["amy"]["clips"].pop("walk")
        with self.assertRaisesRegex(ValueError, "host 'walk'"):
            self.prove()

    def test_a_host_reached_only_as_a_grid_cell_is_accepted(self):
        # An aim layer riding a host the body reaches through a fan, never by that label.
        document = copy.deepcopy(BLENDS)
        document["grids"]["move_yaw"]["cells"].append({"axis": [2, 0], "clip": "walk"})
        self.write_blends(document)
        self.manifest["npcs"]["amy"]["clips"].pop("walk")
        self.assertEqual(self.prove()["clips"][cc.BY_HOST], 1)

    def test_a_reached_grid_that_stands_as_no_blend_space_is_refused(self):
        with self.assertRaisesRegex(ValueError, "grid 'move_yaw'"):
            self.prove(inventories={"bank_layers": spaces()})

    def test_a_grid_no_body_reaches_need_not_stand(self):
        self.assertEqual(self.prove()["grids"][cc.UNREACHED], 1)

    def test_a_derived_grid_stands_under_its_host_suffix(self):
        document = copy.deepcopy(BLENDS)
        document["autolayers"]["walk"] = ["aim_layer", "move_yaw"]
        self.write_blends(document)
        self.assertEqual(self.prove(inventories={"bank_layers": spaces("move_yaw@walk")})
                         ["grids"][cc.BY_HOST], 1)

    def test_a_manifest_short_of_a_declared_body_censuses_nothing(self):
        # A slice cannot decide orphanhood: the body left out is a route the census cannot see.
        self.partition["model_family_of"]["bob"] = "bob"
        summary = self.prove(inventories={"bank_layers": spaces()})
        self.assertEqual(summary["unproved"], ["bob"])
        self.assertIn("not censused", cc.summary_line(summary))


if __name__ == "__main__":
    unittest.main()
