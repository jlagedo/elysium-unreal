"""Measure which bytes of a model image the exporter's own decoder reads.

Library module for `verify_byte_coverage`; it has no command of its own.

CAP4.2 subtracts our decoder's byte coverage from the union retail dereferenced,
so the subtrahend has to be *measured* rather than transcribed. A second walk
written from the same understanding that wrote the decoder would agree with it
by construction and the difference would be a tautology in the other direction --
the same failure `resolve_consumed_spans` avoids on the retail side. So this
module runs `elysium_pipeline.formats.mdl_skel` unmodified and observes it.

Observation is possible without touching the decoder because every read it
performs goes through one of two routes. `struct.unpack_from(fmt, image, offset)`
carries its own length in the format, including the dynamic `f"<{valid}h"` the
RLE walk builds, and `mdl_skel` reaches it through a plain module-global
`struct` -- so replacing that one name intercepts the helper lambdas and the
inline calls alike. What is left is raw indexing: the two run-header bytes
`_rle_channel` reads as `v[p]`, and the terminator scan plus slice in `_cstr`.
A proxy image covers those.

**An unmodelled access is an error, not a silent zero.** `TrackingImage` raises
on every route it does not measure rather than delegating to `bytes`, because a
decoder change that reads by a new route would otherwise quietly shrink our side
of the difference and read as retail requiring bytes we already handle. The
harness would rather fail loudly than under-report.

Two bounds travel with anything measured here.

The frame is the animation path. Retail's spans come from the animation
evaluation hooks, so geometry, material, skin and flex reads are outside the
comparison on both sides; a caller drives the bone, sequence and animation
entry points and nothing else.

A read is recorded where it lands, not what it meant. Attribution to a field or
an identity is the analyzer's, derived from offsets and from the order the reads
arrived in. This module records offsets and lengths.
"""

from __future__ import annotations

import struct as _struct
from contextlib import contextmanager
from typing import Any, Iterator

from research.tooling.capture.resolve_consumed_spans import Coverage


class UnmeasuredRead(Exception):
    """The decoder reached the image by a route this harness does not measure."""


#: `Coverage` tags every span with a role. Ours has one: the decoder read it.
#: What a read meant is the analyzer's question, answered from the offset.
DECODER_READ = "decoder_read"


class Recorder:
    """Every read one instrumented drive performed, in order and as a union.

    Both shapes are kept because they answer different questions. The union is
    what the difference is computed against; the ordered trace is what lets a
    read that lands outside every array -- a name string, a descriptor the walk
    followed to -- be attributed to the record that caused it.
    """

    __slots__ = ("size", "trace", "reads", "outside")

    def __init__(self, size: int) -> None:
        self.size = size
        self.trace: list[tuple[int, int]] = []
        self.reads = 0
        self.outside = 0

    def read(self, offset: int, size: int) -> None:
        self.reads += 1
        if size <= 0:
            return
        if offset < 0 or offset + size > self.size:
            # Recorded rather than raised: a decoder running off the image is a
            # finding about the decoder, and swallowing the walk here would lose
            # every span it had already produced.
            self.outside += 1
            return
        self.trace.append((offset, size))

    def spans(self) -> list[tuple[int, int, str]]:
        return [(offset, offset + size, DECODER_READ) for offset, size in self.trace]

    def coverage(self) -> Coverage:
        covered = Coverage(self.size)
        covered.add(self.spans())
        return covered


class TrackingImage:
    """The model image as `mdl_skel` addresses it, with every read recorded.

    Only the routes the animation path actually uses are implemented. `__len__`
    is allowed and not recorded, because a length is metadata rather than a byte
    read; everything else raises.
    """

    __slots__ = ("_data", "_recorder")

    def __init__(self, data: bytes, recorder: Recorder) -> None:
        self._data = data
        self._recorder = recorder

    def __len__(self) -> int:
        return len(self._data)

    def __getitem__(self, item: Any) -> Any:
        if isinstance(item, int):
            index = item if item >= 0 else len(self._data) + item
            self._recorder.read(index, 1)
            return self._data[item]
        if isinstance(item, slice):
            start, stop, step = item.indices(len(self._data))
            if step != 1:
                raise UnmeasuredRead(f"strided slice {item!r} is not measured")
            self._recorder.read(start, max(0, stop - start))
            return self._data[item]
        raise UnmeasuredRead(f"index by {type(item).__name__} is not measured")

    def index(self, sub: bytes, start: int = 0, stop: int | None = None) -> int:
        """`_cstr`'s terminator scan.

        The scan reads every byte it stepped over *and* the terminator it
        stopped on, so the recorded span ends past the match rather than at it.
        """
        found = (
            self._data.index(sub, start)
            if stop is None
            else self._data.index(sub, start, stop)
        )
        self._recorder.read(start, found + len(sub) - start)
        return found

    def __getattr__(self, name: str) -> Any:
        raise UnmeasuredRead(f"bytes.{name} is not measured")


def _unwrap(buffer: Any) -> Any:
    return buffer._data if isinstance(buffer, TrackingImage) else buffer


class TrackingStruct:
    """A stand-in for the `struct` module that records every offset read.

    `unpack_from` is the only entry that carries an offset, so it is the only
    one that records. `unpack` is refused on a tracked image on purpose: it
    would read from zero with no offset to record, and the only legitimate way
    to reach it is on bytes a measured slice already produced.
    """

    __slots__ = ("_recorder", "error", "Struct")

    def __init__(self, recorder: Recorder) -> None:
        self._recorder = recorder
        self.error = _struct.error
        self.Struct = _struct.Struct

    def unpack_from(self, fmt: str, buffer: Any, offset: int = 0) -> tuple:
        self._recorder.read(offset, _struct.calcsize(fmt))
        return _struct.unpack_from(fmt, _unwrap(buffer), offset)

    def calcsize(self, fmt: str) -> int:
        return _struct.calcsize(fmt)

    def unpack(self, fmt: str, buffer: Any) -> tuple:
        if isinstance(buffer, TrackingImage):
            raise UnmeasuredRead("struct.unpack over the whole image is not measured")
        return _struct.unpack(fmt, buffer)

    def pack(self, fmt: str, *values: Any) -> bytes:
        return _struct.pack(fmt, *values)

    def __getattr__(self, name: str) -> Any:
        raise UnmeasuredRead(f"struct.{name} is not measured")


@contextmanager
def instrumented(module: Any, image: bytes) -> Iterator[tuple[TrackingImage, Recorder]]:
    """Run a decoder module against one image with every read recorded.

    The module's own `struct` global is swapped and restored, so nothing about
    the decoder changes and instrumentation cannot survive the block -- a leaked
    shim would silently record another test's reads into this recorder.
    """
    recorder = Recorder(len(image))
    original = module.struct
    module.struct = TrackingStruct(recorder)
    try:
        yield TrackingImage(image, recorder), recorder
    finally:
        module.struct = original
