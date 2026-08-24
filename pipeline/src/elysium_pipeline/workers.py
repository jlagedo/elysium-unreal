"""Process-pool fan-out for the offline decode.

Threads do not widen this work. BSP and MDL parsing is Python byte-struct reading that holds the
GIL for the whole of its run; only the `zlib` and Pillow calls inside a texture decode release it.
Separate interpreters widen it, and they also take the decode caches out of the sharing problem
entirely: each worker carries its own `tex_cache`, so no entry is ever read while another shard is
part-way through filling it.

A shard is correct only where what it writes is a function of what it was given. Two rules keep
that true for the corpus:

- **Disjoint by output name, not by input.** A shard owns every input that resolves to a file
  name, so no file has two authors and the result does not depend on which worker finished first.
- **Atomic where a duplicate is still possible.** Two materials that share a base texture can land
  in different shards. What they write is byte-identical -- name and pixels are both a function of
  the source texture and its role -- so the duplicate costs time, not correctness, and
  `tex_to_png.save_png` publishes it in one step.

A worker function must be importable by name (Windows spawns rather than forks), and both its
arguments and its result must pickle.
"""

from __future__ import annotations

from collections.abc import Callable, Iterable, Sequence
from concurrent.futures import ProcessPoolExecutor
import contextlib
import io
import os
from typing import Any


def default_jobs() -> int:
    """One worker per physical core.

    Decoding the install is CPU-bound: BSP and MDL parsing is Python byte-struct work and texture
    decode is zlib plus Pillow, so the useful width is the core count rather than a share of it.
    Hyperthreads are excluded -- two decode workers on one physical core contend for the same
    integer units and return less than one.
    """

    physical = None
    try:
        import psutil

        physical = psutil.cpu_count(logical=False)
    except Exception as error:                          # noqa: BLE001 - report, then fall back
        print(f"[export] psutil could not report the core count ({error}); "
              "sizing jobs from the logical count instead")
    if not physical:
        physical = max(1, (os.cpu_count() or 2) // 2)
    return max(1, physical)


def even_chunks(items: Sequence[Any], count: int) -> list[list[Any]]:
    """Split `items` into at most `count` contiguous, near-equal, non-empty shards."""

    items = list(items)
    if not items:
        return []
    count = max(1, min(count, len(items)))
    size, remainder = divmod(len(items), count)
    chunks: list[list[Any]] = []
    start = 0
    for index in range(count):
        stop = start + size + (1 if index < remainder else 0)
        chunks.append(items[start:stop])
        start = stop
    return chunks


def map_chunks(
    function: Callable[[list[Any]], Any],
    items: Iterable[Any],
    *,
    jobs: int,
    label: str,
    initializer: Callable[..., None] | None = None,
    initargs: tuple[Any, ...] = (),
) -> list[Any]:
    """Run `function(shard)` over contiguous shards of `items`, results in shard order.

    One shard runs inline: a focused unit then costs no process spawn, and a failure keeps the
    traceback it was raised with instead of a pickled copy. A worker that raises propagates --
    a shard that failed decoded nothing, and continuing would write a corpus whose manifest
    silently omits what it could not read.
    """

    chunks = even_chunks(list(items), jobs)
    if not chunks:
        return []
    if len(chunks) == 1:
        if initializer is not None:
            initializer(*initargs)
        return [function(chunks[0])]

    total = sum(len(chunk) for chunk in chunks)
    print(f"  {label}: {total} item(s) across {len(chunks)} worker(s)", flush=True)
    with ProcessPoolExecutor(
        max_workers=len(chunks), initializer=initializer, initargs=initargs
    ) as pool:
        return list(pool.map(function, chunks))


#: Loaded once per worker process. ``[None]`` is a real answer -- `load_anorms` warns and the
#: export drops morph targets -- so presence in the list is the sentinel, not the value.
_ANORMS: list = []


def _anorms():
    if not _ANORMS:
        from elysium_pipeline.formats import mdl_skel

        _ANORMS.append(mdl_skel.load_anorms())
    return _ANORMS[0]


def character_source_worker(kind: str, stem: str, model_rel: str, out_dir: str,
                            clip_labels: Sequence[str] = (),
                            ensure_labels: Sequence[str] = ()) -> str:
    """Write one character `.eskm` container in this process; returns its console output.

    Container writing is GIL-bound byte-struct parsing, so the character-source task graph
    dispatches each stale task here rather than running it on a scheduler thread. The install
    index is process-memoized (`install.build_index`), so every task after a worker's first
    reuses it, and the unit-vector table loads once per process the same way. Output is
    captured and returned so the caller's progress log carries it; on failure the captured
    tail rides the raised error, because a pickled exception crosses the process boundary
    without the worker's stdout.
    """
    if kind not in ("model", "bank", "cinematic", "prop"):
        # Checked before any install read: a dispatch bug must not cost an index build.
        raise RuntimeError(f"{kind} {stem}: unknown character source kind: {kind!r}")

    from elysium_pipeline.exporters import UE_mdl_skeletal
    from elysium_pipeline.formats import install

    buffer = io.StringIO()
    try:
        with contextlib.redirect_stdout(buffer), contextlib.redirect_stderr(buffer):
            index = install.build_index(verbose=False)
            if kind == "model":
                UE_mdl_skeletal.write_model(index, model_rel, out_dir, stem=stem,
                                            anorms=_anorms())
            elif kind == "bank":
                UE_mdl_skeletal.write_bank(index, model_rel, out_dir, stem)
            elif kind == "cinematic":
                UE_mdl_skeletal.write_cinematic(index, model_rel, out_dir, stem)
            else:
                UE_mdl_skeletal.write_model(index, model_rel, out_dir, stem=stem,
                                            anorms=None, clip_labels=tuple(clip_labels),
                                            ensure_labels=tuple(ensure_labels))
    except (Exception, SystemExit) as exc:
        tail = "\n".join(buffer.getvalue().splitlines()[-20:])
        raise RuntimeError(
            f"{kind} {stem}: {type(exc).__name__}: {exc}"
            + (f"\n--- worker output tail ---\n{tail}" if tail else "")
        ) from exc
    return buffer.getvalue()
