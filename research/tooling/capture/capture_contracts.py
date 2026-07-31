"""Versioned contracts shared by retail capture drivers and analyzers."""

from __future__ import annotations

from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
from typing import Iterable

import psutil


ROOT = Path(__file__).resolve().parent
REPO_ROOT = ROOT.parents[2]
CONTRACT_ROOT = ROOT / "contracts"
DEFAULT_EXPERIMENT = (
    REPO_ROOT
    / "research"
    / "cases"
    / "animation-pose"
    / "experiments"
    / "retained-baseline.json"
)
MANIFEST_CONTRACT = "elysium.retail-capture-session"
MANIFEST_VERSION = 1
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")


class ContractError(ValueError):
    """A tracked capture contract is missing or internally inconsistent."""


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(8 * 1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise ContractError(message)


def _nonempty_string(value: object, field: str) -> str:
    _require(isinstance(value, str) and bool(value.strip()), f"{field} is required")
    return value


def validate_experiment(document: dict[str, object]) -> dict[str, object]:
    _require(document.get("specification_version") == 1, "unsupported experiment specification_version")
    _nonempty_string(document.get("id"), "experiment.id")
    question = _nonempty_string(document.get("question"), "experiment.question")
    _require(question.rstrip().endswith("?"), "experiment.question must be falsifiable and end in '?'")
    controls = document.get("controlled_variables")
    _require(isinstance(controls, dict) and bool(controls), "experiment.controlled_variables must be non-empty")
    for name, value in controls.items():
        _nonempty_string(name, "experiment.controlled_variables key")
        _nonempty_string(value, f"experiment.controlled_variables.{name}")
    for field in ("required_probes", "expected_invariants", "artifacts"):
        values = document.get(field)
        _require(isinstance(values, list) and bool(values), f"experiment.{field} must be non-empty")
        for index, value in enumerate(values):
            _nonempty_string(value, f"experiment.{field}[{index}]")
    bands = document.get("acceptance_bands")
    _require(isinstance(bands, dict) and bool(bands), "experiment.acceptance_bands must be non-empty")
    for name, policy in bands.items():
        _nonempty_string(name, "experiment.acceptance_bands key")
        _nonempty_string(policy, f"experiment.acceptance_bands.{name}")
    return document


def load_experiment(path: Path) -> dict[str, object]:
    document = json.loads(path.resolve().read_text(encoding="utf-8"))
    _require(isinstance(document, dict), "experiment document must be an object")
    return validate_experiment(document)


def load_numerical_policy() -> dict[str, object]:
    path = CONTRACT_ROOT / "numerical_policy.json"
    document = json.loads(path.read_text(encoding="utf-8"))
    _require(document.get("policy_version") == 1, "unsupported numerical policy version")
    domains = document.get("domains")
    _require(isinstance(domains, dict), "numerical policy domains must be an object")
    required = {
        "local_translation_cm",
        "quaternion_angle_degrees",
        "model_world_translation_cm",
        "matrix_rms",
        "flex_weight",
        "final_vertex_cm",
    }
    _require(set(domains) == required, "numerical policy domain set is incomplete")
    for name, bands in domains.items():
        _require(isinstance(bands, dict), f"numerical policy {name} must be an object")
        warning = bands.get("absolute_warning")
        failure = bands.get("absolute_failure")
        _require(
            isinstance(warning, (int, float))
            and isinstance(failure, (int, float))
            and 0 <= warning < failure,
            f"numerical policy {name} bands must satisfy 0 <= warning < failure",
        )
    return document


def parse_assignments(values: Iterable[str], field: str) -> dict[str, str]:
    result: dict[str, str] = {}
    for assignment in values:
        if "=" not in assignment:
            raise ContractError(f"{field} value must be NAME=VALUE: {assignment!r}")
        name, value = assignment.split("=", 1)
        _nonempty_string(name, f"{field} name")
        _nonempty_string(value, f"{field}.{name}")
        if name in result:
            raise ContractError(f"{field} repeats {name!r}")
        result[name] = value
    return result


def process_launch_context(pid: int) -> dict[str, object]:
    process = psutil.Process(pid)
    return {
        "command": process.cmdline(),
        "working_directory": process.cwd(),
    }


def analyzer_versions(paths: Iterable[Path]) -> list[dict[str, str]]:
    return [
        {
            "name": path.name,
            "sha256": file_sha256(path.resolve()),
        }
        for path in paths
    ]


def create_session_manifest(
    *,
    session: Path,
    capture_command: list[str],
    pid: int,
    launch: dict[str, object],
    environment: dict[str, str],
    distribution: str,
    patch: str,
    modules: dict[str, dict[str, str]],
    probe_profile: str,
    map_name: str,
    experiment_path: Path,
    clock_controls: dict[str, str],
    analyzers: list[dict[str, str]],
    capture: dict[str, object],
) -> dict[str, object]:
    experiment = load_experiment(experiment_path)
    load_numerical_policy()
    resolved_experiment = experiment_path.resolve()
    try:
        tracked_experiment = resolved_experiment.relative_to(REPO_ROOT).as_posix()
    except ValueError as exc:
        raise ContractError("experiment specification must live in the repository") from exc
    manifest: dict[str, object] = {
        "contract": MANIFEST_CONTRACT,
        "schema_version": MANIFEST_VERSION,
        "session_id": session.name,
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "state": "capturing",
        "capture_command": capture_command,
        "pid": pid,
        "launch": launch,
        "environment": environment,
        "retail": {
            "distribution": distribution,
            "patch": patch,
        },
        "modules": modules,
        "probe_profile": probe_profile,
        "map": map_name,
        "experiment": {
            "id": experiment["id"],
            "path": tracked_experiment,
            "sha256": file_sha256(resolved_experiment),
        },
        "clock_controls": clock_controls,
        "outputs": {},
        "records": {
            "captured": 0,
            "written": 0,
            "dropped": 0,
            "incomplete": 0,
        },
        "hook_health": {
            "state": "pending",
            "details": {},
        },
        "analyzers": analyzers,
        "capture": capture,
    }
    return validate_session_manifest(manifest)


def validate_session_manifest(manifest: dict[str, object]) -> dict[str, object]:
    _require(manifest.get("contract") == MANIFEST_CONTRACT, "unknown session manifest contract")
    _require(manifest.get("schema_version") == MANIFEST_VERSION, "unsupported session manifest schema_version")
    for field in ("session_id", "created_utc", "probe_profile", "map"):
        _nonempty_string(manifest.get(field), field)
    _require(manifest.get("state") in {"capturing", "complete", "failed"}, "invalid session state")
    command = manifest.get("capture_command")
    _require(isinstance(command, list) and bool(command), "capture_command must be non-empty")
    for index, argument in enumerate(command):
        _nonempty_string(argument, f"capture_command[{index}]")
    launch = manifest.get("launch")
    _require(isinstance(launch, dict), "launch must be an object")
    launch_command = launch.get("command")
    _require(isinstance(launch_command, list) and bool(launch_command), "launch.command must be non-empty")
    for index, argument in enumerate(launch_command):
        _nonempty_string(argument, f"launch.command[{index}]")
    _nonempty_string(launch.get("working_directory"), "launch.working_directory")
    environment = manifest.get("environment")
    _require(isinstance(environment, dict), "environment must be an object")
    for name, value in environment.items():
        _nonempty_string(name, "environment name")
        _nonempty_string(value, f"environment.{name}")
    retail = manifest.get("retail")
    _require(isinstance(retail, dict), "retail must be an object")
    _nonempty_string(retail.get("distribution"), "retail.distribution")
    _nonempty_string(retail.get("patch"), "retail.patch")
    modules = manifest.get("modules")
    _require(isinstance(modules, dict) and bool(modules), "modules must be non-empty")
    for name, module in modules.items():
        _nonempty_string(name, "module name")
        _require(isinstance(module, dict), f"module {name} must be an object")
        _nonempty_string(module.get("path"), f"module {name}.path")
        digest = _nonempty_string(module.get("sha256"), f"module {name}.sha256")
        _require(bool(SHA256_PATTERN.fullmatch(digest)), f"module {name}.sha256 is invalid")
    experiment = manifest.get("experiment")
    _require(isinstance(experiment, dict), "experiment must be an object")
    _nonempty_string(experiment.get("id"), "experiment.id")
    _nonempty_string(experiment.get("path"), "experiment.path")
    digest = _nonempty_string(experiment.get("sha256"), "experiment.sha256")
    _require(bool(SHA256_PATTERN.fullmatch(digest)), "experiment.sha256 is invalid")
    clock = manifest.get("clock_controls")
    _require(isinstance(clock, dict) and bool(clock), "clock_controls must be non-empty")
    for name, value in clock.items():
        _nonempty_string(name, "clock_controls name")
        _nonempty_string(value, f"clock_controls.{name}")
    records = manifest.get("records")
    _require(isinstance(records, dict), "records must be an object")
    for field in ("captured", "written", "dropped", "incomplete"):
        value = records.get(field)
        _require(isinstance(value, int) and value >= 0, f"records.{field} must be a non-negative integer")
    outputs = manifest.get("outputs")
    _require(isinstance(outputs, dict), "outputs must be an object")
    for name, output in outputs.items():
        _nonempty_string(name, "output name")
        _require(isinstance(output, dict), f"output {name} must be an object")
        _require(
            isinstance(output.get("bytes"), int) and output["bytes"] >= 0,
            f"output {name}.bytes must be non-negative",
        )
        digest = _nonempty_string(output.get("sha256"), f"output {name}.sha256")
        _require(bool(SHA256_PATTERN.fullmatch(digest)), f"output {name}.sha256 is invalid")
    analyzers = manifest.get("analyzers")
    _require(isinstance(analyzers, list), "analyzers must be an array")
    for index, analyzer in enumerate(analyzers):
        _require(isinstance(analyzer, dict), f"analyzers[{index}] must be an object")
        _nonempty_string(analyzer.get("name"), f"analyzers[{index}].name")
        digest = _nonempty_string(analyzer.get("sha256"), f"analyzers[{index}].sha256")
        _require(bool(SHA256_PATTERN.fullmatch(digest)), f"analyzers[{index}].sha256 is invalid")
    health = manifest.get("hook_health")
    _require(isinstance(health, dict), "hook_health must be an object")
    _nonempty_string(health.get("state"), "hook_health.state")
    _require(isinstance(health.get("details"), dict), "hook_health.details must be an object")
    _require(isinstance(manifest.get("capture"), dict), "capture must be an object")
    if manifest["state"] == "complete":
        _require(bool(outputs), "complete session must contain output hashes")
        _require(health["state"] == "healthy", "complete session hook_health must be healthy")
    return manifest


def output_metadata(session: Path, paths: Iterable[Path]) -> dict[str, dict[str, object]]:
    result: dict[str, dict[str, object]] = {}
    for path in paths:
        resolved = path.resolve()
        result[resolved.relative_to(session.resolve()).as_posix()] = {
            "bytes": resolved.stat().st_size,
            "sha256": file_sha256(resolved),
        }
    return result


def write_session_manifest(path: Path, manifest: dict[str, object]) -> None:
    validate_session_manifest(manifest)
    path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
