# Step 4, packet 4a — preflight, move and binding manifests, checker

Start: `0019-5-class-tree`, `21bb56cf` (step 3 committed and accepted), clean tree, manifest
`step-3-species-dispatch` (phase 3).
Executor: Claude Code, Opus 5.5.
Outcome: step 4's scope is a re-derivable record, and the checker that will accept the step exists.
No C++ change. Lands as its own commit (owner decision, 2026-09-26).

## Records

- `manifest.json`: `history.step3.commit = 21bb56cf…`, `step4_packets`; `ACCEPTED_PHASES` gains 4
  (the phase stays 3 until the step is accepted).
- [moves-step4.tsv](../moves-step4.tsv), the **move manifest**: one row per `FElysiumNpc` member the
  species touch, 1,504 rows. Identity columns (member, kind, declaring family, defining files, the
  owner its port users imply, the retail owner the ledger proposes) are re-derived from the accepted
  step-3 tree by `--check step4`. Draft dispositions:
  - 688 `move` onto a species class, 252 `collapse` (224 forwarding overrides, 21 class-table
    members, 1 split field — plus 6 already dissolved with them);
  - 526 `stay` on `FElysiumNpc` (233 Troika API the moved bodies call, 152 virtual surface, 109
    Troika/base bodies, 15 data-query tables, 11 cross-branch helpers, 6 words an activity predicate
    reads);
  - 21 `deferred` (17 controller line, step 7; 4 maker, step 8);
  - 17 `investigate`: the `F*Species` row types, which follow their accessors.
- [fields-step4.tsv](../fields-step4.tsv), the **binding manifest**: every record of an introduced
  species' own datamap in the pinned replay, 248 rows over 36 tables — 222 fields, 15 inputs, 11
  outputs. Draft: 106 `bind` to an existing port member, 79 `declare` a new member, 15 `input-seam`,
  11 `output`, 37 `investigate` (wrong or ambiguous joins across sibling offsets, and the shadow rows
  that re-declare a Troika word).
- [decisions-step4.json](../decisions-step4.json): the owner decisions, the rules above, the packet
  split (4b–4i), the test inventory (35 files, 2,134 references to members that leave
  `FElysiumNpc`) and four findings.

## Findings that change the plan

- **The rats' detection is not reachable yet.** Binding the eight keys is step 4's, but
  `ScurryingShouldDetect` `0x103acac0` and `ScurryingFindFleeDestination` `0x103acba0` have no port
  caller: their retail callers, Scurrying `GatherConditions` `0x103ac500` and `StartTask`
  `0x103ac740`, are story-8 residue. Step 4 asserts the bodies on the authored values; played
  detection needs the residue ports unless the owner widens the step.
- **Makers read species words.** The zombie maker reads `m_iZombieAIType`; the fleshpile maker
  reads and writes the Andrei's runner and kill counters. Those words stay on `FElysiumNpc` until
  step 8 and the species descriptors bind to that storage.
- **One port field stands for three retail words.** `SpeciesShunnedFindCount` is Gargoyle
  `+0x6680`, Hengeyokai `+0x6678` and Tzimisce `+0x66b8`; step 4 splits it.
- **Shadow rows exist.** Hengeyokai, Tzimisce, SabbatLeader and VampireBoss datamaps re-declare
  Troika words (`+0x6458`, `+0x6498`…); each binds explicitly on its class.

## Checker

`kernel_migration_step4.py` (`--check step4`):
- **Always:** binding-manifest identity against the pinned replay; move-manifest identity against
  the accepted step-3 tree.
- **At phase 4:** no `investigate` rows; a `move` member is declared on its owner's header and
  defined in its cpp and nowhere on `FElysiumNpc`; a `collapse` member is gone; `stay`/`deferred`
  members remain; every bound field has storage on its owner and every keyed/output row is
  generated; overlay targets name no removed symbol; `--check step3` (historical); pinned
  acceptance artifacts, every expectation consumed.

## Checks

- `pytest pipeline/tests/test_kernel_migration_step4.py`: 10 passed.
- `kernel_migration --check step4`: PENDING, both manifests match.

Disposition: complete; ready to commit as the step-4 prerequisite.
