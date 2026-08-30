"""The install's up-first answer for one image identity."""

from __future__ import annotations

from typing import Callable

from elysium_pipeline.formats.image_glb.model import (
    ImageSourceClosure,
    asset_id,
    container_of,
    normalize_image_path,
)
from elysium_pipeline.formats.unit_contract.origin import SourceMember, origin_of


class ImageSourceError(RuntimeError):
    """The selected image member is absent from the install."""


def load_source_closure(
    index: dict,
    key: str,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
) -> ImageSourceClosure:
    if read_bytes is None:
        from elysium_pipeline.formats import install

        read_bytes = install.read
    normalized = normalize_image_path(key)
    entry = index.get(normalized)
    data = read_bytes(index, normalized) if entry is not None else None
    if data is None:
        raise ImageSourceError(f"missing required image member: {normalized}")
    member = SourceMember(role="image", path=normalized, data=data, origin=origin_of(entry))
    return ImageSourceClosure(normalized, asset_id(normalized), container_of(normalized), member)
