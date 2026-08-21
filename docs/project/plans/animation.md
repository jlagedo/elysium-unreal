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
gate currently stands in for, the ragdoll physics-asset bake, and the owner-played acceptance
sweep. **Owns the pose, not the number**: lethality, soak, the damage roll and the health commit
are 13.3's; this rung consumes their outcome.

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
cells the vocabulary already carries. The two named omissions of the gate stay open and stay
reported: the hit-buildup counter, whose per-victim versus per-attacker/victim-pair accounting is
still an open RE question, and the second, unidentified template predicate.

**The ragdoll handoff is a bake-side follow-up.** The death transaction hands off by holding the
final pose because no baked mesh carries a physics asset. A true ragdoll needs the character bake
to generate one per body family — bodies, constraints and a collision profile derived from the
baked skeleton — with the runtime handoff switching to simulation at the same point it now freezes.
Until then the held pose is the recorded stand-in, not the design.

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
on the direction its blow states, a launched body flies and lands, and a killed body dies and hands
off to the ragdoll.
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
`CBaseAnimatingOverlay` array as an `ACT_*_LAYER_*` selection, rate-scaled at start then
free-running (`docs/vtmb/animation_and_movers.md` A.4c), so a second scene-time-pinned player
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
