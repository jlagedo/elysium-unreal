# Manifest v1 — accepted step-0 scope and extension rules

`manifest.json` owns schema/phase, input pins, packet state and the explicit deferrals.
`classes.tsv` and `factories.tsv` contain authored execution mappings joined to the existing
class census and pinned corpus. They are not replacement retail ledgers. Factory observations
and full listings are generated only into the work root. No runtime survey consumes this map yet.

`kernel_migration.py` is the shared reader. Its factory check refuses missing pins, duplicate
case-folded aliases, unresolved/multiple factory answers, absent owners, vtable disagreement,
changed listing hashes, invalid class chains and changed deferrals. It verifies all 74 factories
without activating the production proximity-survey replacement before step 2.

Identity decisions already expressed by the reader:

- Field: module + declaring retail class + offset + member. A sibling offset is not a global key.
- Body/override: module + address + introducing class + slot/signature + receiver class. A shared
  address never collapses different overrides; a helper can use a null slot but still needs an
  explicit signature/owner decision. Existing overlay target text alone is not a definition.
- Class: retail class, final port owner/base and introduction/fold step. Dead classes retain
  census identities and no port type. Factories refer to class records; aliases remain separate.

`decisions.json` and `field-decisions.tsv` record reviewed execution decisions. The generated
inventory joins bodies, fields, fixture classifications, exact compatibility consumers, source
dependencies and preserved rule identities to the existing oracle. Source hashes detect new or
changed compatibility sites. Every ambiguous/unimplemented future target is retained; candidate
comments and overlay surfaces never authorize movement. Thirteen exceptional source identities
are explicitly resolved, including seven relevant to the next step's deletion review.

`kernel_migration_replay` uses exact counted replacements and signature-selected definition moves,
and fails on ambiguous overloads or unrecognized source. Recipes and rehearsal implementations
remain outside the repository. `kernel_migration_audit` validates actual predecessor composition,
repeat-application refusal, field identity/flags and the maker duplicate-state boundary.

`--check factories` is a narrow check. `--check step0` verifies the hashed artifacts in
`acceptance.json`, the current source inventory, authored mappings, class-qualified datamap flags,
compiled rehearsal source, representative probes, focused tests and the integration gate. Editing
status strings alone cannot unlock it. No phase after 0 is accepted by this version; later steps
must extend their ownership/generation checks with their consuming operations.

`expectations/step-0.json` has no regression expectations. `test_delta` reads exactly three suite
reports, ignores durations/timestamps for equality, compares warning/error and stub counts per
test, and compares order for explicitly listed deterministic tests. Expectations name an exact
test, evidence and owning packet; removals also need a coverage disposition. Every expectation
must be consumed. Report failures/incompleteness cannot be made green by expectations.
