"""`uv run elysium doctor` reports the corpus index's unclaimed PAKFILE members (SF-1.1).

The line is counted from `members[].embedded[]` directly rather than trusted from a published
`summary`, so it prints against an index built before `summary` carried `embeddedUnclaimed`
too. Nothing here depends on the real `E:\\elysium-work` corpus; every index is a small file
this module writes into `tmp_path`.
"""

from __future__ import annotations

from pathlib import Path

from elysium_pipeline.cli import _corpus_index_sentinel_report, _corpus_index_unclaimed_report
from elysium_pipeline.config import ProjectConfig
from elysium_pipeline.formats.corpus_index_glb import CORPUS_INDEX_EXTENSION
from elysium_pipeline.formats.unit_contract import asset_block, encode_glb


def _config(export_v2_root: Path | None) -> ProjectConfig:
    return ProjectConfig(
        repo_root=Path("."),
        project=Path("ElysiumUE.uproject"),
        game_root=None,
        work_root=None,
        export_root=None,
        export_v2_root=export_v2_root,
        ue_root=None,
        unreal_zen_data_path=None,
        unreal_local_data_cache_path=None,
        unreal_shader_work_root=None,
        temp_root=None,
    )


def _write_index(export_root: Path, members: list[dict]) -> None:
    document = {
        "asset": asset_block("CorpusIndex"),
        "extensionsUsed": [CORPUS_INDEX_EXTENSION],
        "extensionsRequired": [CORPUS_INDEX_EXTENSION],
        "extensions": {CORPUS_INDEX_EXTENSION: {"members": members}},
    }
    export_root.mkdir(parents=True, exist_ok=True)
    (export_root / "index.glb").write_bytes(encode_glb(document, b""))


def _embedded(*rows: tuple[str, str | None]) -> dict:
    return {
        "path": "maps/tutorial.bsp",
        "embedded": [{"member": member, "asset": asset} for member, asset in rows],
    }


def test_no_export_v2_root_reports_nothing() -> None:
    assert _corpus_index_unclaimed_report(_config(None)) is None


def test_no_index_yet_reports_nothing(tmp_path: Path) -> None:
    assert _corpus_index_unclaimed_report(_config(tmp_path)) is None


def test_an_index_with_nothing_unclaimed_reports_nothing(tmp_path: Path) -> None:
    _write_index(tmp_path, [_embedded(("materials/wall.vmt", "vtmb:material:wall"))])
    assert _corpus_index_unclaimed_report(_config(tmp_path)) is None


def test_the_line_counts_and_breaks_down_unclaimed_rows_by_extension(tmp_path: Path) -> None:
    _write_index(
        tmp_path,
        [
            _embedded(
                ("materials/wall.vmt", "vtmb:material:wall"),
                ("materials/a.vmt", None),
                ("materials/b.vmt", None),
                ("materials/probe.tth", None),
            ),
            _embedded(("materials/other.ttz", None)),
        ],
    )
    line = _corpus_index_unclaimed_report(_config(tmp_path))
    assert line == "corpus index: 4 embedded PAKFILE members unclaimed (2 .vmt, 1 .tth, 1 .ttz)"


def test_the_count_is_thousands_separated_like_the_worked_example(tmp_path: Path) -> None:
    vmt = [("materials/vmt%d.vmt" % i, None) for i in range(7501)]
    tth = [("materials/tth%d.tth" % i, None) for i in range(1325)]
    ttz = [("materials/ttz%d.ttz" % i, None) for i in range(750)]
    _write_index(tmp_path, [_embedded(*vmt, *tth, *ttz)])
    line = _corpus_index_unclaimed_report(_config(tmp_path))
    assert line == (
        "corpus index: 9,576 embedded PAKFILE members unclaimed "
        "(7,501 .vmt, 1,325 .tth, 750 .ttz)"
    )


# --- sentinel material references (SF-1.2 / props seam validation) -----------------------------
#
# A `vtmb:missing-<kind>:` sentinel carries no dependency row by contract (`references.py`), so
# it is invisible to `references[]` and every check built over the graph; `summary.sentinelReferences`
# and `summary.sentinelReferenceUnits` are what the doctor line reads. Reading straight from
# `summary` -- unlike the embedded-PAKFILE line above -- is deliberate: no table on the index
# carries the fact any other way to recompute it from, so an index built before these two summary
# keys existed simply reports nothing, same as one with no sentinel at all.


def _write_summary_index(export_root: Path, summary: dict) -> None:
    document = {
        "asset": asset_block("CorpusIndex"),
        "extensionsUsed": [CORPUS_INDEX_EXTENSION],
        "extensionsRequired": [CORPUS_INDEX_EXTENSION],
        "extensions": {CORPUS_INDEX_EXTENSION: {"summary": summary}},
    }
    export_root.mkdir(parents=True, exist_ok=True)
    (export_root / "index.glb").write_bytes(encode_glb(document, b""))


def test_an_index_with_no_sentinel_references_reports_nothing(tmp_path: Path) -> None:
    _write_summary_index(tmp_path, {})
    assert _corpus_index_sentinel_report(_config(tmp_path)) is None


def test_the_sentinel_line_counts_references_and_units(tmp_path: Path) -> None:
    _write_summary_index(
        tmp_path, {"sentinelReferences": 1523, "sentinelReferenceUnits": 597}
    )
    line = _corpus_index_sentinel_report(_config(tmp_path))
    assert line == "corpus index: 1,523 vtmb:missing-* sentinel reference(s) across 597 unit(s)"
