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

### 9.1 `.dlg` parser + dlgexpr — remaining

The remaining fidelity gap is retail `CDialog::GetStartingLine`: scan starting-condition
sentinels in physical file order, evaluate against live player/NPC/`G`, take the first passing
valid link, then honor `usescript`/line-1/first-stored-line fallbacks. Acceptance includes
Jack's overlapping and shadowed conditions plus line-action → `OnDialogEnd` ordering.
→ `docs/vtmb/game_runtime.md`.

### 9.2 Conversation UI + audio-by-path — remaining

The UI slice landed. Remaining: presentation completion (speaker/emotion cues, the 8.10 subtitle
path), and live physical-device acceptance. Acceptance includes `(Auto-Link)`/`(Auto-End)` as
hidden control rows: retain the preceding NPC subtitle through its voice turn, run the automatic
row action before following its link, and never publish the marker as a player response.
Content is reproduced verbatim; presentation modernizes. *Deps:* 9.1, 6.5, 6.6, 8.6.

### 9.3 Level-script execution — remaining

The core landed (B2, 9.3b, 9.3c). The character-method fill is delegated to backing systems:
inventory natives are 9.8's; feats/stats were 9.4's; disposition/camera/barter arrive with
their domains. `SquadSeesPlayer` stays a stub (no shipped caller). Remaining blocked on the
inventory follow-up.

### 9.8 Inventory & items — remaining

The ownership, loose-pickup, explicit loot-container session and CommonUI transfer panel form the
loot core. **Remaining:** player drop, `StartBarter` plus the buy/sell barter UI (8.6),
`trigger_inventory_check`, and the `TravelsWithPlayer()` absent-set half; buy/sell pricing is
9.10's. The plain `item_container` lid mover and the animated-container `soundgroup` path remain;
the tutorial lockpick's attempt HUD remains a logged stub.
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

Apply the recovered RE39 TERM4/TERM5 tutorial contract and close the required TERM2 cases; parse
patch-first `TerminalDefinition` into
a plain-C++ terminal state machine; give `FElysiumTerminal` the exclusive explicit-use session
and authoritative non-bindable `hackcmd` path; reproduce Function ordering on the one queue;
export model-local screen metadata; project a modern 36×24 CommonUI console over the physical
screen through a fixed `Focus` camera request. Keyboard line editing and the controller's
semantic action palette reach the same handler; VtMB's terminal font/VGUI/512×512 raster are
not reused. TERM7 email and TERM8 screensaver stay in 10.7. *Design:*
`docs/architecture/computer-terminal-architecture.md`; behavior: `docs/vtmb/computer-terminals.md`.
*Acceptance:* from real input, keyboard and gamepad focus `tuthack`, enter `Safe` by password or
Hacking bypass, execute Unlock, enqueue `OnTrigger0`, reveal/unlock the safe through the authored
map wires, quit, restore the exact
previous camera; the grid stays readable inside the bezel at 1080p/1440p/4K. *Deps:* RE39
TERM2, 4.11, 6.8, 9.6, 11.4–11.8.

### 13.5 Combat AI — remaining

The sound-event bus, senses and stimulus memory, the conditions layer, the ideal-state pass, the
enemy transaction, the alert/combat schedule families, the loadout and `aiscripted_schedule` are
built over the schedule kernel and the body-owner arbiter. Open: the **footstep hearing
producer** (the one bus category with nothing raising it — the seam is marked in
`Substrate/ElysiumPlayerEntity.cpp`); the **flinch and reaction action families** on the
animation side, which are CCC11's; `COND_HIT_BY_DOOR`'s producer and the door-obstruction
retreat's consumers, which stay in 10.7; and the `Prone` state, a named refusal until a
producer is recovered. Facts: `docs/vtmb/npc-ai-reverse-engineering.md`.
*Acceptance:* the tutorial's hostile beats run from real producers — authored
`OnFoundPlayer`/`OnDamaged` consequences fire as wired and the combat lessons' opponents fight
and die as retail. *Deps:* 11.10.
