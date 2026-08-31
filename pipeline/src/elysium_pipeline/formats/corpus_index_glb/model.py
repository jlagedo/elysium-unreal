"""Identity, vocabulary and record shapes for the corpus-index GLB seam.

`docs/architecture/seam_map_corpus_index.md` owns the facts; this module is their code. There is
exactly one corpus index per export root: a scene-less unit with no BIN chunk and no byte ledger,
because it is a product over other products and every byte it describes is ledgered by the unit
that owns it.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import PurePosixPath
from typing import Any

from elysium_pipeline.formats.unit_contract import extension_name, identity_block

KIND = "corpus-index"
KIND_TITLE = "Corpus-index"
SCHEMA_VERSION = "1.0.0"
CORPUS_INDEX_EXTENSION = extension_name(KIND)

#: The one identity this seam publishes. It carries no key: one export root holds one index.
ASSET_ID = f"vtmb:{KIND}"

#: The file name, directly below `$ELYSIUM_EXPORT_V2_ROOT`.
OUTPUT_NAME = "index.glb"

#: The four states a walked member can be in.
DISPOSITIONS = ("unit", "companion", "residue", "unclaimed")

#: The six evidence-backed residue categories, in the order the seam map tabulates them.
RESIDUE_CATEGORIES = (
    "authoring-leftover",
    "unreachable-member",
    "engine-binary",
    "user-data",
    "foreign-file",
    "excluded-by-decision",
)

#: The ten properties no single unit can validate, in the order the seam map tabulates them.
CHECK_NAMES = (
    "surface-property-inheritance",
    "model-include-tree",
    "scene-expression-rows",
    "surface-sound-scripts",
    "nav-graph-stamp",
    "font-list",
    "dialogue-line-audio",
    "map-partition",
    "texture-material-roles",
    "map-references-published",
)

#: The only two things the walk leaves out, each with the reason it is not a member.
EXCLUDED_TREES = (
    {
        "path": "pack*.vpk",
        "reason": "the VPK container files are the source of their members, not members",
    },
    {
        "path": "maps/graphs/",
        "reason": "loose tree the retail engine writes while it runs; the VPK-shipped "
                  "maps/graphs/*.ain and *.loc members are the nav-graph seam's and are kept",
    },
    {
        "path": "maps/soundcache/",
        "reason": "loose tree the retail engine writes while it runs",
    },
)

#: The loose subtrees below an install root that the walk skips, lower-cased and slash-terminated.
EXCLUDED_LOOSE_PREFIXES = ("maps/graphs/", "maps/soundcache/")

#: The sections `coverage.mapped[]` grades. Coverage grades the walk rather than bytes here.
COVERAGE_SECTIONS = (
    "sourceResolution",
    "excludedTrees",
    "members",
    "units",
    "references",
    "inverse",
    "danglingReferences",
    "orphans",
    "crossUnitChecks",
    "census",
    "summary",
)

#: The dependency role every `units[]` row is also declared under, so a reader that only
#: understands the unit contract still sees the whole corpus.
DEPENDENCY_ROLE = "corpus-unit"


class CorpusIndexModelError(ValueError):
    """A record does not fit the corpus index's vocabulary."""


def output_relative_path() -> PurePosixPath:
    """`index.glb`, directly below the export root: one corpus index per export root."""

    return PurePosixPath(OUTPUT_NAME)


def kind_of(asset: str) -> str:
    """The unit kind of one stable identity: `vtmb:<kind>:<key>` -> `<kind>`."""

    text = str(asset)
    if not text.startswith("vtmb:"):
        raise CorpusIndexModelError(f"{asset!r} is not a stable identity")
    rest = text[len("vtmb:"):]
    kind, _, _ = rest.partition(":")
    if not kind:
        raise CorpusIndexModelError(f"{asset!r} names no unit kind")
    return kind


@dataclass(frozen=True, slots=True)
class MemberSource:
    """One source for one install-relative key: the winner, or one of its shadowed losers."""

    origin: dict[str, Any]
    byte_length: int
    sha256: str

    def to_json(self) -> dict[str, Any]:
        return {
            "origin": self.origin,
            "byteLength": int(self.byte_length),
            "sha256": self.sha256,
        }


@dataclass(frozen=True, slots=True)
class Member:
    """One `members[]` row: the winning member, what owns it, and what it lost to."""

    path: str
    source: MemberSource
    #: Every losing source for the same path, highest precedence first.
    shadowed: tuple[MemberSource, ...] = ()
    disposition: str = "unclaimed"
    #: The owning unit for `unit` and `companion`; the first when the member selects several.
    asset: str | None = None
    #: Every owning unit, published only where one member selects more than one (a cut table).
    assets: tuple[str, ...] = ()
    #: For a BSP: the PAKFILE members it carries, each with the unit it became.
    embedded: tuple[dict[str, Any], ...] = ()
    #: For `residue`: the category and the fact that proves the engine never reads the member.
    evidence: dict[str, Any] | None = None

    def __post_init__(self) -> None:
        if self.disposition not in DISPOSITIONS:
            raise CorpusIndexModelError(f"{self.path}: unknown disposition {self.disposition!r}")
        if self.disposition in ("unit", "companion") and not self.asset:
            raise CorpusIndexModelError(f"{self.path}: a {self.disposition} member names its unit")
        if self.disposition == "residue" and not self.evidence:
            raise CorpusIndexModelError(f"{self.path}: residue carries its evidence")

    @property
    def extension(self) -> str:
        name = self.path.rsplit("/", 1)[-1]
        stem, dot, suffix = name.rpartition(".")
        return ("." + suffix.lower()) if (dot and stem) else ""

    def to_json(self) -> dict[str, Any]:
        row: dict[str, Any] = {
            "path": self.path,
            "origin": self.source.origin,
            "byteLength": int(self.source.byte_length),
            "sha256": self.source.sha256,
            "shadowed": [loser.to_json() for loser in self.shadowed],
            "disposition": self.disposition,
        }
        if self.asset is not None:
            row["asset"] = self.asset
        if self.assets:
            row["assets"] = list(self.assets)
        if self.embedded:
            row["embedded"] = [dict(entry) for entry in self.embedded]
        if self.evidence is not None:
            row["evidence"] = dict(self.evidence)
        return row


@dataclass(frozen=True, slots=True)
class Unit:
    """One `units[]` row: a published unit as the index found it under the export root."""

    asset: str
    kind: str
    path: str
    schema_version: str
    byte_length: int
    sha256: str
    warnings: dict[str, Any] = field(default_factory=lambda: {"count": 0, "reasons": []})
    dependency_count: int = 0
    unresolved_count: int = 0
    unsupported_count: int = 0

    def to_json(self) -> dict[str, Any]:
        return {
            "asset": self.asset,
            "kind": self.kind,
            "path": self.path,
            "schemaVersion": self.schema_version,
            "byteLength": int(self.byte_length),
            "sha256": self.sha256,
            "warnings": {
                "count": int(self.warnings.get("count", 0)),
                "reasons": list(self.warnings.get("reasons", ())),
            },
            "dependencyCount": int(self.dependency_count),
            "unresolvedCount": int(self.unresolved_count),
            "unsupportedCount": int(self.unsupported_count),
        }


@dataclass(frozen=True, slots=True)
class Reference:
    """One `references[]` row: one `dependencies` row of one unit, as a graph edge."""

    source: str
    role: str
    target: str
    source_path: str
    resolved: bool
    #: The VMT key this edge was read from, where the row carries one (a material's texture
    #: bindings do); `None` for a row whose dependency kind has no such key.
    parameter: str | None = None

    def to_json(self) -> dict[str, Any]:
        row: dict[str, Any] = {
            "from": self.source,
            "role": self.role,
            "to": self.target,
            "sourcePath": self.source_path,
            "resolved": bool(self.resolved),
        }
        if self.parameter is not None:
            row["parameter"] = self.parameter
        return row


#: The three install roots the walk covers, as the seam map's own identity block spells them.
SOURCE_ROOTS = (
    "<VTMB>/Vampire/",
    "<VTMB>/Vampire/pack*.vpk",
    "<VTMB>/Unofficial_Patch/",
)


def identity(export_root: str) -> dict[str, Any]:
    """`identity`: the asset, the install roots walked, the policy, and the export root indexed.

    The corpus index is cut from no one member, so its `sourcePaths` are the three install roots
    the walk covers; `exportRoot` names the corpus of published units the index is an index of.
    """

    return identity_block(ASSET_ID, SOURCE_ROOTS, exportRoot=str(export_root))
