"""UnrealBuildTool, editor, content, bake, test, and development harness driver."""

from __future__ import annotations

from collections.abc import Sequence
from dataclasses import dataclass
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil

from elysium_pipeline import workspace_lock


FONT_ASSETS = (
    "FF_SpectralSC_Regular.uasset",
    "FF_SpectralSC_SemiBold.uasset",
    "FF_Spectral_Regular.uasset",
    "FF_Spectral_Italic.uasset",
    "FF_Spectral_SemiBold.uasset",
    "FF_Inter_Regular.uasset",
    "FF_Inter_SemiBold.uasset",
)
#: A map's stages. Prop meshes belong to the shared corpus scope, whose stages are its own.
TEST_ABSTENTION_TOKEN = "ELYSIUM_TEST_ABSTAIN"

#: The compile database clangd reads, and the lock guarding the engine-root copy every
#: checkout on this machine writes through. `.clangd` points clangd at the repository root,
#: so the generated file is copied out of the engine tree to sit beside it.
CLANG_DATABASE = "compile_commands.json"
CLANG_DATABASE_LOCK = ".elysium-clang-database.lock"

#: The tier names `uv run elysium test` accepts, and the automation filter each selects. A bare
#: word that is not one of these is a typo rather than a filter -- `Automation RunTest` matches by
#: substring and reports success for a selection that matched nothing.
TEST_TIERS = {
    "substrate": "Elysium.Substrate.",
    "content": "Elysium.Content.",
    # Needs a generated `/Game` package but no export corpus -- a real material graph, a declared
    # input asset. Separate from `content` so a corpus run is not slowed by generated-asset policy
    # and a reader is not told the run needed the user's own game.
    "policy": "Elysium.Policy.",
}

#: How long an automation launch may take before the watchdog kills it. Well above a cold boot
#: that compiles shaders (the slowest observed tier run is under two minutes) and far below
#: "forever", which is what a wedged commandlet costs without it.
TEST_TIMEOUT_SECONDS = 900.0

#: Deadlines for the unattended launches a profile run drives. A commandlet that wedges before
#: its own logging starts produces no diagnostic and no exit, so every launch carries a bound
#: sized to the work it does. `process.run` kills the child on the deadline and raises
#: `ProcessTimeout`, which names the command and its bound.
BUILD_TIMEOUT_SECONDS = 3600.0
#: Generating the compile database is a UnrealBuildTool run that compiles nothing -- it filters
#: the actions the build already planned and writes them out, about two seconds here. The bound
#: is loose enough to absorb a cold `dotnet` start and tight enough that a wedged run does not
#: hold the build hostage for the hour `BUILD_TIMEOUT_SECONDS` allows.
CLANG_DATABASE_TIMEOUT_SECONDS = 300.0
POLICY_TIMEOUT_SECONDS = 1800.0
CORPUS_TIMEOUT_SECONDS = 7200.0
#: One batch is `MAP_BAKE_BATCH` maps of Nanite build, Lumen surface-cache fitting and texture
#: compression, each a cold DDC miss on a first run.
MAP_BAKE_TIMEOUT_SECONDS = 10800.0
CHARACTER_BAKE_TIMEOUT_SECONDS = 7200.0
WIELD_BAKE_TIMEOUT_SECONDS = 3600.0
CLOTH_TIMEOUT_SECONDS = 1800.0

#: How many automation reports are retained under `$ELYSIUM_WORK_ROOT/reports/tests/`. Each run
#: writes a stamped directory and nothing used to remove one, so the directory grew without bound.
#: The reports are a debugging aid for the run you just made, not an archive -- git and the run
#: journals in `$ELYSIUM_WORK_ROOT/logs/` are the durable record.
TEST_REPORT_RETENTION = 50


class UnrealFailure(RuntimeError):
    pass


_EDITOR_EXECUTABLES = frozenset({"unrealeditor.exe", "unrealeditor-cmd.exe"})

#: How many streamed editor-output lines stay in memory for reports. The runner's mirror log
#: keeps every line, so the bound loses nothing the log does not already hold.
EDITOR_TAIL_LINES = 2000

#: Idle services a batch editor launch pays boot cost for and never uses: Live Coding's
#: console thread, the audio device, and the Perforce source-control probe. Applied only to
#: `-unattended` launches -- an interactive editor keeps Live Coding for C++ hot reload and an
#: attended game keeps its sound.
_HEADLESS_EDITOR_ARGS = ("-NoLiveCoding", "-noP4", "-nosound")


def _run(config, runner, executable: Path | str, args: Sequence[str], *,
         timeout: float | None = None) -> None:
    arguments = list(map(str, args))
    executable_name = Path(executable).name.casefold()
    tail_lines = None
    if executable_name in _EDITOR_EXECUTABLES:
        tail_lines = EDITOR_TAIL_LINES
        shader_work_root = getattr(config, "unreal_shader_work_root", None)
        if shader_work_root is not None and not any(
            argument.casefold().startswith("-shaderworkingdir=")
            for argument in arguments
        ):
            arguments.append(f"-shaderworkingdir={shader_work_root}")
        lowered = {argument.casefold() for argument in arguments}
        if "-unattended" in lowered:
            arguments.extend(flag for flag in _HEADLESS_EDITOR_ARGS
                             if flag.casefold() not in lowered)
    result = runner.run(
        [str(executable), *arguments], cwd=config.repo_root, tail_lines=tail_lines,
        timeout=timeout,
    )
    if result.returncode:
        raise UnrealFailure(f"{executable} exited with {result.returncode}")


def editor_executable(config, *, commandlet: bool = False) -> Path:
    leaf = "UnrealEditor-Cmd.exe" if commandlet else "UnrealEditor.exe"
    path = config.ue_root / "Engine" / "Binaries" / "Win64" / leaf
    if not path.is_file():
        raise UnrealFailure(f"Unreal editor executable not found: {path}")
    return path


#: Unreal Build Accelerator's own default, and the base of the per-checkout range below.
ACCELERATOR_BASE_PORT = 1345
ACCELERATOR_PORT_RANGE = 64


def accelerator_port(config) -> int:
    """One stable Unreal Build Accelerator port per checkout.

    The accelerator's server binds a fixed port, so concurrent builds from different
    engine copies collide on it. Deriving the port from the checkout path keeps a lane's
    port stable across runs without any stored assignment.
    """

    seed = os.path.normcase(str(Path(config.repo_root).resolve())).encode("utf-8")
    offset = int.from_bytes(hashlib.blake2b(seed, digest_size=2).digest(), "big")
    return ACCELERATOR_BASE_PORT + (offset % ACCELERATOR_PORT_RANGE)


def build(config, runner, mode: str = "", extra: Sequence[str] = ()) -> None:
    script = {
        "rebuild": "Rebuild.bat",
        "clean": "Clean.bat",
        "analyze": "Build.bat",
        "": "Build.bat",
    }.get(mode)
    if script is None:
        raise ValueError(f"unknown build mode: {mode}")
    arguments = [
        "ElysiumUEEditor",
        "Win64",
        "Development",
        f"-Project={config.project}",
        "-WaitMutex",
    ]
    # UnrealBuildTool and its accelerator both default to one machine-wide log and trace
    # under the user settings directory, which every engine copy resolves to the same path,
    # and the accelerator's server always binds port 1345. Two checkouts building at once
    # race to rotate the same log and die before reaching a compiler, so each checkout gets
    # its own files and its own port.
    log_root = getattr(config, "log_root", None)
    if log_root is not None:
        log_root.mkdir(parents=True, exist_ok=True)
        arguments.append(f"-Log={log_root / 'UnrealBuildTool.log'}")
        arguments.append(f"-UBATraceOutputFile={log_root / 'UnrealBuildAccelerator.uba'}")
    arguments.append(f"-UBAPort={accelerator_port(config)}")
    if mode == "analyze":
        arguments.append("-StaticAnalyzer=Default")
    arguments.extend(extra)
    _run(config, runner, config.ue_root / "Engine" / "Build" / "BatchFiles" / script,
         arguments, timeout=BUILD_TIMEOUT_SECONDS)


def generate_clang_database(config, runner) -> None:
    """Refresh this checkout's `compile_commands.json` from the target last built.

    clangd resolves a file's include paths, its forced-included `Definitions.h` and the UHT
    output directory holding its `.generated.h` by looking the file up in this database. A
    source file added since the last generation has no entry, so it parses with guessed
    flags -- against Unreal that yields a file of unresolved headers and no usable symbols
    rather than a visible error. Run this after adding or removing a source file.

    This is deliberately not on the build path, and running it costs one full rebuild.
    `-Mode=GenerateClangDatabase` forces `-Compiler=Clang`, `-NoPCH` and a non-unity build,
    and UnrealBuildTool is supposed to keep that out of the real build's way by suffixing the
    intermediate folder with `GCD`. On an *installed* engine it does not: `UEBuildTarget`
    reads `Unreal.IsEngineInstalled() ? UnrealIntermediateEnvironment.Default : ...`, so the
    suffix is dropped and every module's `.rsp` under `Intermediate/Build/.../<Module>/` is
    rewritten with clang flags. The next build finds them modified, invalidates the makefile
    and recompiles the whole target. A build that also refreshed the database would therefore
    never be incremental again.

    `-NoExecCodeGenActions` skips the header-tool run the last build already did. The
    generated headers are on disk, so the database comes out byte-identical to a full run at
    a fraction of the cost.
    """

    if config.ue_root is None:
        return
    engine_copy = config.ue_root / CLANG_DATABASE
    arguments = [
        "-Mode=GenerateClangDatabase",
        "-NoExecCodeGenActions",
        f"-project={config.project}",
        "-game",
        "ElysiumUEEditor",
        "Win64",
        "Development",
        "-WaitMutex",
    ]
    log_root = getattr(config, "log_root", None)
    if log_root is not None:
        log_root.mkdir(parents=True, exist_ok=True)
        arguments.append(f"-Log={log_root / 'UnrealBuildTool.ClangDatabase.log'}")
    # UnrealBuildTool always writes the database to the engine root, which every checkout on
    # this machine shares, so generating it and copying it here is one critical section --
    # otherwise a checkout can copy the database another checkout just wrote for its own
    # sources. The lock lives beside the file it guards, in the tree they contend over.
    with workspace_lock.exclusive_path_lock(config.ue_root / CLANG_DATABASE_LOCK):
        _run(config, runner,
             config.ue_root / "Engine" / "Build" / "BatchFiles" / "Build.bat",
             arguments, timeout=CLANG_DATABASE_TIMEOUT_SECONDS)
        shutil.copyfile(engine_copy, config.repo_root / CLANG_DATABASE)


def generate_policy_content(config, runner, generators: Sequence[str] | None = None, *,
                            include_auxiliary: bool = True) -> None:
    common = ["-unattended", "-nosplash", "-nopause", "-stdout"]
    content_arguments = [
        str(config.project),
        "-run=pythonscript",
        f"-script={config.repo_root / 'pipeline/unreal/build_content.py'}",
    ]
    if generators is not None:
        content_arguments.append("-PolicyGenerators=" + ",".join(dict.fromkeys(generators)))
    content_arguments.extend(common)
    _run(
        config,
        runner,
        editor_executable(config, commandlet=True),
        content_arguments,
        timeout=POLICY_TIMEOUT_SECONDS,
    )
    if include_auxiliary:
        generate_auxiliary_policy_content(config, runner)


def generate_auxiliary_policy_content(config, runner) -> None:
    """The two policy launches `build_content.py` cannot host, plus their existence checks.

    The font import needs a Slate application, which `-run=pythonscript` never creates, so it
    boots the full editor; `make_player_anim_bp.py` runs `main()` at module scope and is not a
    registered generator, so `build_content.py` refuses its name and it keeps its own
    commandlet.
    """
    common = ["-unattended", "-nosplash", "-nopause", "-stdout"]
    _run(
        config,
        runner,
        editor_executable(config),
        [
            str(config.project),
            f"-ExecutePythonScript={config.repo_root / 'pipeline/unreal/make_ui_fonts.py'}",
            *common,
        ],
        timeout=POLICY_TIMEOUT_SECONDS,
    )
    # The player animation graph, rebuilt from its tracked text. A commandlet rather than the
    # Slate-enabled editor above: it authors no font and needs no RHI.
    _run(
        config,
        runner,
        editor_executable(config, commandlet=True),
        [
            str(config.project),
            "-run=pythonscript",
            f"-script={config.repo_root / 'pipeline/unreal/make_player_anim_bp.py'}",
            *common,
        ],
        timeout=POLICY_TIMEOUT_SECONDS,
    )
    graph_asset = (
        config.repo_root / "Content" / "ElysiumGenerated" / "Animation" / "ABP_ElysiumBiped.uasset"
    )
    if not graph_asset.is_file():
        raise UnrealFailure("the player animation graph was not generated: " + str(graph_asset))

    font_root = config.repo_root / "Content" / "ElysiumGenerated" / "UI" / "Fonts"
    missing = [name for name in FONT_ASSETS if not (font_root / name).is_file()]
    if missing:
        raise UnrealFailure("font generation did not produce: " + ", ".join(missing))


def bake_corpus(config, runner, *, force: bool = False) -> None:
    """Bake the shared asset corpus onto /ElysiumBaked/Shared.

    One scope, no map: a texture, a material and a static model belong to the install, so each is
    baked once rather than once per map that draws it. The commandlet decides per asset whether
    anything is authored, by comparing each recipe against the hash stamped on the asset, so a
    fully current corpus launches, reports every asset reused, and exits.
    """
    _run(
        config,
        runner,
        editor_executable(config, commandlet=True),
        [
            str(config.project),
            "-run=pythonscript",
            f"-script={config.repo_root / 'pipeline/unreal/bake_map.py'}",
            "-BakeCorpus=1",
            *(["-BakeForce=1"] if force else []),
            "-AllowCommandletRendering",
            "-unattended",
            "-nosplash",
            "-nopause",
            "-stdout",
            "-FullStdOutLogOutput",
        ],
        timeout=CORPUS_TIMEOUT_SECONDS,
    )


#: Maps per editor process. The commandlet garbage-collects between maps, but loaded texture
#: platform data and its RHI resources still accumulate across the loop -- one process reached
#: 23 GB and the machine's commit limit at the fiftieth map -- so a profile bake runs in
#: processes of this many maps, each starting from a fresh heap for about ten seconds of boot.
MAP_BAKE_BATCH = 12


def bake_maps(
    config,
    runner,
    maps: Sequence[str],
    *,
    force: bool = False,
    particles: bool = False,
    batch_size: int | None = None,
) -> None:
    """Bake the named maps, `batch_size` maps per editor process (the whole list when None).

    `particles` opts the map's Niagara authoring pass in; it is off by default because
    force-deleting a Niagara package the asset compiler still owns crashes the editor, and a
    launch without it leaves whatever particle packages the mount already carries untouched.

    Editor start-up (module load, plugin init, registry scan) is the fixed cost per process;
    the commandlet garbage-collects between maps and isolates per-map failures, and a crash
    mid-run costs only a relaunch: every saved asset carries its recipe stamp and is reused.
    Profile bakes pass `MAP_BAKE_BATCH` so one process's growth stays bounded.

    Per-asset reuse is the commandlet's own decision, read off each asset's recipe stamp; a
    failed batch is reported by the editor log naming the failing map.
    """
    maps = list(dict.fromkeys(maps))
    step = max(1, batch_size) if batch_size else max(1, len(maps))
    for offset in range(0, len(maps), step):
        batch = maps[offset : offset + step]
        _run(
            config,
            runner,
            editor_executable(config, commandlet=True),
            [
                str(config.project),
                "-run=pythonscript",
                f"-script={config.repo_root / 'pipeline/unreal/bake_map.py'}",
                f"-BakeMaps={','.join(batch)}",
                *(["-BakeForce=1"] if force else []),
                *(["-BakeParticles=1"] if particles else []),
                "-AllowCommandletRendering",
                "-unattended",
                "-nosplash",
                "-nopause",
                "-stdout",
                "-FullStdOutLogOutput",
            ],
            timeout=MAP_BAKE_TIMEOUT_SECONDS,
        )


def bake_characters(config, runner, stems: Sequence[str], *, props: Sequence[str] = (),
                    force: bool = False) -> None:
    """Bake the named models onto /ElysiumBaked/Characters.

    The whole cast goes through one editor process, and the commandlet decides per unit whether
    anything is authored, by comparing each recipe against the hash stamped on the assets -- a
    current cast launches, reports every unit reused, and exits.

    Memory is bounded by releasing each scope's packages as it completes rather than by the async
    compilation throttler, which cannot see this work: only a task reporting -1 draws against that
    budget and animation compression reports 0.
    """
    stems = list(dict.fromkeys(stems))
    props = list(dict.fromkeys(props))
    if not stems and not props:
        raise ValueError("character bake needs at least one body or placed model")
    arguments = [
        str(config.project),
        "-run=pythonscript",
        f"-script={config.repo_root / 'pipeline/unreal/bake_characters.py'}",
        f"-BakeCharacters={','.join(stems)}",
        "-unattended",
        "-nosplash",
        "-nopause",
        "-stdout",
        "-FullStdOutLogOutput",
    ]
    if props:
        arguments.insert(4, f"-BakeProps={','.join(props)}")
    if force:
        arguments.insert(4, "-BakeForce=1")
    _run(config, runner, editor_executable(config, commandlet=True), arguments,
         timeout=CHARACTER_BAKE_TIMEOUT_SECONDS)


def bake_wield(config, runner, stems: Sequence[str] = ()) -> None:
    """Bake the named wield models onto /ElysiumBaked/Items/Wield.

    One editor process, `-BakeWield=<csv>` naming the stems to build. Empty `stems` bakes the
    whole corpus -- `bake_wield.py` itself treats an empty selector as every real model the
    manifest declares, so this function does not have to resolve that list itself.
    """
    stems = list(dict.fromkeys(stems))
    arguments = [
        str(config.project),
        "-run=pythonscript",
        f"-script={config.repo_root / 'pipeline/unreal/bake_wield.py'}",
        f"-BakeWield={','.join(stems)}",
        "-unattended",
        "-nosplash",
        "-nopause",
        "-stdout",
        "-FullStdOutLogOutput",
    ]
    _run(config, runner, editor_executable(config, commandlet=True), arguments,
         timeout=WIELD_BAKE_TIMEOUT_SECONDS)


def make_cloth_assets(config, runner, stems: Sequence[str]) -> None:
    """Generate a Chaos cloth asset per authored garment among `stems`.

    Runs behind the character bake rather than under `build_content`'s umbrella: a cloth asset
    binds to a skeletal mesh's reference skeleton, and the umbrella runs before any character
    exists. Most named models author no garment and are simply absent from the sidecar directory.
    """
    _run(
        config,
        runner,
        editor_executable(config, commandlet=True),
        [
            str(config.project),
            "-run=pythonscript",
            f"-script={config.repo_root / 'pipeline/unreal/make_cloth_assets.py'}",
            f"-ClothStems={','.join(dict.fromkeys(stems))}",
            "-unattended",
            "-nosplash",
            "-nopause",
            "-stdout",
            "-FullStdOutLogOutput",
        ],
        timeout=CLOTH_TIMEOUT_SECONDS,
    )


def verify_characters(config, runner, stems: Sequence[str], *, props: Sequence[str] = ()) -> None:
    stems = list(dict.fromkeys(stems))
    props = list(dict.fromkeys(props))
    if not stems and not props:
        raise ValueError("character verification needs at least one body or placed model")
    arguments = [
        str(config.project),
        "-run=pythonscript",
        f"-script={config.repo_root / 'pipeline/unreal/bake_verify_characters.py'}",
        f"-BakeCharacters={','.join(stems)}",
        "-unattended",
        "-nosplash",
        "-nopause",
        "-stdout",
        "-FullStdOutLogOutput",
    ]
    if props:
        arguments.insert(4, f"-BakeProps={','.join(props)}")
    _run(
        config,
        runner,
        editor_executable(config, commandlet=True),
        arguments,
        timeout=CHARACTER_BAKE_TIMEOUT_SECONDS,
    )


def verify_bakes(config, runner, maps: Sequence[str], *, batch_size: int = 4) -> None:
    maps = list(dict.fromkeys(maps))
    for offset in range(0, len(maps), max(1, batch_size)):
        batch = maps[offset : offset + max(1, batch_size)]
        _run(
            config,
            runner,
            editor_executable(config, commandlet=True),
            [
                str(config.project),
                "-run=pythonscript",
                f"-script={config.repo_root / 'pipeline/unreal/bake_verify.py'}",
                f"-BakeMaps={','.join(batch)}",
                "-unattended",
                "-nosplash",
                "-nopause",
                "-stdout",
                "-FullStdOutLogOutput",
            ],
            timeout=MAP_BAKE_TIMEOUT_SECONDS,
        )


def run_tests(config, runner, filter_name: str = "Elysium.", *,
              parity_stems: Sequence[str] = ()) -> dict:
    """Run an automation selection and report what actually executed.

    `parity_stems` widens the per-model parity slice, which is otherwise two hard-coded bodies.
    It is the only test in the suite built for slice iteration, and it was previously reachable
    only by invoking the commandlet by hand.

    Raises rather than returning a green summary when the run proved nothing: an unknown tier
    name, a selection that matched no test, a tier that abstained entirely, or a report that
    counts failures. `Automation RunTest` matches by substring and reports success for a
    selection that matched nothing, so a typo is otherwise indistinguishable from a clean run.
    """
    selected = TEST_TIERS.get(filter_name.lower())
    if selected is None:
        # A bare word is a tier name and only these exist; anything carrying a dot is a caller
        # spelling a fully qualified filter, which is passed through untouched.
        if "." not in filter_name:
            raise UnrealFailure(
                f"unknown test tier '{filter_name}'; expected one of "
                f"{', '.join(sorted(TEST_TIERS))}, or a fully qualified filter such as "
                f"'Elysium.Substrate.Knockback.'"
            )
        selected = filter_name
    if config.work_root is None:
        raise UnrealFailure("automation needs ELYSIUM_WORK_ROOT for its retained report")
    reports_dir = config.work_root / "reports" / "tests"
    prune_test_reports(reports_dir, TEST_REPORT_RETENTION - 1)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    slug = re.sub(r"[^a-z0-9]+", "-", selected.lower()).strip("-") or "all"
    report = reports_dir / f"{stamp}-{slug}"
    suffix = 1
    while report.exists():
        report = report.with_name(f"{stamp}-{slug}-{suffix}")
        suffix += 1
    arguments = [
        str(config.project),
        f"-ElysiumContentRoot={config.export_root}",
        f"-ExecCmds=Automation RunTest {selected};Quit",
        f"-ReportExportPath={report}",
        "-unattended",
        "-nopause",
        "-nosplash",
        "-nullrhi",
        "-stdout",
        "-FullStdOutLogOutput",
    ]
    if parity_stems:
        arguments.insert(2, "-ElysiumParityStems=" + ",".join(parity_stems))
    _run(config, runner, editor_executable(config, commandlet=True), arguments,
         timeout=TEST_TIMEOUT_SECONDS)
    summary = summarize_test_report(report)
    summary["report_path"] = str(report.resolve())
    if not summary["total"]:
        raise UnrealFailure(
            f"'{selected}' matched no test; the run proved nothing "
            f"(report: {summary['report_path']})"
        )
    if summary["failed"]:
        raise UnrealFailure(
            f"{summary['failed']} of {summary['total']} test(s) failed "
            f"(report: {summary['report_path']})"
        )
    if not summary["executed"]:
        raise UnrealFailure(
            f"all {summary['total']} test(s) under '{selected}' abstained; the prerequisite "
            f"they need is unavailable, so the tier is vacuous "
            f"(report: {summary['report_path']})"
        )
    return summary


def prune_test_reports(reports_dir: Path, keep: int = TEST_REPORT_RETENTION) -> int:
    """Drop all but the newest `keep` automation reports. Returns how many were removed.

    Names are UTC stamps, so lexical order is chronological. A report that cannot be removed --
    a viewer holding its HTML open, most often -- is reported and skipped rather than failing the
    run that just passed.
    """
    if not reports_dir.is_dir():
        return 0
    existing = sorted((child for child in reports_dir.iterdir() if child.is_dir()),
                      key=lambda child: child.name)
    removed = 0
    for stale in existing[:max(0, len(existing) - keep)]:
        try:
            shutil.rmtree(stale)
            removed += 1
        except OSError as exc:
            print(f"WARNING - could not prune automation report {stale}: {exc}")
    return removed


def summarize_test_report(report_dir: Path) -> dict:
    """{total, executed, abstained, failed, seconds, abstentions} from an automation report.

    The commandlet's exit code is the only verdict the pipeline reads, and a test that declines to
    run reports Success -- so a tier can be green while most of it never touched an asset. This
    reads the report back so the count that matters is visible: how many tests actually ran.
    """
    index = report_dir / "index.json"
    summary = {"total": 0, "executed": 0, "abstained": 0, "failed": 0,
               "seconds": 0.0, "abstentions": []}
    if not index.is_file():
        return summary
    try:
        with index.open(encoding="utf-8-sig") as handle:
            data = json.load(handle)
    except (OSError, ValueError):
        return summary
    tests = data.get("tests", [])
    summary["total"] = len(tests)
    summary["failed"] = int(data.get("failed", 0) or 0)
    summary["seconds"] = float(data.get("totalDuration", 0.0) or 0.0)
    for test in tests:
        messages = " ".join(
            entry.get("event", {}).get("message", "") for entry in test.get("entries", [])
        )
        # New tests emit the token explicitly. The legacy phrases keep reports from older editor
        # builds readable while the C++ suite migrates; they can go once every supported build
        # emits TEST_ABSTENTION_TOKEN.
        if (TEST_ABSTENTION_TOKEN in messages
                or "marked incomplete" in messages
                or "skipping content validation" in messages):
            summary["abstained"] += 1
            summary["abstentions"].append(test.get("fullTestPath", "?"))
        else:
            summary["executed"] += 1
    return summary


def common_game_args(config) -> list[str]:
    return [str(config.project), "-game", f"-ElysiumContentRoot={config.export_root}"]


def run_editor(config, runner, extra: Sequence[str] = ()) -> None:
    _run(
        config,
        runner,
        editor_executable(config),
        [str(config.project), f"-ElysiumContentRoot={config.export_root}", *extra],
    )


# `play gr` is not a map: it boots the session into the green room's own stage world instead of
# naming a level, which is the same place `elysium.gr` goes from a running game.
GREEN_ROOM_TARGETS = frozenset({"gr", "greenroom", "green-room", "green_room"})


def run_play(config, runner, map_name: str | None = None, extra: Sequence[str] = ()) -> None:
    args = [
        *common_game_args(config),
        "-dx12",
        "-windowed",
        "-resx=1600",
        "-resy=900",
        "-log",
        "-NewConsole",
        "-LogCmds=LogElysiumWorld Verbose, LogElysiumIO Verbose",
    ]
    values = list(extra)
    if map_name and map_name.lower() in GREEN_ROOM_TARGETS:
        # The window is the point of this launch, so it comes up open, holding the mouse and docked
        # down the left edge rather than floating over the body it is there to inspect. F1 still
        # hands the keyboard back to the game.
        args += ["-ElysiumGreenRoom", "-GreenRoomLab", "-GreenRoomDock=left"]
        # `play gr <model> <clip>`, both optional -- with no model the stage comes up empty and the
        # window picks one. An empty `-Switch=` makes Unreal's parser swallow the NEXT token as the
        # value, so a blank pair is omitted rather than passed.
        for switch in ("GreenRoomStem", "GreenRoomClip"):
            if not values or values[0].startswith("-"):
                break
            args.append(f"-{switch}={values.pop(0)}")
    elif map_name:
        args.append(f"-ElysiumMap={map_name}")
    args.extend(values)
    _run(config, runner, editor_executable(config), args)


#: The bodies `debug compose` stands when none is named. Both of them, because these are the two
#: the retail capture recorded (`malkavian_male_armor_0`, 1,468 frames; `malkavian_female_armor_0`,
#: 1,254) and so the two whose composed pose there is a retail pose to score against at all.
COMPOSE_BODIES = ("malkavian_female_armor_0", "malkavian_male_armor_0")


@dataclass(frozen=True)
class HarnessOptions:
    """Harness-wide switches, split out of the positional arguments.

    These exist because the harnesses are otherwise only drivable by editing files: `--set` is the
    only way to put a console variable in front of a run, `--live` is the only way to watch one
    instead of reading stills afterwards, and `--promote` is the only way to say that what a run
    just recorded is the new truth.
    """

    exec_cmds: tuple[str, ...] = ()
    live: bool = False
    gym: bool = False
    sited: bool = False
    promote: bool = False
    drive: bool = False
    arena: bool = False
    #: The bodies a harness stands, in the order given. Empty means the harness's own default set,
    #: which for the composed-pose run is both of the bodies the retail capture recorded.
    bodies: tuple[str, ...] = ()


def _split_bodies(value: str) -> list[str]:
    """`--body a,b` and `--body a --body b` mean the same thing."""
    return [stem.strip() for stem in value.split(",") if stem.strip()]


def _take_options(values: list[str]) -> tuple[list[str], HarnessOptions]:
    """Returns `(positional, options)`.

    Parsed before any harness branch reads a positional, because a switch left in the positional
    list becomes an argument: `--gym` would arrive at the movement harness as `-MoveCourse=--gym`.
    """
    positional: list[str] = []
    exec_cmds: list[str] = []
    bodies: list[str] = []
    flags = {
        "--live": False,
        "--gym": False,
        "--sited": False,
        "--promote": False,
        "--drive": False,
        "--arena": False,
    }
    index = 0
    while index < len(values):
        value = values[index]
        if value in {"--set", "--cvar"}:
            if index + 1 >= len(values):
                raise ValueError(f"{value} needs a command, e.g. --set 'elysium.Mute 0'")
            exec_cmds.append(values[index + 1])
            index += 2
            continue
        if value.startswith(("--set=", "--cvar=")):
            exec_cmds.append(value.split("=", 1)[1])
            index += 1
            continue
        # `--body` is repeatable and comma-splittable, because a harness that stands one body per
        # launch needs the SET stated in one command line -- naming them one launch at a time is how
        # a second body ends up never run.
        if value == "--body":
            if index + 1 >= len(values):
                raise ValueError("--body needs a model stem, e.g. --body malkavian_male_armor_0")
            bodies.extend(_split_bodies(values[index + 1]))
            index += 2
            continue
        if value.startswith("--body="):
            bodies.extend(_split_bodies(value.split("=", 1)[1]))
            index += 1
            continue
        if value in flags:
            flags[value] = True
            index += 1
            continue
        positional.append(value)
        index += 1
    return positional, HarnessOptions(
        exec_cmds=tuple(exec_cmds),
        live=flags["--live"],
        gym=flags["--gym"],
        sited=flags["--sited"],
        promote=flags["--promote"],
        drive=flags["--drive"],
        arena=flags["--arena"],
        bodies=tuple(bodies),
    )


def run_harness(config, runner, kind: str, args: Sequence[str]) -> Path | None:
    values, options = _take_options(list(args))
    exec_cmds = list(options.exec_cmds)
    live = options.live
    gym_only, sited_only, promote = options.gym, options.sited, options.promote
    common = common_game_args(config)
    editor = editor_executable(config)
    if kind == "profile":
        map_name = values[0] if values else "sp_tutorial_1"
        launch = [
            *common, "-dx12", "-windowed", "-resx=2560", "-resy=1440",
            f"-ElysiumMap={map_name}", "-ElysiumProfile", "-csvGpuStats",
            "-unattended", "-nosplash", "-nopause", "-stdout", "-FullStdOutLogOutput",
        ]
        if len(values) > 1:
            launch.append(f"-ProfileCam={values[1]}")
        launch.extend(values[2:])
        _run(config, runner, editor, launch)
        _run(
            config,
            runner,
            os.fspath(Path(os.sys.executable)),
            ["-m", "elysium_pipeline.validation.profile_report", "--map", map_name],
        )
        return None
    if kind == "probe":
        maps = values or [
            path.name
            for path in config.export_root.iterdir()
            if path.is_dir() and (path / f"{path.name}.lights").is_file()
        ]
        if not maps:
            raise UnrealFailure("no exported maps with a .lights sidecar")
        rays = os.environ.get("PROBE_RAYS", "64")
        for map_name in maps:
            _run(
                config,
                runner,
                editor,
                [
                    *common, "-dx12", "-windowed", "-resx=640", "-resy=360", "-nosound",
                    f"-ElysiumMap={map_name}", "-ElysiumProbe", f"-ProbeRays={rays}",
                    "-unattended", "-nosplash", "-stdout", "-FullStdOutLogOutput",
                ],
            )
        return None
    if kind == "shots":
        map_name = values[0] if values else "sp_tutorial_1"
        launch = [
            *common, "-dx12", "-RenderOffScreen", "-ForceRes", "-windowed",
            "-ResX=2560", "-ResY=1440", f"-ElysiumMap={map_name}", "-ElysiumShots",
            "-unattended", "-nosplash", "-nopause", "-stdout", "-FullStdOutLogOutput",
        ]
        if len(values) > 1:
            launch.append(f"-ShotCam={values[1]}")
        launch.extend(values[2:])
        _run(config, runner, editor, launch)
        return None
    if kind == "move":
        course = values[0] if values else ""
        hz = values[1] if len(values) > 1 else "60"
        if course.isdigit():
            hz, course = course, ""

        # Two hosts, two questions. The gym brackets where a threshold is, on geometry derived from
        # the movement constants, in an empty stage world that needs no exported map. The sited
        # courses answer whether we match retail, which is real-geometry-only.
        hosts = ["gym", "sited"]
        if gym_only:
            hosts = ["gym"]
        elif sited_only:
            hosts = ["sited"]

        for host in hosts:
            if host == "sited" and not (config.export_root / "sp_tutorial_1").is_dir():
                print("[move] sp_tutorial_1 is not exported; skipping the sited courses")
                continue
            launch = [
                *common, "-ElysiumMove", f"-MoveHz={hz}",
                "-UseFixedTimeStep", f"-FPS={hz}", "-nullrhi", "-unattended",
                "-nosplash", "-nosound", "-stdout", "-FullStdOutLogOutput",
            ]
            launch.append("-MoveGym" if host == "gym" else "-ElysiumMap=sp_tutorial_1")
            if course:
                launch.append(f"-MoveCourse={course}")
            # Anything past the course and the rate reaches the editor verbatim, which is how
            # `-MoveBody=<stem>` picks the body the gym stands and how a cvar is set for one run.
            launch.extend(values[2:])
            if exec_cmds:
                launch.append("-ExecCmds=" + ";".join(exec_cmds))
            _run(config, runner, editor, launch)

        # Recording without judging is what this harness did before: the comparator existed but
        # nothing ran it. Chained here, its verdict is the command's own exit code.
        # No `--gym-baseline`: the gym stands a real baked body now, so its recordings are
        # game-derived and live beside the sited courses' under the work root, which the comparator
        # resolves for itself.
        diff = ["-m", "elysium_pipeline.validation.channel_diff"]
        if promote:
            diff.append("--promote")
        _run(config, runner, os.fspath(Path(os.sys.executable)), diff)
        return None
    if kind == "compose":
        # One map, one weapon, one BODY, one launch -- the harness seats a body and drives it, so a
        # second scenario in the same process would inherit the first one's motion. Two bodies is
        # therefore two launches, run here rather than left to the caller: the capture stands both
        # the male and the female Malkavian, and a defect the female's proportions happen to hide is
        # a defect nobody sees until the male is stood too.
        weapon = values[0] if values else "item_w_ithaca_m_37"
        hz = values[1] if len(values) > 1 else "60"
        if weapon.isdigit():
            hz, weapon = weapon, "item_w_ithaca_m_37"
        map_name = "sp_tutorial_1"
        for value in values:
            if value.startswith("-ElysiumMap="):
                map_name = value.split("=", 1)[1]
        if not (config.export_root / map_name).is_dir():
            raise UnrealFailure(
                f"{map_name} is not exported; the composed-pose run drives a body on a real map")
        bodies = list(options.bodies) or list(COMPOSE_BODIES)

        reports: list[Path] = []
        for body in bodies:
            launch = [
                *common, "-ElysiumCompose", f"-ComposeHz={hz}", f"-ComposeWeapon={weapon}",
                f"-ComposeBody={body}", f"-ElysiumMap={map_name}",
                "-UseFixedTimeStep", f"-FPS={hz}", "-nullrhi", "-unattended",
                "-nosplash", "-nosound", "-stdout", "-FullStdOutLogOutput",
            ]
            launch.extend(v for v in values[2:] if not v.startswith("-ElysiumMap="))
            if exec_cmds:
                launch.append("-ExecCmds=" + ";".join(exec_cmds))
            _run(config, runner, editor, launch)
            reports.append(config.export_root / "_compose"
                           / f"{map_name}-{weapon}-{body}.json")

        # Recording without judging is the failure mode every other harness here already fixed:
        # the comparator runs chained, and its verdict is this command's exit code. Both runs go to
        # ONE comparator call so the closing summary can put the two bodies' arm scalars beside each
        # other -- two calls would print two verdicts and no comparison.
        diff = ["-m", "elysium_pipeline.validation.compose_diff"]
        for report in reports:
            diff.extend(["--run", os.fspath(report)])
        _run(config, runner, os.fspath(Path(os.sys.executable)), diff)
        return None
    if kind == "cast":
        course = values[0] if values else ""
        hz = values[1] if len(values) > 1 else "60"
        if course.isdigit():
            hz, course = course, ""

        # The other producer of the same trace, on one of two hosts. By default the arena it records
        # over is geometry the run stands itself in the stage world, the way the gym is — what a
        # course measures is the cast body and the resolver, not a level. `--sited` is the other
        # question: the priority map's own cast, walking the map's own authored route, which is the
        # only host an acceptance claim can be made on.
        sited_map = "sm_hub_1" if sited_only else ""
        launch = [
            *common, "-ElysiumCast", f"-CastHz={hz}",
            "-UseFixedTimeStep", f"-FPS={hz}", "-nullrhi", "-unattended",
            "-nosplash", "-nosound", "-stdout", "-FullStdOutLogOutput",
        ]
        if sited_map:
            if not (config.export_root / sited_map).is_dir():
                raise UnrealFailure(
                    f"{sited_map} is not exported; the sited cast courses walk its own route")
            launch.append(f"-ElysiumMap={sited_map}")
        if course:
            launch.append(f"-CastCourse={course}")
        # Anything past the course and the rate reaches the editor verbatim, which is how
        # `-CastBody=<stem>` picks the body the arena stands.
        launch.extend(values[2:])
        if exec_cmds:
            launch.append("-ExecCmds=" + ";".join(exec_cmds))
        _run(config, runner, editor, launch)

        # Its own run directory, and therefore its own baseline root. The comparator fails a stem
        # that a baseline carries and a run does not, so pointing both at `_move` would make a
        # player-only run report every cast course as missing — and the two cast hosts are apart
        # for the same reason.
        cast_root = config.export_root / "_cast"
        if sited_map:
            cast_root = cast_root / sited_map
        diff = [
            "-m", "elysium_pipeline.validation.channel_diff",
            "--out", os.fspath(cast_root),
            "--gym-baseline", os.fspath(cast_root / "baseline"),
        ]
        if promote:
            diff.append("--promote")
        _run(config, runner, os.fspath(Path(os.sys.executable)), diff)
        return None
    if kind == "gr":
        # The interactive green room. It shares the harness's stage and body factory and nothing
        # else: no offscreen rendering, no `-unattended`, no capture, no contact sheet, and no exit
        # -- the window is the point, so the process lives until it is closed.
        # No map by default: the lab boots into its own stage world, an empty level with the stage
        # built into it and nothing else loaded. Naming one still loads it, which is how a body can
        # be auditioned against a real map's environment.
        stem = values[0] if values else ""
        clip = values[1] if len(values) > 1 else ""
        map_name = values[2] if len(values) > 2 else ""
        launch = [
            *common, "-dx12", "-ForceRes", "-windowed", "-ResX=1920", "-ResY=1080",
            "-ElysiumGreenRoom", "-GreenRoomLab",
            "-nosplash", "-nopause", "-stdout", "-FullStdOutLogOutput",
        ]
        # `--drive` stands the named body on the PAWN, on the generated movement gym, driven by real
        # input and framed by the shipping camera. Without it the lab stands a clip-review body on
        # the stage, which is what the animation programme verifies on.
        #
        # `--arena` is the same body on a different floor: a clean square room with one cover solid,
        # interesting-place anchors and a BUILT navmesh, which is what a spawned cast needs in order
        # to path at all. It wins over `--drive` if both are given, being the larger request.
        if options.arena:
            launch.append("-GreenRoomArena")
        elif options.drive:
            launch.append("-GreenRoomDrive")
        if map_name:
            launch.append(f"-ElysiumMap={map_name}")
        # An empty `-Switch=` makes Unreal's parser swallow the NEXT token as the value, so a
        # stem-less launch must omit the pair rather than pass it blank.
        if stem:
            launch.append(f"-GreenRoomStem={stem}")
        if clip:
            launch.append(f"-GreenRoomClip={clip}")
        if exec_cmds:
            launch.append("-ExecCmds=" + ";".join(exec_cmds))
        _run(config, runner, editor, launch)
        return None
    if kind in {"greenroom", "modelroom"}:
        if kind == "greenroom":
            case = values[0] if values else "player"
            map_name = values[1] if len(values) > 1 else (
                "sp_theatre" if case in {"embrace", "props"} else "sp_tutorial_1"
            )
            review_args = [f"-GreenRoomCase={case}", "-GreenRoomSettle=15"]
        else:
            if len(values) < 2:
                raise ValueError(
                    "modelroom requires <mesh-stem> <clip> [anim-set bone-root] [map]"
                )
            stem, clip = values[:2]
            anim_set = bone_root = ""
            map_name = "sp_tutorial_1"
            if len(values) > 2 and values[2].startswith("models/"):
                if len(values) < 4:
                    raise ValueError("a cinematic anim-set requires a bone root")
                anim_set, bone_root = values[2:4]
                if len(values) > 4:
                    map_name = values[4]
            elif len(values) > 2:
                map_name = values[2]
            # An empty `-Switch=` is not an empty value to Unreal's parser -- it takes the NEXT
            # token as the value, so passing the pair unconditionally made a clip-vocabulary
            # review read its anim set as "-GreenRoomBoneRoot=" and take the cinematic path.
            # Omit both unless a cinematic set was actually named.
            review_args = [
                "-GreenRoomCase=review", f"-GreenRoomStem={stem}", f"-GreenRoomClip={clip}",
            ]
            if anim_set:
                review_args += [f"-GreenRoomAnimSet={anim_set}",
                                f"-GreenRoomBoneRoot={bone_root}"]
            review_args.append("-GreenRoomSettle=8")
        # `--live` opens a window and stays in it: the harness's own stills answer "is it there
        # and does it hold together", but anything about timing -- a garment settling, a blend
        # easing -- only reads in motion, and offscreen rendering cannot show that.
        launch = [*common, "-dx12"]
        if not live:
            launch.append("-RenderOffScreen")
        launch += [
            "-ForceRes", "-windowed", "-ResX=1920", "-ResY=1080",
            f"-ElysiumMap={map_name}", "-ElysiumGreenRoom", *review_args,
            "-nosplash", "-nopause", "-stdout", "-FullStdOutLogOutput",
        ]
        if not live:
            launch.append("-unattended")
        else:
            launch.append("-GreenRoomLive")
        if exec_cmds:
            launch.append("-ExecCmds=" + ";".join(exec_cmds))
        _run(config, runner, editor, launch)
        if kind == "modelroom" and not live:
            review = config.export_root / "_greenroom" / "review"
            _run(
                config,
                runner,
                os.fspath(Path(os.sys.executable)),
                ["-m", "elysium_pipeline.validation.greenroom_contact_sheet", str(review)],
            )
            return review / "review_sheet.png"
        return None
    raise ValueError(f"unknown debug harness: {kind}")
