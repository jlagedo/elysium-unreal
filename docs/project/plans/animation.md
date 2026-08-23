# The character life programme (LIFE) — open-task specifications

Specifications for **open** LIFE tasks only. Status lives solely in `docs/project/roadmap.md`;
a task that lands is deleted here. No status marks in this file.

The programme's goal: **every character alive** — the cast and the player animating through one
resolver, weapons drawn into hands that swing them, bodies that react and die — with every VtMB
rule resolved at bake and Unreal running the result (the governing owner call:
`docs/architecture/animation-architecture.md`). The ladder is strictly cumulative: each rung
consumes the one below it and nothing above it. Retail evidence is the **banked capture corpus**
under `$ELYSIUM_WORK_ROOT/research` plus the closed RE record in
`docs/vtmb/animation_and_movers.md`; the live capture instrument is retired as a programme and
returns only as an escalation oracle on a named divergence
(`docs/vtmb/vtmb-animation-reverse-engineering.md` → "Programme method").

The CCC slice (camera, controls, the played movement feel) remains its own surface in
`plans/three-cs.md`; this programme owns everything a body plays. How to drive an agent
session per task — session shape, kickoff prompts, scope traps:
`docs/operations/life-agent-playbook.md`.

### LIFE4 Weapons in hands — the third-person wielded body

What a character holds, finished. The recovered contract — four item model roles, the equip
transaction and sex selection, the follow-attach bone copy, the seven unskinned prop bones, the
`anim_prefix` key, the shipped corpus — is `docs/vtmb/wielded_weapons.md`; the bake lane and
binding metadata are `docs/architecture/wielded-weapon-integration.md`; the corpus, masters and
`/ElysiumBaked/Items/DA_WieldModels` are on the mount. The runtime attachment exists in the
working tree (leader-pose follower, `(classname, sex)` resolution, the green-room `gr_wield`
lane); what remains is making it *true*:

- `changball`/`gio_spirit` are thrown projectiles — free-standing actors playing their own
  clips, never attached. The inherited unknowns (no recovered holster-carry state,
  `item_g_stake`'s nulled models, `handleclaws` tracking) stay named in the owning doc and gate
  nothing here. A row resolving to a null model is a legitimate no-geometry answer, never a
  missing-asset warning.

*Acceptance:* from real input in the arena, drawing each of the six wield-model melee weapons
puts its geometry in the correct hand on a male and a female body; the weapon tracks the hand
through locomotion and through a swing with no separate drive call; the honest check passes on
an animated base and fails when the prop-bone channels are removed; holstering removes the
model; first person suppresses it and third person restores it without a rebuild; the pose is
correct in the Content Browser preview, not only in our runtime. *Deps:* LIFE0; LIFE2
for the per-weapon translation half.

### LIFE5 Reactions and combat actions

What is left of the rung: the flying knockback chain, the authored knockback table the grounded
gate currently stands in for, the NPC melee sequence selector, and the owner-played acceptance
sweep. **Owns the pose, not the number**: lethality, soak, the damage roll and the health commit
are 13.3's; this rung consumes their outcome. The ragdoll a killed body hands off to is PHYS1's.

**The NPC melee sequence selector.** A melee attack's sequence is chosen through vtable slot 331
(`+0x52c`) on the **owner**, and the two arms are different systems rather than two settings of
one. `CBasePlayer` (`0x10160F90`) is the direction-keyed arm: it requires a melee-capable weapon
(`+0x5a0 & 0x18000`), matches each candidate's authored button mask at `+0x2D4` against
`m_nButtons & 0x79A`, prefers exact → directional → strafe → neutral, seeds its answer with `-1`
and returns `answer >= 0` — so an activity whose candidates author no mask is **not answered**, and
the caller performs its own fallback. That arm is reproduced: `FElysiumActivityClipRequest::
bRequireStateMask` refuses the weighted draw, and the melee swing sets it for a player-side owner.

`CBaseCombatCharacter::ChooseMeleeAttackSequence` (`0x10347180`) is the cast arm and is **not**
reproduced — the runtime draws by weight in its place. Its rule is recovered whole and is owned by
`docs/vtmb/combat-and-damage.md` → "The cast arm": four scored bits, a subset match, a sixteen-step
ranked search, and — where more than one candidate matches — an `actweight`-weighted draw, so the
arm is not deterministic. It reads `+0x2D4` exactly once, to **zero a candidate's score when a mask
is authored**, which is the mirror of the player arm: masked clips are the player's, unmasked clips
are the cast's.

What the slice owes, now that nothing about the rule is open:

- **Two pipeline fields that do not exist yet.** The low reach edge at `+0x2CC` (its own statedness,
  `FLT_MIN`-marked, 516 descriptors — not the same population as `reach`) and the 24-byte envelope
  records at `+0x2BC`/`+0x2C0`. `swings` does **not** carry the envelopes: those are the 188-byte
  contact records, a different array with a different job. An envelope's two corners are not a
  position, so a converter must not run them through the positional path — the axes are reach
  distance, lateral tolerance and vertical offset.
- **The scored query, which is three scalars rather than a box:** the XY-only distance from the
  attacker to the enemy's AABB centre, the signed height difference, and the enemy's half-extents.
- **The flag walk and the weighted draw as a pure rule** beside `ElysiumSwingContact.h`, including
  the bool's narrow meaning — it reports a premium match, not "a sequence was chosen", and the
  caller keys off the out-index.
- **No obstruction query.** Both arms trace and neither reads the result; the answer feeds only a
  debug overlay. A service-seam query here would be invented behaviour.

Until it lands, an NPC melee swing selects by weight, which is a stated stand-in and not the
recovered rule.

The behavioural consequence of the split: because no `ACT_MELEE_ATTACK_2COMBO_<FAMILY>` clip
authors a mask, the player arm can never answer one, so the automatic `2COMBO` substitution is
offered on the player and refused every time — while every NPC reaches it through the cast arm
normally. Live capture on the pinned retail binary, 43 of 43 player presses at Melee 5.

**A reaction is not an idle.** `StateForActivity` maps the locomotion slice plus `Unknown`,
`Swim` and `Treadwater` onto eight states, so a knockback or a death projects to `Idle` and a
resolved asset plays as an idle overlay. Each family added here extends the activity codes and
the state projection in the same change, or rides a montage slot and leaves `GraphState` naming
the held locomotion state.

**The flying knockback chain.** The grounded band lands the cell; this band moves the body. The
recovered contract is `docs/vtmb/combat-and-damage.md` → "Launch is a velocity assignment, in two
stages" / "How a flying chain ends", and the slice consumes it whole rather than re-deriving any
of it:

- **Launch is an assignment, not an impulse.** The reaction body computes the velocity on the
  flying branch alone and schedules the chain; the *next* think assigns it. Reproducing the
  one-think delay is part of the behaviour — a body launched in the same think leaves before the
  reaction cell is on screen. Magnitude interpolates the recovered horizontal/vertical pairs on
  `t = GetRawAttackValue * 0.1` (`1.0` with no attacker, forced `0.1` for a chain reaction), and
  neither the inputs nor the result are clamped; direction is `normalize(victim − attacker)` with
  z zeroed, the victim's own negated facing when there is no attacker, and the attacker's velocity
  in a chain reaction.
- **The flying path forces direction bucket 0** — a `+180` yaw offset — regardless of what the
  four-band classifier answered, and `TranslateFlyingKnockback` downgrades the flying range to the
  grounded one for a victim that is the player. The player is never launched; its reaction stays
  the view kick the grounded band already reproduces.
- **The terminator is land detection**, not a clip end: the `_INTO`/`_LAND`/`_WALL_LAND` cells end
  on sequence completion, while the *flight* ends when the body is grounded or is falling with a
  successful ground probe. Wall contact is a sub-chain rather than an ending — rebound velocity is
  the wall vector times `100.0`, a timer arms at `curtime + 0.01`, and the chain enters
  `..._WALL_FALL`. Movement goes through the motor seam; nothing here solves physics.
- **A gunshot enters the same chain**, and this band owns that entry too. A ranged hit dispatches on
  the victim, measures shooter-to-victim distance against the fire mode's authored
  `Major`/`MinorKnockbackDist`, and resolves a cell from a static direction/height/family table —
  inside the major band a `FLYING_INTO_{dir}` cell, so **a close shotgun hit launches an NPC**;
  inside the minor band alone a grounded `SMALL_{dir}`. It carries no swing record, so the authored
  candidate table above does not apply to it. Retail authors the pair on four weapons plus four
  patch additions, and the player's own slot is a no-op.

The cells this band plays are the flying half of the shipped corpus recorded in
`docs/vtmb/animation_and_movers.md`, and the get-up the corpus does not ship stays a named absence.

**The authored knockback table replaces the stand-in.** The swing sidecar already carries each
record's four direction buckets of up to four candidate activity names, plus the `+0xB8` rotation
byte and the `+0xBA == 2` unconditional marker (the `swings` column, written by
`pipeline/src/elysium_pipeline/formats/mdl_skel.py`). The
slice consumes them: the direction comes off the byte and bucket `k` answers direction
`(byte_b8 + k) mod 4`, so a consumer never assumes bucket 0 is one particular way round; a bucket
with more than one candidate draws with `RandomInt` on an owned stream. That retires
`StandInKnockbackSize`/`StandInKnockbackHeight` — the deterministic single `NORMAL_HIGH_{dir}`
candidate and its "no draw at all" note — and reaches the `SMALL` family and the two `LOW_BACK`
cells the vocabulary already carries.

The gate's two omissions are no longer RE questions, so the slice implements them rather than
reporting them (`docs/vtmb/combat-and-damage.md` → "Who may be knocked back"). The hit-buildup
counter is one scalar **on the victim**, admitted at `<= npc_hit_buildup_amount` (default `2`) or on
the record's `+0xBA == 2`, and cleared by the body's **own** swing passing `0.8` of its cycle — so
it needs a per-body counter and no relationship bookkeeping at all. What was recorded as a second,
unidentified template predicate is the dead-victim refusal the reproduction's alive filter already
carries; the one term still absent is the `CNPC_VTzimisceRunner` class bypass. The slice also owes
the ordering: retail commits health before the knockback entry, so a **killing blow is not
knocked back** — the body dies where it stands and hands off.

**The ragdoll handoff belongs to PHYS1, not here.** The death transaction hands off by holding the
final pose because no baked mesh carries a physics asset; generating one per body from the model's
own `.phy` rig, and seeding the simulation with the killing blow's force, is PHYS1's whole subject
(`docs/architecture/physics-architecture.md`). This rung owns only the point of handoff, which
already exists and needs no change. Until PHYS1 lands, the held pose is the recorded stand-in.

**Named residuals**, each small, each real:

- `FElysiumPlayer::OnRuntimeModelChanged` releases the held reaction claim before the body it was
  taken on is torn down; `FElysiumNpc`'s sibling does not, so a cast body swapped mid-hold leaves a
  zero-duration claim addressed to a component nothing can name again.
- The montage route rebuilds its montage on every play, so the recovered restart rule is inert on
  it. A held repeat re-settling onto a special idle therefore restarts where the graph route would
  hold — visible on crouch, whose non-looping into-pose is the counter-fact the rule exists for.
- A scripted beat restored mid-action replays its segment but does not re-take the segment claim,
  so the restored run holds its pose on a channel it does not own.
- `QuerySwingContacts` has live-only coverage: the swept bone segments go through the embodiment
  seam, and the substrate double records rather than sweeps, so the sub-step batching is proven in
  the arena and nowhere else.

*Acceptance:* a real `+attack` press swings a held melee weapon and fires a held firearm — the
melee blow committing inside its clip's **authored contact window** and the shot on its own
authored sequence event; an NPC's ambient behaviour and a scripted beat both reach a pose through
the intent seam, and `m_iszCustomMove` plays over a travelling body; a struck body is knocked back
on the direction its blow states, a launched body flies and lands, and a killed body dies and
reaches the handoff (what the handoff hands to is PHYS1's).
*Deps:* LIFE2, LIFE3, LIFE4; `docs/vtmb/combat-and-damage.md` (its open joins are numeric and
gate 13.3, not this rung).

### LIFE6 The first-person viewmodel

A separate body, not a camera mode. Two halves, landing in order:

**The corpus (pipeline).** Export and bake one deterministic, patch-first manifest for both
first-person roles: exactly **21** models under `models/hands/**` (active and repeated
`M_Hands`/`F_Hands` clandoc values plus the script-only male/female Tremere `_shield` swaps)
and exactly **17** packed weapon viewmodels (the 12 accepted firearm geometries plus grenade,
lockpick-reference and three Discipline models); each row's source key, normalized stem, role,
skeleton signature, attachments, sequences and events, with item `viewmodel`, `anim_prefix`,
`camera_class` and `reload_single` joins retained as provenance. Package layout is a contract:
`/ElysiumBaked/Characters/Viewmodels/Hands/<stem>/SK_<stem>` and
`.../Viewmodels/Weapons/<stem>/SK_<stem>`, one compatible skeleton per identical hierarchy,
case-folded sorted iteration so the same corpus produces the same packages on every run. The
exporter owns cache fingerprints; a changed or removed manifest row invalidates its own
products and a pre-save stale sweep below the two viewmodel roots removes renamed/deleted rows.
Verification resolves every manifest package, checks mesh/skeleton/sequence counts and events
against the container, rejects duplicate paths, asserts the 21/2/17/12 census. Missing authored
references stay diagnostics (`v_gangrel_fem_hands.mdl` reported dangling, not repaired).
Outputs remain game-derived and gitignored.

**The body (runtime).** The hands and weapon render as **two skeletal components** — not
merged, no socket attach — each consuming one semantic animation intent, resolving its matching
family sequence, evaluating independently in the same camera-root space with authored
`Camera01` as bone 0 and bind frames as the alignment contract. Viewmodel projection is
independent of player FOV: `t = tan(viewmodel_fov·π/360)`, scales `1/t` and `aspect/t`, clip
range 1..28400 in the recovered space, 4:3 or 16:9 under the widescreen/anamorphic setting; the
11.13d view projection owns the local-body/viewmodel visibility decision, and a hidden view
suppresses both submissions without destroying components or resetting sequence state. Server
sequence events are timing carriers into weapon mode dispatch — weapon logic commits the
shot/reload transaction; client viewmodel events own muzzle flash and shell presentation only.
Tremere shield scripts swap the hands-role model to the baked `_shield` variant. **Melee has no
first-person model, measured** — when retained first person suppresses retail's melee/
`force_3rd` camera move, the presenter renders no hands and no weapon; nothing is synthesized.
**Scope — owner call, made: ranged only**; lockpick and Discipline viewmodels ride the same
machinery, deferred rather than designed out.

*Acceptance is presentation-only:* test intents drive draw, idle, fire, dry-fire, ordinary
reload and the M37 begin/per-shell/complete phases on both components for every accepted
firearm family, proving clan/shield selection, matching sequence/cycle, `Camera01`/right-hand
alignment, both FOV/aspect policies, visibility suppression/resume and the empty melee view —
without spending ammunition, creating a projectile or originating a VtMB event. *Deps:* LIFE2
(catalog), LIFE5 (event carrier for real fire timing), 11.13d. Recovered contracts: `docs/vtmb/animation_and_movers.md`,
`docs/vtmb/camera-view-modes.md`.

### LIFE7 The cinematic path and gestures

Choreographed playback migrates from the sequence player's absolute-time seek to a montage
position. **Deliberately last among the mechanisms**: the theatre is the proven ground and
stays on its verified seek path until the stack under it is established. Owns the
gesture/sequence un-collapse — a gesture is an overlay in the same four-slot
`CBaseAnimatingOverlay` array as an `ACT_*_LAYER_*` selection, composed at a flat `0.1` weight,
rate-scaled at start then free-running and auto-killed at its end
(`docs/vtmb/animation_and_movers.md` A.4c), so a second scene-time-pinned player
would reproduce timing retail does not have; whether the composite wants a montage slot or the
weapon layers' overlay treatment is this rung's design call, as is whether a scene's clip
changes regain a crossfade (the divergence CCC9 recorded).

**Paired actions** land here — owner call, made. A paired action is two bodies posed against each
other, which is the cinematic path's problem rather than the reaction channel's: the rung owes the
recovered role/size/side variant arithmetic (policy over LIFE2's catalog — it adds no graph
machinery) and the claim shape that keeps both participants' base channels held for one
transaction. The feed transaction is the shipped consumer.

- **The choreo-scene rewire (hard slice).** The scene pipeline is broken end-to-end and has not
  been exercised since the LIFE programme started: NPCs do not load at their scene marks, a
  playing scene reproduces with its cast outside the camera, and the camera and trigger systems
  have both changed underneath the wiring. The slice re-wires the whole path — actor placement
  at marks, the scene camera, the triggers that start scenes — before any montage-migration work
  builds on it. A static gap analysis of the whole path — the seam inventory, the divergence
  timeline, and the ranked gap map — is at
  <https://claude.ai/code/artifact/c4cd94a9-eb8c-4ce8-bd56-fea738b43ca6>. (The arbitration slot's claim lifecycle is proven independently of this; a
  scene's Scene-band claims submit and release correctly even while the staging is broken.)
  Two known claim-lifecycle edges ride along for this slice: `ReleaseActorClips` early-returns
  when `elysium.SceneActors` is 0, so toggling that cvar mid-scene leaks the scene's claim and
  parks the body's base channel; and a body destroyed without a stop leaves its inert
  `CinematicClaims` entry unswept.
- **The composition weights are recovered; what is left is the confirmation pass.** Both scalars
  are constants in the DLL: an autolayer composes at a literal `1.0` against a binary per-bone
  mask, and a choreographed `gesture` composes at `SetLayer`'s literal `0.1`
  (`docs/vtmb/animation_and_movers.md` §A.3, §A.4c). The runtime already matches the autolayer
  value, so no stand-in remains there. The optional confirmation is an analysis pass over the
  **banked** captures rather than a new hook — the combine is closed arithmetic, so a host's
  decoded local, its layer's decoded local and the composed local determine the scalar per bone,
  and `sm_hub_1` carries all three for the `move_and_ranged` banks. The gesture value has no banked
  witness at all (all five databases are `sp_theatre`), and the one shipped scene that composes a
  gesture over a running sequence is `prince_beckett_dialog` on `la_ventruetower_1`. The `0.1`
  is what the un-collapse is designed against: a gesture leans the base pose 10%, it does not
  replace it.
- **The `Prince_Escort_Male` cluster**, now mostly attributed. The characterization report
  (`golden/golden_theater/cap5.9-characterization-*.json`) puts the bank at 693 over-band records
  on the current evaluator, 351 `brujah_Male_Armor_0` and 342 `Lacroix`. Applying the retail
  hemisphere-corrected quaternion decode collapses that to **18**, and the same correction zeroes
  `move_and_ranged`'s separate 1,318-record residual — so the dominant cause is a corpus-wide
  decode defect, not a cinematic-bank one. The include route, the remap table and the static bind
  are all ruled out directly (zero route disagreements; bind error ~2.4e-5 with no bone over band),
  and `bonerename` selects the bank rather than binding bones inside it. What is left is the one
  actor-discriminating signal: the `bone_name` join fails 3,599/3,599 on Lacroix against 4/3,599 on
  brujah, alongside a 72-vs-78 bone-count asymmetry. The cheapest next measurement is an offline
  bone-name diff from the existing capture database — no new capture, no bake. The doc's earlier
  1,061/65% figures do not reproduce from the banked snapshot and are withdrawn.

*Deps:* LIFE5's montage mechanism; the theatre plan's 12.x rows consume the result.

### LIFE8 Alive — played acceptance

The programme's finish line, played by the owner from real input on a real map (map priority
order): the player walks, runs and sneaks among a cast travelling at authored speeds; drawing a
weapon changes the stance and puts geometry in the hand; a melee swing tracks; a shot fires on
its sequence event; a struck NPC flinches by direction, a blocked attacker reacts, a killed
body dies and hands off; a scripted beat and an ambient behaviour both pose through the intent
seam. Every request in the session resolves or names its miss in the selection record — a quiet
log is not acceptance, and no dev shortcut is acceptance evidence (playable-path rule 3). This
row also owns the **capture-tooling trim**: delete probes, readers, fixtures and dependencies
no retained evidence path or active investigation uses — the community-decoder harnesses stay,
as `docs/vtmb/procedural_bones.md` and `docs/vtmb/vtmb-animation-reverse-engineering.md` rest
on measurements only their runs produce.

### LIFE9 Secondary-motion calibration — parked

**Parked: presentation polish behind the graphics freeze (playable-path rule 2); revisit at the
thaw.** Both retail mechanisms are located, solved and fully accounted (28-byte bone-chain
records at `MDLHeader` +396/+400; StudioRender particle cloth behind header flag `0x400`) —
facts and confidence ledger: `docs/vtmb/secondary_motion.md`. The parked remainder:
game-independent numeric replays of both decoded solves; controlled retail series for the bone
chain; for cloth, the smallest post-skin particle capture around the StudioRender step/reset
boundary, then Sheriff/Jeanette comparison including delta, wind and collision inputs. The
AnimDynamics calibration slice stays deliberately small — female Malkavian armour 0 and
Jeanette's two hair routes only — fitting native gravity/damping/spring/cone against the
controlled series, labelled an Unreal presentation approximation unless the numeric replay
itself matches. This is the one parked item that would need **new** live capture; that renewed
scope is its own owner call at the thaw.

### Open owner calls

Movement-orientation/strafing settings so `move_yaw` resolves off the neutral cell in ordinary
play; sync-group phase matching between gaits (retail's crossfades are phase-independent).
Each lands as a recorded divergence in its owning doc when made.

### Divergences to record when settled

Each lands beside the faithful behaviour in its owning doc: layer blend-in/out times (ours —
the record carries no ramp) → `docs/vtmb/animation_and_movers.md`; blend-space interpolation
replacing the authored cell selection, the bake-then-blend residual across a crossfade (bounded
≤10° measured), and an overlay/additive composed over a host that does not declare it →
`docs/architecture/animation-architecture.md`.
