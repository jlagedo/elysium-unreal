"""Contract tests for the include-model closure.

The graph the corpus actually contains has stubs that carry no clips, diamonds that
reach the same included model two ways, and at least one body naming a model that was
never exported. All three appear here.
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
            "models/npc/body.glb": support.model_unit(
                "vtmb:model:npc/body",
                includes=["vtmb:model:shared/all"],
            ),
            "models/shared/all.glb": support.model_unit(
                "vtmb:model:shared/all",
                includes=[
                    "vtmb:model:shared/idles",
                    "vtmb:model:shared/combat",
                ],
            ),
            "models/shared/idles.glb": support.model_unit(
                "vtmb:model:shared/idles", animations=4
            ),
            "models/shared/combat.glb": support.model_unit(
                "vtmb:model:shared/combat", animations=7
            ),
        }
    )
    closure = _closure(tmp_path, "models/npc/body.glb")
    assert len(closure.nodes) == 3
    assert closure.clip_count == 11
    assert {node.identity for node in closure.with_clips()} == {"vtmb:model:shared/idles",
        "vtmb:model:shared/combat"}


def test_a_bank_with_no_clips_is_marked_a_stub(tmp_path: Path) -> None:
    # Ten shared files hold no clips at all; a browser sorted by name misleads
    # anyone who does not know that.
    _corpus(tmp_path,
        {
            "models/npc/body.glb": support.model_unit(
                "vtmb:model:npc/body", includes=["vtmb:model:shared/all"]
            ),
            "models/shared/all.glb": support.model_unit(
                "vtmb:model:shared/all", includes=["vtmb:model:shared/real"]
            ),
            "models/shared/real.glb": support.model_unit(
                "vtmb:model:shared/real", animations=3
            ),
        }
    )
    closure = _closure(tmp_path, "models/npc/body.glb")
    stubs = {node.identity for node in closure.nodes if node.is_stub}
    assert stubs == {"vtmb:model:shared/all"}


def test_a_bank_reached_two_ways_is_visited_once(tmp_path: Path) -> None:
    _corpus(tmp_path,
        {
            "models/npc/body.glb": support.model_unit(
                "vtmb:model:npc/body",
                includes=["vtmb:model:shared/a", "vtmb:model:shared/b"],
            ),
            "models/shared/a.glb": support.model_unit(
                "vtmb:model:shared/a", includes=["vtmb:model:shared/shared"]
            ),
            "models/shared/b.glb": support.model_unit(
                "vtmb:model:shared/b", includes=["vtmb:model:shared/shared"]
            ),
            "models/shared/shared.glb": support.model_unit(
                "vtmb:model:shared/shared", animations=5
            ),
        }
    )
    closure = _closure(tmp_path, "models/npc/body.glb")
    assert len(closure.nodes) == 3
    assert closure.clip_count == 5


def test_a_cycle_terminates_instead_of_recursing_forever(tmp_path: Path) -> None:
    _corpus(tmp_path,
        {
            "models/npc/body.glb": support.model_unit(
                "vtmb:model:npc/body", includes=["vtmb:model:shared/a"]
            ),
            "models/shared/a.glb": support.model_unit(
                "vtmb:model:shared/a",
                includes=["vtmb:model:shared/b"],
                animations=1,
            ),
            "models/shared/b.glb": support.model_unit(
                "vtmb:model:shared/b",
                includes=["vtmb:model:shared/a"],
                animations=2,
            ),
        }
    )
    closure = _closure(tmp_path, "models/npc/body.glb")
    assert len(closure.nodes) == 2
    assert closure.clip_count == 3


def test_depth_is_the_shortest_distance_from_the_body(tmp_path: Path) -> None:
    _corpus(tmp_path,
        {
            "models/npc/body.glb": support.model_unit(
                "vtmb:model:npc/body",
                includes=["vtmb:model:shared/near", "vtmb:model:shared/via"],
            ),
            "models/shared/via.glb": support.model_unit(
                "vtmb:model:shared/via", includes=["vtmb:model:shared/near"]
            ),
            "models/shared/near.glb": support.model_unit(
                "vtmb:model:shared/near", animations=1
            ),
        }
    )
    closure = _closure(tmp_path, "models/npc/body.glb")
    depths = {node.identity: node.depth for node in closure.nodes}
    assert depths["vtmb:model:shared/near"] == 1


def test_a_bank_that_was_never_exported_is_reported_not_skipped(tmp_path: Path) -> None:
    # Two bodies in the corpus name a bank with no file behind it.
    _corpus(tmp_path,
        {
            "models/npc/body.glb": support.model_unit(
                "vtmb:model:npc/body", includes=["vtmb:model:npc/gone/gone"]
            )
        }
    )
    closure = _closure(tmp_path, "models/npc/body.glb")
    assert len(closure.missing) == 1
    assert closure.missing[0].identity == "vtmb:model:npc/gone/gone"
    assert not closure.missing[0].is_stub, "a missing file is not a stub"


def test_a_body_with_no_banks_has_an_empty_closure(tmp_path: Path) -> None:
    _corpus(tmp_path,
        {"models/npc/body.glb": support.model_unit("vtmb:model:npc/body")}
    )
    closure = _closure(tmp_path, "models/npc/body.glb")
    assert closure.nodes == ()
    assert closure.clip_count == 0


def test_the_walk_stops_at_the_depth_limit(tmp_path: Path) -> None:
    units = {
        "models/npc/body.glb": support.model_unit(
            "vtmb:model:npc/body", includes=["vtmb:model:shared/b0"]
        )
    }
    for step in range(6):
        units["models/shared/b%d.glb" % step] = support.model_unit(
            "vtmb:model:shared/b%d" % step,
            includes=["vtmb:model:shared/b%d" % (step + 1)],
            animations=1,
        )
    _corpus(tmp_path, units)
    document = glb.read_json(tmp_path / "models/npc/body.glb")
    closure = banks.closure(document, tmp_path, max_depth=3)
    assert {node.depth for node in closure.nodes} == {1, 2, 3}


def test_clips_come_back_in_file_order(tmp_path: Path) -> None:
    # Names carry the source index, which is what the import filter matches on.
    support.write_corpus(
        tmp_path,
        {
            "models/shared/frenzy.glb": support.model_unit(
                "vtmb:model:shared/frenzy", animations=3
            )
        },
    )
    assert banks.clip_names("vtmb:model:shared/frenzy", tmp_path) == ["0:clip", "1:clip", "2:clip"]


def test_a_bank_with_no_clips_lists_none(tmp_path: Path) -> None:
    support.write_corpus(
        tmp_path,
        {"models/shared/stub.glb": support.model_unit("vtmb:model:shared/stub")},
    )
    assert banks.clip_names("vtmb:model:shared/stub", tmp_path) == []


def test_a_bank_with_no_file_lists_none_rather_than_raising(tmp_path: Path) -> None:
    assert banks.clip_names("vtmb:model:shared/gone", tmp_path) == []


def test_bone_names_come_back_in_declared_order() -> None:
    # Bank and body declare their own bone tables, so name order is the only join.
    names = ["Bip01", "Bip01 Pelvis", "Bip01 Spine"]
    document = support.document_of(
        support.model_unit("vtmb:model:npc/body", bones=names)
    )
    assert banks.bone_names(document) == tuple(names)


def test_a_document_without_a_skeleton_yields_no_names() -> None:
    assert banks.bone_names({}) == ()
    assert banks.bone_remaps({}) == {}
