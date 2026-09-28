"""One checked subprocess implementation for all project builders and tools."""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass
from datetime import datetime, timezone
import os
from pathlib import Path
import subprocess
import sys
import threading
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

    def annotate(self, note: str) -> "ProcessFailure":
        """Append caller context to the message the CLI prints, keeping type and category.

        A caller that knows what the child was doing -- which automation test was in flight,
        where its report lives -- adds it here rather than re-raising as a different type,
        so the exit category and the retained output tail still reach the CLI trailer."""

        self.args = (f"{self.args[0]}; {note}" if self.args else note,)
        return self


class ProcessTimeout(ProcessFailure):
    """A child process outlived its deadline and was killed."""

    def __init__(self, result: ProcessResult, category: int, timeout: float):
        self.timeout = timeout
        RuntimeError.__init__(
            self,
            f"{result.argv[0]} exceeded its {timeout:.0f}s deadline and was killed "
            f"after {result.duration_seconds:.1f}s"
        )
        self.result = result
        self.category = category


class ProcessIdle(ProcessTimeout):
    """A child process printed nothing for ``idle_timeout`` seconds and was killed.

    A subclass of ``ProcessTimeout`` so every caller that already handles a killed child
    handles this one too. ``last_line`` is the last non-blank line the child printed before
    it went silent -- the nearest thing to a statement of what it was doing when it hung."""

    def __init__(
        self,
        result: ProcessResult,
        category: int,
        idle_timeout: float,
        last_line: str | None,
        timeout: float | None = None,
    ):
        self.timeout = timeout
        self.idle_timeout = idle_timeout
        self.last_line = last_line
        RuntimeError.__init__(
            self,
            f"{result.argv[0]} printed nothing for {idle_timeout:.0f}s and was killed "
            f"after {result.duration_seconds:.1f}s; last output line: "
            + (repr(last_line) if last_line is not None else "(none)")
        )
        self.result = result
        self.category = category


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
        timeout: float | None = None,
        idle_timeout: float | None = None,
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
            timeout=timeout,
            idle_timeout=idle_timeout,
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
    timeout: float | None = None,
    idle_timeout: float | None = None,
) -> ProcessResult:
    """Run an argv safely, stream combined output, and retain it for reports.

    ``tail_lines`` bounds retention: when set, ``ProcessResult.output`` carries
    only the newest ``tail_lines`` lines. An editor commandlet under
    ``-FullStdOutLogOutput`` streams hundreds of thousands of lines; the mirror
    log keeps every line in either mode, so a bounded tail loses nothing the
    log does not already hold.

    ``timeout`` bounds the whole run. A commandlet that wedges before its
    ``;Quit`` writes no more output and never exits, so the streaming read
    blocks forever; a watchdog kills the child and the call raises
    ``ProcessTimeout``. Left unset the call waits indefinitely, which is
    correct for an attended launch the operator can see.

    ``idle_timeout`` bounds silence: when no line has arrived for that many
    seconds the child is killed and the call raises ``ProcessIdle`` (a
    ``ProcessTimeout``), naming the span and the last line the child printed.
    A hung automation test stops printing long before any whole-run deadline,
    so the idle bound turns a quarter hour of dead air into two minutes. Only a
    complete line resets the clock; a child that is legitimately quiet for long
    stretches -- UnrealBuildTool inside one big compile -- must not set it.
    """

    command = tuple(os.fspath(value) for value in argv)
    if not command:
        raise ValueError("process argv must not be empty")
    if tail_lines is not None and tail_lines <= 0:
        raise ValueError(f"tail_lines must be positive, got {tail_lines}")
    if idle_timeout is not None and idle_timeout <= 0:
        raise ValueError(f"idle_timeout must be positive, got {idle_timeout}")
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

    # The watchdog, not `Popen.communicate(timeout=...)`: this reads the child's output line by
    # line so it can mirror it live, and that read is what blocks when a commandlet wedges.
    # Killing the child closes the pipe, which is what releases the loop below. One thread
    # serves both bounds: it sleeps until the nearer of the deadline and the idle expiry,
    # re-reads the idle clock the read loop keeps pushing forward, and kills on whichever
    # bound actually lapsed.
    killed_by: str | None = None
    last_activity = before
    last_line: str | None = None
    watchdog_stop = threading.Event()

    def kill(reason: str) -> None:
        nonlocal killed_by
        killed_by = reason
        try:
            process.kill()
        except OSError:
            pass

    def watch() -> None:
        deadline_at = before + timeout if timeout is not None else None
        while True:
            now = time.monotonic()
            waits: list[float] = []
            if deadline_at is not None:
                if now >= deadline_at:
                    kill("deadline")
                    return
                waits.append(deadline_at - now)
            if idle_timeout is not None:
                idle_at = last_activity + idle_timeout
                if now >= idle_at:
                    kill("idle")
                    return
                waits.append(idle_at - now)
            if watchdog_stop.wait(min(waits)):
                return

    watchdog = (
        threading.Thread(target=watch, name="process-watchdog", daemon=True)
        if timeout is not None or idle_timeout is not None
        else None
    )
    if watchdog is not None:
        watchdog.start()

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
            # A float store is atomic under the GIL; the watchdog only ever reads it.
            last_activity = time.monotonic()
            if line.strip():
                last_line = line.rstrip("\r\n")
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
            watchdog_stop.set()
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
    if killed_by == "idle":
        assert idle_timeout is not None
        raise ProcessIdle(result, category, idle_timeout, last_line, timeout)
    if killed_by == "deadline":
        assert timeout is not None
        raise ProcessTimeout(result, category, timeout)
    if check and returncode:
        raise ProcessFailure(result, category)
    return result
