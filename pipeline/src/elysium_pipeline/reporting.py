"""Structured run and task reporting shared by the command surface."""

from __future__ import annotations

from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
from enum import IntEnum
import json
from pathlib import Path
import re
import time
from typing import Any


class ExitCode(IntEnum):
    USAGE_OR_CONFIG = 2
    DEPENDENCY_OR_TOOLCHAIN = 3
    BUILD = 4
    OFFLINE_EXPORT = 5
    UNREAL_OR_BAKE = 6
    VALIDATION = 7


@dataclass(slots=True)
class TaskResult:
    name: str
    status: str
    started_at: str
    duration_seconds: float
    detail: str = ""
    outputs: list[str] = field(default_factory=list)
    metadata: dict[str, Any] = field(default_factory=dict)


@dataclass(slots=True)
class RunReport:
    command: str
    arguments: list[str]
    started_at: str = field(
        default_factory=lambda: datetime.now(timezone.utc).isoformat()
    )
    completed_at: str | None = None
    status: str = "running"
    exit_code: int = 0
    tasks: list[TaskResult] = field(default_factory=list)
    metadata: dict[str, Any] = field(default_factory=dict)
    _started_monotonic: float = field(default_factory=time.monotonic, repr=False)

    def add_task(
        self,
        name: str,
        *,
        status: str,
        started_at: str,
        duration_seconds: float,
        detail: str = "",
        outputs: list[str] | None = None,
        metadata: dict[str, Any] | None = None,
    ) -> TaskResult:
        result = TaskResult(
            name=name,
            status=status,
            started_at=started_at,
            duration_seconds=duration_seconds,
            detail=detail,
            outputs=outputs or [],
            metadata=metadata or {},
        )
        self.tasks.append(result)
        return result

    def finish(self, *, exit_code: int = 0, status: str | None = None) -> None:
        self.completed_at = datetime.now(timezone.utc).isoformat()
        self.exit_code = int(exit_code)
        self.status = status or ("succeeded" if exit_code == 0 else "failed")
        self.metadata.setdefault(
            "duration_seconds", time.monotonic() - self._started_monotonic
        )

    def to_dict(self) -> dict[str, Any]:
        result = asdict(self)
        result.pop("_started_monotonic", None)
        return result

    def artifact_stem(self) -> str:
        """The filename stem this run's report and console log share.

        Both artifacts land beside each other under the log root, and an agent handed one
        path has to be able to guess the other. Deriving the stem once here is what keeps
        them from drifting: the two spellings used to differ, so a command whose name held
        an underscore or a slash wrote `20260911T233524Z-export_v2-map.log` next to
        `20260911T233524Z-export-v2-map.json`.
        """

        stamp = datetime.fromisoformat(self.started_at).strftime("%Y%m%dT%H%M%S.%fZ")
        slug = re.sub(r"[^a-z0-9]+", "-", self.command.lower()).strip("-") or "run"
        return f"{stamp}-{slug}"

    def write(self, log_root: Path) -> Path:
        log_root.mkdir(parents=True, exist_ok=True)
        destination = log_root / f"{self.artifact_stem()}.json"
        destination.write_text(
            json.dumps(self.to_dict(), indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        return destination
