# The VtMB player entity

VtMB represents the player as one server entity whose inherited datamap is also the durable
character record. Movement, rendering and camera code embody or observe that entity; they do not
replace it as the owner of game state. This document owns the complete player-object boundary:
identity in the entity world, property families, command/update lifecycle, save/travel identity and
the relationships to the physical body, inventory entities, viewmodels and cinematic controller.

System-specific behavior remains in its existing owning document. This document joins those facts
and records the questions that require one player-wide transaction to answer.

## Evidence boundary

The established surface combines:

- the player entity's `FENTTABLE_PLAYER` save record and inherited 277-field datamap
  (`savegame_format.md`);
- the server user-command and game-frame call order (`game_runtime.md`);
- the player and combat-character datamap inputs plus `FindPlayer` (`script_api.md`);
- leading-`!` name resolution and the `!playercontroller` relationship (`entity_io.md`);
- movement, camera, animation, feeding, stealth, inventory and damage research in their owning
  documents and tracked cases.

The reproducible player-wide static investigation is
`research/cases/player-entity/`. Static evidence can establish field ownership, defaults, writers,
readers and call order. Engine-owned collision, traces, light samples and rendered camera results
remain service inputs. Frame-exact presentation, map-transition behavior and teardown require
controlled retail runs after the static transaction is complete.

## Identity and class chain

Exactly one saved entity carries `FENTTABLE_PLAYER`. Its inherited save surface is:

```text
CBaseEntity 41
CBaseToggle 1
CBaseAnimating 15
CBaseAnimatingOverlay 0
CBaseFlex 1
CBaseCombatCharacter 139
CBasePlayer 67
CHL2_Player 5
total 277
```

The player is engine-created rather than authored as a row in a map's entity lump. It already
exists when `createplayer` opens chargen; the wizard edits that entity in place. The class is
`player`. `FindPlayer()` returns the first spawned player entity or null.

`!player` and `!pvsplayer` use the special leading-`!` single-result lookup path. The player also
has the literal target name `!player`, but ordinary target-name fan-out is not what gives either
alias its semantics. Maps and scripts use the resolved entity as an ordinary target, activator,
filter candidate, teleport subject, damage receiver and dialogue listener after lookup.

`!playercontroller` is not another player and not a controller object standing in for gameplay
authority. It is the map-epoch `npc_VPlayerController` relationship created for choreography: a
temporary animating duplicate that borrows selected model, transform and character state, then
returns its final pose anchor to the player when removed. Its lifetime and restoration rules belong
to `entity_io.md`.

## Construction, spawn and placement

The retail construction transaction is statically closed at its class boundary:

```text
register entity factory for classname player
  -> PutClientInServer calls CreateEntityByName("player")
    -> allocate 0x2548 bytes
      -> CBaseCombatCharacter constructor
      -> CBasePlayer constructor
      -> construct CHL2 local data at player +0x2464
      -> install the CHL2_Player vtable
    -> bind the client and supplied player name
    -> dispatch virtual slot 103: CHL2_Player::Spawn
      -> CBasePlayer::Spawn
      -> CHL2/VtMB post-spawn defaults
```

Construction establishes storage and ownership, not a playable body. The inherited constructors
invalidate entity handles, construct the compact 224-slot inventory-handle array, allocate the
player-owned VtMB character helper at `+0x1d24`, initialise dynamic containers and establish the
keyring backup. The final CHL2 constructor adds its local-data block and installs the final vtable.
The strings `Player Created` and `Player Name` occur in a diagnostic character dump, not in this
creation transaction.

`CBasePlayer::Spawn` is the gameplay reset. It raises the `+0x1e00` spawn/restore gate, invokes the
inherited combat-character spawn, restores health and maximum health to 100, clears life state, the
six eight-entry gait tables, transient handles, deadlines, motion and fall state, unlocks a locked
player and selects `Player_Malkavian` when no valid player template is present. It then creates
`item_w_unarmed`, establishes the standing/duck hulls, `SOLID_BBOX`, collision group 3 and walk
movement, joins team `player`, lowers the gate and calls `OnPlacedInMap`. The CHL2 wrapper clears
the next stealth-update deadline after the base spawn and, for the still-unnamed `+0x19f6` latch,
can leave the result non-solid in move type 9 and equip fists.

`OnPlacedInMap` clears map-local transient handles and runs placement setup. In developer mode it
also validates the live player: solid flags, bbox solid type, collision group, allowed movement
mode, exact hull and the required baseline inventory (`item_w_unarmed`, wallet, keyring and light
clothing, plus an appropriate fist/claw weapon). These checks are retail diagnostics rather than
another source of gameplay authority. The terminal post-spawn helper conditionally publishes a
client entity-state message; its message identity remains unresolved, so it is not yet assigned to
the law system merely because it reads law-adjacent fields.

## Property families

The save proves which values persist, but persistence alone does not prove runtime authority. The
player-wide field ledger must classify each of the 277 records as authored, authoritative,
derived, cached or presentation-only and name every writer and consumer.

| Family | Established contents | Owning detail |
|---|---|---|
| Entity identity and world state | name, target, owner, parent, transform, velocity, flags, health projection, model and think state | `entity_io.md`, `savegame_format.md` |
| Character sheet | base/current attributes, abilities and disciplines; clan, sex, blood, humanity, Masquerade, health/damage and derived pools | `game_runtime.md`, `skills-and-checks.md` |
| Progression and identity | player name, History, money, blood stolen, XP ledger and accumulators, passive effects, quest journal and selected quest-log area | `game_runtime.md`, `savegame_format.md` |
| Law and world response | criminal, supernatural and investigate activity levels; witnessed incidents; Masquerade throttle; delayed police response; cop/hunter pursuit and heightened alert | this document; `game_runtime.md` |
| Combat and disciplines | discipline flags/cooldowns/cast counter, Obfuscate/Protean state, feed deadlines, ranged accuracy and current weapon activity | `disciplines.md`, `feeding.md`, `combat-and-damage.md` |
| Stealth target surface | feet/centre/head light samples, aggregate light, next sample/update, vision distance, cone and hearing values | `stealth.md` |
| Movement | six per-direction gait tables plus inherited movement, ground, water and view state | `source_movement.md`, `game_runtime.md` |
| Animation and camera | sequence, cycle, pose parameters, head/eye data, camera view/target handles and player action state | `animation_and_movers.md`, `camera-view-modes.md` |
| Inventory relationships | 224 compact item handles, active/last/melee weapon, armour, view weapon and two viewmodel handles | `inventory.md`, `savegame_format.md` |
| Terminal state | per-terminal global email flag records | `computer-terminals.md` |

Inventory is an object relationship, not a bag of item names. Each carried item remains a complete
moveable entity whose owner points to the player; the player's arrays and equipped handles point
back to those entities. The `.HL3` transition list identifies the player and the particular
moveable entities that left a map. `FENTTABLE_MOVEABLE` only marks eligibility and is not itself
proof that an entity travelled.

## World and frame relationships

The verified server order is:

```text
client move message
  -> CBasePlayer::ProcessUsercmds
    -> PlayerRunCommand for each replayed/new command
      -> StartCommand and command-scoped curtime/frametime
      -> UpdateButtonState
      -> CHL2_Player::PreThink
      -> player Think
      -> SetupMove / movement / FinishMove
      -> ProcessImpacts
      -> CBasePlayer::PostThink
      -> FinishCommand
server GameFrame
  -> ordinary entity thinks
  -> event queue
```

The player is therefore the one entity whose own update is part of command processing rather than
the later ordinary entity-think pass. Dropped commands are replayed synchronously, so any
player-owned rule in this chain must be classified by command, not assumed to run once per rendered
frame.

The `player Think` step is a scheduled-think dispatcher, not a second unconditional rules pass.
`RunPlayerThink` compares the entity's next-think time with the command's simulation window; when
due, it clamps the dispatch time to the command start, temporarily publishes that value as server
`curtime`, clears the next-think field and calls the registered entity think. Otherwise it does
nothing. `PlayerDeathThink` is registered through the unnamed datamap think record and can therefore
run here; ordinary live-player rules remain in `PreThink` and `PostThink`.

Recovered slices already prove work inside this transaction: gait tables and stealth factors in
`CHL2_Player::PreThink`; feeding pulse and player action classification in the player update; and
animation routing in `CBasePlayer::PostThink`. The player-wide extraction below joins those slices
into the top-level `PreThink` and `PostThink` order. Law and world-response branches are classified
below; the remaining address-labelled status, camera and transition subcalls stay open rather than
being assigned guessed semantics.

### World-owned area authority

The player does not retain its own safe/combat/Elysium enum. The singleton world owns clamped
`m_nAreaType` at `+0x49c`, publishes it through `DT_WORLD`, and derives its map baseline from the
`worldspawn` key `safearea`. Dynamic `world.SetSafeArea(...)` calls mutate that same world field.

A changed value is a world-to-player transaction. `CWorldEvents::SetSafeArea` iterates connected
players and applies policy before marking the world state dirty: Elysium equips
`item_w_unarmed` and tears down all active and targeted Discipline effects; safe/Masquerade entry
ends Celerity and Protean; combat entry has no immediate player teardown. Future weapon, feed and
Discipline requests consult world state again through server-side player predicates. Thus the
player relates to zone authority as a policy subject and verb actor, not as the owner of a copied
area property.

Map travel obtains a new world's authored baseline. The field's network replication is closed,
but this static pass did not establish a save-datamap record for it; same-map save/load retention
remains open.

### Recovered `PreThink` body

`CHL2_Player::PreThink` is vtable slot 436 (`0x10350830`). It has two top-level paths rather than
one unconditional update. The saved `m_bInVehicle` byte at player `+0x20f4` selects a short path:

```text
UpdateClientData
-> CheckTimeBasedDamage
-> unresolved post-damage player hook 0x1016b440
-> return
```

The non-vehicle path validates the saved handle at `+0x1c58` against the protected-action predicate
at vtable `+0x670`, rebuilds the six gait tables when required and then stops immediately for
global game-over or saved `m_iPlayerLocked` (`+0x2304`). A live, unlocked player continues in this
verified order:

```text
VtMB player rule/status pass 0x10169960
-> ItemPreFrame 0x10174c10
-> WaterMove 0x10166120
-> retained CHL2 SuitPower_Update 0x103528d0
-> UpdateClientData 0x101755d0
-> CheckTimeBasedDamage 0x1016b270
-> unresolved post-damage player hook 0x1016b440
-> train/physics flag maintenance
-> if life state is non-zero: PlayerDeathThink 0x101668b0 and return
-> directional-use train, jump, duck and fall-velocity handling
-> if curtime >= m_flNextStealthUpdate: stealth recompute 0x103517e0, schedule +0.1 s
-> cvar-gated StealthDebugDump 0x10351e10
-> final virtual 0x10352c90 (empty body)
```

The first call is not a generic engine frame hook. Its body assembles player status bits, advances
VtMB-specific timed state and directly joins supernatural-act, law/response and character-effect
helpers. Its recovered law order is supernatural-level expiry, delayed response-cop deadline,
heightened-alert expiry and queued supernatural incidents. `ItemPreFrame` reaches the active
weapon's pre-frame virtual only after the next-attack deadline. `UpdateClientData` sends changed
HUD, character, inventory and weapon state; it is not gameplay authority despite its position in
the server transaction.

`SuitPower_Update` is an important negative result. Slot 472 still charges or drains inherited
CHL2 auxiliary power at `+0x2464` using active-device bits `+0x252c` and load `+0x2530`. Those
fields have no established VtMB datamap or authored-content contract. The executable's dormant
Source bookkeeping is therefore not a VtMB rule to reproduce unless a separate authored consumer
is found.

Death begins late enough that rule/status, active-weapon pre-frame, water, client-data and
time-based-damage work has already run. `PlayerDeathThink` then preempts train use, jumping,
ducking, fall tracking, stealth recomputation and the final hooks. This is also why the unnamed
datamap think registration cannot be treated as an input. The routine schedules itself again,
damps grounded motion, advances dying to dead, plays `interface/final_death.wav`, waits for the
game-rules and input-release gates, records the dead-state deadline and eventually clears the death
input/timer state. Its terminal helper invokes virtual slot 103, `Spawn`, only when either of two
global load/game-state flags is set; otherwise it simply retires the scheduled think. Death is
therefore a staged state machine with a conditional respawn, not entity destruction.

### Law, Masquerade and world response

The player owns three activity-level channels. They are not one generic “wanted” value:

| Channel | Retained player state | Mutation and expiry |
|---|---|---|
| supernatural | level `+0x1ccc`, expiry `+0x1ce4`, incident count `+0x1cec` | non-zero writes raise but never lower the retained level, refresh its deadline and increment the count; `PlayerRuleUpdate` clears an expired level |
| criminal | protected level `+0x1cd8`, expiry `+0x1ce0`, incident count `+0x1ce8` | the same raise/refresh/count rule; the law helper reached by `SetAnimation` clears an expired level, while player action 300 reasserts level 1 as `LockPick` |
| investigate | level `+0x1cdc` | direct replacement with no companion timer or incident count in the player setter |

`SetCriminalLevel`, `SetSupernaturalLevel` and `SetInvestigateLevel` accept only an integer
entity-input variant, clamp it to `0..5`, and treat a wrong type or negative value as zero. Zero explicitly
clears a timed level and sets its expiry to `-1`. For criminal and supernatural non-zero calls, a
positive explicit duration wins; otherwise the duration is the greater of the previously retained
level and `pl_min_act_timer`. The map activity trigger passes `-1`, so its repeated `Touch` calls
refresh a finite deadline rather than establish an indefinite level. A lower non-zero request does
not lower an already higher level, but it still refreshes that deadline and increments the channel's
incident count.

The upstream witness transaction is distributed, followed by a late world-area guard:

```text
accepted player verb
  -> raise criminal and/or supernatural activity level
  -> increment that channel's player act count
NPC condition gathering
  -> compare player act count with this NPC's processed count
  -> require the retained closest player and COND_SEE_PLAYER for the direct-player lane
  -> compare current activity severity with this NPC's authored flee/attack thresholds
  -> retain offender, origin, severity and supernatural flee-only policy
  -> set one or both criminal/supernatural flee/attack AI conditions
NPC schedule selection/translation
  -> require the retained offender still be the player
  -> submit the retained incident, then copy the player count into the processed count
terminal incident thunk
  -> require world m_nAreaType != 0
player incident consumer
  -> Masquerade and/or delayed police-response policy
```

The activity producer decides severity, not guilt. A successful targeted Discipline commit raises
supernatural activity to the target record's `SupernaturalLvl`; when that independent record also
has `Overt`, the same commit raises criminal activity to level 3. Feeding pulses raise
supernatural 2 and criminal 3 for two seconds, and an interrupted feed raises criminal 1 for two
seconds. `trigger_player_activity_level` can author all three channels directly. Physics drag and
lift use the configured supernatural value, while throws use the configured criminal value. None
of the player setters or the targeted-Discipline commit receives a zone argument.

Each NPC owns four authored thresholds — criminal flee/attack and supernatural flee/attack — plus
independent processed counts, retained witnessed levels, locations, offender handles and three
ignore deadlines. A threshold of 6 cannot be reached by the player's clamped `0..5` activity
level, so it effectively disables that reaction. Condition gathering clears and recomputes the
five investigate/law conditions. It rejects the law pass while frenzied or busy with a dynamic
interaction; when an ignore deadline closes that channel's observation window it advances the
processed count without producing a reaction. NPC spawn initializes the three deadlines to zero.
Feeding opens both criminal and supernatural windows on the victim for three seconds, entering
NPC state 14 opens the criminal window for two seconds, and the closest-player special case opens
the Nosferatu window for five seconds. No other static caller reaches those three deadline setters.

The parallel world-event lane retains short-lived records with kind, severity, origin, expiry and
offender. An NPC accepts a record only when its origin is inside the view cone, within
`m_flSeekDistInspection`, and an unobstructed trace reaches it. The strongest accepted criminal and
supernatural records independently feed the NPC's flee and attack thresholds. This is the spatial
witness test; ordinary hearing and the target record's `TriggerAISound` remain separate AI-sound
machinery.

The four law conditions are `COND_CRIMINAL_FLEE_LEVEL` (31),
`COND_CRIMINAL_ATTACK_LEVEL` (32), `COND_SUPERNATURAL_FLEE_LEVEL` (33) and
`COND_SUPERNATURAL_ATTACK_LEVEL` (34). Schedule branches, not condition gathering, call the two
player incident consumers. A supernatural flee-only branch instead inserts a 16-byte player-owned
scare record keyed by NPC identity: a repeat keeps the greater severity and refreshes its
timestamp. `PlayerRuleUpdate` selects from that queue, submits the supernatural incident and
removes consumed/expired records. Seeing a sufficiently close `Player_Nosferatu` has its own
severity-2 flee-only admission and does not require a new Discipline cast.

Activity levels and witnessed incidents are separate stages. `PlayerCriminalIncident`
(`0x1017f2a0`) publishes the changed player state and enters common police-response admission; it
does not change Masquerade. `PlayerSupernaturalIncident` (`0x1017f4a0`) has two independent
consumers in an active game:

1. when `curtime >= m_flMasqueradeTimerNext`, call `ChangeMasqueradeLevel(+1)` and set the next
   deadline from `debug_masquerade_timer`;
2. when `debug_supernatural_cop_spawn` is enabled, enter the same police-response admission as a
   criminal incident.

This closes both sides of the player transaction. `Overt` raises criminal activity; authored
`SupernaturalLvl` raises supernatural activity; NPC perception and thresholds decide whether an
incident is witnessed; only the admitted supernatural incident can mutate Masquerade. The sole
criminal and supernatural incident thunks then require world `m_nAreaType != 0`, suppressing both
consequence paths in combat area. Elysium instead blocks weapon, feed and Discipline verbs before
their commit, while the HUD icon is a client projection of the replicated enum. Zone therefore
joins twice, not through one `zone + verb + witness` callback.

`ChangeMasqueradeLevel` mutates sheet stat index `0x1c`, republishes player client state and fires
the game-rules output for the resulting level plus the generic level-changed output. An increment
whose resulting value is greater than four loads `sp_masquerade_1`. The retail loss boundary is
therefore a native level-5 transaction, not only a UI convention.

Police response is retained and delayed:

```text
witnessed criminal incident
or enabled supernatural incident
  -> reject while m_iHuntersInPursuitCount > 0
  -> queue severity, witness handle and incident position
  -> schedule random debug_response_timer_min..debug_response_timer_max deadline
  -> before expiry, replace only with a higher severity; do not reschedule
PlayerRuleUpdate
  -> on deadline, defer across the global load gate
  -> consume the queue
  -> continue only if the saved witness handle still resolves
  -> desired cops = max(1, severity - 1)
  -> apply debug_cop_grace_time duplicate/higher-severity delta policy
  -> spawn the delta through the global NPC maker, or retain it for a cop wait area
```

The no-wait path does not construct a cop inside the player object. It marks the player as the
response target and invokes the configured global NPC maker once per required delta; a maker
failure reports `Failed to spawn with NPCMaker`. The wait-area path records its deadline and the
maximum desired count, then fires the game-rules waiting/coming outputs for later release.

Pursuit and alert are another state machine after admission. `m_iCopsInPursuitCount` at `+0x1d10`
fires `OnStartCopPursuitMode` on its zero-to-one edge and zeros a retained alert deadline, causing
the next player rule pass to expire that alert. Its
one-to-zero edge fires `OnEndCopPursuitMode`, sets `m_bInHeightenedAlert` at `+0x1d18`, schedules
`m_flHeightenedAlertExpireTimer` at `+0x1d1c` from
`debug_heightened_alert_expire_time`, and fires `OnStartCopAlertMode`. `PlayerRuleUpdate` later
clears that byte and fires `OnEndCopAlertMode` when the deadline expires. The independent
`m_iHuntersInPursuitCount` at `+0x1d14` fires its own start/end outputs and suppresses new
police-response admission while non-zero. Dialogue predicates read these pursuit/alert states, so they
cannot be reduced to the criminal activity level or to whether a cop actor currently exists.

### Recovered `PostThink` body

`CBasePlayer::PostThink` is vtable slot 437 (`0x1016be10`). It first expires pending client state,
refreshes absolute velocity and smooths velocity. Global game-over, `m_iPlayerLocked`, a non-live
player or the byte returned by vtable slot 406 (`+0x19f6`) skips the live main body but still
reaches the common tail. Slots 406–408 are a getter/setter/clear trio for that byte; its semantic
name is not yet recovered. The live main body orders:

```text
standing/duck collision bounds and controlled-use validation
-> ItemPostFrame 0x10174ce0
-> grounded fall-sound/reset
-> realized action classifier 0x1016bb50
-> SetAnimation 0x10164240 when the action is non-negative
-> invalid-sequence clamp
-> StudioFrameAdvance / DispatchAnimEvents / simulation time
-> Weapon_FrameUpdate
-> UpdatePlayerSound
-> forced-origin presentation state, when armed
-> PostThinkVPhysics
```

`ItemPostFrame` gives a controlling use entity first refusal; otherwise it dispatches the active
weapon's busy-frame or ordinary post-frame virtual according to the next-attack deadline. The
action classifier consumes the movement that just completed, so animation is a result of the
command rather than an input to movement. `SetAnimation` is the already-recovered priority router,
including paired-action ownership and protected activity ranges.

The common tail begins with `CBasePlayer::SimulatePlayerSimulatedEntities` (`0x1017c300`), which
removes stale handles and advances the surviving owned entities. VtMB then performs a keyring
countdown/removal, a conditional `CBaseCombatCharacter::HungerCheck` (`0x1033f4c0`), an
address-labelled status reaction (`0x10338900` -> `0x10338920`), bidirectional state copying with
the saved handle at `+0x1db0`, a delayed callback and an active-weapon notification. The
`+0x1db0` copy branch is a player/body or cinematic-state join, but its concrete entity class is
not yet proven and remains open.

One VtMB-specific Animalism beast-model transition inside the live body returns directly after
resetting action/model state. It bypasses both the remainder of the live body and the common tail;
this exceptional return must be validated before teardown or transformation behavior is
implemented.

The world observes the player in several distinct roles:

- **target** — `!player`, `!pvsplayer`, `FindPlayer()` and ordinary handle relationships;
- **activator/caller provenance** — touch, `+use`, filters, outputs and changelevel admission;
- **combat character** — sheet, damage, feeding, inventory and inherited datamap inputs;
- **perception target** — eye/feet geometry, light/sound modifiers and NPC memory;
- **dialogue listener/camera subject** — a game entity with a separately rendered body and view;
- **transition root** — the durable player plus the selected moveable entities that accompany it.

These roles share identity but not mechanism. A trace result, light sample or collision overlap is
an engine answer about the body; the resulting game decision and persistent state belong to the
entity transaction.

## The animation update, and where it sits in the frame

The player's whole animation decision lives inside **`CBasePlayer::PostThink`**
(`vampire.dll 0x1016be10`, vtable slot 437), which runs once per user command, strictly **after**
movement. The chain into it is
`CServerGameClients::vfunc8` -> `ProcessUsercmds` (slot 461) -> `PlayerRunCommand` (slot 462) ->
`CPlayerMove::RunCommand` (`0x101874a0`), which sets `gpGlobals->frametime` and `curtime` from the
command, copies `ucmd->viewangles` into `pl.v_angle`, runs pre-think, `SetupMove`,
`ProcessMovement`, `FinishMove`, `ProcessImpacts`, and then post-think.

The animation-relevant steps of `PostThink`, in order:

| step | what |
|---|---|
| 7 | `ItemPostFrame()` (slot 458) -- **before** the animation decision |
| 9a | melee freeze: with `m_IdealActivity == ACT_MELEE_ATTACK` and no movement buttons, velocity is zeroed |
| 9b | `FUN_1016bb50` -- the compact `PLAYER_*` code classifier |
| 9c | `SetAnimation(code)` (slot 449) -> the gait ladder, the `move_yaw` write and the aim latch |
| 11 | `StudioFrameAdvance()` (slot 250) -- returns the interval, and advances all four overlay slots |
| 12 | `FUN_101600a0(interval * m_flSpeedScale)` -- the weapon turn-rate spread accumulator |
| 13 | `DispatchAnimEvents(interval, this)` (slot 258) |
| 15 | `UpdateCharacter(interval)` (slot 312) -> expressions, the eye maintainer, **`UpdatePoseParameters`**, melee swing |
| 24 | the `m_hControllerNPC` (`+0x1db0`) possession mirror |

**The player does run `UpdateCharacter`.** A caller census that resolves only the thunk
(`0x1000669f`) reports a single `CAI_BaseNPCTroika` caller and reads as NPC-only; the disassembly
settles it -- `0x1016c316` is `MOV EAX,[EBP]; PUSH ESI; MOV ECX,EBP; CALL dword ptr [EAX + 0x4e0]`,
slot 312, and `vtmb_func 0x103246d0` lists `CBasePlayer#312`. There is a second dispatch at
`0x1016c0e5` on the beast-form early-return path. This matters because slot 312 is the only route by
which `aim_pitch` and `aim_yaw` ever reach the model
(`docs/vtmb/animation_and_movers.md` -> "The aim pair is latched and slewed").

## The weapon activity queue is a one-deep lookahead

`m_aCurWpnActivity` = **`CBaseCombatCharacter + 0x14b0`** and `m_aNextWpnActivity` = **`+0x14b4`**,
both ints, both saved, sentinel **`-1`** (named in the `CBaseCombatCharacter` datamap builder;
`_DAT_10619218 = 0x14b0`, `_DAT_10619244 = 0x14b4`).

**Both fields are declared on `CBaseCombatCharacter`, so an NPC's save file carries them, and every
accessor is on the `CBasePlayer` path.** No NPC reads or writes either. The complete ledger is four
functions and nothing else in 38,562 decompiled functions touches either offset:

| function | address | role |
|---|---|---|
| `CBasePlayer::Spawn` (slot 103) | `0x1016d260` | both to `-1`; the only reset |
| the commit | `0x1015fbb0` | push |
| the drain | `0x1015fcd0` | pop on completion |
| `CBasePlayer::DrawDebugTextOverlays` | `0x10161460` | read-only, prints `"WpnActv: %s"` |

The commit:

```c
m_aNextWpnActivity = -1;                                  // cleared FIRST, unconditionally
if (!bForce && GetLayerActivity(0) == activity)   return;
seq = SelectWeightedSequence(activity, -1);
if (seq < 0)                                      return;
if (!bForce && GetLayerSequence(0) == seq)        return;
SetLayer(/*layer*/ 0, activity, seq, /*autokill*/ true);  // vfunc +0x430, SLOT 0 literal
m_aCurWpnActivity = activity;
m_AnimOverlay[0].m_flPlaybackRate = m_flSpeedScale;       // +0x744 <- +0x1488
if (GetActiveWeapon()) {
    m_aNextWpnActivity = weapon->vfunc(+0x484)();         // the weapon names its follow-up
    if (weapon->vfunc(+0x488)() > 1) {                    // a stage count
        if (m_aNextWpnActivity == -1 && m_aCurWpnActivity != -1) SetLayerBlendIn (0, 0.0f);
        else if (m_aNextWpnActivity != 0)                        SetLayerBlendOut(0, 0.0f);
    }
}
```

The drain is `CBasePlayer::OnLayerFinished` (`0x1015fcd0`), the **only** override of vtable slot
`+0x1c0` anywhere in the hierarchy, called by the overlay stack when an auto-kill layer finishes:

```c
if (m_aCurWpnActivity == completedActivity && m_aNextWpnActivity != -1)
    if (IsGestureFinished(m_aCurWpnActivity)) {
        if (!IsLayerLooping(0)) CommitActivity(-1, m_aNextWpnActivity, /*bForce*/ true);
        else                    CommitActivity(-1, m_aCurWpnActivity,  /*bForce*/ true);
    }
```

**It is not a FIFO.** `Cur` is what is mounted on overlay slot 0; `Next` is the weapon's *declared*
follow-up, re-asked on every commit and cleared to `-1` at the top of every commit, so a commit that
early-outs leaves the queue empty.

**And it is dead for every weapon in the game except the frag grenade.** The two virtuals the
commit consults have exactly two bodies each across the ~250 weapon and item classes:

| slot | base body | returns | the one override |
|---|---|---|---|
| 289 (`+0x484`) | `0x10149d00`, `OR EAX,0xffffffff; RET` | **-1** | `CWeaponThrown_Grenade_Frag` `0x103ee480`, a jump table on the grenade's throw state at `+0x920` |
| 290 (`+0x488`) | `0x10149d20`, `MOV EAX,1; RET` | **1** | `CWeaponThrown_Grenade_Frag` `0x103ef5c0`, returns **2** |

The `> 1` gate is a hard compare against `1`, so the blend-time branch is unreachable for every
other weapon; and because slot 289 returns `-1`, `m_aNextWpnActivity` is `-1` after every commit and
the drain's guard is permanently false. `CWeaponRanged_Rifle_M37` overrides five virtuals in total
and none of them is near 289 or 290. **The M37's multi-part reload lives in the view-model activity
set (`ACT_VM_RELOAD_BEGIN_M37` / `_M37` / `_COMPLETE_M37`), not in the body overlay**, whose
third-person reload is the single `ACT_RELOAD_LAYER_M37`. The queue's one live user walks the frag
grenade through `ACT_HOLD_LAYER_GRENADE` (3831) to `ACT_THROW_LAYER_GRENADE` (3833) or
`ACT_ROLL_LAYER_GRENADE` (3835). The drain re-enters the ordinary apply path with `base = -1`
(leave the gait alone), so the follow-up goes through the identical translate-and-commit path and
immediately establishes its own `Next`. A looping layer re-commits the same activity instead.

**The player is pinned to overlay slot 0**, confirmed in five places: `SetLayer(0, ...)` twice,
`RemoveLayer(0)`, `IsLayerLooping(0)`, and both layer-0 getters. The player never calls
`AddGesture` and therefore never allocates a slot.

## Combat stance is a player-only concept, and reading it extends it

`CBasePlayer::IsInCombatStance` is vtable slot **411** (`+0x66c`, `0x1015ff40`); slot **410**
(`+0x668`, `0x1015fdf0`) is the setter, whose whole body is `m_flLastCombatAnimTime = curtime`.

```c
if (m_bIsMorphed /*+0x1edc, byte*/) return true;
if (m_hMeleeOpponent /*+0x1c58*/ is live && opponent->vfunc(+0x278)())
    m_flLastCombatAnimTime = curtime;                     // a WRITE, inside the reader
return (m_flLastCombatAnimTime /*+0x19b0*/ > 0.0f)
    && (m_flLastCombatAnimTime + 5.0f > curtime);         // 5.0f read byte-exact at 0x10454110
```

Four paths stamp the clock, and the fourth is the predicate itself: the `PLAYER_ATTACK1` arm of the
selector (gated only on an active weapon existing, so a whiffed swing counts as a landed one),
`CBasePlayer::OnTakeDamage` when the attacker or inflictor has `+0x9c != 0`,
`CWeaponMelee::RequestActivity` on every accepted swing, and the melee-opponent refresh above. So
the query is **not pure**, and the selector calls it up to three times per frame.

**NPCs have no such concept.** On `CAI_BaseNPC` and its 77 subclasses slot 410 is an identity stub
(`0x101a6420`) and slot 411 is **a bare `ret`**. A reimplementation must not model this as a shared
`CBaseCombatCharacter` predicate.

## Idle variation, and two dead fields

The player **never selects `ACT_FIDGET`** -- the literal is referenced only by the activity
registration table and by two `CAI_BaseNPC` animal schedules. `ACT_CHARSHEET_FIDGET` is client-side
and event-driven, issued by the character-sheet 3-D preview panel when the previewed model changes,
not by the world player. The whole `vdata/System/DispositionTable.txt` stance and fidget cadence is
dispatched at slot 611, and `CBasePlayer`'s vtable has 469 slots.

**The player's only idle variation is the ordinary weighted-random `SelectWeightedSequence(ACT_IDLE)`
made on entry to `ACT_IDLE`.** There is no re-roll while idle and no timer decays it. Two dead
fields make the intent visible: `m_bPlayerAnimIdleFinished` is written once (zero, in `Spawn`) and
read nowhere, and `+0x1c68`, a last-non-idle-activity timestamp written in three places, is read
nowhere in `vampire.dll`.

## Script and entity-input surface

The player datamap at `0x10580edc`, built by `FUN_1015af10`, contains 157 records beginning at
`0x10580f24` and exposes **ten external inputs**: `GiveItem`, `AwardExperience`, `Whisper`,
`SetCriminalLevel`, `RemoveCamera`, `PlayHUDParticle`, `StopHUDParticle`,
`SetInvestigateLevel`, `SetSupernaturalLevel` and `Holster`.

The apparent eleventh “input” is `CBasePlayerPlayerDeathThink`: a `VOID` record with no external
name, flag `0x20` and callback `FUN_101668b0`. It registers a think function, not an AcceptInput
name. Reconstructing all 157 dynamic records from the builder distinguishes it from the ten records
that carry an external name and input flag `0x8`. The player also inherits the 25
`CBaseCombatCharacter` inputs; those are part of the same datamap walk, not a second player API.

`GiveItem` has two externally visible routes: a player input for entity I/O and a Character method
found before the datamap walk by Python attribute resolution. Identical spelling does not collapse
their dispatch contracts.

## Save and transition relationship

The player is both a saved entity and the root of a cross-level object group. A complete
reproduction has to preserve four different kinds of identity:

1. the durable character values that survive every map;
2. the current map's player entity and its command/update schedule;
3. moveable item entities that leave one map with the player and materialize in the next;
4. map-local relationships, such as a feed victim or `!playercontroller`, that may only rebind
   when their owning map epoch is restored.

Restore does not reconstruct a second character object. `CBasePlayer::Restore` raises the same
`+0x1e00` gate used by spawn, restores the inherited entity, rebases three saved deadlines against
the restored clock and aborts cleanly if the inherited restore fails. It then rejoins team
`player`; without a landmark it resolves a spawn point and replaces origin and orientation. It
restores the standing or duck collision hull, republishes the global player/edict relationship,
restores the quest/experience registry, clears transient restore state, lowers the gate and
finalises the restore context. The gate also suppresses the terminal post-spawn client message
while restoration is in progress.

The exact retail hand-off from the old entity/save IDs through landmark transition into the player
and carried-item handles remains a player-wide research join. Save-field evidence and the recovered
player restore establish the root object's endpoint; they do not yet establish carried-item
materialisation, active-equipment rebinding or callback order across changelevel.

## Destruction and ownership cleanup

The CHL2 scalar-deleting destructor enters the inherited `CBasePlayer` destructor and only then
optionally returns the `0x2548` entity allocation to `CBaseEntity::operator delete`. The inherited
destructor frees the owned VtMB character helper at `+0x1d24`, another owned helper at `+0x245c`,
and the player's dynamic lists and containers before entering the combat-character/entity base
destructors.

It does not walk the 224 inventory handles deleting item entities, nor does it explicitly destroy
camera targets or transition entities in the recovered body. Those fields are entity handles and
their targets have separate world lifetimes. Teardown must therefore retire the player-owned
storage and the world relationship graph as two coordinated operations; treating every referenced
entity as destructor-owned would incorrectly delete map or carried entities.

## Open research contract

The player-wide investigation keeps these questions open:

1. Produce the full 277-row ledger with declaring class, type, flags, offset, default, writer,
   reader, authority class and related saved handle.
2. Recover the remaining activation and removal callers around the ordered construction,
   spawn, scheduled `Think`, death, restore and destructor paths; `PreThink` and `PostThink`
   address-labelled subcalls still need field and content joins.
3. Live-capture feeding, Physics Hand and overt/covert Discipline incidents across occlusion,
   threshold and ignore-window boundaries; the activity producer, spatial witness,
   condition/schedule admission, combat-zone guard, Masquerade, police-response, pursuit and alert
   transactions are statically closed.
4. Classify origin, hull centre, eye/view offset, control angles, body facing, view entity, camera
   target and local-body visibility; first-person viewmodel ownership is recovered
   (`docs/vtmb/animation_and_movers.md`).
5. Recover the map-transition transaction for the player and carried moveable entities, including
   active/last weapon, armour, reserve ammunition and stale-handle policy; separately live-test
   same-map save/load retention of world `m_nAreaType`, whose replication but not save record is
   established.
6. Resolve the `0x1016b440` post-damage hook, the slot-406 `+0x19f6` latch, the
   `0x10338900`/`0x10338920` status reaction, the `+0x1db0` PostThink relationship and the
   Animalism early-return consequences; the remaining top-level `PreThink`/`PostThink` consumers
   are classified.
7. Validate spawn/load, transition, conditional death respawn, cinematic-controller removal and
   world-side teardown boundaries in controlled retail incidents after the static joins close.

## Reproduction boundary

The faithful game object is one entity identity with inherited game state and one command-timed
update transaction. Unreal may own its pawn, collision, movement solving, camera evaluation and
rendering, but those engine objects are embodiment and query providers. They do not become a
second character sheet, inventory owner, event target or save authority.
