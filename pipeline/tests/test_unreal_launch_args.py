"""Editor launch arguments: headless flags ride every unattended editor boot, retention is
bounded to a tail, and interactive launches keep Live Coding and sound."""
from __future__ import annotations

from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

from elysium_pipeline import unreal


class RecordingRunner:
    def __init__(self):
        self.calls: list[tuple[list[str], int | None]] = []

    def run(self, argv, *, cwd=None, tail_lines=None, **_kwargs):
        self.calls.append(([str(value) for value in argv], tail_lines))
        return SimpleNamespace(returncode=0)


class LaunchArgumentTests(unittest.TestCase):
    def _config(self, temporary: str) -> SimpleNamespace:
        root = Path(temporary)
        return SimpleNamespace(
            repo_root=root,
            project=root / "ElysiumUE.uproject",
            export_root=root / "exports",
        )

    def test_an_already_present_flag_is_not_duplicated(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = self._config(temporary)
            runner = RecordingRunner()
            with mock.patch.object(unreal, "editor_executable",
                                   return_value=Path("UnrealEditor-Cmd.exe")):
                unreal._run(config, runner, Path("UnrealEditor-Cmd.exe"),
                            ["-unattended", "-nosound"])
            (argv, _tail), = runner.calls
            assert argv.count("-nosound") == 1
            assert "-NoLiveCoding" in argv

    def test_interactive_editor_keeps_live_coding_and_sound(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = self._config(temporary)
            runner = RecordingRunner()
            with mock.patch.object(unreal, "editor_executable",
                                   return_value=Path("UnrealEditor.exe")):
                unreal.run_editor(config, runner)
            (argv, tail_lines), = runner.calls
            for flag in ("-NoLiveCoding", "-noP4", "-nosound"):
                assert flag not in argv
            assert tail_lines == unreal.EDITOR_TAIL_LINES

    def test_a_non_editor_executable_is_untouched(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = self._config(temporary)
            runner = RecordingRunner()
            unreal._run(config, runner, Path("Build.bat"), ["-unattended"])
            (argv, tail_lines), = runner.calls
            assert argv[1:] == ["-unattended"]
            assert tail_lines is None

    def test_auxiliary_policy_content_is_its_own_pair_of_launches(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = self._config(temporary)
            runner = RecordingRunner()
            graph = config.repo_root / "Content" / "ElysiumGenerated" / "Animation"
            graph.mkdir(parents=True)
            (graph / "ABP_ElysiumBiped.uasset").write_bytes(b"graph")
            fonts = config.repo_root / "Content" / "ElysiumGenerated" / "UI" / "Fonts"
            fonts.mkdir(parents=True)
            for name in unreal.FONT_ASSETS:
                (fonts / name).write_bytes(b"font")
            with mock.patch.object(
                unreal, "editor_executable",
                side_effect=lambda _config, commandlet=False: Path(
                    "UnrealEditor-Cmd.exe" if commandlet else "UnrealEditor.exe"),
            ):
                unreal.generate_auxiliary_policy_content(config, runner)
            assert len(runner.calls) == 2
            font_argv, _ = runner.calls[0]
            assert any(
                value.startswith("-ExecutePythonScript=") for value in font_argv)
            graph_argv, _ = runner.calls[1]
            assert any(
                "make_player_anim_bp.py" in value for value in graph_argv)
