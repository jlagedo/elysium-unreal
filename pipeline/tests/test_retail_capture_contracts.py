from __future__ import annotations

from collections import Counter
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

REPO_ROOT = Path(__file__).resolve().parents[2]

from research.tooling.capture.capture_contracts import (
    ContractError,
    DEFAULT_EXPERIMENT,
    create_session_manifest,
    load_experiment,
    load_numerical_policy,
    validate_session_manifest,
)
from research.tooling.capture.generated_record_schemas import (
    ANIMATION_FILE_HEADER,
    ANIMATION_RECORD_HEADER,
    POSE_FILE_HEADER,
    POSE_RECORD_HEADER,
    RECORDS,
)
from research.tooling.capture.legacy_capture import (
    LegacyCaptureReader,
    validate_legacy_golden_metadata,
)


GENERATOR = (
    REPO_ROOT
    / "research"
    / "tooling"
    / "capture"
    / "contracts"
    / "generate_record_schemas.py"
)


def _write_pose_capture(path: Path) -> None:
    header = POSE_FILE_HEADER.pack(
        b"ELPOSE2\0",
        2,
        POSE_FILE_HEADER.size,
        10_000,
        100,
        123,
        0x1000,
        0x2000,
        0x3000,
        0x4F00,
        b"a" * 64,
        b"",
    )
    payload = bytes(12 * 4 * 2)
    record = POSE_RECORD_HEADER.pack(
        b"POSE",
        POSE_RECORD_HEADER.size + len(payload),
        1,
        150,
        7,
        0x4000,
        0x5000,
        0x12345678,
        1,
        0x6000,
        1,
        2,
        3,
        4,
        5,
        b"synthetic.mdl",
    )
    path.write_bytes(header + record + payload)


def _write_animation_capture(path: Path) -> None:
    header = ANIMATION_FILE_HEADER.pack(
        b"ELANIM2\0",
        2,
        ANIMATION_FILE_HEADER.size,
        10_000,
        100,
        123,
        0x7000,
        0x968A0,
        0x8FD00,
        0x12345678,
        b"b" * 64,
        b"",
    )
    payload = bytes(3 * 4 + 4 * 4 + 4)
    rows = []
    for ordinal, magic in enumerate((b"BASE", b"FINL"), start=1):
        rows.append(
            ANIMATION_RECORD_HEADER.pack(
                magic,
                ANIMATION_RECORD_HEADER.size + len(payload),
                ordinal,
                200 + ordinal,
                7,
                0x5000,
                0x4000,
                0x12345678,
                1,
                3,
                0.25,
                0.5,
                1,
                0x8000,
                0x9000,
            )
            + payload
        )
    path.write_bytes(header + b"".join(rows))


def _direct_pose_summary(path: Path) -> dict[str, object]:
    with path.open("rb") as stream:
        header = POSE_FILE_HEADER.unpack(stream.read(POSE_FILE_HEADER.size))
        records = 0
        models: Counter[str] = Counter()
        while raw := stream.read(POSE_RECORD_HEADER.size):
            values = POSE_RECORD_HEADER.unpack(raw)
            payload_bytes = values[1] - POSE_RECORD_HEADER.size
            stream.read(payload_bytes)
            records += 1
            models[values[-1].split(b"\0", 1)[0].decode("ascii")] += 1
    return {
        "format": header[0].rstrip(b"\0").decode("ascii"),
        "record_count": records,
        "models": dict(models),
    }


def _direct_animation_summary(path: Path) -> dict[str, object]:
    with path.open("rb") as stream:
        header = ANIMATION_FILE_HEADER.unpack(
            stream.read(ANIMATION_FILE_HEADER.size)
        )
        stages: Counter[str] = Counter()
        while raw := stream.read(ANIMATION_RECORD_HEADER.size):
            values = ANIMATION_RECORD_HEADER.unpack(raw)
            payload_bytes = values[1] - ANIMATION_RECORD_HEADER.size
            stream.read(payload_bytes)
            stages[values[0].decode("ascii")] += 1
    return {
        "format": header[0].rstrip(b"\0").decode("ascii"),
        "record_count": sum(stages.values()),
        "stages": dict(stages),
    }


class RetailCaptureContractTests(unittest.TestCase):
    def test_generated_record_contracts_are_current_and_unique(self) -> None:
        subprocess.run(
            [sys.executable, str(GENERATOR), "--check"],
            cwd=REPO_ROOT,
            check=True,
        )
        ids = [record["id"] for record in RECORDS.values()]
        self.assertEqual(len(ids), len(set(ids)))
        for record in RECORDS.values():
            self.assertEqual(record["struct"].size, record["minimum_bytes"])
            self.assertLessEqual(record["minimum_bytes"], record["maximum_bytes"])
        capture_root = REPO_ROOT / "research" / "tooling" / "capture"
        generated_python = capture_root / "generated_record_schemas.py"
        generated_header = capture_root / "generated_record_schemas.h"
        handwritten = [
            path
            for path in capture_root.glob("*")
            if path.suffix in {".py", ".cpp", ".h"}
            and path not in {generated_python, generated_header}
        ]
        for path in handwritten:
            source = path.read_text(encoding="utf-8")
            self.assertNotIn("<8sIIQqIIIII65s11s", source, path)
            self.assertNotIn("<4sIQqIIIIII5I64s", source, path)
            self.assertNotIn("<4sIQqIIIIIiffi2I", source, path)
            self.assertNotIn("struct PoseRecordHeader {", source, path)
            self.assertNotIn("struct AnimationRecordHeader {", source, path)

    def test_experiment_runner_rejects_an_underspecified_document(self) -> None:
        with tempfile.TemporaryDirectory(dir=REPO_ROOT) as temporary:
            invalid = Path(temporary) / "invalid-experiment.json"
            invalid.write_text(
                json.dumps(
                    {
                        "specification_version": 1,
                        "id": "missing-contract",
                        "question": "What happens?",
                    }
                ),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(
                ContractError, "controlled_variables"
            ):
                load_experiment(invalid)
        for runner in ("capture_live_pose.py", "capture_live_scene.py"):
            source = (
                REPO_ROOT / "research" / "tooling" / "capture" / runner
            ).read_text(encoding="utf-8")
            self.assertIn("load_experiment(args.experiment)", source)
        self.assertEqual(load_experiment(DEFAULT_EXPERIMENT)["id"], "retained-baseline")

    def test_session_manifest_and_numerical_policy_validate(self) -> None:
        policy = load_numerical_policy()
        self.assertEqual(len(policy["domains"]), 6)
        with tempfile.TemporaryDirectory() as temporary:
            session = Path(temporary) / "session"
            session.mkdir()
            manifest = create_session_manifest(
                session=session,
                capture_command=["capture", "start"],
                pid=123,
                launch={
                    "command": ["vampire.exe", "-console"],
                    "working_directory": r"C:\Games\Vampire",
                },
                environment={"VPROJECT": r"C:\Games\Vampire\vampire"},
                distribution="user-owned-retail",
                patch="Unofficial Patch 11.4",
                modules={
                    "vampire.exe": {
                        "path": r"C:\Games\Vampire\vampire.exe",
                        "sha256": "a" * 64,
                    }
                },
                probe_profile="retained-baseline-v1",
                map_name="sp_theatre",
                experiment_path=DEFAULT_EXPERIMENT,
                clock_controls={"fps_max": "30"},
                analyzers=[{"name": "analyzer.py", "sha256": "b" * 64}],
                capture={"method": "synthetic"},
            )
            self.assertEqual(
                validate_session_manifest(manifest)["state"], "capturing"
            )
            del manifest["records"]["dropped"]
            with self.assertRaisesRegex(ContractError, "records.dropped"):
                validate_session_manifest(manifest)
        capture_root = REPO_ROOT / "research" / "tooling" / "capture"
        pose_source = (capture_root / "capture_live_pose.py").read_text(
            encoding="utf-8"
        )
        scene_source = (capture_root / "capture_live_scene.py").read_text(
            encoding="utf-8"
        )
        for source in (pose_source, scene_source):
            self.assertIn("create_session_manifest(", source)
            self.assertIn("write_session_manifest(", source)
        self.assertIn("validate_session_manifest(", scene_source)

    def test_legacy_adapters_preserve_current_summaries(self) -> None:
        validate_legacy_golden_metadata()
        with tempfile.TemporaryDirectory() as temporary:
            session = Path(temporary)
            pose_path = session / "scene.elpose"
            animation_path = session / "animation.elanim"
            _write_pose_capture(pose_path)
            _write_animation_capture(animation_path)
            (session / "done.txt").write_text(
                "complete=1\nqueued=3\nwritten=3\ndropped=0\n",
                encoding="ascii",
            )

            with LegacyCaptureReader(pose_path) as reader:
                pose_records = list(reader)
                self.assertEqual(reader.header.format, "ELPOSE2")
            with LegacyCaptureReader(animation_path) as reader:
                animation_records = list(reader)
                self.assertEqual(reader.header.format, "ELANIM2")

            pose_summary = _direct_pose_summary(pose_path)
            animation_summary = _direct_animation_summary(animation_path)
            environment = os.environ.copy()
            environment.update(
                {
                    "ELYSIUM_VTMB_ROOT": str(REPO_ROOT),
                    "ELYSIUM_WORK_ROOT": temporary,
                }
            )
            analyzer = subprocess.run(
                [
                    sys.executable,
                    "-c",
                    (
                        "import json; from pathlib import Path; "
                        "from research.tooling.capture.capture_live_scene "
                        "import summarize, summarize_animation; "
                        f"p=Path({str(session)!r}); "
                        "print(json.dumps({'pose': summarize(p), "
                        "'animation': summarize_animation(p)}))"
                    ),
                ],
                cwd=REPO_ROOT,
                env=environment,
                check=True,
                capture_output=True,
                text=True,
            )
            current = json.loads(analyzer.stdout)
            self.assertEqual(pose_summary["format"], "ELPOSE2")
            self.assertEqual(pose_summary["record_count"], len(pose_records))
            self.assertEqual(pose_summary["models"], {"synthetic.mdl": 1})
            self.assertEqual(
                current["pose"]["record_count"], len(pose_records)
            )
            self.assertEqual(
                current["pose"]["models"][0]["draw_records"],
                len(pose_records),
            )
            self.assertEqual(animation_summary["format"], "ELANIM2")
            self.assertEqual(
                animation_summary["record_count"], len(animation_records)
            )
            self.assertEqual(
                animation_summary["stages"],
                dict(Counter(record.magic for record in animation_records)),
            )
            self.assertEqual(
                current["animation"]["record_count"], len(animation_records)
            )
            self.assertEqual(
                current["animation"]["stages"],
                dict(Counter(record.magic for record in animation_records)),
            )


if __name__ == "__main__":
    unittest.main()
