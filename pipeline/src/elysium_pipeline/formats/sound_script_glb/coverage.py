"""The coverage block every sound-script unit publishes, built from its decode's byte ledger.

`decode.py` writes every claim through `elysium_pipeline.formats.unit_contract.ByteLedger`
directly, so the byte-level half of coverage is already the contract's row shape by the time it
reaches here; this module only adds the semantic half -- which top-level fields are `mapped` for
each kind -- and assembles the six-key `coverage` object the contract's `coverage_block` builds.
"""

from __future__ import annotations

from elysium_pipeline.formats.unit_contract import coverage_block

#: The extension-root fields each kind's `record` makes true statements about, once its
#: `unresolved`/`unsupported` rows are empty. `identity`, `sourceResolution`, `dependencies` and
#: `coverage` are the contract's own keys and are not restated here.
MAPPED_FIELDS = {
    "game-sound": ["kind", "dormant", "parameters", "record", "comments"],
    "manifest": ["kind", "parameters", "record", "comments"],
    "soundscape": ["kind", "dormant", "parameters", "record", "comments"],
    "sentence": ["kind", "parameters", "record"],
    "dsp-preset": ["kind", "parameters", "record", "comments"],
}


def build_coverage(model) -> dict:
    return coverage_block(
        mapped=MAPPED_FIELDS[model.kind],
        typed_unidentified=model.typed_unidentified,
        #: `model.omissions` is the same evidence-backed list the extension root publishes as its
        #: own `omissions` key; routed here too, so a byte ledger that claims `omitted-proven`
        #: ranges is never paired with an empty `coverage.omittedProven`.
        omitted_proven=model.omissions,
        byte_ledger=model.byte_coverage,
        unresolved=model.unresolved,
        unsupported=model.unsupported,
    )
