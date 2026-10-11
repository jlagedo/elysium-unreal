# L0 restart checkpoint

Updated 2026-10-11 by main. Full L0 closing gate remains open; the supported Goal is active.
Read [goal.md](goal.md) at every continuation. Main directly coordinates and accepts each run;
one fresh Sol High worker at a time, no nested coordinators, no worker commits, never push.

## Accepted state

- Accepted base-velocity packet: the local commit containing this checkpoint, parent
  `5b48bb61fc19cadcc599d0107a12ddfc10777bfc` (34-path plan acceptance).
  Its exact accepted HEAD/tree/blob receipt is
  `E:/elysium-work/codex/l0-p1-velocity-20261011/acceptance-checkpoint.json`.
  Plan parent `8f8aea1b0619b5b07711b2174218cba40a1847f8` is the sound prerequisite.
- Plan receipt: `E:/elysium-work/codex/l0-delivery-plan-20261011/acceptance-checkpoint.json`.
  Main independently verified structural coverage, committed blobs and the clean tree.
- Sound receipt:
  `E:/elysium-work/codex/l0-sound-producer-implementation-20261011/final-review-r1/checkpoint.json`.
  Its build, pipeline 191, native 2, affected 21, default 194 and six sound/KV arena records passed.
  Reuse the accepted evidence; generic sound emission and impact consumers remain open.
- Supported Goal recovery: `get_goal` returned null; owner-authorized `create_goal` then
  confirmed active. No internal app storage or prior completion state was edited.

## Contract closed — D02/P1 base velocity

Fresh worker `/root/l0_p1_velocity` implemented the getter portion of
`L0.physics.physics-object-contract-2`, retail base slot199 `0x100ad1a0`.
Packet and oracle: `E:/elysium-work/codex/l0-p1-velocity-20261011/packet.md`.
Main approved the precode contract after independent oracle and installed public API review.

The initial production branch passed 1/1:
`E:/elysium-work/codex/l0-p1-velocity-20261011/initial-branch-report/index.json`.
A spawned .phy prop moved 2.38273955 cm; actual native/query world velocity was (101,-53,17)
cm/s and body-local QAngle rate was (28.648,-42.972,-14.324) degrees/s. This proves the selected
native getter branch, not P1 or cross-engine trajectory equivalence. Expanded native tests prove
nullable outputs, angular-last alias ordering, literal rotated QAngle axes independent of inertia
axes, parented fallback without dirty writes, replacement/clear, recipe independence and alias
retirement before native release. The original arena failure/source was retained: runtime fixture
spawn omitted required model annotation/preparation. A bounded initial-row harness repair mirrors
the baked static annotation and uses the existing production model preparation barrier.

Main independently reviewed the oracle, installed APIs and all changed paths. Build passed
(`20261011T201857.936745Z-build.json`); default 195/195 with zero failures/not-run/abstentions
(`reports/tests/20261011T201903.093487Z-elysium-session-elysium-substrate/index.json`).
Affected arena 5/5 in one boot (`reports/arena/20261011T201945.195623Z/index.json`): velocity,
solid mask, damage type, baseline anim_prop_event and visual-state regression. All first_unmet
values are null. Worker ModelPhysics 10/10 and native branch receipts remain in packet scratch.
Port generator --check and repo policy passed. Only base199's audit row is promoted; broad brief,
P1/D02 and higher animating/NPC consumers remain open. Full arm/full arena/repeated boot-order
gates are still required at their closing scope; no full story closure is claimed here.

## Remaining foundation and next witness

Next packet: [next-packet.md](next-packet.md). Reconcile P1's existing body ownership/lifetime evidence:
model, static bbox and shadow caller arms; real enumeration/replacement/removal; stale callback
and alias fences; required native saved state. Select the first concrete unmet consumer contract
from source and retained receipts. Do not append GetCollideable/P2 to this run.

Then close E2/P2 and integrated P3: actual filters/query results/material association, mutation
phase, think/removal fences, shadow targets versus solved poses, publication timing and
parent/local/absolute/bounds through the required live caller. Existing contact/frame/material
discrepancies remain unaccepted. An IVP clone is never a prerequisite.

## Authority and scope

- [delivery-plan.md](delivery-plan.md), [milestones.tsv](milestones.tsv),
  [delivery-map.tsv](delivery-map.tsv), [service-contract-map.tsv](service-contract-map.tsv).
- [handoffs.md](handoffs.md) for higher layers; [spec.md](spec.md) for the full closing gate.
- Retained walks/history: [walks/L0-physics-producer.md](walks/L0-physics-producer.md),
  [walks/L0-sound-producer.md](walks/L0-sound-producer.md), [progress.md](progress.md),
  [runs.md](runs.md). Read only relevant sections; historical run counts are not progress percentages.
- Retail-save import/dry runs/retail roster conversion excluded. Native save/load/travel and
  faithful L0 state remain required. Account for the four retained scope-review briefs before
  full closure. No gameplay divergence is approved. No owner decision is currently pending.

Full closure still requires every active record in one arena run, required repeated boot orders,
default and arm zero failures, baseline preservation, owned audit verdicts, observable engine
contracts, registered higher hooks and disposition of every mapped requirement.
