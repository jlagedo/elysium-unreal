"""The byte ledger and coverage block for one vdata unit, built on the shared package.

A vdata unit carries no span and no sibling members (`source.py`), so the ledger is always exactly
one row: gapless over the whole file the closure was decoded from. Building it is `decode.py`'s
job -- it is the one place that has the tokens and the tree -- so this module only wraps the
shared `ByteLedger` constructor and the fixed six-key coverage shape.
"""

from __future__ import annotations

from typing import Any, Iterable

from elysium_pipeline.formats.unit_contract import ByteLedger, coverage_block as _coverage_block
from elysium_pipeline.formats.unit_contract.origin import SourceMember

#: The field names every unit represents directly, whatever its grammar, published in
#: `coverage.mapped`.
MAPPED_FIELDS = ("identity", "sourceResolution", "grammar", "projection")

#: The keys a unit names in `coverage.mapped` only where its own grammar and content gave it
#: something to put there, so a `delimited`/`freeform` unit never claims a `tree` it never
#: publishes, and a `keyvalues` unit never claims the `rows` array it always leaves empty
#: (`formats/sound_glb/coverage.py`'s `mapped_keys(model)` states the same rule).
CONDITIONAL_FIELDS = ("rootKey", "tree", "rows", "comments", "dependencies", "anomalies", "omissions")


def mapped_keys(model) -> list[str]:
    """The extension keys one vdata unit actually represents, named once."""

    present = {
        "rootKey": model.root_key is not None,
        "tree": model.grammar == "keyvalues",
        "rows": model.grammar in ("delimited", "freeform"),
        "comments": bool(model.comments),
        "dependencies": bool(model.dependencies),
        "anomalies": bool(model.anomalies),
        "omissions": bool(model.omissions),
    }
    return list(MAPPED_FIELDS) + [key for key in CONDITIONAL_FIELDS if present[key]]


def new_ledger(member: SourceMember) -> ByteLedger:
    """A gapless ledger over the whole member -- a vdata unit is never cut from a span."""

    return ByteLedger(member.path, member.data)


def coverage_block(
    *,
    model,
    ledger_row: dict[str, Any],
    typed_unidentified: Iterable[Any] = (),
    omitted_proven: Iterable[Any] = (),
    unresolved: Iterable[Any] = (),
    unsupported: Iterable[Any] = (),
) -> dict[str, Any]:
    """The six-key coverage object: what the seam maps, plus the one byte-ledger row.

    `omitted_proven` carries the seam's own `omissions[]` rows: the evidence for every
    `omitted-proven` byte the ledger claims (`whitespace`, `bom`, `empty-member`), per the unit
    contract's "an evidence-backed omission carrying its reason in `omissions`
    or `coverage.omittedProven`".
    """

    return _coverage_block(
        mapped=mapped_keys(model),
        typed_unidentified=typed_unidentified,
        omitted_proven=omitted_proven,
        byte_ledger=[ledger_row],
        unresolved=unresolved,
        unsupported=unsupported,
    )
