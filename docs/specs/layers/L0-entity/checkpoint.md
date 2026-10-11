# L0 current planning checkpoint

Planning snapshot: 2026-10-11. The owner has now authorized the main thread to resume under
the revised [Goal](goal.md), superseding the earlier pause pending review. Main must record
actual startup/Goal status when it receives the handoff. This is not a worker report, a
successful gate or an implementation commit.

## Main startup and plan acceptance

Main inspected the handoff on 2026-10-11: HEAD is the accepted sound commit
`8f8aea1b0619b5b07711b2174218cba40a1847f8`, the index is empty, and all 34 dirty/untracked
paths match the separate planning inventory. The owner-authorized execution turn is proceeding
under `goal.md`; the sound source and receipts are reused. Main reviewed the scope, milestone
graph, traceability and closing gate before the separate documentation acceptance commit.

The supported Goal query currently returns `goal: null`. Main has not created a replacement
Goal, changed Codex internal storage, replaced the app objective or claimed an app-level resume.
Unattended continuation requires the existing Goal to be resumed or reattached in the app;
this app-state limitation does not block the authorized implementation turn.

## Start here

- Active plan: [delivery-plan.md](delivery-plan.md).
- Owner-authorized execution instructions: [goal.md](goal.md).
- Brief ownership/scope: [delivery-map.tsv](delivery-map.tsv).
- Milestone dependency graph: [milestones.tsv](milestones.tsv).
- Engine replacement contracts: [service-contract-map.tsv](service-contract-map.tsv).
- Higher-layer obligations: [handoffs.md](handoffs.md).
- Planning coverage and validation: [plan-review.md](plan-review.md).
- Existing evidence: [progress.md](progress.md), [runs.md](runs.md) and the relevant `walks/`
  sections. Read only the sections needed by the next packet.

## Implementation snapshot to reconcile in main

- Last observed HEAD: `8f8aea1b0619b5b07711b2174218cba40a1847f8` (live sound-script source
  and native audio readiness). Main committed this packet while the planning review ran.
  Planning documents remain separate uncommitted work; do not include them in a code packet.
- Main's newly written `progress.md` entry reports native audio 2/2, affected 21/21, default
  194/194 and six sound/KV arena records passing, with actual playback/lifetime evidence linked
  in `walks/L0-sound-producer.md`. The packet is now committed at the HEAD above. Reuse those
  receipts; this side review did not rerun the gates or certify the whole audio subsystem.
- Historical original-run acceptance: r001–r021, r029 and r030 (23 runs). Preserve that evidence;
  do not infer that every appended parked requirement or subsystem is closed.
- No implementation status or audit verdict was changed by this planning pass. No engine jobs,
  code edits, commits, pushes, Goal changes or worker dispatch were performed by this pass.

## Next milestone after plan acceptance

First read the committed sound packet's checkpoint and review/commit the planning changes
separately, as directed in `goal.md`. Then close **D02 physical entity
foundation** through P1, E2/P2 and P3. Reuse the accepted body/material/shadow work and confirmed
walks. The first dispatch packet must name the smallest remaining observable gap from the
current code and receipts. A narrow source inspection found the base velocity query and
GetCollideable still stubbed; [next-packet.md](next-packet.md) prepares a bounded P1 getter
candidate with its precise oracle-confirmation and producer checks. This is not a full code audit
or a dispatched task.

Known open contracts in the latest recorded evidence: broader body/swap/filter lifetime,
query/contact callback equivalence, moving/shared-edge authored material association,
gameplay timing/publication, and the integrated transform/bounds consumer that blocked r022.
The committed sound packet has the reported native evidence above; generic emission and impact
consumer boundaries remain open. These are milestone requirements; they are not permission
to reconstruct IVP or implement higher layers.

## Scope and owner decisions

- Retail-save import, dry-run decoder work and retail roster conversion are excluded by the
  owner. Native persistence and safe rejection of unsupported native saves stay in scope.
- Higher-layer parked remainders are explicit handoffs in the map; verify/register the exact
  hook and recipient obligation before claiming L0 closure. Do not implement those layers here.
- Retained non-core constraint/animation-link family and generic physics-asset bake obligations
  need core-dependency/scope accounting. Keep them visible; no silent deletion or expansion.
- No gameplay divergence is accepted by the replan. Routine faithful implementation, Unreal
  API choice and producer/test repairs need no additional owner approval.

## Update after each accepted implementation run

Replace this snapshot with accepted HEAD, active milestone, exact contract closed and receipt,
first unmet condition, remaining gate requirements, next bounded packet and any genuine owner
decision. Preserve the historical logs by link. Do not replace this file with an accumulating
transcript or report original run count as percent complete.
