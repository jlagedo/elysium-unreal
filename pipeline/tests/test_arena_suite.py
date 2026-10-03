"""`uv run elysium arena`'s launcher: record discovery, grouping, the command line, the merge and
the verdict, from canned host `index.json` files. No editor is launched."""

from __future__ import annotations

import json
from pathlib import Path
from types import SimpleNamespace

import pytest
from typer.testing import CliRunner

from elysium_pipeline import arena_suite
from elysium_pipeline.arena_suite import ArenaError, Boot, ScenarioRecord


def _write_record(root: Path, relative: str, **fields) -> Path:
    path = root / "Arena" / "scenarios" / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(fields), encoding="utf-8")
    return path


def _record(name: str, stage: str = "arena", *, shares_map: bool = False,
            expect_fail: bool = False) -> ScenarioRecord:
    return ScenarioRecord(name=name, stage=stage, expect_fail=expect_fail, shares_map=shares_map,
                          path=Path(f"{name}.json"))


def _config(tmp_path: Path) -> SimpleNamespace:
    return SimpleNamespace(project=tmp_path / "ElysiumUE.uproject", repo_root=tmp_path,
                           work_root=tmp_path / "work")


def _host_index(host: str, *rows: dict) -> dict:
    return {"host": host, "hz": 60, "scenarios": list(rows)}


def _row(name: str, result: str, **extra) -> dict:
    row = {"name": name, "result": result, "known_red": "", "error": "", "first_unmet": None,
           "game_seconds": 1.0, "wall_seconds": 0.5, "events": 3, "trace": f"{name}.trace.tsv"}
    row.update(extra)
    return row


# --- discovery --------------------------------------------------------------------------------


def test_discovery_reads_every_record_in_relative_path_order(tmp_path: Path) -> None:
    _write_record(tmp_path, "cover.json", name="cover", stage="arena", known_red="1: arbiter")
    _write_record(tmp_path, "_selftest/must_fail.json", name="must_fail", stage="arena",
                  expect_fail=True)
    _write_record(tmp_path, "maps/door.json", name="door", stage="map:sp_tutorial_1",
                  shares_map=True)
    records = arena_suite.discover_records(tmp_path)
    assert [record.name for record in records] == ["must_fail", "cover", "door"]
    assert records[0].expect_fail is True
    assert records[2].map_name == "sp_tutorial_1" and records[2].shares_map is True
    assert records[1].expect_fail is False and records[1].shares_map is False


def test_discovery_refuses_a_duplicate_name(tmp_path: Path) -> None:
    _write_record(tmp_path, "a.json", name="cover", stage="arena")
    _write_record(tmp_path, "b/a.json", name="cover", stage="arena")
    with pytest.raises(ArenaError, match="'cover' is used by both"):
        arena_suite.discover_records(tmp_path)


@pytest.mark.parametrize("fields, field", [
    ({"stage": "arena"}, "`name`"),
    ({"name": "x", "stage": "lab"}, "`stage`"),
    ({"name": "x", "stage": "map:"}, "`stage`"),
    ({"name": "x", "stage": "arena", "expect_fail": "yes"}, "`expect_fail`"),
])
def test_discovery_names_the_file_and_the_field_of_a_bad_record(tmp_path: Path, fields: dict,
                                                                field: str) -> None:
    _write_record(tmp_path, "bad.json", **fields)
    with pytest.raises(ArenaError, match="bad.json") as raised:
        arena_suite.discover_records(tmp_path)
    assert field in str(raised.value)


def test_discovery_without_records_is_a_usage_error(tmp_path: Path) -> None:
    with pytest.raises(ArenaError) as raised:
        arena_suite.discover_records(tmp_path)
    assert raised.value.exit_code == 2


# --- selection and grouping -------------------------------------------------------------------


def test_selection_keeps_discovery_order_and_refuses_an_unknown_name() -> None:
    records = [_record("a"), _record("b"), _record("c")]
    assert [r.name for r in arena_suite.select_records(records, [])] == ["a", "b", "c"]
    assert [r.name for r in arena_suite.select_records(records, ["c", "a"])] == ["a", "c"]
    with pytest.raises(ArenaError, match="'nope'"):
        arena_suite.select_records(records, ["a", "nope"])


def test_grouping_boots_the_arena_first_then_one_boot_per_map_record_unless_shared() -> None:
    records = [
        _record("door", "map:sp_tutorial_1"),
        _record("cover"),
        _record("walk", "map:sp_tutorial_1", shares_map=True),
        _record("talk", "map:sp_tutorial_1", shares_map=True),
        _record("control"),
        _record("hub", "map:sm_hub_1"),
    ]
    boots = arena_suite.plan_boots(records)
    assert [(boot.stage, boot.names, boot.slug) for boot in boots] == [
        ("arena", ["cover", "control"], "arena"),
        ("map:sp_tutorial_1", ["walk", "talk"], "map-sp_tutorial_1"),
        ("map:sp_tutorial_1", ["door"], "map-sp_tutorial_1-door"),
        ("map:sm_hub_1", ["hub"], "map-sm_hub_1-hub"),
    ]


# --- the command line -------------------------------------------------------------------------


def test_the_arena_boot_passes_the_launch_contract(tmp_path: Path) -> None:
    boot = Boot(stage="arena", records=(_record("cover"), _record("control")), slug="arena")
    argv = arena_suite.boot_arguments(["Elysium.uproject", "-game"], boot, tmp_path / "arena", 30)
    assert argv[:2] == ["Elysium.uproject", "-game"]
    for switch in ("-ElysiumArena", "-ArenaScenarios=cover,control", f"-ArenaOut={tmp_path / 'arena'}",
                   "-ArenaHz=30", "-FPS=30", "-UseFixedTimeStep", "-nullrhi", "-unattended",
                   "-nosplash", "-nosound", "-stdout", "-FullStdOutLogOutput"):
        assert switch in argv
    assert not any(arg.startswith("-ElysiumMap") for arg in argv)
    # An empty `-Switch=` makes Unreal's parser swallow the next token.
    assert not any(arg.endswith("=") for arg in argv)


def test_a_map_boot_names_its_map(tmp_path: Path) -> None:
    boot = Boot(stage="map:sp_tutorial_1", records=(_record("door", "map:sp_tutorial_1"),),
                slug="map-sp_tutorial_1-door")
    argv = arena_suite.boot_arguments(["p", "-game"], boot, tmp_path, 60)
    assert "-ElysiumMap=sp_tutorial_1" in argv
    assert "-ArenaScenarios=door" in argv


# --- the merge and the verdict ----------------------------------------------------------------


def test_a_green_run_with_an_expected_fail_passes(tmp_path: Path) -> None:
    boot = Boot(stage="arena", records=(_record("control"), _record("cover")), slug="arena")
    index = _host_index("arena", _row("control", "pass"),
                        _row("cover", "expected-fail", known_red="1: the body arbiter"))
    merged = arena_suite.merge_reports(tmp_path, [(boot, index, "exit 1")], 60)
    assert arena_suite.verdict(merged) == 0
    assert merged["counts"]["pass"] == 1 and merged["counts"]["expected-fail"] == 1
    assert merged["scenarios"][0]["trace"] == str(tmp_path / "arena" / "control.trace.tsv")
    assert merged["scenarios"][1]["host"] == "arena"


@pytest.mark.parametrize("result", ["fail", "unexpected-pass", "error"])
def test_a_failing_result_fails_the_suite_with_the_test_verdict_code(tmp_path: Path,
                                                                    result: str) -> None:
    boot = Boot(stage="arena", records=(_record("cover"),), slug="arena")
    merged = arena_suite.merge_reports(
        tmp_path, [(boot, _host_index("arena", _row("cover", result)), "")], 60)
    assert arena_suite.verdict(merged) == 7
    assert merged["failed"] == 1


def test_a_record_the_host_did_not_report_is_an_error(tmp_path: Path) -> None:
    boot = Boot(stage="arena", records=(_record("cover"), _record("control")), slug="arena")
    merged = arena_suite.merge_reports(
        tmp_path, [(boot, _host_index("arena", _row("cover", "pass")), "")], 60)
    missing = [row for row in merged["scenarios"] if row["name"] == "control"]
    assert missing and missing[0]["result"] == "error"
    assert "did not report it" in missing[0]["error"]
    assert arena_suite.verdict(merged) == 7


def test_a_host_that_wrote_no_index_errors_every_record_it_was_given(tmp_path: Path) -> None:
    arena = Boot(stage="arena", records=(_record("cover"),), slug="arena")
    door = Boot(stage="map:sp_tutorial_1", records=(_record("door", "map:sp_tutorial_1"),),
                slug="map-sp_tutorial_1-door")
    merged = arena_suite.merge_reports(
        tmp_path,
        [(arena, _host_index("arena", _row("cover", "pass")), ""),
         (door, None, "UnrealEditor.exe exited with 3")],
        60)
    door_row = merged["scenarios"][-1]
    assert door_row["name"] == "door" and door_row["result"] == "error"
    assert "wrote no index.json" in door_row["error"] and "exited with 3" in door_row["error"]
    assert merged["hosts"][1]["index"] is False
    assert arena_suite.verdict(merged) == 7


def test_the_summary_names_the_first_unmet_expectation_of_each_failure(tmp_path: Path) -> None:
    boot = Boot(stage="arena", records=(_record("cover"), _record("control")), slug="arena")
    unmet = {"index": 2, "expect": {"who": "arena_gunman", "kind": "seqfinished", "match": ""},
             "deadline": 14.0}
    merged = arena_suite.merge_reports(
        tmp_path,
        [(boot, _host_index("arena", _row("cover", "fail", first_unmet=unmet),
                            _row("control", "pass")), "exit 1")],
        60)
    lines = arena_suite.summary_lines(merged)
    assert lines[0].startswith("arena: 2 scenario(s) in 1 boot(s): 1 pass, 1 fail")
    cover = next(i for i, line in enumerate(lines) if " cover " in f"{line} ")
    assert lines[cover].lstrip().startswith("fail")
    assert lines[cover + 1] == '    first unmet: #2 arena_gunman seqfinished "" by 14.0s'
    assert any(line.startswith("  report: ") for line in lines)


# --- one run, end to end, with a stand-in for the editor --------------------------------------


def test_a_run_boots_each_host_and_writes_the_merged_index(tmp_path: Path) -> None:
    _write_record(tmp_path, "cover.json", name="cover", stage="arena")
    _write_record(tmp_path, "_selftest/must_fail.json", name="must_fail", stage="arena",
                  expect_fail=True)
    _write_record(tmp_path, "door.json", name="door", stage="map:sp_tutorial_1")
    launches: list[list[str]] = []

    def launch(argv) -> str:
        argv = list(argv)
        launches.append(argv)
        out = Path(next(arg for arg in argv if arg.startswith("-ArenaOut=")).split("=", 1)[1])
        names = next(arg for arg in argv if arg.startswith("-ArenaScenarios=")).split("=", 1)[1]
        rows = [_row(name, "pass") for name in names.split(",")]
        host = "arena" if not any(arg.startswith("-ElysiumMap=") for arg in argv) else "map"
        (out / "index.json").write_text(json.dumps(_host_index(host, *rows)), encoding="utf-8")
        return ""

    merged = arena_suite.run_arena(_config(tmp_path), runner=None, launch=launch,
                                   stamp="20261003T000000.000000Z")
    assert len(launches) == 2
    assert "-ArenaScenarios=must_fail,cover" in launches[0]
    assert "-ElysiumMap=sp_tutorial_1" in launches[1]
    written = tmp_path / "work" / "reports" / "arena" / "20261003T000000.000000Z" / "index.json"
    assert json.loads(written.read_text(encoding="utf-8"))["counts"]["pass"] == 3
    assert arena_suite.verdict(merged) == 0
    assert arena_suite.payload(merged)["arena_report"] == str(written)


def test_an_unknown_name_is_refused_before_anything_boots(tmp_path: Path) -> None:
    _write_record(tmp_path, "cover.json", name="cover", stage="arena")

    def launch(argv) -> str:
        raise AssertionError("nothing may boot")

    with pytest.raises(ArenaError, match="'covr'"):
        arena_suite.run_arena(_config(tmp_path), runner=None, names=["covr"], launch=launch)
    assert not (tmp_path / "work" / "reports" / "arena").exists()


def test_the_cli_registers_the_arena_command() -> None:
    from elysium_pipeline.cli import app

    result = CliRunner().invoke(app, ["arena", "--help"])
    assert result.exit_code == 0, result.output
    assert "--hz" in result.output
