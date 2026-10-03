"""The real kernel build, made once per test process and shared by the kernel test modules.

The ledger's, the shape's and the census generator's oracle tests each held a module-scoped fixture
that rebuilt the whole chain beneath it: three ledgers and two shapes per run. Here the ledger is
built once, the shape on that ledger and the census model on that shape, and every module asks for
the same objects (`kernel_cache` keeps the expensive stages between runs as well). Each answer is a
skip, not a failure, when the corpus is not on the machine; the tests that ask carry the `corpus`
marker, so the default run never does.
"""

from __future__ import annotations

import functools
import sys
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
for _path in (REPO / "research" / "tooling", REPO / "research" / "tooling" / "ghidra" / "driver"):
    if str(_path) not in sys.path:
        sys.path.insert(0, str(_path))


@functools.cache
def corpus_present(records: bool = False) -> bool:
    """The Ghidra corpus the ledger reads; with `records`, the datamap records the shape reads too."""
    try:
        import corpus
        import datamap_layout
        from elysium_pipeline.paths import research_root

        return (corpus._corpus_dir() / "corpus.sqlite").is_file() and \
            (not records or datamap_layout.load(research_root(), "vampire.dll") is not None)
    except Exception:  # noqa: BLE001 -- no work root on this machine
        return False


def _require(records: bool = True) -> None:
    if not corpus_present(records):
        pytest.skip("the Ghidra corpus" + (" or the datamap records are" if records else " is")
                    + " not on this machine")


@functools.cache
def _ledger():
    import kernel_ledger as kl

    return kl.build(kl.MODULE, kl.DEFAULT_DEPTH, REPO)


@functools.cache
def _shape():
    import kernel_ledger as kl
    import kernel_shape as ks

    return ks.build(kl.MODULE, kl.DEFAULT_DEPTH, REPO, ledger=_ledger())


@functools.cache
def _model():
    import gen_kernel_shape as gks

    return gks.model_of(REPO, _shape())


def ledger():
    """`kernel_ledger.build` on the real corpus."""
    _require(records=False)
    return _ledger()


def shape():
    """`kernel_shape.build`'s (shape, rows, signatures), on `ledger()`."""
    _require()
    return _shape()


def model():
    """`gen_kernel_shape.build`'s census model, on `shape()`."""
    _require()
    return _model()
