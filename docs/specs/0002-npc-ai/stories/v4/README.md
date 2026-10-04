# V4 — the animation chain under the kernel: the design

Planner's design for `spec.md` § Step 2, V4 (was C2; size M in the spec). It has grown since the
draft: N13 (the slow walk), the corpse, the stealth-kill corpse, the facing, slot 363, the weighted
pick and the anim events. It is cut into a reading story and three sub-stories (V4r, V4a–V4c),
each testable at its close, plus one item for the adversarial judge. Written 2026-10-04 on
`spec-0002/step-2` at `9c4f4a20` with V3c's edits uncommitted in the tree. **V4 is not started.**
Paths are relative to `Source/ElysiumUE/Private/Substrate/` unless they say otherwise.

| story | briefs |
|---|---|
| V4r (**done**, 2026-10-04) | `brief-R1-walk-reader.md`, `brief-R2-chain-reader.md`; `brief-J-judge.md` (the adversarial judge, after R2). Outputs: `packets-R1.md`, `packets-R2.md`, `packets-spike.md` (V4d's step 0), and `stories/v1/triage.md` § "Judge's rulings, V4" (J1–J9) |
| V4r settling (**done**, 2026-10-04) | no brief here. Outputs: `packets-S1.md` (death and corpse; walk and turn), `packets-S2.md` (weapons and attack data), `packets-S3.md` (the overlay, the move-and-shoot gates, the attack coordinator), `packets-S4.md` (the settling reads of the judge's second sitting: the NPC clip, the melee band's constants, the fade, the pedestrian's corpse, the turn script's helpers), and `stories/v1/triage.md` § "Judge's rulings, V4 — second sitting" (J2b, J10–J14). Each packet ends with § "Changes to the plan"; every change is written into the briefs of `v4/`, `../v4o/`, `../v5a/`, `../v11/` |
| V4a | `brief-A0-seam.md`, `brief-A1-dispatcher.md`, `brief-A2-clock-words.md`, `brief-A3-view-cone.md`, `brief-A4-player-camera-dispatch.md`, `brief-A-integrator.md` |
| V4b | `brief-R1b-slow-turn-reader.md` (one short read, J9, before B1/B2 start), `brief-B1-body-speed.md`, `brief-B2-move-yaw-facing.md`, `brief-B-integrator.md` |
| V4c | `brief-C1-attack-producers.md`, `brief-C2-pick-disposition-corpse.md`, `brief-C-integrator.md` |
| V4d | `brief-D-ragdoll.md` (its § "After the spike") |

**(amended after V4r, 2026-10-04)** The briefs are final: each was amended from the packets and the
judge's rulings and a coder works from its brief alone. There is no `packets.md`: R1's packet is
`packets-R1.md`, R2's `packets-R2.md`, the ragdoll spike's `packets-spike.md`. Where a ruling and
this document's older text disagree, the ruling wins; the paragraphs below that V4r changed carry
the same mark. Two parts of V4b still wait on reader R1b (three constants and the slow turn's
cause): they are marked in `brief-B1` and `brief-B2`, with the stop rule that applies. Two more
packets will be written into this directory: `packets-R1b-measurement.md` (the seam agent's
measured turn) and `packets-R1b.md` (R1b's read).

**(amended after the settling packets S1–S4 and the judge's second sitting, 2026-10-04)** Four
settling reads and a second sitting of the judge landed after V4r and their changes are written
into the briefs; **where a brief and this document disagree, the brief wins**, and the
paragraphs below that they corrected carry this mark. In short:

| packet | what it settled | what it changed here |
|---|---|---|
| `packets-S1.md` | `CreateCorpse 0x1032c0e0` arm by arm, the two `Event_Killed` bodies, the state-7 writers, feed and explosion deaths; `TASK_FACE_ENEMY` 0x2e whole (Troika `RunTask` **`0x102aae61`**, `AI_ClampYaw 0x102e1d10`); the velocity script `0x102630b0`'s five passes, its constants (50.0, 0.2, 150.0) and the arrival tolerance (0.0625 units, 2-D); the female `walk_0` (37 frames at 33.82 fps) | **B1 no longer waits on R1b**; R1b is narrowed to why the port's `task_face_enemy` ran 13.4 s; `damage_lethal_death` expects no schedule after death; `lifecycle.md`'s chain is already rewritten; no record reaches the `ACT_DIERAGDOLL` seed or `SCHED_DIE`; the pedestrian's `CreateCorpse` override `0x103a38c0` is wired by C2; the corpse's removal differs per body (`brief-D-ragdoll.md`) |
| `packets-S2.md` | all eight weapon operator bodies (slot 370); `ModeDispatch(1)` and `Shot`'s gates; the attack-rate keys; the melee band `0x103ea7e0`; `GetBestMeleeWeapon`; `0x102a11d0`; who reads the attack extents; `MeleeSwingUpdate 0x10346cd0` | **no "unread operator" seam** — `ContactEventCycle` is removed for every NPC wielder; the sweep integrates its own stamp `+0xaa4` in `N = ceil(dt × 100)` steps; the melee operator's trigger set; J2 went back to the judge — **re-ruled J2b (row below): the bbox import is withdrawn, slot 247's body stays in C1 on a named seam** |
| `packets-S3.md` | the `0x47` / `0x48` gesture gate and its cvar; slot 481; the writers of flags2 `0x400`; condition `0x4f`; slot 273's callers; the layer walk; the six coordinator bodies; the bat-only melee chain | `cover_move_shoot`'s red is **the slot-575 seam** (`ActiveWeaponCapabilityWord` answers 0), fixed in V4o lane O2, not V5; the two melee records take `0xdd` in reach and `0xcb` out of reach, one `MELEE_IDLE 0xc7` first being retail; V4o runs after V4b |
| `packets-S4.md` and **the judge's second sitting** (`stories/v1/triage.md` § "Judge's rulings, V4 — second sitting": J2b, J10–J14) | who fills an NPC's clip (`Inventory_Insert 0x10334e70`; `Shot` never lowers it); the melee band's five constants; `0x102fb4e0`'s flag (0); who can carry flags2 `0x400` on the two maps; `CNPC_VPedestrian::CreateCorpse 0x103a38c0` whole; `SUB_FadeOut 0x10269960` and who has the fade bit (25 of 62 makers); the turn script's two helpers; `0x1033d940`; the `GetBestMeleeWeapon` tables | **J2b**: the bbox import withdrawn and filed with the acquire cone's story (C1 keeps slot 247 on the `SequenceBounds` seam). **J10**: `0x102a11d0` implemented now in V11-1 (`melee_ally_in_the_way`). **J11**: ChangBros, FrenzyShadow, Bach, ManBat filed on `spec.md`'s on-demand line. **J12**: the port's NPC clip spend and refusal is an old bug, fixed in V4o O3 (`ranged_sustained_fire`); the reload finish stays a seam for V5b. **J13 / J14.1**: four corpse-removal clocks — C2 the thinks (the pedestrian's `Think` gate, the fade), D the body that survives them; records `corpse_removed_unseen`, `corpse_kindred_burns`, `corpse_pedestrian_stays`, `corpse_fades`; `BurnModel`'s look filed to 0014. **A0** gains the `removed` event kind (H22) and writes the six new records red. **J14.4**: V5a-1 ports the melee band. **J14.6**: B1 takes the turn script's helpers |

**Line numbers are today's and will move.** V3c is being integrated now, V3d deletes the arbiter,
the owner enum, `ThinkInDialog` and `RouteScheduleMaintenance` (hundreds of lines out of
`ElysiumNpc.cpp`), and T6b moves includes and splits headers. Every coder and integrator
**re-locates each site by Grep on the function name** before editing; a cited line is a hint, the
function name is the address. Nothing below depends on code V3d deletes.

**Provenance marks.** *(read)* the planner read the listing or the port body this session.
*(doc)* walked in `docs/vtmb/` and cited, not re-read in the listing. *(draft)* from
`consolidation/draft-2026-09-30.md` § C2 or the triage, not re-verified. *(record)* from an arena
record's `about`, not re-verified.

## 1. What retail does: the contract

**The frame.** `CAI_BaseNPC::PostRun 0x1026c7c0` *(read)*: `RunAnimation 0x1026c540` → slot 258
`DispatchAnimEvents(interval, this)` (`+0x408`) → `CBaseCombatCharacter::Weapon_FrameUpdate(interval)`,
in that order, inside the NPC's own think, after `RunAI`'s tasks. `RunAnimation` *(read)*: slot 250
`StudioFrameAdvance(0)`; a debug-flag zeroing of the interval when the navigator has no goal;
`AimGun` under `CAP_AIM_GUN`; then the idle re-pick — outside states 4 (SCRIPT) and 7 (DEAD), with
**`m_Activity (+0xfec) == 1`** and slot 251 true: **`m_bSequenceLoops +0x65d` false →
`SelectHeaviestSequence(m_TranslatedActivity +0xff4)`, true →
`SelectWeightedSequence(m_TranslatedActivity +0xff4)`**, committed by `0x10260a50` when not −1.
*(amended after V4r, 2026-10-04: R2 read the gate on `m_Activity`, `param_1[0x3fb] == 1`, not
`m_IdealActivity`, and the pick's argument as `m_TranslatedActivity`, `param_1[0x3fd]`, not
`m_Activity`; `shape.md` § "The activity commit" holds it.)* `Weapon_FrameUpdate 0x1032aa40` has
one caller, `PostRun`: the active weapon's slot 369 (`+0x5c4`, `CBaseCombatWeapon 0x1024efa0` on
every weapon class) with the wielder — the weapon model's `StudioFrameAdvance(0)`; if `finished &&
loops`, `SelectWeightedSequence(m_Activity +0x8a8)` → `m_nSequence`, `ResetSequenceInfo`; then the
weapon's slot 258 `(interval, wielder)`. For an NPC it is the world-model weapon's animation clock
with its events delivered to the NPC's `HandleAnimEvent`: no fire, no sweep. The player never calls
it *(R2 item 2)*. The Troika think's order: `RunAI` → `PostRun` → `PerformMovement`, then slot 312
`UpdateCharacter` when due (`0x1029365b`) *(doc)*.

**The clock.** `StudioFrameAdvance 0x1008f120` *(read)*: re-seeds a zero `m_flPrevAnimTime`; a zero
interval becomes 0.1; advances `m_flCycle` by `GetSequenceCycleRate × m_flPlaybackRate × dt`; wraps
(looping) or clamps (non-looping) at the ends and sets `m_bSequenceFinished`; writes
`m_fSequencePastHalf +0x568`; then **every advance** writes `m_flYawSpeed +0x560 =
GetSequenceYawSpeed(m_nSequence)` and `m_flGroundSpeed +0x654 = GetSequenceGroundSpeed(m_nSequence)`;
`OnSequenceFinished` on the rising edge. `GetSequenceYawSpeed 0x10091310` *(read)* =
`GetSequenceTurnYaw / SequenceDuration` (0 for a zero duration); `GetSequenceTurnYaw 0x1008f8f0`
asks `FUN_10428690` with the live pose parameters and returns `angles[1]`: the pose-weighted sum,
over the up-to-four blend corners, of the last movement record's `angle` — the baked `YawDegrees`.
Every shipped record's angle is 0.0, so `GetSequenceTurnYaw` and `GetSequenceYawSpeed` answer **0
on shipped data**: `+0x560` is real after V4a and still 0 *(amended after V4r, 2026-10-04: R1
item 2; the "0 on shipped data" is the bake header's statement, not re-counted)*.
A second advance in one tick is inert (`dt = 0` fails `dt > *0x1044f020`), so the first caller in
the think owns the advance; `AutoMovement 0x10280a50` calls slot 250 first, unconditionally, and
the port's call there is right *(R2 item 8; the constant's value and the early-out's return are
unrecovered)*.
`GetSequenceGroundSpeed 0x10091490` = `GetSequenceMoveDist / SequenceDuration`, pose-weighted over a
blend fan, so **a fan's speed tracks the live `move_yaw` every tick** (`animation_and_movers.md`
§ "Scripted travel speed…" :1682-1688, § "One speed pipeline…" :1706) *(doc)*.
`GetIdealYawSpeed 0x100916a0` = `m_flYawSpeed` *(read)*; `GetIdealSpeed 0x10091740` = `+0x654`, no
playback-rate term *(doc)*. `ResetSequenceInfo 0x10090950` writes the same two words, playback rate
1.0, zeroes `m_flLastEventCheck`, and calls slot 247 *(doc; :1708-1716)*.

**The events.** Slot 258 on every NPC is `CBaseAnimatingOverlay::DispatchAnimEvents 0x10098c80`: the
base `0x10091880`, then the four overlay layers through `0x10098cd0` with their own cursors
(`animation_events.md` :41-102) *(doc)*. The base: window `[m_flLastEventCheck +0x658, m_flCycle +
0.1 × cycleRate)`, **0.1 s look-ahead**; `m_bSequenceFinished` cleared, then set when the look-ahead
end reaches 1.0 (non-looping: clamped to 1.0); `m_fSequencePastHalf`; the wrap swept once with
`STUDIO_LOOPING`; ids ≥ 5000 skipped; `+0x658` stores the look-ahead end; `OnSequenceFinished` on the
rising edge. So the finish an activity-waiting task reads is set by the dispatcher one look-ahead
early *(doc)*.

*(amended after V4r, 2026-10-04 — R2 item 1, the body the lanes port.)* The wrapper `0x10098c80`
calls the base `0x10091880`, then `0x10098cd0` on layers 0..3 (`+0x734`, stride `0x30`). **The
base:** `m_bSequenceFinished (+0x65c) = 0`; `flEnd = m_flCycle + 0.1 (0x104491b4) ×
GetSequenceCycleRate × m_flPlaybackRate`; non-looping (`m_bSequenceLoops +0x65d == 0`) with a
seqdesc: `flEnd >= 1 || flEnd < 0` → finished = 1 and `flEnd = 1.0`, else `m_fSequencePastHalf
(+0x568) = flEnd > 0.5`; looping: the same without the clamp, plus the start wrapped into `[0,1)`;
`m_flLastEventCheck (+0x658) = flEnd`; events with id `< 5000` in `[start, flEnd)` or, with
`seqdesc.flags & 1` and `flEnd >= 1`, `< flEnd − 1`; each to the HANDLER's slot 259 (`+0x40c`);
`eventtime = (cycle − m_flCycle) / rate + m_flAnimTime`. The interval argument is unused. With no
seqdesc on a non-looping sequence neither the finish nor past-half is written.
`OnSequenceFinished` is a direct call (`0x10091b9a → 0x10091c80`), not a slot, on the rising edge
(flag set now, clear at entry). **Per layer:** zeroes `layer+4` and never sets it; no past-half;
`layer+0x2c` = the layer's look-ahead end; no clamp; no "in use" or weight test; `eventtime` from
the owner's `m_flAnimTime`. **`StudioFrameAdvance`** only SETS the finish flag (cycle left in
`[0,1)`), writes past-half from the real cycle and the two speed words, and makes its own
rising-edge `OnSequenceFinished`; `ResetSequenceInfo` clears. **The finish flag a task reads**
(slot 251, `IsActivityFinished 0x10272900` = `m_bSequenceFinished && m_nSequence ==
m_nIdealSequence`): in a `RunTask` arm it is the dispatcher's look-ahead value from the previous
think's `PostRun` — true 0.1 s of clip time before the pose ends — unless a `ResetSequenceInfo`
cleared it since; an arm that calls `AutoMovement` first advances the clock itself and can add a
true, never remove one; within `PostRun`, `RunAnimation`'s own slot-251 test reads
`StudioFrameAdvance`'s value OR'd onto the previous look-ahead.

**Who dispatches** *(amended after V4r, 2026-10-04 — R2 extension 1)*. Slot 258 has four call
sites in the whole image: `PostRun`; `CBasePlayer::PostThink 0x1016be10`; the weapon's slot 369
`0x1024efa0`; `CCameraAnimated`'s think `0x10071840`. **Props never dispatch**: `CDynamicProp`'s
think `0x10190850` advances and never dispatches, and no other prop think does. The player:
`PostThink` runs slot 250 `(0)` → `0x101600a0` → slot 258 `(interval, this)` → slot 312
`UpdateCharacter`; its handler `CBasePlayer::HandleAnimEvent 0x10178a10` gates on `!IsObserver &&
source == this`, takes 4050, 4051 and 2060 itself, swallows 2050..2053, and passes the rest to
`0x1032e330`; slot 258 is the overlay body, so the player's layers dispatch too. The camera:
`0x10071840`, 10 Hz, advance → dispatch → the finish test on `m_bSequenceFinished`, base
dispatcher only, handler `CBaseAnimating::HandleAnimEvent 0x10091da0` (2070, 2071, 4005, else
`DevWarning`).

Each event goes to slot 259 `HandleAnimEvent`: base `0x10274e30`, Troika `0x1029b290`, species
bodies, `CBaseCombatCharacter::HandleAnimEvent 0x1032e330` (feed 4006/4007, 4020, ornaments),
and `3000..0xfa2` → `Weapon_HandleAnimEvent` → the weapon's `Operator_HandleAnimEvent +0x5c8`
(`conditions-and-states.md` :3504-3583, `animation_events.md` :207-240, :358-440) *(doc)*. The NPC's
ranged shot is the 3031 event, authored on 105 `move_and_ranged` sequences, through
`CWeaponRanged 0x10238160`; **no shipped sequence authors a melee commit** except the dog's bite on
3001, and 3047 (the NPC swing trigger) is authored nowhere (`combat-and-damage.md` :1027-1046)
*(doc)*. **The NPC attack producers** *(amended after V4r, 2026-10-04 — R2 item 3)*. Ranged: the
start `0x102a4505` sets the burst count only; the run `0x102ab0a9` → `0x102aaa60` writes
`m_flLastAttackTime = curtime` and `RestartIdealActivity(0x19)`; the event 3030..3044 →
`CWeaponRanged 0x10238160` → `0x10238320` → `ModeDispatch(1) 0x102383b0` → slot 373 `Shot
0x102387b0`. **No retail path fires a Troika human's shot without the event.** A second producer:
the move-and-shoot overlay `0x102e8560` fires from a LAYER's 3031 (J5: a stub in V4, below).
Melee: the start arm `0x102a45c6` (weapon `+0x5a0 & 0x18000`) writes `m_flLastAttackTime`, calls
the weapon's slot 326 `PrimaryAttack` (`CWeaponMelee 0x103eaca0`: owner in a grapple → return; no
player owner → slot 372 `RequestActivity(0x4b, 1, 1) 0x103e9e00` → the swing sequence on the owner
through slot 311, the playback rate, `m_flNextAttack`), then `AutoMovement`. **The contact has no
anim event**: it is `UpdateCharacter 0x103246d0` → `MeleeSwingUpdate 0x10346cd0`, reached from the
Troika think's tail `0x1029365b` — the NPC's own slot 312, after `PostRun` and `PerformMovement`,
on the update clock, not every think. *Not read:* the species weapon bodies (`0x103ed200`, the
flamethrower, thrown).
*(amended after S2, 2026-10-04 — `packets-S2.md` items 1 and 9; this replaces "Not read" above.)*
**Every operator body is read**: slot 370 has eight bodies in the image. The ranged body
`0x10238160` serves `CWeaponRanged` and all 16 subclasses, the flamethrower and the other species
classes included — the event and only the event; the base body `0x1024f030` (thrown, unarmed,
discipline) takes no event and has no weapon-side timer, the species' own task bodies being the
retail path; `0x103ed200` is the player's grenade release, not a species body. The melee operator
`0x103ea5b0` sends 3001, 3030..3037, 3039..3044 and 3047 to the weapon's `PrimaryAttack` when the
operator is not a player. The sweep `MeleeSwingUpdate 0x10346cd0` integrates **its own stamp**
(`m_flLastMeleeSwingUpdate +0xaa4`), not the caller's interval (slot 315 takes no argument), in
**`N = ceil(dt × 100)`** steps, so it covers the whole gap between due thinks.

**The attack extents.** Slot 247 `SetAttackExtentsForSequence 0x10090c80` *(read)*, called by
`ResetSequenceInfo`: only when `Flags2 & 4`; reads the sequence descriptor's bbox (`+0x1c..+0x30`),
takes the radial excess over the collision's maxs, and hands it to the entity's slot 15 (`+0x3c`).
*(amended after V4r, 2026-10-04 — R2 item 5.)* That slot 15 is the **entity's**,
`CBaseEntity::SetAttackExtents 0x1009af40` (collision `0x100dc220` + `+0x50`,
`m_vecAttackExtents`); the motor's slot 15 (`0x102e2180`, below) is another table.
`CAI_BaseNPCTroika::Spawn 0x10298d30` calls `AddFlag2(4)`, so **the slot is live on every Troika
NPC**: each `ResetSequenceInfo` re-derives the attack extents from the sequence bbox.
`RemoveFlag2(4)`: `CPayphone`, `CNPC_VHengeyokai`, `CNPC_VMingXiaoTentacle`, `CNPC_VTzimisce`,
`…HeadClaw`, `…Runner` (Spawn and vfunc130), `CNPC_VWerewolf`. The readers the judge names: the
sleep arms save `+0x50` through slot 16 (`0x102a29fa`, `0x102a710e` → `+0x65d0`) and `TaskFail` /
`OnScheduleChange` restore it. *Unrecovered:* slot 16's other readers; the body of `0x10090c80`
beyond the radial excess read here.
*(amended after S2, 2026-10-04 — `packets-S2.md` item 7; both "unrecovered" are settled.)* The
body, whole: `e[i] = max(|bbmin[i]|, bbmax[i])`; `e.x = e.y = sqrt(e.x² + e.y²)`; `e[i] = e[i] >
collision maxs[i] ? e[i] − maxs[i] : 0`; entity slot 15. Slot 16's readers are **saves only**
(`0x102a1910` twice, `0x102b7110`). The engine copy (the spatial partition element, `+0x28`)
widens an element's box only in a query carrying Troika's flag, and the only flagged queries are
the player's acquire cone (`0x1040f550`, `0x1040f080`). **So nothing in NPC AI reads the extents**:
not melee reach, not the sweep, not a condition. J2 went back to the judge on that and is
**re-ruled, J2b** (the second sitting): the import is withdrawn and filed with the story that
ports the acquire cone; slot 247's body stays in C1 on the named `SequenceBounds` seam and lands
writing nothing, proven by its arm test only (§8 Q4).

**Turning and the walk.** The facing-target queue (`shape.md` :419-457) and the yaw ladders (`shape.md`
:1473-1500) *(doc)*: `CAI_BaseNPCTroika::MaxYawSpeed 0x10297ce0` answers `|GetIdealYawSpeed()| ×
cvar` (floor 1.0) **only under `m_afMemory & 0x2000`** (the turn ladder's tag), otherwise
30/160/20/25/45 by activity. `FacingIdeal 0x10278c80` is `|DeltaIdealYaw| <= 0.006`. `CAI_Motor`
slot 18 `0x102e19e0` *(read)*: past the owner's `+0x838` test, if `m_nSequence` carries the
`move_yaw` pose parameter, the heading comes from slot 15 (the facing-queue average, `0x102e2180`),
the move is reissued at that quantised yaw, and `-(AngleDiff(heading, GetAbsAngles().y))` is written
to the owner's `m_flDesiredMoveYaw +0x63ec` (Troika) or as the `move_yaw` pose parameter.

*(amended after V4r, 2026-10-04 — R1, which replaces the paragraph above where they differ.)*
- **The motor's slot 15 `0x102e2180`** returns a float as well as the vector: the total interest
  `1 − Π(1 − wᵢ)`. An empty queue answers the zero vector and influence 0.0.
- **Slot 18 `0x102e19e0`** is the SDK's `CAI_Motor::MoveFacing`. Owner slot 526 (`+0x838`,
  `OverrideMoveFacing(move, m_flMoveInterval)`) true → return. `flMoveYaw =
  UTIL_VecToYaw(move.dir, move+0x0c)`. A sequence without `move_yaw` (`0x102e2820`):
  `SetIdealYawAndUpdate(AngleMod(flMoveYaw), −1)` (`0x102e1c10`). With `move_yaw`: `dir =
  facingDir·w + move.facing(move+0x18)·(1 − w)`, normalised, `SetIdealYawAndUpdate(
  AngleMod(VecToYaw(dir)), −1)`; then `−UTIL_AngleDiff(flMoveYaw, GetAngles().y)` to the owner's
  `m_flDesiredMoveYaw +0x63ec` when the Troika self-cast resolves, else
  `SetPoseParameter("move_yaw")`. With an empty queue the heading is `move.facing`, whole.
- **`move.facing` is `MoveGroundExecute`'s** (`CAI_HumanoidMotor` vfunc 19 `0x10264680`): it
  rebuilds the move script (`0x10262590` → the velocity script `0x102630b0`, the turn script
  `0x102627e0`), takes the yaw = `GetLocalAngles().y` or the turn script interpolated at
  `m_flMoveInterval` and `AngleMod`-quantised, copies the move, overwrites the copy's `facing`
  with `UTIL_YawToVector(yaw)` and calls slot 18 on the copy. The turn script is the direction
  from each waypoint to the next, rate-limited backwards (`_DAT_10457f60`): a walking NPC with no
  facing target faces along its path, eased through corners. Right after slot 18 it re-writes
  `+0x654 = GetSequenceGroundSpeed(m_nSequence)` (`0x10264841/46`).
- **How `GetIdealSpeed` becomes distance — the velocity script `0x102630b0`**: the ideal velocity
  is owner slot 248 (`GetIdealSpeed 0x10091740`; **50.0 when it answers 0**); acceleration = ideal
  + `_DAT_104493c0`; each waypoint's speed is `ideal × clamp(dot(in, out) + _DAT_10449198, 0, 1)`
  and **the last waypoint's is 0**; forward and backward passes limit by constant acceleration.
  `MoveGroundExecute` steps `(|m_vecVelocity| + flNewSpeed) × interval × 0.5`, clamps to
  `move.maxDist (+0x28)` as `MoveGroundStep 0x102e1760` does, writes `m_vecVelocity = move.dir ×
  flNewSpeed` and moves through `0x102e0bd0`. Retail accelerates to, cruises at, and decelerates at
  a constant rate from `+0x654`; the stop is finite.
- **`m_flDesiredMoveYaw +0x63ec`** has one reader, `0x102bf310` (`SetPoseParameter("move_yaw",
  +0x63ec, 0)`, dispatched; the port calls it from `Think19NormalSet2`). Writers: slot 18;
  `0x102a9940`; zeroed by `TaskFail 0x1029adb0`, `OnScheduleChange 0x102a0940`, `RunTask
  0x102aacf0`, `0x102bf770`, and two species bodies.
- **The turn ladder is closed for a Troika human**: `0x10297640` runs only under `debug_turning`
  (default 0) `|| m_bAllowTurningAnims +0x65f9`, which the Troika ctor writes 0 and no keyfield
  sets. So **`0x2000` is never tagged** on a human and the sentence above about the tag describes
  a path shipped humans do not take. `MaxYawSpeed 0x10297ce0`, untagged, in combat with
  `m_Activity` 1 or 5: `debug_turning_speed` = **90**; any other activity 45. `RunTask` 0x2e
  (`0x102889b5`) sets `m_flLastYawTime = −1` every call, so `UpdateYaw 0x102e1e20` integrates over
  0.1 s: 90° per call. A 135° `TASK_FACE_ENEMY` completes on the second `RunTask`, ≤ 0.2 s (0.3 s
  under the 45 reading): **the record's bound is 0.5 s**.
- **The walk speed is each body's own `walk_0`** *(R1 item 4, measured)*: the female bank's is
  101.278 cm/s (sentry2, `vampire_hunter_chick`), the male bank's 136.683 cm/s. "136.7" is the
  male cell, not a universal retail walk speed.
- *Unrecovered (reader R1b, `brief-R1b-slow-turn-reader.md`):* `_DAT_104493c0`, `_DAT_10449198`,
  `_DAT_10457f60`; the retail arrival tolerance. *Unrecovered, no owner yet:* `0x102e0bd0`; what
  slot 526 answers on the Troika line; `StartTask` 0x2e's "turn tail"; `AI_ClampYaw` (engine).
- *(amended after S1, 2026-10-04 — `packets-S1.md` items 5–7; this replaces the two bullets on
  the turn and the "Unrecovered" above where they differ.)* **`RunTask` 0x2e on a Troika NPC is
  `0x102aae61`, not the base `0x102889b5`.** It does not reset the yaw clock: `StartTask`'s turn
  tail `0x102a44d1` does (`m_flLastYawTime = −1`), once. `RunTask` calls `SetTurnActivity` every
  call unless `m_afMemory & 0x2000`, re-reads `MaxYawSpeed` every call, and `UpdateYaw
  0x102e1e20` integrates 0.1 s on the first call and the real time between thinks after it, at
  `speed × 10` through `AI_ClampYaw 0x102e1d10` (in `vampire.dll`). The 0.5 s bound stands.
  **The constants**: `0x104493c0` = 50.0 (acceleration = ideal + 50), `0x10449198` = 0.2 (the
  corner bias), `0x10457f60` = 150.0 (the turn script's degrees per second of segment time).
  **The velocity script is five passes** (prune, forward, backward with the same `dv > 0` test,
  cruise points with no guards, times), rebuilt every `MoveGroundExecute` from the body's current
  position and speed and **sampled at time `m_flMoveInterval`, not at the body's place on the
  path** — `brief-B1-body-speed.md` item 2 has it whole. **The arrival tolerance** is 0.0625
  units (0.119 cm), 2-D, a constant (`0x102ef510`); the body lands on the waypoint by clamping
  the step. The female `walk_0` is 37 frames at 33.82 fps: 101.278 cm/s stands. The turn
  script's two helpers are read (`packets-S4.md` f.1): `0x1013d450` is
  `UTIL_ApproachAngle(target, value, speed)` and `0x10262c20` the 0.01 s / 0.8 insert rule —
  `brief-B1-body-speed.md` item 5 has both. *Still unrecovered:* `0x102e0bd0`'s five trailing
  arguments; slot 526 on the Troika line (B2 reads it first); why the port's turn took 13.4 s
  (R1b).

**Slot 363 on the enemy.** `GatherEnemyConditions 0x10270b20` asks the enemy's combat character
`FInViewCone(this)` (`0x1027106c`): true → `ENEMY_FACING_ME 0x56`, else `BEHIND_ENEMY 0x57`
(ported, `ElysiumNpcBaseConditions2.cpp:503-513`) *(read)*. On the player that is the base
`CBaseCombatCharacter::FInViewCone 0x10326750` *(read)*: candidate slot 192 (`+0x300`, the point),
**this character's `m_flFieldOfView`**, the candidate's slot 29 (`+0x74`, its cone scalar), then
`FinViewCone3dNew 0x103264d0` (the 2-D body when the cvar at `0x10936f74` reads 2; it ships 3).
The Troika line writes `m_flFieldOfView +0x1574 = 0.2` at spawn (`0x10298de8`); the player's is 0.5
(`senses.md` :53-58) *(doc)*. *(amended after V4r, 2026-10-04 — R2 item 6.)* Slot 192 is
`0x10027160` on NPC and player: the collision box centre in world space. Slot 29: Troika
`0x101aa630` → `m_flStealthVisionCone +0x63c8`, which has no writer in the image → 0 (inferred);
the player `0x1034f390` → `+0x1c74`. The player's writer is `CBasePlayer::Spawn 0x1016d260`:
`m_flFieldOfView = 0.5`; slot 363 on `CHL2_Player` is the base `0x10326750`.

**The weighted pick.** `SelectWeightedSequence 0x1008dc40`: candidates across the three model slots
(the include-shadowing rule), then `RandomInt(0, total−1)` walked by `weights[i] <= r`; all-zero →
uniform; none → −1; one → itself. `SelectHeaviestSequence 0x1008dd30`: strict max, first wins
(`activity_enum.md` :294-337) *(doc)*.

**The disposition change.** `SetDisposition 0x102c0f70` (387 bytes), gated on `m_bDisableAI
+0x6080`: `m_IdealActivity = 0xf1`, `m_nIdealSequence +0x5ccc` from `CDispositionTable::
GetTransitionAnim 0x100ed150` (`stance_trans_<old>_<n>_<new>_<n>`, then `_1_…_1`, then the new
disposition's `idle[stance]`), `ResetSequenceInfo` (`animation_and_movers.md` :1906-1909)
*(draft)*. *(amended after V4r, 2026-10-04 — R2 item 7; this replaces "gated on `m_bDisableAI`":
the flag gates the immediate commit only.)* `old = +0x64d4`; the lookup `0x100ec530`, a miss →
`("Neutral", 1)` with `old = -1`; the tuning writes always (`+0x64d8`, `+0x6584/8`, `+0x5b94`,
`+0xe3c`, `+0x10b4`, `+0x64d0`, `+0x10b8`); if the index changed: `seq = old == -1 ? slot 611
(0x102c12a0) : GetTransitionAnim 0x100ed150`; if `seq >= 0`: `m_IdealActivity = 0xf1`,
`m_nIdealSequence = seq`; and if `!m_bDisableAI`: `m_nSequence = seq`, `m_flCycle = 0`,
`m_Activity = 0xf1`, `m_flAnimTime = curtime`, `ResetSequenceInfo`. `GetTransitionAnim`'s
fallbacks stay.

**Death.** `lifecycle.md` § "The death chain, kill to corpse" :2642-2800 *(doc)*: slot 144
`Event_Killed` → `0x1032b9b0` → slot 301 `CreateCorpse 0x1032c0e0` → `BecomeClientRagdoll
0x10090180`, which **seeds the pose with `SelectWeightedSequence(ACT_DIERAGDOLL 0x21)`, `m_flCycle =
0`, `ResetSequenceInfo`**, makes the NPC non-solid, clears its think and hands the pose to physics;
the drawn body falls, the entity stays at the death spot. Base `DIE` plays no death animation (a
recovered negative). The stealth kill reaches the same chain through `0x10165d90`.
*(amended after V4r, 2026-10-04 — J8 and a fresh read; `brief-D-ragdoll.md` § "What retail does"
is the corrected chain.)* The seed runs only for bone −1 (`0x1009021a`) and `CreateCorpse` passes
a real bone, so an ordinary corpse ragdolls from the pose it holds. `CreateCorpse 0x1032c0e0`
replaces the think at `0x1032c404` (`SUB_PVSRemove`, `curtime + 10.0`) on every ordinary arm, so
**an ordinary kill never reaches `SCHED_DIE`, rig or no rig**; the state-7 fork's
`BecomeClientRagdoll(vec3_origin, -1, 0)` (`0x1028a8ec`), the only caller that seeds
`ACT_DIERAGDOLL`, is reached only by other state-7 writers (unrecovered which; lane C2 reads them
first). `lifecycle.md` § "The ordered chain" steps 5–6 and § "A death sounds more than once" say
otherwise and are corrected by lane C2 before it codes.
*(amended after S1, 2026-10-04 — `packets-S1.md` items 1–4.)* The walk is done and confirmed, and
**S1 already rewrote `lifecycle.md` § "The ordered chain" steps 5–6** and listed the state-7
writers there; C2 keeps only § "A death sounds more than once" (once on an ordinary kill; three
times only on the rig-less fork route). The fork is the only **NPC** caller with bone −1
(`0x1012b370` and `0x102b5bb0` are not NPC paths); its retail reachers are the deferred script
death (`CineCleanup :0x2b22`), `0x1027d0a0`, the Werewolf and the zombie's collapse. **No step-2
record reaches the fork: step 2 needs neither the `ACT_DIERAGDOLL` seed nor `SCHED_DIE`**; both
stay ported arms reached by arm tests. The think after death differs per body — ordinary mortal
`SUB_PVSRemove` (view cone and PVS and `FVisible`), Kindred or `Has_Burning_Death` `BurnModel`
and `SUB_Remove` at +10 s, a pedestrian none (`CNPC_VPedestrian::CreateCorpse 0x103a38c0`,
**unwired in the port; C2 wires it**), spawnflag bit 9 `SUB_StartFadeOut` at +10 s. A rig-less
model has its bounds zeroed and is not made non-solid. Feed and explosion deaths are the ordinary
chain.

## 2. What the port does instead

| # | mechanism | port (today's line) | retail |
|---|---|---|---|
| M1 | slot 258 on the NPC chain is a counting stub; `PostRun` calls it | `ElysiumNpcBaseMotor.cpp:587-597` (`PostRun`, the call :595); stub `ElysiumAnimatingOverlaySlots.cpp:112` (`0x10098c80`), base `ElysiumAnimatingSlots.cpp:321` | `0x10098c80` / `0x10091880` inside `PostRun` |
| M2 | **events fire from the world tick's poll**, before every NPC thinks, reading the **visual pose layer's** clip phase, keyed by play id; no look-ahead, no `+0x658`, no finish / past-half writes, no `OnSequenceFinished` | `FElysiumEntityWorld::AdvanceAnimEvents` `ElysiumEntityWorld.cpp:2077` (called :1938, before `RunThinks` :1938-1943; loop :2091-2099); `FElysiumAnimating::AdvanceAnimEvents` `ElysiumAnimatingImpl.cpp:310-396` (phase :41-42, timeline :363, ≥5000 skip :370, `animevent` tap :373-377); rule `ElysiumAnimEvents.cpp:32-108` | §1 "The events" |
| M3 | `Weapon_FrameUpdate` is a counter | `ElysiumNpcBaseMotor.cpp:597` (`++PostRunWeaponUpdates`) | `0x1026c7c0` tail |
| M4 | `m_flGroundSpeed` / `m_flYawSpeed` never computed on the kernel; `GetIdealYawSpeed` answers 0. **(amended after V4r, 2026-10-04)** The claimed consequence — "so the turning arm floors at 1.0 (10°/s: the 13.4 s `task_face_enemy`)" — is refuted as the cause (R1 item 3): no shipped human is tagged `0x2000`, the port's `MaxYawSpeed` is arm-for-arm, and `+0x560` stays 0 on shipped data even when written. Why the port's turn took 13.4 s is **unread**: A0 measures it, reader R1b names the arm (J9) | `GroundSpeedCm()` seam `ElysiumNpcPositions2.cpp:81`; `GetIdealYawSpeed` stub `ElysiumAnimatingSlots.cpp:185-190`; comment `ElysiumNpcBaseAnim.cpp:229-230`; ladder `ElysiumNpcMotor.cpp:208-226`; a test pinning the stub `Tests/ElysiumNpcKernelMotorTests.cpp:246` | `0x1008f120`, `0x10090950`, `0x100916a0` |
| M5 | the walk speed is the **body's own**: the visual fan read at the body's measured `move_yaw` (velocity heading − actor yaw, slewed 720°/s); the kernel's `move_yaw` goes to its own record only; `0x102e19e0` is verdicted "mechanism" | `CommandedTravelSpeed` `Visual/ElysiumNpcBody.cpp:335-342` → `MaxWalkSpeed` :475-482; `Visual/ElysiumAnimationDriver.cpp:597-612, 663-666`; `ElysiumLocomotionSample.cpp:21-74`; kernel write `ElysiumNpcThink.cpp:343-344` → `ElysiumCombatCharacterSlotBodies.cpp:134-146`; verdict `kernel_verdicts.tsv:1466` | `0x102e19e0`, `+0x654` at the live `move_yaw` |
| M6 | **(amended after V4r, 2026-10-04 — R1 item 4, measured.)** N13 restated: **the walk speed is right and the time is lost at arrival.** Mid-leg sentry2 moves at a constant `speed2d` 101.278 cm/s, facing its path (`move_yaw_vel` 0) — exactly its own bank's `walk_0` (the female bank; 136.683 is the male cell, and the "0.44×" compared a female body with it). The body covers the leg in ~5.6 s, then creeps the last ~30 cm for ~4.1 s (distance × ~0.53 per 0.7 s) until it is inside the follower's 1 cm radius; only then does the kernel see `arrived` (10.428 s on `input_clearpatrolpath`). Mechanism, inferred from code and not toggled: the body is a `UCrowdFollowingComponent` agent, nothing calls `SetCrowdSlowdownAtGoal(false)`, and the acceptance radius is `max(FollowerArrivalFloorCm 1.0, 0.0625 units)` (`Visual/ElysiumNpcBody.cpp` `ApplyCrowdState`, `ResolveFollowerRequest`; `MakeNavigatorMoveRequest`, `ElysiumNpcBaseStartTask.cpp`). Retail's stop is the velocity script's (§1). The fix is J9's: B1 ports the whole profile; the slowdown toggle alone is a new divergence, not adopted. The other three N13 records were not run; `patrol_monk_loop`'s and the pedestrian's banks are unchecked (A0 checks them). *The draft's text, kept for the record:* N13: the walk covers ~0.55–0.7 m/s against 136.7 cm/s. Cause unread. The lead (a body yawed ~90° off its path by a facing target) is doubtful: a plain patrol adds no facing target (callers only `ElysiumNpcThink.cpp:221-225` `MOVE_FACE_ENEMY`, `ElysiumNpcTroikaHelpers.cpp:701-714`, `ElysiumNpcThinkSpecies.cpp:222`) and an empty queue orients to movement (`Visual/ElysiumNpcBody.cpp:107`); the kernel's ideal yaw is copied from the body during a move (`ElysiumNpcBaseMotor.cpp:1560-1563`, a named divergence) | §1 "Turning and the walk"; packet R1 |
| M7 | slot 363 on the combat character is a counting stub answering false; the cone body exists but compares against a constant 0.2, not the observer's `m_flFieldOfView` | stub `ElysiumCombatCharacterSlots.cpp:841-846`; body `ElysiumNpcSenses.cpp:378-391` (`DefaultViewConeDot`); slot 362 `ElysiumCombatCharacterSlotBodies.cpp:167-174`; the NPC's FOV word `ElysiumNpcLifecycle2.inl:118`, `ElysiumNpcSpawn.inl:92` | `0x10326750` |
| M8 | the sequence bridge: one clip per activity (`Variant = 0`, cached), so `SelectWeightedSequence` never draws; the loop bit is a guess (everything loops but the cine's `Play`), though the bake carries `STUDIO_LOOPING` | `SequenceForActivity` `ElysiumNpcAnim.cpp:375-391`; rows `SequenceRowFor` :321-340; `PlaySequenceClip` :394; loop guess `ElysiumNpcBaseAnim.cpp:65-75`; call `ElysiumNpcBaseStartTask.cpp:~400` | `0x1008dc40`, `0x1008dd30`; divergence row 4 (`stories/v1/divergences.md`) |
| M9 | `SetDisposition` plays the cross-disposition transition **directly on the body** (`PlayNpcClip`) or `ResetAnimToIdle`; no `m_IdealActivity` / `m_nIdealSequence` write | `FElysiumNpc::SetDisposition` `ElysiumNpc.cpp:1107-1177` | `0x102c0f70` |
| M10 | slot 247 a counting stub; the sequence bbox is not carried to the runtime. **(amended after V4r, 2026-10-04)** live on every Troika NPC (R2 item 5); the port's one live reader of the extents is the feed/use target box (`ElysiumNpcAccess::AttackBounds`), which stands on `TroikaNPCInit`'s `(-1,-1,-1)` for the NPC's whole life. J2: implemented in V4c, the bbox on the `UElysiumBodyData` row | `ElysiumAnimatingSlots.cpp:228-232`, caller `ElysiumNpcBaseAnim.cpp:135-138` | `0x10090c80` |
| M11 | the NPC shot commits either from the 3030–3044 event **or from a `ContactEventCycle` estimate timer (0.5 of the clip)** when the clip has none; the melee contact is swept by the world interaction tick | `ElysiumWeaponClasses.h:303` (`ContactEventCycle`), `ElysiumWeaponClasses.cpp:1263-1317, 2166, 2257, 2361`; swing start `ElysiumNpcStartTask.cpp:557, 852-862`; sweep `AdvanceMeleeSwings` `ElysiumEntityWorldInteraction.cpp:299-326`, `AdvanceSwingContact` / `MeleeContact` (`ElysiumWeaponClasses.cpp:2503, 2842`) | 3031 → `0x10238160`; the melee contact is the NPC's own slot 312 → `MeleeSwingUpdate 0x10346cd0` from the think's tail `0x1029365b`, with no anim event (R2 item 3; amended after V4r, 2026-10-04) |
| M12 | the corpse: the chain reaches `CreateCorpse` → `BecomeClientRagdoll` (`ElysiumNpc.cpp:285-311`), but **no `UPhysicsAsset` is baked for any character**, so `StartBodyRagdoll` refuses and `HoldBodyFinalPose` freezes the current standing frame; the `ACT_DIERAGDOLL` seed is unported (`ElysiumNpc.cpp:307`) | `ElysiumNpcBase.cpp:107`; refusal `ElysiumEntityBodies.cpp:1513-1529`; hold :1569; bake `pipeline/.../physics_data.py:5`, `ElysiumWorldServices.h:930` | `0x10090180`; the fall is 0014's (stories 1–3, 5) |
| M13 | the `on_ground` probe reads the motor capsule's `FindFloor`, and a dead body's actor collision is off (`SetActorEnableCollision(false)`, re-applied every dead think), so it reads **false for every corpse by construction** — a harness fault, not the game's | `Debug/ElysiumArenaScenarioRunner.cpp:667-680` → `ElysiumNpcBodyGeometry.cpp:120-125`; `Visual/ElysiumNpcBody.cpp:1250-1261, 1313`; `ElysiumNpc.cpp:646-649` | — |
| M15 | **(added after V4r, 2026-10-04)** the move-and-shoot overlay is a counter: `RunTaskOverlay` only counts (`ElysiumNpcBaseMaintain.cpp`, `++MoveAndShootOverlay.UpdateCalls`), so a gunman never fires on the move; `cover` is green against that | `ElysiumNpcBaseMaintain.cpp:~105` | `RunTaskOverlay 0x10289c90` → `0x102e8560` when slot 575 passes (enemy, flags2 `0x400`, weapon `& 0x6000`, `CAP_MOVE_SHOOT`): `AddGesture(TranslateActivity(0x1a))`, `Weapon_SetActivity(0x19)`, the `0x47`/`0x48` gestures; the shot from the layer's 3031. J5: stub in V4, red record `cover_move_shoot`, the stack filed to 0015, the wire to 0002 R3 **V4o ported it** (2026-10-04: `RunMoveAndShootOverlay`, the four layers, the event shot). |
| M16 | **(added after V4r, 2026-10-04)** the bridge's row 0 plays nothing, where retail plays the model's own sequence 0 at rate 1.0 on a `LookupSequence` miss (N19) | `ElysiumNpcAnim.cpp` `SequenceRowFor` / `PlaySequenceClip`; `ElysiumNpcBaseAnim.cpp` `ResetSequenceInfo` | `0x101a833d`, `0x10090a23`. J1: lane A2 |
| M14 | other counting stubs on the chain: `GetVelocity`, `BurnModel`, `AddExtraAnimationModels`, `SetPoseParameter02`, `GetGroundSpeedVelocity` | `ElysiumAnimatingSlots.cpp:168, 194, 211, 329, 357` | — |

**Already retail — reuse, do not rewrite.** `StudioFrameAdvance` (`ElysiumNpcBaseAnim.cpp:186-238`,
minus the two speed words and the overlay layers), `ResetSequenceInfo` (:85-140),
`IsActivityFinished` (:344-349), `RunAnimation`'s call order (:151-182, the pick still to port);
`HandleAnimEvent` base `ElysiumNpcBaseMisc2.cpp:204-431`, Troika `ElysiumNpcMisc2.cpp:385-594`,
species `ElysiumNpcMisc2Species.cpp:192-572`, combat character `ElysiumCombatCharacter.cpp:2104`,
weapon `ElysiumWeaponClasses.cpp:1349`; the facing queue, its blend and `FacingIdeal`
(`ElysiumNpcBaseFacing.cpp:36-188, 385-393`); `MotorUpdateYaw` (`ElysiumNpcBaseRunTask.cpp:183-209`);
the death chain through `CreateCorpse` (`ElysiumNpcSpawn.cpp:26`, `ElysiumNpcBaseSpawn.cpp:44-112`,
`ElysiumCombatCharacter.cpp:1429-1507`); `GatherEnemyConditions`' slot-363 call site.
**The bake already carries what V4 needs** except the bbox: events (`UElysiumClipData::Events`,
`ElysiumClipData.h:35`), activity and `actweight` (`FElysiumBodySequence`, `ElysiumBodyData.h:33-35`),
`STUDIO_LOOPING` (`ElysiumNpcClips.h:147`), ground speed and the fan
(`ElysiumClipData.h:38-40`, `Public/Visual/ElysiumBlendGrids.h:98-131`), the turn yaw
(`ElysiumClipMovement.h:56`, `YawDegrees`); a per-model sequence table (`UElysiumBodyData::Sequences`
→ `FElysiumNpcClipSet`, `ElysiumBodyData.cpp:21-29`, `ElysiumAnimSubsystem.cpp:234`); a retail-shaped
draw (`PickWeighted`, `ElysiumAnimationResolve.cpp:703-748`, hash-seeded). V4 is runtime work, except
§8 Q3 and Q4. *(amended after V4r, 2026-10-04)* The raw index is carried too: `rawIndex =
sequenceBase + the descriptor's index in its own .mdl` (`importers/body_data.py:83-89`), loaded as
`FElysiumNpcClip::RawIndex` (`Public/Visual/ElysiumNpcClips.h:68`), and none of the 1,430 staged
body tables lacks a `rawIndex 0` row (J1) — the comment at `Visual/ElysiumAnimationResolve.cpp`
("the character export writes no raw index") is stale. Q4's bbox is one pipeline change in V4c
(J2): `records.Seq` already carries `bbmin` / `bbmax` (`skeletal_stage/unit.py:234`) into the
`unit.sequences` that `body_data.project_body` walks.

## 3. The seam (method step 1) — one commit, opening V4a

Written and built by one agent (`brief-A0-seam.md`); changes no behaviour; every V4 record stays red.

| stub | retail | admitting default | stands for |
|---|---|---|---|
| `float FElysiumNpcBase::LastEventCheck` | `m_flLastEventCheck +0x658` | 0; zeroed where `ResetSequenceInfo` zeroes it (that write lands in A2) | the play-id cursor of the world poll |
| `float YawSpeed`, `float GroundSpeed` on `FElysiumNpcBase`; `GroundSpeedCm()` reads `GroundSpeed` | `m_flYawSpeed +0x560`, `m_flGroundSpeed +0x654` | 0 (today's answer) | `GroundSpeedCm()`'s seam `ElysiumNpcPositions2.cpp:81` |
| `bool SequencePastHalf` (if not already a word) | `m_fSequencePastHalf +0x568` | false | the named gap in `StudioFrameAdvance` |
| the bridge row's accessors `SequenceEvents(int32)`, `SequenceLoops(int32)` (exists as `bLoops`), `SequenceTurnYaw(int32)`, `SequenceGroundSpeedAt(int32, poseParams)` | the studio descriptor: events, `flags & 1`, `GetSequenceTurnYaw`, `GetSequenceGroundSpeed` | empty / today's loop guess / 0 / 0 | the clip resolver's metadata the bake already carries |
| `FElysiumNpcBase::DispatchAnimEvents(float, FElysiumEntity*) override` in a new `ElysiumNpcBaseAnimEvents.cpp` | slot 258 `0x10098c80` on the NPC chain | forwards to today's stub | M1 |
| `FElysiumCombatCharacter` gains `m_flFieldOfView +0x1574` (moved from the NPC side if it lives there; NPC spawn keeps writing 0.2) | `CBaseCombatCharacter +0x1574` | NPC 0.2 as today; the player's value is 0.5, written at `CBasePlayer::Spawn 0x1016d260` (R2 item 6) — **the write lands in A4**, which owns the player's file this wave; A3 ports the cone | the constant `DefaultViewConeDot` |
| shape-map rows `0x658`, `0x560`, `0x654`, `0x568`, `0x1574` | — | — | `ElysiumNpcKernelShapeMap.cpp` |
| harness **H18** probes `speed2d` (cm/s, the body's horizontal speed), `move_yaw` (the body's, degrees), `ground_speed` (the kernel's `+0x654`, cm/s) | — | — | N13's direct acceptance |
| harness **H19** `elysium_entity_get` adds, per NPC: facing-queue count and target, orient-to-movement on/off, `MaxWalkSpeed`, the kernel's `m_flDesiredMoveYaw` / `move_yaw` pose value and `+0x654`. **(amended after V4r, 2026-10-04)** plus `attack_extents` (`m_vecAttackExtents +0x50`, J2), the three yaw words (the `MaxYawSpeed` answer, `m_afMemory`, `m_Activity`, J9) and the driver's live `Selection.MoveYaw` (R1: `animation` / `next_animation` / `axis_fraction` are written only by a resolve and go stale during a continuous walk — the readout says so) | — | — | the N13 diagnosis behind R1's run; the slow turn's measurement |
| harness **H20** probe `corpse_on_floor` **(amended after V4r, 2026-10-04 — the spike)**: the `Bip01 Pelvis` **bone** of the drawn mesh (`GetBoneLocation`, readable under `-nullrhi`; never the component's location, which is below the floor at rest, and never the capsule) within the record's own height bound of the floor under it **and at rest: the pelvis speed under ~5 cm/s** — never the sleep state (the body never sleeps). The bound is per record's body, measured: with Unreal's default asset `regular_cop` rests at 16.2 cm and `bum_male` at 32.9 cm; the `.phy` asset's numbers are V4d's to measure. `on_ground` documented as the motor capsule (false on every corpse) | — | — | M13 |
| harness **H21** (the owner, K3): the `animevent` trace for every animating entity, `who: "player"` accepted | — | — | the K3 records |
| **(added after V4r, 2026-10-04)** `FElysiumSequenceWords` and `ElysiumAnimEvents::DispatchBase` / `DispatchLayer` declared in `ElysiumAnimEvents.h` (§ "Shared names"), bodies dispatching nothing; `FElysiumPlayer::PostThinkAnimation()` declared, empty, not called yet | `0x10091880`, `0x10098cd0`; `CBasePlayer::PostThink 0x1016be10` | no event, no write | so A1 (owns the dispatcher) and A4 (only calls it) compile against one signature |
| records (below) | — | — | — |

Records the seam writes or corrects (the integrator owns `Arena/` edits after the seam):
- **new** `world/anim_footsteps_walk.json`: sentry2 walking a two-point patrol (the
  `patrol_sentry2_pingpong` staging); `animevent` `2050` then `2051` on sentry2 during the leg (the
  walk clips carry them, `animation_events.md` :185-188), `never animevent` with an id ≥ 5000 on
  the NPC. A guard: may pass today through the poll; it must still pass once the poll is retired
  for NPCs.
- **new** `combat/face_enemy_turn.json`, red: a hostile Troika human, the player placed ~135° behind
  its facing, in combat; `task_face_enemy` then its `taskdone` **within 0.5 s** (`packets-R1.md`
  item 3: 90° per 0.1 s call from `MaxYawSpeed 0x10297ce0`, two calls for 135°). `known_red`
  "V4b". The seam agent also **measures** it (J9): the trace and the three H19 yaw words during the
  13.4 s turn, for reader R1b.
- **corrected (record error, bug protocol step 1)** `damage_lethal_death`, `verbs_stealth_kill`:
  the end probe `on_ground` → H20 `corpse_on_floor`; add `corpse` `match: "ragdoll"`. `known_red`
  "V4d (no character physics asset)".
- `known_red` retargeted: `sense_enemy_facing_me` → "V4a"; `ranged_open_fire` → "V4a (slot 363),
  then V5 (N2)"; the three patrols and `places_pedestrian_visit` → "V4b (N13)"; `melee_swing` →
  "V11 (N3)".
- **(amended after V4r, 2026-10-04 — J3, J4, J5, J7; `brief-A0-seam.md` has each in full.)**
  New: `anim_player_footsteps`; `anim_player_weapon_event` (two records: the firearm's 3031 before
  its `damage`; the melee's `never animevent` in 3000..0xfa2 with the `damage` landing);
  `anim_prop_event` as a non-vacuous **`never`** on `models/items/walkie_talkie/walkie_talkie.mdl`;
  `combat/cover_move_shoot.json`, red, `known_red` "0015 (the NPC overlay stack); the wire 0002
  R3". Record errors corrected with their retail source: `melee_swing` (`hit_event` → a `damage`
  on the player, `never animevent` on the brawler); `ranged_open_fire` (`shot_event` matches
  `3031`, plus `never` a `damage` on the player before it); the four N13 `known_red` texts (each
  body's own `walk_0`; the cause is the arrival, marked inferred until toggled). A deadline moves
  only where it was computed from 136.7 for a body on another bank. `script_walk_to_mark`
  (N19) is the A integrator's to re-measure, not the seam's.
- **(amended after S1–S3, 2026-10-04; `brief-A0-seam.md` has each in full.)**
  `damage_lethal_death` expects **no schedule after the death** (its `never` drops `DIE (` /
  `SCHED_DIE_RAGDOLL (` and forbids any `schedule` after `dies`). `face_enemy_turn` cites
  `RunTask` `0x102aae61`. `cover_move_shoot`'s `known_red` names the slot-575 seam ("V4o O2 …
  then V4o O1"), replacing "0015 …; the wire 0002 R3" above. `melee_swing` and **`chase_melee`**
  (added to the seam's corrected records) take the `_NR` schedules — `0xdd` in reach, `0xcb` out
  of reach (`0xd2` inside 100 units without `0x51`) — with one `MELEE_IDLE 0xc7` first being
  retail; `melee_swing`'s `hit_event` → `damage` as above.
- **(added by the judge's second sitting, 2026-10-04 — J10, J12, J13.)** Harness **H22**: the
  `removed` trace event kind (an entity leaving the entity world), if no such kind exists. Six
  new records written red, each `known_red` naming its lane: `corpse_removed_unseen`,
  `corpse_kindred_burns`, `corpse_pedestrian_stays` (C2), `corpse_fades` (C2),
  `ranged_sustained_fire` (V4o O3), `melee_ally_in_the_way` (V11-1). `brief-A0-seam.md` item 7b.

## 4. The cut

V4 cannot fit one story under rule 8: it needs eight coder lanes and two reading packets. Cut by
dependency — the clock and the frame advance with the events first; then the walk, which needs the
clock's ground speed; then attack and death, which need the events:

| story | size | agents | builds planned (allowed) | records turned green | stay red, on |
|---|---|---|---|---|---|
| **V4r** reading — **done 2026-10-04** | S–M | R1, R2 readers (+ the judge, one agent, after R2) = 3 | 0 (R1 ran one lab session on the existing build) | — (`packets-R1.md`, `packets-R2.md`, into `docs/vtmb/`; the rulings J1–J9) | — |
| **V4a** seam; the clock's speed words and row 0 (N19); the dispatcher in `PostRun`; slot 363; **the player's and the camera's dispatch (lane A4)** | M | seam agent, A1, A2, A3, **A4**, integrator = **6** | 2 (2) | `sense_enemy_facing_me`; `anim_footsteps_walk` (guard); `anim_player_footsteps`; `anim_player_weapon_event` (both); `anim_prop_event`; `script_walk_to_mark` (N19, re-measured); `cover`, `control_sequence` stay green | `ranged_open_fire` → V5 (N2) once its `shot_event` is met; `cover_move_shoot` → 0015 / R3 (J5) |
| **V4b** the walk's arrival and the turn (N13) | S–M | **reader R1b**, B1, B2, integrator = **4** | 1 (2) | `patrol_sentry2_pingpong`, `patrol_monk_loop`, `input_clearpatrolpath`, `places_pedestrian_visit`, `face_enemy_turn` | stop rule (J9): if the creep survives the measured toggle, the cause is unread again and B1 does not land |
| **V4c** attack producers and swing movement; slot 247 on its seam; shared animation pick, disposition, corpse clocks, enemy memory and team registry | M | C1, C2, C3, integrator = 4 | one `build --arm` (2 maximum); no import (J2b) | `chase_melee`, `melee_enemy_blocked`, `corpse_fades`, `corpse_pedestrian_stays`, `melee_same_team`, `team_damage_gate`, `ranged_enemy_dead` and its retarget control; S13's damage controls; the death transaction | `damage_lethal_death`, `verbs_stealth_kill`: `corpse_on_floor` → V4d; real reload → V5 |
| **V4d** the corpse falls: a physics asset from the `.phy`, Unreal's solve (the owner, 2026-10-04) | M | spike, coder, integrator = 3 | 1–2 (2), plus 2–3 scoped bakes | `damage_lethal_death`, `verbs_stealth_kill` (`corpse_on_floor`) | the death impulse, `prop_ragdoll`, the full corpus → 0014 |

Order V4r → V4a → V4b → V4c, with V4d any time after V4a. **19 agents** (V4r 3, V4a 6, V4b 4,
V4c 3, V4d 3 — rule 8's cap on V4a's six is the coordinator's to check, J3); 5–6 builds planned,
8 allowed. V4b and V4c could swap (V4c needs only V4a); the walk goes first because four records
and V8's hub wait on it. *(amended after V4r, 2026-10-04: A4 and R1b added.)*

**V4a — the frame** *(amended after V4r, 2026-10-04 — J1, J3, J4, J5; the file lists below are the
lanes' whole lists and are disjoint).* A1: the base dispatcher `0x10091880` and the per-layer body
`0x10098cd0` as **bodies over a small words struct** (`FElysiumSequenceWords`, § "Shared names")
so one body serves the NPC, the player and the camera; the overlay wrapper `0x10098c80` as the
NPC's slot 258 on the kernel's words, the event table from the bridge row, each event to slot 259
in order, the `animevent` tap inside the dispatcher; **the world poll deleted whole**
(`FElysiumEntityWorld::AdvanceAnimEvents`, `FElysiumAnimating::AdvanceAnimEvents`, the
`ElysiumAnimEvents::Advance` rule), one call left at the same place in the tick,
`FElysiumPlayer::PostThinkAnimation()`; props get nothing in its place (retail never dispatches
for a prop); the four overlay layers on the NPC a seam answering "no layer", named for
`CAnimationLayer` (`+0x734`, stride `0x30`) and for `0x102e8560` as its only step-2 pusher. A2:
`StudioFrameAdvance` and `ResetSequenceInfo` write `+0x560` / `+0x654` from the row;
`ResetSequenceInfo` zeroes `+0x658`; `GetIdealYawSpeed` / `GetIdealSpeed` as plain reads; the
row's loop bit from the baked `STUDIO_LOOPING`; **N19's row 0** — the embodiment accessor "this
body's `RawIndex 0` clip", row 0 resolved from it with the clip's own loop bit at rate 1.0. A3:
slot 363 as `0x10326750` over the observer's `m_flFieldOfView`, the candidate's slot 192 point and
slot 29 scalar. **A4:** the player's `PostThink` step (retail `0x1016be10`: advance → slot 258 →
slot 312) inside `PostThinkAnimation()`, the cycle read from the pose layer's phase as a named
seam (the player's own sequence clock is filed to 0015, layer 0); the camera's dispatch inside
`FElysiumCameraAnimated::Think` (`0x10071840`) with its finish from the dispatcher's flag; the
player's FOV write (0.5 at spawn, because A4 owns the player's file this wave);
`docs/vtmb/player-entity.md` § "Recovered `PostThink` body" corrected. **A1 owns the dispatcher
function; A4 only calls it**, by the signature in § "Shared names", which the seam declares.
- A1: `ElysiumNpcBaseAnimEvents.cpp`, `ElysiumAnimEvents.{h,cpp}`, `ElysiumAnimatingImpl.cpp`,
  `ElysiumEntityWorld.cpp` (the poll's function and its call site only), the three declarations
  (`Public/ElysiumAnimating.h`, `Public/ElysiumEntity.h` — `AdvanceAnimEvents` only —,
  `Public/ElysiumEntityWorld.h`), new `Tests/ElysiumNpcKernelAnimEventsTests.cpp`, and any test
  whose only subject is `ElysiumAnimEvents::Advance`.
- A2: `ElysiumNpcBaseAnim.{cpp,inl}`, `ElysiumAnimatingSlots.cpp` (slots 242, 248 only),
  `ElysiumNpcAnim.cpp` (the row accessors, row 0), `ElysiumNpcPositions2.cpp`,
  `Public/ElysiumWorldServices.h` (`IElysiumEmbodiment`: the one accessor),
  `Public/ElysiumMapActor.h` and `Private/Map/ElysiumMapActorEmbodiment.cpp` (its body),
  `Private/Tests/ElysiumTestServices.h` (the recording double), `Private/Visual/
  ElysiumAnimationResolve.cpp` (the stale comment only), `Tests/ElysiumNpcKernelAnimTests.cpp`,
  `Tests/ElysiumNpcKernelMotorTests.cpp` (the `:246` stub assertion).
- A3: `ElysiumCombatCharacterSlots.cpp` (slot 363 only), `ElysiumCombatCharacterSlotBodies.cpp`,
  `ElysiumNpcSenses.{h,cpp}` (the cone body's threshold), new
  `Tests/ElysiumCombatCharacterConeTests.cpp`.
- A4: `ElysiumPlayerEntity.cpp`, `Public/ElysiumPlayer.h` (the player's dispatch words only),
  `ElysiumGrapple.cpp` (`TickStealthKill`'s entry only), `ElysiumCameraAnimated.{h,cpp}`,
  `ElysiumFeed.cpp` (the feed timeline's hand-off to the dispatch and its comments only),
  `Tests/ElysiumNpcKernelPlayerControllerTests.cpp` (its `AdvanceAnimEvents()` calls), new
  `Tests/ElysiumPlayerPostThinkTests.cpp` and `Tests/ElysiumCameraAnimatedThinkTests.cpp`,
  `docs/vtmb/player-entity.md`.

**V4b — the walk's arrival and the turn** *(amended after V4r, 2026-10-04 — R1 and J9).* Reader
**R1b first** (no build): the three constants of the velocity and turn scripts, the retail arrival
tolerance, and the arm that makes the port's `task_face_enemy` take 13.4 s (from A0's measured
trace). B1: the body's commanded speed is the kernel's `GetIdealSpeed` shaped by **the whole
velocity script `0x102630b0`** (acceleration, corner speeds, the last waypoint's 0, the trapezoid
step) — the crowd follower's slowdown at goal goes off *because* retail's deceleration replaces
it; the toggle alone is a new divergence and is not adopted. No fan change. B2: the motor's slot
15 returns the influence, slot 18 blends the queue with `move.facing` by it, `move.facing` is
`MoveGroundExecute`'s yaw (the path direction, else the current yaw), `m_flDesiredMoveYaw =
−AngleDiff(moveYaw, yaw)`; and the slow turn's fix, final once R1b names its cause. The B
integrator's first act on its build is the measured toggle on `input_clearpatrolpath` (J9's stop
rule). *(amended after S1, 2026-10-04.)* **R1b is narrowed to the port's slow turn only; B1 does
not wait on it** — its constants, the five passes and the tolerance are in its brief. B2's
citation for the turn is the Troika arm `0x102aae61`. Whether `FollowerArrivalFloorCm` 1.0 cm
gives way to retail's 0.119 cm is a judge item, filed by the B integrator.
- B1: `Visual/ElysiumNpcBody.{h,cpp}`, new `Visual/ElysiumNpcMoveScript.{h,cpp}` (the velocity
  script as a plain function), `Public/ElysiumWorldServices.h` with its implementers
  `Public/ElysiumMapActor.h`, `Private/Map/ElysiumMapActorEmbodiment.cpp`,
  `Private/Tests/ElysiumTestServices.h` (the two accessors of § "Shared names"), new
  `Tests/ElysiumNpcMoveScriptTests.cpp`. `Visual/ElysiumAnimationDriver.cpp` and
  `Visual/ElysiumLocomotionSample.cpp` are read, not edited.
- B2: `ElysiumNpcMotor10.{cpp,inl}`, `ElysiumNpcBaseFacing.cpp`, `ElysiumNpcBaseMotor.cpp` (the
  move's ideal-yaw copy, `:1560-1563`), `ElysiumNpcThink.cpp` (the `move_yaw` write, `:343-344`),
  `Tests/ElysiumNpcKernelFacingTests.cpp`, `Tests/ElysiumNpcKernelMotorTests.cpp`, plus the one
  file R1b names for the slow turn if it is none of these and not B1's (nor V11-1's, when V11
  shares the wave: `../v11/README.md` §4).

**V4c — C1, C2, C3, then one integrator** *(final after S13 and the owner's rulings,
2026-10-04; V11 and V4o are landed).* C1: event-only NPC attack producers, weapon frame,
swing as the kernel row with interval movement (`chase_melee`), own-clock slot315 sweep, D9
endpoints, same-team contact and slot247's arm-tested body on the false SequenceBounds seam.
C2: one weighted/heaviest candidate body, NPC and non-NPC animation picks on **NpcSchedule**,
disposition, corpse seed/pedestrian/fade thinks, BestEnemy liveness and memory fidelity, released
feed fallback and player PostThink/registration/damage hooks. C3: combat-character TeamName and
uint16 symbol, normalized registry cleared at both level boundaries, spawn/restore registration,
getter/SameTeam/TeamFilter and four-reader recovery. Real reload stays V5; corpse physics stays
V4d; bbox import/re-bake is withdrawn here (J2b). These briefs supersede the older shared-name
notes where amended.

The following are the **whole, disjoint coder manifests** (braces expand); function scopes are
in the linked briefs. Prefix is `Source/ElysiumUE/Private/Substrate/` unless stated.

- **C1** ([brief-C1-attack-producers.md](brief-C1-attack-producers.md)):
  `ElysiumNpcBaseMotor.{cpp,inl}`;
  `ElysiumWeaponClasses.{h,cpp}`;
  `ElysiumEntityWorldInteraction.cpp`;
  `ElysiumNpcStartTask.cpp`;
  `ElysiumNpcThink.{cpp,inl}`;
  `ElysiumCombatCharacterSlotBodies.cpp`;
  `ElysiumAnimatingSlotBodies.cpp`;
  `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelMotorTests.cpp`;
  `Source/ElysiumUE/Private/Tests/ElysiumWeaponTests.cpp`;
  `Source/ElysiumUE/Private/Tests/ElysiumNpcAttackExtentsTests.cpp`;
  `docs/vtmb/combat-and-damage.md`.

- **C2** ([brief-C2-pick-disposition-corpse.md](brief-C2-pick-disposition-corpse.md)):
  `ElysiumNpcAnim.cpp`;
  `ElysiumNpc.h`;
  `ElysiumNpcBaseAnim.cpp`;
  `ElysiumNpcBaseStartTask.cpp`;
  `ElysiumNpc.cpp`;
  `ElysiumNpcBaseRunTask.{cpp,inl}`;
  `ElysiumNpcBaseSpawn.{cpp,inl}`;
  `ElysiumCombatCharacter.cpp`;
  `ElysiumNpcPedestrian.{h,cpp}`;
  `ElysiumPlayerEntity.cpp`;
  `ElysiumFeed.cpp`;
  `ElysiumNpcBaseSenses10.cpp`;
  `ElysiumNpcBaseSenses.cpp`;
  `ElysiumNpcEnemyMemory.{h,cpp}`;
  `ElysiumNpcBaseConditions2.cpp`;
  `Source/ElysiumUE/Private/Visual/ElysiumAnimationResolve.cpp`;
  `Source/ElysiumUE/Private/Visual/ElysiumAnimationPick.{h,cpp}`;
  `Source/ElysiumUE/Private/Player/ElysiumAnimationIntent.cpp`;
  `ElysiumProp.cpp`;
  `Source/ElysiumUE/Private/Tests/ElysiumPlayerPostThinkTests.cpp`;
  `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelSpeciesMisc10Tests.cpp`;
  `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelAnimTests.cpp`;
  `Source/ElysiumUE/Private/Tests/ElysiumNpcCombatTests.cpp`;
  `Source/ElysiumUE/Private/Tests/ElysiumNpcEnemyTests.cpp`;
  `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelSenses10Tests.cpp`;
  `docs/vtmb/npc-ai/lifecycle.md`;
  `docs/vtmb/npc-ai/senses.md`;
  `docs/vtmb/feeding.md`.

- **C3** ([brief-C3-team-registry.md](brief-C3-team-registry.md)):
  `Source/ElysiumUE/Public/ElysiumPlayer.h`;
  `Source/ElysiumUE/Private/Substrate/ElysiumTeamRegistry.{h,cpp}`;
  `Source/ElysiumUE/Private/Substrate/ElysiumCombatCharacterTeam.cpp`;
  `Source/ElysiumUE/Private/Tests/ElysiumTeamRegistryTests.cpp`;
  `docs/vtmb/npc-ai/teams.md`.

**Disjointness:** C1 ∩ C2 = C1 ∩ C3 = C2 ∩ C3 = ∅. No shared coder file. C1 owns
ElysiumWeaponClasses.cpp: C2 supplies K4's exact patch and C1 applies it. C2 owns
ElysiumCombatCharacter.cpp / ElysiumNpcBaseSpawn.{cpp,inl} / ElysiumPlayerEntity.cpp: it applies
C3's damage/spawn/player hooks. C3 owns Public/ElysiumPlayer.h's team declarations. The integrator
applies reported world/restore/embodiment/generation lines serially after all three reports;
retargets kernel_verdicts.tsv and regenerates, never hand-edits generated Slots.cpp/bindings;
writes the S13 records; reviews all diffs for C4458/C4459, includes and double definitions;
builds once with --arm (two maximum), runs named records, default, arm, full arena once after
the last build, then one explicitly staged commit with verdict table, never pushes. No report*.md.

## 5. Reading packets (method step 4) — V4r, before any coder

| packet | what must be recovered | size | feeds |
|---|---|---|---|
| **R1** the walk | (1) slot 18 `0x102e19e0` (read above) with slot 15 `0x102e2180` on an **empty** queue: what heading it answers; who calls slot 18 (`CAI_HumanoidMotor` vfunc19 `0x10264680`, `MoveGroundExecute`) and the second `+0x654` write `0x10264841/46`; how the step turns `GetIdealSpeed` into distance (`MoveGroundStep 0x102e1760`); the readers of `m_flDesiredMoveYaw +0x63ec`. (2) `FUN_10428690`: which movement field `GetSequenceTurnYaw` returns (is it the baked `YawDegrees`?). (3) `face_enemy_turn`'s bound: the activity during `TASK_FACE_ENEMY` in combat, the turn ladder `0x10297640`'s pick and tag, the turn clip's yaw speed on `regular_cop`, the resulting seconds. (4) **One diagnostic lab session** (`uv run elysium gr --arena --headless`, `elysium.gr_scenario patrol_sentry2_pingpong`): `elysium_entity_get sentry2` ≥ 5 samples mid-leg — `speed2d`, `facing_yaw`, `locomotion.move_yaw_vel`, `move_yaw_wish`, `animation`, `axis_fraction` — and the walk fan's cells for sentry2's model. Verdict on the lead: `move_yaw_vel ≈ ±90` with `speed2d ≈ 60` holds it; `≈ 0` with `≈ 60` refutes it and points at the fan's cells or scale | ~1.5 KB of listing + one session | B1, B2, the seam's `face_enemy_turn` |
| **R2** the chain | (1) `0x10098c80` / `0x10098cd0`: order base → layers, the finish / past-half writes against `StudioFrameAdvance`'s, `OnSequenceFinished`'s slot; with `NPCThink`'s order (tasks, then `PostRun`), which finish value slot 251 reads. (2) `Weapon_FrameUpdate`: what it does for an NPC. (3) The NPC shot: `TASK_RANGE_ATTACK1`'s arms → the activity → 3031 → `CWeaponRanged::Operator_HandleAnimEvent 0x10238160` → the fire body; any retail path that fires without the event. The NPC melee: how `TASK_MELEE_ATTACK1` starts a swing (3047 is authored nowhere) and where the per-frame contact sweep runs; whether `melee_swing`'s `hit_event` (`animevent` within 3 s of the swing) is retail — a record error if no event is authored on the swing. (4) Who pushes an NPC overlay layer on the step-2 records' paths. (5) `0x10090c80`'s slot 15 (`+0x3c`) and the writers of `Flags2 & 4`. (6) Slot 363's inputs: slot 192 and slot 29 on an NPC candidate; the player's `m_flFieldOfView` writer and value. (7) `SetDisposition 0x102c0f70` arm by arm; `RunAnimation 0x1026c540`'s pick (read here) into the doc. (8) `AutoMovement`'s second `StudioFrameAdvance(0)` (`ElysiumNpcBaseMotor.cpp:564`): retail or not | ~3 KB | A1, A2, A3, C1, C2 |

Everything else V4 ports is walked in `docs/vtmb/` (§1). Then **the judge** (`brief-J-judge.md`)
rules on §8 Q3 (the corpse fall) and, if R2 shows slot 247 live, Q4 (the bbox) — before V4a's seam,
because the seam writes the death records' `known_red`.

*(amended after V4r, 2026-10-04.)* **Both packets and the rulings have landed**: `packets-R1.md`,
`packets-R2.md`, `stories/v1/triage.md` § "Judge's rulings, V4" (J1–J9). What they left unread is
one more short read, **R1b** (`brief-R1b-slow-turn-reader.md`, J9), before B1/B2 start:

| packet | what must be recovered | size | feeds |
|---|---|---|---|
| **R1b** the slow turn and the stop | (1) from A0's measured `face_enemy_turn` trace and H19 words, the arm that makes the port's `task_face_enemy` take 13.4 s. (2) `_DAT_104493c0`, `_DAT_10449198`, `_DAT_10457f60`. (3) The retail arrival tolerance against the port's 1 cm follower floor | < 1 KB of listing, no build, no run | B1, B2 |

*(amended after S1, 2026-10-04.)* R1b's (2) and (3) are settled by `packets-S1.md` (50.0, 0.2,
150.0; 0.0625 units 2-D) and so is retail's side of (1). **R1b now reads only the port's side of
(1)** and feeds B2 item 4 alone.

## 6. Tests

Deleted, not converted (port-only, stub or seam tests), each by the lane that owns its file:

| test | file | why | lane |
|---|---|---|---|
| the assertion "`GetIdealYawSpeed()` answers 0, so it lands on 1.0" | `ElysiumNpcKernelMotorTests.cpp:246` | pins a stub | A2 |
| `NpcCombat.Death`'s `StartBodyRagdoll → 0` / `HoldBodyFinalPose` / "no `PlayNpcClip`" assertions (the rest of it, if anything retail remains, stays) | `ElysiumNpcCombatTests.cpp:1629-1661` | pins the no-rig mechanism | C2 |
| any test of `ElysiumAnimEvents::Advance` (amended after V4r, 2026-10-04: the rule is deleted with the poll, for every entity) | A1 audits `Tests/` by Grep | pins the poll | A1 |
| the weapon tests' `ContactEventCycle` estimate assertions on an NPC wielder (J6) | `ElysiumWeaponTests.cpp` | pin a port mechanism | C1 |
| any test pinning one clip per activity on the bridge | C2 audits by Grep on `SequenceForActivity` | pins the divergence | C2 |
| slot 258 / 247 / 363 stub-count assertions (`FireAnimatingOverlaySlot` / `FireCombatCharacterSlot` counts) | the closure / dispatch tests, by Grep | pins a stub; the dispatch-table rows (slot → body name) stay | A1, A3, C1 |

Replaced by the records of §4 and these arm tests, each pinning a retail address:
`Elysium.Arm.NpcKernelAnimEvents.Window` (`0x10091880`: closed-bottom open-top, look-ahead 0.1 ×
rate, the wrap once with `STUDIO_LOOPING` and lost without it, ≥ 5000 skipped, 1.0 never fires on a
non-looping clip, `ResetSequenceInfo` restarts at 0, finish and past-half written, `OnSequenceFinished`
once); `.PostRunOrder` (`0x1026c7c0`: `RunAnimation`, slot 258, `Weapon_FrameUpdate`);
`Elysium.Arm.NpcKernelAnim.SpeedWords` (`0x1008f120` / `0x10090950` write `+0x560` and `+0x654`;
`0x100916a0` / `0x10091740` plain reads); `.WeightedPick` and `.HeaviestPick` (`0x1008dc40`,
`0x1008dd30`); `.RunAnimationPick` (`0x1026c540`'s loop-bit fork); `.DieRagdollSeed` (`0x10090180`);
`.SetDisposition` (`0x102c0f70`); `Elysium.Arm.CombatCharacter.FInViewCone` (`0x10326750` with the
player's FOV and the NPC's scalar); `Elysium.Arm.NpcKernelMotor.MoveYaw` (`0x102e19e0`).
*(amended after V4r, 2026-10-04)* `.RunAnimationPick` pins R2's gate (`m_Activity == 1`, the pick
on `m_TranslatedActivity`); `.DieRagdollSeed` states who reaches the bone −1 arm. Added:
`Elysium.Arm.NpcKernelAnimEvents.Layer` (`0x10098cd0`, A1); `Elysium.Arm.NpcKernelAnim.SequenceZero`
(`0x101a833d` / `0x10090a23`, A2); `Elysium.Arm.Player.PostThinkOrder` (`0x1016be10`) and
`Elysium.Arm.CameraAnimated.ThinkOrder` (`0x10071840`), both A4;
`Elysium.Arm.NpcKernelMotor.VelocityScript` (`0x102630b0`, B1);
`Elysium.Arm.NpcKernelAnim.AttackExtents` (`0x10090c80`, C1).

## 7. Kept divergences (rule 2), for the owner

- **K1 (existing, kept): the motor runs on the body's tick** under Unreal's movement component
  (0019/6). After V4b its contract is retail's: the speed input is the kernel's `GetIdealSpeed`
  (`+0x654` at the kernel's `move_yaw`), the heading the kernel's; the event order is untouched
  because the motor fires no kernel event. *(amended after V4r, 2026-10-04 — J9.)* "Retail's
  input" covers the **whole speed curve**, not only the cruise speed: B1 ports the velocity script
  `0x102630b0` as the body's commanded speed, and Detour's slowdown at goal goes off because
  retail's deceleration replaces it. **Slowdown off with no retail deceleration is a new
  divergence: recorded here, not adopted.** Whether the port's 1 cm follower floor is retail's
  arrival tolerance is unverified (R1b). *(amended after S1, 2026-10-04.)* It is **not**:
  retail's is 0.0625 units (0.119 cm), 2-D (`0x102ef510`), and the body lands on the waypoint by
  the clamped step. Keeping 1.0 cm as a named divergence of the crowd follower or replacing it
  is **for the judge**; B1 leaves the value and states it at the line.
- **K2 (existing, kept, named modernization): the clip resolver stands for the studio sequence
  table**, and events come from the baked table, never from Unreal notifies. V4 narrows it: the
  kernel's sequence rows carry the baked flags, weights, events and speeds, so only the asset
  lookup remains a divergence.
- **K3 — NOT a divergence (the owner, 2026-10-04): the world poll goes for every entity, not only
  NPCs.** Retail can be followed here, so rule 2 does not allow keeping it: retail's dispatcher is
  `CBaseAnimating::DispatchAnimEvents` (slot 258, `0x10091880`), run from each entity's own think,
  for the player (the 4050/4051 camera band), props and the camera alike. Packet R2 recovers where
  each of those entities calls it and in what order against its frame advance; lane A1 then moves
  them onto the same dispatcher as the NPC and the world-tick poll is deleted whole. If R2 shows
  one of them needs substrate another spec owns, that part alone goes to the judge.
  *(amended after V4r, 2026-10-04 — R2 extension 1, J3, J4; this replaces "props … alike" above.)*
  **Retail props never dispatch**: `CDynamicProp`'s think `0x10190850` advances and never
  dispatches, and slot 258 has four call sites in the whole image, none a prop. So the prop poll
  is **deleted with no replacement and no seam** — there is no retail input to stand for; the
  record `anim_prop_event` states the negative on a model whose clip authors an event. **The
  player and the camera dispatch** from their own thinks on the NPC's dispatcher bodies (lane A4):
  the player in `PostThinkAnimation()` at the tick's `PostThink` point — retail `0x1016be10`:
  advance → slot 258 → slot 312 — and the camera inside its think (`0x10071840`). One residue,
  named at its line and listed for the owner, not adopted as new: the player has no kernel
  sequence words, so its cycle is **read from the pose layer's phase**, a seam that "stands for
  `m_flCycle` until the player's `StudioFrameAdvance` is ported" — the player's kernel sequence
  clock is filed to **0015, layer 0**. The retail look-ahead moves every player shot's commit and
  the camera's end 0.1 s of clip time earlier: retail's timing, triaged under §8 Q5, never
  loosened.
- **K4 — NOT a divergence (the owner, 2026-10-04): the visual side's `PickWeighted` hash seed**
  (`ElysiumAnimationResolve.cpp:723`). Retail's pick is `SelectWeightedSequence` on
  `CBaseAnimating`, drawn on the game's random stream, for every animating entity. Packet R2
  recovers which retail body makes each pick the visual layer makes today and which stream it
  draws on; lane C2 then routes them through the same weighted draw as the kernel's. A pick with no
  retail counterpart (a purely Unreal-side blend choice) is listed by name for the owner.
  *(amended after V4r, 2026-10-04 — R2 extension 2.)* Every retail pick is
  `SelectWeightedSequence 0x1008dc40` → `0x10427fc0`, `RandomInt(0, total−1)` on the one shared
  engine stream (`*0x1070b244` slot 2). The port's `PickWeighted` has one production call
  (`TryActivity`, `ElysiumAnimationResolve.cpp:187`); what differs per site is who sets `Variant`
  — `packets-R2.md` § "Extension 2" has the table of seven sites and their retail bodies, and C2
  routes each as that table maps it. **No pick without a retail counterpart was found**; the hash
  seed and the driver's variant-keyed cache are the port-only parts. *Not walked:* the 54 retail
  callers one by one. *Not ruled:* which port stream stands for the shared engine stream at the
  non-NPC sites (C2 proposes, the owner decides).
- **K6 — a stub with a red record, not a divergence (J5, 2026-10-04): the move-and-shoot overlay
  `0x102e8560`.** A gunman running to cover fires from a layer's 3031 in retail; the port counts
  the call and pushes nothing. V4 keeps the counter and A1's "no layer" seam, named; the red
  record `cover_move_shoot` holds the debt; the NPC overlay stack is filed to 0015 and the wire to
  0002 step 3 (R3). Stated to the owner: step 2's `cover` family cannot prove the run-and-gun, and
  gate 2's claim for cover is re-cut to "reaches cover and fires from it".
  *(amended after S3, 2026-10-04.)* The debt is taken by story **V4o** (`../v4o/README.md`),
  after V4b. The record's first cause is not the missing layers: the port's slot 575
  (`FElysiumNpc::ShouldMoveAndShoot`) reads `ActiveWeaponCapabilityWord()`, a seam answering 0,
  so the overlay never arms (`packets-S3.md` item 4.1; V4o lane O2). Not V5.
- **K5 — a named modernization (the owner, 2026-10-04): the corpse's fall is Unreal's.** The fall
  is solved by Chaos with no calibration against VtMB's simulation; the bodies, masses and joint
  limits come from the game's `.phy` data; what game logic observes (`OnDeath` on the kill tick,
  the `corpse` event, the entity frozen at the death spot, its removal) stays retail's. Retail's
  own ragdoll is client-side and nothing the bytecode reads depends on where the drawn body rests.
  Story **V4d**, `brief-D-ragdoll.md`. The first text's "rigless corpse holding the
  `ACT_DIERAGDOLL` seed pose" is withdrawn: that seed runs only for bone -1 (`0x1009021a`) and
  `CreateCorpse 0x1032c0e0` always passes a real bone, so an ordinary corpse ragdolls from the pose
  it holds.
- Not V4's, named: anim-event sounds (`EmitSoundScriptMisc19` appends to a list,
  `ElysiumNpcBaseMisc2.cpp:198-202`), the 2042–2044 weapon-model counters (`:390-411`), the 2020
  bone controller — audio and weapon-model stories; the four overlay layers (a seam, A1).

## 8. Risks and open questions, each with a recommendation

- **Q1. The cut** into V4r + V4a–c. *Recommend accept*: each closes on its own records; rule 8.
- **Q2. N13's cause is unknown** (§2 M6). R1's session settles it before B1/B2 are final. If the
  cause is the baked fan (cells, units, symmetrization in `ElysiumBlendGrids`), the fix is pipeline
  + a character re-import: **for the adversarial judge** — cost, from the recorded logs: a full
  character import 1,434 s (1,207 entries, ~17.7k assets,
  `$ELYSIUM_WORK_ROOT/logs/20260928T205853.840641Z-import-characters.json`), a partial ~1,172 s.
  *Recommend*: if the fan is wrong, implement now (four records and V8's hub depend on the walk).
  **(amended after V4r, 2026-10-04) Moot**: R1 measured the cruise speed right and no baked cell
  or scale at fault; the cause is the arrival (§2 M6), fixed in V4b under J9. No re-import.
- **Q3. The corpse fall — settled by the owner, 2026-10-04: story V4d, size M** (`brief-D-ragdoll.md`).
  The first estimate (0014/1–3 + 5: M + M + L + XS and a 24-minute re-import) was re-validated by
  three agents and was too large: the `.phy` is decoded and already baked as one data asset per
  model (621, read by nothing); the handoff exists and only lacks a physics asset; the two
  calibrations are not needed under the owner's ruling; the bake step has its own fingerprint, so
  no mesh or animation is re-imported. What it missed: a dead body switches its whole actor's
  collision off every think (`ElysiumNpcBody.cpp:1313`), so a ragdoll would fall through the floor.
  `damage_lethal_death` and `verbs_stealth_kill` keep `corpse_on_floor` and go green in V4d. Not
  for the judge any more. **(amended after V4r, 2026-10-04 — `packets-spike.md`.)** The spike
  refuted the last point: the drawn mesh is the map actor's component, only attached to the
  motor, so the dead body's collision switch does not reach it — the body fell and rested with
  no handoff or collision change. The collision fix is withdrawn; the rest test is a speed
  threshold and the height bound is per body (`brief-D-ragdoll.md` § "After the spike").
- **Q4. Slot 247 needs the sequence bbox**, which the bake drops (`clip_data.py:13-25`). If R2 shows
  `Flags2 & 4` set on any NPC class, **for the judge**: the bbox on the `UElysiumBodyData` row
  (re-authors the `DA_` assets; unverified whether that avoids re-cooking the animation packages) or
  in `clip_data.py` (changes the animation fingerprint: full 1,434 s re-import). *Recommend*: leave
  the named seam unless a step-2 record observes a hit the extents decide.
  **(amended after V4r, 2026-10-04) Ruled, J2: implement now.** The slot is live on every Troika
  NPC. `bboxMinCm` / `bboxMaxCm` on each `body_data.py` row (centimetres, the Y reflection);
  `FElysiumBodySequence` and `FElysiumNpcClip` carry them; C1 ports `0x10090c80` whole against the
  bridge accessor `SequenceBounds(int32)`. The body data has its own fingerprint
  (`pipeline/unreal/import_characters.py:478`), separate from the animations', so only the `DA_`
  assets re-author. The C integrator runs the one import (~20 min, unmeasured). **Stop rule:** if
  the import reports any animation package rebuilt rather than reused, stop and file the data half
  for V4d's bake window; the body then stands on the accessor answering none. No step-2 record
  observes the extents — stated, not hidden: the proof is the arm test and a live read of
  `attack_extents` on `ranged_open_fire`'s shooter.
  **(amended after S2, S4 and the judge's second sitting, 2026-10-04) Re-ruled, J2b: J2's import
  is withdrawn.** `packets-S2.md` item 7 and `packets-S4.md` item c close the reader list: the
  extents are only saved and restored in game code, and the engine copy widens only the
  player's acquire cone (`0x1040f550`, `0x1040f080`) — which is not ported, so the port has no
  observer: no record, no arm test on real data and no live read can tell a right value from a
  wrong one. **Now, C1**: slot 247's body `0x10090c80` whole, against `SequenceBounds`, a
  **named seam** answering "no descriptor" ("stands for the seqdesc bbox `+0x1c..+0x30`; filled
  when the acquire cone is ported"); the slot writes nothing, as retail with no seqdesc. Proof:
  the arm test `Elysium.Arm.NpcKernelAnim.AttackExtents` on a double; no arena record, stated.
  **Filed with the story that ports the acquire cone** (`spec.md` carries the line): the two
  row fields, the runtime fields, the one import, and the port's feed-target box growing by
  the extents (`ElysiumMapActor.cpp:~1513-1518`), not changed now. No pipeline run in V4c.
- **Q5. The finish moves one look-ahead earlier.** With the dispatcher writing `m_bSequenceFinished`,
  every activity-waiting task completes ~0.1 s × rate earlier — retail's timing. Records with tight
  windows may move. *Recommend*: the V4a integrator runs the whole arena at close; a moved verdict is
  triaged under the bug protocol, never loosened.
- **Q6. Removing the `ContactEventCycle` estimate** silences an NPC shot on a clip with no 3031.
  That is retail (only `move_and_ranged` carries it) if R2 finds no fallback. *Recommend* C1 logs
  once per (model, sequence) an NPC attack clip with no fire event, so a silent gunman is visible.
  **(amended after V4r, 2026-10-04) Ruled, J6:** removed for `CWeaponRanged` — R2 found no retail
  path that fires a Troika human's shot without the event. Three-part guard: the removal is keyed
  on the operator body, and a body R2 did not read (`0x103ed200`, the flamethrower, thrown) keeps
  today's estimate behind a seam named for its address and "unread"; the log line above at
  Warning; the C integrator lists every NPC class whose ranged attack activity has no 3030..3044
  clip — the reading owed, filed to V5 with the three NPC bodies that call a weapon's slot 326
  directly. The player's use of the estimate is not touched in V4.
  **(amended after S2, 2026-10-04) The "unread body" seam is dropped: there is none.** All eight
  operator bodies are read (`packets-S2.md` item 1); `0x103ed200` is the player's grenade
  release; the flamethrower and the other species ranged classes use `CWeaponRanged`'s body and
  `Shot`; thrown and unarmed weapons take no event and have no weapon-side timer. The estimate
  is removed for **every** NPC wielder, `UsesUnreadOperatorEstimate()` is not written, and the
  silent-class list's first half is empty. The Warning and the integrator's list stand.
  **(J11, the second sitting.)** The four species whose fire is task code — ChangBros,
  FrenzyShadow, Bach, ManBat — are verified absent from both witness maps and filed on
  `spec.md`'s "on demand, not in the sequence" line, one row each; the Warning is their tripwire.
- **Q11 (added by the judge's second sitting, J12). The NPC's clip.** Retail fills it once at
  equip (`max(Default_Size, 1)`) and never lowers it; the port spends and refuses for every
  wielder — an old bug in landed work, fixed in V4o lane O3. Every gunman then fires without
  running dry: `NO_PRIMARY_AMMO` cannot rise from firing, and a record that expects a reload
  after emptying a gun is a record error. The reload finish stays a seam (V5b).
- **Q12 (added by the second sitting, J13 / J14.1). The corpse's removal is four clocks**, all
  state: unseen mortal at a 10 s poll, Kindred at +10 s, fade at about +13.8 s (25 of the 62
  makers on the two maps make fade children — `verbs_stealth_kill`'s victim is one), pedestrian
  never. C2 ports the missing thinks, D keeps the simulating body alive through a removal; four
  records prove them.
- **Q7. `melee_swing`'s `hit_event`** may be a record error (no authored melee commit, §1). R2
  settles; the V4c integrator corrects the record with the retail source if so. The record stays red
  on N3 (V11) regardless; the spec's "melee-and-die" scenario cannot close in V4.
  **(amended after V4r, 2026-10-04) It is a record error (R2 item 3c, J7)** — the male `baseball`
  bank authors events on four stealth-kill clips only; the seam agent A0 corrects it, not the
  V4c integrator.
- **Q10 (added after V4r, 2026-10-04). The player's shot and the cutscene camera move 0.1 s of
  clip time earlier** under the retail look-ahead (J3, J4): the weapon tests, the feed timeline
  and `OnCameraComplete` on every cutscene. Retail's timing; the A integrator triages a moved
  verdict as under Q5.
- **Q8. `ElysiumNpc.cpp`** is touched by C2 only within V4; V3d and T6b move it first — re-locate.
- **Q9. Save words.** `+0x658`, `+0x560`, `+0x654`, `+0x568` are datamap words retail saves. V4 does
  not touch the save walk; V6 (resume) adds them with the cursor.

## 9. Harness gaps V4 needs

- **H18** (seam): `speed2d`, `move_yaw`, `ground_speed` probes — N13's acceptance becomes direct.
  **(amended after V4r, 2026-10-04 — R1, J7) The acceptance is per body:** mid-leg on a straight
  leg, `speed2d` within 10 % of **that body's own `walk_0`** from its bank's
  `move_and_ranged.clips.json` — sentry2 (`vampire_hunter_chick`, the female bank) 101.278 cm/s →
  91.2..111.4; a male-bank body 136.683 cm/s → 123.0..150.4. Sentry2 already passes this today:
  the probe guards the cruise speed, and what turns the N13 records green is the `arrived` time.
  "Within 10 % of 136.7" was a record error for any body not on the male bank.
- **H19** (seam): `elysium_entity_get`'s facing / speed fields (§3) — what R1's session lacked;
  plus `attack_extents`, the three yaw words and `Selection.MoveYaw` (§3).
- **H20** (seam): `corpse_on_floor` read from the drawn mesh's `Bip01 Pelvis` bone, at rest by a
  speed threshold, the height bound per record's body (§3); `on_ground` documented as the capsule.
- **H21** (seam): the `animevent` trace for every animating entity, `who: "player"`.
- An anim-event tap check needs no new kind: `animevent` keeps its text (`<id> <options>`) and moves
  to the kernel dispatcher (A1); `anim_footsteps_walk` guards it.
- Numbers H18–H21 follow H17 (the arena seed door); take the next free numbers if others land first.
- Not needed: H6, H8 (V6), H13 (V8).

## 10. Boundaries — named, not designed here

- **V5**: `GatherAttackConditions`' clears, red 4, N1, N2 (`ranged_open_fire`'s wait). V4a only
  gets the record past `BEHIND_ENEMY` and its `shot_event`.
- **V6**: the save walk of V4's words and the resume; `OnTakeDamage_Alive`'s last-damage record.
- **V7**: the inputs, including the playback/speed-scalar input `0x102c3580`
  (`SetPlaybackAndSpeedScalar 0x1008d230`) — V4 reads `m_flGroundSpeedScalar` as 1.0.
- **V11**: the attack coordinator (N3); `chase_melee`, `melee_swing`, "melee-and-die".
- **0014**: the rig, the fall, the death impulse, the corpse volume (Q3). **0005**: the death family.
- **R3**: cover and the goal selectors. **Audio**: anim-event sounds.

## Rules for every agent of V4

- Read `AGENTS.md`, `spec.md` § Standing rules, § The method per story, § The bug protocol and the
  owner's rulings, this README, and your brief. `Arena/README.md` if you touch records or the runner.
- **Re-locate every cited site by Grep on its function name**: V3c, V3d and T6b move lines.
- **Query budget**: 10 s warns (log to `$ELYSIUM_WORK_ROOT/logs/slow-queries.tsv`), 60 s stops (never
  retried as-is or widened). Never read a file over ~200 KB whole. Look an address up with
  `uv run elysium research where <addr>` / `research section` before searching `docs/`.
- Text through the built-in **Grep / Read / Glob** tools, never shell `grep`/`cat`/`sed`.
- **Wait on a background command by its completion notification**, never a sleep or polling loop.
- **Coders never build**, never launch the editor, never run the arena or a suite; a coder may run
  only the single Python test file of a module it changed. Touch only your brief's files; a line
  another file needs goes in your report, exact, with its place.
- No record under `Arena/scenarios/` is edited by a coder; the seam agent and the integrator do.
- Follow the listing; cite the address at every line you port. A retail input with no source yet is
  a seam answering "nothing", named for the retail field. Divergences only as named in §7; a new one
  is recorded in your report, not adopted.
- No new test of a port mechanism. Do not commit (the seam agent and the integrators commit once
  each, on `spec-0002/step-2`, never push). Report ≤300 words.

## Shared names (fixed here so lanes agree)

- `FElysiumNpcBase::LastEventCheck` (`+0x658`), `YawSpeed` (`+0x560`), `GroundSpeed` (`+0x654`),
  `SequencePastHalf` (`+0x568`); `FElysiumCombatCharacter::FieldOfView` (`+0x1574`).
- The bridge row accessors on `FElysiumNpc`: `SequenceEvents(int32 Seq)` →
  `TConstArrayView<FElysiumAnimEvent>`, `SequenceLoops(int32)`, `SequenceTurnYaw(int32)` (degrees),
  `SequenceGroundSpeedAt(int32 Seq, poseParams)` (cm/s).
- `FElysiumNpcBase::DispatchAnimEvents(float Interval, FElysiumEntity* Handler)` — slot 258.
- `FElysiumNpc::SelectWeightedSequence(int32 Activity)` / `SelectHeaviestSequence(int32 Activity)` —
  the kernel's draws (C2); the `NpcSchedule` stream.
- Trace kinds unchanged; `animevent` text `<id> <options>`.

*(amended after V4r, 2026-10-04 — the names the new lanes share.)*

- **The dispatcher, shared by the NPC, the player and the camera.** Declared by the seam (A0) in
  `ElysiumAnimEvents.h`, **owned and filled by A1; A4 only calls it.** Nobody but A1 edits
  `ElysiumAnimEvents.{h,cpp}` in the wave; a field or parameter another lane finds missing goes in
  its report.

  ```cpp
  // One sequence's words, as `CBaseAnimating::DispatchAnimEvents 0x10091880` reads and writes them.
  struct FElysiumSequenceWords
  {
      int32 Sequence = 0;              // m_nSequence (the trace and the census name it)
      float Cycle = 0.f;               // m_flCycle, [0,1)
      float CycleRate = 0.f;           // GetSequenceCycleRate × m_flPlaybackRate, per second
      float AnimTime = 0.f;            // m_flAnimTime (a layer: the OWNER's)
      bool  bLoops = false;            // m_bSequenceLoops +0x65d
      bool  bHasDescriptor = true;     // a seqdesc exists (none: no finish, no past-half)
      bool  bDescriptorLoops = false;  // seqdesc.flags & 1 (the wrap clause)
      float LastEventCheck = 0.f;      // in/out: m_flLastEventCheck +0x658 (a layer: layer+0x2c)
      bool  bSequenceFinished = false; // out: m_bSequenceFinished +0x65c (a layer: layer+4, zeroed)
      bool  bSequencePastHalf = false; // out: m_fSequencePastHalf +0x568 (untouched for a layer)
  };

  namespace ElysiumAnimEvents
  {
      // 0x10091880. Fires each event on Handler.HandleAnimEvent (slot 259) in table order and
      // emits the `animevent` trace for Source. Returns true on the finish flag's rising edge:
      // the caller then makes its own OnSequenceFinished call (0x10091c80 is a direct call).
      bool DispatchBase(FElysiumSequenceWords& Words, TConstArrayView<FElysiumAnimEvent> Events,
                        FElysiumEntity& Source, FElysiumEntity& Handler);
      // 0x10098cd0, one overlay layer: no clamp, no past-half, the finish word zeroed, never set.
      void DispatchLayer(FElysiumSequenceWords& Layer, TConstArrayView<FElysiumAnimEvent> Events,
                         FElysiumEntity& Source, FElysiumEntity& Handler);
  }
  ```
- `void FElysiumPlayer::PostThinkAnimation()` — the player's `PostThink` step (`0x1016be10`).
  Declared and left empty by the seam; **A1 writes its one call site** in the world tick (where
  `AdvanceAnimEvents()` and the `TickStealthKill()` call after it stand today, both replaced by
  it); **A4 writes its body** (the dispatch, then `TickStealthKill()`).
- `IElysiumEmbodiment::GetBodyClipByRawIndex(USkeletalMeshComponent* Body, int32 RawIndex)` → the
  body's `FElysiumNpcClip` with that `RawIndex`, or none (A2; the default and the recording double
  answer none). N19's row 0 asks it for 0.
- V4b, both added by B1 to the interface in `Public/ElysiumWorldServices.h` that carries `MoveTo`
  (and to its implementers), B2 only calls the second: `float KernelIdealSpeedCm()` as the body's
  read of the kernel's `GetIdealSpeed` (B1 states the direction of the call and the header in its
  report if the body reaches the kernel another way); `bool GetNpcMoveFacingYaw(float&
  OutYawDegrees) const` — the active move's turn-script yaw (the direction along the path, eased),
  false when no move is under way (B2 then takes the current yaw, retail's other arm). The
  integrator reconciles a parameter the interface's convention forces.
- `FElysiumNpc::SequenceBounds(int32 Seq, FVector& OutMinCm, FVector& OutMaxCm) const` → false
  for "no descriptor" (the body in `ElysiumNpcAnim.cpp` is **C2's**; **C1** calls it from slot
  247). The data fields are C1's: `bboxMinCm` / `bboxMaxCm` on the `body_data.py` row,
  `BboxMinCm` / `BboxMaxCm` on `FElysiumBodySequence` and `FElysiumNpcClip`.
