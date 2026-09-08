"""Assemble one corpus index from a walk of the install and a walk of the export root.

This is the seam's decode: it reads no install member itself beyond what `walk` already hashed,
and it reads every published unit through the unit contract's own container and extension-root
rules. The result is the extension root the exporter writes.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Sequence

from elysium_pipeline.formats.corpus_index_glb import backfill, checks, graph, reconcile
from elysium_pipeline.formats.corpus_index_glb.coverage import coverage
from elysium_pipeline.formats.corpus_index_glb.model import (
    DEPENDENCY_ROLE,
    SCHEMA_VERSION,
    Member,
    Reference,
    Unit,
    identity,
)
from elysium_pipeline.formats.corpus_index_glb.walk import InstallWalk
from elysium_pipeline.formats.unit_contract import extension_root


class CorpusIndexDecodeError(RuntimeError):
    """The corpus cannot be indexed."""


@dataclass(frozen=True, slots=True)
class CorpusIndexModel:
    """The complete decode of one corpus index, ready for `build_document`."""

    export_root: str
    walk: InstallWalk
    units: tuple[Unit, ...]
    references: tuple[Reference, ...]
    inverse: dict[str, list[dict[str, str]]]
    dangling: list[dict[str, Any]]
    orphans: list[dict[str, Any]]
    cross_unit_checks: list[dict[str, Any]]
    #: Every member naming a unit the corpus does not publish, and every `unit` member whose
    #: bytes its own unit contradicts. Both fail the export; neither is published, because a
    #: published index has none of either.
    unpublished_claims: list[dict[str, Any]]
    source_disagreements: list[dict[str, Any]]
    census: dict[str, Any]
    summary: dict[str, Any] = field(default_factory=dict)
    #: Every indexed unit's extension root, keyed by identity. Not published: it is the corpus
    #: the checks and the back-fill read, and every fact worth keeping is already in the tables.
    roots: dict[str, dict[str, Any]] = field(default_factory=dict)

    @property
    def members(self) -> tuple[Member, ...]:
        return self.walk.members

    @property
    def unclaimed(self) -> list[str]:
        return self.walk.unclaimed()

    @property
    def failed_checks(self) -> list[str]:
        return checks.failed(self.cross_unit_checks)


def summary(
    members: Sequence[Member],
    units: Sequence[Unit],
    edges: Sequence[Reference],
    orphan_rows: Sequence[dict[str, Any]],
    dangling_rows: Sequence[dict[str, Any]],
    check_rows: Sequence[dict[str, Any]],
) -> dict[str, Any]:
    """What one line of a report needs, and the number the export's guarantee is written on."""

    counts = {"unit": 0, "companion": 0, "residue": 0, "unclaimed": 0}
    for member in members:
        counts[member.disposition] += 1
    embedded_total = 0
    embedded_unclaimed = 0
    unclaimed_by_extension: dict[str, int] = {}
    for member in members:
        for entry in member.embedded:
            embedded_total += 1
            if entry.get("asset") is None:
                embedded_unclaimed += 1
                extension = _embedded_extension(str(entry.get("member", "")))
                unclaimed_by_extension[extension] = unclaimed_by_extension.get(extension, 0) + 1
    return {
        "members": len(members),
        "units": len(units),
        "references": len(edges),
        "unit_members": counts["unit"],
        "companions": counts["companion"],
        "residue": counts["residue"],
        "unclaimed": counts["unclaimed"],
        "orphans": sum(int(row["count"]) for row in orphan_rows),
        "danglingReferences": sum(int(row["count"]) for row in dangling_rows),
        "warnings": sum(int(unit.warnings.get("count", 0)) for unit in units),
        "checksPassed": sum(1 for row in check_rows if row["passed"]),
        "checksFailed": sum(1 for row in check_rows if not row["passed"]),
        "embeddedMembers": embedded_total,
        "embeddedUnclaimed": embedded_unclaimed,
        "embeddedUnclaimedByExtension": unclaimed_by_extension,
        # A `vtmb:missing-<kind>:` sentinel is `resolved: false` and carries no dependency row by
        # contract (`references.missing_sentinel`), so it is invisible to `references[]`,
        # `danglingReferences[]` and every check built over the graph; this is the corpus-wide
        # count of the retail gaps it stands in for -- a fact, not a defect, so it lands in the
        # summary rather than a `crossUnitChecks[]` failure.
        "sentinelReferences": sum(int(unit.sentinel_count) for unit in units),
        "sentinelReferenceUnits": sum(1 for unit in units if unit.sentinel_count),
    }


def _embedded_extension(member_path: str) -> str:
    """The lower-case extension of one PAKFILE member's path, `""` if it has none."""

    name = member_path.replace("\\", "/").rsplit("/", 1)[-1]
    stem, dot, suffix = name.rpartition(".")
    return ("." + suffix.lower()) if (dot and stem) else ""


def decode_corpus_index(walk: InstallWalk, export_root: Path) -> CorpusIndexModel:
    """Index one export root against one install walk, touching no published file.

    The back-fill is deliberately not part of this: the index refuses to publish an incomplete
    corpus, and refusing after having rewritten a dozen thousand units would be a side effect of
    a failed command. `write_inverse` is the separate last step.
    """

    root = Path(export_root)
    units, roots = graph.read_corpus(root)
    edges = graph.references(roots)
    # One residue rule's evidence is the reference graph -- a `sound/` member with no extension
    # is unreachable because nothing names it -- so the walk's unclaimed members are re-asked
    # once the graph exists. Nothing a seam claimed is touched.
    walk = walk.with_reference_graph(edge.target for edge in edges)
    return _assemble(walk, root, units, edges, roots)


def _assemble(walk, root, units, edges, roots) -> CorpusIndexModel:
    check_rows = checks.run(units, edges, roots)
    # A check may find a reference the corpus cannot answer that no unit declares as a dependency
    # row; `danglingReferences[]` is where the index reports one, so the checks run first.
    dangling_rows = graph.dangling(edges, checks.dangling_rows(check_rows))
    orphan_rows = graph.orphans(units, edges)
    unpublished, disagreements = reconcile.failures(walk.members, units, roots)
    return CorpusIndexModel(
        export_root=str(root),
        walk=walk,
        units=tuple(units),
        references=tuple(edges),
        inverse=graph.inverse(edges),
        dangling=dangling_rows,
        orphans=orphan_rows,
        cross_unit_checks=check_rows,
        unpublished_claims=unpublished,
        source_disagreements=disagreements,
        census=graph.census(walk.members, units),
        summary=summary(
            walk.members, units, edges, orphan_rows, dangling_rows, check_rows
        ),
        roots=dict(roots),
    )


def write_inverse(model: CorpusIndexModel) -> CorpusIndexModel:
    """Write the fields no unit can write about itself, and re-hash the units that changed.

    "The index writes those fields into the target units' JSON chunks as its last step and
    re-hashes them". The reference graph is unchanged
    by it -- the fields are identity, not dependencies -- so only the rows that quote a unit's
    bytes are rebuilt.
    """

    root = Path(model.export_root)
    rows = backfill.rows_for(model.units, model.references, model.roots)
    units = backfill.apply(root, model.units, rows, model.roots)
    if list(units) == list(model.units):
        return model
    return _assemble(model.walk, root, units, list(model.references), model.roots)


def build_root(model: CorpusIndexModel) -> dict[str, Any]:
    """The `ELYSIUM_vtmb_corpus_index` extension root, in the seam map's key order."""

    return extension_root(
        schema_version=SCHEMA_VERSION,
        identity=identity(model.export_root),
        source_resolution=model.walk.source_resolution,
        dependencies=graph.dependencies(model.units, DEPENDENCY_ROLE),
        coverage=coverage(model.members),
        excludedTrees=[dict(row) for row in model.walk.excluded_trees],
        members=[member.to_json() for member in model.members],
        units=[unit.to_json() for unit in model.units],
        references=[edge.to_json() for edge in model.references],
        inverse=model.inverse,
        danglingReferences=model.dangling,
        orphans=model.orphans,
        crossUnitChecks=model.cross_unit_checks,
        census=model.census,
        summary=model.summary,
    )
