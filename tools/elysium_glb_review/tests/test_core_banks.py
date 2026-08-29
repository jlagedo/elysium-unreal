"""Contract tests for the animation-bank closure.

The graph the corpus actually contains has stubs that carry no clips, diamonds that
reach the same bank two ways, and at least one body naming a bank that was never
exported. All three appear here.
"""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from core import banks, glb

from . import support


class ClosureTests(unittest.TestCase):
    def setUp(self) -> None:
        self._scratch = tempfile.TemporaryDirectory()
        self.root = Path(self._scratch.name)
        self.addCleanup(self._scratch.cleanup)

    def _corpus(self, units: dict[str, bytes]) -> None:
        support.write_corpus(self.root, units)

    def _closure(self, body_relative: str) -> banks.Closure:
        document = glb.read_json(self.root / body_relative)
        return banks.closure(document, self.root)

    def test_walks_through_a_stub_to_the_banks_that_hold_clips(self) -> None:
        # A body names one bank; the clips live two levels further down.
        self._corpus(
            {
                "characters/npc/body.glb": support.character_unit(
                    "vtmb:character-body:npc/body",
                    banks=["vtmb:animation-bank:shared/all"],
                ),
                "characters/shared/all.glb": support.character_unit(
                    "vtmb:character-body:shared/all",
                    banks=[
                        "vtmb:animation-bank:shared/idles",
                        "vtmb:animation-bank:shared/combat",
                    ],
                ),
                "characters/shared/idles.glb": support.character_unit(
                    "vtmb:character-body:shared/idles", animations=4
                ),
                "characters/shared/combat.glb": support.character_unit(
                    "vtmb:character-body:shared/combat", animations=7
                ),
            }
        )
        closure = self._closure("characters/npc/body.glb")
        assert len(closure.nodes) == 3
        assert closure.clip_count == 11
        assert {node.identity for node in closure.with_clips()} == {"vtmb:animation-bank:shared/idles",
                          "vtmb:animation-bank:shared/combat"}

    def test_a_bank_with_no_clips_is_marked_a_stub(self) -> None:
        # Ten shared files hold no clips at all; a browser sorted by name misleads
        # anyone who does not know that.
        self._corpus(
            {
                "characters/npc/body.glb": support.character_unit(
                    "vtmb:character-body:npc/body", banks=["vtmb:animation-bank:shared/all"]
                ),
                "characters/shared/all.glb": support.character_unit(
                    "vtmb:character-body:shared/all", banks=["vtmb:animation-bank:shared/real"]
                ),
                "characters/shared/real.glb": support.character_unit(
                    "vtmb:character-body:shared/real", animations=3
                ),
            }
        )
        closure = self._closure("characters/npc/body.glb")
        stubs = {node.identity for node in closure.nodes if node.is_stub}
        assert stubs == {"vtmb:animation-bank:shared/all"}

    def test_a_bank_reached_two_ways_is_visited_once(self) -> None:
        self._corpus(
            {
                "characters/npc/body.glb": support.character_unit(
                    "vtmb:character-body:npc/body",
                    banks=["vtmb:animation-bank:shared/a", "vtmb:animation-bank:shared/b"],
                ),
                "characters/shared/a.glb": support.character_unit(
                    "vtmb:character-body:shared/a", banks=["vtmb:animation-bank:shared/shared"]
                ),
                "characters/shared/b.glb": support.character_unit(
                    "vtmb:character-body:shared/b", banks=["vtmb:animation-bank:shared/shared"]
                ),
                "characters/shared/shared.glb": support.character_unit(
                    "vtmb:character-body:shared/shared", animations=5
                ),
            }
        )
        closure = self._closure("characters/npc/body.glb")
        assert len(closure.nodes) == 3
        assert closure.clip_count == 5

    def test_a_cycle_terminates_instead_of_recursing_forever(self) -> None:
        self._corpus(
            {
                "characters/npc/body.glb": support.character_unit(
                    "vtmb:character-body:npc/body", banks=["vtmb:animation-bank:shared/a"]
                ),
                "characters/shared/a.glb": support.character_unit(
                    "vtmb:character-body:shared/a",
                    banks=["vtmb:animation-bank:shared/b"],
                    animations=1,
                ),
                "characters/shared/b.glb": support.character_unit(
                    "vtmb:character-body:shared/b",
                    banks=["vtmb:animation-bank:shared/a"],
                    animations=2,
                ),
            }
        )
        closure = self._closure("characters/npc/body.glb")
        assert len(closure.nodes) == 2
        assert closure.clip_count == 3

    def test_depth_is_the_shortest_distance_from_the_body(self) -> None:
        self._corpus(
            {
                "characters/npc/body.glb": support.character_unit(
                    "vtmb:character-body:npc/body",
                    banks=["vtmb:animation-bank:shared/near", "vtmb:animation-bank:shared/via"],
                ),
                "characters/shared/via.glb": support.character_unit(
                    "vtmb:character-body:shared/via", banks=["vtmb:animation-bank:shared/near"]
                ),
                "characters/shared/near.glb": support.character_unit(
                    "vtmb:character-body:shared/near", animations=1
                ),
            }
        )
        closure = self._closure("characters/npc/body.glb")
        depths = {node.identity: node.depth for node in closure.nodes}
        assert depths["vtmb:animation-bank:shared/near"] == 1

    def test_a_bank_that_was_never_exported_is_reported_not_skipped(self) -> None:
        # Two bodies in the corpus name a bank with no file behind it.
        self._corpus(
            {
                "characters/npc/body.glb": support.character_unit(
                    "vtmb:character-body:npc/body", banks=["vtmb:animation-bank:npc/gone/gone"]
                )
            }
        )
        closure = self._closure("characters/npc/body.glb")
        assert len(closure.missing) == 1
        assert closure.missing[0].identity == "vtmb:animation-bank:npc/gone/gone"
        assert not closure.missing[0].is_stub, "a missing file is not a stub"

    def test_a_body_with_no_banks_has_an_empty_closure(self) -> None:
        self._corpus(
            {"characters/npc/body.glb": support.character_unit("vtmb:character-body:npc/body")}
        )
        closure = self._closure("characters/npc/body.glb")
        assert closure.nodes == ()
        assert closure.clip_count == 0

    def test_the_walk_stops_at_the_depth_limit(self) -> None:
        units = {
            "characters/npc/body.glb": support.character_unit(
                "vtmb:character-body:npc/body", banks=["vtmb:animation-bank:shared/b0"]
            )
        }
        for step in range(6):
            units["characters/shared/b%d.glb" % step] = support.character_unit(
                "vtmb:character-body:shared/b%d" % step,
                banks=["vtmb:animation-bank:shared/b%d" % (step + 1)],
                animations=1,
            )
        self._corpus(units)
        document = glb.read_json(self.root / "characters/npc/body.glb")
        closure = banks.closure(document, self.root, max_depth=3)
        assert {node.depth for node in closure.nodes} == {1, 2, 3}


class ClipListingTests(unittest.TestCase):
    def setUp(self) -> None:
        self._scratch = tempfile.TemporaryDirectory()
        self.root = Path(self._scratch.name)
        self.addCleanup(self._scratch.cleanup)

    def test_clips_come_back_in_file_order(self) -> None:
        # Names carry the source index, which is what the import filter matches on.
        support.write_corpus(
            self.root,
            {
                "characters/shared/frenzy.glb": support.character_unit(
                    "vtmb:character-body:shared/frenzy", animations=3
                )
            },
        )
        assert banks.clip_names("vtmb:animation-bank:shared/frenzy", self.root) == ["0:clip", "1:clip", "2:clip"]

    def test_a_bank_with_no_clips_lists_none(self) -> None:
        support.write_corpus(
            self.root,
            {"characters/shared/stub.glb": support.character_unit("vtmb:character-body:shared/stub")},
        )
        assert banks.clip_names("vtmb:animation-bank:shared/stub", self.root) == []

    def test_a_bank_with_no_file_lists_none_rather_than_raising(self) -> None:
        assert banks.clip_names("vtmb:animation-bank:shared/gone", self.root) == []


class SkeletonJoinTests(unittest.TestCase):
    def test_bone_names_come_back_in_declared_order(self) -> None:
        # Bank and body declare their own bone tables, so name order is the only join.
        names = ["Bip01", "Bip01 Pelvis", "Bip01 Spine"]
        document = support.document_of(
            support.character_unit("vtmb:character-body:npc/body", bones=names)
        )
        assert banks.bone_names(document) == tuple(names)

    def test_a_document_without_a_skeleton_yields_no_names(self) -> None:
        assert banks.bone_names({}) == ()
        assert banks.bone_remaps({}) == {}
