# 0006 first-kill — Jack sends the player back to kill the park patrol: melee combat, the enemy transaction, death

## Witness

On `sp_tutorial_1`, from real input, the player draws a melee weapon and kills the patrol NPC:
weapon visible in hand in third person, the NPC's combat AI closes and swings back, the blow
lands inside its clip's authored contact window, damage and the health commit resolve as retail,
the NPC's death transaction runs (`OnKilled`, the death sequence ladder, the pose handoff), and
the corpse falls under ragdoll simulation and stays lootable where it died. Proof is the
owner-played acceptance sweep this spec's rows share (LIFE4/LIFE5's sweep, which rides a real
attack input) plus the existing headless coverage: `elysium.gr_wield_check` (wield tracking),
the melee contact walk's sub-step sweep tests, and PHYS1's ragdoll acceptance. Scope is held to
this one NPC and this one fight; the broader combat system (multiple enemies, the full schedule
graph, ranged weapons) is witnessed later on `sm_hub_1`.

## Scope

- Roadmap rows absorbed:
  - **13.3 Firearms & melee basics** — the melee half only: the damage spine, the one typed
    health commit, the melee half of the weapon controller's two-part attack transaction, the
    melee contact instant. Firearms/ranged goes to 0009.
  - **13.5 Combat AI** — none built here. The whole NPC AI (senses, memory, the enemy
    transaction, the alert/combat schedule families, the schedule host) is **0005**'s; this spec
    witnesses the first fight against it and owns the weapon, damage, death and ragdoll half.
  - **LIFE4 Weapons in hands — third person** (whole row).
  - **LIFE5 Reactions and combat actions** (whole row).
  - **PHYS1 The ragdoll rig** (whole row).
- Out of scope (belongs to another spec or is parked):
  - 13.1 Stealth / the stealth-kill transaction (`vdata/system/StealthKillRules.txt`, grapple
    mode 3, RE50) — this witness is an open melee kill, not a stealth kill.
  - 13.2 Disciplines, 13.4 Computer terminals/hacking — separate specs.
  - `SkillRequirement`'s consumer, the ranged spread-cone interpolation, the step-9 template
    filters and word-15 commit — 0009 (firearms).
  - PHYS2 The physics hands, PHYS3 The rest of the physics world — separate specs.
  - LIFE6 The first-person viewmodel, LIFE7 The cinematic path and gestures, LIFE10 the weapon
    layer composition gap — separate specs.
  - `COND_HIT_BY_DOOR` / door-obstruction retreat, the `Prone` state — **0005**.
  - The footstep hearing producer — **0005**.
  - Paired actions — moved to LIFE7 (owner call, made).
  - The multi-enemy / full schedule-graph combat system — witnessed on `sm_hub_1` in a later spec.

## Requirements

1. **Melee damage spine.** One typed health commit and a weapon controller with a two-half
   attack transaction; the melee half is in scope. `docs/vtmb/combat-and-damage.md`.
2. **Melee contact instant.** Scheduled on one queue; the character bake must carry authored
   animation events so a real notify delivers the same input the queue models today.
   `docs/vtmb/combat-and-damage.md`.
3. **Weapons visible in hand, third person.** Four item model roles, the equip transaction and
   sex selection, follow-attach bone copy, the seven unskinned prop bones, `anim_prefix` —
   already reproduced; this spec closes the owner-played acceptance sweep proving it on real
   input. `docs/vtmb/wielded_weapons.md`,.
4. **Player attack input.** A real `+attack` press turns into the weapon transaction through
   retail's `ItemPostFrame` refusal order; a press-edge for melee, with a firearm mode's
   `allow_autofire` the one held exception (ranged behaviour itself is 0009's).
5. **Melee contact walk.** Authored per-clip swing windows tick-batched into 100 Hz sub-steps,
   swept bone segments through the embodiment seam, per-record hit-once groups, one opposed roll
   staged at swing start off retail's 60-unit/0.7 query, serial-scoped. Replaces
   `ContactEventCycle` for melee entirely.
6. **NPC melee sequence selector — player arm.** Vtable slot 331 (`+0x52c`); `CBasePlayer`
   (`0x10160F90`) is direction-keyed: requires a melee-capable weapon (`+0x5a0 & 0x18000`),
   matches each candidate's authored button mask at `+0x2D4` against `m_nButtons & 0x79A`,
   prefers exact → directional → strafe → neutral, answers `-1` when no candidate authors a
   mask. Reproduced via `FElysiumActivityClipRequest::bRequireStateMask`.
7. **NPC melee sequence selector — cast arm (open).** `CBaseCombatCharacter::
   ChooseMeleeAttackSequence` (`0x10347180`) is recovered whole (`docs/vtmb/combat-and-damage.md`
   → "The cast arm": four scored bits, subset match, sixteen-step ranked search, an
   `actweight`-weighted draw when more than one candidate matches, zeroing a candidate's score
   when `+0x2D4` authors a mask) but **not reproduced**; the runtime draws by weight in its
   place, a stated stand-in.
8. **`2COMBO` substitution.** Because no `ACT_MELEE_ATTACK_2COMBO_<FAMILY>` clip authors a mask,
   the player arm never answers one, so the substitution is offered and refused every time on the
   player — verified live, 43/43 player presses at Melee 5 — while every NPC reaches it normally
   through the cast arm.
9. **Reaction is not an idle.** `StateForActivity` maps locomotion plus `Unknown`/`Swim`/
   `Treadwater` onto eight states; a knockback or death projects to `Idle` and plays as an idle
   overlay unless a family extends the activity codes and state projection together.
10. **Grounded knockback.** The verified retail gate: margin-band entry, alive ∧
    `¬Disallow_Knockbacks` eligibility, asymmetric direction bands, NPC-only yaw snap, zero RNG in
    the gate path. Landed with the single `NORMAL_HIGH_{dir}` candidate and the hit-buildup gate
    as named stand-ins (see #12).
11. **Flying knockback chain (open).** `docs/vtmb/combat-and-damage.md` → "Launch is a velocity
    assignment, in two stages" / "How a flying chain ends": launch is an assignment computed on
    the flying branch, applied the *next* think (one-think delay is part of the behaviour);
    magnitude interpolates recovered horizontal/vertical pairs on
    `t = GetRawAttackValue * 0.1`; direction is `normalize(victim − attacker)` z-zeroed, or the
    victim's negated facing with no attacker, or the attacker's velocity in a chain reaction. The
    flying path forces direction bucket 0 (+180 yaw); `TranslateFlyingKnockback` downgrades to
    grounded for a player victim, who is never launched. Terminator is land detection, not clip
    end; wall contact is a sub-chain (rebound = wall vector × 100.0, timer at `curtime + 0.01`,
    enters `..._WALL_FALL`). A gunshot enters the same chain via shooter-to-victim distance
    against the fire mode's `Major`/`MinorKnockbackDist` (0009's producer; this spec owns the
    chain it feeds).
12. **Authored knockback table and eligibility gate.** Cell comes off the landing swing record's
    four direction buckets rotated by `+0xB8`; the gate carries all four recovered terms
    including the hit-buildup counter and the `CNPC_VTzimisceRunner` class bypass. One selector
    arm not reproduced: retail's per-class default activity at virtual `+0x644` when bucket 0 is
    also empty — the no-record `0x8B` cell stands in. Hit-buildup is one scalar per victim,
    admitted at `<= npc_hit_buildup_amount` (default 2) or record `+0xBA == 2`, cleared by the
    body's own swing passing 0.8 of its cycle. `docs/vtmb/combat-and-damage.md` → "The authored
    table lives in the swing record".
13. **Blocked/stagger family.** Clip `reach_cm`/`blocked_reaction` columns, the authored reaction
    fade, one `PlayReactionActivity` producer, `WasMeleeBlocked`, defender `ACT_BLOCK`/
    `ACT_BLOCK_HEAVY`, attacker's authored blocked reaction with `ACT_BLOCKED_REACTION_RIGHT`
    fallback, base-channel holds both sides, flinch yield, player's `wpn_secondaryatk` block
    intent, save schemas 26/27.
14. **Reaction channel envelope.** `DamageFlinch`'s fade at `0x103229d0`: 0.1 s in / 0.3 s out, no
    hold. `0x10345AB0`'s facing constant is `0.0f` — open forward hemisphere with a `1e-4`
    coincident-origin bypass. Resume phase: no stored phase — the apply path zeroes `m_flCycle`
    on a sequence change unless the incoming activity is a gait on both sides.
    `docs/vtmb/combat-and-damage.md`, `animation_and_movers.md`.
15. **NPC retaliation.** Damage feeds a 5-second derived enemy memory that stated relationships
    supersede; a struck neutral reaches Combat and swings back through the same weapon
    transaction. This is this spec's enemy-transaction/attack half of 13.5.
16. **NPC death family.** `OnKilled` transaction (claims released, Mind Dead, frozen-not-hidden,
    collision off); `TASK_PLAY_DEATH_SEQUENCE` ladder (arg → `ACT_DIESIMPLE` → `ACT_IDLE`); a
    handoff that holds the final pose until PHYS1 bakes a physics asset; corpse state restored
    synchronously on load. `docs/vtmb/npc-ai-reverse-engineering.md`, RE-D1–RE-D5.
17. **Ragdoll rig (PHYS1).** R8's model GLB and cooked physics source-data projection carry
    geometry, solid/ledge ownership, constraints, parameters, metadata, provenance (324 rigs, 289
    with 15 solids/14 constraints). Two calibrations gate simulation and must be scored against
    evidence, not guessed: the **solid transform frame** (score against each model's bind-pose
    bone transforms) and the **constraint axis identity** (score by settled pose under gravity).
    Simulation admission must fail rather than guess until both are settled.
18. **Ragdoll bake.** One `USkeletalBodySetup` per solid with authored convex hulls as
    `AggGeom.ConvexElems`, authored mass as a body-instance override, `damping`/`rotdamping` as
    linear/angular damping, `surfaceprop` through the existing surface-property table; one
    `UPhysicsConstraintTemplate` per constraint with frames, finite-angle limits and friction
    from PHYS1's own acceptance. Zero-freedom source records must not be assumed a weld; source
    friction must not be substituted with unmeasured Chaos damping.
19. **Death impulse.** The force envelope recovered whole in `docs/vtmb/combat-and-damage.md`:
    the damage force or its synthesised replacement, plus absolute velocity, plus the
    physics-object term, clamped to `50000.0`, applied at the hit bone or `Bip01 Spine2` when the
    hit bone is unknown, converted through the Source impulse unit. Retires the recorded "no
    impulse" divergence.
20. **Corpse interaction volume stays at the death origin.** Retail's corpse entity *is* the
    dying NPC, frozen non-solid at the death spot, while only the drawn body slides; the
    use/feed/loot anchor does not follow the pelvis. Modernization: making it follow is a Feel
    divergence and needs an explicit owner call before it is written — currently unruled, so not
    built.
21. **`StartBodyRagdoll`/`HoldBodyFinalPose` handoff.** `StartBodyRagdoll` needs no change and
    simply stops returning false once PHYS1 lands; `HoldBodyFinalPose` becomes the fallback for a
    body with no rig, which is what it was always described as.

## Design

`FElysiumEntityWorld`/embodiment own the melee contact sweep and the swing-record-driven
selector on the player side; the cast-arm weighted draw is a temporary stand-in beside
`ElysiumSwingContact.h`. `StateForActivity` and the activity/state projection carry every new
reaction family (knockback, death) through the same locomotion state machine a gait uses, so a
readout and a pose cannot disagree. The flinch/reaction claim is a holdable claim released by
condition (`ClipCompletion`/`Envelope`/`Predicate`). Death hands off to PHYS1 through the existing
`HoldBodyFinalPose` seam; PHYS1 is a V2 physics builder over R8's preserved source data, gated by
two scored calibrations, that authors a `UPhysicsAsset` onto the baked `USkeletalMesh`. The
governing rule for the physics half: **VtMB owns the rules, Chaos owns the solve**
.

## Seams

- Consumes: R8's preserved model GLB / cooked physics source-data projection (physics geometry,
  solids, constraints, parameters, metadata, provenance); 9.8 (inventory) and 11.10 (Play-tier
  harness) for 13.3's acceptance; the AUD2 footstep hearing producer (unknown spec number) which
  13.5 needs for the sound-event bus half — not built here; 0005 for the senses/conditions layer
  the enemy transaction reads; 0009 for the ranged fire-mode `Major`/`MinorKnockbackDist` values
  the flying knockback chain (#11) consumes.
- Provides: the melee damage spine and typed health commit that 13.2's Bloodshield/Fortitude/
  Potence joins consume; the `HitInfo` AI-schedule channel 13.5's kernel reads; the reaction/death
  pose machinery LIFE7's paired actions and montage mechanism build on; the ragdoll physics asset
  and death-impulse seam PHYS2/PHYS3 place bodies against.

## Tasks

- [~] **13.3 melee half** — damage spine, typed health commit, weapon controller melee half
  landed headless. Open: melee contact instant via real animation-event notify (currently
  queue-scheduled), `SkillRequirement`'s consumer.
- [~] **13.5 enemy-transaction half** — enemy transaction, alert/combat schedule families,
  `aiscripted_schedule` landed headless over the schedule kernel and body-owner arbiter. Open:
  played beats, the flinch/reaction action family (this spec builds it as LIFE5).
- [~] **LIFE4** — corpus/masters/`DA_WieldModels` baked; prop-bone tracking, channel arbitration,
  equip funnels, visibility suppression and weapon-state animation landed; real-input draw works
  via the command bus. Open: [ ] owner-played acceptance sweep (rides LIFE5's sweep).
- [~] **LIFE5** — body-kind resolver fork, one activity door, `2COMBO` substitution, reload chain,
  hit-yaw reaction stream, sequence-event carrier (all three slices), blocked/stagger family,
  player attack producer, melee contact walk, combo family, NPC retaliation, grounded knockback,
  NPC death family, holdable reaction claim, `DefaultSlot` montage-slot mechanism all landed.
  Open:
  - [ ] Owner-played acceptance sweep (closes LIFE4's too).
  - [ ] Flying knockback chain (RE-unblocked; formula and terminator recovered, #11).
  - [ ] Consume the authored per-attack knockback table now in the sidecars.
  - [ ] NPC melee sequence selector cast arm (`ChooseMeleeAttackSequence`, #7) — runtime draws by
    weight today.
  - [ ] Four named residuals: NPC-side reaction-claim release on a mid-hold body swap; montage
    route's inert restart rule; a restored scripted beat's un-retaken segment claim;
    `QuerySwingContacts`' live-only coverage.
- [ ] **PHYS1** — deferred after R8 (owner ruling 2026-09-05). Open:
  - [ ] Solid transform frame calibration (score against bind-pose bone transforms).
  - [ ] Constraint axis identity calibration (score by settled pose under gravity).
  - [ ] V2 physics builder: `UPhysicsAsset`/`USkeletalBodySetup`/`UPhysicsConstraintTemplate` bake.
  - [ ] Death impulse application at the hit bone (or `Bip01 Spine2` fallback).
  - [ ] `StartBodyRagdoll` returns true; `HoldBodyFinalPose` becomes the no-rig fallback.
  - Open owner call: corpse interaction volume following the pelvis (currently ruled against;
    stays at death origin until an explicit call is made).

## Open questions

- Whether/when to reproduce `ChooseMeleeAttackSequence`'s cast arm, or keep the weighted-draw
  stand-in permanently (plan states it as open, no owner call recorded).
- The corpse interaction-volume-follows-pelvis divergence: unruled, needs an explicit owner call
  before it is written.
- Where the audio programme's place lands relative to this spec's schedule (roadmap notes it as
  an open owner call generally, not specific to this spec).
- Exact spec number(s) for AUD2 (footstep hearing producer) and 0005/0009's precise scope
  boundaries with this spec's seams — stated here as best-known, not confirmed.
