"""The two members of one script as the install resolves them.

The `.py` selects the unit and resolves UP-first; the same-stem `.pyc` resolves UP-first
independently and joins as the companion. CPython 2.1 has no `zipimport`, so the interpreter
reads only the loose filesystem: a `.pyc` that ships only inside a VPK is provenance and never
executes, and each member says which of the two it is.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Callable

from elysium_pipeline.formats.script_glb.model import (
    COMPILED_EXTENSION,
    SCRIPT_ROOT,
    SOURCE_EXTENSION,
    asset_id,
    compiled_path,
    normalize_script_key,
    source_path,
)
from elysium_pipeline.formats.unit_contract import SourceMember, origin_of, read_member

#: The install trees this seam needs indexed. `install.ASSET_DIRS` walks the converter trees
#: only, and `python/` is not one of them, so a caller that wants the loose sources -- the ones
#: the interpreter actually executes -- passes these. `sound/` and `dlg/` are here because the
#: seam resolves the members its literals name, not because it reads them.
EXTRA_DIRS = ("python", "sound", "dlg")


class ScriptSourceError(RuntimeError):
    """The requested script is absent from the install, or resolves to no member at all."""


@dataclass(frozen=True, slots=True)
class ScriptSourceClosure:
    """One script's members: the executed source, the compiled companion, or both."""

    key: str
    asset: str
    source_kind: str
    py: SourceMember | None
    pyc: SourceMember | None
    #: Whether each member is one the interpreter can execute, keyed by role.
    executed: dict[str, bool] = field(default_factory=dict)

    def members(self) -> tuple[SourceMember, ...]:
        return tuple(member for member in (self.py, self.pyc) if member is not None)


def script_dirs(asset_dirs: tuple[str, ...]) -> tuple[str, ...]:
    """`asset_dirs` widened with the trees this seam reads, in a stable order."""

    return tuple(asset_dirs) + tuple(
        name for name in EXTRA_DIRS if name not in tuple(asset_dirs)
    )


def _member(
    index: dict,
    path: str,
    role: str,
    read_bytes: Callable[[dict, str], bytes | None] | None,
) -> SourceMember | None:
    entry = index.get(path)
    if entry is None:
        return None
    data = read_member(index, path, read_bytes=read_bytes)
    if data is None:
        return None
    return SourceMember(role=role, path=path, data=data, origin=origin_of(entry))


def source_keys(index: dict) -> list[str]:
    """Every script key the index resolves, in sorted order.

    A `.py` names a unit whatever else the install holds for its stem; a `.pyc` names one only
    when no `.py` sibling resolves, because otherwise the two are members of one unit. A member
    below `python/` under any other extension is a corpus-index residue row, not a unit:
    `python/warehouse/warehouse.old` is Python source the interpreter never imports.
    """

    sources: set[str] = set()
    compiled: set[str] = set()
    for key in index:
        if not key.startswith(SCRIPT_ROOT):
            continue
        if key.endswith(SOURCE_EXTENSION):
            sources.add(key[len(SCRIPT_ROOT): -len(SOURCE_EXTENSION)])
        elif key.endswith(COMPILED_EXTENSION):
            compiled.add(key[len(SCRIPT_ROOT): -len(COMPILED_EXTENSION)])
    return sorted(sources | (compiled - sources))


def load_source_closure(
    index: dict,
    key: str,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
) -> ScriptSourceClosure:
    """Resolve both members of one script independently through the supplied UP-first index."""

    folded = normalize_script_key(key)
    py = _member(index, source_path(folded), "py", read_bytes)
    pyc = _member(index, compiled_path(folded), "pyc", read_bytes)
    if py is None and pyc is None:
        raise ScriptSourceError(
            f"the install resolves neither {source_path(folded)} nor {compiled_path(folded)}"
        )
    if py is not None and pyc is not None:
        source_kind = "py+pyc"
    elif py is not None:
        source_kind = "py"
    else:
        source_kind = "pyc-only"
    executed = {}
    if py is not None:
        executed["py"] = py.origin.kind == "loose"
    if pyc is not None:
        # The 2.1 interpreter has no `zipimport`; a companion that ships only inside a VPK is
        # provenance the engine never opens.
        executed["pyc"] = pyc.origin.kind == "loose"
    return ScriptSourceClosure(
        key=folded,
        asset=asset_id(folded),
        source_kind=source_kind,
        py=py,
        pyc=pyc,
        executed=executed,
    )
