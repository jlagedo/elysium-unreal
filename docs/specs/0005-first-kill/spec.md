# 0005 first-kill — Jack sends the player back to kill the park patrol: melee combat, the enemy transaction, death

## Witness
On `sp_tutorial_1`, from real input, the player draws a melee weapon and kills the patrol NPC:
weapon visible in hand in third person, the NPC's combat AI closes and swings back, the blow
lands inside its clip's authored contact window, damage and the health commit resolve as retail,
the NPC's death transaction runs (`OnKilled`, the death sequence ladder, the pose handoff), and
the corpse stays lootable where it died. Scope is this one NPC and this one fight; multiple
enemies, the full schedule graph and ranged weapons are witnessed later on `sm_hub_1`.

## Scope
The melee half of the weapon controller's two-part attack transaction, the damage spine and its
one typed health commit, the melee contact instant, wielded weapons in third person, the
reaction and knockback families, NPC retaliation, the NPC death family. Owned elsewhere and
consumed here: the whole NPC AI (senses, memory, the enemy transaction's schedule families, the
schedule host, the motor) — **0002**; the stealth kill — **0002** (3a–3c); disciplines — **0006**;
firearms, the spread cone, `SkillRequirement`, the word-15 commit — **0008**; the ragdoll rig,
the death impulse and the corpse volume — **0014**; the first-person viewmodel — **0013**; the
weapon overlay layers — **0015**; paired actions — **0010**.

## Sources
- Oracle: `docs/vtmb/combat-and-damage.md`, `docs/vtmb/wielded_weapons.md`,
  `docs/vtmb/animation_and_movers.md`, `docs/vtmb/npc-ai/README.md` (the death
  family, RE-D1–RE-D5), `docs/vtmb/animation_events.md`.
- Authored data, V2 seams under `$ELYSIUM_WORK_ROOT/exports_v2/`: `models/` (swing records, the
  per-attack knockback table in the sidecars), `vdata/` (`npctemplate*.txt`, weapon tables),
  `maps/sp_tutorial_1.entities.glb`.

## Witness data
- **Contact.** Authored per-clip swing windows tick-batched into 100 Hz sub-steps, swept bone
  segments through the embodiment seam, per-record hit-once groups, one opposed roll staged at
  swing start off retail's 60-unit / 0.7 query, serial-scoped.
- **Player selector.** Slot 331 (`+0x52c`); `CBasePlayer` (`0x10160F90`) is direction-keyed:
  requires a melee-capable weapon (`+0x5a0 & 0x18000`), matches each candidate's authored button
  mask at `+0x2D4` against `m_nButtons & 0x79A`, prefers exact → directional → strafe → neutral,
  answers `-1` when no candidate authors a mask. `2COMBO`: no `ACT_MELEE_ATTACK_2COMBO_<FAMILY>`
  clip authors a mask, so the player arm never answers one (43/43 presses at Melee 5) while every
  NPC reaches it through the cast arm.
- **Cast selector.** `CBaseCombatCharacter::ChooseMeleeAttackSequence` (`0x10347180`): four
  scored bits, subset match, sixteen-step ranked search, an `actweight`-weighted draw when more
  than one candidate matches, a candidate's score zeroed when `+0x2D4` authors a mask.
- **Knockback, grounded.** Margin-band entry, alive ∧ `¬Disallow_Knockbacks`, asymmetric
  direction bands, NPC-only yaw snap, zero RNG in the gate. The NPC knockback start `0x102a01b0`
  (gated on `0x1028a190` and `!IsInDialog`, `SelectHeaviestSequence`, `m_knockbackType`)
  dispatches slot 614 `ResetThinkTimers` before installing `0x14d` / `0x14c`.
- **Knockback, flying.** Launch is an assignment computed on the flying branch and applied the
  *next* think; magnitude interpolates recovered horizontal/vertical pairs on `t =
  GetRawAttackValue * 0.1`; direction is `normalize(victim − attacker)` z-zeroed, the victim's
  negated facing with no attacker, or the attacker's velocity in a chain reaction; the flying
  path forces bucket 0 (+180 yaw); `TranslateFlyingKnockback` downgrades a player victim to
  grounded; the terminator is land detection; wall contact is a sub-chain (rebound = wall vector
  × 100.0, timer `curtime + 0.01`, `..._WALL_FALL`). A gunshot enters by shooter-to-victim
  distance against the fire mode's `Major`/`MinorKnockbackDist` (0008's producer).
- **The authored table.** The cell comes off the landing swing record's four direction buckets
  rotated by `+0xB8`; the gate carries the hit-buildup counter (one scalar per victim, admitted
  at `<= npc_hit_buildup_amount` (2) or record `+0xBA == 2`, cleared by the body's own swing
  passing 0.8 of its cycle) and the `CNPC_VTzimisceRunner` bypass; the per-class default
  activity at virtual `+0x644` when bucket 0 is also empty.
- **Blocked/stagger.** Clip `reach_cm`/`blocked_reaction`, the authored reaction fade, one
  `PlayReactionActivity` producer, `WasMeleeBlocked`, defender `ACT_BLOCK`/`ACT_BLOCK_HEAVY`,
  the attacker's authored blocked reaction with `ACT_BLOCKED_REACTION_RIGHT` fallback, the base
  channel holding both sides, flinch yield, the player's `wpn_secondaryatk` block intent.
- **Reaction envelope.** `DamageFlinch`'s fade at `0x103229d0`: 0.1 s in / 0.3 s out, no hold;
  `0x10345AB0`'s facing constant is `0.0f` with a `1e-4` coincident-origin bypass; no stored
  resume phase — the apply path zeroes `m_flCycle` on a sequence change unless both sides are a
  gait. A knockback or death projects to `Idle` in `StateForActivity` and plays as an idle
  overlay unless a family extends the codes and the projection together.
- **Retaliation.** Damage feeds a 5-second derived enemy memory that stated relationships
  supersede; a struck neutral reaches Combat and swings back through the same transaction.
- **Death.** `OnKilled` (claims released, Mind Dead, frozen-not-hidden, collision off);
  `TASK_PLAY_DEATH_SEQUENCE` ladder (arg → `ACT_DIESIMPLE` → `ACT_IDLE`); the handoff holds the
  final pose; corpse state restored synchronously on load.

## Stories
In build order. A story is done when every behaviour it lists is in the substrate and its
recovery is written in the oracle section it names. Numbers are stable ids cited by other
documents; a split keeps the number and adds a letter. Each open story carries the retail
contract the code must match, the job, what it consumes or provides, and a size (XS–XL) with the
model / effort tier recommended for it.

- [x] **1. The melee damage spine and the typed health commit** (was 13.3, melee half). One
  typed health commit; the melee half of the weapon controller's two-half transaction; the
  `+attack` press through retail's `ItemPostFrame` refusal order (press-edge for melee,
  `allow_autofire` the held exception). Oracle: `combat-and-damage.md`.
- [x] **2. Weapons in hand, third person** (was LIFE4). Four item model roles, the equip
  transaction and sex selection, follow-attach bone copy, the seven unskinned prop bones,
  `anim_prefix`; prop-bone tracking, channel arbitration, equip funnels, visibility suppression,
  weapon-state animation. Oracle: `wielded_weapons.md`.
- [x] **3. The melee contact walk and the player selector** (was LIFE5). The sub-step sweep,
  the direction-keyed slot-331 arm via `FElysiumActivityClipRequest::bRequireStateMask`, the
  `2COMBO` substitution, the combo family. Oracle: `combat-and-damage.md`.
- [x] **4. Reactions, blocked/stagger, grounded knockback, retaliation, death** (was LIFE5).
  `StateForActivity`'s eight states; the blocked family with save schemas 26/27; the grounded
  gate with the `NORMAL_HIGH_{dir}` candidate and the hit-buildup gate as named stand-ins; the
  5-second derived memory; the `OnKilled` transaction and the death ladder; the holdable reaction
  claim; the `DefaultSlot` montage-slot mechanism. Oracle: `combat-and-damage.md`,
  `npc-ai/README.md` (RE-D1–RE-D5).
- [ ] **5. The melee contact instant from a real animation event.**
  Retail: the contact is an authored animation event on the swing clip; the queue-scheduled
  instant the port runs today models it without a notify.
  Job: the character bake carries the authored events; a real notify delivers the same input the
  queue models; the queue path retired.
  Oracle: `animation_events.md`, `combat-and-damage.md` § "The contact instant".
  Size: M. Effort: Sonnet / high.
- [ ] **6. The cast-arm selector.**
  Retail: `ChooseMeleeAttackSequence` (`0x10347180`) as recovered above.
  Job: the scored search and the weighted draw replacing the runtime's draw-by-weight stand-in.
  Oracle: `combat-and-damage.md` § "The cast arm".
  Size: M. Effort: Sonnet / high.
- [ ] **7. The knockback start's think reset.**
  Retail: `0x102a01b0` dispatches slot 614 before installing `0x14d` / `0x14c`, so a knocked-back
  NPC thinks on the frame of the hit.
  Job: `FElysiumNpc::ResetThinkTimers` at the port's knockback install.
  Consumes: 0002/15.
  Oracle: `npc-ai/lifecycle.md` § "The think cadence, decoded" (Slot 614 dispatch sites).
  Size: XS. Effort: Sonnet / low.
- [ ] **8. The flying knockback chain.**
  Retail: the launch, its one-think delay, the magnitude and direction rules, the terminator and
  the wall sub-chain (Witness data).
  Job: the chain on the flying branch; a player victim downgraded; the gunshot entry left as the
  seam 0008 fills.
  Oracle: `combat-and-damage.md` § "Launch is a velocity assignment, in two stages", § "How a
  flying chain ends".
  Size: M. Effort: Opus / high.
- [ ] **9. The authored knockback table.**
  Retail: the cell off the landing swing record's buckets rotated by `+0xB8`; the gate's four
  terms; the `+0x644` per-class default when bucket 0 is empty.
  Job: the table now in the sidecars consumed in place of the `NORMAL_HIGH_{dir}` candidate; the
  hit-buildup gate real; `+0x644` a named seam (the no-record `0x8B` cell stands in).
  Oracle: `combat-and-damage.md` § "The authored table lives in the swing record".
  Size: M. Effort: Sonnet / high.
- [ ] **10. Four named residuals.**
  Job: the NPC-side reaction-claim release on a mid-hold body swap; the montage route's inert
  restart rule; a restored scripted beat's un-retaken segment claim; `QuerySwingContacts`'
  live-only coverage.
  Size: S. Effort: Sonnet / medium.

## Seams
- Provides: the damage spine and typed health commit to 0006's Bloodshield/Fortitude/Potence
  joins and to 0008; the `HitInfo` AI-schedule channel 0002's kernel reads; the reaction and
  death pose machinery 0010's paired actions build on; the death handoff 0014 completes.
- Consumes: 0002 (senses, conditions, the enemy transaction's programs, the motor); 0008 for the
  fire mode's `Major`/`MinorKnockbackDist` the flying chain reads.
- Open recoveries: none beyond the `+0x644` default activity (9).
