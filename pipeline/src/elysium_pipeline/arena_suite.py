"""`uv run elysium arena` -- the Green Room arena scenarios as one suite (spec 0002 step 1, T5).

A scenario is a tracked JSON record under `<project>/Arena/scenarios/**/*.json` (its schema is
`Arena/README.md`). This module reads only five of its words -- `name`, `stage`, `expect_fail`,
`shares_map` and `seed` -- groups the requested records by stage and boots the headless arena run
once per group, arena first. A map boot carries its first record's `seed` as `-ArenaSeed`, which
the host's New Game seeds from in place of the clock, so the map's activation replays (H17).
The launch contract (the switches, the per-host `index.json`, the exit code) is `docs/specs/0002-npc-ai/stories/wave2/seam.md` § "The launch contract".

One run writes `$ELYSIUM_WORK_ROOT/reports/arena/<timestamp>/`: a directory per boot holding that
host's own `index.json` and trace files, and one merged `index.json` beside them. The verdict is
the test command's: 7 when any scenario's result is `fail`, `unexpected-pass` or `error`.
"""

from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import shutil
from typing import Any, Callable, Iterable, Sequence

from elysium_pipeline.process import ProcessFailure
from elysium_pipeline.reporting import ExitCode

#: Where the records live, relative to the project directory.
SCENARIO_DIR = Path("Arena") / "scenarios"
#: The arena host's stage word; a map host's is `map:<map>`.
ARENA_STAGE = "arena"
MAP_STAGE_PREFIX = "map:"
#: A record's `seed` range: the host stores it as an `int32` (`ElysiumArenaScenario.cpp`), and the
#: schema (`Arena/README.md`) says "whole number >= 0". Absent = 0, the host's default.
SEED_MAX = 2**31 - 1
#: The fixed step the run is driven at, unless `--hz` says otherwise (`-ArenaHz` and `-FPS`).
DEFAULT_HZ = 60
#: One boot's bounds. The arena host's whole suite is budgeted at 30 s wall; these only catch a
#: host that hung, so they are generous.
BOOT_TIMEOUT_SECONDS = 900.0
BOOT_IDLE_TIMEOUT_SECONDS = 300.0
#: How many runs `reports/arena/` keeps.
REPORT_RETENTION = 50
#: The results that fail the suite (`seam.md`): a plain failure, a known red that went green (the
#: record's `known_red` is stale), and a scenario the harness could not run.
FAILING_RESULTS = frozenset({"fail", "unexpected-pass", "error"})
RESULT_ORDER = ("pass", "fail", "expected-fail", "unexpected-pass", "error")

#: A boot's launcher: the editor argv in, "" for a clean exit or a sentence describing how the
#: process ended otherwise. A non-zero exit is not by itself an error -- the host exits 1 when a
#: scenario failed -- so the host's `index.json` decides.
Launcher = Callable[[Sequence[str]], str]


class ArenaError(RuntimeError):
    """A request the suite cannot run: no records, a malformed record, a name that matches none."""

    exit_code = int(ExitCode.USAGE_OR_CONFIG)


class ArenaFailure(RuntimeError):
    """The suite ran and its verdict is a failure."""

    exit_code = int(ExitCode.VALIDATION)


@dataclass(frozen=True)
class ScenarioRecord:
    name: str
    stage: str
    expect_fail: bool
    shares_map: bool
    path: Path
    seed: int = 0

    @property
    def map_name(self) -> str | None:
        return self.stage[len(MAP_STAGE_PREFIX):] if self.stage.startswith(MAP_STAGE_PREFIX) else None


@dataclass(frozen=True)
class Boot:
    """One editor launch: one host, the records it runs, and its directory under the run."""

    stage: str
    records: tuple[ScenarioRecord, ...]
    slug: str

    @property
    def map_name(self) -> str | None:
        return self.stage[len(MAP_STAGE_PREFIX):] if self.stage.startswith(MAP_STAGE_PREFIX) else None

    @property
    def seed(self) -> int:
        """The boot's seed: its first record's (H17).

        A map host activates the map once, at boot, before any record starts, so the boot is seeded
        by New Game from this value and every record re-seeds from its own `seed` at its start, as
        the host has always done. A record booted alone therefore replays whole for its seed; a
        later record of a shared boot runs on the map the earlier records left, and replays only as
        part of that same boot, in that order.
        """
        return self.records[0].seed if self.records else 0

    @property
    def names(self) -> list[str]:
        return [record.name for record in self.records]


def scenario_root(project_dir: Path) -> Path:
    return Path(project_dir) / SCENARIO_DIR


def _read_record(path: Path, root: Path) -> ScenarioRecord:
    where = path.relative_to(root).as_posix()
    try:
        data = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, ValueError) as exc:
        raise ArenaError(f"scenario record {where}: not readable JSON ({exc})") from exc
    if not isinstance(data, dict):
        raise ArenaError(f"scenario record {where}: the top level is not an object")
    name = data.get("name")
    if not isinstance(name, str) or not name.strip():
        raise ArenaError(f"scenario record {where}: `name` is missing or not a string")
    stage = data.get("stage")
    if not isinstance(stage, str) or not (
            stage == ARENA_STAGE
            or (stage.startswith(MAP_STAGE_PREFIX) and len(stage) > len(MAP_STAGE_PREFIX))):
        raise ArenaError(
            f"scenario record {where}: `stage` must be \"{ARENA_STAGE}\" or "
            f"\"{MAP_STAGE_PREFIX}<map>\", not {stage!r}")
    expect_fail = data.get("expect_fail", False)
    shares_map = data.get("shares_map", False)
    for key, value in (("expect_fail", expect_fail), ("shares_map", shares_map)):
        if not isinstance(value, bool):
            raise ArenaError(f"scenario record {where}: `{key}` must be true or false")
    seed = data.get("seed", 0)
    if isinstance(seed, float) and seed.is_integer():
        seed = int(seed)
    if isinstance(seed, bool) or not isinstance(seed, int) or not 0 <= seed <= SEED_MAX:
        raise ArenaError(
            f"scenario record {where}: `seed` must be a whole number from 0 to {SEED_MAX}, "
            f"not {seed!r}")
    return ScenarioRecord(name=name.strip(), stage=stage, expect_fail=expect_fail,
                          shares_map=shares_map, path=path, seed=seed)


def discover_records(project_dir: Path) -> list[ScenarioRecord]:
    """Every record under `Arena/scenarios/`, in relative-path order; a duplicate name is an error."""

    root = scenario_root(project_dir)
    if not root.is_dir():
        raise ArenaError(f"no scenario records: {root} does not exist")
    paths = sorted(root.rglob("*.json"), key=lambda path: path.relative_to(root).as_posix())
    records = [_read_record(path, root) for path in paths]
    seen: dict[str, Path] = {}
    for record in records:
        if record.name in seen:
            raise ArenaError(
                f"scenario name '{record.name}' is used by both "
                f"{seen[record.name].relative_to(root).as_posix()} and "
                f"{record.path.relative_to(root).as_posix()}")
        seen[record.name] = record.path
    if not records:
        raise ArenaError(f"no scenario records under {root}")
    return records


def select_records(records: Sequence[ScenarioRecord], names: Iterable[str]) -> list[ScenarioRecord]:
    """The records named (every one when none is), in discovery order; an unknown name is an error."""

    wanted = [name for name in names if name]
    if not wanted:
        return list(records)
    known = {record.name for record in records}
    unknown = [name for name in wanted if name not in known]
    if unknown:
        raise ArenaError(
            f"no scenario record named {', '.join(repr(name) for name in unknown)}; "
            f"the records are: {', '.join(sorted(known))}")
    chosen = set(wanted)
    return [record for record in records if record.name in chosen]


def _slug(text: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "-", text).strip("-") or "boot"


def plan_boots(records: Sequence[ScenarioRecord]) -> list[Boot]:
    """One boot per host, arena first; then the map hosts in the order their records come.

    A map host is not rebuilt between records, so a map record runs alone in its own boot unless it
    sets `"shares_map": true`; the sharing records of one map run together in one boot, ahead of
    that map's single ones.
    """

    boots: list[Boot] = []
    arena = tuple(record for record in records if record.stage == ARENA_STAGE)
    if arena:
        boots.append(Boot(stage=ARENA_STAGE, records=arena, slug=ARENA_STAGE))
    maps: list[str] = []
    for record in records:
        if record.stage != ARENA_STAGE and record.stage not in maps:
            maps.append(record.stage)
    for stage in maps:
        of_map = [record for record in records if record.stage == stage]
        map_slug = "map-" + _slug(stage[len(MAP_STAGE_PREFIX):])
        shared = tuple(record for record in of_map if record.shares_map)
        if shared:
            boots.append(Boot(stage=stage, records=shared, slug=map_slug))
        for record in of_map:
            if not record.shares_map:
                boots.append(Boot(stage=stage, records=(record,),
                                  slug=f"{map_slug}-{_slug(record.name)}"))
    return boots


def boot_arguments(common: Sequence[str], boot: Boot, out_dir: Path, hz: int) -> list[str]:
    """The editor argv for one boot, modelled on the `cast` harness's launch.

    No switch is ever passed with an empty value: Unreal's parser would take the next token as it.
    A path value is passed as one argv element and never quoted by hand, as `unreal.py` passes
    `-ReportExportPath=` and the import roots: the process layer quotes an element holding a space,
    and the engine's Windows launch (`ProcessCommandLine`, LaunchWindows.cpp) re-quotes the value of
    a `-Key=value with spaces` element for `FParse::Value`. A quote added here would be doubled.
    """

    arguments = [
        *common, "-ElysiumArena",
        f"-ArenaScenarios={','.join(boot.names)}",
        f"-ArenaOut={os.fspath(out_dir)}",
        f"-ArenaHz={hz}",
        "-UseFixedTimeStep", f"-FPS={hz}", "-nullrhi", "-unattended",
        "-nosplash", "-nosound", "-stdout", "-FullStdOutLogOutput",
    ]
    if boot.map_name:
        arguments.append(f"-ElysiumMap={boot.map_name}")
        # The map is activated once, at boot, before the first record's own re-seed: the boot's
        # seed must reach New Game, or the activation draws (every NPC's first think, every logic
        # timer) come from the clock and the record passes or fails by boot (H17). The arena host
        # rebuilds its stage per record, after re-seeding, so it is not passed one.
        arguments.append(f"-ArenaSeed={boot.seed}")
    return arguments


def read_host_index(out_dir: Path) -> dict | None:
    """A host's `index.json`, or None when it wrote none (or wrote something unreadable)."""

    index = Path(out_dir) / "index.json"
    if not index.is_file():
        return None
    try:
        data = json.loads(index.read_text(encoding="utf-8-sig"))
    except (OSError, ValueError):
        return None
    return data if isinstance(data, dict) and isinstance(data.get("scenarios"), list) else None


def _error_row(record: ScenarioRecord, boot: Boot, error: str) -> dict[str, Any]:
    return {
        "name": record.name, "result": "error", "known_red": "", "error": error,
        "first_unmet": None, "game_seconds": 0.0, "wall_seconds": 0.0, "events": 0, "trace": "",
        "host": boot.stage, "boot": boot.slug, "expect_fail": record.expect_fail,
    }


def merge_reports(run_dir: Path, outcomes: Sequence[tuple[Boot, dict | None, str]],
                  hz: int) -> dict[str, Any]:
    """One report from every boot's: (boot, its index or None, how its process ended).

    A record its host did not report, or every record of a host that wrote no index, is an `error`
    row -- a scenario that silently vanished is the failure this report exists to make visible.
    Trace paths become absolute, so the merged report can be read without knowing the layout.
    """

    hosts: list[dict[str, Any]] = []
    scenarios: list[dict[str, Any]] = []
    for boot, index, ended in outcomes:
        out_dir = Path(run_dir) / boot.slug
        host = {"host": boot.stage, "boot": boot.slug, "out": os.fspath(out_dir),
                "requested": boot.names, "process": ended or "exit 0", "index": index is not None}
        hosts.append(host)
        if index is None:
            reason = (f"the {boot.stage} host wrote no index.json"
                      + (f" ({ended})" if ended else ""))
            host["error"] = reason
            scenarios.extend(_error_row(record, boot, reason) for record in boot.records)
            continue
        by_name = {record.name: record for record in boot.records}
        reported: set[str] = set()
        for row in index.get("scenarios", []):
            if not isinstance(row, dict):
                continue
            merged = dict(row)
            name = str(merged.get("name", ""))
            reported.add(name)
            merged["host"] = boot.stage
            merged["boot"] = boot.slug
            merged["expect_fail"] = by_name[name].expect_fail if name in by_name else False
            trace = merged.get("trace") or ""
            merged["trace"] = os.fspath(out_dir / trace) if trace else ""
            scenarios.append(merged)
        for record in boot.records:
            if record.name not in reported:
                scenarios.append(_error_row(
                    record, boot, f"the {boot.stage} host did not report it"
                    + (f" ({ended})" if ended else "")))
    counts = {result: 0 for result in RESULT_ORDER}
    for row in scenarios:
        result = str(row.get("result", "error"))
        counts[result] = counts.get(result, 0) + 1
    return {
        "hz": hz,
        "out": os.fspath(run_dir),
        "hosts": hosts,
        "counts": counts,
        "failed": sum(1 for row in scenarios if str(row.get("result", "error")) in FAILING_RESULTS),
        "scenarios": scenarios,
    }


def verdict(merged: dict[str, Any]) -> int:
    """0 when nothing failed; the test command's verdict code (7) otherwise."""

    return int(ExitCode.VALIDATION) if merged.get("failed") else 0


def _first_unmet(row: dict[str, Any]) -> str:
    unmet = row.get("first_unmet")
    if not isinstance(unmet, dict):
        return ""
    expect = unmet.get("expect") if isinstance(unmet.get("expect"), dict) else {}
    deadline = unmet.get("deadline")
    by = f" by {float(deadline):.1f}s" if isinstance(deadline, (int, float)) else ""
    return (f"#{unmet.get('index', '?')} {expect.get('who', '?')} {expect.get('kind', '?')} "
            f"{json.dumps(expect.get('match', ''))}{by}")


def summary_lines(merged: dict[str, Any]) -> list[str]:
    """One headline, one line per scenario, and under each failure what went unmet."""

    rows = merged.get("scenarios", [])
    counts = merged.get("counts", {})
    hosts = merged.get("hosts", [])
    told = ", ".join(f"{counts[result]} {result}" for result in RESULT_ORDER if counts.get(result))
    lines = [f"arena: {len(rows)} scenario(s) in {len(hosts)} boot(s): {told or 'nothing ran'}"]
    width = max((len(str(row.get("name", ""))) for row in rows), default=0)
    for row in rows:
        result = str(row.get("result", "error"))
        tags = []
        if row.get("expect_fail"):
            tags.append("self-test")
        if row.get("host") and row.get("host") != ARENA_STAGE:
            tags.append(str(row["host"]))
        tag = f"  ({', '.join(tags)})" if tags else ""
        timing = (f"{float(row.get('game_seconds') or 0.0):.1f}s game "
                  f"{float(row.get('wall_seconds') or 0.0):.1f}s wall "
                  f"{int(row.get('events') or 0)} events")
        lines.append(f"  {result:<15} {str(row.get('name', '')):<{width}}  {timing}{tag}")
        if result in FAILING_RESULTS:
            unmet = _first_unmet(row)
            if unmet:
                lines.append(f"    first unmet: {unmet}")
            if row.get("error"):
                lines.append(f"    error: {row['error']}")
            if row.get("trace"):
                lines.append(f"    trace: {row['trace']}")
        elif result == "expected-fail" and row.get("known_red"):
            lines.append(f"    known red: {row['known_red']}")
    lines.append(f"  report: {Path(str(merged.get('out', ''))) / 'index.json'}")
    return lines


def payload(merged: dict[str, Any]) -> dict[str, Any]:
    """What `--json` carries for an arena run (`arena_report`: the envelope owns `report`)."""

    return {
        "arena_report": os.fspath(Path(str(merged.get("out", ""))) / "index.json"),
        "hz": merged.get("hz"),
        "counts": merged.get("counts", {}),
        "failed": merged.get("failed", 0),
        "scenarios": [
            {key: row.get(key) for key in
             ("name", "result", "host", "known_red", "error", "first_unmet", "game_seconds",
              "wall_seconds", "events", "trace")}
            for row in merged.get("scenarios", [])
        ],
    }


def _prune(reports_dir: Path, keep: int) -> None:
    if not reports_dir.is_dir():
        return
    existing = sorted((child for child in reports_dir.iterdir() if child.is_dir()),
                      key=lambda child: child.name)
    for stale in existing[:max(0, len(existing) - keep)]:
        try:
            shutil.rmtree(stale)
        except OSError as exc:
            print(f"WARNING - could not prune arena report {stale}: {exc}")


def _editor_launcher(config, runner) -> Launcher:
    """The real launch: `unreal`'s editor runner (headless flags, shader dir), bounded."""

    from elysium_pipeline import unreal

    try:
        executable = unreal.editor_executable(config)
    except unreal.UnrealFailure as exc:
        raise unreal.UnrealFailure(str(exc), exit_code=int(ExitCode.UNREAL_OR_BAKE)) from exc

    def launch(arguments: Sequence[str]) -> str:
        try:
            unreal._run(config, runner, executable, list(arguments),
                        timeout=BOOT_TIMEOUT_SECONDS, idle_timeout=BOOT_IDLE_TIMEOUT_SECONDS,
                        category=int(ExitCode.UNREAL_OR_BAKE))
        except unreal.UnrealFailure as exc:
            return str(exc)
        except ProcessFailure as exc:
            return str(exc)
        return ""

    return launch


def run_arena(config, runner, names: Iterable[str] = (), *, hz: int = DEFAULT_HZ,
              launch: Launcher | None = None, stamp: str | None = None) -> dict[str, Any]:
    """Discover, select, boot once per host, merge, and write the run's `index.json`.

    Every request error -- no records, a malformed one, an unknown name -- is raised before
    anything boots. `launch` and `stamp` are seams for the tests.
    """

    if hz < 1:
        raise ArenaError(f"--hz must be a positive rate, not {hz}")
    if config.work_root is None:
        raise ArenaError("the arena suite needs ELYSIUM_WORK_ROOT for its report")
    records = select_records(discover_records(Path(config.project).parent), names)
    boots = plan_boots(records)
    if launch is None:
        launch = _editor_launcher(config, runner)

    reports_dir = Path(config.work_root) / "reports" / "arena"
    _prune(reports_dir, REPORT_RETENTION - 1)
    base = stamp or datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    run_dir = reports_dir / base
    suffix = 1
    while run_dir.exists():
        run_dir = reports_dir / f"{base}-{suffix}"
        suffix += 1
    run_dir.mkdir(parents=True)

    from elysium_pipeline import unreal

    common = unreal.common_game_args(config)
    outcomes: list[tuple[Boot, dict | None, str]] = []
    for boot in boots:
        out_dir = run_dir / boot.slug
        out_dir.mkdir(parents=True, exist_ok=True)
        print(f"[arena] {boot.stage}: {', '.join(boot.names)}")
        ended = launch(boot_arguments(common, boot, out_dir, hz))
        outcomes.append((boot, read_host_index(out_dir), ended))

    merged = merge_reports(run_dir, outcomes, hz)
    (run_dir / "index.json").write_text(
        json.dumps(merged, indent=2, sort_keys=False) + "\n", encoding="utf-8")
    return merged
