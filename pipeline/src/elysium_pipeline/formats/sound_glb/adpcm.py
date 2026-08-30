"""Microsoft ADPCM, decoded block by block with the coefficient pairs the `fmt ` chunk declares.

The decode is the integer arithmetic the format specifies exactly, so the samples are recoverable
completely from the encoded range: the `data` body is graded `derived`, and the accessor it
produces states the same waveform the source stored, not a second interpretation of it.
"""

from __future__ import annotations

from array import array
from typing import Sequence

#: The delta adaptation the format fixes; indexed by the encoded nibble.
ADAPTATION_TABLE = (
    230, 230, 230, 230, 307, 409, 512, 614,
    768, 614, 512, 409, 307, 230, 230, 230,
)

#: The seven coefficient pairs every shipped member declares. Carried so a member that declares a
#: different set still decodes from its own `fmt ` chunk rather than from this table.
STANDARD_COEFFICIENTS = (
    (256, 0), (512, -256), (0, 0), (192, 64), (240, 0), (460, -208), (392, -232),
)

#: The preamble each channel writes at the head of a block: predictor, delta, sample1, sample2.
PREAMBLE_BYTES = 7


class AdpcmDecodeError(ValueError):
    """A block cannot be decoded from the format the member declares."""


def samples_per_block(block_align: int, channels: int) -> int:
    """How many sample frames a full block of `block_align` bytes carries."""

    if channels <= 0:
        raise AdpcmDecodeError(f"a block cannot carry {channels} channels")
    nibble_bytes = block_align - PREAMBLE_BYTES * channels
    if nibble_bytes < 0:
        raise AdpcmDecodeError(
            f"blockAlign {block_align} is shorter than the {channels}-channel preamble"
        )
    return 2 + nibble_bytes * 2 // channels


def _truncate(numerator: int) -> int:
    """`numerator / 256` as the format specifies it: integer division toward zero.

    Python's `//` rounds toward minus infinity, which puts every negative predictor one LSB below
    what a reference decoder produces, so the sign is handled explicitly.
    """

    return numerator // 256 if numerator >= 0 else -((-numerator) // 256)


def _clamp(value: int) -> int:
    if value > 32767:
        return 32767
    if value < -32768:
        return -32768
    return value


def decode_block(
    block: bytes,
    channels: int,
    coefficients: Sequence[Sequence[int]],
    output: array,
) -> int:
    """Decode one block into `output`, interleaved by channel. Returns the frames appended."""

    if channels not in (1, 2):
        raise AdpcmDecodeError(f"MS ADPCM carries 1 or 2 channels, not {channels}")
    if len(block) < PREAMBLE_BYTES * channels:
        raise AdpcmDecodeError(
            f"a {channels}-channel block is at least {PREAMBLE_BYTES * channels} bytes, "
            f"not {len(block)}"
        )
    predictor_index = [block[channel] for channel in range(channels)]
    for index in predictor_index:
        if index >= len(coefficients):
            raise AdpcmDecodeError(
                f"a block selects coefficient pair {index} of {len(coefficients)}"
            )
    cursor = channels
    delta = []
    for channel in range(channels):
        delta.append(int.from_bytes(block[cursor:cursor + 2], "little", signed=True))
        cursor += 2
    sample1 = []
    for channel in range(channels):
        sample1.append(int.from_bytes(block[cursor:cursor + 2], "little", signed=True))
        cursor += 2
    sample2 = []
    for channel in range(channels):
        sample2.append(int.from_bytes(block[cursor:cursor + 2], "little", signed=True))
        cursor += 2

    coefficient_one = [coefficients[index][0] for index in predictor_index]
    coefficient_two = [coefficients[index][1] for index in predictor_index]

    # The block states the two most recent samples in reverse order; they are output before any
    # nibble is read, which is why a full block carries `2 + nibbles/channels` frames.
    for channel in range(channels):
        output.append(sample2[channel])
    for channel in range(channels):
        output.append(sample1[channel])
    frames = 2

    nibble_bytes = len(block) - cursor
    total_nibbles = (nibble_bytes * 2 // channels) * channels
    channel = 0
    for position in range(total_nibbles):
        byte = block[cursor + position // 2]
        nibble = (byte >> 4) if position % 2 == 0 else (byte & 0x0F)
        signed = nibble - 16 if nibble > 7 else nibble
        predicted = _truncate(sample1[channel] * coefficient_one[channel]
                              + sample2[channel] * coefficient_two[channel])
        predicted = _clamp(predicted + signed * delta[channel])
        output.append(predicted)
        sample2[channel] = sample1[channel]
        sample1[channel] = predicted
        step = (ADAPTATION_TABLE[nibble] * delta[channel]) // 256
        delta[channel] = step if step >= 16 else 16
        channel = (channel + 1) % channels
    return frames + total_nibbles // channels


def decode(
    data: bytes,
    channels: int,
    block_align: int,
    coefficients: Sequence[Sequence[int]],
) -> tuple[array, int, int, int]:
    """Decode a whole `data` body. Returns (samples, frames, blocks, bytes consumed).

    The block count decoded is what the `data` chunk holds: a final partial block is decoded for
    the frames its nibbles carry rather than padded up to `blockAlign`. A tail too short to carry
    a preamble decodes to nothing and is left for the caller to account for as its own range.
    """

    if block_align <= 0:
        raise AdpcmDecodeError(f"blockAlign {block_align} is not a block size")
    output = array("h")
    frames = 0
    blocks = 0
    position = 0
    while position < len(data):
        block = data[position:position + block_align]
        if len(block) < PREAMBLE_BYTES * channels:
            break                       # too short to carry a preamble; the caller accounts it
        frames += decode_block(block, channels, coefficients, output)
        blocks += 1
        position += len(block)
    return output, frames, blocks, position
