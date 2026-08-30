"""The coverage object and the extension root that carries it.

Coverage is stated in two vocabularies: the semantic states grade a source field or record, the
ledger states grade a byte range. A field graded `equivalent` and the bytes it was read from
graded `mapped` describe one decode from two directions.
"""

from __future__ import annotations

from typing import Any, Iterable, Sequence

from elysium_pipeline.formats.unit_contract.origin import SOURCE_POLICY

#: How a source field or record is graded. A complete unit has no `unresolved` and no
#: `unsupported` row; `typedUnidentified` carries a typed value whose meaning is unknown, so the
#: value survives the export and still counts against completeness.
SEMANTIC_STATES = frozenset(
    {"mapped", "equivalent", "derived", "omitted-proven", "unresolved", "unsupported"}
)

#: `coverage`'s keys, in publication order.
COVERAGE_KEYS = (
    "mapped",
    "typedUnidentified",
    "omittedProven",
    "byteLedger",
    "unresolved",
    "unsupported",
)

#: The keys every extension root opens with, in publication order, before its kind-specific ones.
ROOT_KEYS = ("schemaVersion", "identity", "sourceResolution", "dependencies", "coverage")


class CoverageError(ValueError):
    """A coverage or extension-root block does not have the shape the contract publishes."""


def extension_name(kind: str) -> str:
    """The extension one unit kind declares: `ELYSIUM_vtmb_<kind>`, underscored."""

    return "ELYSIUM_vtmb_" + str(kind).replace("-", "_")


def coverage_block(
    *,
    mapped: Iterable[Any] = (),
    typed_unidentified: Iterable[Any] = (),
    omitted_proven: Iterable[Any] = (),
    byte_ledger: Iterable[Any] = (),
    unresolved: Iterable[Any] = (),
    unsupported: Iterable[Any] = (),
) -> dict[str, Any]:
    """The six coverage lists, always all six, so a reader never has to tell absent from empty."""

    return {
        "mapped": list(mapped),
        "typedUnidentified": list(typed_unidentified),
        "omittedProven": list(omitted_proven),
        "byteLedger": list(byte_ledger),
        "unresolved": list(unresolved),
        "unsupported": list(unsupported),
    }


def identity_block(
    asset: str,
    source_paths: str | Sequence[str],
    **extra: Any,
) -> dict[str, Any]:
    """The unit's stable ID, the source path(s) it was decoded from, and the policy that found
    them. One path publishes `sourcePath`; several publish `sourcePaths`."""

    if not asset:
        raise CoverageError("a unit identity names its stable asset ID")
    block: dict[str, Any] = {"asset": str(asset)}
    if isinstance(source_paths, str):
        block["sourcePath"] = source_paths
    else:
        paths = [str(path) for path in source_paths]
        if not paths:
            raise CoverageError(f"{asset}: a unit identity names the source it was decoded from")
        if len(paths) == 1:
            block["sourcePath"] = paths[0]
        else:
            block["sourcePaths"] = paths
    block["sourcePolicy"] = SOURCE_POLICY
    for key, value in extra.items():
        if key in block:
            raise CoverageError(f"{asset}: identity field {key!r} is already the contract's")
        block[key] = value
    return block


def extension_root(
    *,
    schema_version: str,
    identity: dict[str, Any],
    source_resolution: dict[str, Any],
    dependencies: Iterable[Any],
    coverage: dict[str, Any],
    **kind_specific: Any,
) -> dict[str, Any]:
    """The extension root: the five contract keys in order, then the seam's own keys in the
    order it passed them."""

    collision = [key for key in kind_specific if key in ROOT_KEYS]
    if collision:
        raise CoverageError(f"kind-specific key(s) {', '.join(collision)} are the contract's")
    root: dict[str, Any] = {
        "schemaVersion": str(schema_version),
        "identity": identity,
        "sourceResolution": source_resolution,
        "dependencies": list(dependencies),
        "coverage": coverage,
    }
    root.update(kind_specific)
    return root
