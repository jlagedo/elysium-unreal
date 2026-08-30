"""The byte ledger and coverage block for one ui-resource unit, built on the shared package.

A ui-resource unit carries no span and no sibling members (`source.py`), so the ledger is always
exactly one row: gapless over the whole file the closure was decoded from.
"""

from __future__ import annotations

from typing import Any, Iterable

from elysium_pipeline.formats.unit_contract import ByteLedger, coverage_block as _coverage_block
from elysium_pipeline.formats.unit_contract.origin import SourceMember

#: The field names every unit represents directly whatever its category is.
BASE_MAPPED_FIELDS = (
    "identity",
    "sourceResolution",
    "encoding",
    "grammar",
    "dependencies",
    "comments",
    "anomalies",
    "omissions",
)

#: The typed-projection fields a unit may populate, `(json name, model attribute)`. Exactly one
#: (or, for a `keyvalues`-grammar unit, `tree` plus at most one) is not `None` on a given unit; a
#: unit is graded `mapped` only for the field(s) it actually carries
#: (`seam_map_unit_contract.md`'s semantic states grade "a source field or record", not a field
#: name the unit never populated).
_OPTIONAL_FIELDS = (
    ("tree", "tree"),
    ("scheme", "scheme"),
    ("layout", "layout"),
    ("menu", "menu"),
    ("hud", "hud"),
    ("titles", "titles"),
    ("rows", "rows"),
    ("substitutions", "substitutions"),
    ("strings", "strings"),
    ("options", "options"),
    ("menuScene", "menu_scene"),
)


def mapped_fields(model: Any) -> list[str]:
    """The `coverage.mapped` field names this particular unit populates."""

    fields = list(BASE_MAPPED_FIELDS)
    fields.extend(name for name, attr in _OPTIONAL_FIELDS if getattr(model, attr) is not None)
    return fields


def omitted_proven_rows(whitespace_bytes: int) -> list[dict[str, Any]]:
    """The single `coverage.omittedProven` row a grammar's insignificant whitespace earns once it
    has claimed at least one byte of it, evidencing the ledger's `omitted-proven` whitespace
    claims per `seam_map_unit_contract.md`'s "an evidence-backed omission carrying its reason in
    `omissions` or `coverage.omittedProven`"."""

    if not whitespace_bytes:
        return []
    return [
        {
            "role": "whitespace",
            "reason": "insignificant-separator-bytes",
            "byteLength": whitespace_bytes,
        }
    ]


def new_ledger(member: SourceMember) -> ByteLedger:
    """A gapless ledger over the whole member -- a ui-resource unit is never cut from a span."""

    return ByteLedger(member.path, member.data)


def coverage_block(
    *,
    mapped: Iterable[str],
    ledger_row: dict[str, Any],
    typed_unidentified: Iterable[Any] = (),
    omitted_proven: Iterable[Any] = (),
    unresolved: Iterable[Any] = (),
    unsupported: Iterable[Any] = (),
) -> dict[str, Any]:
    return _coverage_block(
        mapped=mapped,
        typed_unidentified=typed_unidentified,
        omitted_proven=omitted_proven,
        byte_ledger=[ledger_row],
        unresolved=unresolved,
        unsupported=unsupported,
    )
