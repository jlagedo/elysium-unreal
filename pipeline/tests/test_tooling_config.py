from __future__ import annotations

import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from elysium_pipeline import config


_PATH_KEYS = {
    "ELYSIUM_UE_ROOT",
    "ELYSIUM_VTMB_ROOT",
    "ELYSIUM_WORK_ROOT",
    "ELYSIUM_EXPORT_ROOT",
    "ELYSIUM_UNREAL_SHADER_WORK_ROOT",
    "ELYSIUM_TEMP_ROOT",
    "UE-ZenDataPath",
    "UE-LocalDataCachePath",
    "ProgramData",
}
_PATH_KEYS_CASEFOLDED = {key.casefold() for key in _PATH_KEYS}


class ProjectConfigPrecedenceTests(unittest.TestCase):
    def _resolve(
        self,
        repo: Path,
        environment: dict[str, str],
        *,
        game: Path | None = None,
        work: Path | None = None,
        ue: Path | None = None,
    ) -> config.ProjectConfig:
        clean_environment = {
            key: value
            for key, value in os.environ.items()
            if key.casefold() not in _PATH_KEYS_CASEFOLDED
        }
        clean_environment.update(environment)
        with (
            mock.patch.object(config, "_repository_root", return_value=repo),
            mock.patch.dict(os.environ, clean_environment, clear=True),
        ):
            return config.ProjectConfig.resolve(
                game,
                work,
                ue,
                require_game=True,
                require_work=True,
                require_ue=True,
            )

    def test_cli_paths_override_environment_and_local_file(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo = root / "repo"
            repo.mkdir()
            roots = {}
            for source in ("local", "environment", "argument"):
                for kind in ("game", "work", "ue"):
                    path = root / source / kind
                    path.mkdir(parents=True)
                    roots[source, kind] = path
            (repo / ".elysium.local.env").write_text(
                "\n".join(
                    (
                        f"ELYSIUM_VTMB_ROOT={roots['local', 'game']}",
                        f"ELYSIUM_WORK_ROOT={roots['local', 'work']}",
                        f"ELYSIUM_UE_ROOT={roots['local', 'ue']}",
                    )
                ),
                encoding="utf-8",
            )

            resolved = self._resolve(
                repo,
                {
                    "ELYSIUM_VTMB_ROOT": str(roots["environment", "game"]),
                    "ELYSIUM_WORK_ROOT": str(roots["environment", "work"]),
                    "ELYSIUM_UE_ROOT": str(roots["environment", "ue"]),
                },
                game=roots["argument", "game"],
                work=roots["argument", "work"],
                ue=roots["argument", "ue"],
            )

            self.assertEqual(resolved.game_root, roots["argument", "game"].resolve())
            self.assertEqual(resolved.work_root, roots["argument", "work"].resolve())
            self.assertEqual(resolved.ue_root, roots["argument", "ue"].resolve())
            self.assertEqual(
                resolved.export_root,
                (roots["argument", "work"] / "exports").resolve(),
            )

    def test_environment_overrides_local_file_and_export_override_is_independent(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo = root / "repo"
            repo.mkdir()
            local_game = root / "local-game"
            local_work = root / "local-work"
            local_ue = root / "local-ue"
            env_game = root / "env-game"
            env_work = root / "env-work"
            env_ue = root / "env-ue"
            export = env_work / "custom-export"
            for path in (
                local_game,
                local_work,
                local_ue,
                env_game,
                env_work,
                env_ue,
                export,
            ):
                path.mkdir(parents=True)
            (repo / ".elysium.local.env").write_text(
                "\n".join(
                    (
                        f"ELYSIUM_VTMB_ROOT={local_game}",
                        f"ELYSIUM_WORK_ROOT={local_work}",
                        f"ELYSIUM_UE_ROOT={local_ue}",
                    )
                ),
                encoding="utf-8",
            )

            resolved = self._resolve(
                repo,
                {
                    "ELYSIUM_VTMB_ROOT": str(env_game),
                    "ELYSIUM_WORK_ROOT": str(env_work),
                    "ELYSIUM_UE_ROOT": str(env_ue),
                    "ELYSIUM_EXPORT_ROOT": str(export),
                },
            )

            self.assertEqual(resolved.game_root, env_game.resolve())
            self.assertEqual(resolved.work_root, env_work.resolve())
            self.assertEqual(resolved.ue_root, env_ue.resolve())
            self.assertEqual(resolved.export_root, export.resolve())

    def test_local_file_is_used_when_arguments_and_environment_are_absent(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo = root / "repo"
            repo.mkdir()
            game = root / "game"
            work = root / "work"
            ue = root / "ue"
            zen = root / "zen"
            local_ddc = root / "local-ddc"
            shader_work = root / "shader-work"
            process_temp = root / "temp"
            for path in (game, work, ue, zen, local_ddc, shader_work, process_temp):
                path.mkdir()
            (repo / ".elysium.local.env").write_text(
                "\n".join(
                    (
                        f'ELYSIUM_VTMB_ROOT="{game}"',
                        f"ELYSIUM_WORK_ROOT={work}",
                        f"ELYSIUM_UE_ROOT={ue}",
                        f"UE-ZenDataPath={zen}",
                        f"UE-LocalDataCachePath={local_ddc}",
                        f"ELYSIUM_UNREAL_SHADER_WORK_ROOT={shader_work}",
                        f"ELYSIUM_TEMP_ROOT={process_temp}",
                    )
                ),
                encoding="utf-8",
            )

            resolved = self._resolve(repo, {})

            self.assertEqual(resolved.game_root, game.resolve())
            self.assertEqual(resolved.work_root, work.resolve())
            self.assertEqual(resolved.ue_root, ue.resolve())
            self.assertEqual(resolved.unreal_zen_data_path, zen.resolve())
            self.assertEqual(
                resolved.unreal_local_data_cache_path, local_ddc.resolve()
            )
            self.assertEqual(
                resolved.unreal_shader_work_root, shader_work.resolve()
            )
            self.assertEqual(resolved.temp_root, process_temp.resolve())

            with mock.patch.dict(os.environ, {}, clear=True):
                resolved.apply_environment()
                self.assertEqual(os.environ["UE-ZenDataPath"], str(zen.resolve()))
                self.assertEqual(
                    os.environ["UE-LocalDataCachePath"], str(local_ddc.resolve())
                )
                self.assertEqual(os.environ["TEMP"], str(process_temp.resolve()))
                self.assertEqual(os.environ["TMP"], str(process_temp.resolve()))


class EngineRootTests(unittest.TestCase):
    def _engine(self, root: Path, name: str) -> Path:
        engine = root / name
        for relative in (
            Path("Engine/Build/BatchFiles/Build.bat"),
            Path("Engine/Binaries/Win64/UnrealEditor.exe"),
        ):
            path = engine / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("stub\n", encoding="utf-8")
        return engine

    def test_a_complete_engine_copy_resolves(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            engine = self._engine(root, "UE_5.8_agent1")
            self.assertEqual(config.validate_engine_root(engine), engine.resolve())

    def test_a_missing_or_incomplete_engine_copy_is_named(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with self.assertRaisesRegex(config.ConfigError, "does not exist"):
                config.validate_engine_root(root / "absent")

            partial = self._engine(root, "UE_5.8_partial")
            (partial / "Engine/Binaries/Win64/UnrealEditor.exe").unlink()
            with self.assertRaisesRegex(config.ConfigError, "UnrealEditor.exe"):
                config.validate_engine_root(partial)

    def test_an_installed_copy_without_its_rules_assembly_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            engine = self._engine(root, "UE_5.8_agent1")
            installed = engine / "Engine/Build/InstalledBuild.txt"
            installed.write_text("", encoding="utf-8")

            # Copying an installed engine without Engine/Intermediate leaves UnrealBuildTool
            # with no rules assembly, and it refuses to regenerate one.
            with self.assertRaisesRegex(config.ConfigError, "precompiled rules assembly"):
                config.validate_engine_root(engine)

            rules = engine / "Engine/Intermediate/Build/BuildRules/UE5Rules.dll"
            rules.parent.mkdir(parents=True, exist_ok=True)
            rules.write_text("stub\n", encoding="utf-8")
            self.assertEqual(config.validate_engine_root(engine), engine.resolve())


class BuildParallelismTests(unittest.TestCase):
    def test_the_default_is_one_share_of_the_machine(self) -> None:
        with mock.patch.object(config.os, "cpu_count", return_value=16):
            self.assertEqual(config.default_build_parallelism(3), 5)
            self.assertEqual(config.default_build_parallelism(1), 16)
        with mock.patch.object(config.os, "cpu_count", return_value=4):
            self.assertEqual(config.default_build_parallelism(8), 2)

    def test_the_cap_lands_in_project_scope_where_ubt_reads_it_last(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            checkout = Path(temporary)
            written = config.write_build_parallelism(checkout, 5)
            self.assertEqual(written, checkout / config.UNREAL_BUILD_CONFIG)
            body = written.read_text(encoding="utf-8")
            self.assertIn("<MaxParallelActions>5</MaxParallelActions>", body)
            self.assertIn(
                'xmlns="https://www.unrealengine.com/BuildConfiguration"', body
            )
            with self.assertRaises(config.ConfigError):
                config.write_build_parallelism(checkout, 0)


if __name__ == "__main__":
    unittest.main()
