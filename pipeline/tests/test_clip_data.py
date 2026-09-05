from types import SimpleNamespace

import pytest

from elysium_pipeline.formats import mdl_skel as source
from elysium_pipeline.importers.clip_data import product_metadata


def unit():
    swing = source.SwingRecord(.8, .2, 0, "hand", (1, 2, 3), (2, 3, 4),
                               (("ACT_KNOCKBACK",), (), ("ACT_OTHER",), ()), 3, 255, True, ())
    clip = source.Seq("attack", 1, 31, 30., "ACT_ATTACK", 2, 2,
                      reach=10., low_reach=0., swings=(swing,),
                      combo=source.ComboChain(0, "ACT_DODGE", "next", "alt", .2, .8, .1),
                      events=(source.Event(.5, 1000, 2, "payload with spaces; and punctuation"),
                              source.Event(.5, 1001, 2, "payload with spaces; and punctuation")),
                      movement=(source.Movement(30, 1, 2., 4., 0., (0., 1., 0.), (0., 3., 0.)),),
                      envelopes=(((1., 2., 3.), (4., 5., 6.)),))
    return SimpleNamespace(id="vtmb:model:bank", sequences=[clip], mdl={"sequences":[{"label":"attack", "index":7}]})


def test_contact_events_and_movement_keep_order_domains_and_stated_zero():
    data = product_metadata(unit(), [SimpleNamespace(name="attack",base="",flags=2,frames=31)])["attack"]
    row = data["slice"]["clips"]["attack"][0]
    assert row[7] == 25.4
    assert row[11] == 0.
    assert row[9][0]["degenerate"]
    assert row[9][0]["start"] == .8 and row[9][0]["end"] == .2
    assert row[9][0]["a_cm"] == [2.54, -5.08, 7.62]
    assert row[9][0]["kb_names"] == [["ACT_KNOCKBACK"], [], ["ACT_OTHER"], []]
    assert row[12][0]["min"] == [2.54, 5.08, 7.62]
    assert row[10]["mask"] == 0 and row[10]["w_hold"] == .1
    assert data["slice"]["seq"]["attack"] == [7]
    assert data["timelines"]["events"]["attack"] == [[.5,1000,2,1],[.5,1001,2,1]]
    assert data["timelines"]["event_options"] == ["", "payload with spaces; and punctuation"]
    assert data["cycleSeconds"] == 1.
    assert data["groundDistanceCm"] == pytest.approx(7.62)
    assert data["timelines"]["movement"]["attack"][0][-3:] == [0., -7.62, 0.]
    assert data["counts"] == {"swings":1,"envelopes":1,"events":2,"movement":1,"combo":True,"knockbacks":[[1,0,1,0]]}


def test_derived_forms_keep_the_semantic_owner_and_native_label():
    data = product_metadata(unit(), [SimpleNamespace(name="attack@host",base="host",flags=2,frames=31)], "Bip02")
    record = data["attack@host"]
    assert record["sourceLabel"] == "attack"
    assert record["ownerRoot"] == "Bip02"
    assert "attack@host" in record["timelines"]["events"]


def test_unmapped_native_clip_is_not_given_default_metadata():
    with pytest.raises(ValueError, match="no semantic source"):
        product_metadata(unit(), [SimpleNamespace(name="unexplained",base="",flags=0,frames=31)])
