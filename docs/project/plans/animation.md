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

### LIFE3 One resolver for the whole cast

The translation tables, the graph state and the ownership of the base pose are behind this rung:
`TranslateActivity` walks the committed weapon ladders, the two `CBasePlayer` rows and the
recovered NPC class bodies in their witnessed orders, availability-probing each rung against the
body's own vocabulary; `GraphState` is projected once in the resolver's step 6 and read by the anim
instance, Cog, the `act_state` channel and the MCP surface rather than derived four times; a
locomotion publish takes the base pose back from a one-shot only while the body is locomoting; the
bind loads whatever asset the record names rather than only what the outcome calls `Resolved`; and
a request that binds nothing reports once per `(stem, request, outcome)`.

What remains is the speed authority being one number, and evidence that says so.

- **Pose and speed come from the same activity.** `ResolveGaitSpeeds` keys stem, weapon, form and
  variant with `Source=Player` and no classname or `ActorState`, while the pose walks the NPC
  `+0x5dc` body — an idle human poses `ACT_WALK_RELAXED` and travels at the un-relaxed player fan.
  `GaitSpeedForSelection` then switches on `ActivityCode(Selection.ResolvedActivity)`, whose
  vocabulary is the unsuffixed literals only, so every translated name falls through to the
  resolve-time cell and the stride stops tracking direction. Resolve the gait with the same
  source, classname and state the pose uses, and classify it from the requested activity or
  `GraphState`.
- **The motor commands the cell the body is about to play.** `GaitSpeed` returns `Table->Forward()`
  and `MoveTo` sets one `MaxWalkSpeed`, while the posed fan is steered by realized `move_yaw`. A
  patrol turnaround plays a strafe cell at the forward speed. Command `SpeedAt(move_yaw)` per
  substep, or hold translation until facing is aligned. Scripted `Run` passes its gait kind so the
  push can re-derive it; scripted `Walk` and `Custom` keep the caller's number.
- **The trace carries the sample the driver ticked.** The writer's contract is the published sample
  and selection and nothing else; the cast harness calls `SampleLocomotion()`, which recomputes
  `FromCharacterMovement` after the driver already consumed one. The driver keeps the sample it
  classified and both producers read that.
- **No-slide is a predicate, not a printout.** `speed2d` and `act_stride` are independent columns
  compared only against the previous recording, at a 2.0 u/s tolerance, and a body that never moves
  resolves `ACT_IDLE` on every frame of a walk course. The check is intra-row: on moving frames,
  `|speed2d − act_stride|` and `|act_stride −` the body's authored forward cell are below a tight
  epsilon, peak speed clears the still cut, and the course's activity codes include the gait. This
  also settles whether the bar admits a commanded run — either `CommandedSpeed` is published on the
  cast sample, or the opening frames of every run request are walk-classified and the claim says so.
- **The harness fails loudly.** An input that does not arm its leaf, a course filter that matches
  nothing, a missing body, an empty write and a refused `EndFrame` all fail the run with a non-zero
  status; the output directory is cleared at run start so a stale recording cannot be read as this
  one. The default body is a named locomoting humanoid rather than the first baked stem, and the
  comparator checks the recorded body against the baseline's.
- **The coverage sweep carries class, weapon and state.** The 52/114/570 loops set stem, activity,
  source and the ladder flag only, so the committed tables' load-bearing work — availability and the
  armed/alert branch — is never entered and `GraphState` identity is never asserted. Drive the cast
  half with each body's authored classname, at least one equipped weapon and `ActorState` across
  Idle/Alert/Combat; require `GraphState == StateForActivity(requested)`; separate sequence-zero
  (no clip, poses nothing) from a fallback that named a clip. The glock claim asserts the resolved
  clip label on a real body, not only on the fixture.
- **The discrete key carries the actor classname.** The key is activity, stem, weapon, state and
  route. `ActorClassname` is what finds the `+0x5dc`/`+0x5e0` class bodies and is pushed onto the
  intent without being compared, so a first tick whose entity had not resolved keeps the wrong class
  body for the life of the request.
- **Gait qualifies on a real map.** `FElysiumCastRun` stands the combat arena in the stage world
  under `-nullrhi`; it is a regression instrument and not acceptance evidence (playable-path rule
  3). The gait bullet closes on one priority-map patrol with the selection record in the log.

*Acceptance:* on a priority map, a cast member walks a patrol at the cell its own tables author,
armed and unarmed, with the selection record in the log; the intra-row no-slide predicate holds
over that patrol and over a recorded scripted run; and the coverage sweep that says the cast poses
is driven with the classname, weapon and state the committed tables are keyed on. *Deps:* LIFE0,
LIFE2.

### LIFE4 Weapons in hands — the third-person wielded body

What a character holds, finished. The recovered contract — four item model roles, the equip
transaction and sex selection, the follow-attach bone copy, the seven unskinned prop bones, the
`anim_prefix` key, the shipped corpus — is `docs/vtmb/wielded_weapons.md`; the bake lane and
binding metadata are `docs/architecture/wielded-weapon-integration.md`; the corpus, masters and
`/ElysiumBaked/Items/DA_WieldModels` are on the mount. The runtime attachment exists in the
working tree (leader-pose follower, `(classname, sex)` resolution, the green-room `gr_wield`
lane); what remains is making it *true*:

- **Tracking through the prop bone.** With the prop-bone channels baked, verify the
  follower actually rides the animated prop bone / hand through locomotion and a swing. The
  observed defect: the weapon renders at a fixed offset while the body animates, and the
  install check reads 0.0000 because it compares transforms through the leader array — the same
  never-animated bone on both sides. No socket, no offset, no correction factor: a placement
  that needs one means the bake is wrong upstream, and the fix goes there.
- **An honest check.** The wield verification compares the *rendered* weapon mount against the
  wearer's hand over several frames of an animated base, not the leader array against itself;
  it reports drift as a failure with the frame and distance.
- **The equip funnels.** Parse `wieldmodel_m`/`wieldmodel_f` and `anim_prefix` onto
  `FElysiumItemDef`; attach on equip and detach on holster through
  `FElysiumWeapon::OnEquipped`/`OnHolstered` and `FElysiumInventory::SetActiveWeapon` — no
  second equip path. NPC bodies and the player body take one path; equip selects on the wielder
  through `IsMale` with no player-only branch.
- **Visibility.** Consume `FElysiumCameraView::bDrawWorldWeapon` (published, currently
  reader-less): suppress submission only — never destroy the attachment or clear a model to
  hide it.
- **The channel arbitration slot, before any layer is wired.** `FElysiumAnimationIntent` declares
  the channels and only `Base` is arbitrated; `BuildLocomotionIntent` always writes `Base` and the
  driver's `Tick` has no request queue, so an attack or aim request has nowhere to go but the base
  locomotion replacement. The driver takes a request slot the action families write, with a
  priority order that decides which channel owns the pose — and it replaces LIFE3's interim rule
  that a locomotion publish only spares a one-shot while the body is not locomoting. Nothing in
  this rung's attack or aim path lands before it.
- **Weapon-state animation.** Draw/holster, per-weapon idle/walk translation and the aim/attack
  layer families arm through the table-declared hosts (LIFE2's tables, LIFE0's host rule), so a
  drawn weapon changes how the body stands and moves, not only what the hand holds. This rung makes
  the standing-with-weapon call: either the player producer walks `PlayerGaitLadder()` with a live
  `CombatReady`/`Relaxed` query so a combat-ready stand requests `ACT_AIM`, or the committed 8-row
  ladder and the 17 compact codes are named beside `Classify` as this rung's, so two artifacts do
  not both read as the live selector. A corpus case pins the combat arm: a real combatant stem with
  a drawn glock and `ActorState == Combat` keeps its requested gait untranslated, then takes the
  weapon ladder only. `FormTag` rides the driver, the intent and the gait key with no writer; a row
  that reads it is pushed from `SetTranslationContext` in the same change. The translation hops the
  resolver walked — the class answer, the availability rung, the weapon rung — reach the selection
  record so a readout can show which rung fired.
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

Directional hit, knockback and death reactions, then weapon and interaction actions; each
family closes its own reachability slice. Every remaining producer moves onto the intent seam —
`PlayNpcActivity` stays a compatibility adapter until patrol and scripted travel cross over.
**Owns the pose, not the number**: lethality, soak, the damage roll and the health commit are
13.3's; this rung consumes their outcome.

Three seams open before the first family lands:

- **The cast chain is discriminated by body kind, not by producer.** `Source` is the producer, and
  an NPC's damage reaction and its patrol address the same body through different sources; the
  resolver forks the recovered cast chain on `Source == Npc`, so `Damage`, `Scene`, `Interaction`
  and `Debug` take `CBasePlayer`'s one-pass chain with no `+0x5dc`, no class/weapon alternation and
  no four-way probe. The fork moves to an explicit body-kind test and `Source` keeps arbitration and
  diagnostics; a `Source=Damage` intent on a human combatant still walks the class body and the
  probe.
- **`PlayNpcActivity` stops being a second resolver.** It calls `PickActivityClip` on the raw
  `ACT_*` — no weapon ladder, no class body, no availability probe, no selection record, no named
  miss — and patrol, scripted travel, ambient, schedule and the weapon path all reach it. Anything
  the driver already classifies goes through `Resolve` with classname, weapon and state; whatever
  is left of the adapter goes through the same call or is deleted.
- **A reaction is not an idle.** `StateForActivity` maps the locomotion slice plus `Unknown`,
  `Swim` and `Treadwater` onto eight states, so a hit, a melee attack or a scripted custom move
  projects to `Idle` and a resolved asset plays as an idle overlay. Each family added here extends
  the activity codes and the state projection in the same change, or rides a montage slot and
  leaves `GraphState` naming the held locomotion state.

Recovered resolver rules it adds (all policy over LIFE2's catalog — none adds graph machinery).
The class/weapon alternation and its four-way availability ladder are LIFE3's and already run;
what this rung adds over them is the combat state their predicates read — the reload chain among
them, whose start derives both end times from the *selected sequence's* duration over its rate
rather than the authored `ReloadTime`; the
restart rule that clears and restarts a repeated identical request; paired-action
role/size/side variant arithmetic; the **authored, not directional** blocked reaction (the
attacker plays the activity its own sequence descriptor stores, `ACT_BLOCKED_REACTION_RIGHT`
fallback — only the flinch is directional, `ACT_HIT_HEAD`/`ACT_HIT_TORSO` by `hit_yaw`); and
the `2COMBO` substitution keyed by base Brawl/Melee. There is no firearm stagger to build —
ranged capability `0x2000` does not satisfy the block gate `0x18000`. There is no transition
traversal to build either: `AdvanceToIdealActivity` reaches an ideal activity directly, because
the graph its `FindTransitionSequence` branch walks is unauthored on every shipped model
(`docs/vtmb/animation_and_movers.md` → "The transition graph is unauthored").

One montage-slot mechanism serves both `scripted_sequence`'s `m_iszIdle → m_iszPlay →
m_iszPostIdle` and an interesting place's enter/hold/leave segments — the same shape, built
together or there come to be two.

**The sequence-event carrier is built here.** Nothing in the bake emits a `UAnimNotify`, so a
VtMB sequence event reaches no Unreal notify and the ballistics trigger has nothing to ride —
an attack-layer event is what reaches `CWeaponRanged::Shot`, on 4–16-frame clips where a
sub-threshold node weight during a blend drops queued notifies. The bake and the guard test
land together; a guard before the carrier would assert against a synthetic notify and rot.

*Acceptance:* an NPC's ambient behaviour and a scripted beat both reach a pose through the
intent seam, and `m_iszCustomMove` plays over a travelling body; a struck body flinches on the
`hit_yaw` grid the hit direction selects, and a blocked attacker plays the reaction its own
sequence names; a weapon's attack-layer sequence event reaches the shot while the layer is
still blending in. *Deps:* LIFE2, LIFE3, LIFE4; `docs/vtmb/combat-and-damage.md` (its open
joins are numeric and gate 13.3, not this rung).

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
(catalog), LIFE5 (event carrier for real fire timing), 11.13d, RE42's remaining controlled
matrix for final verification. Recovered contracts: `docs/vtmb/animation_and_movers.md`,
`docs/vtmb/camera-view-modes.md`.

### LIFE7 The cinematic path and gestures

Choreographed playback migrates from the sequence player's absolute-time seek to a montage
position. **Deliberately last among the mechanisms**: the theatre is the proven ground and
stays on its verified seek path until the stack under it is established. Owns the
gesture/sequence un-collapse — a gesture is an overlay in the same four-slot
`CBaseAnimatingOverlay` array as an `ACT_*_LAYER_*` selection, rate-scaled at start then
free-running (`docs/vtmb/animation_and_movers.md` A.4c), so a second scene-time-pinned player
would reproduce timing retail does not have; whether the composite wants a montage slot or the
weapon layers' overlay treatment is this rung's design call, as is whether a scene's clip
changes regain a crossfade (the divergence CCC9 recorded).

- **The composition weight, measured.** The four-byte autolayer record carries no weight, ramp
  or flags; the scalar lives in the game DLL and only retail answers it. The move is an
  analysis pass over the **banked** captures, not a new hook: the combine is closed arithmetic,
  so a host's decoded local, its layer's decoded local and the composed local determine the
  scalar per bone, and a value consistent across the mask's bones measures it while confirming
  the combine. Acceptance is a coverage argument — the recipe exercises weapon draw, an aim
  transition and a sequence crossfade, and constancy counts only when witnessed over conditions
  that would have varied it. Until it lands, the runtime's `1.0` remains a named stand-in and
  the green room labels it as such; the measured value gates how a gesture reads against its
  base.
- **The `Prince_Escort_Male` cluster.** 1,061 banked records the include transform does not
  explain, 65% on one two-actor cinematic bank (351 `brujah_Male_Armor_0`, 342 `Lacroix`); the
  discriminating shape is the **actor**, not the bank or cell — same grid, same cell pair,
  different residual rates per actor — which is where diagnosis starts. A cinematic-bank
  defect, diagnosed here where the cinematic path lands.

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
