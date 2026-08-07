"""UnrealBuildTool, editor, content, bake, test, and development harness driver."""

from __future__ import annotations

from collections.abc import Sequence
import os
from pathlib import Path


FONT_ASSETS = (
    "FF_SpectralSC_Regular.uasset",
    "FF_SpectralSC_SemiBold.uasset",
    "FF_Spectral_Regular.uasset",
    "FF_Spectral_Italic.uasset",
    "FF_Spectral_SemiBold.uasset",
    "FF_Inter_Regular.uasset",
    "FF_Inter_SemiBold.uasset",
)
DEFAULT_BAKE_STAGES = "textures,materials,world,sky,props,particles,level"


class UnrealFailure(RuntimeError):
    pass


def _run(config, runner, executable: Path | str, args: Sequence[str]) -> None:
    result = runner.run([str(executable), *map(str, args)], cwd=config.repo_root)
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


def generate_policy_content(config, runner) -> None:
    common = ["-unattended", "-nosplash", "-nopause", "-stdout"]
    _run(
        config,
        runner,
        editor_executable(config, commandlet=True),
        [
            str(config.project),
            "-run=pythonscript",
            f"-script={config.repo_root / 'pipeline/unreal/build_content.py'}",
            *common,
        ],
    )
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
    font_root = config.repo_root / "Content" / "VtMB" / "UI" / "Fonts"
    missing = [name for name in FONT_ASSETS if not (font_root / name).is_file()]
    if missing:
        raise UnrealFailure("font generation did not produce: " + ", ".join(missing))


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


def bake_characters(config, runner, stems: Sequence[str]) -> None:
    """Bake the named models onto /ElysiumBaked/Characters.

    The whole cast goes through one editor process. Animation compression reports no memory estimate
    of its own, so the engine's throttler never engages against it -- the cap below is what keeps a
    long queue of sequences from exhausting the address space, and the bank pass is what keeps the
    queue short.
    """
    _run(
        config,
        runner,
        editor_executable(config, commandlet=True),
        [
            str(config.project),
            "-run=pythonscript",
            f"-script={config.repo_root / 'pipeline/unreal/bake_characters.py'}",
            f"-BakeCharacters={','.join(dict.fromkeys(stems))}",
            # AnimationCompression estimates its own cost as 0 MB, so the async-compilation
            # throttler lets an unbounded number of jobs run and the process dies of a failed
            # allocation rather than of anything wrong with the model it was on.
            "-ini:Engine:[ConsoleVariables]:Editor.AsyncAssetCompilationMaxMemoryUsage=8",
            "-unattended",
            "-nosplash",
            "-nopause",
            "-stdout",
            "-FullStdOutLogOutput",
        ],
    )


def verify_characters(config, runner, stems: Sequence[str]) -> None:
    _run(
        config,
        runner,
        editor_executable(config, commandlet=True),
        [
            str(config.project),
            "-run=pythonscript",
            f"-script={config.repo_root / 'pipeline/unreal/bake_verify_characters.py'}",
            f"-BakeCharacters={','.join(dict.fromkeys(stems))}",
            "-unattended",
            "-nosplash",
            "-nopause",
            "-stdout",
            "-FullStdOutLogOutput",
        ],
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


def run_tests(config, runner, filter_name: str = "Elysium.") -> None:
    aliases = {"substrate": "Elysium.Substrate.", "content": "Elysium.Content."}
    selected = aliases.get(filter_name.lower(), filter_name)
    report = config.export_root / "_tests"
    _run(
        config,
        runner,
        editor_executable(config, commandlet=True),
        [
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
        ],
    )


def common_game_args(config) -> list[str]:
    return [str(config.project), "-game", f"-ElysiumContentRoot={config.export_root}"]


def run_editor(config, runner, extra: Sequence[str] = ()) -> None:
    _run(
        config,
        runner,
        editor_executable(config),
        [str(config.project), f"-ElysiumContentRoot={config.export_root}", *extra],
    )


def run_play(config, runner, map_name: str | None = None, extra: Sequence[str] = ()) -> None:
    args = [
        *common_game_args(config),
        "-dx12",
        "-windowed",
        "-resx=1600",
        "-resy=900",
        "-log",
        "-LogCmds=LogElysiumWorld Verbose, LogElysiumIO Verbose",
    ]
    if map_name:
        args.append(f"-ElysiumMap={map_name}")
    args.extend(extra)
    _run(config, runner, editor_executable(config), args)


def _take_options(values: list[str]) -> tuple[list[str], list[str], bool]:
    """Split harness-wide options out of the positional arguments.

    Two of these exist because the harnesses are otherwise only drivable by editing files:
    `--set` is the only way to put a console variable in front of a run, and `--live` is the
    only way to watch one instead of reading stills afterwards.

    Returns `(positional, exec_cmds, live)`.
    """
    positional: list[str] = []
    exec_cmds: list[str] = []
    live = False
    index = 0
    while index < len(values):
        value = values[index]
        if value in {"--set", "--cvar"}:
            if index + 1 >= len(values):
                raise ValueError(f"{value} needs a command, e.g. --set 'elysium.Cloth 0'")
            exec_cmds.append(values[index + 1])
            index += 2
            continue
        if value.startswith(("--set=", "--cvar=")):
            exec_cmds.append(value.split("=", 1)[1])
            index += 1
            continue
        if value == "--live":
            live = True
            index += 1
            continue
        positional.append(value)
        index += 1
    return positional, exec_cmds, live


def run_harness(config, runner, kind: str, args: Sequence[str]) -> Path | None:
    values, exec_cmds, live = _take_options(list(args))
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
        launch = [
            *common, "-ElysiumMove", "-ElysiumMap=sp_tutorial_1", f"-MoveHz={hz}",
            "-UseFixedTimeStep", f"-FPS={hz}", "-nullrhi", "-unattended",
            "-nosplash", "-nosound", "-stdout", "-FullStdOutLogOutput",
        ]
        if course:
            launch.append(f"-MoveCourse={course}")
        _run(config, runner, editor, launch)
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
