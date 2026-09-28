# Story 8 — family RunAi19, walked prose (lane L13a)

Spec 0019 story 8, pass I. Each section is one `rule` row of `families-19-29/RunAi19-READING.md`,
walked arm by arm off the listing (`vtmb_asm`) with the decompiled C as a second reader. Pass C folds
these into `schedule-kernel.md` / `lifecycle.md`. Port: `ElysiumNpcBaseRunAi19.cpp` (`CAI_BaseNPC`),
`ElysiumNpcRunAi19.cpp` (`CAI_BaseNPCTroika`), `ElysiumNpcRunAi19Species.cpp` (the species slot-432
overrides). Every scope-trace push/pop (`g_ScopeTraceStack`, the `"NULL ENTITY"` name pick) and every
VProf scope / RDTSC bracket in these bodies is absent by convention: none has an observable.

Retail's think chain around this family: `NPCThink 0x10292de0` asks `RunAlternateAI(bReduced)` and runs
slot 432 `RunAI(bReduced)` only when it answers false; `bReduced` is `!IsThinkDue` of the AI clock.
The thirteen-byte species slot-432 bodies (`CGeneric_NPC`, `CNPC_VAnimal` `0x10360160`,
`CNPC_VChangBros` `0x10385a10`, `CNPC_VGhoulCroucher` `0x10387500`, …, verdict `dead`) are bare
forwards to `CAI_BaseNPCTroika::RunAI 0x1028fcc0` and are not separate port bodies: the species classes
inherit `FElysiumNpc::RunAI`.

## `0x1026f110` `CAI_BaseNPC::RunAI(bool bReduced)` (756 bytes, slot 432)

1. `m_bConditionsGathered (+0x5ca4) = 0` (`0x1026f1ad`). The port's form of the byte is
   `Cognition.GatheredAt` (non-negative = gathered this pass, L07); the clear is the negative.
2. The debug overlay: `DAT_1070af4c` is the `developer` ConVar — `[+4]` its parent, slot 1
   `IsCommand` false and `m_nValue (+0x2c) != 0` (`0x1026f1be`..`0x1026f1cb`) — AND the navigator's
   `m_bNotOnNetwork` (`m_pNavigator +0x34`, `0x1026f1d3`) → `AddTimedOverlay("NPC w/no reachable
   nodes!", 5)` (`0x1026f1d8`..`0x1026f1e1`). Shipped `developer` is 0: dead in retail.
3. Only when `bReduced` is false (`0x1026f1ea`) AND `m_hDialogPartner (+0x0fe8)` fails the three-part
   EHANDLE test (`0x1026f1f9` / `0x1026f215` / `0x1026f219`): slot 433 `GatherConditions`
   (`0x1026f237`), then `if (!m_bConditionsGathered) m_bConditionsGathered = 1` (`0x1026f243` /
   `0x1026f245`) for an override that did not call the base. Both refusals are a plain skip: the
   standing condition word is not touched.
4. `0x1026ab50`, the shrunk-hull head probe, on every pass (`0x1026f29a`).
5. Slot 434 `PrescheduleThink` (`0x1026f2b6`), bracketed by RDTSC into `0x1090fe68/6c`.
6. `MaintainSchedule 0x102817c0(this, bReduced)` (`0x1026f302`).
7. Full passes only (`0x1026f30b`): `ClearCondition 0x10269b50` of `LIGHT_DAMAGE 0x4c`
   (`0x1026f311`), `HEAVY_DAMAGE 0x4d` (`0x1026f31a`), `WAS_BUMPED 0x38` (`0x1026f323`), in that
   order. This is the one-pass life of the damage and bump bits: raised by the damage transaction
   and the touch handler between passes, visible to exactly one full `MaintainSchedule`.
8. `m_bRanAI (+0x1b4c) = 1` (`0x1026f32c`), on every pass.

Reads `+0x5ca4`, `+0x5d34`→`+0x34`, `+0x0fe8`, the `developer` ConVar; writes `+0x5ca4`, `+0x1b4c`
and (through `0x10269b50`) the condition word.

**Unrecovered:** the navigator's `m_bNotOnNetwork` has no producer in the port
(`Conditions19NavNotOnNetwork` answers false), so the overlay arm cannot open; `AddTimedOverlay` is a
recording seam (no debug-overlay service).

## `0x1028fcc0` `CAI_BaseNPCTroika::RunAI(bool bReduced)` (141 bytes, slot 432)

Three calls whose order is the whole body: `0x1028fc90` (`0x1028fd2c`), `UpdatePedestrianInfo
0x102a0d20` (`0x1028fd31`; ECX is still `this` — `0x1028fc90` does not touch it), then
`CAI_BaseNPC::RunAI 0x1026f110(bReduced)` (`0x1028fd3d`). Filled for `CAI_BaseNPCTroika`, `CNPCMaker`
and its two subclasses, `CNPC_VBaseBoss`, `CNPC_VNewscaster`, `CNPC_VPlaceholder`,
`CNPC_VTzimisceRunner`, `CNPC_VWerewolf` and `CPayphone`.

`0x1028fc90` (11 instructions, no verdict row of its own): `m_flStealthHearingDist (+0x63cc) = 0`,
`m_flStealthVisionScalar (+0x63c4) = 1.0`, `m_flStealthVisionCone (+0x63c8) = 1.0`
(`0x1028fc95`..`0x1028fca5`). A discipline's stealth modifier therefore lives exactly one AI pass.
`UpdatePedestrianInfo` runs before the gather because it clears `SHOULD_INTERACT 0x10`,
`CROSSWALK_WALK 0x12` and `CROSSWALK_DONTWALK 0x13` on every crosswalk-path pass.

Note: `CNPCMaker::Spawn` (`0x1034b9d0`) calls `0x1028fb70` then `0x1028fc90` on the MAKER; the port's
`RecomputePerceptionDistances` seam (`ElysiumNpcSocial10.cpp`) names `0x1028fc90` as a perception
recompute, which it is not — it is this stealth reset.

**Unrecovered:** none.

## `0x1028fd80` `CAI_BaseNPCTroika::RunAlternateAI(bool bReduced)` (337 bytes, `RET 4`)

The argument is `NPCThink`'s `bReduced` byte; each arm pushes it on and none reads it.

1. Head arm: `m_GrapplePartner (+0x1538)` live by the three-part EHANDLE test (`0x1028fdf2` /
   `0x1028fe0f` / `0x1028fe14`) AND `m_GrappleRole (+0x153c) == 1` — the victim — (`0x1028fe1f`):
   `m_IdealActivity (+0x0ff0) - 0xf88` unsigned `<= 0xb1` (`0x1028fe31`) indexes the byte table
   `0x1028fedc`; entries `0xf88 0xf91 0xf9a 0xfb7 0xfc0 0xff7 0x1000 0x1009 0x1039` call
   `CAI_BaseNPC::AutoMovement 0x10280a50` (`0x1028fe42`, plain `RET`, no argument). TRUE either way
   (`0x1028fe52`).
2. Otherwise `m_eAlternateAI (+0x644c)` unsigned `> 4` answers FALSE (`0x1028fe60`); the table at
   `0x1028ff90`: 0 → FALSE (`0x1028fec1`); 1 → `0x10290040` (the door approach, `0x1028fe6e`); 2 →
   `0x10290200` (`0x1028fe84`); 3 → `0x102902e0` (`0x1028fe9a`); 4 → `0x10290350` (`0x1028feb0`);
   each arm's answer is returned.

`0x10290200` (mode 2, the door opened; no verdict row of its own): `MaintainActivity 0x102727d0`
(`0x10290203`); `curtime < m_flAlternateAIExpireTimer (+0x6450)` → TRUE (`0x1029021d`); else a live
`m_hOpeningDoor (+0x5d24)` (`0x1029022c`..`0x1029024e`) → slot 448 `TaskFail(0xe)` (`0x10290256`),
`+0x5d24 = -1`, `m_bOpeningDoorWait (+0x5d30) = 0`, `+0x644c = 0`, TRUE; a gone door → when the
navigator's goal is set (`0x102ee2e0`, `0x10290283`) `0x102bf7e0` (`0x1029028e`), then slot 528
`ValidateNavGoal` (`0x10290297`), `+0x644c = 0`, TRUE — the wait flag is left as it was.

`0x102902e0` (mode 3, the door blocked): `MaintainActivity` (`0x102902e3`); at or after the expiry
(`0x102902fd`) `TaskFail(0xe)` and the same three clears (`0x10290305`..`0x1029031d`); TRUE.

**Unrecovered:** none in the body. `AutoMovement` has no observable in a headless world (the
interval-movement seam answers no delta), so the nine-activity split is carried but not witnessed.

## `0x1039e3d0` `CNPC_VMingXiaoTentacle::RunAI` (820 bytes)

Three blocks before `CAI_BaseNPCTroika::RunAI` (`0x1039e633`), inside a scope trace and the VProf node
`CNPC_VMingXiaoTentacle_RunAI` (both absent).

1. The evade re-plan. `FLD curtime; FCOMP m_flUpdateEvadeTimer (+0x667c); AND 0x4100; JNZ`: only a
   curtime STRICTLY past the timer runs it (`0x1039e4a6`). The timer is re-armed FIRST, to curtime +
   `RandomFloat(1.0, 2.0)` (`0x1039e4be` / `0x1039e4cf`), then: the navigator's goal active
   (`0x102ee6a0`, `0x1039e4d5` / `0x1039e4dc`) and its goal type (`0x102ee620`) == 4
   (`GOALTYPE_LOCATION`, `0x1039e4f0`). A null slot 167 `GetEnemy` (`0x1039e4fa`) fails the task:
   `+0x1b48 = 0x4ba`, reason 6. With an enemy, `FLD m_flEnemyDist (+0x6268); FCOMP 256.0; TEST AH,0x41;
   JP` continues only AT OR BELOW 256 (`0x1039e523`); `GetEnemy` is asked again (`0x1039e529`) and
   `0x102edae0(navigator, &enemy->GetAbsOrigin(), 512.0, 30000.0, &out)` (`0x1039e53c`..`0x1039e551`;
   slot 217 takes no argument, the pushes are `0x102edae0`'s) — a refusal fails with line `0x4c6`,
   reason 7; a node then `0x102ee220(navigator, &out)` (`0x1039e573`), whose false answer fails with
   line `0x4d0`, reason `0xc`; true falls through. Every failing arm stamps `+0x1b44` =
   `E:\Vampire\main\dlls\hl2_dll\NPC_VMingXiaoTentacle.cpp` (`0x1039e58c`) and calls slot 448.
2. The collision release: `m_bIgnoreCollision (+0x6688)` set (`0x1039e5a4`) and NOT `curtime <
   m_flIgnoreCollisionTimer (+0x6684)` (`0x1039e5bc`, an equal stamp has expired): the flag is CLEARED
   first (`0x1039e5cb`), then `IsAreaClear(GetAbsOrigin(), 0x202400b, 0, 0)` (`0x1039e5db`); a blocked
   area sets the flag again and the timer to curtime + 1.0 (`0x1039e5f2` / `0x1039e5f9`).
3. The landing sounds: `m_bHitGroundSound (+0x6698)` clear and slot 209 `GetGroundEntity` non-null
   (`0x1039e607` / `0x1039e615`): `0x1039f030` (`tentacle_hit_ground.wav`, `CHAN_BODY`), `0x1039f1a0`
   (`tentacle_flopping_loop.wav`, `CHAN_VOICE`; the loop `0x1039f310` stops at death), then the flag.

`0x102ee220` is `m_pPath->SetGoalPos` (`0x1030b950`) then the route build `0x102f1dc0(this, 0, 0)`,
returning its byte; on a refused build `0x102f1dc0` itself fails the task (`OnNavFailed(0xc, 1)`), so
retail fails twice on that arm.

**Unrecovered:** the port's navigator keeps no goal type (`NavGoalState` answers -1), so arm 1 stops
after the re-arm; the node search `0x102edae0` is a seam answering false; `0x102ee220`'s answer is a
seam answering true. The species shape map lists `+0x667c`, `+0x6684`, `+0x6698` ABSENT.

## `0x103bdef0` `CNPC_VTzimisce` slot 432 (80 bytes)

The decompiled `this == 1` is the stack word `PUSH ECX` reserves; `0x103c0160(this, &kind)`
(`0x103bdef9`) answers a blocker or null. Null → the base (`0x103bdf2f`). Otherwise kind 1 →
`0x103c0860(blocker)` (`0x103bdf11`), any other → `0x103c05e0(blocker)` (`0x103bdf2a`); both then
`CAI_BaseNPCTroika::RunAI` (`0x103bdf1d` / `0x103bdf36`).

`0x103c0160` (909 bytes, `RET 4`): start = slot 220 `GetOrigin` + (0, 0, 4.0 `_DAT_10450aa0`)
(`0x103c0189`); end = start + `AngleVectors(slot 221)` forward × `tzimisce_obstruction_lookahead`
(ConVar `0x1093cd28`, "24"); `m_Collision` mins/maxs; `CTraceFilterSimpleTwoEnt(this,
GetIgnoreCollisionEntity(), m_CollisionGroup +0x368)`; `TraceRay` mask `0x202400b` (`0x103c0425`).
`fraction == 1.0` → null (`0x103c0475`); the world entity (`0x1023bd00`) → null (`0x103c049e`); slot 69
`NavIgnoreCollision(hit)` false → null (`0x103c04ba`); else `*kind = 0`, answer the hit
(`0x103c04d2`). **It only ever writes 0: the kind-1 arm `0x103c0860` is dead in retail.**

`0x103c0860` (74 bytes, blocker unused): slot 62 `SetOrigin(GetOrigin() + (0, 0, slot 522
StepHeight()))`. `0x103c05e0` (501 bytes): null blocker or no `m_pPhysicsObject (+0x36c)` → return
(`0x103c05f1` / `0x103c05ff`); v = this body's slot 199 velocity rotated −90° `(v.y, −v.x, v.z)`; a
world-only line from the blocker's origin along v (`0x103c06d5`) that hits flips v on all three
components (`0x103c0708`..`0x103c0717`); v × `tzimisce_obstruction_scalar` ("5"); v.z +=
`tzimisce_obstruction_z` ("75"); `IPhysicsObject` slot 41 `AddVelocity(&v, &zero)` (`0x103c07c4`).

**Unrecovered:** the kernel hull trace is a seam answering clear, so no blocker reaches the port;
`m_CollisionGroup`, the world entity and the blocker's physics object are seams; slot 41's name is
inferred (`CBaseCombatWeapon` drop `0x10252a90`).

## `0x103c1d20` `CNPC_VTzimisceHeadClaw` slot 432 (26 bytes)

`0x103c2230(this, false)` (`0x103c1d25`) — the slowed-grab teardown, which acts only while
`m_flSlowedExpire (+0x6678)` is above 0.0 and has reached curtime — then `CAI_BaseNPCTroika::RunAI`
directly (`0x103c1d31`, thunk `0x1000704f`, no ChangBros forwarder).

## `0x1035e980` `CNPC_VAndreiBlood` slot 432 (171 bytes)

`m_bActivated (+0x66cc)` clear (`0x1035e98b`): `m_NPCState (+0x5cc0) = 1`, `m_IdealNPCState (+0x5cc4)
= 1` written directly — no `SetState`, no `OnStateChange` (`0x1035e98d` / `0x1035e997`); a live
`m_hClosestPlayer (+0x628c)` (`0x1035e9aa`..`0x1035e9cd`) whose slot 404 `IRelationType` is not 4
(`0x1035e9dd`) gets `AddClassRelationship(1, 4, 10)` (`0x1035e9e7`); `m_bfNPCStateFlags (+0x5b64) &=
~4` (`0x1035e9ec`..`0x1035e9fb`); `0x10385a10` (`0x1035ea01`). Set: both words 2 (`0x1035ea16` /
`0x1035ea1c`), `0x10385a10` (`0x1035ea22`).

**Unrecovered:** the port derives `+0x5b64` from the state; the clear of bit 2 is carried by IDLE's
derived byte (`0x31`), retail's other stale bits of the previous byte are not.

## `0x10361110` `CNPC_VAsianVampire::RunAI` (111 bytes)

`UpdateMovedTimeStamp 0x10362540` (`0x10361163`, bare `RET`: no argument) then `0x10385a10`
(`0x1036116f`). The stamp is what `StationaryForTooLong 0x10362670` in `SelectSchedule 0x10360eb0`
reads.

## `0x10363b60` `CNPC_VBach` slot 432 (186 bytes)

`m_bCanFightYet (+0x66a8)` clear → `SetEnemy(NULL)` (`0x10363b6b` / `0x10363b6f`), every pass. Then
`m_bShieldActive (+0x66a5)` set and `m_flShieldTime (+0x6684)` STRICTLY before curtime (`0x10363b97`,
`TEST AH,0x41`): the stat list whose `+0x10` tag is 3 (`0x10363ba5`..`0x10363bbd`), else the lazily
constructed global `0x109f0b40` (`0x10363bc9`..`0x10363be4`), `CVStatList_t::SetBase(0xd, 0)`
(`0x10363bf7`), `m_bShieldActive = 0` (`0x10363bfd`). Then `0x10385a10` (`0x10363c0c`). The raise is
`GatherAttackConditions 0x10363db0` (`SetBase(0xd, 5)`, 6.0 s).

## `0x103747e0` `CNPC_VDog` slot 432 (164 bytes)

`m_Activity (+0x0fec)` 1 and `m_NPCState` 1 (`0x103747ec` / `0x103747f4`): `RandomInt(0, 0xff)` through
the import `[0x109f3868]` (`0x103747fd`) EQUAL to 0x80 → slot 310 `SetActivity(3)` (`0x10374813`);
`0x10360160` (`0x10374820`). `0x6e` (ACT_SNARL) with `m_bSequenceFinished (+0x065c)` →
`SetActivity(1)` and `0x10360160` (`0x10374843` / `0x10374850`). 3 (ACT_FIDGET) or `0x40` (ACT_SIT)
with the byte → `SetActivity(1)` (`0x1037486e`). Every path ends in `0x10360160`.

## `0x10378b80` `CNPC_VGargoyle` slot 432 (80 bytes)

The Tzimisce shape with `0x103796a0` / `0x10379e80` / `0x10379b40` and the `gargoyle_obstruction_*`
ConVars (`0x1093af60` "24", `0x1093afa8` "5", `0x1093b050` "75"). The sweep adds a re-trace with both
ends raised by slot 522 `StepHeight` (18.0): clear, the world, or a hit slot 69 refuses → kind 1
(`0x103799fe`..`0x10379a0e`, `0x10379a40`), else kind 0 (`0x10379a24`); the FIRST trace's entity is
answered, so kind 1 steps up even when the raised trace hits world geometry. `0x10379b40`: null →
return (`0x10379b54`); `FClassnameIs(blocker, "prop_dynamic")` (inline, case-insensitive,
`0x10379b5a`..`0x10379bcd`) → the prop's slot 266 with the gargoyle (`CBreakableProp::Break`
`0x1018fb90`, `0x10379bd4`) and return; else the physics push (`0x10379be6`..`0x10379db5`). Then
`0x10385a10`.

**Unrecovered:** as Tzimisce; the prop break is `FElysiumProp::InputBreak` (the prop's damage, gibs
and explosion are not stood).

## `0x10380120` `CNPC_VHengeyokai` slot 432 (111 bytes)

The Gargoyle twin (`0x10380fc0` / `0x103816e0` / `0x10381460`, `hengeyokai_obstruction_*` at
`0x1093b238` / `0x1093b298` / `0x1093b1f0`), then `0x10385a10` (`0x10380157`), then the one-shot console
trigger: `hengeyokai_stun` (`0x1093b2e0`, "0") `GetInt() != 0` (`0x10380164` slot 1 IsCommand,
`0x10380175` `m_nValue`) → `0x103830e0` (the morph: schedule `0x16e`, skin fade 0.0, `FadeToSkin(1)`,
`0x10380179`) then `ConVar::SetValue(0)` (`0x10246160`, `0x10380185`).

## `0x1038e990` `CNPC_VManBat` slot 432 (26 bytes)

`0x1038f020(this, false)` (`0x1038e995`), then `0x10385a10` (`0x1038e9a1`). `Event_Killed 0x1038e8c0`
is the force-true caller.

## `0x103a3670` `CNPC_VPedestrian` slot 432 (257 bytes)

`0x10385a10` FIRST (`0x103a367b`). Then `m_bfAINPCFlags (+0x14b8) & 0x80` (`IN_FLEE_SCHED`,
`0x103a368f`) and curtime NOT below `m_flNextFleeSoundTime (+0x641c)` (`0x103a36ab`): `+0x641c =
curtime + 10.0` (`0x103a36c7`); `AngleVectors(slot 221)` forward (`0x103a36d4`); point = slot 220
origin − forward × 256.0 (`0x103a36dd`..`0x103a3729`); `CSoundEnt::InsertSound(8 SOUND_DANGER, &point,
DAT_1072bc8c, 10.0, DAT_1072bcc3, this)` (`0x103a3762`). The two cells are `sound_volume_table` row 27
`NPC_DISCIPLINE_ALERT` (radius, occlusion byte).

## `0x103a75c0` `CNPC_VSabbatLeader::RunAI` (111 bytes)

`0x10385a10` (`0x103a7618`) then `UpdateBloodSplash 0x103aa960` (`0x103a761f`, no argument).

## `0x103aebd0` `CNPC_VSheriffMan::RunAI` (121 bytes)

`m_bFloorHeightsCached (+0x66e7)` clear (`0x103aec29`) → `CacheFloorHeights 0x103b1510`
(`0x103aec2d`); then `0x10385a10` (`0x103aec39`).

## `0x103df850` `CNPC_VZombie` slot 432 (364 bytes)

1. `m_iZombieAIType (+0x6678) == 1` (`0x103df85a`) and `m_flPlayerDist (+0x6264)` STRICTLY above
   `m_flRemoveDist (+0x66dc)` (`0x103df86f`): `DevMsg("zombie %s: player too far away, killing self
   (%.1f > %.1f)...\n", GetDebugName(), dist, removeDist)` (`0x103df892`), slot 119 `Kill`
   (`0x103df89f`). Nothing returns: the pass continues.
2. `0x10360160` (`0x103df8ac`).
3. Type 1 or 0 (`0x103df8ba` / `0x103df8be`), `m_Activity (+0x0fec) != 0x4b` (`0x103df8cb`),
   `HasCondition(0x46 SEE_ENEMY)` (`0x103df8dc`), `LungeDistanceMin (+0x250, 98.0) <= dist`
   (`0x103df8f7`, `TEST AH,0x41; JP`), NOT `LungeDistanceMax (+0x254, 160.0) < dist` (`0x103df914`),
   NOT `curtime < m_flGrappleReadyTimer (+0x66d8)` (`0x103df930`), `m_hClosestPlayer` live
   (`0x103df93b`..`0x103df95d`) with a player record (`+0xa8`, `0x103df967`), and `Percent (+0x264, 2)
   > RandomInt(0, 99)` (`0x103df985` / `0x103df988`): `0x102ae750(0x16a, 0)` (`0x103df993`) and
   `m_flGrappleReadyTimer = curtime + DelayBetween (+0x280, 20.0)` (`0x103df9ab`).
4. Every exit: `m_bGroundSpeedFromIntervalMovement (+0x05ac) = 1` (`0x103df9b1`).

The packet's verdict line put `m_Activity` at `+0x628c`; the listing reads `+0x0fec`.
