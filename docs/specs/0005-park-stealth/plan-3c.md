# 3c — Stealth-kill commitment and paired action

Planning baseline: `9262bb9c`, 2026-09-08; clean tree before this document. Scope:
[spec.md](spec.md), checkbox **3c**. This is an implementation plan; no runtime changes,
builds, or live acceptance were performed. The checkbox stays open.

## Outcome

Real player input commits a qualified victim into a type-3 grapple. The attacker drives the
retail paired action, the victim dies at the recovered completion boundary through 0006's
death transaction, and the victim's authored `OnDeath` advances the tutorial. Both bodies,
weapons, movement, AI, camera, and input return to their correct post-action states.

## Findings that determine the work

| Seam | Current evidence | Implementation consequence |
|---|---|---|
| Admission | `ElysiumStealthKillRules.cpp::FindVictim` implements the cached query, capability, target predicate, rear arc/obliviousness, distance, and partner check. Its comment explicitly defers stand clearance and paired-position selection to 3c. | Complete the shared admission path and call it from the query as retail does; successful selection must not promise an impossible pair. Preserve the same-frame cache. |
| Commitment | `FElysiumPlayer` exposes eligibility and victim lookup, but no commitment method. Retail `PlayerTryStealthKill` `0x10167370` writes player `+0x1c58` and `+0x1c60` before returning `StartGrappleAttack(victim, 3)`'s result. | Add one substrate commitment entry point; recover those fields' identities and failure semantics before choosing storage. |
| Pair state | `ElysiumCombatCharacter.cpp::EnterGrapplePair` records role/type/position, victim animation driver, and asymmetric holster flags. Entry is void and does not implement retail's full refusal, movement, holster, placement, or activity transaction. | Extend the shared mechanism; merely calling the existing helper does not implement 3c. Preserve feed and payphone consumers. |
| Animation | Retail `SetAnimation` `0x10164240` gives protected activities first refusal, leaves role 1 passive, and dispatches mode 3 to virtual `+0x694`; a completed leaf causes paired teardown and ordinary animation reselection. | Use attacker-owned advancement over the existing activity/claim system, with synchronized victim playback. |
| Death | `FElysiumNpc::OnKilled` already invokes shared death/output handling, releases animation/body ownership, and starts the death schedule. Shared `OnKilled` guards duplicate output. | Reuse this authority, but recover how the paired terminal pose survives the death handoff before integrating it. |
| Tutorial | The spec identifies `stealth_victim_maker`'s inherited child `OnDeath → thug_maker_4.Spawn`. | Exercise the authored maker/child/output queue chain, then the subsequent lesson progression. No lesson-specific completion call. |

### Input correction established during planning

The existing `stealth.md` input paragraph misidentifies `0x103eaca0` as secondary attack.
The assembly contains the stealth thunk call at **`0x103ead0b`**, before the normal primary
attack requests. `combat-and-damage.md` identifies this body as melee primary attack; the
corpus label is `CWeaponMelee_Torch::PrimaryAttack`, so class/vtable ownership still needs
closure verification. The actual secondary body **`0x103eae00`** requests activity `0x4e`
and contains no stealth attempt. Do not wire an unconditional secondary-attack commitment
from the old paragraph.

`PlayerUse` **`0x10167850`** also calls the thunk: after existing-target validation and the
`(buttons | pressed | released) & 0x20` gate, after the Protean-other exclusion, and before
ordinary target/use handling. This is not simply a rising-edge-only hook. Preserve that order.

## Implementation sequence

### 1. Close the consequential retail contracts

Before runtime edits, recover the functions, virtual owners/overrides, field readers/writers,
and callers for these parts. Use assembly wherever the decompilation is damaged or ambiguous.
Record the recovered chain in `docs/vtmb/stealth.md` and cross-reference existing animation,
feeding, and combat oracles rather than creating conflicting copies.

- Resolve player virtual **`+0x694`** to its implementation and walk every completion arm:
  sequence-finished predicate, victim validity, damage construction, flags, attribution,
  lethal dispatch, and ordering relative to `EndGrapple`. The exact handler address and lethal
  payload are not yet verified by this plan. Do not substitute a duration timer, guessed
  animation event, direct health assignment, or direct `OnDeath` emission.
- Close primary-attack class/vtable ownership, unarmed and weapon overrides, command dispatch,
  cooldown/busy gates, and failed-stealth fallback. Correct the input paragraph above.
- Walk `CanStartGrappleAttack` **`0x103285a0`**, position search **`0x10328af0`**,
  initial activity **`0x10328c80`**, `StartGrappleAttack` **`0x10328df0`**, activity translation
  **`0x10328380`**, and `SetGrappleActivity` **`0x1032a100`**. Recover size/orientation choices,
  both role-specific weapon-bank translations, sequence selection, and exact placement.
- Verify entry/leave overrides (**slots 379/380**, base **`0x10329760` / `0x10329a70`**),
  NPC alternate-AI suppression, saved movement/weapon restoration, weapon virtual `+0x534`
  with mode-3 activity `0x17`, camera activation/clearing, and teardown **`0x10329560`**.
  Include missing partner, refused second entry, missing clips, teleport, and external death.
- Inspect the actual V2 tutorial entity rows and the player/victim weapon-bank clips needed
  by the witness. Confirm sequence metadata, duration, root/bone alignment, and event delivery
  survive export, bake, and load. Read `pipeline/AGENTS.md` before any necessary pipeline edits.

Exit: an address-backed transition table with guards, ordered writes, side effects, failure
results, and live producers. Any unrecovered input gets a named field/accessor seam returning
“nothing”; an input essential to the witness remains an explicit acceptance blocker.

### 2. Complete the shared grapple substrate

Own the tightly coupled code in `ElysiumPlayer.h`, `ElysiumCombatCharacter.cpp`,
`ElysiumStealthKillRules.{h,cpp}`, and the relevant NPC/movement/embodiment services.

- Implement shared eligibility/position resolution and feed its result into victim lookup.
  Include standing-hull clearance, same-partner/type behavior, attacker handover, victim refusal,
  and clip availability in retail order. Preserve recovered query-side mutations if present.
- Make pair entry capable of the actual retail acceptance/refusal transaction: attacker first,
  victim second, appropriate rollback, real holster and move-state changes, saved placement,
  roles 0/1, type 3, position, and victim-to-attacker animation driver.
- Apply the recovered alignment and NPC alternate-AI/body ownership policy. Keep entity handles,
  state, clocks, and dispatch in plain C++; embodiment supplies collision, bones, and playback.
- Complete paired teardown and restoration once, shared by normal completion and interruption.
  Inspect every existing feed/payphone caller before changing the helper's contract.

### 3. Implement the mode-3 action and death handoff

Use the existing animation resolver, weapon banks, body claims, and gameplay clock. A dedicated
substrate grapple implementation file is appropriate if it keeps the shared lifecycle coherent.

- Start base **`ACT_SNEAKATTACK_SUCCESS` (`0x1015`)**, translate each role independently, select
  the matching authored sequences, and start their cycles together. The attacker advances;
  ordinary gait selection and victim AI cannot replace the pair while it owns the bodies.
- Execute the recovered weapon `0x17` action and associated presentation through existing
  services. Use the established grapple camera anchors and recovered activation path.
- Port virtual `+0x694`'s full completion policy. Route its lethal result through existing damage
  and death handling, preserving attribution and the terminal paired pose/death-schedule order.
- Ensure missing content or service failures issue a diagnosable `Warning` and take the recovered
  failure/teardown path. Expected gameplay refusals need no implementation-failure warning.
- Preserve save refusal while a live pair exists and clear transient pair/camera/claim state
  through map or entity lifecycle teardown. Do not introduce save migration.

### 4. Wire commitment and its player-facing result

- Add the single player commitment method beside `FindStealthKillVictim` in
  `ElysiumPlayerEntity.cpp`; reuse the cached query and return actual start success.
- Insert it at the recovered primary-attack point in `ElysiumWeaponClasses.cpp::AttackIntent`
  and the corresponding use point in `ElysiumEntityWorldInteraction.cpp`. Preserve cooldown,
  held/pressed/released semantics and normal fallback on failure. A committed input cannot
  also launch a swing, block, conversation, or ordinary use.
- Publish the eligible stealth action through the existing interaction/HUD snapshot so the
  visible prompt and input consume the same gameplay answer. Rendering must not run a second
  admission query or invent eligibility. Audit current action classification and movement locks
  for type 3 instead of treating it as ordinary feed release.

### 5. Verify the integrated witness

Extend `ElysiumStealthKillTests.cpp` with behavior tests, and relevant existing input,
animation, camera, feed, and death fixtures where their contracts change.

| Test boundary | Required evidence |
|---|---|
| Admission | Stand obstruction, missing role clip, short/tall and orientation variants, occupied partner, same-frame cached success/failure; 3a/3b regressions remain valid. |
| Real dispatch | Qualified primary and use inputs commit; failed attempt falls through correctly; secondary follows its recovered behavior; repeated/held/released input cannot duplicate the action. Exercise dispatch, not only the new helper. |
| Transaction | Both roles/type/driver/position, actual holster and motion ownership, refused second entry and rollback, missing content, invalid partner, teleport and external death. |
| Timing | No premature death; completion follows the recovered attacker predicate; victim does not advance independently; replayed completion does not emit death twice. Test unequal frame steps across the completion boundary. |
| Handoff | Correct lethal attribution, `OnDeath` once, terminal pose/death schedule, partner release, restored movement/weapons, camera clear, and save gate. |
| Tutorial I/O | Authored maker output inherited by `stealth_victim`, queued `OnDeath → thug_maker_4.Spawn`, then the actual downstream lesson chain; no hardcoded tutorial shortcut. |
| Shared consumers | Ordinary feed including trance/release, payphone entry/exit, camera anchor resolution, normal melee input, and ordinary NPC death remain correct. |

Freeze source before `uv run elysium build`; then run `uv run elysium test
Elysium.Substrate.StealthKill` and the existing affected regression filters after confirming
their names. Follow `execution.md`'s single execution lease and review workflow for an
implementation run. Broaden checks only for changed dependencies or unresolved failures.

Finally play the tutorial through real input: acquire the prompt, commit, observe synchronized
bodies and camera, verify death timing and authored progression, then verify control recovery.
Use fists and an available melee weapon, with blocked-space and interrupted-pair checks.
Record logs and rendered evidence together. Check prompt readability at 1080p, 1440p, and 4K
if its presentation changes. Headless tests do not establish paired visual acceptance.

## Completion and scope boundary

3c is one integrated checkbox. Necessary shared grapple/death/animation/content repairs are
part of finishing it; neither the existing pair fields nor 0006's death code alone satisfies
the dependency. Do not expand this into all unimplemented grapple modes or the remaining NPC
AI stories. Preserve modes already consumed by feeding and payphone interactions.

Update the oracle and execution evidence as work lands. Mark 3c complete only after the full
input → pair → synchronized death → authored output path and its required failure arms work.
Report compilation, automated tests, independent review, and rendered tutorial acceptance
separately. Any required missing content/service producer remains open, with a concrete next
step; no new modernization is assumed by this plan.
