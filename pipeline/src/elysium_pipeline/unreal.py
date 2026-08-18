"""UnrealBuildTool, editor, content, bake, test, and development harness driver."""

from __future__ import annotations

from collections.abc import Sequence
from dataclasses import dataclass
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re


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
DEFAULT_BAKE_STAGES = "textures,materials,world,sky,particles,level"
TEST_ABSTENTION_TOKEN = "ELYSIUM_TEST_ABSTAIN"


class UnrealFailure(RuntimeError):
    pass


def _run(config, runner, executable: Path | str, args: Sequence[str]) -> None:
    arguments = list(map(str, args))
    executable_name = Path(executable).name.casefold()
    shader_work_root = getattr(config, "unreal_shader_work_root", None)
    if (
        executable_name in {"unrealeditor.exe", "unrealeditor-cmd.exe"}
        and shader_work_root is not None
        and not any(
            argument.casefold().startswith("-shaderworkingdir=")
            for argument in arguments
        )
    ):
        arguments.append(f"-shaderworkingdir={shader_work_root}")
    result = runner.run([str(executable), *arguments], cwd=config.repo_root)
    if result.returncode:
        raise UnrealFailure(f"{executable} exited with {result.returncode}")


def editor_executable(config, *, commandlet: bool = False) -> Path:
    leaf = "UnrealEditor-Cmd.exe" if commandlet else "UnrealEditor.exe"
    path = config.ue_root / "Engine" / "Binaries" / "Win64" / leaf
    if not path.is_file():
        raise UnrealFailure(f"Unreal editor executable not found: {path}")
    return path


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
    if mode == "analyze":
        arguments.append("-StaticAnalyzer=Default")
    arguments.extend(extra)
    _run(config, runner, config.ue_root / "Engine" / "Build" / "BatchFiles" / script, arguments)


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
    )
    if not include_auxiliary:
        return
    _run(
        config,
        runner,
        editor_executable(config),
        [
            str(config.project),
            f"-ExecutePythonScript={config.repo_root / 'pipeline/unreal/make_ui_fonts.py'}",
            *common,
        ],
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
    )
    graph_asset = config.repo_root / "Content" / "Elysium" / "Animation" / "ABP_ElysiumBiped.uasset"
    if not graph_asset.is_file():
        raise UnrealFailure("the player animation graph was not generated: " + str(graph_asset))

    font_root = config.repo_root / "Content" / "VtMB" / "UI" / "Fonts"
    missing = [name for name in FONT_ASSETS if not (font_root / name).is_file()]
    if missing:
        raise UnrealFailure("font generation did not produce: " + ", ".join(missing))


def bake_corpus(config, runner, *, asset_plan: Path | None = None) -> None:
    """Bake the shared asset corpus onto /ElysiumBaked/Shared.

    One scope, no map: a texture, a material and a static model belong to the install, so each is
    baked once rather than once per map that draws it. Only textures, materials and props apply --
    the corpus has no world, sky, particle or level input.

    `asset_plan` names the frozen per-asset inputs and policies, so the run rebuilds only the
    assets whose recipe changed. Without one the commandlet rebuilds the whole scope, which is the
    recovery surface for a hand-run bake.
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
            *([f"-BakeAssetPlan={asset_plan}"] if asset_plan is not None else []),
            "-AllowCommandletRendering",
            "-unattended",
            "-nosplash",
            "-nopause",
            "-stdout",
            "-FullStdOutLogOutput",
        ],
    )


def bake_maps(
    config,
    runner,
    maps: Sequence[str],
    *,
    stages: str = DEFAULT_BAKE_STAGES,
    asset_plan: Path | None = None,
    batch_size: int = 4,
) -> None:
    maps = list(dict.fromkeys(maps))
    for offset in range(0, len(maps), max(1, batch_size)):
        batch = maps[offset : offset + max(1, batch_size)]
        args = [
            str(config.project),
            "-run=pythonscript",
            f"-script={config.repo_root / 'pipeline/unreal/bake_map.py'}",
            f"-BakeMaps={','.join(batch)}",
            f"-BakeStages={stages}",
            *([f"-BakeAssetPlan={asset_plan}"] if asset_plan is not None else []),
            "-AllowCommandletRendering",
            "-unattended",
            "-nosplash",
            "-nopause",
            "-stdout",
            "-FullStdOutLogOutput",
        ]
        try:
            _run(config, runner, editor_executable(config, commandlet=True), args)
        except UnrealFailure:
            if len(batch) == 1:
                raise
            failures: list[str] = []
            for name in batch:
                try:
                    bake_maps(
                        config,
                        runner,
                        [name],
                        stages=stages,
                        asset_plan=asset_plan,
                        batch_size=1,
                    )
                except UnrealFailure:
                    failures.append(name)
            raise UnrealFailure(
                "bake batch failed; isolated failing map(s): " + ", ".join(failures or batch)
            )


def bake_characters(config, runner, stems: Sequence[str], *, props: Sequence[str] = (),
                    plan: Path | None = None) -> None:
    """Bake the named models onto /ElysiumBaked/Characters.

    The whole cast goes through one editor process, and `plan` names which scopes and stages of it
    still have to be authored -- everything else on the mount is already current and is left alone.
    Without a plan every scope is rebuilt, which is the recovery surface for a hand-run bake.

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
    if plan is not None:
        arguments.insert(4, f"-BakeCharacterPlan={plan}")
    _run(config, runner, editor_executable(config, commandlet=True), arguments)


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
    _run(config, runner, editor_executable(config, commandlet=True), arguments)


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
        )


def run_tests(config, runner, filter_name: str = "Elysium.", *,
              parity_stems: Sequence[str] = ()) -> dict:
    """Run an automation selection and report what actually executed.

    `parity_stems` widens the per-model parity slice, which is otherwise two hard-coded bodies.
    It is the only test in the suite built for slice iteration, and it was previously reachable
    only by invoking the commandlet by hand.
    """
    aliases = {"substrate": "Elysium.Substrate.", "content": "Elysium.Content."}
    selected = aliases.get(filter_name.lower(), filter_name)
    if config.work_root is None:
        raise UnrealFailure("automation needs ELYSIUM_WORK_ROOT for its retained report")
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    slug = re.sub(r"[^a-z0-9]+", "-", selected.lower()).strip("-") or "all"
    report = config.work_root / "reports" / "tests" / f"{stamp}-{slug}"
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
    _run(config, runner, editor_executable(config, commandlet=True), arguments)
    summary = summarize_test_report(report)
    summary["report_path"] = str(report.resolve())
    return summary


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


def _take_options(values: list[str]) -> tuple[list[str], HarnessOptions]:
    """Returns `(positional, options)`.

    Parsed before any harness branch reads a positional, because a switch left in the positional
    list becomes an argument: `--gym` would arrive at the movement harness as `-MoveCourse=--gym`.
    """
    positional: list[str] = []
    exec_cmds: list[str] = []
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
    if kind == "cast":
        course = values[0] if values else ""
        hz = values[1] if len(values) > 1 else "60"
        if course.isdigit():
            hz, course = course, ""

        # The other producer of the same trace. No map and no host switch: the arena it records over
        # is geometry the run stands itself in the stage world, the way the gym is — what a course
        # measures is the cast body and the resolver, not a level.
        launch = [
            *common, "-ElysiumCast", f"-CastHz={hz}",
            "-UseFixedTimeStep", f"-FPS={hz}", "-nullrhi", "-unattended",
            "-nosplash", "-nosound", "-stdout", "-FullStdOutLogOutput",
        ]
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
        # player-only run report every cast course as missing.
        cast_root = config.export_root / "_cast"
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
