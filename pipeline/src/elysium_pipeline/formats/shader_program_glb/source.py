"""The install closures for the two shader-program unit kinds.

Both members resolve UP-first and independently. A compiled `psh/` bundle whose stem matches a
readable source reads that source too -- it is what `sourceComparison` assembles against -- but
the source stays a hashed dependency rather than a second member: its bytes are the
`vtmb:shader-source:` unit's to account for, and a program unit that took them as a member would
owe a second byte ledger over text it does not restate.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
from typing import Callable

from elysium_pipeline.formats.shader_program_glb.model import (
    PROGRAM_DIR,
    PROGRAM_SUBDIRS,
    PROGRAM_SUFFIX,
    SHADER_PROGRAM_KIND,
    SHADER_SOURCE_KIND,
    SOURCE_DIR,
    SOURCE_SUFFIX,
    normalize_program_key,
    normalize_source_key,
    program_asset_id,
    program_member_path,
    source_asset_id,
    source_member_path,
)
from elysium_pipeline.formats.unit_contract import (
    SourceMember,
    missing_sentinel,
    origin_of,
    read_member,
)


class ShaderSourceError(RuntimeError):
    """The selected shader member is absent from the install."""


@dataclass(frozen=True, slots=True)
class TwinSource:
    """A readable source a compiled program is cross-checked against, by identity and bytes."""

    key: str
    asset_id: str
    path: str
    data: bytes

    @property
    def sha256(self) -> str:
        return hashlib.sha256(self.data).hexdigest()

    @property
    def byte_length(self) -> int:
        return len(self.data)


@dataclass(frozen=True, slots=True)
class ShaderSourceClosure:
    """One readable `materials/dxshaders/<stem>.psh` and what the install holds beside it."""

    key: str
    asset_id: str
    psh: SourceMember
    compiled_twin_path: str
    compiled_twin_present: bool

    @property
    def compiled_twin_asset(self) -> str:
        """The compiled twin's stable ID, or the missing sentinel that stands in its place.

        A named unit the install does not hold keeps its authored spelling in the
        `vtmb:missing-shader-program:` namespace, which is what lets the unit state the reference
        it makes without claiming a unit exists for it.
        """

        if not self.compiled_twin_present:
            return missing_sentinel(SHADER_PROGRAM_KIND, f"psh/{self.key}")
        return program_asset_id(f"psh/{self.key}")

    def members(self) -> tuple[SourceMember, ...]:
        return (self.psh,)


@dataclass(frozen=True, slots=True)
class ShaderProgramClosure:
    """One compiled `shaders/<subdir>/<stem>.vcs`, and the readable twin it is checked against."""

    key: str
    asset_id: str
    vcs: SourceMember
    twin: TwinSource | None
    twin_path: str
    twin_expected: bool
    twin_present: bool = False

    @property
    def stem(self) -> str:
        return self.key.split("/")[-1]

    @property
    def twin_asset(self) -> str | None:
        """The readable source's stable ID, the missing sentinel, or None where none is named.

        A `vsh/` or `fxc/` bundle names no readable source at all: `materials/dxshaders/` holds
        pixel-shader assembly, so a same-stem member there is a different program of that name
        rather than the source this bundle was built from.
        """

        if not self.twin_expected:
            return None
        if self.twin_present:
            return source_asset_id(self.stem)
        return missing_sentinel(SHADER_SOURCE_KIND, self.stem)

    def members(self) -> tuple[SourceMember, ...]:
        return (self.vcs,)


def shader_source_keys(index: dict) -> list[str]:
    """Every `materials/dxshaders/*.psh` stem the install resolves, folded and sorted."""

    prefix, suffix = SOURCE_DIR + "/", SOURCE_SUFFIX
    return sorted(
        path[len(prefix):-len(suffix)]
        for path in index
        if path.startswith(prefix) and path.endswith(suffix) and "/" not in path[len(prefix):]
    )


def program_keys(index: dict) -> list[str]:
    """Every `shaders/{psh,vsh,fxc}/*.vcs` member the install resolves, as `<subdir>/<stem>`."""

    keys: list[str] = []
    for subdir in PROGRAM_SUBDIRS:
        prefix = f"{PROGRAM_DIR}/{subdir}/"
        keys.extend(
            path[len(f"{PROGRAM_DIR}/"):-len(PROGRAM_SUFFIX)]
            for path in index
            if path.startswith(prefix)
            and path.endswith(PROGRAM_SUFFIX)
            and "/" not in path[len(prefix):]
        )
    return sorted(keys)


def _member(index: dict, path: str, role: str, read_bytes) -> SourceMember:
    """One resolved member, read through the contract's own install reader.

    A member the install carries as zero bytes is a member: the unit publishes it with an
    `empty-member` omission rather than refusing, and only a key the install does not resolve at
    all is an error here.
    """

    entry = index.get(path)
    data = read_member(index, path, read_bytes=read_bytes) if entry else None
    if data is None:
        raise ShaderSourceError(f"missing required shader member: {path}")
    return SourceMember(role=role, path=path, data=data, origin=origin_of(entry))


def load_shader_source_closure(
    index: dict,
    key: str,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
) -> ShaderSourceClosure:
    """The closure for one `vtmb:shader-source:` unit."""

    normalized = normalize_source_key(key)
    path = source_member_path(normalized)
    member = _member(index, path, "psh", read_bytes)
    twin_path = f"{PROGRAM_DIR}/psh/{normalized}{PROGRAM_SUFFIX}"
    return ShaderSourceClosure(
        key=normalized,
        asset_id=source_asset_id(normalized),
        psh=member,
        compiled_twin_path=twin_path,
        compiled_twin_present=twin_path in index,
    )


def load_source_closure(
    index: dict,
    key: str,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
) -> ShaderProgramClosure:
    """The closure for one `vtmb:shader-program:` unit, the package's selecting kind.

    Only a `psh/` program has a readable twin: `materials/dxshaders/` ships pixel-shader assembly
    alone, so a same-stem `vsh/` or `fxc/` member is a different program of the same name rather
    than the source it was built from.
    """

    normalized = normalize_program_key(key)
    subdir, stem = normalized.split("/")
    path = program_member_path(normalized)
    member = _member(index, path, "vcs", read_bytes)
    twin_path = f"{SOURCE_DIR}/{stem}{SOURCE_SUFFIX}"
    twin_expected = subdir == "psh"
    twin_present = twin_path in index
    twin: TwinSource | None = None
    if twin_expected and twin_present:
        data = read_member(index, twin_path, read_bytes=read_bytes)
        if data is not None:
            twin = TwinSource(
                key=stem,
                asset_id=source_asset_id(stem),
                path=twin_path,
                data=data,
            )
    return ShaderProgramClosure(
        key=normalized,
        asset_id=program_asset_id(normalized),
        vcs=member,
        twin=twin,
        twin_path=twin_path,
        twin_expected=twin_expected,
        twin_present=twin_present,
    )
