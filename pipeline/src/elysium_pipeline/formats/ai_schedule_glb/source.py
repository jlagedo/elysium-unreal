"""Resolving this seam's one source member, and enumerating the units it yields.

Every unit of this seam is cut from the same file, `dlls/vampire.dll`, so the closure is one
`read_member` and the key set comes from the census rather than from the install index: the index
knows the image is there, and only the image knows how many id spaces it holds.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Callable

from elysium_pipeline.formats.ai_schedule_glb import census as census_module
from elysium_pipeline.formats.ai_schedule_glb.image import ImageError
from elysium_pipeline.formats.ai_schedule_glb.model import ROOT_KEY, SOURCE_MEMBER, normalize_key
from elysium_pipeline.formats.unit_contract.origin import Origin, origin_of, read_member


class AiScheduleSourceError(ValueError):
    """The install does not carry the image this seam is cut from."""


@dataclass(frozen=True, slots=True)
class AiScheduleSourceClosure:
    """The image, its origin, and the census read from it."""

    key: str
    data: bytes
    origin: Origin
    census: census_module.Census


def _read_image(
    index: dict,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
) -> tuple[bytes, Origin]:
    entry = index.get(SOURCE_MEMBER)
    if entry is None:
        raise AiScheduleSourceError(f"missing required member: {SOURCE_MEMBER}")
    data = read_member(index, SOURCE_MEMBER, read_bytes=read_bytes)
    if data is None:
        raise AiScheduleSourceError(f"missing required member: {SOURCE_MEMBER}")
    return data, origin_of(entry)


def source_keys(
    index: dict,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
) -> list[str]:
    """Every unit key this image yields: one per id space, plus the root, sorted.

    The root sorts with the rest rather than being appended, so the plural export's order is one
    rule and not "the classes, and then the odd one".
    """

    try:
        data, _ = _read_image(index, read_bytes=read_bytes)
    except AiScheduleSourceError:
        return []                      # an install with no image yields no units
    try:
        census = census_module.build(data)
    except ImageError:
        # The member is there and is not a PE32 at all, so it is not the image this seam reads and
        # it yields nothing. A `CensusError` is the other case -- a PE whose shape this seam cannot
        # recover -- and that is NOT swallowed: an image we half-understand must fail loudly rather
        # than quietly publish zero schedules.
        return []
    return sorted({owner.key for owner in census.owners} | {ROOT_KEY})


def load_source_closure(
    index: dict,
    key: str,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
) -> AiScheduleSourceClosure:
    normalized = normalize_key(key)
    data, origin = _read_image(index, read_bytes=read_bytes)
    census = census_module.build(data)
    if normalized != ROOT_KEY and normalized not in census.by_key():
        raise AiScheduleSourceError(
            f"{normalized}: this image holds no id space owned by that class"
        )
    return AiScheduleSourceClosure(key=normalized, data=data, origin=origin, census=census)


__all__ = [
    "AiScheduleSourceClosure",
    "AiScheduleSourceError",
    "load_source_closure",
    "source_keys",
]
