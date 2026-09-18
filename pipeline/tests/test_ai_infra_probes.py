"""Regression tests for 0018 story 1's census and recovered query surface."""

from __future__ import annotations

import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "research" / "tooling" / "probes"))

import ai_infra_census as census  # noqa: E402
import ai_infra_surface as surface  # noqa: E402


def test_census_keeps_squad_rows_and_entity_graph_nodes():
    roles = census.classify("npc_vhuman", {"squadname": "squad_warehouse"})
    assert roles["squad"]
    assert not roles["maker"]

    roles = census.classify("info_node_werewolf", {})
    assert roles["graph_node"]
    assert not roles["hint"]


def test_census_report_exposes_maker_types_and_squads(tmp_path, monkeypatch):
    maps = tmp_path / "maps"
    nav_graphs = tmp_path / "nav-graphs"
    maps.mkdir()
    nav_graphs.mkdir()
    (maps / "witness.entities.glb").touch()
    (nav_graphs / "witness.glb").touch()

    def row(classname, **values):
        pairs = [{"key": "classname", "value": classname}]
        pairs.extend({"key": key, "value": value} for key, value in values.items())
        return {"classname": classname, "keyValues": pairs}

    monkeypatch.setattr(census, "export_v2_root", lambda: tmp_path)
    monkeypatch.setattr(census, "read_entity_rows", lambda _: [
        row("info_node", nodeid="1"),
        row("npc_maker", NPCType="npc_VHuman", NPCSquadname="alpha"),
        row("npc_VHuman", squadname="alpha"),
    ])
    monkeypatch.setattr(census, "read_nav_block", lambda _: {
        "header": {"numNodes": 1},
        "nodes": [{"index": 0}],
        "links": [],
    })

    report = census.build_report()
    witness = report["maps"][0]
    assert witness["graph_nodes"] == 1
    assert witness["squad_members"] == 1
    assert witness["maker_squad_requests"] == 1
    assert witness["squads"] == 1
    assert report["maker_types"][0]["npc_type"] == "npc_VHuman"
    assert report["squad_map_rows"][0] == {
        "map": "witness",
        "squad": "alpha",
        "placed_members": 1,
        "maker_requests": 1,
    }


def test_surface_manifest_has_unique_answered_queries():
    addresses = [address for _, address, _, _ in surface.QUERY_SPECS]
    assert len(addresses) == len(set(addresses))
    assert all(query.strip() and answer.strip()
               for _, _, query, answer in surface.QUERY_SPECS)
    assert {obj for obj, _, _, _ in surface.QUERY_SPECS} == {
        "hint", "place", "patrol", "squad", "coordinator", "standoff", "sound",
    }


def test_oracle_sections_indexes_only_the_address_column(tmp_path):
    index = tmp_path / "index.md"
    index.write_text(
        "| Address | Function | Sections |\n"
        "|---|---|---|\n"
        "| `0x11111111` | FUN_11111111 | "
        "docs/vtmb/a.md § title mentions `0x22222222` |\n",
        encoding="utf-8",
    )

    sections = surface.oracle_sections(index)
    assert sections[0x11111111] == ["title mentions `0x22222222`"]
    assert 0x22222222 not in sections
