# Gameplay plan — open-task specifications

Specifications for **open** gameplay tasks (interaction P4, dialogue/persistence P9, tutorial
mechanics P13, the long tail). Status lives solely in `docs/project/roadmap.md`; a task that
lands is deleted here. No status marks in this file. Governing design:
`docs/architecture/gameplay-systems-architecture.md`.

### 4.8 Rotating/linear/elevator family — remaining

`func_elevator` and `prop_button` are landed. Still open: `func_rotating` (spin-up/down,
hurt-touch), `func_movelinear`, keyframed movers, and **the mover-push remainder**: movers are
`MOVETYPE_PUSH` and displace what they touch from their own side. `FElysiumMoverBase` sweeps
instead, and Chaos resolving that sweep already shoves the pawn out of a closing door's arc
(measured) — nothing tunnels, but `dmg` is not dealt, `OnBlockedClosing` does not fire unless
the sweep is fully blocked, and the displacement is a physics artifact. The remainder targets
the **observable, not the mechanism** (S12): a blocked closing door deals `dmg`, fires
`OnBlockedClosing`, and displaces the pawn from its own side — through the engine's
sweep/displacement response, never a `PhysicsPushEntity` port, residual difference enumerated
in the owning doc. *Deps:* 4.1.

### 4.10 `game_sign` / `prop_sign` — remaining

The sign leaves and shared parser cover the exclusive `prop_sign` session, `OnReadBegin`,
`definition_file`, first-true dependency redirect and `OnReadEnd`. Still open:
`NewspaperData`/multi-column, `ClientCommand`, `fade_out` linger, `pause` semantics +
`spawnflags 5` undecoded; real `.fnt`-role type is 8.8.
*Acceptance:* `+use` on `sign_chopshop_upstairs` reads "password: chopshop"; a dispatch-wrapper
sign picks its variant from `G`. *Deps:* 4.4, 5.2 for the redirect.

### 4.11 Close the trigger and `+use`-prop I/O gaps RE35 exposed — remaining

`CPropSwitch` is a sequence player whose `OnActivate` fires on clip end. Still open: its
`soundgroup` on/off events and a runtime reset hook that can apply `reset_state`. Terminal
interaction is owned separately by 13.4. → `docs/vtmb/entity_io.md`. *Deps:* 4.1, 8.3.

### 4.12 Door faithfulness gaps — remaining

The locked-use matrix (no `OnLockedUse` on a door — that output is `prop_switch`'s; silent `Open`,
locked-only `+use` sound), the unconditional `+use` `ResolveToggleStateFromTransform` resync, and
the activator-relative swing (`OpenAwayFromEntity` plus the `bResolveSwing`/`SF_DOOR_ONEWAY` gates)
are landed and recorded in `docs/vtmb/animation_and_movers.md` B and `entity_io.md`. What remains is
recovered-to-retail reproduce work under P4's RE-first rule:

- **The full `CBaseDoor::Use` guard chain.** `+use` still routes through `InputToggle` rather than
  retail's ordered chain: the `noopenwanted` refusal, the mid-motion self-heal step, and the
  `{AT_BOTTOM, AT_TOP} ∪ NO_AUTO_RETURN` admission set (a `GOING_*` non-return door is a silent
  no-op in retail, a reverse here) are unmodelled.
- **`MoveDone` dispatches on the mutable `ToggleState`** instead of a move-start arrival callback,
  so a locked door caught mid-close by a resync can drop a single `OnFullyClosed`. The durable fix
  makes arrival independent of the live state.
- **Input-level outputs and admission.** Retail fires `OnOpen` at the `Open` input and again in
  `DoorGoUp`, and `Open`/`Close`/`Toggle` carry their own admission gates; Elysium's differ. The
  three handlers are now decompiled (`FUN_100f0170`/`FUN_100f00a0`/`FUN_100f0210`): `Open` and
  `Toggle` gate on `IsUseRefused`, **`Close` carries no lock test**, admission is
  `!= AT_TOP` / `!= AT_BOTTOM` so each also runs from the opposite in-flight state, and `Toggle`
  reaches the motion helpers directly so only `Open`/`Close` double-fire.
  → `animation_and_movers.md` B.4.
- **Mover sound emission points.** Retail emits `close` at **arrival** (`DoorHitBottom`), not at
  motion start, and starts no loop and stops none — `swing` is one event like the others, so any
  looping is the soundgroup's. Elysium plays `close` at motion start and owns the loop in code;
  both differ — owner-adjudicated against AUD2. → `animation_and_movers.md` B.4.3.
- **The blocked family.** Retail's `Blocked` damages in **both** directions, self-reverses only
  behind `CRotDoor`'s re-entrancy byte (so a plain `func_door` never reverses), and **synchronises**
  its targetname group to its own direction rather than reversing it; `StartBlocked`/`EndBlocked`
  carry the `OnBlocked*`/`OnUnblocked*` edges with different activators, and `CRotDoor::Blocked`
  adds a stuck detector. All recorded in `animation_and_movers.md` B.4.4. The any-entity blocker
  filter is NPC-pending (P13, and the 10.7 door-obstruction reaction). **Blocking constraint:** a
  pure rotation is never swept — `UPrimitiveComponent::MoveComponentImpl` skips the sweep on a zero
  translation delta — so `OnMoveBlocked` is unreachable for a rotating leaf and none of this is
  observable there until a rotating-sweep mechanism exists.
- **Held on a live-retail capture**: the swing **blocked-latch inversion**. The flip itself and its
  arming predicate are recovered; what is not statically recoverable is the *identity* of the
  blocker's `+0x98`, zero-initialised in the `CBaseEntity` constructor with no setter or accessor in
  the image. The adjacent `+0x94` is the `CAI_BaseNPC` pointer, which narrows `+0x98` to the same
  cached derived-type family without naming it. Capture which blocker classes set it.
  (The **mid-motion self-heal** reissued-move arguments are **closed**: disassembly at
  `0x100efdc0`/`0x100efddf` shows `DoorGoDown(1)` and `DoorGoUp(1,1)` — both reissues propagate to
  the linked leaf and the open one re-resolves its swing.)
- **Investigate the runtime visible rotation.** A live session showed a baked `func_door_rotating`
  firing `OnOpen` and the swing sound while its body did not visibly rotate on screen; the substrate
  state/collision cycle is correct, so this is a map/visual-body concern — whether the baked brush
  body follows the mover transform. Confirm whether 4.3's brush-travel holds for a rotating leaf at
  runtime. → `docs/vtmb/animation_and_movers.md` B. *Deps:* 4.1, 4.3.

### 9.1 `.dlg` parser + dlgexpr — remaining

The remaining fidelity gap is retail `CDialog::GetStartingLine`: scan starting-condition
sentinels in physical file order, evaluate against live player/NPC/`G`, take the first passing
valid link, then honor `usescript`/line-1/first-stored-line fallbacks. Acceptance includes
Jack's overlapping and shadowed conditions plus line-action → `OnDialogEnd` ordering.
→ `docs/vtmb/game_runtime.md`.

### 9.2 Conversation UI — remaining

The UI slice landed. Remaining: presentation completion (speaker/emotion cues, the 8.10 subtitle
path), and live physical-device acceptance. Acceptance includes `(Auto-Link)`/`(Auto-End)` as
hidden control rows: run the automatic row action before following its link and never publish the
marker as a player response. Content is reproduced verbatim; presentation modernizes. The line
audio, the subtitle publish and the retained-NPC-subtitle rule are AUD3's (`plans/audio.md`).
*Deps:* 9.1, AUD3, 8.6.

### 9.3 Level-script execution — remaining

The core landed (B2, 9.3b, 9.3c). The character-method fill is delegated to backing systems:
inventory natives are 9.8's; feats/stats were 9.4's; disposition/camera/barter arrive with
their domains. `SquadSeesPlayer` stays a stub (no shipped caller). Remaining blocked on the
inventory follow-up.

### 9.8 Inventory & items — remaining

The ownership, loose-pickup, explicit loot-container session and CommonUI transfer panel form the
loot core. **Remaining:** player drop, `StartBarter` plus the buy/sell barter UI (8.6),
`trigger_inventory_check`, and the `TravelsWithPlayer()` absent-set half; buy/sell pricing is
9.10's. The plain `item_container` lid mover remains, and the tutorial lockpick's attempt HUD
remains a logged stub; the lid cue and the animated-container `soundgroup` path are AUD2's.
→ `docs/vtmb/inventory.md`, `docs/architecture/gameplay-systems-architecture.md`. *Deps:* 9.4.

### 9.9 NPC disposition & reactions — remaining

The tutorial talk/feed presentation slice and the RPG reaction-score calculator over
`reaction.txt`/`reactions000.txt` landed. **Not finished:** the dialogue consumer that reads the
score, the `React` Character method, loud-expression policy and broader expression/gesture
semantics. The three social domains stay independent (K4): the reaction score is consumed by
dialogue and never by combat targeting. *Deps:* B4.

### 9.10 Economy

250 calls: `MoneyAdd`/`MoneyRemove` (INTEGER inputs on the combat character) +
`CurrentMoney`/`SetMoney`. One integer on the sheet plus vendor `worth` when 9.8's barter
lands; the retail pricing formula is an open join — until closed the service carries a marked
placeholder behind the same seam.

### 10.7 Long tail *(parked; promote to tasks when reached)*

Combat AI, perception and the schedule graph are promoted — they are 13.5.
The **door-obstruction reaction**'s decision is written and tested but has no
producer for either obstruction source; it needs, in order: actor→entity resolution (a reverse
body→`FElysiumEntity` lookup, reusable well beyond doors), `COND_HIT_BY_DOOR` from
`FElysiumDoorBase::OnMoveBlocked` past its player-only check plus a door-blocks-NPC-path
producer, and a hint-node reader over `info_node_cover_*` (342/81/36 across the exports) with
`FElysiumInterestingPlace`'s claim-release shape. Unreachable on the tutorial path. Blocked on
recovery, not effort: the follower controller (unrecovered `follower_type` radii) and
return-to-initial (no producer). Facts: `docs/vtmb/npc-ai-reverse-engineering.md`. Also parked here:
ragdoll/IK; an `.ents` cook-cache if parse time bites; the
lump-8 bake contingency (Lumen Lite noted as the cheaper alternative); retail `.sav` import
(RE7, non-goal). **vdata-driven systems** promote from here as reached: wider
hacking/email/screensaver completion, vendors, impact FX, per-category sound schemes, minor UI
content tables. A comment-phrasing pass is owed where "transcription" understates the
analysis → spec → implementation separation (`ElysiumStance.cpp`, `ElysiumSoundScheme.{h,cpp}`,
`ElysiumSkeletalBuild.cpp`, `ElysiumMapActor.cpp`); and `Elysium.Content.MapSnapshot`'s
failure is a fixture gap (unticked baseline NPCs carry no `nextthink`), fixed in the fixture.

### 13.1 Stealth — remaining

The light/Sneaking target surface, `trigger_stealth_mod`'s balanced overlap contributions and
13.5's consumption of the published scalars are built. Open: the **stealth-kill transaction** —
`vdata/system/StealthKillRules.txt`, deaf arc / approach depth, grapple mode 3 on qualified
targets (recovered under RE50, unimplemented) — and 8.9's committed observer snapshot.
Faithful transaction: `docs/vtmb/stealth.md`. *Acceptance:* from real input, the tutorial's
ordinary stealth and stealth-kill lessons complete as retail. *Deps:* 8.9, 11.10, 13.5.

### 13.2 Disciplines — remaining

Activation/deactivation over `vdata/disciplinetgt_*`, blood cost, the queue-owned expiry, the
targeted cast transaction, the Bloodshield/Fortitude/Potence joins into 13.3's commit, the
`HitInfo` AI-schedule channel into 13.5's kernel and the `ClearActiveDisciplines` teardown are
built. Open joins, each a named refusal in the runtime: Celerity's native time/movement consumer
and level table, Obfuscate's visibility/break/damage-bonus matrix, Protean's per-rank consumers,
the frenzy family and `SetScriptedDiscipline` pending inputs, and the client-disable
presentation. Remaining RE: `docs/vtmb/disciplines.md`. *Acceptance:* the tutorial's discipline
lesson completes as retail. *Deps:* 13.3.

### 13.3 Firearms & melee basics — remaining

The damage spine, the one typed health commit and the weapon controller with its two-half attack
transaction are built. Open joins, each a marked seam rather than a redesign: the step-9 template
filters and the **word-15 commit** (accumulated onto the descriptor, never multiplied in); the
ranged **spread cone's interpolation input** between `SpreadAngle` and `SpreadAngleMax`; the
**melee contact instant**, scheduled on the one queue until the character bake carries authored
animation events for a real notify to deliver the same input; and `SkillRequirement`'s consumer.
→ `docs/vtmb/combat-and-damage.md`. *Acceptance:* the tutorial's range + melee lessons complete
as retail. *Deps:* 9.8, 11.10.

### 13.4 Computer terminals & tutorial hacking

The parser, the plain-C++ terminal state machine, `FElysiumTerminal`'s exclusive explicit-use
session, the authoritative non-bindable `hackcmd` path, Function ordering on the one queue and the
console's projection through the model's exact `screen` material slot are built.

**Remaining.** Make Escape and Quit end the session from every input mode and during a skill
attempt, rather than reaching the command router — retail escapes a password prompt through the use
dispatcher, not through `AcceptCmd`. Make a content-load failure a named error instead of a
permanently unusable prop. Derive the screen basis from the model's `screen`/`screen_axis`
attachments and use it for both the recovered `0.7` facing gate and a fixed `Focus` camera request
framed to keep the bezel and part of the prop in shot; no offline screen-metadata export is needed.
Give the screensaver ownership of the projection surface so an idle terminal is live before its
first session, reproducing the recovered random-cell placement on `ss_start`/`ss_delay`. Add the CRT
screen material and the monospace type tokens, and the email state machine including
`global_email` reconciliation. The four authority-side cues over the exported `old_computer` group
and the local keystroke click are AUD2's; this task raises them. `autodelete` is inert in retail and is not implemented. The
controller's semantic action palette and virtual text entry reach the same handler as keyboard line
editing; VtMB's terminal font/VGUI/512×512 raster are not reused. *Design:*
`docs/architecture/computer-terminal-architecture.md`; behavior: `docs/vtmb/computer-terminals.md`.
*Acceptance:* from real input, keyboard and gamepad focus `tuthack`, enter `Safe` by password or
Hacking bypass, execute Unlock, enqueue `OnTrigger0`, reveal/unlock the safe through the authored
map wires, quit, restore the exact
previous camera; the grid stays readable inside the bezel at 1080p/1440p/4K. *Deps:* 4.11, AUD2, 9.6,
11.4–11.8.

### 13.5 Combat AI — remaining

The sound-event bus, senses and stimulus memory, the conditions layer, the ideal-state pass, the
enemy transaction, the alert/combat schedule families, the loadout and `aiscripted_schedule` are
built over the schedule kernel and the body-owner arbiter. Open: the **flinch and reaction action
families** on the animation side, which are LIFE5's; `COND_HIT_BY_DOOR`'s producer and the
door-obstruction retreat's consumers, which stay in 10.7; and the `Prone` state, a named refusal
until a producer is recovered. The **footstep hearing producer** — the one bus category with
nothing raising it, the seam marked in `Substrate/ElysiumPlayerEntity.cpp` — is AUD2's; this task
is its consumer. Facts: `docs/vtmb/npc-ai-reverse-engineering.md`.
*Acceptance:* the tutorial's hostile beats run from real producers — authored
`OnFoundPlayer`/`OnDamaged` consequences fire as wired and the combat lessons' opponents fight
and die as retail. *Deps:* 11.10.

## The physics substrate (PHYS)

One owner for everything that touches a simulated body — the prop, the corpse, the carried chair
and the explosion kick. Design: `docs/architecture/physics-architecture.md`. Facts:
`docs/vtmb/phy_vphysics.md` (the container and the ragdoll rig) and
`docs/vtmb/physics-interaction.md` (what simulates, the physics hands, the placed surface). The
governing rule is that design's owner call: **VtMB owns the rules, Chaos owns the solve.**

### PHYS1 The ragdoll rig — export, bake, handoff

**The export.** `phy.py` gains a second product from the same parse: a `RAGD` chunk in each
character's `.eskm`, carrying bone-named solids (parent, transform, mass, damping, rotdamping,
inertia, massbias, surfaceprop, hull range) and one constraint record per `ragdollconstraint`
(parent/child solid index, three axes of min/max/friction). It is a chunk and not a loose sidecar
because the payload is per-model skeletal data addressed by bone name, which is what `SKEL`,
`DYNM`, `BDYN` and `MASK` already are. 324 models carry a rig; 289 of them are one 15-solid,
14-constraint humanoid shape covering every ordinary NPC and all 58 PC bodies. Bone names fold
through the same rule the bank baseline uses, and a name that does not resolve against the baked
skeleton is a hard export error.

**Two calibrations gate it, and both are settled the way `(x, -z, -y)` was** — by scoring candidate
mappings against evidence the corpus already carries, over all 289 canonical rigs, and taking the
winner by margin rather than by argument:

- the **solid transform frame**. A ragdoll `.phy` carries two frames: hull vertices in IVP metres
  (settled) and `origin`/`angles` in Source units and Euler degrees (not). Score against each
  model's own bind-pose bone transforms.
- the **constraint axis identity**. Which of VtMB's x/y/z is Unreal's twist, swing1 and swing2 is
  not stated by the data. Score by settled pose under gravity; a wrong assignment reads as a knee
  bending sideways.

Until each is settled the export **fails rather than guesses**.

**The bake.** `bake_characters.py` gains one stage between the mesh and the clips: a
`UPhysicsAsset` assigned to the baked `USkeletalMesh`. One `USkeletalBodySetup` per solid, with the
authored convex hulls as `AggGeom.ConvexElems` under the same exactness rule the prop bake proved,
the authored mass as a body-instance override, `damping`/`rotdamping` as linear/angular damping,
and `surfaceprop` through the existing surface-property table. One `UPhysicsConstraintTemplate` per
constraint, linear locked, angular limited — **asymmetry resolved by half-range limit plus a
midpoint-biased child frame**, never by taking the larger magnitude; an all-zero constraint is a
weld, not a zero-width limit. `massbias` is carried and unconsumed until a body needs it. Keyed in
the bake cache off the `RAGD` bytes.

**The handoff.** `StartBodyRagdoll` needs no change and simply stops returning false; the
`HoldBodyFinalPose` stand-in becomes what it was always described as, the fallback for a body with
no rig. Two behaviours land with it:

- **the death impulse**, retiring the recorded "no impulse" divergence. The force envelope is fully
  recovered in `docs/vtmb/combat-and-damage.md` — the damage force or its synthesised replacement,
  plus absolute velocity, plus the physics-object term, clamped to `50000.0` — and is applied at
  the hit bone, or at `Bip01 Spine2` when the hit bone is unknown, converted through the Source
  impulse unit.
- **the interaction volume stays at the death origin.** Retail's corpse entity *is* the dying NPC,
  frozen non-solid at the death spot while only the drawn body slides; the use/feed/loot anchor
  therefore does not follow the pelvis. Making it follow is a Feel divergence and needs an explicit
  owner call before it is written.

*Acceptance:* a killed NPC falls under simulation from the pose its death program left, driven off
the killing blow's own force, and comes to rest without a limb inverting; its corpse remains
feedable and lootable where it died. *Deps:* LIFE5's death transaction.

### PHYS2 The physics hands

VtMB's object handling is HL2's gravity-gun code wired to the **use key**, granted to the player as
a hidden inventory item named *Hands* (`weapon_physcannon`). The item record stays authored data —
inventory and the criminal-law check read it by name — and the **grab state does not live on it**.

**The service.** `FElysiumPhysicsHands`, one per player, owned by `FElysiumEntityWorld` and saved
with the player. `TryAcquire` runs inside `UpdatePlayerInteraction` **before** the ordinary focus
query, reproducing retail's order: an open interactive-use session, then the grab candidate, then
`FindUseEntity`.

**The search** is a new embodiment query, `QueryPhysicsGrab`, separate from `QueryPlayerUse` for
the same reason the B6 feed search is separate — a different retail shape with a different mask. In
order: a ray of `physcannon_tracelength` (80 units = **203.2 cm**); on a miss, a hull trace of
±4 units (**±10.16 cm**) along the same segment; then a cone at `physcannon_cone` 0.97. Geometry
only; eligibility stays on the leaf as `FElysiumPhysProp::CanPlayerCarry` — simulating body, not
the player's ground entity, summed mass under the limit, every bounding-box axis under the size
limit — plus the player-side water gate (`m_nWaterLevel < 2`).

**Two constants are open and neither is guessed.** The mass and size limits are float arguments the
decompiler dropped from the eligibility body; recovering them means reading the asm at
`0x10411160`. Until then mass uses `physcannon_maxmass` (250 kg) and the **size test fails loudly**
rather than admitting on an invented constant.

**The carry** rides the existing `+use` session: `CanPlayerFocus` admits, `BeginPlayerUse` returns
`Started(WhileHeld)`, `EndPlayerUse(Released)` releases. `BeginBodyCarry`/`UpdateBodyCarry`/
`EndBodyCarry` on the embodiment are a `UPhysicsHandleComponent` on the pawn — Unreal's own PD
constraint to a target transform, which is the level Source's shadow controller should be
reproduced at. The mass and angular-damping overrides applied while held are restored on release,
as retail's saved arrays do. Throw force is `player_throwforce` 1000, converted through the same
Source impulse unit; that conversion is derived from the unit convention rather than measured, and
a visibly wrong throw arc is the cheap test that it is right.

**The cursor already exists.** `use_icons.json` slot 9 is `PhysicsHand` and slot 1 is `CarryBody`;
the hands publish the icon into `FElysiumInteractionView`, which today only entities produce.

*Acceptance:* in `sp_tutorial_1`, from real input, the office chair (`chairoffice`, 1.00 kg) can be
picked up, carried and dropped, and a sardine can (0.30 kg) can be picked up and thrown; the stool
(25 kg) and the crate (100 kg) behave as retail does; nothing is grabbable underwater.
*Deps:* 4.4, PHYS1's impulse seam.

### PHYS3 The rest of the physics world

`func_physbox` (179 placements in 17 maps) as `FElysiumPhysProp`'s brush twin — the entity's own
`.ents` convex `hulls` as the body, `func_breakable`'s damage surface. The `phys_*` constraint
family (`phys_ballsocket` 84, `phys_constraint` 78, `phys_convert` 56, `phys_thruster` 3,
`phys_constraintsystem` 1, `phys_animlink` 1; `phys_hinge` 43 is landed) over
`UPhysicsConstraintComponent` between two entities' `GetAttachBody()`; `phys_convert` is a
transaction on its target's leaf rather than a constraint. `env_physimpact` (130) and
`env_physexplosion` (61) stop being stubs and become the first non-player consumers of
`AddBodyImpulse` / `AddRadialImpulse`, with their presentation still owned by
`docs/architecture/effects-architecture.md`.

`prop_ragdoll` (52 placements in 14 maps — the Warrens corpses, the Malkavian mansion stalkers, the
Ventrue Tower aftermath) is the one placed server ragdoll and lands here, seeded from its
`demo_sequence` pose onto the PHYS1 physics asset. `physics_prop_ragdoll`, `prop_ragdoll_attached`
and `prop_ragdoll_special` register in VtMB and are placed in no map; they are out of scope, and a
map that places one is a new task rather than a gap here.

*Acceptance:* `sp_tutorial_1` and one Warrens map load with every physics classname resolved and no
unhandled-class warning; a wired `env_physexplosion` scatters the props in its radius.
*Deps:* PHYS2's seam.
