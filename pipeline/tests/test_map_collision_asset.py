"""The per-map collision payload's offline stage (R4.2).

`pipeline/src/elysium_pipeline/importers/map_collision.py` turns `<map>.hulls`, `<map>.dispcol` and
the brush-entity `hulls` of `<map>.ents` into the manifest `pipeline/unreal/import_map_collision.py`
executes.

These pin the pieces that decide whether a wrong payload can be authored at all: the sidecar
acceptance rule the C++ reader states, the 3D-skybox scale that is the one transform this lane
applies, the brush ordinal the runtime looks bodies up by, the refusal when the world collider is
missing, and the asset path that must stay the twin of `FElysiumContentPaths::BakedMapCollision`.
"""
from __future__ import annotations

import json
from pathlib import Path

import pytest

from elysium_pipeline.formats import contents_signature
from elysium_pipeline.importers import map_collision


def _write(path: Path, text: str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path


def _cube(scale: float = 1.0) -> list[float]:
    """Eight corners of a unit cube as one flat row -- the shape a `.hulls` line carries."""
    return [c * scale for corner in (
        (0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0),
        (0, 0, 1), (1, 0, 1), (1, 1, 1), (0, 1, 1),
    ) for c in corner]


#: SOLID: the contents word a `.hulls` row leads with for an ordinary player-solid brush.
SOLID = 0x1


def _hull_line(row: list[float], contents: int = SOLID) -> str:
    """One `.hulls` row: its contents word, then the flat vertex cloud."""

    return f"0x{contents:08x} " + " ".join(f"{c:.4f}" for c in row)


def _write_map(directory: Path, name: str, *, entities, hull_rows=None, disp_rows=None,
               sky_scale: float | None = None) -> Path:
    directory = directory / name
    directory.mkdir(parents=True, exist_ok=True)
    rows = hull_rows if hull_rows is not None else [_cube()]
    _write(directory / f"{name}.hulls",
           "\n".join(_hull_line(row) for row in rows) + "\n")
    if disp_rows:
        _write(directory / f"{name}.dispcol",
               "\n".join(" ".join(f"{c:.4f}" for c in row) for row in disp_rows) + "\n")
    if sky_scale is not None:
        _write(directory / f"{name}.sky", f"origin 0.0 0.0 0.0\nscale {sky_scale}\n")
    with (directory / f"{name}.ents").open("w", encoding="ascii") as handle:
        json.dump({"map": name, "entities": entities}, handle, separators=(",", ":"))
    return directory


def test_read_hull_rows_applies_the_runtime_readers_acceptance_rule(tmp_path):
    path = _write(tmp_path / "probe.hulls", "\n".join([
        _hull_line(_cube()),                              # eight verts: kept
        f"0x{SOLID:08x} 1 2 3 4 5 6 7 8 9",               # three verts: too few for a convex
        f"0x{SOLID:08x} 1 2 3 4 5 6 7 8 9 10 11",         # not whole triples
        " ".join(f"{c:.4f}" for c in _cube()),            # no contents word: the old format
        _hull_line(_cube(2.0)),                           # kept
    ]) + "\n")

    rows = map_collision.read_hull_rows(path)

    assert [len(row) // 3 for row in rows] == [8, 8]
    assert rows[1][3] == pytest.approx(2.0)


def test_the_world_partitions_into_one_body_per_contents_signature(tmp_path):
    """A brush answers four retail masks and the answers differ, so the world is not one body.

    A single set could not say that an NPC clip stops an NPC and not the player, or that an
    unsolid OPAQUE brush stops sight and neither pawn. Groups keep file order and appear in order
    of first appearance, so the payload's bodies land the same way on every run.
    """

    npc_clip, sight_only, pedestrian = 0x08020000, 0x08000080, 0x08002000
    path = _write(tmp_path / "probe.hulls", "\n".join([
        _hull_line(_cube(), SOLID),
        _hull_line(_cube(2.0), npc_clip),
        _hull_line(_cube(3.0), sight_only),
        _hull_line(_cube(4.0), pedestrian),
        _hull_line(_cube(5.0), SOLID),        # joins the first group, after it
        _hull_line(_cube(6.0), 0x0),          # answers no mask: never staged
    ]) + "\n")

    partitions = map_collision.read_hull_partitions(path)

    assert [contents_signature.spell(sig) for sig, _ in partitions] == [
        "PNS-", "-N--", "--S-", "---p"]
    solid = partitions[0][1]
    assert [hull[3] for hull in solid] == [pytest.approx(1.0), pytest.approx(5.0)]
    # The flat list the parity check uses is every staged hull, in the same order.
    assert len(map_collision.read_hull_rows(path)) == 5


def test_brush_entity_rows_key_by_lump_ordinal_and_skip_point_entities(tmp_path):
    directory = _write_map(tmp_path, "sp_probe", entities=[
        {"classname": "worldspawn"},
        {"classname": "func_door", "hulls": [_cube()], "hull_contents": [SOLID]},
        {"classname": "info_player_start"},
        {"classname": "trigger_once", "hulls": [_cube(), _cube(3.0)], "hull_contents": [SOLID, SOLID]},
    ])

    rows = map_collision.brush_entity_rows(directory / "sp_probe.ents", 1.0)

    assert [row["entityIndex"] for row in rows] == [1, 3]
    assert [len(row["hulls"]) for row in rows] == [1, 2]


def test_a_sky_brush_entity_is_not_composed_into_the_collision_at_all(tmp_path):
    """G25. The stage used to multiply a miniature's hulls by the map's sky scale, which on
    `sm_pier_1` walked three sky-flagged `func_brush` Solids from raw z ~= 4939 -- above the map's
    own `world_maxs.z 512`, where nothing in VtMB reaches them -- down to world z -644..-628, three
    invisible collision slabs 21 inches under the harbour surface. The 3D skybox is drawn from its
    own camera and no body ever travels into it, so a miniature contributes no collider and the
    scale has nothing to apply to. The row is skipped whole, by ordinal, so a real brush entity's
    handle still finds its own body."""

    directory = _write_map(tmp_path, "sp_probe", sky_scale=16.0, entities=[
        {"classname": "func_brush", "hulls": [_cube()], "hull_contents": [SOLID], "sky": True},
        {"classname": "func_brush", "hulls": [_cube()], "hull_contents": [SOLID]},
    ])

    rows = map_collision.brush_entity_rows(
        directory / "sp_probe.ents",
        map_collision.read_sky_scale(directory / "sp_probe.sky"),
    )

    assert [row["entityIndex"] for row in rows] == [1]
    assert rows[0]["hulls"][0] == pytest.approx(_cube())


def test_stage_map_counts_the_miniatures_it_kept_out_of_the_collision(tmp_path):
    """G25's own number, so a map that silently loses a real collider is separable from one whose
    miniatures were excluded on purpose."""

    directory = _write_map(tmp_path, "sp_probe", sky_scale=16.0, hull_rows=[_cube()], entities=[
        {"classname": "func_brush", "hulls": [_cube()], "hull_contents": [SOLID], "sky": True},
        {"classname": "func_brush", "hulls": [_cube(2.0)], "hull_contents": [SOLID], "sky": True},
        {"classname": "func_door", "hulls": [_cube(3.0)], "hull_contents": [SOLID]},
    ])

    entry = map_collision.stage_map(
        "sp_probe",
        hulls_path=directory / "sp_probe.hulls",
        dispcol_path=directory / "sp_probe.dispcol",
        ents_path=directory / "sp_probe.ents",
        sky_path=directory / "sp_probe.sky",
    )

    assert entry["parity"]["equal"] is True
    assert entry["stats"]["brushBodies"] == 1
    assert entry["stats"]["skyBrushBodiesExcluded"] == 2
    assert [row["entityIndex"] for row in entry["brushBodies"]] == [2]


def test_stage_map_carries_the_sidecars_and_reports_its_own_numbers(tmp_path):
    disp = [[float(v) for v in range(9)], [float(v) for v in range(9, 18)]]
    directory = _write_map(tmp_path, "sp_probe", sky_scale=16.0,
                           hull_rows=[_cube(), _cube(2.0), _cube(3.0)], disp_rows=disp,
                           entities=[{"classname": "func_door", "hulls": [_cube()], "hull_contents": [SOLID]}])

    entry = map_collision.stage_map(
        "sp_probe",
        hulls_path=directory / "sp_probe.hulls",
        dispcol_path=directory / "sp_probe.dispcol",
        ents_path=directory / "sp_probe.ents",
        sky_path=directory / "sp_probe.sky",
    )

    assert entry["assetPath"] == "/ElysiumBaked/sp_probe/DA_sp_probe_Collision"
    assert entry["parity"]["equal"] is True
    assert entry["stats"] == {
        "worldHulls": 3, "worldHullVertices": 24, "displacementTriangles": 2,
        "brushBodies": 1, "brushHulls": 1, "skyBrushBodies": 0,
        "skyBrushBodiesExcluded": 0,
    }
    # Three vertices per triangle, un-welded and in file order -- the soup LoadDispCol builds.
    assert entry["displacementIndices"] == [0, 1, 2, 3, 4, 5]
    assert entry["displacementVertices"][:9] == pytest.approx(disp[0])


def test_stage_map_refuses_a_map_with_no_world_collider(tmp_path):
    directory = _write_map(tmp_path, "sp_probe", hull_rows=[[1.0, 2.0, 3.0]],
                           entities=[{"classname": "worldspawn"}])

    with pytest.raises(map_collision.MapCollisionStageError, match="no world hulls"):
        map_collision.stage_map(
            "sp_probe",
            hulls_path=directory / "sp_probe.hulls",
            dispcol_path=directory / "sp_probe.dispcol",
            ents_path=directory / "sp_probe.ents",
            sky_path=directory / "sp_probe.sky",
        )


def test_stage_map_collision_refuses_to_run_unscoped(tmp_path):
    with pytest.raises(map_collision.MapCollisionStageError, match="refuses to run unscoped"):
        map_collision.stage_map_collision(tmp_path, maps=[], sidecar_dir=lambda stem: tmp_path)
