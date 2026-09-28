# Story 8 — family Think19, walked prose (lane L13b)

Spec 0019 story 8, pass I. Each section is one `rule` row of `families-19-29/Think19-READING.md`
(plus Damaged19's `0x103cb590` and the review of `0x103a4700`), walked arm by arm off the listing.
Pass C folds these into `lifecycle.md` § "The think cadence" and `schedule-kernel.md`. Port:
`ElysiumNpcBaseThink19.cpp` (`CAI_BaseNPC`), `ElysiumNpcThink19.cpp` (`CAI_BaseNPCTroika`),
`ElysiumNpcThink19Species.cpp`, `ElysiumNpcDamaged19Species.cpp` (Werewolf),
`ElysiumNpcFrenzyShadow.cpp` / `ElysiumNpcPlayerController.cpp` (landed earlier, reviewed here).

Every body below opens with the scope-trace push (`g_ScopeTraceStack`, `m_iName` or `"NULL ENTITY"`)
and closes with its pop, and most carry a VProf node; both are debugger bookkeeping the port does
not carry (story 29b: the ring and the trace stack are absent words). They are not repeated per
section.

## `0x10292de0` `CAI_BaseNPCTroika::NPCThink()` (2552 bytes, slot 431)

44 classes fill slot 431 with this body. Order:

1. `NPCThinkDebugPre` `0x10292500` (`0x10292e4d`), before anything else.
2. `m_bfAINPCFlags2 &= 0x7ffffffb` (`0x10292e5e`/`0x10292e66`): **both** bit `0x4`
   (`SCHEDULE_CHANGED`) and bit `0x80000000` are cleared every think, before the disable test.
3. `m_bDisableAI` (`+0x6080`) set: return (`0x10292e6c`). `m_flNextThink` is not written.
4. `bNormal = IsThinkDue(m_flNextNormalThink)` `0x102906c0` (`0x10292e8e`). With it, **Set1**:
   - `m_flPlayerDist = SetClosestPlayer()` `0x10293a80` (`0x10292ef4`/`0x10292efd`).
   - slot 167 `GetEnemy()` (`0x10292f03`); null or `enemy->m_lifeState (+0x200) != 0`: the three
     words `+0x6268/+0x626c/+0x6270` = `0x469c4000` = **20000.0** (`0x10293059`..`0x1029306a`).
     Otherwise `m_flEnemyDist = |myOrigin - enemyOrigin|` (`0x10292fb0`), `m_flEnemyHeightDiff =
     |my.z - enemy.z|` (`0x10292fbf` `FABS`, `0x10292fc1`), and `m_flEnemyLastKnownDist = |myOrigin -
     lastKnown|` where `lastKnown` is slot 541 `GetEnemies()` → `0x102dfed0` (`0x10293003`); then,
     **inside the enemy arm only**, when the ConVar `*0x10924f74` is not a command (slot 1 false) and
     its int `+0x2c` is non-zero and `flags2 & 0x400`: slot 517 `AddFacingTarget(enemy, &lastKnown,
     1.0, 0.8, 0.0)` (`0x10293051`).
   - slot 221 `GetAngles()` → `AngleVectors` `0x10139610` into `m_vecForward +0x6290` and
     `m_vecRight +0x629c`, up NULL (`0x10293084`/`0x1029308b`).
   - `SetPlayerLOS` `0x10291610` (`0x10293095`); `CacheInterruptConditions` `0x1026a0f0` (`0x1029309c`).
   - `m_bfAINPCFlags & 0x4000`: `AutoMovement` `0x10280a50` (`0x102930b7`).
   - `m_pHintNode` (`+0x5ddc`) set: `m_flOccludedDelay = m_flOccludedDelayCover` (`0x102930d1`), then
     slot 566 `FValidateHintType(m_pHintNode)` (`0x102930db`); false → clear arm. True and
     `m_bStayEntrenched` (`+0x6435`) → keep. Else resolve `m_hHintCoverObject` (`+0x6448`, `-1` or
     stale → NULL) and compare with slot 167 `GetEnemy()` (`0x1029312b`): different → keep; **equal —
     which includes "no cover object and no enemy"** → `HasCondition(0x2e)` (`0x10293133`) → clear
     arm, else `HasCondition(0x48)` (`0x10293140`) → clear arm, else keep. The clear arm is
     `ClearHintNode(5.0)` `0x10295ab0` (`0x10293150`), the `*0x10924a6c` slot-1 read with its answer
     discarded (`0x1029315d`), `SetCondition(0x29)` (`0x10293164`). No hint node:
     `m_flOccludedDelay = m_flOccludedDelayNormal` (`0x10293171`).
   - `m_pShootAtHint` (`+0x6444`) set: slot 566 on it (`0x10293186`); false →
     `m_hShootTargetOverride = -1`, `m_pShootAtHint = NULL` (`0x102931b5`/`0x102931bf`); true →
     `m_hShootTargetOverride = *hint->slot1()` (the hint's own EHANDLE, `0x102931a1`).
   - `m_bfNPCFrenziedFlags & 0x8000` and `RandomInt(0, 99) < 1` (`0x102931ed`/`0x102931f3`): the
     `"Scream_Death"` sound index, searched once for the whole process in the VSound concept table
     (`__strcmpi`, `0x10293231`) and cached in `0x109246c0` behind guard bit `0x10923dd7 & 1`, `-1`
     cached forever when absent; then the VSound play `0x101f5950(this, index, 2, 1.0, 1.25)`
     (`0x1029326c`).
5. `updateInterval = curtime - m_flLastUpdateThink` (`0x10293288`), `bUpdate =
   IsThinkDue(m_flNextUpdateThink)` `0x102906a0` (`0x10293292`), `normalInterval = curtime -
   m_flLastNormalThink` (`0x1029329a`) — all three on EVERY think.
6. With `bNormal`, **Set2**: `ResolveStandingOnHead(normalInterval)` `0x102bf820` (`0x1029330c`);
   `flags2 & 0x4000` clear → `0x102bfdf0(normalInterval)`, an empty `RET 4` (`0x10293321`); ConVar
   `*0x10924d24` gate → `0x102bfe10` the ground check (`0x10293344`); `0x102bf310` the `move_yaw`
   pose (`0x1029334c`); `flags2 & 0x20000000` (`DISAPPEAR`): closest player (`+0x628c`, NULL when
   stale) fails the PVS test `0x101d1a90` or `FVisible(player, 0x2804091)` → `UTIL_Remove(this)`
   `0x101cd940` (`0x102933f1`) — **and the think continues**: `UTIL_Remove` is deferred.
   Then the AI gate `0x1026c3d0` (`0x102933fb`). Refused: `m_flNextThink = curtime + 0.1f`
   (`0x104491b4`) unless `DAT_1093408c` (`0x1029341f`), and **return without the tail** (`0x10293447`
   → `0x102937c6`). Accepted: `bMove = 0x102906e0()` (constant 1), `bAI = IsThinkDue(m_flNextAIThink)`
   `0x10290700`; `RunAlternateAI(!bAI)` (`0x1029356e`) and, when it answers false, slot 432
   `RunAI(!bAI)` (`0x1029357c`); `PostRun()` (`0x10293584`); `PerformMovement(interval, !bMove = 0)`
   (`0x1029359e`); `CalcNextMoveThink` (`0x10293632`); `CalcNextAIThink` (`0x1029363e`).
7. Tail: `bUpdate` → slot 312 `UpdateCharacter(updateInterval)` (`0x1029365b`), and within that arm
   `m_bIsTalking` (`+0x64c0`) with `0x102c0aa0` false → `FinishTalking` `0x102c0ca0` (`0x10293678`).
   `CalcNextUpdateThink` (`0x10293684`), `CalcNextNormalThink` (`0x10293690`), `m_flNextThink =
   (NextUpdate <= NextNormal or unordered) ? NextUpdate : NextNormal` (`0x102936a1`..`0x102936c0`),
   overwritten with `curtime + 0.01f` (`0x10450aa4`) while `m_bJumping` (`+0x6498`, `0x102936d8`);
   then the debug overlay `0x1029bd40` (`0x102936e0`).

**Unrecovered:** the names of flags2 `0x80000000`, conditions `0x2e`/`0x48`/`0x29`; the three ConVars
`0x10924f74`, `0x10924d24`, `0x10924a6c`; `0x102bfdf0`'s purpose (empty); what `0x1093408c` is (the
node-graph flag by its other readers).

## `0x1026ca80` `CAI_BaseNPC::NPCThink()` (643 bytes, slot 431)

Filled by `CAI_BaseNPC`, `CAI_BaseHumanoid`, `CAI_ExpressiveNPC`, `CAI_TestHull`, the four Cine
classes, `CGenericNPC`, `CGenericSabbat_NPC`. `m_bDumpDebugBuffer` (`+0x5b55`) set → clear it and dump
the ring `0x1027efb0` (`0x1026cafd`/`0x1026cb03`). `CacheInterruptConditions` (`0x1026cb0a`).
`m_flNextThink = curtime + 0.1` (the DOUBLE at `0x104493d0`, `0x1026cb1d`) unconditionally, before
every gate. `g_pAINetworkManager` (`0x10934088`) null or its `+0x658` ready byte clear → return
(`0x1026cb2a`/`0x1026cb36`). The AI gate `0x1026c3d0` refused → return (`0x1026cb92`). Accepted:
`m_hGrapplePartner` (`+0x1538`) resolving AND `m_GrappleRole` (`+0x153c`) `== 1` skips slot 432
(`0x1026cc23`); otherwise `RunAI(false)` (`0x1026cc2a`). `PostRun()` (`0x1026cc32`) and
`PerformMovement(interval, false)` (`0x1026cc43`) run on both.

**Unrecovered:** the network manager's `+0x658` name (ready/built).

## `0x10298070` `CAI_BaseNPCTroika::UpdateCharacter(float)` (574 bytes, slot 312)

`m_bIsBossMonster` (`+0x6496`): when not yet registered (`+0x6497 == 0`), the global count
`DAT_10924fb8` (a signed BYTE) `< 2` and slot 464 `GetState() == 2` → the table
`DAT_109247e0[count++] = GetRefEHandle()`, `+0x6497 = 1` (`0x1029812b`/`0x1029812d`); a body still a
boss then skips to the tail (`0x1029813c`). Not a boss but registered (`0x1029814a`): both slots are
walked (`0x1029817a`..`0x1029821f`); an entry whose entity resolves (`0x100290c0`) and is not `this`
is appended to a temporary vector as that entity's own handle (`-1` when it no longer resolves on the
second lookup); `DAT_109247e0 = DAT_109247e4 = 0`, `count = survivors` (`0x1029822b`..`0x10298239`);
when `count != 0` BOTH slots are copied back from the vector (`0x1029824d`..`0x1029825a`) — slot 1
reads past the vector's count when one survived (a retail over-read); `+0x6497 = 0`
(`0x10298260`). Tail, every path: `CBaseCombatCharacter::UpdateCharacter(dt)` `0x103246d0`
(`0x1029829a`). `CWorld::vfunc113` `0x1023bc20` resets the table to `-1, -1` and the count to 0 on
world activate; the one reader is `CBasePlayer::UpdateClientActionState` `0x101755d0`.

**Unrecovered:** what the player reads the registry for (the boss bar); the vector growth policy
behind `0x102c6c30` (whether the slot-1 over-read sees 0 or heap).

## `0x10369120` `CNPC_VCamera::NPCThink()` (210 bytes; also `CNPC_VCameraSecurity`)

Replaces the Troika body outright. `+0x5b55` → clear and dump (`0x1036912d`/`0x10369134`);
`0x1029f2e0` (`m_bDisableAI`) true → return with no stamp (`0x10369142`); `CacheInterruptConditions`
(`0x1036914a`); the AI gate `0x1026c3d0` refused → `DAT_1093408c` set: return; clear:
`m_flNextThink = curtime + 0.1f` (`0x10369175`), return. Accepted: slot 432 `RunAI(false)`
(`0x10369183`); the four `Next*` stamps copied into the four `Last*` (`0x1036919b`..`0x103691b3`);
`m_flNextThink` and all four `Next*` = `curtime + 0.2` (the DOUBLE at `0x10449198`,
`0x103691c8`..`0x103691ea`). No `SCHEDULE_CHANGED` clear, no senses pass, no `PostRun`/movement.

## `0x1037b3f0` `CNPC_VGhoulCroucher::NPCThink()` (202 bytes)

The Troika body first (`0x1037b3f4`). Then `+0x6665` (the SECOND spawn byte, which `Spawn` names
disturbed) and `m_hBurningParticle` (`+0x6670`) resolving to a live object: `rate = 1.0 -
0x101beed0(m_flPlayerDist, 200.0, 600.0)` where `0x101beed0` is the clamped fraction (`> max → 1`,
`< min → 0`), and `rate < 0.1` → `0.1f` (`0x3dcccccd`, `0x1037b46c`; equality keeps, the packet's
"at most" is loose); `0x100fb980(rate)` on the particle (`0x1037b4a3`): a negative rate warns and
becomes 0, the emitter's `+0x48c` = rate, times the cvar `DAT_107083dc` when its `+0x4a1`, and `+0x494`
= the engine tick. The second handle test (`0x1037b47d`) cannot fail after the first passed (one
thread); its NULL-receiver arm (`0x1037b4b2`) is dead.

## `0x10394990` `CNPC_VMingXiao::NPCThink()` (926 bytes)

All species work runs BEFORE the Troika body. `switch (m_eThrowableObjectMode +0x673c)` through the
table `0x10394d30` (mode `> 4` unsigned → default): modes 1 and 2 → `flags2 &= 0x7ffffbff`
(`0x10394aa2`) and slot 518 `AddFacingTarget(GetAbsOrigin() + m_vecPickupSavedForward (+0x672c),
1.0, 0.5, 0.0)` (`0x10394b03`); every other mode → `flags2 |= 0x80000400` (`0x10394b0b`).
`RandomInt(0, 99) < 10` (`0x10394b21`/`0x10394b27`): slots 217 and 219 read and discarded, then
`0x102c42a0` seven times — `"Ming_xiao_slimetrail_emitter"` at `"Bip01 TailRoot"` and
`"Ming_xiao_slimetrail_emitter2"` at `"Bip01 Tail1"`..`"Bip01 Tail6"` (`0x10394b50`..`0x10394bc8`).
`0x10398870` false (not possessing): the six regrow timers `m_rflRegrowTimers[i]` at `+0x66f4`,
index order; a timer with `m_flPrevAnimTime (+0x170) > timer` (ordered, strict: `0x10394bf0`) clears
bit `i` of `m_iSeveredTentacleMask +0x6710` (`0x10394c05`), becomes `FLT_MAX` (`0x10394c0b`),
increments `m_iConnectedTentacleCount +0x670c` (`0x10394c1b`), runs `0x10398800` (`0x10394c21`) and
stores `0x103986b0()` in `m_flIdealRange +0x6748` (`0x10394c2d`). `0x10398870` false again →
`CoordinateTroops` (`0x10394c49`). `PushPhysicsObjects` (`0x10394c50`), `0x102c43f0`
(`0x10394c57`), then the Troika body (`0x10394c5e`).

**Correction to the verdict:** the timers are at `+0x66f4`, the count at `+0x670c`, the mask at
`+0x6710` and the pickup forward at `+0x672c` (listing `0x10394bda`, `0x10394c12`, `0x10394bfb`,
`0x10394ab0`); the verdict's `+0x670c` timers / `+0x6748` count are wrong.

## `0x103a05b0` `CNPC_VNewscaster::NPCThink()` (136 bytes)

`m_flNextMoveThink = m_flNextAIThink = curtime + 1.0` (`0x103a05c1`/`0x103a05d6`), `m_flLastMoveThink
= m_flLastAIThink = curtime` (`0x103a05e5`/`0x103a05f6`); the Troika body (`0x103a05fc`); the story
advance `0x103a0670` (`0x103a0603`); the ConVar `*0x1093be8c` not a command, `+0x2c` non-zero, and
`m_debugOverlays (+0x224) & 1` CLEAR → the text dump `0x103a0ff0(0)` (`0x103a0631`).

## `0x103b9040` `CNPC_VTzimisce::NPCThink()` (16 bytes)

`0x102c43f0` (`0x103b9043`), then a tail jump to the Troika body (`0x103b904b`).

## `0x103c6000` `CNPC_VVampireBoss::NPCThink()` (114 bytes; also `CNPC_VSabbatLeader`)

The Troika body (`0x103c6050`), then `m_flNextThink = curtime + 0.1f` (`0x104ce8b8`, `0x103c6063`).

## `0x103dfa20` `CNPC_VZombie::NPCThink()` (197 bytes)

`0x1029f2e0` (`m_bDisableAI`) true → return (`0x103dfa90`) — no base body, no slot 614.
`m_iZombieAIType (+0x6678) == 1` → `0x103e0a00` (`0x103dfa9d`). `m_flSeekDistInspection (+0x63b8) <=
1.0` (ordered; `JP` skips NaN, `0x103dfab3`) → `InitPerceptionDistances` `0x1028fb70`
(`0x103dfab7`). The Troika body (`0x103dfabe`). `__ftol(m_flNextThink) <= 0` → slot 614
(`0x103dfad0`/`0x103dfad6`).

## `0x1035db20` `CNPC_VAndreiBlood::NPCThink()` (90 bytes)

`CNPC_VVampireBoss::NPCThink` (`0x1035db6c`) and nothing else: Andrei keeps the boss's 0.1 s.

## `0x10361490` `CNPC_VAsianVampire::NPCThink()` (114 bytes)

`CNPC_VVampireBoss::NPCThink` (`0x103614e0`), then `m_flNextThink = curtime + 0.1f` (`0x104a9304`,
`0x103614f3`).

## `0x1036c6c0` `CNPC_VChangBros::NPCThink()` (114 bytes; also Blade and Claw)

`CNPC_VVampireBoss::NPCThink` (`0x1036c710`), then `m_flNextThink = curtime + 0.1f` (`0x104ad9f0`,
`0x1036c723`).

## `0x103af830` `CNPC_VSheriffMan::NPCThink()` (114 bytes)

`CNPC_VVampireBoss::NPCThink` (`0x103af880`), then `m_flNextThink = curtime + 0.1f` (`0x104c6120`,
`0x103af893`).

## `0x10375e50` `CNPC_VFrenzyShadow::NPCThink()` (83 bytes) — reviewed, landed earlier

`CNPC_VPlayerController::NPCThink` `0x103a4700` DIRECT (`0x10375e54`); slot 167 (`0x10375e5d`);
`m_NPCState == 2` (`0x10375e6e`), enemy non-null (`0x10375e72`), `HasCondition(0x46)` false
(`0x10375e7f`) → slot 544 `UpdateEnemyMemory(enemy, enemy->GetAbsOrigin(), enemy + 0x3d4)`
(`0x10375e99`). The landed body matches arm for arm.

## `0x103a4700` `CNPC_VPlayerController::NPCThink()` (19 bytes; also `CNPC_VWolfMorph`) — reviewed

`CALL 0x10002a7c` (the Troika body, `0x103a4703`), then `JMP [EAX+0x998]` slot 614
`ResetThinkTimers` as a tail call (`0x103a470d`). The landed body matches.

## `0x103cb590` `CNPC_VWerewolf::NPCThink()` (404 bytes)

1. ConVar `*0x1093f73c` not a command and `+0x2c` non-zero → `EnableDebugStuff` `0x103dbad0(this)`
   (cdecl, `0x103cb616`); falls through.
2. ConVar `*0x1093f95c` the same → the hint overlay `0x103cb4b0` (`0x103cb63b`), slot 614
   (`0x103cb644`), RETURN (`0x103cb652`).
3. `+0x66a0 == 0` or `+0x6720 == 0` → `InitializeHintData` `0x103d7710` (`0x103cb669`), `0x103cade0`
   (`0x103cb670`), `+0x66a0 = 1` (`0x103cb675`).
4. `g_AIDisabled` (`0x1092053c`) bit 0 SET → straight to slot 614 (`0x103cb683` → `0x103cb718`):
   **the Troika body does not run at all** while AI is disabled.
5. `(+0x66e8 & 0x20) == 0x20` → `TeleportOut` `0x103d4a60` (`0x103cb698`).
6. slot 167 non-null (`0x103cb6a9`) → `engine->slot 120 (tick) % 5` (signed `IDIV`, `0x103cb6bf`)
   through the table `0x103cb754`: 0 `UpdateConditionCanTeleport` (`0x103cb6c8`), 1
   `UpdateConditionEnemyUnreachable` (`0x103cb72b`), 2 `UpdateConditionDeathTriggered`
   (`0x103cb734`), 3 `UpdateConditionCanSpecialMove` (`0x103cb73d`), 4 `CheckStuck(1)`
   (`0x103cb746`); every arm rejoins `0x103cb6cf`.
7. slot 167 non-null again (`0x103cb6db`) → slot 600 with it (`0x103cb6eb`).
8. The Troika body (`0x103cb6f4`).
9. `m_fIsUsingSmallHull (+0x5f2d)` and slot 163 `IsViewable()` → `UpdateFakeHull` `0x103d93b0`
   (`0x103cb713`).
10. slot 614 (`0x103cb71c`).

A negative tick would index before the table (retail hazard; not reachable with a monotonic tick).

**Unrecovered:** the two ConVar names (`0x1093f73c`, `0x1093f95c`).

## Port notes (lane L13b)

- **Where the bodies live.** `FElysiumNpc::NPCThink` calls `Think19NormalSet1` / `Think19NormalSet2` /
  `Think19Tail` in retail order. Slot 312 is `UpdateCharacterRetail(float)`, and the AI console gate
  `0x1026c3d0` is `FElysiumNpcBase::Think19AiConsoleGate`. The live loop still runs
  `FElysiumNpc::Think`; the wave-2 rewire replaces it.
- **Seams** (each answers the admitting value or counts):
  - `DAT_1093408c` / `g_pAINetworkManager+0x658`: both answer true.
  - `CacheInterruptConditions` on a base-only body.
  - `0x102bfe10` under `debug_track_under_ground` "0", and `0x1029bd40`: debug-only.
  - `CBaseCombatCharacter::UpdateCharacter 0x103246d0`: its pieces run on their own owners.
  - `0x100fb980`, the emitter's direct rate setter.
  - The werewolf's `EnableDebugStuff 0x103dbad0` and hint overlay `0x103cb4b0`.
  - The engine tick.
- **Named divergences:**
  - The werewolf round robin divides the world clock's whole-frame count, not an engine tick.
  - The boss registry's slot-1 over-read writes the unbound handle.
  - Retail's raw 0 handle is the unbound handle.
  - A NULL obfuscation candidate in `0x103e0a00` answers false.
  - A negative round-robin remainder dispatches nothing.
- **Retail defects reproduced:**
  - A hint with no cover object and no enemy reaches the 0x2e/0x48 tests.
  - `UTIL_Remove` does not end the think.
  - The zombie resets its clocks on every think whose `m_flNextThink` truncates to `<= 0`.
  - `Scream_Death`'s `-1` is cached for the process.
