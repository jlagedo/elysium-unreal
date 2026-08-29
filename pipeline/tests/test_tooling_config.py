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

            assert resolved.game_root == roots["argument", "game"].resolve()
            assert resolved.work_root == roots["argument", "work"].resolve()
            assert resolved.ue_root == roots["argument", "ue"].resolve()
            assert resolved.export_root == (roots["argument", "work"] / "exports").resolve()

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

            assert resolved.game_root == env_game.resolve()
            assert resolved.work_root == env_work.resolve()
            assert resolved.ue_root == env_ue.resolve()
            assert resolved.export_root == export.resolve()

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

            assert resolved.game_root == game.resolve()
            assert resolved.work_root == work.resolve()
            assert resolved.ue_root == ue.resolve()
            assert resolved.unreal_zen_data_path == zen.resolve()
            assert resolved.unreal_local_data_cache_path == local_ddc.resolve()
            assert resolved.unreal_shader_work_root == shader_work.resolve()
            assert resolved.temp_root == process_temp.resolve()

            with mock.patch.dict(os.environ, {}, clear=True):
                resolved.apply_environment()
                assert os.environ["UE-ZenDataPath"] == str(zen.resolve())
                assert os.environ["UE-LocalDataCachePath"] == str(local_ddc.resolve())
                assert os.environ["TEMP"] == str(process_temp.resolve())
                assert os.environ["TMP"] == str(process_temp.resolve())
