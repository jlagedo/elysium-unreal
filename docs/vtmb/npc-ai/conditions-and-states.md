# NPC AI — Conditions and states

Part of the [NPC AI oracle](./README.md). Sections moved verbatim from
`npc-ai-reverse-engineering.md` on 2026-09-13; their headers are the citation keys.

## The idle branch, decided

`CNPC_VVampire` has no `SelectSchedule` of its own: vtable `+0x6d8` is `CNPC_VHuman::SelectSchedule`
(`0x10384ee0`, 12 clan classes), which handles only `m_NPCState == 2` and tail-calls
`CAI_BaseNPCTroika::SelectSchedule` (`0x102af660`). **There is no `SCHED_VVAMPIRE_IDLE_STAND` in the
binary** — the only `SCHED_VVAMPIRE*` names are the two VampireBoss transform schedules.

`0x102af660` `case 1` evaluates in this order, and the first match wins:

| Step | Test | Result |
|---:|---|---|
| 1 | `IsBusyWithDiscipline` (`0x1033e2b0`, returns `this+0x14b8 & 1`) **or** `m_bInChoreoScene(+0x5bc4)` | `SCHED_TROIKA_IDLE_DISPOSITION` `0x6b` |
| 2 | virtual `+0x97c` (`0x102b93c0`) returns non-zero | returned verbatim |
| 3 | a patrol path is present | the patrol schedule |
| 4 | `m_bUseInteresting(+0x63d9)` | `0xff` SETUP, or `0x102`/`0x106`/`0x105` on the crosswalk/interact/loiter conditions, else `0x100` |
| 5 | `m_bAllowAlertLookaround(+0x6434)` and `RandomInt(0,99) < min(30, (m_iEnemySightings(+0x60a8)+2)*5)` | `SCHED_TROIKA_ALERT_LOOK_AROUND_NI` `0x4f` |
| 6 | `m_hBlockedDoor` or `m_hCondHitByDoor` is valid and `SelectDoorObstructionSchedule` (`0x102b7370`) returns non-zero | returned verbatim |
| 7 | `m_bReturnToInitialPos(+0x6494)` | `0x45`, else `SCHED_TROIKA_IDLE_DISPOSITION` `0x6b` |

`SCHED_TROIKA_IDLE_STAND` (`0x44`) is returned only for state `0xd`; `_NT` (`0x6c`) is never
returned here. Registered IDs decoded from their registration sites: `0x44` `SCHED_TROIKA_IDLE_STAND`,
`0x45` `SCHED_TROIKA_IDLE_RETURN_TO_INITIAL`, `0x4f` `SCHED_TROIKA_ALERT_LOOK_AROUND_NI`,
`0x6b` `SCHED_TROIKA_IDLE_DISPOSITION`, `0x6c` `_NT`, `0xff`
`SCHED_TROIKA_WALK_TO_INTERESTING_PLACE_SETUP`, `0x100` `SCHED_TROIKA_WALK_TO_INTERESTING_PLACE`,
`0x102` `SCHED_TROIKA_WAIT_AT_CROSSWALK`, `0x105` `SCHED_TROIKA_LOITER`, `0x106`
`SCHED_TROIKA_INTERACT`.

Two consequences carry weight. **An NPC inside a choreographed scene always takes the disposition
stance**, ahead of `use_interesting`, patrol and alert-lookaround. And **virtual `+0x97c` is the
follower controller, not a general priority hook**: it returns `0` immediately unless the follow
target EHANDLE at `+0x647c` is valid, and otherwise picks
`SCHED_TROIKA_FOLLOWER_BACKAWAY`/`_FOLLOW_WALK`/`_FOLLOW_RUN`/`_WAIT` (`0x10c`/`0x112`/`0x113`/`0x115`)
by comparing squared distance against the three radii at `+0x6484`/`+0x6488`/`+0x648c`, which a
`follower_type` row supplies.

`SCHED_TROIKA_IDLE_DISPOSITION` is `TASK_SPECIAL_IDLE_ACTIVITY 5; TASK_WAIT_PVS 0`, and
`SCHED_TROIKA_ALERT_LOOK_AROUND_NI` is
`TASK_SET_ACTIVITY ACT_ALERT_FIDGET_LOOKAROUND; WAIT 3; WAIT_RANDOM 3; SET_ACTIVITY ACT_IDLE; WAIT_RANDOM 2`.
What those idle tasks commit, and how a stance is chosen, is the disposition stance machine in
`animation_and_movers.md`.

`SCHED_TROIKA_IDLE_RETURN_TO_INITIAL` (`0x45`) is the selector's other terminal answer, and unlike
`_IDLE_DISPOSITION` it navigates:

```
TASK_SET_LASTPOSITION_TO_INITIAL 0 ; TASK_SET_TOLERANCE_DISTANCE_ABS 5
TASK_GET_PATH_TO_LASTPOSITION 0    ; TASK_WALK_PATH 0
TASK_WAIT_FOR_MOVEMENT 0           ; TASK_FACE_LASTANGLE 0
TASK_CLEAR_LASTPOSITION 0
Interrupts  COND_NEW_ENEMY COND_SEE_ENEMY COND_SQUAD_SEE_ENEMY COND_SEE_FEAR
            COND_LIGHT_DAMAGE COND_HEAVY_DAMAGE COND_GIVE_WAY COND_INVESTIGATE_SOUND
            COND_INVESTIGATE_SIGHT COND_IGNORE_UNKNOWN COND_DETECTED_ATTACK COND_PLAYER_ON_HEAD
```

## `GetSchedule` `0x102ae920` runs ahead of `SelectSchedule`

`GetNewSchedule` (`0x1028a260`) dispatches slot 437 (`CAI_BaseNPCTroika::GetSchedule`
`0x102ae920`, 55 classes) and, only when it answers 0, slot 438 (`SelectSchedule` `0x102af660`,
the state cases above). A non-zero pre-selector answer therefore pre-empts every state case.
(Name, story 29a: the image calls slot 437 `PreSelectSchedule` — the VProf string of
`CNPC_VSabbatLeader`'s override `0x103aa510` — and the corpus now names `0x102ae920`
`CAI_BaseNPCTroika::PreSelectSchedule`; `GetSchedule` is this file's older label for it.) Read
off the decompilation (story 26 ports it); `HasCondition` is `0x10269aa0`, `HasInterruptCondition`
(needs the bit in the running mask) `0x10269d30`.

1. `+0x1b2c = 2` (the selector trace), `m_InvestigateSound` refreshed (`0x101b9880`).
2. `m_iForcedSchedule` non-zero → consumed and returned.
3. Connected to a squad (`m_iSquadDisconnected < 1 && m_pSquad`) and `flags2 & 0x2000`: clear the
   bit; `+0x65e4 != -1` and not `frenziedFlags & 0x80` → 0xeb; else `SquadNewEnemy`
   (`0x103161a0`) with `GetEnemy()` when set.
4. `HasInterruptCondition(WAS_BUMPED 0x38)` → `0x101e3df0(&DAT_10739a4c, this)`; a running
   schedule translating to 0x14a → `0x101e3ee0(&DAT_10739a4c, this)`. Both UNRECOVERED.
5. State 2: `HasInterruptCondition(SUPERNATURAL_ATTACK_LEVEL 0x22)` → when `m_hClosestPlayer` is
   `m_hSupernaturalOffender`, `ReportSupernaturalAct` (`0x1017f4a0`) and
   `m_iPLSupernaturalActProcessed = 0x1017e740(player)`; then slot 0x950 (`SetEnemy`) and slot
   0x954(offender, 5) on the offender. `CRIMINAL_ATTACK_LEVEL 0x20` mirrors it with
   `ReportCriminalAct` (`0x1017f2a0`) and the obfuscated criminal level `+0x6364`.
   `HasCondition(ON_FIRE 0x30)` → 0x151. `GetEnemy() == NULL` → `SetIdealState(no_alert_state ?
   1 : 3)` (`0x1026e340`) and `return GetNewSchedule()` (re-entry). `flags1 & 0x800000`
   (`ATTACK_UNKNOWN`) → cleared; `flags2 & 0x80 == 0` and not frenzied 0x80 → 0x5b, else
   `flags2 &= ~0x80`. `HasCondition(NEW_ENEMY 0x54)` and not frenzied 0x80 → 0xea
   `START_COMBAT`.
6. Any state: `HasCondition(PLAYER_ON_HEAD 0x3b)` and `!IsBusyWithDiscipline()` → cleared; with
   no live `m_hDialogPartner`, `DAT_1092450c` (an interface; slot 1 false and `[0xb] < 4`
   selects a mode, else 0) → mode 0 → 0x7b, 1 → 0x79, 2 → 0x7a, 3 → `RandomInt(0, 99) < 80 ?
   0x79 : 0x7a`. UNRECOVERED: the interface and the three programs.
7. State 0xe: the criminal half of 5, `ON_FIRE` → 0x151, no enemy → the same ideal-state re-entry.
8. Base `0x1028a2a0` (`+0x1b2c = 1`): `FLOATING_OFF_GROUND 0x73` → gravity 1.0 and slot
   0x340(0); `NPC_FREEZE 0x75` → 0x3a; `ON_FIRE 0x30` → 0x151; `FLOATING_OFF_GROUND` → 0x3e.
   Non-zero returns.
9. `flags2 & 2` → `flags1 &= ~8`, `flags2 &= ~2`; navigator goal type (`+0x5d34` → `+0x18`,
   `0x1027d990`) 3 → 0xfc `FINISH_CLIMB`, 1 → 0xfd `FINISH_JUMP`.
10. State 2, `m_bStayEntrenched`, slot 0x940 true → `0x102b7690(1, 0, 0, 0)` non-zero returns
    (the cover chooser).
11. `flags1 & 2` → cleared, 0xf1 `STARTLED`.
12. State 1 (idle): `KNOCKBACK 0x28` → 0x14c; `COMFORT 0x27` → 0x12f; `D_CALM 0x10000` → 0x130
    `CALMED`; `D_FOLLOW 0x100000` → 0x131 `FOLLOW`; `D_POSSESSED 0x40000` → 0x131; a live
    `m_hDialogPartner` → 0x6a. State 2: `KNOCKBACK` → 0x14c. State 3: `0x102b8a10` — `ENEMY_DEAD
    0x58` and `SelectWeightedSequence(ACT 0x61) != -1` → 8.
13. `m_fSavePositionWalk` → cleared, 0x89. Else 0, and `SelectSchedule` runs.

The port's law branch (`ElysiumNpcWitness::SelectLawSchedule`) sits at the top of `SelectSchedule`
by a CHOSEN position; retail's is step 5/7 here. `[VtMB]`

**Story 26 recovery (2026-09-12).** The body re-read whole; the unrecovered items above close
as follows, and two steps are corrected.

- **Step 4 is the discipline manager, not a bump handler.** `DAT_10739a4c` is the global
  `CDisciplineManager` (76 readers: `AddDiscFlag`, `OnTakeDamage`, the HitGroup chain…).
  `0x101e3df0(mgr, npc)`: for each of the 30 discipline bits set in the NPC's active-effect word
  (`npc+0xeb4`) whose record byte `+0x33` is set, `RemoveEffect` (`0x101e3af0`, DevMsg
  `"Discipline<%s> RemoveEffect"`, with the `bumped` flag) — i.e. **being bumped ends every
  active discipline effect authored as bump-interruptible**. `0x101e3ee0(mgr, npc)`: for each
  active effect whose record byte `+0x34` is set, `InterruptSchedule` (`0x101e3a30`,
  `"Discipline<%s> InterruptSchedule"`, the `OnInterruptSchedule` HitInfo at record `+0x2b8`) —
  run when the installed schedule is **`0x14a SCHED_TROIKA_D_MESMERIZE`** (`0x102cc1f0(0x14a)`
  compared by pointer). Neither returns a schedule. The two record bytes are parsed by the
  HitGroup loader beside `DoPossession`/`DoFrenzy` (story 0006 owns their key names —
  UNRECOVERED here).
- **Step 6's interface is a ConVar**: `DAT_1092450c` → `cvar_debug_player_on_head`
  (`0x10924508`, ctor `0x1028bc20`: name `"debug_player_on_head"`, default `"3"`, bounded
  `0..3`, help `"What should I do if the player i…"`); `slot 1` false and `[0xb] < 4` is the
  ordinary ConVar int read. Mode 0 → `0x7b SCHED_TROIKA_PLAYER_ON_HEAD_RUN`
  (`GET_PATH_TO_RANDOM_NODE 256; RUN_PATH; WAIT_FOR_MOVEMENT`, no interrupts), 1 → `0x79
  _DIVE` (`SET_FAIL_SCHEDULE _RUN; TASK_ATTEMPT_DIVE_SIDE 0; SET_SCHEDULE _RUN`), 2 → `0x7a
  _DIVE_FORWARD` (`SET_FAIL_SCHEDULE _DIVE; TASK_ATTEMPT_DIVE_FORWARD 0`), 3 (the shipped
  default) → 80 % `0x79`, else `0x7a`. The arm also `ClearCondition(PLAYER_ON_HEAD)` first,
  and only runs with **no live `m_hDialogPartner`** and `!IsBusyWithDiscipline`.
- **Names.** `0x3a` = base `NPC_FREEZE` (`TASK_FREEZE 0`, interrupt `COND_NPC_UNFREEZE`),
  `0x3e` = base `FALL_TO_GROUND` (`TASK_FALL_TO_GROUND 0`), `0x6a` =
  `SCHED_TROIKA_RUN_DIALOG` (`TASK_RUN_DIALOG 0`, interrupt `COND_PROVOKED`), `0x89` =
  `SCHED_TROIKA_RUN_TO_SAVED` (list in "The alert programs, verbatim"), `0x14a` =
  `SCHED_TROIKA_D_MESMERIZE`, `0x14c` = `SCHED_TROIKA_KNOCKBACK` (`SET_FAIL_SCHEDULE
  MELEE_IDLE; STOP_MOVING; ADD_EVENT_EXPRESSION KNOCKBACK; TASK_MELEE_KNOCKBACK 0`), `0x151`
  = `SCHED_TROIKA_ONFIRE` (`SET_FAIL_SCHEDULE FLEE_AND_COWER_SCREAM; STOP_MOVING;
  ADD_EVENT_EXPRESSION KNOCKBACK; ON_FIRE_INTO; ON_FIRE_LOOP; ON_FIRE_OUTOF`), `0xf1` =
  `SCHED_TROIKA_STARTLED` (`WAIT_RANDOM 0.50; STOP_MOVING; SET_ACTIVITY ACT_WALK`; interrupts
  `LIGHT_DAMAGE HEAVY_DAMAGE`), `0xfc` = `FINISH_CLIMB` (`WALK_PATH; WAIT_FOR_MOVEMENT`), `0xfd`
  = `FINISH_JUMP` (`TASK_JUMP 0; TASK_LAND 0`), `0x130` = `SCHED_TROIKA_CALMED`
  (`SET_NPC_FLAG DONT_INVESTIGATE; NO_DIALOG; D_CALM; SET_FAIL_SCHEDULE Idle_Stand;
  SET_TOLERANCE_DISTANCE 120; GET_PATH_TO_TARGET; WALK_TO_TARGET; WAIT_FOR_MOVEMENT;
  FACE_TARGET; SET_FAIL_SCHEDULE CALMED; WAIT 0.2; WAIT_PVS`, no interrupts, `DELAY_INTERRUPTS`),
  `0x131` = `SCHED_TROIKA_FOLLOW` (the same with `D_FOLLOW`, tolerance 60, `RUN_TO_TARGET`,
  `WAIT 0.1`, interrupts `NEW_ENEMY SEE_ENEMY SQUAD_SEE_ENEMY SEE_FEAR`, no flag). Both
  re-select themselves on failure, so a calmed/followed NPC whose target is unreachable
  re-tries at once, forever. `+0x65e4` is `combat_start_activity`'s resolved id (the
  keyfields section): the squad arm returns `0xeb` only for an NPC that authored one.
- **Step 8 corrected**: base `GetSchedule` (`0x1028a2a0`) tests `NPC_FREEZE 0x75` → `0x3a`
  first, then `ON_FIRE` → `0x151`, then `FLOATING_OFF_GROUND 0x73` → gravity, slot `0x340(0)`,
  `0x3e` — the summary above listed the gravity write ahead of the freeze test.
- **Base `SelectSchedule` `0x1028a380`** (the `default:` of the Troika switch, states 0, 4–7,
  9–0xc): state 1 → `HEAR_DANGER/COMBAT/WORLD/BULLET_IMPACT/PLAYER` → 6 (`ALERT_FACE`);
  `GIVE_WAY 0x68` → `0x38`; navigator goal type 0 → 1 (`IDLE_STAND`); `LIGHT_DAMAGE` with an
  `ACT 0x49` sequence → `0x14`; else 2 (`IDLE_WALK`). State 3 → `ENEMY_DEAD` + `ACT 0x61` → 8;
  damage → face/flinch (`0x19`/7/6); hear-family → 6; else 9 (`ALERT_STAND`). State 4 → a live
  `m_hCine` → `0x2e AISCRIPT`, else `"Script failed for %s"`, `CineCleanup 0x1027d170`, 1.
  State 6 → 1; 7 → ragdoll ? `0x2c` : `0x2b`; `0xc` → 1; state 0 (`"NPC_STATE_IS_NONE!"`),
  the combat fall-through (`"No suitable combat schedule"`) and any other state
  (`"Invalid State for SelectSchedule"`) → **`0x43 FAIL`**. Troika's own switch covers 1, 2, 3,
  8, 0xb, 0xc, 0xd, 0xe, so the base arms a Troika NPC can reach are 4, 6, 7 and the `0x43`
  defaults.

Unrecovered after this pass: the HitGroup record keys behind bytes `+0x33`/`+0x34` (0006), the
`TASK_ATTEMPT_DIVE_*` and `TASK_RUN_DIALOG` arms, `TASK_MELEE_KNOCKBACK`, the `ON_FIRE_*` trio.

## `m_bReturnToInitialPos` is a one-shot armed only by alert or combat

`CAI_BaseNPCTroika + 0x6494` decides between those two terminal answers, and it carries **no
external name** in the datamap (`0x105ce470`), so no map, FGD property or entity input can set it.
`CAI_BaseNPCTroika`'s spawn (`0x1029a0b0`) clears it, and the selector (`0x102af660`) consumes it:
reading it true both returns `0x45` and writes it back to false in the same branch.

The only writer is `OnStateChange` (`0x102ae140`, `CNPC_VVampire` vtable slot `[463]`), which sets it
on transition **into ALERT (2) or COMBAT (3)** — plus custom states `8`, `0xb` and `0xe`. `DEAD` (7)
and the `default` case do not, and **`SCRIPT` (4) falls into that default**, so entering or leaving a
`scripted_sequence` never arms it.

The consequence is load-bearing for authored scenes: an NPC that has only ever sat under script
control has the flag clear, so when a beat releases it the selector returns
`SCHED_TROIKA_IDLE_DISPOSITION` — which carries no navigation task at all. **A released, enemy-less
NPC stands still.** It can only move in that gap by first entering alert or combat, which also arms
the walk-home for its next idle reselection.

## `no_alert_state` does not suppress the alert state

Datamap offsets, from the `typedescription_t` records: `m_bNoAlertState` `0x65f6`,
`m_bAllowAlertLookaround` `0x6434`, `m_iNPCPerception` `0x63b0`, `m_flSeekDistBase` `0x63b4`,
`m_flHearingScalarBase` `0x63bc`.

The keyvalue does real work in four places — `TASK_SUGGEST_STATE` rewrites a request for state 3 to
state 1 (`0x102a1bc8`); `CAI_BaseNPCTroika::SelectIdealState` (`0x102ad660`) skips its damage and
sense promotions; `CNPC_VHuman::SelectIdealState` (`0x103851e0`) and
`CNPC_VHumanCombatPatrol::SelectIdealState` (`0x10387380`) gate theirs on it; and `CNPCMaker`'s
spawn helper (`0x10310c10`) puts a child into state 1 rather than 3.

It is nonetheless **not a suppression**. `0x102ad660` ends in an unconditional
`return CAI_BaseNPC::SelectIdealState(this)` (`0x1026f660`), whose `case 1` promotes idle → alert on
`COND_LIGHT_DAMAGE`, `COND_HEAVY_DAMAGE` and the whole hear-family **with no `m_bNoAlertState`
test**. A second route is independent of it entirely: `SCHED_TROIKA_ALERT_LOOK_AROUND_NI` is
selected from state 1 gated only on `m_bAllowAlertLookaround` and its chance roll.

`InitPerceptionDistances` (`0x1028fb70`) reads `vision` and `hearing` as sentinels: **`-1.0f` means
derive from `npc_perception`**, indexing the `Inspection_Vision_Distances` and
`Inspection_Hearing_Scalars` tables into the effective values at `+0x63b8` and `+0x63c0`; any other
authored value is copied through and `npc_perception` is then inert for that NPC.

## The three `GatherConditions` sweeps and the interest predicate

`CAI_BaseNPCTroika::GatherConditions` (`0x102b27f0`) makes **three** consecutive calls, and neither
of the two previously named iterates a list:

- **`0x102b15c0` — sight**, but the *see-unknown* channel only: one entity, `m_hBestSeeUnknown`
  (`+0x6088`), player-only by construction (requires the cached `CBasePlayer*` at `+0xa8`). Not
  seeing → the 1.5 s grace timer (`+0x6084`), then `COND_LOST_UNKNOWN` and `m_vecLastSeeUnknownPos`
  (`+0x6090`). Seeing → a concealment test, a one-shot `MADE_INITIAL_RESPONSE` roll setting
  `ATTACK_UNKNOWN` or `IGNORE_UNKNOWN` off `m_iSeeUnknownRepeatSightings` (`+0x60a4`) and
  `m_bFullInvestigate` (`+0x6340`), then a 2-D closing-speed classification against `20.0f` into
  `COND_UNKNOWN_ADVANCING/HOLDING/RETREATING` and `COND_INVESTIGATE_SIGHT` (0x26). Retail quirk:
  `HOLDING` needs exact float equality and is effectively dead.
- **`0x102b1a20` — the comfort list**, rate-limited 0.2–0.4 s (re-armed in every state), searched
  only in idle: walks the global `AddToComfortList`/`RemoveFromComfortList` array
  (`0x10323630`/`0x10323770`), nearest at or within 1024 units, at most 3 comforted NPCs per
  comforter, sets `COND_COMFORT` (0x27) and `m_hTargetEnt`. Walked whole below.
- **`0x102b1cd0` — sound**: the six fixed `CSound` records (`m_LastSoundWorld` `+0x61e4`,
  `PhysicsDanger` `+0x6134`, `Danger` `+0x6108`, `Player` `+0x61b8`, `BulletImpact` `+0x618c`,
  `Combat` `+0x6160`), gated on `m_flNextInvestigateSoundTime` (`+0x623c`). Each arm:
  `HasCondition(HEAR_X) && (schedule already interrupts on HEAR_X || ShouldInvestigate(owner, b))`
  → `COND_INVESTIGATE_SOUND` (0x25); `HEAR_DANGER` skips the predicate. Last wins: combat > bullet
  impact > player > danger > physics danger > world. Then `COND_HEAR_FLANK_SOUND` (0x33) and a
  `COND_SEE_SOUND_SOURCE` (0x2d) tail. Walked whole below.

### The see-unknown sweep `0x102b15c0`, walked

Read off the decompilation and disassembly while porting story 10b. `this+0x1821` (dword index) is
`+0x6084`, the same field the summary above names as the grace timer; `this+0x52e` is `+0x14b8`,
`m_bfAINPCFlags` word one, read and written as a raw dword throughout.

**Entry.** `HasCondition(SEE_UNKNOWN 0x01)` — `0x10269aa0`, needs no schedule — is the whole branch.

**Not seeing it (`SEE_UNKNOWN` false).** Calls `FUN_1028e360(this)`, walked next; a false return is
a plain `return` with nothing touched. A true return asks `0x10269c70(this, LOST_UNKNOWN 0x02)` —
the mask-only tester `GatherSounds`'s doc also names, needing an installed schedule and the bit in
the mask, never the condition set — and only then `SetCondition(LOST_UNKNOWN)`.

`FUN_1028e360`, the grace timer, in full:

```
m_hBestSeeUnknown (+0x6088) == -1        -> return true                    (nothing tracked)
resolve the handle; slot dead/mismatched -> return true                    (stale, no grace)
else, the handle is live:
  +0x6084 == 0xbf800000 (-1.0f, the sentinel)
    -> +0x6084 = curtime + 1.5f (_DAT_1044f02c)   ; return false           (arm the grace)
  curtime < +0x6084
    -> return false                                                       (still holding)
  else (grace elapsed):
    +0x6088 = -1                                                          (drop the handle)
    resolve m_hLastSeeUnknown (+0x608c); GetAbsOrigin() (vfunc +0x364) into +0x6090/+0x6094/+0x6098
    -> return true
```

The `+0x608c` resolve has no null test: an unresolved handle leaves `ECX = 0` (`0x1028e40f`) and
`0x1028e411 MOV EDX,[ECX]` faults. The port keeps the last recorded position instead.

A `true` return is "answer `LOST_UNKNOWN` now" in every one of its three arms (nothing tracked, a
stale handle, or the grace has elapsed) — only the third also writes `m_vecLastSeeUnknownPos`
(`+0x6090`) and drops the handle; the other two leave `+0x6088` exactly as they found it. `false` is
the two "wait" arms: the grace was just armed, or it is still running.

**Seeing it (`SEE_UNKNOWN` true).** `+0x6084 = 0xbf800000` unconditionally — every real sighting
resets the grace sentinel, whether or not anything below finds a player to classify. Then the
committed `m_hBestSeeUnknown` entity is resolved (through slot 586, `0x101aa5d0`, which returns
`m_hBestSeeUnknown` for every Troika class) and its `+0xa8` field (the cached `CBasePlayer*`,
the same field the outer-band attention path `0x102b3e00` reads) is tested: null is a bare `return`,
so a tracked non-player entity is never classified past this point — the channel is player-only by
construction, not by a relationship or classname test.

`this+0x14bb` is the top byte of `m_bfAINPCFlags` (bits 24–31), so `~byte & 1` is exactly
"`MADE_INITIAL_RESPONSE` (`0x01000000`) is NOT set" — the one-shot latch both rolls below share.

`bVar3 = FUN_101671a0(player)` — the same admission test the outer-band path uses, true for a player
who is obfuscated (`0x10146a80`: the stat-8 effect list non-empty and `+0x14dc` set), or in a grapple
with `+0x1540 == 3`, or `FL_DUCKING` and `!0x101672d0`; ported as
`FElysiumPlayer::IsInStealthPosture()`. It is a stealth test, not a visibility test. **`!bVar3` (plainly visible)**
takes the first roll; **`bVar3` (hidden/grappled/crouched-unseen)** takes the second and the
closing-speed classification.

**Roll A — plainly visible.** Only when `MADE_INITIAL_RESPONSE` is unset: set it; `threshold =
min(100, (m_iSeeUnknownRepeatSightings(+0x60a4) + 5) * 20)`; `RandomInt(0, 99) < threshold` sets
`ATTACK_UNKNOWN` (`0x00800000`), else clears it. **The threshold is never below 100** for any
`repeat_sightings >= 0` (the only reachable domain — the field is only ever `++`'d or zeroed), so
the roll is retail dead code on its own terms: `ATTACK_UNKNOWN` deterministically sets every time
this arm's one-shot fires — but the `RandomInt(0, 99)` draw is still taken, so the stream advances. Then, UNCONDITIONALLY (latched or freshly rolled): `ATTACK_UNKNOWN`
standing sets `COND_UNKNOWN_RUN_TIMER` (0x04); `ShouldInvestigate(this, player, false)` (`0x102b3270`
via `0x1000e5e3`) gates `SetCondition(INVESTIGATE_SIGHT 0x26)`. Returns.

**Roll B — hidden/grappled/crouched-unseen.** Only when `MADE_INITIAL_RESPONSE` is unset: set it;
`threshold = max(0, 50 - repeat_sightings * 20)` (no upper clamp — 50 is already the ceiling at
`repeat_sightings == 0`); `m_bFullInvestigate (+0x6340) == 0 && RandomInt(0, 99) < threshold` sets
`IGNORE_UNKNOWN` (`0x00400000`, also clearing `FINISHED_IGNORE_UNKNOWN` `0x02000000`), else clears
`IGNORE_UNKNOWN`. Unlike Roll A this ceiling IS reachable (50% at zero repeats, 0% from three on),
so the roll is live.

Then, unconditionally, the 2-D closing speed. `(dx, dy, dz) = GetAbsOrigin(this) -
GetAbsOrigin(player)` is written to a scratch triple (`[ESP+0x1c]`); `(dx, dy, 0)` is copied to
`[ESP+0x10]` and normalized in place by `VectorNormalize` (`0x1057966c`, `ECX = &[ESP+0x10]`), whose
returned length is discarded (`FSTP ST0`). `fVar2 = dot((dx, dy)_normalized, (vel.x, vel.y))` reads
that normalized vector, where `vel` is the player's `m_vecVelocity` (`CBasePlayer+0x3d4`, copied to
`[ESP+0x28]` before the origins are fetched). The velocity is never normalized; `fVar2` is Source
units per second. Positive is the player closing on this NPC. The port's player velocity is world
cm/s, fed each frame by `FElysiumPlayer::SyncFromBody`, and is divided by `ElysiumMove::U` before the
compare.

- `IGNORE_UNKNOWN` standing: `fVar2 > 20.0f (_DAT_1044eb0c)` **and**
  `m_flSeeUnknownStartTimer (+0x60a0) <= curtime` → `SetCondition(UNKNOWN_ADVANCING 0x05)` +
  `SetCondition(INVESTIGATE_SIGHT)`, return. Otherwise: `ClearCondition(SEE_UNKNOWN 0x01)` — the
  mid-sweep retraction, overriding what the sense pass raised earlier in this same
  `GatherConditions` call — then, unless `LOOKED_AT_UNKNOWN (0x00200000)` or
  `FINISHED_IGNORE_UNKNOWN` already stands, `SetCondition(IGNORE_UNKNOWN 0x03)`. Return either way.
- Not ignoring: `SetCondition(INVESTIGATE_SIGHT)` unconditionally, then `fVar2 < 20.0f` →
  `UNKNOWN_RETREATING` (0x07); else `fVar2 <= 20.0f` (reachable only on exact equality, since `<`
  already took the strictly-below case) → `UNKNOWN_HOLDING` (0x06), retail's own dead arm; else
  `UNKNOWN_ADVANCING` (0x05).

Constant `_DAT_1044eb0c = 20.0f` and `_DAT_1044f02c = 1.5f` are shared `.rdata` pool values with
dozens of unrelated readers (damage force vectors, a rat-plane solver, weapon code), so neither can
be pinned to this sweep from the corpus's referrer list alone; both are read here as the closing-
speed edge and the grace duration respectively, matching the summary above.

Job done as `ElysiumNpcCond::GatherSeeUnknown`, called from `ElysiumNpcEnemy::GatherConditions`
immediately after `GatherSight` (which raises `SEE_UNKNOWN` from `TickSight`'s outer-band write) and
before `GatherComfort`, matching retail's own see-unknown / comfort / sound order. `[VtMB]`

### The comfort sweep `0x102b1a20`, walked

Read off the disassembly (the decompiled C hides the order of the idle test and the `<=`).

**Clock, then state.** `FCOMP [ESI+0xe90]` at `0x102b1a30`: `curtime < m_flNextComfortCheckTime`
returns, touching nothing. Otherwise `RandomFloat(0.2, 0.4)` (`0x102b1a55`) and the re-arm `+0xe90 =
curtime + draw` (`0x102b1a67`) run **before** `CMP [ESI+0x5cc0],1 / JNZ 0x102b1c0f` (`0x102b1a60`).
A due NPC re-arms and consumes the draw in every state; a non-idle one goes straight to the tail.

**The search.** Walks the global comfort-target array (`DAT_10937af4` / count `DAT_10937b00`,
written only by `AddToComfortList` / `RemoveFromComfortList` `0x10323630` / `0x10323770`, story 8)
in array order, resolving each handle with no null check (a stale entry would fault). Skips `this`.
`GetAbsOrigin` (slot 220) on both sides, full 3-D distance, then `FCOM best / TEST AH,0x41 / JP skip`
(`0x102b1af6`): the jump is taken only for "greater" or unordered, so a candidate is kept at
**`distance <= best`**. The best is seeded `0x44800000` (1024.0): a comforter at exactly 1024 units
qualifies, nothing beyond does, and a tie goes to the **later** array entry. An empty array skips
the loop and falls to the tail.

**Found one.** `IsBusyWithDiscipline()` (`0x1033e2b0`) or `m_pSchedule (+0x5c38) == NULL` → tail.
Then the running schedule's id through slot 447 compared with `0x12f`, `SCHED_TROIKA_COMFORT`:

- **Running it:** `m_hTargetEnt (+0x5ce4)` still resolving to this nearest comforter → return.
  Otherwise `0x10273e80(this, false)` and return. `0x10273e80` is `TaskComplete(false)`: `+0x5c44`
  (`m_ScheduleState.fTaskStatus`) `= 4` (`TASKSTATUS_COMPLETE`) unless `COND_TASK_FAILED 0x5c`
  stands. It completes the current task, it does not fail the schedule.
- **Not running it:** the squad test. `[comforter+0x94]` is the entity's cached `CAI_BaseNPC*`
  (null for a non-NPC): `CAI_ChangeTarget::InputActivate` (`0x101c99c0`) reads it to clear an NPC's
  goal entity (`+0x5de8`), the shape of Source's `MyNPCPointer()->SetGoalEnt(NULL)`. When non-null,
  both sides compute `m_iSquadDisconnected (+0x5bb0) < 1 ? m_pSquad (+0x5da4) : NULL` and a mismatch
  returns. `+0x5da4` is the squad pointer: its readers pass it to the `CAI_Squad` member calls
  (`0x103158f0`, `0x10316660`, `0x103161a0`) and read a name at `+4`. A player comforter skips the
  test. Then `CMP [comforter+0xe94],3 / JGE` returns (no fallback to the next-nearest); otherwise
  increment `m_iComfortingCount`, a `VPROF` scope (`DAT_10924a6c`), `SetCondition(COMFORT 0x27)` and
  `SetTarget(comforter)` (`0x10279cc0`: `m_hTargetEnt = handle`, or `-1` for null).

**The tail (`0x102b1c0f`).** Not idle, no candidate, busy, or no schedule: a running `0x12f` gets
`TaskComplete(false)`.

**What reads the result.** `GetSchedule` (`0x102ae920`) idle chain returns `0x12f` on `COMFORT`
(after `KNOCKBACK` → `0x14c`). The program, verbatim from the blob:

    SCHED_TROIKA_COMFORT
      TASK_SET_NPC_FLAG NPCFlag:DONT_INVESTIGATE; TASK_SET_NPC_FLAG NPCFlag:NO_DIALOG;
      TASK_SET_FAIL_SCHEDULE SCHEDULE:Idle_Stand; TASK_SET_TOLERANCE_DISTANCE 60;
      TASK_GET_PATH_TO_TARGET 0; TASK_RUN_TO_TARGET 0; TASK_WAIT_FOR_MOVEMENT 0; TASK_FACE_TARGET 0;
      TASK_PLAY_COMFORT_INTO 0; TASK_DO_COMFORT_LOOP 0; TASK_PLAY_COMFORT_OUTOF 0; TASK_WAIT 4;
      TASK_WAIT_PVS 0
    Interrupts: NEW_ENEMY SEE_ENEMY SQUAD_SEE_ENEMY SEE_FEAR LIGHT_DAMAGE HEAVY_DAMAGE GIVE_WAY
      HEAR_DANGER

While `0x12f` runs, `CAI_BaseNPC::GatherConditions` (`0x1026ec30`) plays the idle sound through slot
`0x7dc` instead of `0x7a8`, and `0x1027a420` rolls `RandomInt(0, 20)` instead of `(0, 999)`.
`m_hTargetEnt` is read every think by `CAI_BaseNPC::GatherConditions` → `CAI_Memory::CheckTarget`
(`0x10271d10`: clears `0x4b`/`0x49`, sets `0x4b` when the target is `FVisible` under mask
`0x2804091`, else `0x49`; then `0x10271b10` refreshes a navigator goal that targets it), and by the
eye maintainer's target arm (`0x1026b810`).

**The port.** `ElysiumNpcCond::GatherComfort` is the whole sweep: the clock/re-arm/idle order, the
`<=` search, both `0x12f` arms (`TaskComplete(false)` is `Schedule.bTaskCompletedExternally`), the
squad test through `FElysiumNpc::ConnectedSquad`, the cap, `COMFORT` and `SetTarget`.
`FElysiumNpc::GazeTargetEntity` answers the target, so an idle NPC's eyes take its comforter. Save
schema `ComfortSweep` carries `m_flNextComfortCheckTime` and `m_hTargetEnt`.

Not in the port yet, each with its story in `docs/specs/0002-npc-ai/spec.md`:

- **No squad object** (17). `ConnectedSquad` answers null for every NPC, so the squad test always
  passes.
- **No `COMFORT` consumer.** The `GetSchedule` idle-chain arm is 26; the program, tasks
  `GET_PATH_TO_TARGET 0x15` / `RUN_TO_TARGET 9` / `FACE_TARGET 0x31` / `PLAY_COMFORT_INTO 0xec` /
  `DO_COMFORT_LOOP 0xed` / `PLAY_COMFORT_OUTOF 0xee`, `COND_GIVE_WAY 0x68` (readers `0x1028a380`
  and `CNPC_VZombie` slot 438; no `SetCondition` site carries the literal), the `Idle_Stand` fail
  route and the two `0x12f` readers are 10i. Until then both `0x12f` arms of the sweep never fire.
- **No `CheckTarget`** (10j). `0x4b`/`0x49` and the goal refresh (`0x10271b10`: a navigator goal
  on the target whose flags carry 4 re-paths when the target moved more than `_DAT_104454c8`; a
  goal on another entity is re-pointed at the target) are not gathered.

`ScheduleHost.MoveTarget` is NOT this field: it is Troika's `m_hMoveTargetEnt`, -1 at spawn
(`0x1029a0b0`) and released by `TaskFail` (`0x1029adb0`) and `OnScheduleChange` (`0x102a0940`),
neither of which touches `m_hTargetEnt`. `SetTarget` (`0x10279cc0`) has 16 direct callers: this
sweep, the cine family (`CCineNPC`/`CCineAI` slot 583, `CineCleanup 0x1027d170`, 0003/2),
`StartTask` (base and werewolf), `DoPossession 0x102c51a0` (16c), the follower selector
`0x102b93c0` (16a), `CScriptedTarget`, `CSceneEntity` slot 261, `0x1027c300`, `0x102aa210`,
`CNPC_VZombie` slot 420.

Called from `ElysiumNpcEnemy::GatherConditions` between `GatherSeeUnknown` and `GatherSounds`. `[VtMB]`

**Stories 10i and 10j recovery (2026-09-12).** The three comfort task arms, the `0x12f`
readers, `Idle_Stand` and `CheckTarget`, closed.

- **`TASK_PLAY_COMFORT_INTO 0xec`** (Troika `StartTask` `0x102a51b3`): `act = 0x106a + 3 ×
  RandomInt(0, 1)`; `SelectWeightedSequence(act, -1)`; `SetIdealActivity(act)` (`0x10272650`).
  `0x106a` is `ACT_COMFORT_INTO` and the stride of 3 steps to `ACT_COMFORT2_INTO`; the roll is
  `(0, 1)`, so **`ACT_COMFORT3_*` is never chosen by this task** (the names exist in both
  images; the third variant is reachable only by name). Run arm `0x102ab83c` (shared with
  `PLAY_COWER`, `KICK_HINT`, `KICK_PROP`): `AutoMovement`, then complete when the sequence is
  finished (slot `0x3ec`).
- **`TASK_DO_COMFORT_LOOP 0xed` and `TASK_PLAY_COMFORT_OUTOF 0xee` share one start arm**
  (`0x102a51e8`): `m_Activity (+0xfec) != -1 ? SetIdealActivity(m_Activity) : m_Activity = 0`.
  Re-submitting the current activity is how the INTO→IDLE→OUTOF chain advances:
  `CAI_BaseNPC::SetIdealActivity` (`0x10272650`) is a `switch` on the activity (its jump table
  is the damaged one the corpus flags) that maps an `_INTO` to its `_IDLE` and an `_IDLE` to its
  `_OUTOF` — UNRECOVERED case by case; the comfort trio is one of its rows. **`DO_COMFORT_LOOP`'s
  run arm is `0x102aad71`, a bare `RET`**: the loop never completes on its own; it ends only
  through the sweep's two `TaskComplete(false)` arms above (a new nearest comforter, or leaving
  idle / losing the candidate / getting busy / no schedule). `PLAY_COMFORT_OUTOF`'s run arm is
  the shared sequence-finished test.
- **The `0x12f` readers**: `CAI_BaseNPC::GatherConditions` (`0x1026ec30`) plays the idle
  vocalisation through slot `0x7dc` instead of `0x7a8` while `0x12f` runs, and `0x1027a420`
  rolls `RandomInt(0, 20)` instead of `(0, 999)` — both already stated above; no third reader.
- **`Idle_Stand`** is base schedule 1 (`STOP_MOVING; SET_ACTIVITY ACT_IDLE; WAIT 5; WAIT_PVS`,
  interrupts `NEW_ENEMY SEE_FEAR LIGHT_DAMAGE HEAVY_DAMAGE SMELL PROVOKED GIVE_WAY HEAR_PLAYER
  HEAR_DANGER HEAR_COMBAT HEAR_BULLET_IMPACT`; "The kernel's failure route and the base
  programs, walked"), and the fail route translates it through slot 440 to `0x6b
  IDLE_DISPOSITION` on a Troika NPC — the comfort program's failure lands there, not on base 1. **`COND_GIVE_WAY 0x68`'s reader** is the base idle selector `0x1028a380`
  → `0x38 GIVE_WAY` (`SET_ROUTE_SEARCH_TIME 0.5; SET_TOLERANCE_DISTANCE 5;
  GET_PATH_TO_SAVEPOSITION 2; RUN_PATH_TIMED 2.0; WAIT_FOR_MOVEMENT`), unreachable on a Troika
  NPC whose class handles state 1 itself; the condition stays producer-less.
- **`CAI_Memory::CheckTarget` `0x10271d10`** (VProf `"CAI_Memory::CheckTarget"`): clear `0x4b`
  and `0x49`; `FVisible(target, 0x2804091)` (slot 201) → set `0x4b HAVE_TARGET_LOS` else `0x49
  TARGET_OCCLUDED`; then `0x10271b10`: with the navigator's goal type (`+0x18`) not 3 or 1 and
  a goal present (`0x102ee620` → `0x100113d8(goal)`): the goal's entity (`0x102ee160`) equal to
  `m_hTargetEnt` and the goal flags (`0x102ee640`) carrying `4` → when the target is farther
  than **`_DAT_104454c8 = 80.0` units** from the goal point (`0x102ee140`), re-path
  (`0x10007b4e` → `0x102f1dc0`, the navigator's route rebuild); a goal on another entity →
  `0x102ed310(target, vec3_origin)` (goal entity := target, offset zero, then the same rebuild).
  Slot `0x364` (217) is **`CBaseEntity::GetAbsOrigin`** on all 497 classes; the "slot 220"
  named in the sweep text above is `0x370`, a different accessor — re-read that line before
  relying on it (UNRECOVERED which of the two the sweep's distance uses).

Unrecovered after this pass: `SetIdealActivity`'s transition rows, whether the goal flag `4`
carries a name, `FUN_100113d8`'s exact "has goal" answer.

### The sound sweep `0x102b1cd0`, walked

Read off the decompilation while porting story 10a; it corrects the summary above in three places.

**Entry.** `ClearCondition(INVESTIGATE_SOUND 0x25)` and `ClearCondition(HEAR_FLANK_SOUND 0x33)`,
unconditionally, before the gate. `GatherConditions` itself clears neither, so the sweep owns them.
`SEE_SOUND_SOURCE` is *not* cleared here — its tail has two arms that leave it standing.

**Gate.** The whole six-arm body runs only when `m_flNextInvestigateSoundTime (+0x623c) <= curtime`.
The gate is never re-armed inside this function; all six writers are in `SelectSchedule` and the
sound selectors.

**The arms.** In evaluation order, each claiming the winner, so the last one to pass wins:

| # | Condition | Record | `bCombatMode` |
|---|---|---|---:|
| 1 | `HEAR_WORLD` 0x6e | `+0x61e4` | 0 |
| 2 | `HEAR_PHYSICS_DANGER` 0x71 | `+0x6134` | 0 |
| 3 | `HEAR_DANGER` 0x6a | `+0x6108` | predicate skipped |
| 4 | `HEAR_PLAYER` 0x6f | `+0x61b8` | 0 |
| 5 | `HEAR_BULLET_IMPACT` 0x70 | `+0x618c` | 1 |
| 6 | `HEAR_COMBAT` 0x6d | `+0x6160` | 1 |

**Correction 1 — there is no Flinch arm.** The `Flinch` record `+0x6210` is a real seventh record
and `CommitBestSound` ranks it third, but `0x102b1cd0` never touches it: `HEAR_FLINCH` cannot
produce `INVESTIGATE_SOUND`. It reaches a decision only through `FUN_102b9060`'s own
`HasInterruptCondition(HEAR_FLINCH)`. The "six records" and "seven records" statements elsewhere in
this document are each half right; this is the whole of it.

**Correction 2 — the mask tester is `0x10269c70`, not `HasInterruptCondition`.** Three distinct
testers exist and the sweep uses the third:

| Address | Needs a schedule | Reads `+0x5c5c` | Reads the mask |
|---|:-:|:-:|---|
| `0x10269aa0` `HasCondition` | no | yes | — |
| `0x10269d30` `HasInterruptCondition` | yes | yes | `+0x5c74` |
| `0x10269c70` (the sweep's) | yes | **no** | `+0x5c74` **OR** `+0x5c8c` |

Inside the six arms the difference is invisible — the arm already `&&`s with `HasCondition`. For the
`0x33` and `0x2d` gates it is the whole rule: they are pure "does the running program list this"
tests, satisfied with the condition unset, and they accept the second mask word too.

**`HEAR_FLANK_SOUND` 0x33.** All of: the mask lists `0x33`; there is a winner; `GetEnemy()` is
non-null and equals the winner's owner (a resolved-pointer comparison, not a handle one); and
`dot(soundAt - GetAbsOrigin(), m_vecForward (+0x6290)) < 0.0f` (`_DAT_104454c4` = `0.0f`, confirmed
by its use as the `0.0f <` threshold on the interrupt distances in `0x102b27f0`). Measured from
`GetAbsOrigin` (slot 217), not the eye. No consumer is recovered.

`soundAt` is **not** simply the record's stored origin. It is `FUN_101b99d0(record)`:

```c
if ((record[1] == 0x10 || record[1] == 0x400) && record[0] /*owner*/ resolves)
    return owner->GetAbsOrigin();   // vfunc 0x364
return record + 0x20;               // the stored origin
```

`record+0x04` is the raw CSound type word, so for `BULLET_IMPACT` (`0x10`) and `PHYSICS_DANGER`
(`0x400`) with a live owner the test measures to the owner's CURRENT position, and only for the
other four arms to where the sound was made. Since the gate has already established the owner is
the committed enemy, those two arms ask "is my enemy behind me now" rather than "is the bullet hole
behind me".

**The `SEE_SOUND_SOURCE` 0x2d tail.** Outside the gate. Gated on the mask listing `0x2d`; if it does
not, fall straight through to `ClearCondition(0x2d)`. The subject is `m_hBestSoundSource (+0x5b78)`
— the source the last `CommitBestSound` chose, not this sweep's winner. Comparisons are between
*resolved pointers* throughout.

1. `FUN_102b8cd0(this, source)`: true only when `source != NULL` **and**
   `IRelationType(source)` is neither `D_HT` (1) nor `D_FR` (2). False → `return`, leaving `0x2d`
   untouched. The polarity reads backwards and is worth stating: the tail runs only for a source
   the NPC neither hates nor fears.
2. A first-match chain; the match demands its own sight condition, and a match without it (or no
   match) falls to `ClearCondition(0x2d)`:

   | `source ==` | requires | status |
   |---|---|---|
   | `m_hClosestPlayer` `+0x628c` | `SEE_PLAYER` 0x5a | live |
   | `GetEnemy()` | `SEE_ENEMY` 0x46 | live, but only for a `D_LI`/`D_NU` enemy after step 1 |
   | `m_hLastSeenHateEnt` `+0x5b68` | `SEE_HATE` 0x43 | live; needs the relation to have left `D_HT` |
   | `m_hLastSeenFearEnt` `+0x5b6c` | `SEE_FEAR` 0x44 | live; needs the relation to have left `D_FR` |
   | `m_hLastSeenDislikeEnt` `+0x5b70` | `SEE_DISLIKE` 0x45 | live; same `D_HT` caveat |
   | `m_hLastSeenNemesisEnt` `+0x5b74` | `SEE_NEMESIS` 0x5b | live; same `D_HT` caveat |

3. Otherwise the stranger arm. `curtime < +0x6418` → `return` (sticky). Else
   `FInViewCone(source)` `&& FVisible(source, 0x2804091)` (slot 201, Troika `0x102b4630`)
   — both dispatched virtually, so on a Troika NPC the cone test is the slot-363 override
   `0x102b4540` (the two `npc_ignore_*` ConVars, then the follower/`m_bInPlayerLOS` seam, then
   the base) and NOT the base
   `CBaseCombatCharacter::FInViewCone` `0x10326750` → `SetCondition(0x2d)`, else `ClearCondition(0x2d)`; then
   `+0x6418 = curtime + 0.5f` (`_DAT_104454d0`).

**Correction 3 — `+0x6418`.** It is the stranger arm's own rate limit, `0.5 s`; this sweep is its
only reader and only writer. It appears nowhere else in this document.

**The four `m_hLastSeen*Ent` handles are live.** `CAI_BaseNPC::OnLooked` (`0x1026a2c0`, the base of
Troika slot 469) writes one of them per assessed sighting: the `D_HT` case splits on
`IRelationPriority` into `m_hLastSeenDislikeEnt` (`< 0`, condition 0x45),
`m_hLastSeenHateEnt` (`< 0xb`, 0x43) and `m_hLastSeenNemesisEnt` (else, 0x5b); the `D_FR` case
writes `m_hLastSeenFearEnt` (0x44). Both cases sit behind `m_bfAINPCFlags2 & 0x10000` (`D_CALM`):
the `D_HT` arm jumps to the `case 2` label when it is set, and `case 2` re-tests the same flag and
does nothing — so a sighting under `D_CALM` writes no handle AND raises no condition. `FUN_1027c300`
resets all four to `0xffffffff`.

A method note, because this was got wrong once during the 10a port and corrected here: a
`vtmb_grep` for the decompiler's untyped dword rendering (`param_1[0x16da]`) finds only the reset
and this sweep, and `vtmb_readers` scoped to `CAI_BaseNPCTroika` reports zero accesses — the writer
renders as a NAMED FIELD on the base class, `CAI_BaseNPC::m_hLastSeenHateEnt`. Query the ledger at
the class that declares the field, not the one that reads it, before calling storage dead.

The practical reachability of those three `D_HT`-derived rungs is still narrow: step 1 rejects a
source the NPC currently hates or fears, so the rung fires only when the relation has changed since
the sighting that wrote the handle. Narrow is not dead.

Constants, read out of retail `vampire.dll`'s `.rdata` at file offsets `0x4454c4` / `0x4454d0` (image base `0x10000000`): **`_DAT_104454c4 = 0.0f`** (`00 00 00 00`) and **`_DAT_104454d0 = 0.5f`** (`00 00 00 3f`). Both are shared pool constants with hundreds of readers, so neither can be pinned from the corpus alone — the corpus exposes referrers, not `.rdata` values. `m_vecForward = +0x6290` (cached by
`NPCThink` and `GatherConditions`). The `(**(*DAT_10924a6c + 4))()` call preceding every
`SetCondition` is an AI trace hook with its result discarded; it has no gameplay effect. `[VtMB]`

**The interest predicate `0x102b3270`** (`ShouldInvestigate(candidate, bCombatMode)`), in order:
`m_bfAINPCFlags & (DONT_INVESTIGATE | IN_FLEE_SCHED)` → false; `stay_entrenched` → false; null →
false; candidate is `m_hFollowerBoss` → false; candidate is the committed enemy → **true**; then a
switch on `investigate_mode` (`+0x6338`) or, when `bCombatMode`, `investigate_mode_combat`
(`+0x633c`) — the sight sweep, the world/physics-danger/player sound arms and the vision producer
pass 0, the bullet-impact and combat sound arms pass 1. The modes are a product: 0 never; 1 players
I hate; 2 players not neutral; 3 any player; 4 anything I hate; 5 anything not neutral; 6 anything;
else `DevWarning("Hey FOO!!!  I don't recognize your investigate mode!")` and false. **The shipped
default, 4 on 378 of 426 rows, is "anything I hate" — not "the player".** `bCombatMode` also
unlocks a third-party-brawl proximity override (256 units 2-D / 80 vertical, when the candidate's
enemy is someone I hate). `[VtMB] [script/data]`

## The base condition table

`FUN_102c8ce0` registers one dense namespace, `0x00`–`0x76`, 119 entries; `thunk_FUN_102beae0` is a
wrapper onto the same table (`0x1090ff08 + 0x30`). Roughly 20 derived-class tables carry ids above
`0x76` and were not dumped.

```
00 NONE                      1a INTERRUPT_TIME            34 HIT_BY_DOOR            4e REPEATED_DAMAGE          68 GIVE_WAY
01 SEE_UNKNOWN               1b HAVE_ENEMY_THROW_LOS      35 SHOULD_CHARGE          4f CAN_RANGE_ATTACK1        69 WAY_CLEAR
02 LOST_UNKNOWN              1c CLAW_HINT_INVALID         36 FLYING_WALL_HIT        50 CAN_RANGE_ATTACK2        6a HEAR_DANGER
03 IGNORE_UNKNOWN            1d CLAW_HINT_SPECIAL_INVALID 37 FLYING_NPC_HIT         51 CAN_MELEE_ATTACK1        6b HEAR_THUMPER
04 UNKNOWN_RUN_TIMER         1e INVESTIGATE_LEVEL         38 WAS_BUMPED             52 CAN_MELEE_ATTACK2        6c HEAR_BUGBAIT
05 UNKNOWN_ADVANCING         1f CRIMINAL_FLEE_LEVEL       39 COVER_FAILURE          53 PROVOKED                 6d HEAR_COMBAT
06 UNKNOWN_HOLDING           20 CRIMINAL_ATTACK_LEVEL     3a ENEMY_BLOCKED          54 NEW_ENEMY                6e HEAR_WORLD
07 UNKNOWN_RETREATING        21 SUPERNATURAL_FLEE_LEVEL   3b PLAYER_ON_HEAD         55 ENEMY_TOO_FAR            6f HEAR_PLAYER
08 TOO_CLOSE_FOR_RANGED      22 SUPERNATURAL_ATTACK_LEVEL 3c WEAPON_THROUGH_WALL    56 ENEMY_FACING_ME          70 HEAR_BULLET_IMPACT
09 TOO_FAR_FOR_MELEE         23 CAN_POUNCE                3d SEE_CORPSE             57 BEHIND_ENEMY             71 HEAR_PHYSICS_DANGER
0a BEING_ATTACKED            24 PASS_OUT                  3e SEE_CORPSE_FRIEND      58 ENEMY_DEAD               72 HEAR_FLINCH
0b DETECTED_ATTACK           25 INVESTIGATE_SOUND         3f LOW_PRIMARY_AMMO       59 ENEMY_UNREACHABLE        73 FLOATING_OFF_GROUND
0c SHOULD_DODGE              26 INVESTIGATE_SIGHT         40 NO_PRIMARY_AMMO        5a SEE_PLAYER               74 PLAYER_PUSHING
0d SHOULD_BLOCK              27 COMFORT                   41 NO_SECONDARY_AMMO      5b SEE_NEMESIS              75 NPC_FREEZE
0e SHOULD_STEPBACK           28 KNOCKBACK                 42 NO_WEAPON              5c TASK_FAILED              76 NPC_UNFREEZE
0f SHOULD_KICK               29 HINT_INVALID              43 SEE_HATE               5d SCHEDULE_DONE
10 SHOULD_INTERACT           2a KICK_PROP_INVALID         44 SEE_FEAR               5e SMELL
11 SHOULD_LOITER             2b PLAYER_SNARL_RANGE        45 SEE_DISLIKE            5f TOO_CLOSE_TO_ATTACK
12 CROSSWALK_WALK            2c STOP_BACKUP               46 SEE_ENEMY              60 TOO_FAR_TO_ATTACK
13 CROSSWALK_DONTWALK        2d SEE_SOUND_SOURCE          47 LOST_ENEMY             61 NOT_FACING_ATTACK
14 OUTSIDE_INTERRUPT_DIST    2e EXTENDED_BLOCKED_BY_FRIEND 48 ENEMY_OCCLUDED        62 WEAPON_HAS_LOS
15 INSIDE_INTERRUPT_DIST     2f WAITING_ATTACK_TIME       49 TARGET_OCCLUDED        63 WEAPON_BLOCKED_BY_FRIEND
16 OUTSIDE_INTERRUPT_DIST_E  30 ON_FIRE                   4a HAVE_ENEMY_LOS         64 WEAPON_PLAYER_IN_SPREAD
17 INSIDE_INTERRUPT_DIST_E   31 SQUAD_SEE_ENEMY           4b HAVE_TARGET_LOS        65 WEAPON_PLAYER_NEAR_TARGET
18 OUTSIDE_INTERRUPT_DIST_F  32 SQUAD_LOS_ENEMY           4c LIGHT_DAMAGE           66 WEAPON_SIGHT_OCCLUDED
19 INSIDE_INTERRUPT_DIST_F   33 HEAR_FLANK_SOUND          4d HEAVY_DAMAGE           67 BETTER_WEAPON_AVAILABLE
```

Every identity the port already carried agrees with this dump; its seven placeholders (`SEE_ENEMY`,
`SEE_FEAR`, `HEAR_COMBAT/PLAYER/WORLD/DANGER`, `INVESTIGATE_LEVEL`) are now the registered numbers.
`[VtMB]`

## Interrupt conditions

Schedules declare which new conditions are allowed to abort their current task program. The most
common compiled interrupt conditions are:

| Condition | Schedules |
|---|---:|
| `NEW_ENEMY` | 332 |
| `HEAVY_DAMAGE` | 279 |
| `LIGHT_DAMAGE` | 224 |
| `ENEMY_DEAD` | 182 |
| `SEE_FEAR` | 169 |
| `SEE_ENEMY` | 166 |
| `CAN_MELEE_ATTACK1` | 127 |
| `LOST_ENEMY` | 119 |
| `SQUAD_SEE_ENEMY` | 93 |
| `CAN_MELEE_ATTACK2` | 73 |
| `DETECTED_ATTACK` | 72 |
| `GIVE_WAY` | 67 |
| `CAN_RANGE_ATTACK1` / `CAN_RANGE_ATTACK2` | 65 each |
| `INVESTIGATE_SIGHT` | 65 |
| `INVESTIGATE_SOUND` | 60 |
| `IGNORE_UNKNOWN` | 60 |
| `HEAR_DANGER` | 54 |
| `ENEMY_OCCLUDED` | 36 |
| `HEAR_COMBAT` | 29 |

Forty-two schedules carry a `DELAY_INTERRUPTS` flag. The schedule—not merely the existence of a
condition—decides whether a new stimulus may pre-empt the current behavior immediately. This is
why a faithful AI cannot be implemented as a single global priority list with no current-task
context.

### `DELAY_INTERRUPTS`, decoded

`DELAY_INTERRUPTS` is the **only** schedule flag the engine has. The token table `0x1030d7e0`
answers exactly two spellings — `NONE` → 0 and `DELAY_INTERRUPTS` → **bit 0** — and makes anything
else a load-time `Error`. The schedule-table parser `0x1030d850` OR-accumulates the `Flags` section
into `CAI_Schedule+0x18`, defaulting to 0 when a schedule declares none. `[VtMB]`

Recovered `CAI_Schedule` layout, from that parser and `CAI_BaseNPC::CacheInterruptConditions`
(`0x1026a0f0`):

| Offset | Meaning |
|---|---|
| `+0x00`–`+0x17` | **inverted** interrupt mask, 192 bits — the `!COND_*` form |
| `+0x18` | flags word; bit 0 = `DELAY_INTERRUPTS` |
| `+0x1c` | schedule id |
| `+0x20` / `+0x24` | task array (8 bytes/entry) and count (parser caps at 64) |
| `+0x28`–`+0x3f` | **normal** interrupt mask, 192 bits |
| `+0x40` | name |

The flag has exactly one tester: `CAI_BaseNPC::IsScheduleValid` (`0x10280ff0`), called only from
`MaintainSchedule` (`0x102817c0`). It is **not** consulted on its own — it is ANDed with the NPC's
`m_bDidMaintainSchedule` (`+0x5bb8`):

```c
if (!(!m_bDidMaintainSchedule && (m_pSchedule->flags & 1)))
{
    testBits = (m_Conditions & m_CustomInterruptConditions)
             | (m_InvertedInterruptConditions & ~m_Conditions);
    if (testBits) return false;          // "Break condition: > %s"
}
if (HasCondition(COND_SCHEDULE_DONE) || HasCondition(COND_TASK_FAILED)) return false;
return true;
```

`m_bDidMaintainSchedule` is written in exactly three places: false at spawn (`0x10273ad0`), **false
by every `SetSchedule`** (`0x10280e50`), and true on `MaintainSchedule`'s common exit
(`0x102821ae`). So the flag buys a schedule **one think of immunity, re-armed by every install** —
no timer, no task boundary, no deferral store. Nothing is latched: a condition suppressed on that
think is simply not consulted, and a stimulus that persists is re-gathered and fires on the next
one. `COND_TASK_FAILED` (`0x5c`) and `COND_SCHEDULE_DONE` (`0x5d`) are tested after the gate and
are never delayed. `[VtMB]`

All 42 flagged schedules are installed from **outside** the AI think, which is the case the flag
exists for, and they are one family: the Discipline effects and externally forced states —
`SCHED_TROIKA_D_MESMERIZE`, `D_DAZE`, `D_BERSERK`, `D_TRANCE`, `D_HYSTERIA`, `D_SUICIDE`,
`D_BRAINWIPE`, the eighteen `DO_*_ACTIVITY` performers, `TROIKA_MESMERIZED`, `LAUGHING`, `CALMED`,
`LOST`, `DISORIENTED`, `FLEE_AND_DIE`. Without the flag a forced state would be re-selected away on
the same think that forced it. `[VtMB]`

### `SetSchedule` clears the condition set

`CAI_BaseNPC::SetSchedule` (`0x10280e50`) zeroes six dwords at `+0x5c5c` — the 192-bit condition
set `SetCondition` (`0x10269a20`) and `HasCondition` (`0x10269aa0`) write and read. A condition
standing at the instant a schedule is installed is **destroyed by the install**, so only a stimulus
the next pass re-observes can interrupt the new program. This is the other half of
`DELAY_INTERRUPTS`: without it the flag would be nearly redundant. The same body also calls the
schedule-change virtual (slot 435) first and clears `m_bDidMaintainSchedule`. `[VtMB]`

Two further facts about the effective mask, neither previously recorded: the interrupt column
supports an **inverted `!COND_*` form** (mask at `CAI_Schedule+0x00`, evaluated `mask & ~conditions`;
no shipped schedule uses it), and the mask an NPC actually runs against is **not** the authored one
— `CacheInterruptConditions` copies the schedule's mask onto the NPC each think and then lets
`BuildScheduleTestBits` (`CAI_BaseNPCTroika::0x102ad140`) add and remove conditions per NPC. `[VtMB]`
