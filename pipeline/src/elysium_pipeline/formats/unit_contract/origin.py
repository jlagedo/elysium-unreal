"""Where a unit's bytes came from, and the member table it publishes them as.

Every member resolves UP-first and independently of every other member -- Unofficial_Patch loose,
then retail loose, then the retail VPKs -- so `origin` records the search-path answer for that one
member rather than a policy the unit applied as a whole.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import os
from pathlib import Path
from typing import Any, Callable, Iterable, Sequence

#: The resolution policy every export_v2 unit declares.
SOURCE_POLICY = "up-first"

ORIGIN_KINDS = ("loose", "vpk", "bsp-pakfile")

#: The install subdirectories that are their own loose search root. A loose member below one of
#: them names it, so a reader can tell a patched member from a retail one without the absolute
#: path the exporting machine happened to use.
LOOSE_ROOT_NAMES = ("unofficial_patch", "vampire")


class OriginError(ValueError):
    """An origin does not carry the fields its kind requires."""


@dataclass(frozen=True, slots=True)
class Origin:
    """One member's place in the install.

    `loose` names the install subdirectory; `vpk` names the pack and the member's extent inside
    it; `bsp-pakfile` names the map, the member inside its PAKFILE lump, and the BSP's own origin,
    so a packed member stays traceable to the search-path answer that found the map.
    """

    kind: str
    root: str | None = None
    container: str | None = None
    offset: int | None = None
    size: int | None = None
    map: str | None = None
    member: str | None = None
    origin: "Origin | None" = None

    def __post_init__(self) -> None:
        if self.kind not in ORIGIN_KINDS:
            raise OriginError(f"unknown source origin kind {self.kind!r}")
        if self.kind == "loose" and not self.root:
            raise OriginError("a loose origin names the install subdirectory it was found in")
        if self.kind == "vpk":
            if not self.container or self.offset is None or self.size is None:
                raise OriginError("a vpk origin names its container, offset and size")
        if self.kind == "bsp-pakfile":
            if not self.map or not self.member:
                raise OriginError("a bsp-pakfile origin names its map and member")
            if not isinstance(self.origin, Origin):
                raise OriginError("a bsp-pakfile origin nests the BSP's own origin")

    def to_json(self) -> dict[str, Any]:
        if self.kind == "loose":
            return {"kind": "loose", "root": str(self.root)}
        if self.kind == "vpk":
            return {
                "kind": "vpk",
                "container": str(self.container),
                "offset": int(self.offset or 0),
                "size": int(self.size or 0),
            }
        assert self.origin is not None                      # __post_init__ proved it
        return {
            "kind": "bsp-pakfile",
            "map": str(self.map),
            "member": str(self.member),
            "origin": self.origin.to_json(),
        }


def origin_of(entry: tuple[str, Any]) -> Origin:
    """The origin of one `install.build_index` value: `("loose", path)` or `("vpk", entry)`."""

    kind, value = entry
    if kind == "loose":
        path = Path(value)
        lowered = [part.lower() for part in path.parts]
        root = "loose"
        for candidate in LOOSE_ROOT_NAMES:
            if candidate in lowered:
                root = path.parts[lowered.index(candidate)]
                break
        return Origin(kind="loose", root=root)
    if kind == "vpk":
        pack, offset, size = value
        return Origin(
            kind="vpk",
            container=os.path.basename(pack),
            offset=int(offset),
            size=int(size),
        )
    raise OriginError(f"unknown install index entry kind {kind!r}")


def pakfile_origin(map_name: str, member: str, bsp_origin: Origin) -> Origin:
    """A member of one BSP's PAKFILE lump, carrying the BSP's own origin underneath it."""

    return Origin(kind="bsp-pakfile", map=map_name, member=member, origin=bsp_origin)


@dataclass(frozen=True, slots=True)
class SourceMember:
    """One member the unit was decoded from, with the bytes it is authoritative for.

    A member cut from part of a larger file carries `span`; `data` is then the span's bytes
    alone, because the identity the unit publishes -- and the ledger it proves -- is the span's.
    """

    role: str
    path: str
    data: bytes
    origin: Origin
    span: tuple[int, int] | None = None

    def __post_init__(self) -> None:
        if not self.role:
            raise OriginError(f"{self.path}: a source member names the role it fills")
        if not isinstance(self.origin, Origin):
            raise OriginError(f"{self.path}: a source member carries a resolved origin")
        if self.span is not None:
            offset, length = self.span
            if int(offset) < 0 or int(length) < 0:
                raise OriginError(f"{self.path}: span {offset}+{length} is not a file range")
            if int(length) != len(self.data):
                raise OriginError(
                    f"{self.path}: span claims {length} bytes and the member holds "
                    f"{len(self.data)}"
                )

    @property
    def sha256(self) -> str:
        return hashlib.sha256(self.data).hexdigest()

    @property
    def byte_length(self) -> int:
        return len(self.data)

    @property
    def span_offset(self) -> int:
        """Where the member's first byte sits in the file it was cut from."""

        return int(self.span[0]) if self.span else 0

    def to_json(self) -> dict[str, Any]:
        row: dict[str, Any] = {
            "role": self.role,
            "path": self.path,
            "origin": self.origin.to_json(),
            "byteLength": self.byte_length,
            "sha256": self.sha256,
        }
        if self.span is not None:
            row["span"] = {"offset": int(self.span[0]), "length": int(self.span[1])}
        return row


def source_resolution(members: Sequence[SourceMember] | Iterable[SourceMember]) -> dict[str, Any]:
    """The `sourceResolution` block: the policy, then every member the unit decoded from."""

    return {"policy": SOURCE_POLICY, "members": [member.to_json() for member in members]}


def read_member(
    index: dict,
    key: str,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
) -> bytes | None:
    """The install's bytes for one key, or None when the install carries no member for it.

    `read_bytes` is the injection point a seam's tests use to stand a synthetic install up
    without touching the real one.
    """

    if read_bytes is None:
        from elysium_pipeline.formats import install

        read_bytes = install.read
    return read_bytes(index, key)
