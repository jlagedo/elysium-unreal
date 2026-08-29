"""Contract tests for the animation-bank closure.

The graph the corpus actually contains has stubs that carry no clips, diamonds that
reach the same bank two ways, and at least one body naming a bank that was never
exported. All three appear here.
"""

from __future__ import annotations

import tempfile
from pathlib import Path

from core import banks, glb

from . import support


def _corpus(root: Path, units: dict[str, bytes]) -> None:
    support.write_corpus(root, units)


def _closure(root: Path, body_relative: str) -> banks.Closure:
    document = glb.read_json(root / body_relative)
    return banks.closure(document, root)


def test_walks_through_a_stub_to_the_banks_that_hold_clips(tmp_path: Path) -> None:
    # A body names one bank; the clips live two levels further down.
    _corpus(tmp_path,
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
    closure = _closure(tmp_path, "characters/npc/body.glb")
    assert len(closure.nodes) == 3
    assert closure.clip_count == 11
    assert {node.identity for node in closure.with_clips()} == {"vtmb:animation-bank:shared/idles",
                      "vtmb:animation-bank:shared/combat"}


def test_a_bank_with_no_clips_is_marked_a_stub(tmp_path: Path) -> None:
    # Ten shared files hold no clips at all; a browser sorted by name misleads
    # anyone who does not know that.
    _corpus(tmp_path,
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
    closure = _closure(tmp_path, "characters/npc/body.glb")
    stubs = {node.identity for node in closure.nodes if node.is_stub}
    assert stubs == {"vtmb:animation-bank:shared/all"}


def test_a_bank_reached_two_ways_is_visited_once(tmp_path: Path) -> None:
    _corpus(tmp_path,
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
    closure = _closure(tmp_path, "characters/npc/body.glb")
    assert len(closure.nodes) == 3
    assert closure.clip_count == 5


def test_a_cycle_terminates_instead_of_recursing_forever(tmp_path: Path) -> None:
    _corpus(tmp_path,
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
    closure = _closure(tmp_path, "characters/npc/body.glb")
    assert len(closure.nodes) == 2
    assert closure.clip_count == 3


def test_depth_is_the_shortest_distance_from_the_body(tmp_path: Path) -> None:
    _corpus(tmp_path,
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
    closure = _closure(tmp_path, "characters/npc/body.glb")
    depths = {node.identity: node.depth for node in closure.nodes}
    assert depths["vtmb:animation-bank:shared/near"] == 1


def test_a_bank_that_was_never_exported_is_reported_not_skipped(tmp_path: Path) -> None:
    # Two bodies in the corpus name a bank with no file behind it.
    _corpus(tmp_path,
        {
            "characters/npc/body.glb": support.character_unit(
                "vtmb:character-body:npc/body", banks=["vtmb:animation-bank:npc/gone/gone"]
            )
        }
    )
    closure = _closure(tmp_path, "characters/npc/body.glb")
    assert len(closure.missing) == 1
    assert closure.missing[0].identity == "vtmb:animation-bank:npc/gone/gone"
    assert not closure.missing[0].is_stub, "a missing file is not a stub"


def test_a_body_with_no_banks_has_an_empty_closure(tmp_path: Path) -> None:
    _corpus(tmp_path,
        {"characters/npc/body.glb": support.character_unit("vtmb:character-body:npc/body")}
    )
    closure = _closure(tmp_path, "characters/npc/body.glb")
    assert closure.nodes == ()
    assert closure.clip_count == 0


def test_the_walk_stops_at_the_depth_limit(tmp_path: Path) -> None:
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
    _corpus(tmp_path, units)
    document = glb.read_json(tmp_path / "characters/npc/body.glb")
    closure = banks.closure(document, tmp_path, max_depth=3)
    assert {node.depth for node in closure.nodes} == {1, 2, 3}


def test_clips_come_back_in_file_order(tmp_path: Path) -> None:
    # Names carry the source index, which is what the import filter matches on.
    support.write_corpus(
        tmp_path,
        {
            "characters/shared/frenzy.glb": support.character_unit(
                "vtmb:character-body:shared/frenzy", animations=3
            )
        },
    )
    assert banks.clip_names("vtmb:animation-bank:shared/frenzy", tmp_path) == ["0:clip", "1:clip", "2:clip"]


def test_a_bank_with_no_clips_lists_none(tmp_path: Path) -> None:
    support.write_corpus(
        tmp_path,
        {"characters/shared/stub.glb": support.character_unit("vtmb:character-body:shared/stub")},
    )
    assert banks.clip_names("vtmb:animation-bank:shared/stub", tmp_path) == []


def test_a_bank_with_no_file_lists_none_rather_than_raising(tmp_path: Path) -> None:
    assert banks.clip_names("vtmb:animation-bank:shared/gone", tmp_path) == []


def test_bone_names_come_back_in_declared_order() -> None:
    # Bank and body declare their own bone tables, so name order is the only join.
    names = ["Bip01", "Bip01 Pelvis", "Bip01 Spine"]
    document = support.document_of(
        support.character_unit("vtmb:character-body:npc/body", bones=names)
    )
    assert banks.bone_names(document) == tuple(names)


def test_a_document_without_a_skeleton_yields_no_names() -> None:
    assert banks.bone_names({}) == ()
    assert banks.bone_remaps({}) == {}
