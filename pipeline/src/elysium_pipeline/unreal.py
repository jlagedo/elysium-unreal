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
DEFAULT_BAKE_STAGES = "textures,materials,world,sky,props,level"


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
                    bake_maps(config, runner, [name], stages=stages, batch_size=1)
                except UnrealFailure:
                    failures.append(name)
            raise UnrealFailure(
                "bake batch failed; isolated failing map(s): " + ", ".join(failures or batch)
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


def run_harness(config, runner, kind: str, args: Sequence[str]) -> Path | None:
    values = list(args)
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
            review_args = [
                "-GreenRoomCase=review", f"-GreenRoomStem={stem}", f"-GreenRoomClip={clip}",
                f"-GreenRoomAnimSet={anim_set}", f"-GreenRoomBoneRoot={bone_root}",
                "-GreenRoomSettle=8",
            ]
        _run(
            config,
            runner,
            editor,
            [
                *common, "-dx12", "-RenderOffScreen", "-ForceRes", "-windowed",
                "-ResX=1920", "-ResY=1080", f"-ElysiumMap={map_name}", "-ElysiumGreenRoom",
                *review_args, "-unattended", "-nosplash", "-nopause", "-stdout",
                "-FullStdOutLogOutput",
            ],
        )
        if kind == "modelroom":
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
