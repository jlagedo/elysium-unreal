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

### 4.11 Close the trigger and `+use`-prop I/O gaps RE35 exposed

Four separable pieces. (a) **`trigger_hurt` cadence**: retail deals `damage × 0.5` on entry
then `damage × 3.0` every 3.0 s; the runtime deals `damage` on entry then `damage` every 0.5 s.
Register `HurtNow`, `SetDamage`, `OnHurt`, `OnHurtPlayer` while there. (b) **Trigger
`filtername`**: late activation resolves the retained handle; `filter_activator_name` and
AND/OR `filter_multi` reject before touch-pair deduplication. The same verdict gates
`use_filter_name`. `prop_switch`, the four lockable leaves and the doorknob handle sequence use
the shared 4.4 focus/session path; `CPropSwitch` remains a sequence player whose `OnActivate`
fires on clip end. Still open here: its `soundgroup` on/off events and a runtime reset hook that can
apply `reset_state`. Terminal interaction is owned separately by 13.4.
→ `docs/vtmb/entity_io.md`. *Deps:* 4.1, 8.3.

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

The tutorial talk/feed presentation slice landed. **Not finished:** the RPG reaction-score
calculator (`reaction.txt`/`reactions000.txt`); `React`, loud-expression policy and broader
expression/gesture semantics. Senses, the enemy-selection transaction and the alert/combat/flee
schedule consumers are 13.5's. The three social domains stay independent (K4): the reaction
score is consumed by dialogue and never by combat targeting. *Deps:* B4.

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

### 13.1 Stealth

Load selected `vdata/stealth` and `vdata/system/StealthKillRules.txt`; sneak mode supplies
movement/posture and the three-point light/Sneaking target surface (light sampled through the 11.15
query on the 0.1 s cadence); 13.5's senses — vision, cone, LOS hysteresis, hearing, memory, enemy
selection and the found/lost outputs — consume it; `trigger_stealth_mod` owns balanced overlap
contributions; 8.9 renders only the committed observer snapshot; the stealth-kill transaction
computes deaf arc / approach depth and commits grapple mode 3 on qualified targets. Faithful
observer and stealth-kill transaction: `docs/vtmb/stealth.md`. *Acceptance:* from real input, the
tutorial's ordinary stealth and stealth-kill lessons complete as retail; focused cases cover
threshold edges, the three-sample light cadence, 512-unit LOS crossover, transient/sustained
occlusion, sound-only discovery, overlapping modifier volumes, stale HUD handles, and save/load
inside a volume. *Deps:* 4.7, 8.9, 11.4, 11.8, 11.9, 11.15, 13.5.

### 13.2 Disciplines

Activation/deactivation over `vdata/disciplinetgt_*`, blood cost through the sheet, timed
effects on the one queue (R4), the tutorial's discipline lesson (`ClearActiveDisciplines` and
friends become real). Three powers join 13.3's damage commit rather than owning steps of their
own: Bloodshield fills `HealthBuffer`, Fortitude feeds automatic soak, Potence floors the melee
commit. Targeted `HitInfo` AI-schedule channels assign into 13.5's kernel. The overt→Masquerade
predicate stays unimplemented until recovered. Faithful behavior and remaining RE:
`docs/vtmb/disciplines.md`. *Acceptance:* the tutorial's discipline lesson completes as retail.
*Deps:* 11.4, 13.3.

### 13.3 Firearms & melee basics

Two stages over one seam set. **The damage spine first:** `FElysiumDmg` mirrors the 17-word
`CVDmg_t`; `ElysiumDamage::Apply` runs the confirmed order — family reject → Kindred firearm
lethal→bashing → damage roll at difficulty 6 (or direct input under flag `0x8`) → non-botch
floor → automatic soak → the 8-feat soak table at PC 3 / NPC 7 → soak capped to damage
successes — with the step-9 template filters and the word-15 commit held as marked open joins
(populated, never multiplied in). `FElysiumCombatCharacter::CommitDamage` is the one typed
health commit (`HealthBuffer` → the unkillable cap at the literal 75 → the damage-taken counter
→ aggravated tracking under mask `0xC8000008`) and the real producer of
`OnDamaged`/`OnHalfHealth`, the damage stimulus and `NPC_TAKE_DAMAGE`; the scalar
`TakeDamage(float)` stays the compatibility fallback and never grows semantics; 4.11(a)'s hurt
cadence rides the same commit, and the pending NPC-chain `TakeDamage` input converts to a
direct-input descriptor. **Then weapons:** `FElysiumWeapon` over `vdata/items/` modes,
equip/holster on the active-weapon handle, next-attack on the substrate clock, reload
transactions (end times from the selected sequence's duration over rate; the `reload_single`
interruption latch), dry fire. The attack shape is accepted-swing → contact commit that can
miss, entering on the animation event with the clip's authored event time supplied by activity
resolution so the same path runs headless. Melee adds the automatic `2COMBO` base-rank table,
the opposed record and the `rules.txt` margin classifier; ranged adds per-victim lethality →
Kindred-only `Defensive_Maneuvers` → direct damage. `CalcFeat` stays a rating (K5); the five
reaction concepts stay separate; there is no firearm stagger to build. Full combat AI is 13.5.
Faithful behavior and the open joins (spread formula, swing contact window, `SkillRequirement`):
`docs/vtmb/combat-and-damage.md`. *Acceptance:* the tutorial's range + melee lessons complete
as retail. *Deps:* 9.8, 9.6, 11.4.

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

### 13.5 Combat AI

The NPC mind's combat completion, promoted from the long tail, built in producer order over the
existing schedule kernel and body-owner arbiter (gameplay architecture §5.5, ledger rows 8–9).
(a) **The sound-event bus** — `FElysiumEntityWorld::EmitGameSound` with
`sound_volume_table.txt` categories and radii; real producers: footsteps, gunshots,
`NPC_TAKE_DAMAGE` from the damage commit, feeding pulses, overt casts, doors. (b) **Senses and
memory** — sight admission from spec tuning (`vision`/`hearing` with the `-1.0` sentinel
deriving from `npc_perception`) times the player's stealth scalars; the 2.0 s LOS cadence,
512-unit no-trace shortcut, 8 s grace and 10-failure committed-enemy debounce over the 11.15
queries; hearing consumes the bus; memory keeps lost-LOS distinct from lost-target and fires
`OnFound*`/`OnLost*`/`OnHear*` from the real edges. (c) **The enemy transaction** — conditions
→ the active schedule's interrupt-mask gate (`NEW_ENEMY`/`LOST_ENEMY`/`ENEMY_DEAD`, the
starvation rule a global scorer would miss) → `ShouldChooseNewEnemy` → `BestEnemy` (reachable →
priority → distance, visibility-modified; `D_HT`/`D_FR` rows only — the relationship store's
one consumer, K4) → `SetEnemy` and its memory/output effects. (d) **States and schedules** —
`Alert` and `Combat` go live with their recovered producers (`no_alert_state` acts at its four
recovered sites, never as a blanket suppression); the registry grows the recovered families:
damage flinch/take-cover, melee (dodge/preblock/kick-stepback/attack/circle/advance in the
recovered selector order; weapon capability `0x18000` selects melee vs ranged), ranged
(attack/clear-shot/chase/run-away), investigate, flee/cower; cover reads `info_node_cover_*`
hint entities out of the entity world — an entity read, not a spatial query.
(e) **`aiscripted_schedule`** on the recovered mode table (move-to-goal /
assign-enemy-with-`NEW_ENEMY` / follow-path) and the non-identical `forcestate` mapping
(authored 2 → native alert 3, authored 3 → native combat 2); body-owner values `Schedule` and
`ScriptedSchedule` go live through the arbiter. Per-NPC decision tracing lands with the kernel
(architecture §7). Facts: `docs/vtmb/npc-ai-reverse-engineering.md`. *Acceptance:* the
tutorial's hostile beats run from real producers — authored `OnFoundPlayer`/`OnDamaged`
consequences fire as wired and the combat lessons' opponents fight and die as retail; headless
Substrate coverage first (K10). *Deps:* 13.3, 11.14, 11.15.
