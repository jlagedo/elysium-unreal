"""One public command surface for the Elysium project."""

from __future__ import annotations

from collections import Counter, deque
from contextlib import contextmanager, nullcontext, redirect_stdout
from dataclasses import dataclass, field
from datetime import datetime, timezone
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import threading
import time
from typing import Any, Callable

import typer
from rich.console import Console

from elysium_pipeline.config import ConfigError, ProjectConfig
from elysium_pipeline.dependencies import (
    DependencyError,
    check_dependencies,
    load_project_lock,
    sync_dependencies,
)
from elysium_pipeline.process import ProcessFailure, ProcessRunner
from elysium_pipeline.reporting import ExitCode, RunReport
from elysium_pipeline.workspace_lock import WorkspaceLease, assert_project_idle


console = Console()
#: Where prose goes when stdout is reserved for a machine. `--json` promises that stdout
#: holds one JSON object and nothing else, so every progress line, child echo and error
#: has to leave by the other door.
err_console = Console(stderr=True)
PASSTHROUGH = {"allow_extra_args": True, "ignore_unknown_options": True}
app = typer.Typer(
    name="elysium",
    help="Build, reconstruct, export, validate, and run Elysium-Unreal.",
    no_args_is_help=True,
    pretty_exceptions_show_locals=False,
)
deps_app = typer.Typer(help="Restore and verify locked project dependencies.")
export_app = typer.Typer(help="Export VtMB sources and generate Unreal packages.")
export_v2_app = typer.Typer(help="Run isolated lossless GLB export pipelines.")
# `import` is a Python keyword, so the sub-app object cannot be named after the command it
# registers; the command surface is still `uv run elysium import <family>`.
import_app = typer.Typer(help="Deploy published export_v2 units into the runtime corpus.")
bake_app = typer.Typer(help="Author baked Unreal assets from published export_v2 units.")
verify_app = typer.Typer(help="Check baked packages against what the export declares.")
run_app = typer.Typer(help="Launch the Unreal editor or standalone game.")
debug_app = typer.Typer(help="Run development and acceptance harnesses.")
ide_app = typer.Typer(help="Refresh what a C++ editor reads about this checkout.")
blender_app = typer.Typer(help="Package and drive the Blender GLB review add-on.")
app.add_typer(deps_app, name="deps")
app.add_typer(export_app, name="export")
app.add_typer(export_v2_app, name="export_v2")
app.add_typer(import_app, name="import")
app.add_typer(bake_app, name="bake")
app.add_typer(verify_app, name="verify")
app.add_typer(run_app, name="run")
app.add_typer(debug_app, name="debug")
app.add_typer(ide_app, name="ide")
app.add_typer(blender_app, name="blender")


@dataclass(slots=True)
class CliState:
    game: Path | None
    work: Path | None
    ue: Path | None
    verbose: bool = False
    #: Set by the commands that offer `--json`, read by `_execute`, which owns the envelope.
    json_output: bool = False
    #: What a command wants inside that envelope. `_summary` fills it while printing the
    #: same facts as prose, so the two views cannot describe different runs.
    payload: dict[str, Any] = field(default_factory=dict)
    #: Set by a command whose `--json` product is a document rather than a run status, so
    #: `_execute` keeps its hands off stdout instead of appending a second object.
    json_emitted: bool = False
    #: Writes one line into this run's log. Set by `_execute` while the handle is open, so a
    #: command's own verdict lands in the record as well as on the console.
    log_sink: Callable[[str], None] | None = None

    def resolve(
        self,
        *,
        require_game: bool = False,
        require_work: bool = False,
        require_ue: bool = False,
    ) -> ProjectConfig:
        config = ProjectConfig.resolve(
            self.game,
            self.work,
            self.ue,
            require_game=require_game,
            require_work=require_work,
            require_ue=require_ue,
        )
        config.apply_environment()
        return config


@app.callback()
def root(
    ctx: typer.Context,
    game: Path | None = typer.Option(
        None, "--game", help="Read-only VtMB installation root."
    ),
    work: Path | None = typer.Option(
        None, "--work", help="External generated-output and cache root."
    ),
    ue: Path | None = typer.Option(
        None, "--ue", help="Unreal Engine 5.8 installation root."
    ),
    verbose: bool = typer.Option(
        False, "--verbose", "-v",
        help="Echo every child process line instead of the filtered summary.",
    ),
) -> None:
    # `--verbose` is a root option rather than one on each command for two reasons: there are
    # around a hundred commands, and many of them carry `PASSTHROUGH`, which would forward a
    # trailing `--verbose` into the Unreal command line instead of consuming it here. The
    # environment variable is the form a wrapper script or an agent harness can set once.
    ctx.obj = CliState(
        game=game,
        work=work,
        ue=ue,
        verbose=verbose or os.environ.get("ELYSIUM_VERBOSE", "").strip().lower()
        not in ("", "0", "false", "no", "off"),
    )


def _names_verbose(ctx: typer.Context) -> bool:
    """Whether a verbosity flag was typed after the subcommand, wherever it landed."""

    candidates: list[str] = list(getattr(ctx, "args", ()) or ())
    for value in (getattr(ctx, "params", None) or {}).values():
        if isinstance(value, str):
            candidates.append(value)
        elif isinstance(value, (list, tuple)):
            candidates.extend(item for item in value if isinstance(item, str))
    return bool({"--verbose", "-v"} & set(candidates))


def _state(ctx: typer.Context) -> CliState:
    if not isinstance(ctx.obj, CliState):
        raise RuntimeError("CLI state was not initialized")
    # `--verbose` is a root option, and most commands forward what they do not recognise to
    # Unreal. Typed after the subcommand it silently becomes a child argument, so the console
    # would keep advertising an escape hatch that did nothing. Two places have to be checked:
    # commands with `PASSTHROUGH` collect it in `ctx.args`, while a command whose signature
    # ends in a variadic `Argument` -- `run play <map> <extra...>` -- swallows it as a
    # positional instead. A command with neither has already had it rejected by click.
    if not ctx.obj.verbose and _names_verbose(ctx):
        raise typer.BadParameter(
            "--verbose is a root option: write `elysium --verbose <command> ...`, "
            "not `elysium <command> --verbose`"
        )
    return ctx.obj


#: What a command's child processes may say on the console. Editor commandlets stream
#: thousands of engine lines; the run log keeps every one, the console keeps the signal --
#: our own script output (LogPython) and any warning or error.
#:
#: Two vocabularies, because two kinds of child. The engine writes `Category: Severity:`.
#: `build` runs a *compiler*, whose diagnostics look nothing like that: MSVC writes
#: `foo.cpp(95,28): error C2259:`, the linker writes `LINK : fatal error LNK1181:`, and
#: clang writes `foo.cpp:95:28: error:` -- all lowercase, none carrying a log category. A
#: filter that knows only the engine's shape shows nothing at all for a failed build.
_CHILD_SIGNAL = re.compile(
    r"LogPython|Fatal error|Assertion failed|: Error:|: Warning:|^Error:"
    r"|\): (?:error|warning|fatal error) |: (?:error|warning): |error LNK")

#: ``_CHILD_SIGNAL``'s literal alternatives as plain substrings; ``^Error:``
#: anchors to the line start, so ``startswith`` carries it below. Ordinary
#: engine lines fail every substring test and never reach the regex.
_CHILD_SIGNAL_LITERALS = (
    "LogPython", "Fatal error", "Assertion failed", ": Error:", ": Warning:",
    "): error ", "): warning ", "): fatal error ", ": error: ", ": warning: ", "error LNK")


def _child_signal(line: str) -> bool:
    """Whether a child line is console signal; substring checks gate the regex."""
    if not line.startswith("Error:") and not any(
            literal in line for literal in _CHILD_SIGNAL_LITERALS):
        return False
    return _CHILD_SIGNAL.search(line) is not None


#: The substrings that make a signal line an error rather than a warning, across both
#: vocabularies. Severity decides who gets quoted when the console runs out of room, so a
#: gap here costs an agent the one line it needed.
_ERROR_MARKERS = ("Fatal error", "Assertion failed", ": Error:", "): error ",
                  "): fatal error ", ": error: ", "error LNK")
_WARNING_MARKERS = (": Warning:", "): warning ", ": warning: ")


def _echo_severity(line: str) -> str:
    """`error`, `warning` or `info` -- what must not queue behind what."""

    text = line.lstrip()
    if line.startswith("Error:") or text[:6].lower().startswith("error:"):
        return "error"
    if any(marker in line for marker in _ERROR_MARKERS):
        return "error"
    if any(marker in line for marker in _WARNING_MARKERS):
        return "warning"
    if text.startswith("!") or text[:8].lower().startswith("warning:"):
        return "warning"
    return "info"


def _local_signal(line: str) -> bool:
    """Whether a line the pipeline printed in this process is console signal.

    The offline decoders speak a different vocabulary from an Unreal child: no log category,
    no severity token. They mark a warning or a failure by putting `!` in front of the line,
    and a per-item warning is spelled `warning: <item>: <detail>`. Everything else they print
    is progress -- one row per exported unit, four thousand of them for `textures-glb` --
    which the status line counts instead of quoting.
    """

    text = line.lstrip()[:10].lower()
    # `warning -` is the spelling `unreal.prune_test_reports` and `process.py`'s mirror
    # failure use; `warning:` is the exporters'. Both are the same statement.
    return text.startswith(("!", "warning:", "error:", "warning -", "error -"))


def _console_signal(line: str) -> bool:
    """Either vocabulary. One command can produce both: `export map` decodes in this process
    and launches an editor, and both halves reach the same echo."""

    return _child_signal(line) or _local_signal(line)


#: How many distinct signal lines the console quotes before it stops quoting and starts
#: counting. Past this the run is better served by a category tally and the log path than
#: by the four hundredth variation of one warning.
_ECHO_DISTINCT_LIMIT = 40

#: Errors quote against their own budget rather than competing with warnings for the one
#: above. Measured on this project's failing test runs, the first `: Error:` line ranks
#: 101st to 192nd among distinct signal lines -- behind a wall of `LogElysiumWeapon` and
#: `LogElysiumNpcEnt` warnings -- so a single shared budget shows a failing run everything
#: except why it failed.
_ECHO_ERROR_LIMIT = 20

#: How many categories the tally names before it defers to the log.
_ECHO_CATEGORY_LIMIT = 8

#: How many signal lines a failed command quotes back as evidence. A lean console must not
#: mean a blind failure: without this a failing build reports only its exit code.
_ECHO_EVIDENCE_LINES = 30

#: The `[2026.09.11-23.35.41:233][555]` stamp Unreal writes in front of every log line.
_LOG_STAMP = re.compile(r"^\[[^\]]*\]\[[ 0-9]*\]")

#: Under automation Unreal emits each warning twice: once from the category that raised it,
#: and once mirrored through `LogAutomationController` with the severity moved to the front
#: and a `[log]` marker appended. These three patterns rewrite both spellings to the same
#: text, so the mirror is recognised as a repeat of its own original rather than doubling
#: the console. Measured on a 19,098-line run: 1,795 warnings, 501 of them distinct.
#: The mirror is rewritten into the shape of its own original -- category first, then the
#: severity it was raised at -- rather than having the severity deleted. Deleting it would
#: make `LogFoo: Warning: nav mesh missing` and `LogFoo: Error: nav mesh missing` one key,
#: so whichever arrived second would be silently counted as a repeat of the first and never
#: printed. An escalation is exactly the event this console must not lose.
_AUTOMATION_MIRROR = re.compile(r"^LogAutomationController: (Warning|Error|Display): (\w+): ")
_MIRROR_MARKER = re.compile(r"\s*\[(?:log|warning|error|display)\]\s*$", re.IGNORECASE)
_SEVERITY = re.compile(r"^(\w+): (Warning|Error|Display): ")

#: `LogPython` is in the signal vocabulary to carry our own editor scripts, which speak
#: through `unreal.log()` and come out as `LogPython: [import-characters] ...`. The engine
#: uses the same category at Display verbosity to narrate every plugin's `init_unreal.py`,
#: which is boot plumbing no caller asked about -- around thirty lines in front of every
#: editor run, enough to push the real failure out of a bounded evidence tail. Anything
#: Python actually raises arrives as `LogPython: Error:`/`Warning:` and is caught by those
#: alternatives instead, so nothing actionable is lost here.
#: Matched as a substring against the raw line: the timestamp precedes it, and `_echo_key`
#: has already dropped the `Display:` token by the time a key exists.
_ECHO_BOILERPLATE = "LogPython: Display: "


def _echo_key(line: str) -> str:
    """What makes two child lines the same event: no stamp, no severity, no mirror."""

    text = _LOG_STAMP.sub("", line).strip()
    text = _AUTOMATION_MIRROR.sub(r"\2: \1: ", text)
    text = _MIRROR_MARKER.sub("", text)
    return _SEVERITY.sub(r"\1|\2: ", text).strip()


def _echo_category(key: str) -> str:
    """The label a normalised line is tallied under once the console stops quoting.

    An Unreal line names its own category and is grouped by it. An offline warning does not:
    it is spelled `warning: <item>: <detail>`, so splitting on the first colon would file all
    sixty thousand of `fonts-glb`'s under one label called `warning`. Those are grouped by
    the opening clause of the detail instead, which is the part that repeats across items.

    This labels the tally only. The dedupe key is untouched, so two warnings that differ
    anywhere are still counted, and printed, as two.
    """

    head, _, rest = key.partition(":")
    # `_echo_key` keeps the severity as `Category|Severity`; the tally groups by category.
    head = head.split("|", 1)[0]
    if head.strip().lower() in ("warning", "error") and rest:
        _item, _, detail = rest.strip().partition(":")
        clause = " ".join(detail.partition(":")[0].split()[:6])[:40].strip()
        if clause:
            return f"{head.strip().lower()}: {clause}"
    return head if head and len(head) <= 40 else "(uncategorised)"


class _ChildEcho:
    """The filtered console echo for a child process, with a live status.

    Signal lines (our scripts' LogPython output, engine warnings/errors) print, but only
    the first time each one is said: a repeat and its `LogAutomationController` mirror both
    fold into a count against the line already shown. Past `_ECHO_DISTINCT_LIMIT` distinct
    lines the echo stops quoting altogether and keeps a per-category tally instead.
    Everything else feeds a single rewritten status line -- elapsed time, how many log lines
    have streamed, and the most recent one -- redrawn by a ticker so a quiet editor phase
    never looks stalled. Terminal only; redirected output gets the surviving lines and
    nothing else.

    Closing prints what was withheld and how to get it back. That footer is the contract:
    the run log on disk holds every line, so the console can afford to be this quiet only
    for as long as it says out loud that it is being quiet."""

    def __init__(self, out: Console | None = None, label: str = "working"):
        self._out = out or console
        # The status line used to say "editor" for everything. It now runs for `deps sync`
        # and the offline GLB seams too, which never launch one.
        self._label = label
        self._stream = self._out.file
        self._tty = bool(getattr(self._stream, "isatty", lambda: False)())
        self._lock = threading.Lock()
        self._started = time.monotonic()
        self._count = 0
        self._last = ""
        self._status_len = 0
        self._stop = threading.Event()
        self._seen: Counter[str] = Counter()
        self._categories: Counter[str] = Counter()
        self._distinct_categories: Counter[str] = Counter()
        self._printed = 0
        self._printed_errors = 0
        self._withheld = 0
        self._repeats = 0
        self._warnings = 0
        self._errors = 0
        # First occurrences only: a crash buried under four hundred repeats of one warning
        # is still reachable, which it would not be in a tail of raw lines. Errors keep a
        # second deque of their own, because a run with two hundred distinct warnings would
        # otherwise evict the failure out of a shared one before the command even ends.
        self._evidence: deque[str] = deque(maxlen=_ECHO_EVIDENCE_LINES)
        self._error_evidence: deque[str] = deque(maxlen=_ECHO_EVIDENCE_LINES)
        #: Already-curated lines, kept head-first. See `passthrough`.
        self._curated: list[str] = []
        if self._tty:
            threading.Thread(target=self._tick, daemon=True).start()

    def __call__(self, line: str) -> None:
        if _ECHO_BOILERPLATE in line or not _console_signal(line):
            with self._lock:
                self._count += 1
                self._last = line.strip()
            return
        key = _echo_key(line)
        severity = _echo_severity(line)
        with self._lock:
            self._categories[_echo_category(key)] += 1
            if self._seen[key]:
                self._seen[key] += 1
                self._repeats += 1
                self._count += 1
                return
            self._seen[key] = 1
            self._distinct_categories[_echo_category(key)] += 1
            # Counted per distinct event, not per line: the automation mirror says every
            # warning twice, and a caller branching on this number wants events.
            if severity == "error":
                self._errors += 1
                self._error_evidence.append(line.strip())
            else:
                if severity == "warning":
                    self._warnings += 1
                self._evidence.append(line.strip())
            if severity == "error":
                exhausted = self._printed_errors >= _ECHO_ERROR_LIMIT
            else:
                exhausted = self._printed >= _ECHO_DISTINCT_LIMIT
            if exhausted:
                self._withheld += 1
                self._count += 1
                return
            if severity == "error":
                self._printed_errors += 1
            else:
                self._printed += 1
            self._clear()
            self._out.print(line, markup=False, soft_wrap=True)
            self._draw()

    def passthrough(self, line: str) -> None:
        """Print a line whose producer has already decided it is worth showing.

        Not classified and not counted as suppressed: it was never a candidate for
        suppression. It does become evidence, because the thing that sends curated lines
        here -- the export task graph -- sends a failed task's output tail among them.

        Kept head-first, unlike the other two stores. A curated burst leads with what
        identifies it: `TaskProgress` emits `[7/120] la_hub_1  FAILED  12.4s` and then
        thirty lines of body, so a tail-biased store drops the only line naming the task
        that failed."""

        with self._lock:
            if len(self._curated) < _ECHO_EVIDENCE_LINES:
                self._curated.append(line.strip())
            self._clear()
            self._out.print(line, markup=False, soft_wrap=True)
            self._draw()

    def counts(self) -> dict[str, int]:
        """What the echo saw, for a `--json` envelope."""

        with self._lock:
            return {
                "warnings": self._warnings,
                "errors": self._errors,
                "suppressed_lines": self._count,
            }

    def evidence(self) -> list[str]:
        """The distinct signal lines quoted back when the command failed.

        Errors first and in full: they are why the command failed. Curated lines next, since
        a producer that filtered its own output has already made this judgement. Ordinary
        warnings fill whatever room is left, oldest dropped, so a run with one error and
        three hundred warnings still reports the error."""

        with self._lock:
            picked = list(self._error_evidence) + self._curated
            room = max(0, _ECHO_EVIDENCE_LINES - len(picked))
            if room:
                picked += list(self._evidence)[-room:]
            return picked[:_ECHO_EVIDENCE_LINES]

    def _tick(self) -> None:
        while not self._stop.wait(1.0):
            with self._lock:
                self._draw()

    def _draw(self) -> None:
        if not self._tty:
            return
        minutes, seconds = divmod(int(time.monotonic() - self._started), 60)
        text = f"\u00bb {self._label} {minutes:02d}:{seconds:02d} \u00b7 {self._count} log lines"
        if self._last:
            text += " \u00b7 " + self._last
        width = shutil.get_terminal_size(fallback=(120, 25)).columns - 1
        text = text[:width]
        pad = max(0, self._status_len - len(text))
        self._stream.write("\r" + text + " " * pad + "\r" + text)
        self._stream.flush()
        self._status_len = len(text)

    def _clear(self) -> None:
        if self._status_len:
            self._stream.write("\r" + " " * self._status_len + "\r")
            self._stream.flush()
            self._status_len = 0

    def _footer(self) -> None:
        """Say what was withheld. Silent truncation is worse than none."""

        if self._withheld:
            ranked = self._categories.most_common(_ECHO_CATEGORY_LIMIT)
            for category, total in ranked:
                distinct = self._distinct_categories[category]
                self._out.print(
                    f"[dim]!  {category}: {total} line(s), {distinct} distinct[/dim]"
                )
            remaining = len(self._categories) - len(ranked)
            if remaining > 0:
                self._out.print(f"[dim]  ... and {remaining} more[/dim]")
        parts = []
        if self._count:
            parts.append(f"{self._count:,} lines suppressed")
        if self._repeats:
            parts.append(f"{self._repeats:,} repeats collapsed")
        if self._withheld:
            parts.append(f"{self._withheld:,} distinct not shown")
        if parts:
            self._out.print(
                "[dim]\u2026 " + " \u00b7 ".join(parts) + " \u00b7 --verbose for all[/dim]"
            )

    def close(self) -> None:
        self._stop.set()
        with self._lock:
            self._clear()
            self._tty = False
            self._footer()


class _StdoutTee(io.TextIOBase):
    """`sys.stdout` for the length of one command's in-process work.

    The GLB seams and `export_manager` report with bare `print()` -- one row per unit, some
    thousands of rows for a corpus seam -- and because they run in this process rather than
    a child, that output reached neither the run log nor the filter. It went to the terminal
    and was then gone: a warning printed during `export_v2 textures-glb` could not be
    recovered afterwards at any verbosity. Teeing here puts those lines on the same footing
    as a child's: every one to the log, the signal to the console.

    Bare `print()` arrives in fragments -- the text, then the newline as its own call -- so
    lines are reassembled here before either destination sees them."""

    def __init__(self, log, sink: Callable[[str], None],
                 signal_sink: Callable[[str], None] | None = None):
        self._log = log
        self._sink = sink
        self._signal_sink = signal_sink or sink
        self._pending = ""

    def write_signal(self, line: str) -> None:
        """A line whose producer has already decided it is worth showing.

        Logged and printed without classification. This filter is tuned for raw decoder and
        engine chatter; a nested reporter that has already capped, deduped and elided its own
        output -- `tasking.TaskProgress` is the one -- must not be filtered a second time
        against a vocabulary meant for something else. Duck-typed on purpose: `tasking` finds
        this with `getattr` and never imports this module."""

        if self._log is not None:
            self._log.write(line + "\n")
        self._signal_sink(line)

    @property
    def encoding(self) -> str:
        return "utf-8"

    def writable(self) -> bool:
        return True

    def isatty(self) -> bool:
        # Also load-bearing for `tasking.TaskProgress`, which starts its own status ticker
        # when its stdout is a terminal. Two tickers rewriting one line would fight.
        return False

    def fileno(self) -> int:
        raise io.UnsupportedOperation(
            "the console tee is line-oriented and owns no file descriptor; a child that "
            "needs the real stdout should be run through ProcessRunner, or the whole "
            "command through --verbose"
        )

    @property
    def buffer(self):
        raise io.UnsupportedOperation(
            "the console tee is text-only; a binary write would bypass line reassembly "
            "and interleave inside another line"
        )

    def write(self, text: str) -> int:
        if not text:
            return 0
        self._pending += text
        while True:
            newline = self._pending.find("\n")
            if newline < 0:
                break
            line, self._pending = self._pending[:newline], self._pending[newline + 1:]
            self._emit(line)
        return len(text)

    def flush(self) -> None:
        """Flushes the log, not the pending line.

        `print(..., flush=True)` must not force a partial line out ahead of its newline, or a
        status line lands mid-sentence. The log is a different matter: an offline export runs
        no child process, so `ProcessRunner`'s interval flush never fires and without this
        nothing reaches disk until the handle closes."""

        if self._log is not None:
            self._log.flush()

    def _emit(self, line: str) -> None:
        line = line.rstrip("\r")
        if self._log is not None:
            self._log.write(line + "\n")
        self._sink(line)

    def finish(self) -> None:
        """Release a trailing line that never got its newline."""

        if self._pending:
            pending, self._pending = self._pending, ""
            self._emit(pending)


@contextmanager
def _teed_stdout(log, sink: Callable[[str], None],
                 signal_sink: Callable[[str], None] | None = None):
    """Route bare `print()` through the run log and the filter for the length of an action."""

    tee = _StdoutTee(log, sink, signal_sink)
    real = sys.stdout
    saved = console._file
    # Rich resolves `sys.stdout` at write time, so without this the command's own summary
    # would be swallowed by the tee along with the decoder chatter it is summarising.
    # Pinning the console to the real stream keeps the verdict where a caller can read it.
    console._file = real
    try:
        with redirect_stdout(tee):
            yield
    finally:
        console._file = saved
        tee.finish()


def _summary(state: CliState, human: str, **fields: Any) -> None:
    """One summary line for a person and the same facts as JSON fields.

    Both views come from one call site on purpose: a `--json` payload assembled separately
    from the prose is a payload that eventually disagrees with it."""

    state.payload.update(fields)
    if state.log_sink is not None:
        # The console pin keeps this line out of the filter; without this it would also stay
        # out of the log, and a run log missing "610 of 610 test(s) executed" is a log
        # missing the answer.
        state.log_sink(human)
    if not state.json_output:
        console.print(human)


def _emit_json(
    report: RunReport,
    payload: dict[str, Any],
    log_path: Path | None,
    report_path: Path | None,
    evidence: list[str],
    detail: str = "",
) -> None:
    """The one object a `--json` run writes to stdout.

    The envelope is emitted here rather than by each command so that a failure is structured
    too -- the shape an agent branches on has to exist on the path where something went
    wrong, which is the whole reason it asked for JSON."""

    envelope: dict[str, Any] = {
        "command": report.command,
        "status": report.status,
        "exit_code": report.exit_code,
        "duration_seconds": round(report.metadata.get("duration_seconds", 0.0), 3),
        "log": str(log_path) if log_path else None,
        "report": str(report_path) if report_path else None,
    }
    if detail:
        envelope["detail"] = detail
    if evidence:
        envelope["evidence"] = evidence
    # `setdefault`, not `update`: `status`, `exit_code` and the artifact paths are what a
    # caller branches on, and one command passing `status=` to `_summary` must not be able to
    # rewrite them from underneath it.
    for key, value in payload.items():
        envelope.setdefault(key, value)
    typer.echo(json.dumps(envelope, indent=2, sort_keys=True, default=str))


def _execute(
    state: CliState,
    name: str,
    category: ExitCode,
    action: Callable[[ProjectConfig, ProcessRunner], Any],
    *,
    require_game: bool = False,
    require_work: bool = True,
    require_ue: bool = False,
    activity: bool = False,
) -> Any:
    report = RunReport(command=name, arguments=sys.argv[1:])
    state.payload.clear()
    # In JSON mode stdout belongs to the envelope alone, so the child echo, the progress
    # lines and the error all move to stderr rather than being silenced -- a person
    # debugging a `--json` run still needs to see what happened.
    out = err_console if state.json_output else console
    config: ProjectConfig | None = None
    log_handle = None
    log_path: Path | None = None
    child_echo: _ChildEcho | None = None
    report_path: Path | None = None
    started = datetime.now(timezone.utc).isoformat()
    before = time.monotonic()
    result: Any = None
    failed = False
    code = 0
    detail = ""
    failure_output = ""
    try:
        config = state.resolve(
            require_game=require_game,
            require_work=require_work,
            require_ue=require_ue,
        )
        if config.log_root is not None:
            config.log_root.mkdir(parents=True, exist_ok=True)
            log_path = config.log_root / f"{report.artifact_stem()}.log"
            log_handle = log_path.open("w", encoding="utf-8", newline="\n")
            report.metadata["log"] = str(log_path)
            state.log_sink = lambda text, handle=log_handle: handle.write(text + "\n")
        # Every child gets the filtered echo, not just export and verify: the run log keeps
        # every line, so the console owes only the signal. `--verbose` puts the raw stream
        # back. One sink serves both the child stream and this process's own `print()`, so a
        # command that decodes here and launches an editor reports through one filter.
        sink: Callable[[str], None] | None = None
        signal_sink: Callable[[str], None] | None = None
        # Also when there is no log to write: `--json` promises stdout holds one object, and
        # without the tee an action's bare `print()` lands in front of it. `doctor --repo-only`
        # is exactly that shape -- it needs no work root, so it has no log root either.
        if log_handle is not None or state.json_output:
            if state.verbose:
                sink = signal_sink = lambda line: out.print(
                    line, markup=False, soft_wrap=True)
            else:
                child_echo = _ChildEcho(out, label=name.split(" ", 1)[0])
                sink, signal_sink = child_echo, child_echo.passthrough
        runner = ProcessRunner(
            cwd=config.repo_root,
            environment=os.environ.copy(),
            log=log_handle,
            output_sink=sink,
        )
        lease = nullcontext()
        if activity:
            assert_project_idle(config.project)
            if config.export_root is not None:
                lease = WorkspaceLease(
                    config.export_root,
                    name,
                    config.repo_root,
                )
        with lease:
            with (nullcontext() if sink is None
                  else _teed_stdout(log_handle, sink, signal_sink)):
                result = action(config, runner)
        report.add_task(
            name,
            status="ok",
            started_at=started,
            duration_seconds=time.monotonic() - before,
        )
        report.finish()
    except ConfigError as exc:
        failed, code, detail = True, int(ExitCode.USAGE_OR_CONFIG), str(exc)
    except ProcessFailure as exc:
        failed, code, detail = True, int(exc.category), str(exc)
        failure_output = exc.result.output
    except DependencyError as exc:
        failed, code, detail = True, int(ExitCode.DEPENDENCY_OR_TOOLCHAIN), str(exc)
    except typer.Exit:
        raise
    except Exception as exc:
        failed = True
        code = int(getattr(exc, "exit_code", category))
        detail = str(exc) or type(exc).__name__
    finally:
        if child_echo is not None:
            child_echo.close()
        # Not every way out of that block is an exception this function handles: `typer.Exit`
        # is re-raised just above, and `KeyboardInterrupt` and `SystemExit` are not
        # `Exception` so they were never caught. All three leave without reaching the trailer
        # below, which is what closes the log on every other path -- and a run log left open
        # on Ctrl+C is a run log whose tail never reaches disk. Since the trailer cannot also
        # have run, this closes it exactly once.
        if sys.exc_info()[0] is not None:
            state.log_sink = None
            if log_handle is not None:
                log_handle.close()

    # `ProcessFailure` says only that the child exited non-zero. On its own that is not
    # actionable, and it is all a filtered console would leave behind, so the last distinct
    # signal lines come back with it as bounded evidence.
    evidence: list[str] = []
    if failed:
        if child_echo is not None:
            evidence = child_echo.evidence()
        # Not `elif`: the echo now exists for every command, so an `elif` here would be dead
        # code on the one path it was written for. When the filter's vocabulary does not
        # cover a child -- a toolchain nobody has taught it yet -- the raw tail is what
        # stands between a caller and an exit code with no explanation.
        if not evidence and failure_output:
            evidence = failure_output.splitlines()[-_ECHO_EVIDENCE_LINES:]
        report.add_task(
            name,
            status="failed",
            started_at=started,
            duration_seconds=time.monotonic() - before,
            detail=detail,
        )
        report.finish(exit_code=code)
    if config is not None and config.log_root is not None:
        try:
            report_path = report.write(config.log_root)
        except OSError as exc:
            # The run report is a convenience. The verdict below is not, so an unwritable log
            # root must not replace a real answer with a traceback.
            out.print(f"[dim]run report unavailable: {exc}[/dim]", soft_wrap=True)
    if child_echo is not None:
        counts = child_echo.counts()
        # A command that spawned nothing, or a child that said nothing, adds no key: an
        # envelope of zeroes is noise an agent has to read past on every run.
        if any(counts.values()):
            state.payload.setdefault("child", counts)

    # The log stays open past the echo so the verdict can go into it. A log whose last word
    # is a truncated warning, with the failure only on a console that has since scrolled, is
    # not a record of the run.
    try:
        if failed:
            if state.log_sink is not None:
                state.log_sink(f"error: {detail}")
                for line in evidence:
                    state.log_sink("  " + line)
            out.print("[red]error:[/red] ", end="")
            out.print(detail, markup=False, soft_wrap=True)
            for line in evidence:
                out.print("  " + line, markup=False, soft_wrap=True)
        if report_path:
            out.print(f"[dim]run report: {report_path}[/dim]", soft_wrap=True)
        if log_path:
            out.print(f"[dim]log: {log_path}[/dim]", soft_wrap=True)
    finally:
        state.log_sink = None
        if log_handle is not None:
            log_handle.close()
    if state.json_output and not state.json_emitted:
        _emit_json(report, state.payload, log_path, report_path, evidence, detail)
    if failed:
        raise typer.Exit(code)
    return result


def _shared_cache_root(config: ProjectConfig) -> Path | None:
    """Downloaded dependency archives live in one cache below the work root."""

    return None if config.work_root is None else config.work_root / "cache"


@deps_app.command("sync")
def deps_sync(ctx: typer.Context) -> None:
    def action(config: ProjectConfig, _runner: ProcessRunner) -> None:
        ready = sync_dependencies(config.repo_root, cache_root=_shared_cache_root(config))
        console.print("dependencies ready: " + ", ".join(ready))

    _execute(
        _state(ctx),
        "deps sync",
        ExitCode.DEPENDENCY_OR_TOOLCHAIN,
        action,
        require_work=False,
        activity=True,
    )


@deps_app.command("check")
def deps_check(ctx: typer.Context) -> None:
    def action(config: ProjectConfig, _runner: ProcessRunner) -> None:
        issues = check_dependencies(config.repo_root, load_project_lock(config.repo_root))
        if issues:
            raise DependencyError(
                "; ".join(f"{issue.name}: {issue.detail}" for issue in issues)
            )
        console.print("locked dependencies match")

    _execute(
        _state(ctx),
        "deps check",
        ExitCode.DEPENDENCY_OR_TOOLCHAIN,
        action,
        require_work=False,
    )


def _load_policy(config: ProjectConfig):
    path = config.repo_root / "dev" / "check_repo_policy.py"
    spec = importlib.util.spec_from_file_location("elysium_repo_policy", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load repository policy: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _corpus_index_unclaimed_report(config: ProjectConfig) -> str | None:
    """One warning line naming how many embedded PAKFILE members no seam has claimed yet.

    Counts `members[].embedded[]` rows directly rather than trusting a published `summary`, so
    the line prints against an index built before `summary` carried `embeddedUnclaimed` (an
    index this old lacks the counter; a fresh export always agrees with it). Returns `None` when
    no index exists yet, or it carries no embedded rows to report on.
    """

    if config.export_v2_root is None:
        return None
    from elysium_pipeline.exporters.corpus_index_glb import index_path
    from elysium_pipeline.formats.corpus_index_glb import CORPUS_INDEX_EXTENSION
    from elysium_pipeline.formats.unit_contract import read_glb

    published = index_path(config.export_v2_root)
    if not published.is_file():
        return None
    document, _binary = read_glb(published)
    root = (document.get("extensions") or {}).get(CORPUS_INDEX_EXTENSION)
    if not isinstance(root, dict):
        return None
    members = root.get("members") or ()
    total = 0
    by_extension: dict[str, int] = {}
    for member in members:
        for entry in member.get("embedded") or ():
            if entry.get("asset") is not None:
                continue
            total += 1
            name = str(entry.get("member", "")).replace("\\", "/").rsplit("/", 1)[-1]
            stem, dot, suffix = name.rpartition(".")
            extension = ("." + suffix.lower()) if (dot and stem) else ""
            by_extension[extension] = by_extension.get(extension, 0) + 1
    if total == 0:
        return None
    breakdown = ", ".join(
        f"{count:,} {extension}" for extension, count in sorted(
            by_extension.items(), key=lambda item: -item[1]
        )
    )
    return f"corpus index: {total:,} embedded PAKFILE members unclaimed ({breakdown})"


def _corpus_index_sentinel_report(config: ProjectConfig) -> str | None:
    """One warning line naming how many `vtmb:missing-<kind>:` sentinel references the corpus
    carries -- a reference the referenced kind's own rules make unreachable, resolved `false` and
    carrying no dependency row by contract, so it is invisible to `references[]`,
    `danglingReferences[]` and every cross-unit check built over the graph.

    Reads `summary.sentinelReferences`/`summary.sentinelReferenceUnits` directly rather than
    re-deriving them, so the line prints against an index built before the summary carried the
    counters (an index this old lacks them; a fresh export always agrees with it). These are
    retail data facts, not a corpus defect -- the line is a warning, never a failure.
    """

    if config.export_v2_root is None:
        return None
    from elysium_pipeline.exporters.corpus_index_glb import index_path
    from elysium_pipeline.formats.corpus_index_glb import CORPUS_INDEX_EXTENSION
    from elysium_pipeline.formats.unit_contract import read_glb

    published = index_path(config.export_v2_root)
    if not published.is_file():
        return None
    document, _binary = read_glb(published)
    root = (document.get("extensions") or {}).get(CORPUS_INDEX_EXTENSION)
    if not isinstance(root, dict):
        return None
    summary = root.get("summary") or {}
    total = int(summary.get("sentinelReferences", 0))
    if total == 0:
        return None
    units = int(summary.get("sentinelReferenceUnits", 0))
    return (
        f"corpus index: {total:,} vtmb:missing-* sentinel reference(s) across {units:,} unit(s)"
    )


@app.command("doctor")
def doctor(
    ctx: typer.Context,
    repo_only: bool = typer.Option(False, "--repo-only"),
    json_output: bool = typer.Option(False, "--json"),
    history: bool = typer.Option(False, "--history"),
) -> None:
    state = _state(ctx)
    state.json_output = json_output

    def action(config: ProjectConfig, _runner: ProcessRunner) -> None:
        policy = _load_policy(config)
        errors, warnings = policy.audit(history=history, repo_only=repo_only)
        if not repo_only:
            corpus_warning = _corpus_index_unclaimed_report(config)
            if corpus_warning is not None:
                warnings = [*warnings, corpus_warning]
            sentinel_warning = _corpus_index_sentinel_report(config)
            if sentinel_warning is not None:
                warnings = [*warnings, sentinel_warning]
        state.payload.update(
            {"ok": not errors, "errors": errors, "warnings": warnings}
        )
        if not json_output:
            for warning in warnings:
                console.print(f"[yellow]warning:[/yellow] {warning}")
            for error in errors:
                console.print(f"[red]error:[/red] {error}")
            if not errors:
                console.print(f"repository policy passed ({len(warnings)} warning(s))")
        if errors:
            error = RuntimeError(f"repository policy failed: {len(errors)} error(s)")
            error.exit_code = int(ExitCode.VALIDATION)
            raise error

    _execute(
        _state(ctx),
        "doctor",
        ExitCode.VALIDATION,
        action,
        require_work=not repo_only,
    )


@app.command(
    "build",
    context_settings={"allow_extra_args": True, "ignore_unknown_options": True},
)
def build_command(
    ctx: typer.Context,
    rebuild: bool = typer.Option(False, "--rebuild"),
    clean: bool = typer.Option(False, "--clean"),
    analyze: bool = typer.Option(False, "--analyze"),
    json_output: bool = typer.Option(False, "--json"),
) -> None:
    selected = [
        name for name, enabled in (
            ("rebuild", rebuild),
            ("clean", clean),
            ("analyze", analyze),
        ) if enabled
    ]
    if len(selected) > 1:
        raise typer.BadParameter("--rebuild, --clean, and --analyze are mutually exclusive")
    mode = selected[0] if selected else ""
    state = _state(ctx)
    state.json_output = json_output

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import unreal

        unreal.build(config, runner, mode, ctx.args)
        # UnrealBuildTool says nothing a caller can branch on once its output is filtered,
        # so the command states its own verdict.
        _summary(
            state,
            f"build ok: ElysiumUEEditor Win64 Development ({mode or 'incremental'})",
            mode=mode or "incremental",
        )

    _execute(
        state,
        "build",
        ExitCode.BUILD,
        action,
        require_ue=True,
        activity=True,
    )


@ide_app.command("clangd")
def ide_clangd(ctx: typer.Context) -> None:
    """Regenerate `compile_commands.json` from the target `build` last compiled.

    Run this after adding or removing a C++ source file: clangd has no entry for a file the
    database predates and falls back to guessed flags, which against Unreal reads as a file
    of unresolved headers. It is a separate command because it is not free -- see
    `unreal.generate_clang_database` for why the next build after it is a full one.
    """

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import unreal

        unreal.generate_clang_database(config, runner)

    _execute(
        _state(ctx),
        "ide clangd",
        ExitCode.BUILD,
        action,
        require_ue=True,
        activity=True,
    )


#: `--particles` on every command that bakes a map.
PARTICLE_PASS_HELP = (
    "Author each map's Niagara systems during the bake. Off by default: the pass force-deletes Niagara packages the asset compiler may still own, which crashes the editor. A bake without it leaves the mount's existing particle packages alone."
)


def _export_profile_command(
    ctx: typer.Context,
    profile: str,
    *,
    clean: bool,
    force: bool,
    jobs: int | None,
    particles: bool,
    verify: bool,
) -> None:
    state = _state(ctx)

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        maps = export_manager.export_profile(
            config,
            runner,
            profile,
            clean=clean,
            force=force,
            jobs=jobs,
            particles=particles,
            verify=verify,
            per_task=state.verbose,
        )
        _summary(
            state,
            f"{profile} export complete: {len(maps)} map(s)",
            profile=profile,
            maps=len(maps),
        )

    _execute(
        state,
        f"export {profile}",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        require_ue=True,
        activity=True,
    )


@export_app.command("grid")
def export_grid(
    ctx: typer.Context,
    clean: bool = typer.Option(False, "--clean"),
    force: bool = typer.Option(False, "--force"),
    jobs: int | None = typer.Option(None, "--jobs", min=1),
    particles: bool = typer.Option(False, "--particles", help=PARTICLE_PASS_HELP),
    verify: bool = typer.Option(
        False,
        "--verify",
        help=(
            "Read the baked mount back after the bake (acceptance check). Off by "
            "default: a bake that exits clean and writes its run report is trusted."
        ),
    ),
) -> None:
    _export_profile_command(
        ctx, "grid", clean=clean, force=force, jobs=jobs, particles=particles, verify=verify
    )


@export_app.command("all")
def export_all_command(
    ctx: typer.Context,
    clean: bool = typer.Option(False, "--clean"),
    force: bool = typer.Option(False, "--force"),
    jobs: int | None = typer.Option(None, "--jobs", min=1),
    particles: bool = typer.Option(False, "--particles", help=PARTICLE_PASS_HELP),
    verify: bool = typer.Option(
        False,
        "--verify",
        help=(
            "Read the baked mount back after the bake (acceptance check). Off by "
            "default: a bake that exits clean and writes its run report is trusted."
        ),
    ),
) -> None:
    _export_profile_command(
        ctx, "all", clean=clean, force=force, jobs=jobs, particles=particles, verify=verify
    )


@export_app.command("map")
def export_map(
    ctx: typer.Context,
    maps: list[str] = typer.Argument(...),
    force: bool = typer.Option(False, "--force"),
    intermediate_only: bool = typer.Option(False, "--intermediate-only"),
    particles: bool = typer.Option(False, "--particles", help=PARTICLE_PASS_HELP),
    verify: bool = typer.Option(
        False,
        "--verify",
        help=(
            "Run the deep bake verification commandlet after the bake (acceptance check; "
            "iteration trusts a clean bake exit)."
        ),
    ),
) -> None:
    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        names = export_manager.export_targeted_maps(
            config,
            runner,
            maps,
            force=force,
            intermediate_only=intermediate_only,
            particles=particles,
            verify=verify,
        )
        console.print("map export complete: " + ", ".join(names))

    _execute(
        _state(ctx),
        "export map",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        require_ue=not intermediate_only,
        activity=True,
    )


@verify_app.command("characters")
def verify_characters(
    ctx: typer.Context,
    legacy_root: Path | None = typer.Option(None, "--legacy-root", help="Frozen skeletal product tree."),
    stage_root: Path | None = typer.Option(None, "--stage-root", help="Replacement product tree to compare in order."),
    native: bool = typer.Option(False, "--native", help="Read saved native products (the default when no comparison mode is selected)."),
    fidelity: bool = typer.Option(False, "--fidelity", help="Also compare full geometry snapshots and retained animation samples; deferred fidelity gate."),
    geometry_only: bool = typer.Option(False, "--geometry-only", help="Compare existing hashed native geometry snapshots without launching Unreal."),
    json_output: bool = typer.Option(False, "--json"),
) -> None:
    state = _state(ctx)
    state.json_output = json_output
    native = native or (legacy_root is None and not geometry_only)
    if fidelity and not native:
        raise typer.BadParameter("--fidelity requires native verification")
    if geometry_only and (native or legacy_root is not None):
        raise typer.BadParameter("--geometry-only uses captured stage snapshots; do not combine it with native/legacy selectors")
    if native and legacy_root is not None:
        raise typer.BadParameter("--native uses the character stage's selected units; do not combine it with legacy selectors")
    if not native and not geometry_only and (legacy_root is None) != (stage_root is None):
        raise typer.BadParameter("--legacy-root and --stage-root must be supplied together")

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        if geometry_only:
            from elysium_pipeline.importers.characters import staging_root
            from elysium_pipeline.validation.native_geometry_stage import verify_geometry_stage

            root = stage_root or staging_root(config.work_root)
            geometry = verify_geometry_stage(root, config.export_v2_root)
            _summary(
                state,
                f"captured geometry: {geometry['counts'].get('meshesCompared', 0)} meshes; {root / 'native_geometry_report.json'}",
                mode="geometry-only",
                meshes=geometry["counts"].get("meshesCompared", 0),
                passed=bool(geometry["passed"]),
                geometry_report=str(root / "native_geometry_report.json"),
            )
            if not geometry["passed"]:
                raise RuntimeError("captured geometry verification failed; see native_geometry_report.json")
            return
        if native:
            from elysium_pipeline import unreal
            from elysium_pipeline.importers.characters import staging_root

            root = stage_root or staging_root(config.work_root)
            receipt = root / "native_verify_report.json"
            receipt.unlink(missing_ok=True)
            editor_failure = None
            try:
                unreal.verify_character_stage(config, runner, root / "manifest.json", fidelity=fidelity)
            except unreal.UnrealFailure as error:
                editor_failure = error
            report = _read_json(root / "native_verify_report.json")
            if not report:
                raise RuntimeError("native character verification failed or returned no report")
            if editor_failure or report.get("failed"):
                raise RuntimeError(str(editor_failure or "native character verification failed"))
            if fidelity:
                from elysium_pipeline.validation.native_geometry_stage import verify_geometry_stage

                geometry = verify_geometry_stage(root, config.export_v2_root)
                if not geometry["passed"]:
                    raise RuntimeError("native character geometry verification failed; see native_geometry_report.json")
            _summary(
                state,
                f"native core products: {report['meshes']} meshes, {report['skeletons']} skeletons, "
                f"{report['clips']} clips, {report['blendSpaces']} blend spaces; "
                f"fidelity comparison {'requested' if fidelity else 'deferred'}",
                mode="native",
                meshes=report["meshes"],
                skeletons=report["skeletons"],
                clips=report["clips"],
                blend_spaces=report["blendSpaces"],
                fidelity=bool(fidelity),
            )
            return
        if legacy_root is not None:
            from elysium_pipeline.validation.skeletal_diff import compare_trees, compare_staged_payloads

            staged_manifest = stage_root / "manifest.json"
            staged = _read_json(staged_manifest) if staged_manifest.is_file() else None
            result = (compare_staged_payloads(legacy_root, stage_root, export_root=config.export_v2_root)
                      if staged and staged.get("producer") == "characters"
                      else compare_trees(legacy_root, stage_root))
            report_path = config.work_root / "import" / "characters" / "product_diff.json"
            report_path.parent.mkdir(parents=True, exist_ok=True)
            report_path.write_text(json.dumps(result, indent=2), encoding="utf-8")
            _summary(
                state,
                f"skeletal products: {result['compared']} compared, {result['failed']} failed; {report_path}",
                mode="legacy",
                compared=result["compared"],
                failed=result["failed"],
                passed=bool(result["passed"]),
                diff_report=str(report_path),
            )
            if not result["passed"]:
                raise RuntimeError("skeletal product comparison failed")
            return

    _execute(
        state,
        "verify characters",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=False,
        require_ue=legacy_root is None and not geometry_only,
        require_work=True,
        activity=not geometry_only,
    )


@verify_app.command("model-catalogues")
def verify_model_catalogues(
    ctx: typer.Context,
    json_output: bool = typer.Option(False, "--json"),
) -> None:
    """Verify saved merged model catalogues and references in a fresh editor."""
    state = _state(ctx)
    state.json_output = json_output

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import unreal
        from elysium_pipeline.importers.model_catalogues import staging_root

        root = staging_root(config.work_root)
        receipt = root / "model_catalogues_verify_report.json"
        receipt.unlink(missing_ok=True)
        unreal.model_catalogues(config, runner, root / "manifest.json", verify=True)
        report = _read_json(receipt)
        if not report or not report.get("complete") or report.get("failed"):
            raise RuntimeError("native model catalogue verification failed; see model_catalogues_verify_report.json")
        _summary(
            state,
            "model catalogues and references verified in a fresh editor",
            verified=True,
        )

    _execute(state, "verify model-catalogues", ExitCode.UNREAL_OR_BAKE, action,
             require_work=True, require_ue=True, activity=True)


@verify_app.command("expression-tables")
def verify_expression_tables(
    ctx: typer.Context,
    json_output: bool = typer.Option(False, "--json"),
) -> None:
    """Reload every cooked expression table and compare all retained fields."""
    state = _state(ctx)
    state.json_output = json_output

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import unreal
        from elysium_pipeline.importers.expression_tables import staging_root

        root = staging_root(config.work_root)
        receipt = root / "native_verify_report.json"
        receipt.unlink(missing_ok=True)
        unreal.expression_tables(config, runner, root / "manifest.json", verify=True)
        report = _read_json(receipt)
        if not report or not report.get("complete") or report.get("failed"):
            raise RuntimeError("native expression verification failed or returned no complete report")
        _summary(
            state,
            f"expression tables: {report['verified']} native products verified in a fresh editor",
            verified=report["verified"],
        )

    _execute(state, "verify expression-tables", ExitCode.UNREAL_OR_BAKE, action,
             require_work=True, require_ue=True, activity=True)


@verify_app.command("maps")
def verify_maps(
    ctx: typer.Context,
    maps: list[str] = typer.Argument(None, help="Maps to check. Omit for every baked level."),
    json_output: bool = typer.Option(False, "--json"),
) -> None:
    state = _state(ctx)
    state.json_output = json_output

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        names = export_manager.verify_maps(config, runner, maps)
        _summary(
            state,
            f"map verify complete: {len(names)} map(s)",
            maps=list(names),
            verified=len(names),
        )

    _execute(
        state,
        "verify maps",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        require_ue=True,
        activity=True,
    )


@export_app.command("model")
def export_model(
    ctx: typer.Context,
    model: str = typer.Argument(...),
    animation: str | None = typer.Option(None, "--animation"),
    integrate: bool = typer.Option(False, "--integrate"),
) -> None:
    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destination = export_manager.export_model(
            config,
            runner,
            model,
            animation=animation,
            integrate=integrate,
        )
        console.print(f"model export complete: {destination}")

    _execute(
        _state(ctx),
        "export model",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        require_ue=integrate,
        activity=True,
    )


@export_v2_app.command("texture-glb")
def export_v2_texture_glb(
    ctx: typer.Context,
    texture: str = typer.Argument(
        ...,
        help="VtMB texture path, with optional materials/ prefix and TTH/TTZ suffix.",
    ),
) -> None:
    """Export one complete texture identity to one GLB with one KTX2 payload."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destination = export_manager.export_texture_glb(config, runner, texture)
        console.print(f"texture GLB export complete: {destination}")

    _execute(
        _state(ctx),
        "export_v2 texture-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("textures-glb")
def export_v2_textures_glb(ctx: typer.Context) -> None:
    """Export every patch-first TTH identity through the lossless texture seam."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destinations = export_manager.export_all_texture_glbs(config, runner)
        console.print(f"texture GLB corpus export complete: {len(destinations)} textures")

    _execute(
        _state(ctx),
        "export_v2 textures-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("material-glb")
def export_v2_material_glb(
    ctx: typer.Context,
    material: str = typer.Argument(
        ...,
        help="VtMB material path, with optional materials/ prefix and .vmt suffix.",
    ),
) -> None:
    """Export one complete VMT material identity to one GLB."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destination = export_manager.export_material_glb(config, runner, material)
        console.print(f"material GLB export complete: {destination}")

    _execute(
        _state(ctx),
        "export_v2 material-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("materials-glb")
def export_v2_materials_glb(ctx: typer.Context) -> None:
    """Export every patch-first VMT identity through the lossless material seam."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destinations = export_manager.export_all_material_glbs(config, runner)
        console.print(f"material GLB corpus export complete: {len(destinations)} materials")

    _execute(
        _state(ctx),
        "export_v2 materials-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("surface-property-glb")
def export_v2_surface_property_glb(
    ctx: typer.Context,
    name: str = typer.Argument(..., help="Surface name from scripts/surfaceproperties.txt."),
) -> None:
    """Export one named surface-property table entry to one GLB."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destination = export_manager.export_surface_property_glb(config, runner, name)
        console.print(f"surface-property GLB export complete: {destination}")

    _execute(
        _state(ctx),
        "export_v2 surface-property-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("surface-properties-glb")
def export_v2_surface_properties_glb(ctx: typer.Context) -> None:
    """Export every surface-property table entry through the lossless surface seam."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destinations = export_manager.export_all_surface_property_glbs(config, runner)
        console.print(
            f"surface-property GLB corpus export complete: {len(destinations)} surfaces"
        )

    _execute(
        _state(ctx),
        "export_v2 surface-properties-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("image-glb")
def export_v2_image_glb(
    ctx: typer.Context,
    path: str = typer.Argument(
        ...,
        help="Install-relative `.tga`/`.bmp` path, extension kept.",
    ),
) -> None:
    """Export one TGA or BMP image identity to one GLB."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destination = export_manager.export_image_glb(config, runner, path)
        console.print(f"image GLB export complete: {destination}")

    _execute(
        _state(ctx),
        "export_v2 image-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("images-glb")
def export_v2_images_glb(ctx: typer.Context) -> None:
    """Export every image identity the UP-first install resolves."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destinations = export_manager.export_all_image_glbs(config, runner)
        console.print(f"image GLB corpus export complete: {len(destinations)} images")

    _execute(
        _state(ctx),
        "export_v2 images-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("sound-glb")
def export_v2_sound_glb(
    ctx: typer.Context,
    path: str = typer.Argument(
        ...,
        help="sound/<path>.wav | sound/<path>.mp3 -- the sound/ prefix is tolerated and the "
             "extension is required, because seven stems ship as both spellings.",
    ),
) -> None:
    """Export one WAV or MP3 sound identity to one GLB."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destination = export_manager.export_sound_glb(config, runner, path)
        console.print(f"sound GLB export complete: {destination}")

    _execute(
        _state(ctx),
        "export_v2 sound-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("sounds-glb")
def export_v2_sounds_glb(ctx: typer.Context) -> None:
    """Export every `.wav` and `.mp3` the UP-first index resolves below `sound/`."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destinations = export_manager.export_all_sound_glbs(config, runner)
        console.print(f"sound GLB corpus export complete: {len(destinations)} sounds")

    _execute(
        _state(ctx),
        "export_v2 sounds-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("expression-table-glb")
def export_v2_expression_table_glb(
    ctx: typer.Context,
    stem: str = typer.Argument(
        ...,
        help="Faceposer expression-table stem below expressions/, with or without the "
             ".vfe/.txt extension or the expressions/ prefix.",
    ),
) -> None:
    """Export one Faceposer expression table to one GLB."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destination = export_manager.export_expression_table_glb(config, runner, stem)
        console.print(f"expression-table GLB export complete: {destination}")

    _execute(
        _state(ctx),
        "export_v2 expression-table-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("expression-tables-glb")
def export_v2_expression_tables_glb(ctx: typer.Context) -> None:
    """Export every stem the UP-first install resolves under expressions/."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destinations = export_manager.export_all_expression_table_glbs(config, runner)
        console.print(f"expression-table GLB corpus export complete: {len(destinations)} tables")

    _execute(
        _state(ctx),
        "export_v2 expression-tables-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("shader-source-glb")
def export_v2_shader_source_glb(
    ctx: typer.Context,
    stem: str = typer.Argument(
        ...,
        help="<stem> -- tolerates the materials/dxshaders/ root and the .psh extension "
             "(eyes, dxshaders/eyes, materials/dxshaders/Eyes.psh).",
    ),
) -> None:
    """Export one authored pixel-shader source to one GLB."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destination = export_manager.export_shader_source_glb(config, runner, stem)
        console.print(f"shader-source GLB export complete: {destination}")

    _execute(
        _state(ctx),
        "export_v2 shader-source-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("shader-sources-glb")
def export_v2_shader_sources_glb(ctx: typer.Context) -> None:
    """Export every materials/dxshaders/*.psh the index resolves."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destinations = export_manager.export_all_shader_source_glbs(config, runner)
        console.print(f"shader-source GLB corpus export complete: {len(destinations)} sources")

    _execute(
        _state(ctx),
        "export_v2 shader-sources-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("shader-program-glb")
def export_v2_shader_program_glb(
    ctx: typer.Context,
    key: str = typer.Argument(
        ...,
        help="<subdir>/<stem> -- tolerates the shaders/ root and the .vcs extension "
             "(psh/lightmappedgeneric, shaders/fxc/refract_ps20.vcs).",
    ),
) -> None:
    """Export one compiled shader program to one GLB."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destination = export_manager.export_shader_program_glb(config, runner, key)
        console.print(f"shader-program GLB export complete: {destination}")

    _execute(
        _state(ctx),
        "export_v2 shader-program-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("shader-programs-glb")
def export_v2_shader_programs_glb(ctx: typer.Context) -> None:
    """Export every shaders/{psh,vsh,fxc}/*.vcs the index resolves."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destinations = export_manager.export_all_shader_program_glbs(config, runner)
        console.print(f"shader-program GLB corpus export complete: {len(destinations)} programs")

    _execute(
        _state(ctx),
        "export_v2 shader-programs-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("particle-glb")
def export_v2_particle_glb(
    ctx: typer.Context,
    name: str = typer.Argument(
        ...,
        help="particles/<name>.txt (tolerates the particles/ prefix and the .txt extension).",
    ),
) -> None:
    """Export one particle-system definition to one GLB."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destination = export_manager.export_particle_glb(config, runner, name)
        console.print(f"particle GLB export complete: {destination}")

    _execute(
        _state(ctx),
        "export_v2 particle-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("particles-glb")
def export_v2_particles_glb(ctx: typer.Context) -> None:
    """Export every particle key source_keys(index) resolves."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destinations = export_manager.export_all_particle_glbs(config, runner)
        console.print(f"particle GLB corpus export complete: {len(destinations)} particles")

    _execute(
        _state(ctx),
        "export_v2 particles-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("font-glb")
def export_v2_font_glb(
    ctx: typer.Context,
    key: str = typer.Argument(
        ...,
        help="<key> -- a materials/fonts/<stem>.fnt identity; the materials/fonts/ root and "
             "the .fnt extension are optional on the argument.",
    ),
) -> None:
    """Export one bitmap font face to one GLB."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destination = export_manager.export_font_glb(config, runner, key)
        console.print(f"font GLB export complete: {destination}")

    _execute(
        _state(ctx),
        "export_v2 font-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("fonts-glb")
def export_v2_fonts_glb(ctx: typer.Context) -> None:
    """Export every `.fnt` the install resolves, plus the fontlist registry unit."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destinations = export_manager.export_all_font_glbs(config, runner)
        console.print(f"font GLB corpus export complete: {len(destinations)} fonts")

    _execute(
        _state(ctx),
        "export_v2 fonts-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("font-list-glb")
def export_v2_font_list_glb(ctx: typer.Context) -> None:
    """Export the single vtmb:font-list:fontlist registry unit."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destination = export_manager.export_font_list_glb(config, runner)
        console.print(f"font-list GLB export complete: {destination}")

    _execute(
        _state(ctx),
        "export_v2 font-list-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("sound-script-glb")
def export_v2_sound_script_glb(
    ctx: typer.Context,
    name: str = typer.Argument(..., help="<name> -- a game-sound entry name, case-folded."),
) -> None:
    """Export one named game-sound script entry to one GLB."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destination = export_manager.export_sound_script_glb(config, runner, name)
        console.print(f"sound-script GLB export complete: {destination}")

    _execute(
        _state(ctx),
        "export_v2 sound-script-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("sound-scripts-glb")
def export_v2_sound_scripts_glb(ctx: typer.Context) -> None:
    """Export every game sound, the game-sound manifest and every soundscape."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destinations = export_manager.export_all_sound_script_glbs(config, runner)
        console.print(f"sound-script GLB corpus export complete: {len(destinations)} units")

    _execute(
        _state(ctx),
        "export_v2 sound-scripts-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("sentence-glb")
def export_v2_sentence_glb(
    ctx: typer.Context,
    name: str = typer.Argument(..., help="<name> -- a scripts/sentences.txt entry name."),
) -> None:
    """Export one sentence-table entry to one GLB."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destination = export_manager.export_sentence_glb(config, runner, name)
        console.print(f"sentence GLB export complete: {destination}")

    _execute(
        _state(ctx),
        "export_v2 sentence-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("sentences-glb")
def export_v2_sentences_glb(ctx: typer.Context) -> None:
    """Export every entry of scripts/sentences.txt."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destinations = export_manager.export_all_sentence_glbs(config, runner)
        console.print(f"sentence GLB corpus export complete: {len(destinations)} sentences")

    _execute(
        _state(ctx),
        "export_v2 sentences-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("dsp-preset-glb")
def export_v2_dsp_preset_glb(
    ctx: typer.Context,
    preset_id: str = typer.Argument(..., help="<id> -- a scripts/dsp_presets.txt preset number."),
) -> None:
    """Export one numbered DSP preset to one GLB."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destination = export_manager.export_dsp_preset_glb(config, runner, preset_id)
        console.print(f"dsp-preset GLB export complete: {destination}")

    _execute(
        _state(ctx),
        "export_v2 dsp-preset-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("dsp-presets-glb")
def export_v2_dsp_presets_glb(ctx: typer.Context) -> None:
    """Export every entry of scripts/dsp_presets.txt."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destinations = export_manager.export_all_dsp_preset_glbs(config, runner)
        console.print(f"dsp-preset GLB corpus export complete: {len(destinations)} presets")

    _execute(
        _state(ctx),
        "export_v2 dsp-presets-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("sound-scheme-glb")
def export_v2_sound_scheme_glb(
    ctx: typer.Context,
    stem: str = typer.Argument(
        ...,
        help="<stem> | sound/schemes/<stem>.txt (root prefix and .txt extension tolerated, "
             "case-insensitive).",
    ),
) -> None:
    """Export one music-and-ambience sound scheme to one GLB."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destination = export_manager.export_sound_scheme_glb(config, runner, stem)
        console.print(f"sound-scheme GLB export complete: {destination}")

    _execute(
        _state(ctx),
        "export_v2 sound-scheme-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("sound-schemes-glb")
def export_v2_sound_schemes_glb(ctx: typer.Context) -> None:
    """Export every sound/schemes/*.txt identity."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destinations = export_manager.export_all_sound_scheme_glbs(config, runner)
        console.print(f"sound-scheme GLB corpus export complete: {len(destinations)} schemes")

    _execute(
        _state(ctx),
        "export_v2 sound-schemes-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("scene-glb")
def export_v2_scene_glb(
    ctx: typer.Context,
    path: str = typer.Argument(
        ...,
        help="sound/<path>.vcd (the root prefix and the extension are tolerated).",
    ),
) -> None:
    """Export one choreography scene to one GLB."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destination = export_manager.export_scene_glb(config, runner, path)
        console.print(f"scene GLB export complete: {destination}")

    _execute(
        _state(ctx),
        "export_v2 scene-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("scenes-glb")
def export_v2_scenes_glb(ctx: typer.Context) -> None:
    """Export every `.vcd` choreography the index resolves."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destinations = export_manager.export_all_scene_glbs(config, runner)
        console.print(f"scene GLB corpus export complete: {len(destinations)} scenes")

    _execute(
        _state(ctx),
        "export_v2 scenes-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("model-glb")
def export_v2_model_glb(
    ctx: typer.Context,
    model: str = typer.Argument(
        ...,
        help="models/<model>.mdl -- the root prefix and the .mdl extension are both optional.",
    ),
) -> None:
    """Export one complete MDL model identity to one GLB."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destination = export_manager.export_model_glb(config, runner, model)
        console.print(f"model GLB export complete: {destination}")

    _execute(
        _state(ctx),
        "export_v2 model-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("models-glb")
def export_v2_models_glb(ctx: typer.Context) -> None:
    """Export every models/**.mdl the UP-first index resolves."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destinations = export_manager.export_all_model_glbs(config, runner)
        console.print(f"model GLB corpus export complete: {len(destinations)} models")

    _execute(
        _state(ctx),
        "export_v2 models-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("dialogue-glb")
def export_v2_dialogue_glb(
    ctx: typer.Context,
    path: str = typer.Argument(
        ...,
        help="dlg/<path>.dlg (root prefix and .dlg extension tolerated; slashes and case folded).",
    ),
) -> None:
    """Export one dialogue tree to one GLB."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destination = export_manager.export_dialogue_glb(config, runner, path)
        console.print(f"dialogue GLB export complete: {destination}")

    _execute(
        _state(ctx),
        "export_v2 dialogue-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("dialogues-glb")
def export_v2_dialogues_glb(ctx: typer.Context) -> None:
    """Export every dialogue key source_keys(index) resolves."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destinations = export_manager.export_all_dialogue_glbs(config, runner)
        console.print(f"dialogue GLB corpus export complete: {len(destinations)} dialogues")

    _execute(
        _state(ctx),
        "export_v2 dialogues-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("vdata-glb")
def export_v2_vdata_glb(
    ctx: typer.Context,
    key: str = typer.Argument(
        ...,
        help="<subtree>/<name> (tolerates a leading vdata/ and a trailing .txt, e.g. "
             "items/item_w_katana or vdata/items/item_w_katana.txt).",
    ),
) -> None:
    """Export one vdata table to one GLB."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destination = export_manager.export_vdata_glb(config, runner, key)
        console.print(f"vdata GLB export complete: {destination}")

    _execute(
        _state(ctx),
        "export_v2 vdata-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("vdatas-glb")
def export_v2_vdatas_glb(ctx: typer.Context) -> None:
    """Export every vdata/<subtree>/<name>.txt identity."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destinations = export_manager.export_all_vdata_glbs(config, runner)
        console.print(f"vdata GLB corpus export complete: {len(destinations)} units")

    _execute(
        _state(ctx),
        "export_v2 vdatas-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


def _run_corpus_lane(config: ProjectConfig, deploy, lane: str) -> None:
    """Run one loose-corpus import lane and report it, the same way for every lane.

    The lanes differ only in which units they read and where the bytes land
    (`importers/corpus_deploy.py`); the command surface -- resolve the export root, deploy, print
    one line, name up to ten failures and exit non-zero if there were any -- is one rule.
    """

    from elysium_pipeline.importers import corpus_deploy

    if config.export_v2_root is None:
        raise ConfigError(
            "ELYSIUM_EXPORT_V2_ROOT is not configured; copy dev/paths.example.env to "
            ".elysium.local.env and set the local path"
        )
    result = deploy(config.export_v2_root, corpus_deploy.corpus_root(config.repo_root))
    console.print(result.summary())
    console.print(
        f"  report: {corpus_deploy.bookkeeping_root(result.destination_root, lane) / 'import_report.json'}"
    )
    if result.failures:
        for key, detail in result.failures[:10]:
            console.print(f"[yellow]  {key}: {detail}[/yellow]", markup=True)
        raise RuntimeError(f"{len(result.failures)} {lane} unit(s) could not be imported")


@import_app.command("vdata")
def import_vdata(ctx: typer.Context) -> None:
    """Deploy the vdata corpus from the published GLB units into Content/ElysiumCorpus."""

    def action(config: ProjectConfig, _runner: ProcessRunner) -> None:
        from elysium_pipeline.importers import vdata as importer

        if config.export_v2_root is None:
            raise ConfigError(
                "ELYSIUM_EXPORT_V2_ROOT is not configured; copy dev/paths.example.env to "
                ".elysium.local.env and set the local path"
            )
        result = importer.import_vdata(
            config.export_v2_root, importer.corpus_root(config.repo_root)
        )
        console.print(result.summary())
        if result.failures:
            for key, detail in result.failures[:10]:
                console.print(f"[yellow]  {key}: {detail}[/yellow]", markup=True)
            raise RuntimeError(f"{len(result.failures)} vdata unit(s) could not be imported")

    # No install, no engine and no generated-state lease: the units are self-contained and the
    # destination is loose text nothing bakes from, so the deploy is a file copy and nothing more.
    _execute(_state(ctx), "import vdata", ExitCode.OFFLINE_EXPORT, action, require_work=False)


@import_app.command("dialogue")
def import_dialogue(ctx: typer.Context) -> None:
    """Deploy the dialogue corpus -- `.dlg` files and `.vcd` scenes -- into Content/ElysiumCorpus."""

    def action(config: ProjectConfig, _runner: ProcessRunner) -> None:
        from elysium_pipeline.importers import dialogue as importer

        _run_corpus_lane(config, importer.import_dialogue, "dialogue")

    # No install, no engine and no generated-state lease: the units are self-contained and the
    # destination is loose text nothing bakes from, so the deploy is a file copy and nothing more.
    _execute(_state(ctx), "import dialogue", ExitCode.OFFLINE_EXPORT, action, require_work=False)


@import_app.command("sound")
def import_sound(ctx: typer.Context) -> None:
    """Deploy the sound corpus's `.lip` sidecars into Content/ElysiumCorpus (audio is baked)."""

    def action(config: ProjectConfig, _runner: ProcessRunner) -> None:
        from elysium_pipeline.importers import sound as importer

        _run_corpus_lane(config, importer.import_sound, "sound")

    _execute(_state(ctx), "import sound", ExitCode.OFFLINE_EXPORT, action, require_work=False)


@import_app.command("sound-schemes")
def import_sound_schemes(ctx: typer.Context) -> None:
    """Deploy the sound-scheme corpus -- `sound/schemes/*.txt` -- into Content/ElysiumCorpus."""

    def action(config: ProjectConfig, _runner: ProcessRunner) -> None:
        from elysium_pipeline.importers import sound_schemes as importer

        _run_corpus_lane(config, importer.import_sound_schemes, "sound-schemes")

    _execute(
        _state(ctx), "import sound-schemes", ExitCode.OFFLINE_EXPORT, action, require_work=False
    )


@import_app.command("textures")
def import_textures(
    ctx: typer.Context,
    force: bool = typer.Option(
        False, "--force", help="Re-import every asset even when its recipe stamp is current."
    ),
    select: str | None = typer.Option(
        None, "--select", help="Only units whose key starts with this prefix (e.g. hud/signs)."
    ),
    stage_only: bool = typer.Option(
        False, "--stage-only", help="Write the DDS staging tree and manifest; launch no editor."
    ),
    measure: bool = typer.Option(
        True, "--measure/--no-measure",
        help="Write each built asset's mip 0 back and report the re-encode texel delta.",
    ),
) -> None:
    """Import the texture corpus from the published GLB units into /ElysiumBaked/Textures."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import unreal
        from elysium_pipeline.importers import textures as importer

        if config.export_v2_root is None or config.work_root is None:
            raise ConfigError(
                "ELYSIUM_EXPORT_V2_ROOT and ELYSIUM_WORK_ROOT must be configured; copy "
                "dev/paths.example.env to .elysium.local.env and set the local paths"
            )
        root = importer.staging_root(config.work_root)
        staged = importer.stage_textures(config.export_v2_root, root, select=select)
        console.print(staged.summary())
        for key, detail in staged.failures[:10]:
            console.print(f"[yellow]  {key}: {detail}[/yellow]", markup=True)
        for key, detail in staged.material_failures[:5]:
            console.print(f"[yellow]  material {key}: {detail}[/yellow]", markup=True)
        if stage_only:
            if staged.failures:
                raise RuntimeError(f"{len(staged.failures)} texture unit(s) could not be staged")
            return

        editor_failure: Exception | None = None
        try:
            unreal.import_textures(config, runner, staged.manifest_path, force=force,
                                   measure=measure)
        except unreal.UnrealFailure as error:
            editor_failure = error
        report = _read_json(root / importer.IMPORT_REPORT_NAME)
        failed_assets = (report.get("failed") or []) if report else []
        if report:
            console.print(
                "texture import: "
                f"{report.get('imported', 0)} imported, {report.get('reused', 0)} reused, "
                f"{report.get('pruned', 0)} pruned, {len(failed_assets)} failed"
            )
            for row in failed_assets[:10]:
                console.print(
                    f"[yellow]  {row.get('assetPath')}: {row.get('reason')}[/yellow]", markup=True
                )
        if measure:
            measured = importer.measure_textures(root)
            console.print(measured.summary())
            for row in measured.worst[:10]:
                console.print(
                    f"  {row['unit']}: max {row['maxDelta']} mean {row['meanDelta']:.3f}"
                )
        problems = []
        if staged.failures:
            problems.append(f"{len(staged.failures)} unit(s) could not be staged")
        if failed_assets:
            problems.append(f"{len(failed_assets)} asset(s) failed to import")
        if editor_failure is not None:
            problems.append(str(editor_failure))
        if problems:
            raise RuntimeError("; ".join(problems))

    # The stage is a file transform over the published units; the editor phase needs the engine
    # and the work root, never the game install: the units are self-contained.
    _execute(
        _state(ctx),
        "import textures",
        ExitCode.OFFLINE_EXPORT if stage_only else ExitCode.UNREAL_OR_BAKE,
        action,
        require_work=True,
        require_ue=not stage_only,
        activity=not stage_only,
    )


@import_app.command("surface-properties")
def import_surface_properties(
    ctx: typer.Context,
    force: bool = typer.Option(
        False, "--force", help="Re-author every asset even when its recipe stamp is current."
    ),
    stage_only: bool = typer.Option(
        False, "--stage-only", help="Write the sidecars and manifest; launch no editor."
    ),
) -> None:
    """Import the surface-property table from the published GLB units as physical materials."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import unreal
        from elysium_pipeline.importers import surface_properties as importer

        if config.export_v2_root is None or config.work_root is None:
            raise ConfigError(
                "ELYSIUM_EXPORT_V2_ROOT and ELYSIUM_WORK_ROOT must be configured; copy "
                "dev/paths.example.env to .elysium.local.env and set the local paths"
            )
        root = importer.staging_root(config.work_root)
        staged = importer.stage_surface_properties(config.export_v2_root, root)
        console.print(staged.summary())
        for key, detail in staged.failures[:10]:
            console.print(f"[yellow]  {key}: {detail}[/yellow]", markup=True)
        if stage_only:
            if staged.failures:
                raise RuntimeError(
                    f"{len(staged.failures)} surface-property unit(s) could not be staged")
            return

        editor_failure: Exception | None = None
        try:
            unreal.import_surface_properties(config, runner, staged.manifest_path, force=force)
        except unreal.UnrealFailure as error:
            editor_failure = error
        report = _read_json(root / importer.IMPORT_REPORT_NAME)
        failed_assets = (report.get("failed") or []) if report else []
        if report:
            console.print(
                "surface-property import: "
                f"{report.get('imported', 0)} imported, {report.get('reused', 0)} reused, "
                f"{report.get('pruned', 0)} pruned, {len(failed_assets)} failed"
            )
            for row in failed_assets[:10]:
                console.print(
                    f"[yellow]  {row.get('assetPath')}: {row.get('reason')}[/yellow]", markup=True
                )
        problems = []
        if staged.failures:
            problems.append(f"{len(staged.failures)} unit(s) could not be staged")
        if failed_assets:
            problems.append(f"{len(failed_assets)} asset(s) failed to import")
        if editor_failure is not None:
            problems.append(str(editor_failure))
        if problems:
            raise RuntimeError("; ".join(problems))

    # The stage is a file transform over the published units; the editor phase needs the engine
    # and the work root, never the game install: the units are self-contained.
    _execute(
        _state(ctx),
        "import surface-properties",
        ExitCode.OFFLINE_EXPORT if stage_only else ExitCode.UNREAL_OR_BAKE,
        action,
        require_work=True,
        require_ue=not stage_only,
        activity=not stage_only,
    )


@bake_app.command("map")
def bake_map(
    ctx: typer.Context,
    maps: list[str] = typer.Option(
        ...,
        "--maps",
        help=(
            "Map stem to bake (repeatable: --maps sp_tutorial_1 --maps sm_hub_1). Required -- "
            "the lane refuses to run unscoped. The map must be listed under MapsOnV2Models."
        ),
    ),
    force: bool = typer.Option(
        False, "--force", help="Rebake the level even when its recipe stamp is current."
    ),
    particles: bool = typer.Option(False, "--particles", help=PARTICLE_PASS_HELP),
    verify: bool = typer.Option(
        False,
        "--verify",
        help=(
            "Run the deep bake verification commandlet after the bake (acceptance check; "
            "iteration trusts a clean bake exit)."
        ),
    ),
) -> None:
    """Author each named map's level from its published V2 units: stage the root unit, run
    `bake_map.py`, nothing else. `export map` is the legacy-lane orchestrator around the same
    bake."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        names = export_manager.bake_v2_maps(
            config, runner, maps, force=force, particles=particles, verify=verify)
        console.print("map bake complete: " + ", ".join(names))

    _execute(
        _state(ctx),
        "bake map",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        require_ue=True,
        activity=True,
    )


@bake_app.command("sounds")
def bake_sounds(
    ctx: typer.Context,
    keys: list[str] = typer.Option(
        None,
        "--keys",
        help=(
            "Only these sound units, by key with the extension kept "
            "(interface/bubble_click_on.wav). Repeatable, and each value may be a "
            "comma-separated list. Omit for the whole corpus, which is also the only "
            "scope that prunes."
        ),
    ),
    force: bool = typer.Option(
        False, "--force", help="Re-import every asset even when its recipe stamp is current."
    ),
    stage_only: bool = typer.Option(
        False, "--stage-only", help="Write the staged audio and manifest; launch no editor."
    ),
) -> None:
    """Bake the V2 sound units to `USoundWave` assets under `/ElysiumBaked/Sounds`."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import unreal
        from elysium_pipeline.importers import sounds_bake as importer

        if config.export_v2_root is None or config.work_root is None:
            raise ConfigError(
                "ELYSIUM_EXPORT_V2_ROOT and ELYSIUM_WORK_ROOT must be configured; copy "
                "dev/paths.example.env to .elysium.local.env and set the local paths"
            )
        root = importer.staging_root(config.work_root)
        selection = importer.parse_keys(keys)
        staged = importer.stage_sounds(config.export_v2_root, root, keys=selection)
        console.print(staged.summary())
        for key, detail in staged.failures[:10]:
            console.print(f"[yellow]  {key}: {detail}[/yellow]", markup=True)
        for row in staged.empty[:10]:
            console.print(f"[yellow]  empty: {row['unit']} ({row['reason']})[/yellow]",
                          markup=True)
        for row in staged.placeholders[:10]:
            console.print(
                f"[yellow]  {row['disposition']}: {row['unit']} -> {row['assetPath']} "
                f"({row['sampleCount']} silent samples; {row['reason']})[/yellow]", markup=True)
        if len(staged.placeholders) > 10:
            console.print(f"  ... and {len(staged.placeholders) - 10} more")
        if stage_only:
            if staged.failures:
                raise RuntimeError(f"{len(staged.failures)} sound unit(s) could not be staged")
            return

        editor_failure: Exception | None = None
        try:
            unreal.bake_sounds(config, runner, staged.manifest_path, force=force)
        except unreal.UnrealFailure as error:
            editor_failure = error
        report = _read_json(root / importer.IMPORT_REPORT_NAME)
        failed_assets = (report.get("failed") or []) if report else []
        if report:
            console.print(
                "sound bake: "
                f"{report.get('imported', 0)} imported, {report.get('reused', 0)} reused, "
                f"{report.get('pruned', 0)} pruned, {len(report.get('empty') or [])} empty, "
                f"{len(report.get('placeholders') or [])} placeholder, "
                f"{len(failed_assets)} failed"
            )
            loops = report.get("loops") or {}
            if loops:
                console.print("  loop decisions: " + ", ".join(
                    f"{count} {name}" for name, count in sorted(loops.items())))
            for row in failed_assets[:10]:
                console.print(
                    f"[yellow]  {row.get('assetPath')}: {row.get('reason')}[/yellow]", markup=True
                )
        problems = []
        if staged.failures:
            problems.append(f"{len(staged.failures)} unit(s) could not be staged")
        if failed_assets:
            problems.append(f"{len(failed_assets)} asset(s) failed to import")
        if editor_failure is not None:
            problems.append(str(editor_failure))
        if problems:
            raise RuntimeError("; ".join(problems))

    # The stage is a file transform over the published units; the editor phase needs the engine
    # and the work root, never the game install: the units are self-contained.
    _execute(
        _state(ctx),
        "bake sounds",
        ExitCode.OFFLINE_EXPORT if stage_only else ExitCode.UNREAL_OR_BAKE,
        action,
        require_work=True,
        require_ue=not stage_only,
        activity=not stage_only,
    )


@import_app.command("materials")
def import_materials(
    ctx: typer.Context,
    force: bool = typer.Option(
        False, "--force", help="Re-import every asset even when its recipe stamp is current."
    ),
    select: str | None = typer.Option(
        None, "--select", help="Only units whose key starts with this prefix (e.g. brick)."
    ),
    stage_only: bool = typer.Option(
        False, "--stage-only", help="Write the manifest and provenance sidecars; launch no editor."
    ),
    lookdev: bool = typer.Option(
        False, "--lookdev",
        help="Run only SF-4.7's lookdev map generator; skip staging and instance import.",
    ),
    lookdev_set: str | None = typer.Option(
        None, "--lookdev-set",
        help="Only with --lookdev: review-set JSON to use instead of the tracked "
        "pipeline/unreal/lookdev_set.json.",
    ),
    lookdev_props_set: str | None = typer.Option(
        None, "--lookdev-props-set",
        help="Only with --lookdev: props row-set JSON to use instead of the tracked "
        "pipeline/unreal/lookdev_props_set.json.",
    ),
    lookdev_allow_missing: bool = typer.Option(
        False, "--lookdev-allow-missing",
        help="Only with --lookdev: do not fail when a review-set entry's MI_ is not imported "
        "yet; it is still placed on a loud placeholder.",
    ),
) -> None:
    """Import the material corpus from the published GLB units into /ElysiumBaked/Materials."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import unreal

        if lookdev:
            if select or force:
                raise ConfigError(
                    "--lookdev generates the review map only; it does not stage or import, so "
                    "--select and --force do not apply -- drop them (or --lookdev-set if you "
                    "meant to narrow the review set, not the corpus)"
                )
            # SF-4.7's generator needs the engine and the work root (for the report) but neither
            # the published GLB corpus nor a texture staging tree: it only lays out and saves a
            # map.
            report = unreal.make_lookdev_map(
                config, runner, set_path=lookdev_set, props_set_path=lookdev_props_set,
                allow_missing=lookdev_allow_missing
            )
            if report:
                missing = report.get("missing") or []
                props_missing = report.get("propsMissing") or []
                console.print(
                    f"lookdev map generated: {report.get('placed', 0)} placed, "
                    f"{len(missing)} missing; {report.get('propsPlaced', 0)} props placed, "
                    f"{len(props_missing)} props missing"
                )
                for row in missing[:10]:
                    console.print(f"[yellow]  {row.get('label')}: {row.get('unit')}[/yellow]",
                                  markup=True)
                for row in props_missing[:10]:
                    console.print(f"[yellow]  {row.get('label')}: {row.get('unit')}[/yellow]",
                                  markup=True)
            else:
                console.print("lookdev map generated")
            return

        from elysium_pipeline.importers import materials as importer
        from elysium_pipeline.importers import textures as texture_importer

        if config.export_v2_root is None or config.work_root is None:
            raise ConfigError(
                "ELYSIUM_EXPORT_V2_ROOT and ELYSIUM_WORK_ROOT must be configured; copy "
                "dev/paths.example.env to .elysium.local.env and set the local paths"
            )
        root = importer.staging_root(config.work_root)
        texture_root = texture_importer.staging_root(config.work_root)
        texture_staging_root = texture_root if texture_root.is_dir() else None
        staged = importer.stage_materials(
            config.export_v2_root, root, select=select, texture_staging_root=texture_staging_root
        )
        console.print(staged.summary())
        for key, detail in staged.failures[:10]:
            console.print(f"[yellow]  {key}: {detail}[/yellow]", markup=True)
        if stage_only:
            if staged.failures:
                raise RuntimeError(f"{len(staged.failures)} material unit(s) could not be staged")
            return

        editor_failure: Exception | None = None
        try:
            unreal.import_materials(config, runner, staged.manifest_path, force=force)
        except unreal.UnrealFailure as error:
            editor_failure = error
        report = _read_json(root / importer.IMPORT_REPORT_NAME)
        failed_assets = (report.get("failed") or []) if report else []
        if report:
            console.print(
                "material import: "
                f"{report.get('imported', 0)} imported, {report.get('reused', 0)} reused, "
                f"{report.get('pruned', 0)} pruned, {len(failed_assets)} failed"
            )
            for row in failed_assets[:10]:
                console.print(
                    f"[yellow]  {row.get('assetPath')}: {row.get('reason')}[/yellow]", markup=True
                )
            anomaly_counts = report.get("anomalyCounts") or {}
            if anomaly_counts:
                rollup = ", ".join(f"{kind}={count}" for kind, count in sorted(anomaly_counts.items()))
                console.print(f"  anomalies: {rollup}")
        problems = []
        if staged.failures:
            problems.append(f"{len(staged.failures)} unit(s) could not be staged")
        if failed_assets:
            problems.append(f"{len(failed_assets)} asset(s) failed to import")
        if editor_failure is not None:
            problems.append(str(editor_failure))
        if problems:
            raise RuntimeError("; ".join(problems))

    # The stage is a file transform over the published units; the editor phase needs the engine
    # and the work root, never the game install: the units are self-contained. --lookdev always
    # needs the engine (it launches an editor process), regardless of --stage-only.
    _execute(
        _state(ctx),
        "import materials",
        ExitCode.OFFLINE_EXPORT if (stage_only and not lookdev) else ExitCode.UNREAL_OR_BAKE,
        action,
        require_work=True,
        require_ue=lookdev or not stage_only,
        activity=lookdev or not stage_only,
    )


@import_app.command("model-catalogues")
def import_model_catalogues(
    ctx: typer.Context,
    stage_only: bool = typer.Option(False, "--stage-only"),
    force: bool = typer.Option(False, "--force"),
) -> None:
    """Merge the complete static/skeletal producer inventories into cooked model catalogues."""
    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline.native_model_pipeline import import_model_catalogues as run_import

        run_import(config, runner, stage_only=stage_only, force=force, log=console.print)

    _execute(_state(ctx), "import model-catalogues", ExitCode.OFFLINE_EXPORT if stage_only else ExitCode.UNREAL_OR_BAKE,
             action, require_work=True, require_ue=not stage_only, activity=True)


@import_app.command("expression-tables")
def import_expression_tables(
    ctx: typer.Context,
    stage_only: bool = typer.Option(False, "--stage-only"),
    force: bool = typer.Option(False, "--force"),
) -> None:
    """Stage and cook the complete expression GLB corpus, including unused tables."""
    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline.native_model_pipeline import import_expression_tables as run_import

        run_import(config, runner, stage_only=stage_only, force=force, log=console.print)

    _execute(_state(ctx), "import expression-tables", ExitCode.OFFLINE_EXPORT if stage_only else ExitCode.UNREAL_OR_BAKE,
             action, require_work=True, require_ue=not stage_only, activity=True)


@import_app.command("cook-roots")
def import_cook_roots(
    ctx: typer.Context,
    force: bool = typer.Option(False, "--force"),
) -> None:
    """Publish cook references after character, model, expression and catalogue imports."""
    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import unreal
        from elysium_pipeline.importers import characters, models, expression_tables, model_catalogues

        root = config.work_root / "import" / "cook-roots"
        root.mkdir(parents=True, exist_ok=True)
        manifests = {name: str(module.staging_root(config.work_root) / "manifest.json")
                     for name, module in (("characters", characters), ("models", models),
                                          ("expressions", expression_tables), ("catalogues", model_catalogues))}
        for path in manifests.values():
            if not Path(path).is_file():
                raise RuntimeError("import the producer before publishing cook roots: " + path)
        job = root / "job.json"
        job.write_text(json.dumps({"manifestPaths": manifests, "force": force}, indent=2), encoding="utf-8")
        receipt = root / "cook_root_import_report.json"
        receipt.unlink(missing_ok=True)
        unreal.import_cook_roots(config, runner, job)
        report = _read_json(receipt)
        if not report or not report.get("complete"):
            raise RuntimeError("cook-root publication failed; see " + str(receipt))
        console.print(f"cook root {report['outcome']}: {report['assetPath']}")

    _execute(_state(ctx), "import cook-roots", ExitCode.UNREAL_OR_BAKE, action,
             require_work=True, require_ue=True, activity=True)


@import_app.command("characters")
def import_characters(
    ctx: typer.Context,
    bodies: list[str] = typer.Option(None, "--bodies", help="Unit id, model key or unambiguous body stem; repeatable."),
    stage_only: bool = typer.Option(False, "--stage-only", help="Stage GLB skeletal payloads and preservation inventory."),
    force: bool = typer.Option(False, "--force", help="Re-author native assets even when their recipe is current."),
) -> None:
    """Stage GLB skeletal units and import their native products under /ElysiumBaked/Models."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline.character_pipeline import import_characters as run_import

        run_import(config, runner, bodies=bodies, stage_only=stage_only, force=force, log=console.print)

    _execute(_state(ctx), "import characters", ExitCode.OFFLINE_EXPORT if stage_only else ExitCode.UNREAL_OR_BAKE, action,
             require_work=True, require_ue=not stage_only, activity=not stage_only)


@import_app.command("models")
def import_models(
    ctx: typer.Context,
    units: list[str] = typer.Option(None, "--units", help="Explicit canonical model ID; repeatable. Preserves unselected staged products."),
    maps: list[str] = typer.Option(
        None, "--maps",
        help="Map stem this run stages models for (repeatable: --maps sp_tutorial_1 --maps "
             "sm_hub_1). Exactly one of --maps/--all is required -- the stage refuses to run "
             "unscoped.",
    ),
    all_models: bool = typer.Option(
        False, "--all",
        help="Owner-approved whole-corpus run: every published, referenced model (R1.1's 3,661). "
             "Long-running; --maps is the working mode.",
    ),
    force: bool = typer.Option(
        False, "--force", help="Re-import every asset even when its recipe stamp is current."
    ),
    stage_only: bool = typer.Option(
        False, "--stage-only",
        help="Write the manifest and provenance sidecars; launch no editor.",
    ),
) -> None:
    """Import the referenced model corpus from published GLBs into /ElysiumBaked/Models.

    Stages every model the selection names into a manifest under `import/models/` and then runs
    the headless editor import over it.
    """

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import unreal
        from elysium_pipeline.importers import materials as material_importer
        from elysium_pipeline.importers import models as importer

        if config.export_v2_root is None or config.work_root is None:
            raise ConfigError(
                "ELYSIUM_EXPORT_V2_ROOT and ELYSIUM_WORK_ROOT must be configured; copy "
                "dev/paths.example.env to .elysium.local.env and set the local paths"
            )
        root = importer.staging_root(config.work_root)
        materials_root = material_importer.staging_root(config.work_root)
        materials_staging_root = materials_root if materials_root.is_dir() else None
        staged = importer.stage_models(
            config.export_v2_root, root, maps=maps or None, all_models=all_models, units=units or None,
            materials_staging_root=materials_staging_root,
        )
        console.print(staged.summary())
        for key, detail in staged.failures[:10]:
            console.print(f"[yellow]  {key}: {detail}[/yellow]", markup=True)
        for key, detail in staged.skips[:10]:
            console.print(f"[yellow]  skipped {key}: {detail}[/yellow]", markup=True)
        if stage_only:
            if staged.failures:
                raise RuntimeError(f"{len(staged.failures)} model unit(s) could not be staged")
            return

        editor_failure: Exception | None = None
        try:
            unreal.import_models(config, runner, staged.manifest_path, force=force)
        except unreal.UnrealFailure as error:
            editor_failure = error
        report = _read_json(root / importer.IMPORT_REPORT_NAME)
        failed_assets = (report.get("failed") or []) if report else []
        if report:
            console.print(
                "model import: "
                f"{report.get('imported', 0)} imported, {report.get('reused', 0)} reused, "
                f"{report.get('pruned', 0)} pruned, {len(failed_assets)} failed"
            )
            for row in failed_assets[:10]:
                console.print(
                    f"[yellow]  {row.get('assetPath')}: {row.get('reason')}[/yellow]", markup=True
                )
            phases = report.get("phaseSeconds") or {}
            if phases:
                console.print(
                    "  phases: "
                    + ", ".join(f"{name}={seconds}s" for name, seconds in sorted(phases.items()))
                )
        problems = []
        if staged.failures:
            problems.append(f"{len(staged.failures)} unit(s) could not be staged")
        if failed_assets:
            problems.append(f"{len(failed_assets)} asset(s) failed to import")
        if editor_failure is not None:
            problems.append(str(editor_failure))
        if problems:
            raise RuntimeError("; ".join(problems))

    # The stage is a file transform over the published units; the editor phase needs the engine
    # and the export corpus (it reads each unit's geometry), never the game install.
    _execute(
        _state(ctx),
        "import models",
        ExitCode.OFFLINE_EXPORT if stage_only else ExitCode.UNREAL_OR_BAKE,
        action,
        require_work=True,
        require_ue=not stage_only,
        activity=not stage_only,
    )


@import_app.command("map-entities")
def import_map_entities(
    ctx: typer.Context,
    maps: list[str] = typer.Option(
        None, "--maps",
        help="Map stem this run stages the entity table for (repeatable: --maps sp_tutorial_1 "
             "--maps sm_hub_1). Required -- the stage refuses to run unscoped.",
    ),
    force: bool = typer.Option(
        False, "--force", help="Re-author every asset even when its recipe stamp is current."
    ),
    stage_only: bool = typer.Option(
        False, "--stage-only", help="Write the manifest; launch no editor.",
    ),
) -> None:
    """Import each named map's entity table into /ElysiumBaked/<map>/DA_<map>_Entities.

    Runs the R3.2 producer's entity join over the published units, asserts def-count and per-index
    parity against the `<map>.ents` document the asset replaces, and then authors one
    `UElysiumMapEntities` per map in a headless editor.
    """

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import unreal
        from elysium_pipeline.importers import map_entities as importer

        if (config.export_v2_root is None or config.work_root is None
                or config.export_root is None):
            raise ConfigError(
                "ELYSIUM_EXPORT_V2_ROOT, ELYSIUM_EXPORT_ROOT and ELYSIUM_WORK_ROOT must be "
                "configured; copy dev/paths.example.env to .elysium.local.env and set the local "
                "paths"
            )
        root = importer.staging_root(config.work_root)
        staged = importer.stage_map_entities(
            config.export_v2_root, root, maps=maps or [],
            ents_for=lambda stem: config.export_root / stem / f"{stem}.ents",
        )
        console.print(staged.summary())
        for name, detail in staged.failures:
            console.print(f"[yellow]  {name}: {detail}[/yellow]", markup=True)
        if stage_only:
            if staged.failures:
                raise RuntimeError(f"{len(staged.failures)} map(s) could not be staged")
            return

        editor_failure: Exception | None = None
        try:
            unreal.import_map_entities(config, runner, staged.manifest_path, force=force)
        except unreal.UnrealFailure as error:
            editor_failure = error
        report = _read_json(root / importer.IMPORT_REPORT_NAME)
        failed = (report.get("failed") or []) if report else []
        if report:
            console.print(
                "map-entity import: "
                f"{report.get('imported', 0)} imported, {report.get('reused', 0)} reused, "
                f"{len(failed)} failed"
            )
            for row in failed[:10]:
                console.print(f"[yellow]  {row.get('map')}: {row.get('reason')}[/yellow]",
                              markup=True)
        problems = []
        if staged.failures:
            problems.append(f"{len(staged.failures)} map(s) could not be staged")
        if failed:
            problems.append(f"{len(failed)} map(s) failed to import")
        if editor_failure is not None:
            problems.append(str(editor_failure))
        if problems:
            raise RuntimeError("; ".join(problems))

    # The stage reads the published units and the legacy `.ents` it asserts against; the editor
    # phase needs the engine and nothing else -- every row travels in the manifest.
    _execute(
        _state(ctx),
        "import map-entities",
        ExitCode.OFFLINE_EXPORT if stage_only else ExitCode.UNREAL_OR_BAKE,
        action,
        require_work=True,
        require_ue=not stage_only,
        activity=not stage_only,
    )


@import_app.command("map-collision")
def import_map_collision(
    ctx: typer.Context,
    maps: list[str] = typer.Option(
        None, "--maps",
        help="Map stem this run stages collision for (repeatable: --maps sp_tutorial_1 "
             "--maps sm_hub_1). Required -- the stage refuses to run unscoped.",
    ),
    force: bool = typer.Option(
        False, "--force", help="Re-author every asset even when its recipe stamp is current."
    ),
    stage_only: bool = typer.Option(
        False, "--stage-only", help="Write the manifest; launch no editor.",
    ),
) -> None:
    """Import each named map's collision into /ElysiumBaked/<map>/DA_<map>_Collision.

    Reads `<map>.hulls`, `<map>.dispcol` and the brush-entity `hulls` of `<map>.ents`, asserts
    parity against them, and then authors one cooked `UElysiumMapCollisionPayload` per map in a
    headless editor.
    """

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import unreal
        from elysium_pipeline.importers import map_collision as importer

        if config.work_root is None or config.export_root is None:
            raise ConfigError(
                "ELYSIUM_EXPORT_ROOT and ELYSIUM_WORK_ROOT must be configured; copy "
                "dev/paths.example.env to .elysium.local.env and set the local paths"
            )
        root = importer.staging_root(config.work_root)
        staged = importer.stage_map_collision(
            root, maps=maps or [], sidecar_dir=lambda stem: config.export_root / stem,
        )
        console.print(staged.summary())
        for name, detail in staged.failures:
            console.print(f"[yellow]  {name}: {detail}[/yellow]", markup=True)
        if stage_only:
            if staged.failures:
                raise RuntimeError(f"{len(staged.failures)} map(s) could not be staged")
            return

        editor_failure: Exception | None = None
        try:
            unreal.import_map_collision(config, runner, staged.manifest_path, force=force)
        except unreal.UnrealFailure as error:
            editor_failure = error
        report = _read_json(root / importer.IMPORT_REPORT_NAME)
        failed = (report.get("failed") or []) if report else []
        if report:
            console.print(
                "map-collision import: "
                f"{report.get('imported', 0)} imported, {report.get('reused', 0)} reused, "
                f"{len(failed)} failed"
            )
            for row in failed[:10]:
                console.print(f"[yellow]  {row.get('map')}: {row.get('reason')}[/yellow]",
                              markup=True)
        problems = []
        if staged.failures:
            problems.append(f"{len(staged.failures)} map(s) could not be staged")
        if failed:
            problems.append(f"{len(failed)} map(s) failed to import")
        if editor_failure is not None:
            problems.append(str(editor_failure))
        if problems:
            raise RuntimeError("; ".join(problems))

    # The stage reads the loose collision sidecars and nothing else; the editor phase needs the
    # engine and nothing else -- every number travels in the manifest.
    _execute(
        _state(ctx),
        "import map-collision",
        ExitCode.OFFLINE_EXPORT if stage_only else ExitCode.UNREAL_OR_BAKE,
        action,
        require_work=True,
        require_ue=not stage_only,
        activity=not stage_only,
    )


@import_app.command("map-environment")
def import_map_environment(
    ctx: typer.Context,
    maps: list[str] = typer.Option(
        None, "--maps",
        help="Map stem this run stages the environment for (repeatable: --maps sp_tutorial_1 "
             "--maps sm_hub_1). Required -- the stage refuses to run unscoped.",
    ),
    force: bool = typer.Option(
        False, "--force", help="Re-author every asset even when its recipe stamp is current."
    ),
    stage_only: bool = typer.Option(
        False, "--stage-only", help="Write the manifest; launch no editor.",
    ),
) -> None:
    """Import each named map's environment into /ElysiumBaked/<map>/DA_<map>_Environment.

    Reads `<map>.env`, `<map>.sky` and `<map>.spawn` verbatim, asserts parity against them, and
    then authors one `UElysiumMapEnvironment` per map in a headless editor.
    """

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import unreal
        from elysium_pipeline.importers import map_environment as importer

        if config.work_root is None or config.export_root is None:
            raise ConfigError(
                "ELYSIUM_EXPORT_ROOT and ELYSIUM_WORK_ROOT must be configured; copy "
                "dev/paths.example.env to .elysium.local.env and set the local paths"
            )
        root = importer.staging_root(config.work_root)
        staged = importer.stage_map_environment(
            root, maps=maps or [], sidecar_dir=lambda stem: config.export_root / stem,
        )
        console.print(staged.summary())
        for name, detail in staged.failures:
            console.print(f"[yellow]  {name}: {detail}[/yellow]", markup=True)
        if stage_only:
            if staged.failures:
                raise RuntimeError(f"{len(staged.failures)} map(s) could not be staged")
            return

        editor_failure: Exception | None = None
        try:
            unreal.import_map_environment(config, runner, staged.manifest_path, force=force)
        except unreal.UnrealFailure as error:
            editor_failure = error
        report = _read_json(root / importer.IMPORT_REPORT_NAME)
        failed = (report.get("failed") or []) if report else []
        if report:
            console.print(
                "map-environment import: "
                f"{report.get('imported', 0)} imported, {report.get('reused', 0)} reused, "
                f"{len(failed)} failed"
            )
            for row in failed[:10]:
                console.print(f"[yellow]  {row.get('map')}: {row.get('reason')}[/yellow]",
                              markup=True)
        problems = []
        if staged.failures:
            problems.append(f"{len(staged.failures)} map(s) could not be staged")
        if failed:
            problems.append(f"{len(failed)} map(s) failed to import")
        if editor_failure is not None:
            problems.append(str(editor_failure))
        if problems:
            raise RuntimeError("; ".join(problems))

    # The stage reads the loose environment sidecars and nothing else; the editor phase needs the
    # engine and nothing else -- every value travels in the manifest.
    _execute(
        _state(ctx),
        "import map-environment",
        ExitCode.OFFLINE_EXPORT if stage_only else ExitCode.UNREAL_OR_BAKE,
        action,
        require_work=True,
        require_ue=not stage_only,
        activity=not stage_only,
    )


def _read_json(path: Path) -> dict | None:
    """A JSON object a child process may have written, or None when absent or unreadable."""

    try:
        content = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    return content if isinstance(content, dict) else None


@export_v2_app.command("ui-resource-glb")
def export_v2_ui_resource_glb(
    ctx: typer.Context,
    path: str = typer.Argument(
        ...,
        help="<path> (tolerates the ui-resources/ prefix and a trailing .glb).",
    ),
) -> None:
    """Export one UI resource definition to one GLB."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destination = export_manager.export_ui_resource_glb(config, runner, path)
        console.print(f"ui-resource GLB export complete: {destination}")

    _execute(
        _state(ctx),
        "export_v2 ui-resource-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("ui-resources-glb")
def export_v2_ui_resources_glb(ctx: typer.Context) -> None:
    """Export every UI-resource identity the seam admits."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destinations = export_manager.export_all_ui_resource_glbs(config, runner)
        console.print(f"ui-resource GLB corpus export complete: {len(destinations)} resources")

    _execute(
        _state(ctx),
        "export_v2 ui-resources-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("script-glb")
def export_v2_script_glb(
    ctx: typer.Context,
    path: str = typer.Argument(
        ...,
        help="python/<path>.py | <path>.pyc | <path> -- the python/ root prefix and either "
             "extension are tolerated.",
    ),
) -> None:
    """Export one game-logic Python script to one GLB."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destination = export_manager.export_script_glb(config, runner, path)
        console.print(f"script GLB export complete: {destination}")

    _execute(
        _state(ctx),
        "export_v2 script-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("scripts-glb")
def export_v2_scripts_glb(ctx: typer.Context) -> None:
    """Export every key source_keys(index) resolves into <root>/scripts/."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destinations = export_manager.export_all_script_glbs(config, runner)
        console.print(f"script GLB corpus export complete: {len(destinations)} scripts")

    _execute(
        _state(ctx),
        "export_v2 scripts-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("map-glb")
def export_v2_map_glb(
    ctx: typer.Context,
    map_name: str = typer.Argument(
        ...,
        help="<map> -- the .bsp stem below maps/; the argument tolerates the maps/ prefix and "
             "the .bsp extension.",
    ),
) -> None:
    """Export all four units of one BSP: the root plus entities, lighting and visibility."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destinations = export_manager.export_map_glb(config, runner, map_name)
        console.print(f"map GLB export complete: {len(destinations)} units")

    _execute(
        _state(ctx),
        "export_v2 map-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("maps-glb")
def export_v2_maps_glb(ctx: typer.Context) -> None:
    """Export all four units of every map the UP-first index resolves below maps/."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destinations = export_manager.export_all_map_glbs(config, runner)
        console.print(f"map GLB corpus export complete: {len(destinations)} units")

    _execute(
        _state(ctx),
        "export_v2 maps-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("map-entities-glb")
def export_v2_map_entities_glb(
    ctx: typer.Context,
    map_name: str = typer.Argument(
        None,
        help="<map> -- the .bsp stem below maps/ (the maps/ prefix and the .bsp suffix are "
             "tolerated, case folded).",
    ),
    every: bool = typer.Option(
        False, "--all", help="Export every map's entity unit instead of one."
    ),
) -> None:
    """Export one map's entity-lump unit, or every map's."""

    if bool(map_name) == every:
        raise typer.BadParameter("name one map, or pass --all")

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        if every:
            destinations = export_manager.export_all_map_entities_glbs(config, runner)
            console.print(
                f"map-entities GLB corpus export complete: {len(destinations)} maps"
            )
            return
        destination = export_manager.export_map_entities_glb(config, runner, map_name)
        console.print(f"map-entities GLB export complete: {destination}")

    _execute(
        _state(ctx),
        "export_v2 map-entities-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("map-lighting-glb")
def export_v2_map_lighting_glb(
    ctx: typer.Context,
    map_name: str = typer.Argument(
        None,
        help="<map> -- the map stem; the root prefix and the source extension are tolerated.",
    ),
    every: bool = typer.Option(
        False, "--all", help="Export every map's lighting unit instead of one."
    ),
) -> None:
    """Export one map's lighting unit, or every map's."""

    if bool(map_name) == every:
        raise typer.BadParameter("name one map, or pass --all")

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        if every:
            destinations = export_manager.export_all_map_lighting_glbs(config, runner)
            console.print(
                f"map-lighting GLB corpus export complete: {len(destinations)} maps"
            )
            return
        destination = export_manager.export_map_lighting_glb(config, runner, map_name)
        console.print(f"map-lighting GLB export complete: {destination}")

    _execute(
        _state(ctx),
        "export_v2 map-lighting-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("map-visibility-glb")
def export_v2_map_visibility_glb(
    ctx: typer.Context,
    map_name: str = typer.Argument(
        None,
        help="<map> stem; tolerates the maps/ prefix and the .bsp extension.",
    ),
    every: bool = typer.Option(
        False, "--all", help="Export every map's visibility unit instead of one."
    ),
) -> None:
    """Export one map's visibility unit, or every map's."""

    if bool(map_name) == every:
        raise typer.BadParameter("name one map, or pass --all")

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        if every:
            destinations = export_manager.export_all_map_visibility_glbs(config, runner)
            console.print(
                f"map-visibility GLB corpus export complete: {len(destinations)} maps"
            )
            return
        destination = export_manager.export_map_visibility_glb(config, runner, map_name)
        console.print(f"map-visibility GLB export complete: {destination}")

    _execute(
        _state(ctx),
        "export_v2 map-visibility-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("nav-graph-glb")
def export_v2_nav_graph_glb(
    ctx: typer.Context,
    map_name: str = typer.Argument(..., help="<map> -- the nav graph's map stem."),
) -> None:
    """Export one map's navigation graph to one GLB."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destination = export_manager.export_nav_graph_glb(config, runner, map_name)
        console.print(f"nav-graph GLB export complete: {destination}")

    _execute(
        _state(ctx),
        "export_v2 nav-graph-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("nav-graphs-glb")
def export_v2_nav_graphs_glb(ctx: typer.Context) -> None:
    """Export every maps/graphs/*.ain the index resolves."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destinations = export_manager.export_all_nav_graph_glbs(config, runner)
        console.print(f"nav-graph GLB corpus export complete: {len(destinations)} graphs")

    _execute(
        _state(ctx),
        "export_v2 nav-graphs-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("engine-config-glb")
def export_v2_engine_config_glb(
    ctx: typer.Context,
    path: str = typer.Argument(
        ...,
        help="<path> -- the config's install-relative key; it keeps its own extension "
             "(cfg/user.cfg, lights.rad).",
    ),
) -> None:
    """Export one engine-configuration file to one GLB."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destination = export_manager.export_engine_config_glb(config, runner, path)
        console.print(f"engine-config GLB export complete: {destination}")

    _execute(
        _state(ctx),
        "export_v2 engine-config-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("engine-configs-glb")
def export_v2_engine_configs_glb(ctx: typer.Context) -> None:
    """Export every engine-configuration identity the seam admits."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destinations = export_manager.export_all_engine_config_glbs(config, runner)
        console.print(f"engine-config GLB corpus export complete: {len(destinations)} configs")

    _execute(
        _state(ctx),
        "export_v2 engine-configs-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("corpus-index-glb")
def export_v2_corpus_index_glb(ctx: typer.Context) -> None:
    """Index the whole published corpus into one GLB; fails on any unclaimed member."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        destination = export_manager.export_corpus_index_glb(config, runner)
        console.print(f"corpus-index GLB export complete: {destination}")

    _execute(
        _state(ctx),
        "export_v2 corpus-index-glb",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


@export_v2_app.command("export-all")
def export_v2_export_all(ctx: typer.Context) -> None:
    """Export every isolated GLB seam; corpus-index runs last."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        published = export_manager.export_all_glb_seams(config, runner)
        counts = ", ".join(
            f"{len(destinations)} {seam}" for seam, destinations in published.items()
        )
        console.print(f"export_v2 corpus export complete: {counts}")

    _execute(
        _state(ctx),
        "export_v2 export-all",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        activity=True,
    )


def _corpus_unit(ctx: typer.Context, label: str, **selectors) -> None:
    """Re-decode and re-bake one shared-corpus unit. No map is exported and no `.umap` changes."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        done = export_manager.export_corpus_unit(config, runner, **selectors)
        console.print(
            f"{label} complete: "
            + ", ".join(filter(None, [
                ", ".join(done["models"]),
                ", ".join(done["materials"]),
            ]))
            + " (no map re-baked)"
        )

    _execute(
        _state(ctx),
        label,
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        require_ue=True,
        activity=True,
    )


@export_app.command("prop")
def export_prop(
    ctx: typer.Context,
    model: str = typer.Argument(..., help="Install model path, for example models/scenery/x.mdl."),
    force: bool = typer.Option(False, "--force", help="Rebake even when the recipe is unchanged."),
) -> None:
    """Re-decode one static model and rebake its mesh, materials and textures."""
    _corpus_unit(ctx, "export prop", models=[model], force=force)


@export_app.command("material")
def export_material(
    ctx: typer.Context,
    material: str = typer.Argument(..., help="Install material path, without materials/ or .vmt."),
    force: bool = typer.Option(False, "--force", help="Rebake even when the recipe is unchanged."),
) -> None:
    """Re-resolve one material and rebake its instance and every texture it draws."""
    _corpus_unit(ctx, "export material", materials=[material], force=force)


@export_app.command("texture")
def export_texture(
    ctx: typer.Context,
    texture: str = typer.Argument(..., help="Install texture path, without materials/ or .tth."),
    force: bool = typer.Option(False, "--force", help="Rebake even when the recipe is unchanged."),
) -> None:
    """Re-decode one texture, through every corpus material that draws it."""
    _corpus_unit(ctx, "export texture", textures=[texture], force=force)


@export_app.command("placed-model")
def export_placed_model(
    ctx: typer.Context,
    map_name: str = typer.Argument(..., help="Owning exported map, for example sp_tutorial_1."),
    model: str = typer.Argument(..., help="Placed MDL path used by that map."),
    force: bool = typer.Option(False, "--force", help="Rewrite and rebake this selected model."),
) -> None:
    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        stems = export_manager.export_placed_models(
            config, runner, [map_name], [model], force=force)
        console.print("placed-model export complete: " + ", ".join(stems))

    _execute(
        _state(ctx),
        "export placed-model",
        ExitCode.OFFLINE_EXPORT,
        action,
        require_game=True,
        require_ue=True,
        activity=True,
    )


@export_app.command("bundle")
def export_bundle(
    ctx: typer.Context,
    bundle: str = typer.Argument(...),
    force: bool = typer.Option(False, "--force"),
) -> None:
    allowed = {
        "corpus", "particles", "scripts", "signs", "vdata", "cfg", "scenes",
        "ui", "policy",
    }
    if bundle not in allowed:
        raise typer.BadParameter("bundle must be one of: " + ", ".join(sorted(allowed)))
    # `corpus` decodes offline AND bakes the shared /ElysiumBaked/Shared packages, so it needs
    # both the install and an editor; `policy` needs only the editor.
    needs_editor = bundle in {"policy", "corpus"}

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager

        export_manager.export_bundle(config, runner, bundle, force=force)
        console.print(f"bundle export complete: {bundle}")

    _execute(
        _state(ctx),
        "export bundle",
        ExitCode.UNREAL_OR_BAKE if bundle == "policy" else ExitCode.OFFLINE_EXPORT,
        action,
        require_game=bundle != "policy",
        require_ue=needs_editor,
        activity=True,
    )


@app.command("reconstruct")
def reconstruct(
    ctx: typer.Context,
    rebuild: bool = typer.Option(False, "--rebuild"),
) -> None:
    """Rebuild the whole generated tree from the VtMB install, from an empty mount.

    Always cleans first and always forces: a reconstruction that reused a receipt would
    reproduce whatever the last run left, which is the state it exists to discard. Every
    generator and bake therefore runs, and the products are the run's own output rather than
    a claim about them -- verifying them is `uv run elysium test`, a separate command.
    """

    state = _state(ctx)

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import export_manager, unreal

        ready = sync_dependencies(config.repo_root, cache_root=_shared_cache_root(config))
        console.print("dependencies ready: " + ", ".join(ready))
        unreal.build(config, runner, "rebuild" if rebuild else "")
        maps = export_manager.export_profile(
            config,
            runner,
            "all",
            clean=True,
            force=True,
            per_task=state.verbose,
        )
        _summary(state, f"reconstruction complete: {len(maps)} map(s)", maps=len(maps))

    _execute(
        state,
        "reconstruct",
        ExitCode.VALIDATION,
        action,
        require_game=True,
        require_ue=True,
        activity=True,
    )


@blender_app.command("build")
def blender_build(ctx: typer.Context) -> None:
    """Validate and package the review add-on into the work root."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import blender

        archive = blender.build(config, runner)
        console.print(f"built {archive}")

    _execute(_state(ctx), "blender build", ExitCode.DEPENDENCY_OR_TOOLCHAIN, action)


@blender_app.command("install")
def blender_install(ctx: typer.Context) -> None:
    """Package the add-on and install it into Blender, enabled."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import blender

        module = blender.install(config, runner)
        console.print(f"installed and enabled as {module}")

    _execute(_state(ctx), "blender install", ExitCode.DEPENDENCY_OR_TOOLCHAIN, action)


@blender_app.command("review")
def blender_review(
    ctx: typer.Context,
    target: str | None = typer.Argument(None, help="A .glb to open on startup."),
) -> None:
    """Open Blender with the add-on enabled."""

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import blender

        path = Path(target) if target else None
        if path is not None and not path.is_absolute():
            path = config.export_v2_root / path
        blender.review(config, runner, path)

    _execute(
        _state(ctx), "blender review", ExitCode.DEPENDENCY_OR_TOOLCHAIN, action, activity=True
    )


@blender_app.command("report")
def blender_report(
    ctx: typer.Context,
    json_output: bool = typer.Option(False, "--json", help="Print the report as JSON."),
) -> None:
    """Sweep the export_v2 corpus for integrity problems.

    Needs no Blender: the add-on's core is deliberately free of `bpy` so this runs here.
    """

    state = _state(ctx)
    # The product here is the report document itself, not a run status, so this keeps its
    # own shape and only borrows the stream discipline: stdout is the document, prose is
    # stderr. Without this the run-report and log lines land in the middle of the JSON.
    state.json_output = json_output

    def action(config: ProjectConfig, _runner: ProcessRunner) -> None:
        from elysium_pipeline import blender

        summary, data, destination = blender.corpus_report(config, write=not json_output)
        if json_output:
            typer.echo(json.dumps(data, indent=2, sort_keys=True))
            state.json_emitted = True
            return
        console.print(summary)
        if destination is not None:
            console.print(f"report written to {destination}")

    _execute(state, "blender report", ExitCode.VALIDATION, action, activity=True)


@app.command("test")
def test_command(
    ctx: typer.Context,
    filter_name: str = typer.Argument("Elysium."),
    stems: list[str] = typer.Option(
        None, "--stem", help="Widen the per-model parity slice (repeatable)."
    ),
    json_output: bool = typer.Option(False, "--json"),
) -> None:
    state = _state(ctx)
    state.json_output = json_output

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import unreal

        summary = unreal.run_tests(config, runner, filter_name, parity_stems=stems or ())
        # A test that declines to run still reports Success, so the executed count is the only
        # honest measure of what a green tier covered.
        _summary(
            state,
            f"{summary['executed']} of {summary['total']} test(s) executed"
            f" in {summary['seconds']:.1f}s"
            + (f"; {summary['abstained']} abstained (prerequisite unavailable)"
               if summary["abstained"] else ""),
            filter=filter_name,
            total=summary["total"],
            executed=summary["executed"],
            abstained=summary["abstained"],
            failed=summary["failed"],
            seconds=summary["seconds"],
            abstentions=summary["abstentions"],
            automation_report=summary["report_path"],
        )
        if not json_output:
            console.print(f"automation report: {summary['report_path']}", soft_wrap=True)
            for name in summary["abstentions"][:8]:
                console.print(f"  abstained: {name}")
            if len(summary["abstentions"]) > 8:
                console.print(f"  ... and {len(summary['abstentions']) - 8} more")

    _execute(
        state,
        "test",
        ExitCode.VALIDATION,
        action,
        require_ue=True,
        activity=True,
    )


@run_app.command("editor", context_settings=PASSTHROUGH)
def run_editor(ctx: typer.Context, extra: list[str] = typer.Argument(None)) -> None:
    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import unreal

        unreal.run_editor(config, runner, [*(extra or ()), *ctx.args])

    _execute(
        _state(ctx),
        "run editor",
        ExitCode.UNREAL_OR_BAKE,
        action,
        require_ue=True,
        activity=True,
    )


@run_app.command("play", context_settings=PASSTHROUGH)
def run_play(
    ctx: typer.Context,
    map_name: str | None = typer.Argument(None),
    extra: list[str] = typer.Argument(None),
) -> None:
    """Launch the game with Unreal's live log console and retained `Saved/Logs` session log.

    `play <map>` boots straight into that map; no argument goes to the menu.

    `play gr <model> <clip>` is the one target that is not a map: it boots into the green room's
    stage world with Cog up and the green-room window docked down the left edge, the same room
    `elysium.gr` opens from a running session. The model and the clip are optional.
    """

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import unreal

        unreal.run_play(config, runner, map_name, [*(extra or ()), *ctx.args])

    _execute(
        _state(ctx),
        "run play",
        ExitCode.UNREAL_OR_BAKE,
        action,
        require_ue=True,
        activity=True,
    )


@debug_app.command("profile", context_settings=PASSTHROUGH)
def debug_profile(ctx: typer.Context, args: list[str] = typer.Argument(None)) -> None:
    _debug(ctx, "profile", [*(args or ()), *ctx.args])


@debug_app.command("probe", context_settings=PASSTHROUGH)
def debug_probe(ctx: typer.Context, args: list[str] = typer.Argument(None)) -> None:
    _debug(ctx, "probe", [*(args or ()), *ctx.args])


@debug_app.command("shots", context_settings=PASSTHROUGH)
def debug_shots(ctx: typer.Context, args: list[str] = typer.Argument(None)) -> None:
    _debug(ctx, "shots", [*(args or ()), *ctx.args])


@debug_app.command("move", context_settings=PASSTHROUGH)
def debug_move(ctx: typer.Context, args: list[str] = typer.Argument(None)) -> None:
    """Replay the movement courses and diff them against their baselines.

    Usage: `debug move <course> <hz>`, both optional. Two hosts run by default -- the generated
    gym, which brackets where a threshold is, and the sited courses on `sp_tutorial_1`, which
    answer whether we match retail. `--gym` or `--sited` runs one of them; `--promote` makes what
    the run just recorded the new baseline.
    """
    _debug(ctx, "move", [*(args or ()), *ctx.args])


@debug_app.command("cast", context_settings=PASSTHROUGH)
def debug_cast(ctx: typer.Context, args: list[str] = typer.Argument(None)) -> None:
    """Record the cast's locomotion courses and diff them against their baselines.

    Usage: `debug cast <course> <hz>`, both optional. The other producer of the same body trace
    `debug move` records for the player: a cast body is driven by an authored travel order -- a
    patrol route, a scripted beat -- while its own driver's selection is written through the same
    channels, and every course judges its own no-slide predicate before anything is compared.

    Two hosts. By default the body stands on the arena in the stage world, which is the regression
    instrument; `--sited` drives the priority map's own cast down the map's own authored route,
    armed and unarmed, which is the only host an acceptance claim can be made on. `-CastBody=<stem>`
    picks the body the arena stands; `--promote` makes what the run just recorded the new baseline.
    """
    _debug(ctx, "cast", [*(args or ()), *ctx.args])


@debug_app.command("compose", context_settings=PASSTHROUGH)
def debug_compose(ctx: typer.Context, args: list[str] = typer.Argument(None)) -> None:
    """Record the pose the animation graph composes, headless, and score it against the capture.

    Usage: `debug compose <weapon> <hz>`, both optional (default `item_w_ithaca_m_37` at 60 Hz).
    Drives a body on a real map through the real input router -- strafe, then strafe with the
    trigger held, then a reload -- and writes the player body's own component-space bone transforms
    per frame beside the four overlay slot rows that produced them.

    This is the half `Elysium.Content.RigCompose` cannot reach: that test evaluates baked sequences
    directly and never stands a graph up, so a slot published at the wrong weight, a bone mask
    resolved against the wrong skeleton or a chain composed in the wrong order is invisible to it.
    `-ElysiumMap=<name>` picks the host map.

    **Both bodies by default**, one launch each: the harness seats a body and drives it, so a second
    body in the same process would inherit the first one's motion. They are the two the retail
    capture recorded -- `malkavian_female_armor_0` and `malkavian_male_armor_0` -- and both reports
    go to one scoring call, whose closing summary puts their arm scalars beside each other.
    `--body <stem>` runs one; it is repeatable and takes a comma list.
    """
    _debug(ctx, "compose", [*(args or ()), *ctx.args])


@debug_app.command("oracle")
def debug_oracle(
    ctx: typer.Context,
    validate: str | None = typer.Option(
        None, "--validate", metavar="SESSION",
        help="A life_rig_pose capture directory: hold the compositor to what retail drew."),
    emit: bool = typer.Option(
        False, "--emit", help="Write the dense oracle under $ELYSIUM_EXPORT_ROOT/_oracle."),
    stems: list[str] = typer.Option(
        None, "--stem", help="A body stem (repeatable); the two captured Malkavians by default."),
    hosts: list[str] = typer.Option(
        None, "--host", help="A base sequence label to answer states for (repeatable)."),
    search_aim: bool = typer.Option(
        False, "--search-aim", help="With --validate: search the aim pitch the capture omits."),
    runs: list[str] = typer.Option(
        None, "--run", help="A `debug compose` report: hold the running graph to the compositor "
                            "frame by frame (repeatable)."),
) -> None:
    """The reference compositor: retail's pose arithmetic offline, from the user's own install.

    `--validate <session>` holds it to the capture, once per rule -- the only place anything is
    compared against the capture. `--emit` writes the dense oracle every downstream instrument is
    held to instead: `Elysium.Content.OracleIdentity` composes the baked mount at the same states
    and asserts identity, with no capture and no search in between.
    """
    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        import sys as _sys

        if runs:
            argv = [_sys.executable, "-m", "elysium_pipeline.validation.graph_identity"]
            for run in runs:
                argv.extend(["--run", run])
            runner.run(argv, check=True)
            return
        argv = [_sys.executable, "-m", "elysium_pipeline.validation.retail_compositor"]
        chosen = stems or ["malkavian_female_armor_0", "malkavian_male_armor_0"]
        for stem in chosen:
            argv.extend(["--stem", stem])
        if validate:
            argv.extend(["--validate", validate])
            if search_aim:
                argv.append("--search-aim")
        elif emit:
            argv.extend(["--emit", os.fspath(config.export_root / "_oracle")])
            for host in hosts or ["m37_aggressive_run", "m37_ready", "m37_relaxed_run",
                                  "supershotgun_aggressive_run", "steyr_aggressive_run"]:
                argv.extend(["--host", host])
        else:
            raise ValueError("debug oracle needs --validate <session>, --emit or --run <report>")
        runner.run(argv, check=True)

    _execute(
        _state(ctx),
        "debug oracle",
        ExitCode.VALIDATION,
        action,
        require_game=True,
        activity=True,
    )


@debug_app.command("greenroom", context_settings=PASSTHROUGH)
def debug_greenroom(ctx: typer.Context, args: list[str] = typer.Argument(None)) -> None:
    _debug(ctx, "greenroom", [*(args or ()), *ctx.args])


@debug_app.command("modelroom", context_settings=PASSTHROUGH)
def debug_modelroom(ctx: typer.Context, args: list[str] = typer.Argument(None)) -> None:
    _debug(ctx, "modelroom", [*(args or ()), *ctx.args])


@app.command("gr", context_settings=PASSTHROUGH)
def green_room(ctx: typer.Context, args: list[str] = typer.Argument(None)) -> None:
    """Open the interactive green room: one body, live, with cloth tuning.

    Usage: `gr <model> <clip> <map>`, and all three are optional -- with no model the stage
    comes up empty and the window picks one. Square brackets are Rich markup in a Typer
    help string, so the optional arguments are written in angle brackets here.

    `--drive` opens the second mode: the named body on the player pawn, standing on
    the generated movement gym, walked with WASD and framed by the shipping camera, with
    the sample, the selection record and the evaluated pose read back in the window. F1
    hands the keyboard between the game and the window.

    `--arena` opens the third: the same driven body in a clean square room -- flat floor,
    four walls, one cover block, interesting-place anchors -- with a built navmesh over it,
    which is what lets spawned characters path. The AI window stands the cast up, arms the
    player, and reads back each character's mind, conditions, schedule and enemy.

    Top level rather than under `debug` because it is driven by hand rather than run as a
    check, and it captures nothing.
    """
    _debug(ctx, "gr", [*(args or ()), *ctx.args])


def _debug(ctx: typer.Context, kind: str, args: list[str]) -> None:
    # The rendering is already offscreen; this is only about what happens to the sheet afterwards.
    # `--no-open` leaves it on disk and reports the path, which is what an unattended or scripted
    # run wants — handing a file to the shell's image viewer is a side effect nothing asked for.
    open_sheet = "--no-open" not in args
    args = [value for value in args if value != "--no-open"]

    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        from elysium_pipeline import unreal

        output = unreal.run_harness(config, runner, kind, args)
        if output is not None:
            console.print(f"review sheet: {output}")
            if open_sheet and os.name == "nt" and output.is_file():
                os.startfile(output)  # type: ignore[attr-defined]

    _execute(
        _state(ctx),
        f"debug {kind}",
        ExitCode.VALIDATION,
        action,
        require_ue=True,
        activity=True,
    )


@app.command("research", context_settings=PASSTHROUGH)
def research(
    ctx: typer.Context,
    case: str = typer.Argument(...),
    args: list[str] = typer.Argument(None),
) -> None:
    def action(config: ProjectConfig, runner: ProcessRunner) -> None:
        case_root = config.repo_root / "research" / "cases" / case
        values = [*(args or ()), *ctx.args]
        if not case_root.is_dir():
            matches = sorted(
                path
                for path in (config.repo_root / "research" / "tooling").rglob(f"{case}.py")
                if "__pycache__" not in path.parts
            )
            if len(matches) != 1:
                detail = (
                    f"unknown research case or tool: {case}"
                    if not matches
                    else f"ambiguous research tool {case}: {', '.join(map(str, matches))}"
                )
                raise ValueError(detail)
            result = runner.run([sys.executable, str(matches[0]), *values])
            if result.returncode:
                raise ProcessFailure(result, int(ExitCode.VALIDATION))
            return
        if values and values[0].endswith(".json"):
            spec = Path(values.pop(0))
            if not spec.is_absolute():
                spec = config.repo_root / spec
        else:
            specs = sorted((case_root / "specs").glob("*.json"))
            if not specs:
                raise ValueError(f"research case has no specs: {case}")
            spec = specs[0]
        result = runner.run(
            [
                sys.executable,
                str(config.repo_root / "research/tooling/ghidra/driver/ghidra_context.py"),
                str(spec),
                *values,
            ]
        )
        if result.returncode:
            raise ProcessFailure(result, int(ExitCode.VALIDATION))

    _execute(
        _state(ctx),
        "research",
        ExitCode.VALIDATION,
        action,
        require_game=False,
    )


@app.command(
    "mcp",
    context_settings=PASSTHROUGH,
)
def mcp(ctx: typer.Context) -> None:
    config = _state(ctx).resolve(require_work=False)
    result = subprocess.run(
        [sys.executable, "-m", "elysium_pipeline.devtools.mcp_proxy", *ctx.args],
        cwd=config.repo_root,
        env=os.environ.copy(),
    )
    raise typer.Exit(result.returncode)
