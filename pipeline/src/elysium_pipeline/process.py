"""One checked subprocess implementation for all project builders and tools."""

from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime, timezone
import os
from pathlib import Path
import subprocess
import sys
import time
from typing import Callable, Mapping, Sequence, TextIO


OutputSink = Callable[[str], None]


@dataclass(frozen=True, slots=True)
class ProcessResult:
    argv: tuple[str, ...]
    cwd: Path
    returncode: int
    started_at: str
    duration_seconds: float
    output: str

    @property
    def succeeded(self) -> bool:
        return self.returncode == 0


class ProcessFailure(RuntimeError):
    """A child process failed; ``category`` is the public CLI exit code."""

    def __init__(self, result: ProcessResult, category: int):
        self.result = result
        self.category = category
        super().__init__(
            f"{result.argv[0]} exited with {result.returncode} "
            f"after {result.duration_seconds:.1f}s"
        )


class ProcessRunner:
    """Command-scoped adapter used by build, Unreal, and debug drivers."""

    def __init__(
        self,
        *,
        cwd: Path,
        environment: Mapping[str, str] | None = None,
        log: TextIO | None = None,
        output_sink: OutputSink | None = None,
    ):
        self.cwd = cwd
        self.environment = environment
        self.log = log
        self.output_sink = output_sink

    def run(
        self,
        argv: Sequence[str | os.PathLike[str]],
        *,
        cwd: Path | None = None,
        category: int = 3,
        check: bool = False,
    ) -> ProcessResult:
        return run_process(
            argv,
            cwd=cwd or self.cwd,
            environment=self.environment,
            log=self.log,
            output_sink=self.output_sink,
            check=check,
            category=category,
        )


def run_process(
    argv: Sequence[str | os.PathLike[str]],
    *,
    cwd: Path,
    environment: Mapping[str, str] | None = None,
    log: TextIO | None = None,
    output_sink: OutputSink | None = None,
    check: bool = True,
    category: int = 3,
) -> ProcessResult:
    """Run an argv safely, stream combined output, and retain it for reports."""

    command = tuple(os.fspath(value) for value in argv)
    if not command:
        raise ValueError("process argv must not be empty")
    resolved_cwd = cwd.resolve()
    started = datetime.now(timezone.utc).isoformat()
    before = time.monotonic()
    lines: list[str] = []
    process = subprocess.Popen(
        command,
        cwd=resolved_cwd,
        env=dict(environment) if environment is not None else None,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        bufsize=1,
    )
    assert process.stdout is not None
    for line in process.stdout:
        lines.append(line)
        if log is not None:
            log.write(line)
            log.flush()
        if output_sink is not None:
            output_sink(line.rstrip("\r\n"))
        elif log is None:
            sys.stdout.write(line)
            sys.stdout.flush()
    returncode = process.wait()
    result = ProcessResult(
        argv=command,
        cwd=resolved_cwd,
        returncode=returncode,
        started_at=started,
        duration_seconds=time.monotonic() - before,
        output="".join(lines),
    )
    if check and returncode:
        raise ProcessFailure(result, category)
    return result
