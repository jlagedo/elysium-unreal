"""The widened `.hulls` sidecar: every brush that answers a retail mask, led by its contents word.

The legacy sidecar kept only the player-solid brushes and wrote bare coordinates, so the NPC-only
clips, the sight-only brushes and the pedestrian volumes never left the BSP -- on `sp_tutorial_1`,
5 brushes an NPC cannot pass and 17 that stop its sight. The format change is a declared
divergence from a byte-compared file, which is why `classify_hulls` proves it by projection
rather than by assertion, and why that projection is tested here in both directions.
"""

from __future__ import annotations

import os
from pathlib import Path

import pytest

from elysium_pipeline.exporters.UE_map_sidecars import BLOCK_MASK
from elysium_pipeline.formats import contents_signature
from elysium_pipeline.validation import map_sidecar_diff


def _cube(scale: float = 1.0) -> list[float]:
    return [c * scale for corner in (
        (0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0),
        (0, 0, 1), (1, 0, 1), (1, 1, 1), (0, 1, 1),
    ) for c in corner]


def _row(contents: int, scale: float = 1.0) -> str:
    return f"0x{contents:08x} " + " ".join(f"{c:.4f}" for c in _cube(scale))


def _legacy_row(scale: float = 1.0) -> str:
    return " ".join(f"{c:.4f}" for c in _cube(scale))


SOLID, NPC_CLIP, SIGHT_ONLY, PEDESTRIAN, WINDOW = 0x1, 0x08020000, 0x08000080, 0x08002000, 0x2


def _write(path: Path, rows: list[str]) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(rows) + "\n", encoding="ascii")
    return path


# ---------------------------------------------------------------- the projection both ways

def test_the_projection_recovers_the_legacy_bytes_exactly(tmp_path):
    """Drop the contents column, keep the BLOCK_MASK rows, and it is the old file again."""

    legacy = _write(tmp_path / "legacy" / "m.hulls", [_legacy_row(1.0), _legacy_row(4.0)])
    producer = _write(tmp_path / "producer" / "m.hulls", [
        _row(SOLID, 1.0),
        _row(NPC_CLIP, 2.0),        # never in the legacy file
        _row(SIGHT_ONLY, 3.0),      # never in the legacy file
        _row(WINDOW, 4.0),
        _row(PEDESTRIAN, 5.0),      # never in the legacy file
    ])

    diff = map_sidecar_diff.file_diff(legacy, producer)
    assert not diff["equal"]
    assert map_sidecar_diff.classify_hulls(diff, legacy, producer) == "named_divergence"


def test_a_moved_coordinate_survives_the_projection_and_is_still_a_defect(tmp_path):
    legacy = _write(tmp_path / "legacy" / "m.hulls", [_legacy_row(1.0)])
    producer = _write(tmp_path / "producer" / "m.hulls", [_row(SOLID, 1.5)])

    diff = map_sidecar_diff.file_diff(legacy, producer)
    assert map_sidecar_diff.classify_hulls(diff, legacy, producer) == "unexpected"


def test_a_dropped_player_solid_row_is_still_a_defect(tmp_path):
    legacy = _write(tmp_path / "legacy" / "m.hulls", [_legacy_row(1.0), _legacy_row(2.0)])
    producer = _write(tmp_path / "producer" / "m.hulls", [_row(SOLID, 1.0)])

    diff = map_sidecar_diff.file_diff(legacy, producer)
    assert map_sidecar_diff.classify_hulls(diff, legacy, producer) == "unexpected"


def test_a_reordered_row_is_still_a_defect(tmp_path):
    legacy = _write(tmp_path / "legacy" / "m.hulls", [_legacy_row(1.0), _legacy_row(2.0)])
    producer = _write(tmp_path / "producer" / "m.hulls", [_row(SOLID, 2.0), _row(SOLID, 1.0)])

    diff = map_sidecar_diff.file_diff(legacy, producer)
    assert map_sidecar_diff.classify_hulls(diff, legacy, producer) == "unexpected"


def test_a_file_still_in_the_old_format_is_not_a_named_divergence(tmp_path):
    """It would be a producer that never gained the column, not one that gained it correctly."""

    legacy = _write(tmp_path / "legacy" / "m.hulls", [_legacy_row(1.0)])
    producer = _write(tmp_path / "producer" / "m.hulls", [_legacy_row(2.0)])

    diff = map_sidecar_diff.file_diff(legacy, producer)
    assert map_sidecar_diff.classify_hulls(diff, legacy, producer) == "unexpected"


def test_an_unchanged_file_is_byte_equal(tmp_path):
    legacy = _write(tmp_path / "legacy" / "m.hulls", [_row(SOLID)])
    producer = _write(tmp_path / "producer" / "m.hulls", [_row(SOLID)])

    diff = map_sidecar_diff.file_diff(legacy, producer)
    assert map_sidecar_diff.classify_hulls(diff, legacy, producer) == "byte_equal"


# ---------------------------------------------------------------- the row format itself

def test_the_row_shape_makes_an_old_reader_fail_rather_than_misread():
    """A contents word read as a coordinate would be a silent, enormous vertex.

    The legacy format is a multiple of three tokens; the new one is 1 mod 3. No row can be
    mistaken for the other format, so a reader built for either refuses the other outright.
    """

    for contents in (SOLID, NPC_CLIP, SIGHT_ONLY, PEDESTRIAN):
        tokens = _row(contents).split()
        assert len(tokens) % 3 == 1
        assert tokens[0].startswith("0x")
        assert len(_legacy_row().split()) % 3 == 0


def test_the_five_signature_classes_a_witness_map_can_carry_are_all_expressible():
    for contents, expected in (
        (SOLID, "PNS-"),
        (WINDOW | 0x10000000, "PN--"),
        (NPC_CLIP, "-N--"),
        (SIGHT_ONLY, "--S-"),
        (PEDESTRIAN, "---p"),
    ):
        assert contents_signature.signature_str(contents) == expected


def test_only_the_player_solid_rows_would_have_reached_the_legacy_file():
    """The mask the projection uses is the exporter's own, not a copy of it."""

    assert BLOCK_MASK == 0x1 | 0x2 | 0x8 | 0x4000 | 0x10000
    assert SOLID & BLOCK_MASK and WINDOW & BLOCK_MASK
    for contents in (NPC_CLIP, SIGHT_ONLY, PEDESTRIAN):
        assert not contents & BLOCK_MASK


# ---------------------------------------------------------------- against a real export

def _sidecar(map_name: str) -> Path | None:
    root = os.environ.get("ELYSIUM_EXPORT_ROOT") or os.environ.get("ELYSIUM_WORK_ROOT")
    if not root:
        return None
    path = Path(root) / ("exports" if "EXPORT" not in str(root).upper() else "") / map_name / \
        f"{map_name}.hulls"
    return path if path.is_file() else None


@pytest.mark.parametrize("map_name", ["sp_tutorial_1", "sm_hub_1"])
def test_an_exported_sidecar_carries_the_signatures_the_spec_pins(map_name):
    path = _sidecar(map_name)
    if path is None:
        pytest.skip(f"{map_name} has not been exported on this machine")

    tally: dict[str, int] = {}
    for line in path.read_text(encoding="ascii").splitlines():
        tokens = line.split()
        if not tokens:
            continue
        assert tokens[0].startswith("0x"), "a row without its contents word"
        assert len(tokens) % 3 == 1, "a row whose vertices are not whole triples"
        mark = contents_signature.signature_str(int(tokens[0], 16))
        tally[mark] = tally.get(mark, 0) + 1

    expected = {
        "sp_tutorial_1": {"PNS-": 2725, "PN--": 309, "--S-": 17, "-N--": 5},
        "sm_hub_1": {"PNS-": 4020, "PN--": 151, "PN-p": 93, "-N-p": 31, "---p": 9},
    }[map_name]
    # The sky miniature's brushes are dropped, so a count may fall short of the census; none may
    # exceed it, and no signature may appear that the census never saw.
    assert set(tally) <= set(expected), f"unexpected signature: {set(tally) - set(expected)}"
    for mark, count in tally.items():
        assert count <= expected[mark], f"{mark}: {count} rows against a census of {expected[mark]}"
