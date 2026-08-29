"""Contract tests for the KTX2 reader.

The fixture writes level data smallest-level-first, as a conformant writer does, so a
reader that assumes level offsets ascend with the index fails here rather than in the
viewport.
"""

from __future__ import annotations

import struct

import pytest

from core import ktx2

from . import support


def test_level_index_begins_after_the_thirty_two_byte_index_block() -> None:
    # 12 identifier + 36 header + 32 index. Reading the index at 48 is the classic
    # way to get plausible-looking garbage out of a valid file.
    assert ktx2.LEVEL_INDEX_OFFSET == 80


def test_parses_the_declared_header_fields() -> None:
    data, _images = support.build_ktx2(
        vk_format=ktx2.VK_BC3_UNORM, width=64, height=32, levels=4
    )
    texture = ktx2.parse(data)
    assert texture.vk_format == ktx2.VK_BC3_UNORM
    assert (texture.width, texture.height) == (64, 32)
    assert texture.level_count == 4
    assert len(texture.levels) == 4
    assert texture.format.fourcc == b"DXT5"


def test_rejects_bytes_that_are_not_ktx2() -> None:
    with pytest.raises(ktx2.Ktx2Error):
        ktx2.parse(b"\x00" * 128)


def test_level_zero_is_the_largest_level() -> None:
    data, _images = support.build_ktx2(width=64, height=64, levels=5)
    texture = ktx2.parse(data)
    assert (texture.levels[0].width, texture.levels[0].height) == (64, 64)
    assert (texture.levels[4].width, texture.levels[4].height) == (4, 4)


def test_level_offsets_descend_as_the_index_ascends() -> None:
    # The index is ordered largest-first while the data is stored smallest-first.
    data, _images = support.build_ktx2(width=64, height=64, levels=5)
    offsets = [level.byte_offset for level in ktx2.parse(data).levels]
    assert offsets == sorted(offsets, reverse=True)


def test_level_dimensions_floor_at_one() -> None:
    data, _images = support.build_ktx2(width=8, height=1, levels=4)
    texture = ktx2.parse(data)
    assert [(l.width, l.height) for l in texture.levels] == [(8, 1), (4, 1), (2, 1), (1, 1)]


def test_level_count_zero_still_yields_one_entry() -> None:
    # levelCount == 0 asks a loader to generate the chain from the base level.
    data, _images = support.build_ktx2(levels=0)
    texture = ktx2.parse(data)
    assert texture.level_count == 0
    assert len(texture.levels) == 1


def test_a_plain_two_dimensional_texture_has_one_image_per_level() -> None:
    data, images = support.build_ktx2(width=16, height=16, levels=3)
    texture = ktx2.parse(data)
    assert texture.images_per_level == 1
    for level in range(3):
        assert ktx2.image_bytes(data, texture, level) == images[level], f"level={level}"


def test_cubemap_faces_slice_in_declared_order() -> None:
    data, images = support.build_ktx2(faces=6, width=8, height=8, levels=1)
    texture = ktx2.parse(data)
    assert texture.is_cubemap
    assert texture.images_per_level == 6
    for face in range(6):
        assert ktx2.image_bytes(data, texture, 0, face=face) == images[face], f"face={face}"


def test_array_layers_are_the_outer_loop() -> None:
    # Images are concatenated layer, then face, then depth slice.
    data, images = support.build_ktx2(layers=3, faces=6, width=8, height=8)
    texture = ktx2.parse(data)
    assert texture.is_array
    assert texture.images_per_level == 18
    assert ktx2.image_bytes(data, texture, 0, layer=2, face=1) == images[2 * 6 + 1]


def test_layer_count_zero_means_one_element_not_none() -> None:
    data, _images = support.build_ktx2(layers=0)
    texture = ktx2.parse(data)
    assert texture.layers == 1
    assert not texture.is_array


@pytest.mark.parametrize("kwargs", [{"level": 9}, {"face": 6}, {"layer": 2}])
def test_rejects_indices_outside_the_declared_ranges(kwargs: dict) -> None:
    data, _images = support.build_ktx2(faces=6, layers=2, levels=2)
    texture = ktx2.parse(data)
    with pytest.raises(ktx2.Ktx2Error):
        ktx2.image_bytes(data, texture, **kwargs)


def test_refuses_supercompressed_payloads_rather_than_returning_garbage() -> None:
    data, _images = support.build_ktx2(supercompression=ktx2.SUPERCOMPRESSION_ZSTD)
    texture = ktx2.parse(data)
    with pytest.raises(ktx2.Ktx2Error):
        ktx2.image_bytes(data, texture, 0)


@pytest.mark.parametrize(
    ("vk_format", "block_bytes"),
    [
        (ktx2.VK_BC1_RGBA_UNORM, 8),
        (ktx2.VK_BC2_UNORM, 16),
        (ktx2.VK_BC3_UNORM, 16),
    ],
)
def test_block_sizes_match_the_declared_format(vk_format: int, block_bytes: int) -> None:
    data, _images = support.build_ktx2(vk_format=vk_format, width=8, height=8)
    texture = ktx2.parse(data)
    # 8x8 is four 4x4 blocks.
    assert ktx2.expected_image_bytes(texture, 0) == 4 * block_bytes


def test_uncompressed_rows_are_tightly_packed() -> None:
    # Three-byte texels get no padding to a four-byte boundary.
    data, _images = support.build_ktx2(
        vk_format=ktx2.VK_R8G8B8_UNORM, width=5, height=3, levels=1
    )
    texture = ktx2.parse(data)
    assert ktx2.expected_image_bytes(texture, 0) == 5 * 3 * 3


def test_declared_and_actual_image_sizes_agree() -> None:
    data, _images = support.build_ktx2(width=32, height=16, levels=4, faces=6)
    texture = ktx2.parse(data)
    for level in range(4):
        actual = len(ktx2.image_bytes(data, texture, level))
        assert actual == ktx2.expected_image_bytes(texture, level), f"level={level}"


def test_an_unknown_format_is_named_rather_than_guessed() -> None:
    data, _images = support.build_ktx2()
    broken = bytearray(data)
    struct.pack_into("<I", broken, 12, 999)
    texture = ktx2.parse(bytes(broken))
    assert texture.format is None
    with pytest.raises(ktx2.Ktx2Error):
        ktx2.expected_image_bytes(texture, 0)
