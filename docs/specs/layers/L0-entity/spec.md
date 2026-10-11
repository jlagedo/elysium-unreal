# L0 — entity

**Standing rule for every story:** follow retail behaviour. This spec owns its layer's rows in
`../audit.tsv`; a story names the rows it closes. What it needs from a lower layer is built there
first (`AGENTS.md`: missing inputs); what only a higher layer runs is left as a listed upward hook.
A story closes on its required records green on the arena and its rows re-read against the port.

**Active execution plan (2026-10-11):** [delivery-plan.md](delivery-plan.md), with
[brief ownership](delivery-map.tsv), [milestone dependencies](milestones.tsv) and the compact
[checkpoint](checkpoint.md). Original briefs/runs are evidence, not direct dispatch instructions.

## Scope

- **Retail:** `CBaseEntity` and physics (movetypes, VPhysics objects, touch, push), entity I/O and the event queue, triggers, movers and doors, the save framework, the sound list, sound emission, effects, ConVars and game rules.
- **Historical inventory:** 165 stories / 264 briefs / 88 original bundles. These identifiers
  remain traceable; the old 391-function count and byte estimates are not a current work estimate.
- **Delivery:** D01–D11 subsystem contracts plus D00 harness/tooling, with explicit scope and
  higher-layer dispositions in the delivery map. No subsystem closure is asserted by this replan.
- **Ready for dispatch:** a bounded packet has real producers, a named observer and complete
  acceptance criteria. The old “nothing open below L0” flag did not establish same-layer readiness.
- **Hooks:** preserve and verify `../hooks.tsv`; additional handoffs are tracked explicitly.
- **Witnesses:** the arena, `sp_tutorial_1` and `sm_hub_1` (map records).

## The gate — the layer is done when

1. every active L0 story's required records, including subsystem integration records, are green
   on one `uv run elysium arena` run, and a record that was
   intermittent is green in three boot orders;
2. every row owned by the L0 closing scope in `../audit.tsv` is done, or a named divergence the owner accepted
   (`../decisions.md`);
3. the default and arm tiers have 0 failures, and every record that passed in `../baseline.md`
   still passes;
4. its upward hooks are listed, each with the layer that completes it;
5. its state-bearing engine-replaced rows ([`contract-checks.md`](contract-checks.md)) keep retail's
   inputs, outputs and event order, shown by a record, or are named divergences the owner accepted;
6. every mapped requirement is accounted for: active L0 work has its closing proof, excluded
   import work stays excluded, higher-layer handoffs are recorded, and retained scope-review
   items have an explicit disposition. Every subsystem includes its own native saved-state
   and lifecycle contract; no new count or dashboard substitutes for the behavioral gate.

## Order

The owner has authorized main to resume under [goal.md](goal.md). Reconcile the sound packet
at `8f8aea1b` and review/commit the plan before dispatch. Follow the milestone graph and close
D02 bodies/queries/transforms next,
reusing the accepted harness, entity/data, sound/effects and save-framework work. Then close the
consumer services and entity chains in dependency order, followed by native restore/travel and
the full L0 gate. Small runs remain the execution unit; subsystem contracts are the delivery unit.

## Boundaries and decisions

The [delivery plan](delivery-plan.md) owns active execution boundaries. Unreal/Chaos supplies
geometry, broadphase, contacts, solving and rigid-body mechanisms; the substrate preserves
observable filtering, movement/shadow rules, lifecycle and timing. Audio follows the same rule:
authored selection/emission behavior is ours; loading, decoding, streaming and mixing are Unreal's.
No engine-internal layout or byte output is required without a named gameplay/content observer.

Routine implementation choices are resolved using the vision: preserve strict radius comparisons,
retail slot/expiry behavior, restore order and native state meaning; register known higher-layer
hooks and use Unreal resource/renderer services. These are not new owner-approval questions.
The existing single-player scope remains. Do not add multiplayer restoration or save compatibility.

Only a proposed observable gameplay divergence or a real scope change goes to the owner, with
retail evidence and its consequence. Existing contact/all-solid/timing/material mismatches and
law-event differences remain unaccepted until corrected or explicitly approved. The replan
accepts no approximation merely because Chaos or another Unreal service implements it.

## Scope reconciliation

- **Excluded by owner:** retail-save import, external-save dry-run decoder and retail roster
  conversion. The two import-only stories and the appended dry-run brief remain as marked
  historical records. Their gates are not L0 completion requirements.
- **Native save work retained:** block ordering, state/identity/time restoration, slot storage,
  safe current-build validation, diagnostics, save/load and travel. Each layer owns its fields;
  no whole L1–L5 gameplay restore is required to finish the L0 service.
- **Higher-layer remainders:** the delivery map assigns the twelve misplaced whole briefs and
  the higher-layer portions of mixed briefs to explicit handoffs. Register/verify each real
  upward hook and recipient requirement before closure; these assignments do not declare the
  higher implementation or hook already complete. See [handoffs.md](handoffs.md).
- **Retained additional scope:** the non-core constraint/animation-link family and generic
  physics-asset bake are visible under D09. Establish whether each is a required core producer
  or obtain an explicit scope disposition before the final gate. They are neither silently
  dropped nor prerequisites for every foundation packet.
- **Historical checkpoints:** body/reference/recipe, static/shadow, material and native-contact
  acceptance remains in `runs.md` and `progress.md`. No source behavior or audit verdict was
  changed by this documentation replan. Reuse main's sound checkpoint at `8f8aea1b`.
