#!/usr/bin/env python3
"""The NPC kernel's generated surface, regenerated or checked in one process.

Seven tools own it, and four of them stand on the ledger: `kernel_shape` builds on
`kernel_ledger`, `gen_kernel_shape` on `kernel_shape`, and `kernel_story8_shape` and
`gen_kernel_bindings` on `gen_kernel_shape`. Run one by one, each rebuilds the whole corpus pass
underneath it (the ledger nine times over for the full set). Here the ledger, the shape and the
census model are built once and every tool renders from the same objects.

`--check` is the gate: one pass, every tool in its own `--check` mode, nothing written. The ledger
runs with `--reach` for every map whose cut is committed (`docs/vtmb/npc-kernel/reach/<map>.tsv`),
so a stale reach cut fails the gate with the tables.

Without it the tools write, in the order below, and then the gate runs on fresh builds. The chain
is not a straight line: the ledger's citation scan reads `Source/`, and `kernel_story8_shape`
writes forwarding bodies there that the scan does not skip, so a later tool's write can leave an
earlier table stale. A pass repeats until the gate holds, at most `MAX_PASSES` times.

Across processes the builds come from `kernel_cache` (the corpus stage, the SDK index, the shape's
corpus half, the citation scan per file), and a `--check` whose inputs and outputs have not changed
since it last passed replays that pass: an unchanged tree answers in well under a second.

Usage::

    uv run elysium research kernel --check     # the gate
    uv run elysium research kernel             # regenerate until the gate holds
    uv run elysium research kernel --verbose   # every tool's own output as well

Each tool still runs alone (`uv run elysium research kernel_ledger --check`) for its other options.
"""

from __future__ import annotations

import argparse
import contextlib
import functools
import io
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "ghidra" / "driver"))
sys.path.insert(0, str(Path(__file__).resolve().parent))

import gen_kernel_bindings  # noqa: E402
import gen_kernel_shape  # noqa: E402
import gen_kernel_tunables  # noqa: E402
import kernel_cache  # noqa: E402
import kernel_ledger  # noqa: E402
import kernel_lists  # noqa: E402
import kernel_reach  # noqa: E402
import kernel_shape  # noqa: E402
import kernel_story8_shape  # noqa: E402

from elysium_pipeline.paths import repo_root  # noqa: E402

# Each tool after the ones whose builds it stands on.
TOOLS = (
    ("kernel_ledger", kernel_ledger),
    ("kernel_lists", kernel_lists),
    ("kernel_shape", kernel_shape),
    ("gen_kernel_shape", gen_kernel_shape),
    ("kernel_story8_shape", kernel_story8_shape),
    ("gen_kernel_bindings", gen_kernel_bindings),
    ("gen_kernel_tunables", gen_kernel_tunables),
)
# The builds the tools share. Every caller reaches them through the module attribute
# (`kl.build`, `ks.build`, `g.build`) or, in the module itself, the global of the same name, so
# rebinding the attribute to a memo is the whole of the sharing.
SHARED_BUILDS = (kernel_ledger, kernel_shape, gen_kernel_shape)
MAX_PASSES = 3


def share_builds() -> None:
    for module in SHARED_BUILDS:
        module.build = functools.cache(module.build)


def fresh_builds() -> None:
    """Forget every shared build: the next tool reads the tree as it is now."""
    for module in SHARED_BUILDS:
        module.build.cache_clear()
    kernel_cache.forget()


def reach_maps() -> list[str]:
    """Every map whose reach cut is committed: the gate checks each, the write mode regenerates it."""
    folder = repo_root().joinpath(*kernel_ledger.DEFAULT_OUTPUT, kernel_reach.REACH_DIR)
    return sorted(path.stem for path in folder.glob("*.tsv")) if folder.is_dir() else []


def tool_args(name: str, check: bool) -> list[str]:
    args = ["--check"] if check else []
    if name == "kernel_ledger":
        for map_name in reach_maps():
            args += ["--reach", map_name]
    return args


def run(name: str, module, check: bool, verbose: bool) -> int:
    """One tool's `main`, its output held back: its last line on success, all of it on failure.

    A tool refuses with `raise SystemExit("why")`. Left alone that ends this process, and its
    reason goes to stderr, which is not held here; caught, it is the tool's FAIL row with the
    reason as its last line, and the tools after it still run.
    """
    out = io.StringIO()
    reason = ""
    start = time.perf_counter()
    with contextlib.redirect_stdout(out):
        try:
            status = module.main(tool_args(name, check))
        except SystemExit as stop:
            if stop.code is None or isinstance(stop.code, int):
                status = stop.code or 0
            else:
                status, reason = 1, str(stop.code)
    text = out.getvalue()
    if reason:
        text += ("" if not text or text.endswith("\n") else "\n") + reason + "\n"
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    print(f"  {'ok  ' if status == 0 else 'FAIL'} {name:<20} {time.perf_counter() - start:6.1f}s  "
          f"{lines[-1] if lines else ''}", flush=True)
    if verbose or status:
        print(text, end="" if text.endswith("\n") else "\n", flush=True)
    return status


def run_all(check: bool, verbose: bool) -> list[str]:
    """Every tool once on fresh builds; the names of the ones that failed."""
    fresh_builds()
    return [name for name, module in TOOLS if run(name, module, check, verbose)]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true",
                        help="verify every generated file; write nothing")
    parser.add_argument("--verbose", action="store_true", help="print every tool's own output")
    args = parser.parse_args(argv)
    share_builds()
    start = time.perf_counter()

    if args.check:
        failed = run_all(check=True, verbose=args.verbose)
        if failed:
            print(f"kernel --check: stale: {', '.join(failed)}; "
                  "regenerate with `uv run elysium research kernel`")
            return 1
        maps = reach_maps()
        print(f"kernel --check: all {len(TOOLS)} tools match"
              + (f", the reach cut of {', '.join(maps)} with the ledger" if maps else "")
              + f" ({time.perf_counter() - start:.1f}s)")
        return 0

    for number in range(1, MAX_PASSES + 1):
        print(f"pass {number}: write")
        failed = run_all(check=False, verbose=args.verbose)
        if failed:
            print(f"kernel: {', '.join(failed)} failed while writing (its output is above)")
            return 1
        print(f"pass {number}: check")
        failed = run_all(check=True, verbose=args.verbose)
        if not failed:
            print(f"kernel: regenerated, all {len(TOOLS)} tools match after {number} pass"
                  f"{'es' if number > 1 else ''} ({time.perf_counter() - start:.1f}s)")
            return 0
    print(f"kernel: still stale after {MAX_PASSES} passes: {', '.join(failed)}; "
          "the generators do not settle")
    return 1


if __name__ == "__main__":
    sys.exit(main())
