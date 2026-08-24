"""One checked subprocess implementation for all project builders and tools."""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass
from datetime import datetime, timezone
import os
from pathlib import Path
import subprocess
import sys
import time
from typing import Callable, Mapping, Sequence, TextIO


OutputSink = Callable[[str], None]

#: Seconds between mirror-log flushes while a child streams. Nothing in the
#: pipeline reads the run log mid-run -- the console status feeds off the
#: output sink -- so the interval only bounds how stale an externally tailed
#: log can be; the flush after the child exits guarantees completeness.
LOG_FLUSH_INTERVAL_SECONDS = 1.0


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
        tail_lines: int | None = None,
    ) -> ProcessResult:
        return run_process(
            argv,
            cwd=cwd or self.cwd,
            environment=self.environment,
            log=self.log,
            output_sink=self.output_sink,
            check=check,
            category=category,
            tail_lines=tail_lines,
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
    tail_lines: int | None = None,
) -> ProcessResult:
    """Run an argv safely, stream combined output, and retain it for reports.

    ``tail_lines`` bounds retention: when set, ``ProcessResult.output`` carries
    only the newest ``tail_lines`` lines. An editor commandlet under
    ``-FullStdOutLogOutput`` streams hundreds of thousands of lines; the mirror
    log keeps every line in either mode, so a bounded tail loses nothing the
    log does not already hold.
    """

    command = tuple(os.fspath(value) for value in argv)
    if not command:
        raise ValueError("process argv must not be empty")
    if tail_lines is not None and tail_lines <= 0:
        raise ValueError(f"tail_lines must be positive, got {tail_lines}")
    resolved_cwd = cwd.resolve()
    started = datetime.now(timezone.utc).isoformat()
    before = time.monotonic()
    # maxlen=None keeps everything; a positive maxlen keeps the newest tail.
    lines: deque[str] = deque(maxlen=tail_lines)
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
    active_log = log
    active_sink = output_sink
    stdout_available = True

    def record_mirror_failure(destination: str, exc: OSError) -> None:
        warning = (
            f"WARNING - child output mirror to {destination} failed: "
            f"{type(exc).__name__}: {exc}; continuing without that mirror\n"
        )
        lines.append(warning)
        if active_log is not None and destination != "log":
            try:
                active_log.write(warning)
                active_log.flush()
            except OSError:
                pass

    last_flush = time.monotonic()
    try:
        for line in process.stdout:
            lines.append(line)
            if active_log is not None:
                try:
                    active_log.write(line)
                    now = time.monotonic()
                    if now - last_flush >= LOG_FLUSH_INTERVAL_SECONDS:
                        active_log.flush()
                        last_flush = now
                except OSError as exc:
                    active_log = None
                    record_mirror_failure("log", exc)
            if active_sink is not None:
                try:
                    active_sink(line.rstrip("\r\n"))
                except OSError as exc:
                    active_sink = None
                    record_mirror_failure("output sink", exc)
            elif active_log is None and stdout_available:
                try:
                    sys.stdout.write(line)
                    sys.stdout.flush()
                except OSError as exc:
                    record_mirror_failure("stdout", exc)
                    stdout_available = False
    finally:
        try:
            process.stdout.close()
        finally:
            returncode = process.wait()
        if active_log is not None:
            # The interval flush leaves a buffered tail; the log is complete
            # once the child has exited.
            try:
                active_log.flush()
            except OSError as exc:
                active_log = None
                record_mirror_failure("log", exc)
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
