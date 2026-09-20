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

## The sound gate — `0x1027a5c0`, `0x102b4c10`, `0x1027a640`, `0x102b4c40`, `0x103b9f10`

"May I make a sound now" is slot 486 `FOkToMakeSound`, and "I just did" is slot 487
`JustMadeSound`, which arms the clock the first one reads. Each has a base body and a Troika one,
and the **Troika bodies are replacements, not extensions** — neither calls up.

`CAI_BaseNPC::FOkToMakeSound` (`0x1027a5c0`, 88 bytes) is three refusals in order. First
`curtime <= m_flSoundWaitTime` (`+0x5ce8`): the comparison carries the equal bit (`FNSTSW` masked
`0x4100` at `1027a5d8`), so a sound at exactly the deadline is refused. Then, when the NPC is
squad-connected (`m_iSquadDisconnected +0x5bb0 < 1` and `m_pSquad +0x5da4` non-null), the same test
against the **squad's own copy** of the clock at `squad+0x60` — either clock can gag this NPC. Last
`SF_NPC_GAG` (`m_spawnflags & 2`) with `m_NPCState != NPC_STATE_COMBAT`: a gagged NPC is silent
outside combat and vocal inside it. Otherwise true.

`CAI_BaseNPCTroika::FOkToMakeSound` (`0x102b4c10`, 24 bytes) throws all three away:
`return !IsInDialog()`. Every `npc_V*` class in the family descends from `CAI_BaseNPCTroika`, so on
a shipped NPC the sound-wait clock and `SF_NPC_GAG` **do not gate vocalisation at all** — the only
reason a Troika body stays quiet is that it is talking (`IsInDialog` `0x102c1170`: `m_bIsTalking`
`+0x64c0`, a queued dialogue string `+0x64ec`, the dialogue partner `+0xfe8`, the bound speech scene
`+0x6554`).

`JustMadeSound` writes `m_flSoundWaitTime = curtime + RandomFloat(min, max)` and, when the squad is
connected, a **second independent draw** into `squad+0x60`. The two clocks therefore drift apart;
retail does not reuse the first number. The three bodies differ only in the window and in whether
the squad half exists: base `0x1027a640` draws `(1.5, 2.0)`, Troika `0x102b4c40` draws
`(0.25, 0.75)`, and `CNPC_VTzimisce::vfunc487` (`0x103b9f10`, 41 bytes — the only species override
of the slot) draws `(0.5, 0.75)` and writes **no squad copy at all**.

**Unrecovered:** nothing. **Not built:** `m_pSquad`. This substrate has no squad object, so the
squad arm of all five bodies is asked and never answers; the port's `FElysiumNpc::ConnectedSquad()`
is where it lands the day a squad layer exists.

## The idle-sound gate — `0x1027a420`, `0x10294040`

Slot 509 `ShouldPlayIdleSound` is what `GatherConditions` asks before it dispatches an idle
vocalisation. `CAI_BaseNPCTroika` (`0x10294040`, 24 bytes) is the dialogue gate and a delegation:
`IsInDialog() ? false : CAI_BaseNPC::ShouldPlayIdleSound()`.

`CAI_BaseNPC::ShouldPlayIdleSound` (`0x1027a420`, 201 bytes) has to be read off the listing — the
decompiled C hides the order of the last two arms. Five refusals: `m_NPCState` not in
{ `NPC_STATE_IDLE` 1, `NPC_STATE_ALERT` 3 }; `SF_NPC_GAG`; a clear `m_bIsBCCTargetable`; a LIVE
`m_hDialogPartner` (`+0xfe8`); `IsBusyWithDiscipline()`. Then a weight, and this is the arm the
slot's one-line summary misses:

* the weight is `999`, **unless** a schedule is installed (`m_pSchedule +0x5c38`) and slot 447
  `GetLocalScheduleId(m_pSchedule->id)` answers `0x12f` `SCHED_TROIKA_COMFORT`, in which case it is
  `0x14` (20) and the float-sound arm below is **skipped entirely**;
* otherwise the body asks slot 510 `ShouldPlayFloatSound()` (vtable `+0x7f8`) FIRST, and when that
  says yes it plays slot 507 `FloatSound()` (`+0x7ec`) and **returns false**. The float sound is
  played *instead of* an idle sound, never beside it;
* only then `RandomInt(0, weight) == 0`.

So a comforting NPC vocalises roughly fifty times as often as an ordinary one and never floats while
it comforts — the two facts the `0x12f` special case buys.

**Unrecovered:** nothing in the body. **Not built:** `m_bIsBCCTargetable` (`+0x7ec` on
`CBaseCombatCharacter`) has no port member and its arm is not tested; slot 447 is a declared stub;
and no registered program in the port is `0x12f` yet (story 10i), so the comfort weight is
unreachable.

## The float-sound gate — `0x1027a530`, `0x10294070`

Slot 510 `ShouldPlayFloatSound` decides whether an idle body plays the "float" ambience instead of
speaking. The Troika body (`0x10294070`, 410 bytes) is eight gates and a distance, in the listing's
order:

1. `IsInDialog()` (`10294075`);
2. a **live grapple** — `+0x1538` resolves through the handle table AND `+0x153c != -1`
   (`10294082`), the same pair the move solver reads as "a live grapple partner";
3. `IsUnconscious()` — `m_iMiscFlags & 1` (`102940be`);
4. `m_bfAINPCFlags & 0x20000`, the `SLEEPING` bit (`102940cb`);
5. `m_IdealNPCState != NPC_STATE_IDLE(1)` (`102940db`);
6. `m_NPCState != NPC_STATE_IDLE(1)` (`102940ee`);
7. `m_hClosestPlayer` (`+0x628c`) not live (`102940fa`);
8. that **player's own** `+0xfe8` dialogue partner being live (`10294161`) — an NPC does not float
   while the player nearest it is in a conversation.

Then the threshold: a magic-static lazy load (guard `DAT_10924d1c`) of the `Float_Sound_Info` rule
table into `DAT_10924290`, whose **row 0** — `FloatSoundDistance`, authored `50.0` Source units —
is cached in `_DAT_1092483c`, and `m_flPlayerDist (+0x6264) <= threshold` with the equal bit
carried (`AND 0x4100` at `102941f3`). Only then the tail call into the base body.

Note gates 5 and 6: **both** the ideal and the current state must be IDLE. `0x1027e660`'s name
table orders retail's enum None(0) Idle(1) Combat(2) Alert(3), so the `CMP …,1` pair is idle and not
alert.

`CAI_BaseNPC::ShouldPlayFloatSound` (`0x1027a530`, 109 bytes) is the roll itself:
`m_iFloatSoundFrequency` (`+0x10e8`, keyfield `floatfreq`) equal to `0` refuses, equal to `8`
refuses — two **separate** equalities, so 8 is a hole in the middle of the range rather than a
ceiling and 7 and 9 both roll — then `engine->Time() < m_flNextFloatSoundTime` (`+0x10ec`) refuses,
and finally `RandomInt(0, m_iFloatSoundFrequency) == 0`, the authored "plays 1 time in X".

**Unrecovered:** nothing. **Not built:** `m_flNextFloatSoundTime` has one writer, slot 507
`FloatSound` (`0x10294f40`, layer 14), so until that body lands nothing re-arms the window and the
third refusal never fires.

## The condition clears — `0x1026dc80`, `0x1026e5c0`, `0x1026d7f0`, `0x101a89a0` (2026-09-13)

Four slot bodies, all of them fixed lists rather than rules.

**`ClearAttackConditions` (`0x1026dc80`, slot 560)** is eleven `ClearCondition` calls and nothing
else: `CAN_RANGE_ATTACK1` `0x4f`, `CAN_RANGE_ATTACK2` `0x50`, `CAN_MELEE_ATTACK1` `0x51`,
`CAN_MELEE_ATTACK2` `0x52`, `EXTENDED_BLOCKED_BY_FRIEND` `0x2e`, `WAITING_ATTACK_TIME` `0x2f`,
`WEAPON_HAS_LOS` `0x62`, `WEAPON_BLOCKED_BY_FRIEND` `0x63`, `WEAPON_PLAYER_IN_SPREAD` `0x64`,
`WEAPON_PLAYER_NEAR_TARGET` `0x65`, `WEAPON_SIGHT_OCCLUDED` `0x66`. Nothing about it depends on the
weapon, the state or the capability word; `TOO_CLOSE_TO_ATTACK`, `TOO_FAR_TO_ATTACK` and
`NOT_FACING_ATTACK` are deliberately NOT in the list and survive the clear.

**`ClearSenseConditions` (`0x1026e5c0`, slot 477)** is `ClearConditions(0x105c97dc, 0xe)`. The table
was read out of retail's `.rdata` and is fourteen dwords:
`43 45 46 44 5b 5a 6a 6d 6e 6f 6b 6c 71 5e` — `SEE_HATE`, `SEE_DISLIKE`, `SEE_ENEMY`, `SEE_FEAR`,
`SEE_NEMESIS`, `SEE_PLAYER`, `HEAR_DANGER`, `HEAR_COMBAT`, `HEAR_WORLD`, `HEAR_PLAYER`,
`HEAR_THUMPER`, `HEAR_BUGBAIT`, `HEAR_PHYSICS_DANGER`, `SMELL`. It is the SEE family table
(`0x105c979c`, 6) plus eight of the ten HEAR entries; `HEAR_BULLET_IMPACT` `0x70` and `HEAR_FLINCH`
`0x72` are the two the HEAR sweep owns that a sense clear does **not** take away.

**`RemoveIgnoredConditions` (`0x1026d7f0`, slot 459)**, read off the listing because the decompiler
drops its jump table, is a guard and a dispatch: while `m_NPCState` is 4 (SCRIPT) **and** `m_hCine`
(`+0x5d74`) still resolves through the entity table, it calls THAT entity's own slot 459. Outside
state 4, and with a dead cine handle, it writes nothing at all.

**`CCineAISchedule::RemoveIgnoredConditions` (`0x101a89a0`)** is the body the dispatch reaches. It
first refuses when `0x101a8930` says the scene is already in this state, then walks `m_hTargetEnt`
through `+0x94` to the scene's NPC and clears fourteen conditions on **that** NPC, in this order:
`LIGHT_DAMAGE` `0x4c`, `HEAVY_DAMAGE` `0x4d`, `REPEATED_DAMAGE` `0x4e`, then the
`m_bCondTookDamage` byte at `+0x5b80`, then `INVESTIGATE_LEVEL` `0x1e`, `CRIMINAL_FLEE_LEVEL` `0x1f`,
`SUPERNATURAL_FLEE_LEVEL` `0x21`, `HEAR_FLINCH` `0x72`, `CRIMINAL_ATTACK_LEVEL` `0x20`,
`SUPERNATURAL_ATTACK_LEVEL` `0x22`, `INVESTIGATE_SOUND` `0x25`, `INVESTIGATE_SIGHT` `0x26`,
`COMFORT` `0x27`, `BEING_ATTACKED` `0x0a`. The order is not sorted and the damage byte sits in the
middle of it.

**Unrecovered:** nothing in the four bodies. **Not built:** the cine chain. This substrate carries
`FElysiumEntity::ScriptOwner` but no scripted-scene object with a target entity, and `0x101a8930`
has no counterpart, so slot 459 reaches its guard and stops.

## The two ranged attack bands — `0x1026d890`, `0x1026d920` (2026-09-13)

Slots 553 and 554 take `(flDot, flDist)` and **return** a condition number; they set nothing.
`GatherAttackConditions` (`0x1026dd10`, slot 561) is what calls `SetCondition` on the answer, after
routing to the weapon's own `+0x5b4` / `+0x5bc` when one is held. Every distance comparison is
strict; the dot comparison carries the equal bit (`TEST AH,0x5 / JNP`).

`RangeAttack1Conditions` (`0x1026d890`): under 100 gives `TOO_CLOSE_FOR_RANGED` `0x08`; under 200
gives `TOO_CLOSE_TO_ATTACK` `0x5f`; over 1024 gives `TOO_FAR_TO_ATTACK` `0x60`; else `flDot >= 0.5`
gives `CAN_RANGE_ATTACK1` `0x4f`, otherwise `NOT_FACING_ATTACK` `0x61`.

`RangeAttack2Conditions` (`0x1026d920`) is the same shape with its own numbers and **one fewer
band** — there is no close-to-attack rung: under 64 gives `0x08`; over 512 gives `0x60`; else
`flDot >= 0.5` gives `CAN_RANGE_ATTACK2` `0x50`, otherwise `0x61`.

The constants were read out of retail `vampire.dll`'s `.rdata` (image base `0x10000000`):
`_DAT_10450564` = `100.0f`, `_DAT_104492b8` = `200.0f`, `_DAT_1045d650` = `1024.0f`,
`_DAT_10451acc` = `64.0f`, `_DAT_10483aac` = `512.0f`, and `_DAT_10449270` = **`0.5` as a double**
(`FCOMP double ptr`), not the float the overlapping symbol at that address reads as.

`CNPC_VBatSwarm` (`0x103675e0` / `0x10367610`) and `CNPC_VSheriffSwarm` (`0x103b2590` /
`0x103b25c0`) are the only overrides of either slot and all four are unmodified forwards, so the
base answer is every class's answer. **Unrecovered:** nothing.

## `FCanCheckAttacks` — `0x10270840`, `0x102953a0` (2026-09-13)

Slot 564's base body (`0x10270840`) is Source's own, verbatim: `GetNavType()` neither `NAV_CLIMB`
(3) nor `NAV_JUMP` (1), `COND_SEE_ENEMY` `0x46` standing and `COND_ENEMY_TOO_FAR` `0x55` clear. The
two nav refusals come first, so a climbing or jumping body never evaluates its conditions.

The Troika body (`0x102953a0`) that every `npc_V*` classname actually reaches puts one suppression
in front of it: when `CapabilitiesGet()` carries `bits_CAP_WEAPON_MELEE_ATTACK1` **`0x8000`** — the
listing tests `AH`'s sign bit, which is EAX bit 15, not bit 31 — and `GetActiveWeapon()` is non-null
and `m_bInMelee` (`+0x6078`) is clear, it returns false outright. Otherwise it tail-calls the base.

**Unrecovered:** nothing. **Not built:** `GetNavType`. The nav type lives on `CAI_Navigator`
(`+0x5d34` then `+0x18`) and this substrate's motor seam carries no such word, so the port answers
`NAV_GROUND` — the value that does not suppress — and the two refusals are unreachable.

## `OnStateChange`, slot 463 — `0x102ae140`, `0x1026e3e0`, `0x10260630`, `0x10368ea0`, `0x103ba2c0` (2026-09-13)

Slot 463 is called on the state EDGE with `(old, new)`. The chain is species body, Troika body, base
body, and three of the four species shapes chain while one does not.

**`CAI_BaseNPCTroika::OnStateChange` (`0x102ae140`)** is two halves, and the split is the point: an
entry `goto` skips the first half whenever `old == new`.

The CHANGED half: slot 610 `0x102adfe0` (the default-expression selector) with the new state; then
`0x102ae310`, the Troika-line weapon show/hide; then a switch on the new state —

* `2` COMBAT and `3` ALERT fall through to the tail write;
* `7` DEAD releases `m_sppPatrolPath` (`+0x6590`) through `0x1029f5d0` and **skips** the tail write;
* `8` FLEE fires `m_OnStateFleeing` with `GetEnemy()` as activator, substituting `this` when there
  is no enemy — the output is never skipped;
* `0xb` HUNT sets `m_flHuntExpireTimer = curtime + RandomFloat(10, 20)`;
* `0xe` (the criminal window) sets `m_flCriminalIgnoreTimer` (`+0x6398`) to `curtime + 2.0` and
  **falls through** to the tail write;
* every other state skips the tail write.

The tail write is `m_bReturnToInitialPos = 1` (`+0x6494`). Then, still inside the changed half,
`m_afMemory &= 0x07ffffff` (the top FIVE bits go) and, unless the nav type is `NAV_CLIMB` or
`NAV_JUMP`, `m_bfAINPCFlags &= ~PRESERVE_PATH` (`0x8`).

The UNCONDITIONAL half runs on every call, including a no-op transition: clear `MADE_HUNT_PATH`
(`0x1000`), release `m_sppPatrolPathHunt` (`+0x6594`), clear `AT_CROSSWALK` (`0x4`),
`m_bGoToIdleState = 0` (`+0x63fc`), `ClearHintNode(this, 5.0)` (`0x10295ab0`),
`m_bCondTookDamage = 0` (`+0x5b80`), then `CAI_BaseNPC::OnStateChange`.

**`CAI_BaseNPC::OnStateChange` (`0x1026e3e0`)** assigns `m_bfNPCStateFlags` whole from the new
state; that table is already recorded on `FElysiumNpcFlags::NpcStateFlagsForRetailState`.

The four species shapes: **`CNPC_VGuard1`** (`0x1037d020`), **`CNPC_VHunter`** (`0x10388880`) and
**`CNPC_VGhoulCroucher`** (`0x103871c0`, shared by `CNPC_VHumanCombatant`,
`CNPC_VHumanCombatPatrol`, `CNPC_VSabbatGunman`, `CNPC_VStalker`, `CNPC_VYukie`, `CNPC_ProneDialog`)
hide or unhide the active weapon and chain. **`CNPC_VTzimisce`** (`0x103ba2c0`) maps the new state,
only on a real transition, to an index into `PTR_s_normal_10653120` — whose four entries read out of
`.rdata` are `normal`, `angry`, `scream`, `dead` — stores the looked-up index at
`m_idxDefExpression` (`+0x10b4`) and blends to it over `1.0` (`0x103b9f50` then `0x103b9f90`); idle
maps to 0, alert/combat/hunt to 1, dead to 3, and **`scream` is reached by no state**. It then
chains. **`CNPC_VCamera`** and **`CNPC_VCameraSecurity`** (`0x10368ea0`) have an **empty** body: a
camera's state change writes nothing at all, not even the base state-flag byte.
**`CAI_BaseHumanoid`** (`0x10260630`) runs a two-call pre-step (vtable `+0x934` with the new state,
then `0x10260670` with the result) and chains **straight to the base**, skipping the Troika body
entirely.

**Unrecovered:** `CAI_BaseHumanoid`'s pre-step — both calls are HL2-line bodies outside this
closure; and `0x102ae310`, the Troika weapon show/hide, which is not one of story 29c-1's rows.
**Not built:** retail states 8, 0xb and 0xe have no member of this runtime's state vocabulary, so
those three arms are recovered and unreachable; and this runtime has no `SetExpression`, so the
Tzimisce arm records the expression NAME and blends nothing.

## The species `SelectIdealState` overrides — `0x10369060`, `0x103945a0`, `0x1039e310` (2026-09-13)

Slot 461 has exactly three species overrides and all three are complete **replacements** — none of
them chains into `CAI_BaseNPCTroika::SelectIdealState` (`0x102ad660`). All three also write the
file/line ideal-state trace at `+0x1b38` / `+0x1b3c` / `+0x1b40`, which the shape map records ABSENT.

* **`CNPC_VCamera`** (`0x10369060`, shared with `CNPC_VCameraSecurity`): `m_IdealNPCState = 3`
  (ALERT) with **no test of any kind**. A camera's ideal state is a constant.
* **`CNPC_VMingXiao`** (`0x103945a0`): unless `IsAlive()` (slot 158) and `GetState() != 7`, it writes
  `m_IdealNPCState = 7` at source line `0x5da` — and then **unconditionally overwrites it** from
  `GetEnemy() ? 2 : 1` (lines `0x5e0` / `0x5e4`). The dead-state write cannot survive the same call;
  it is retail's own dead code.
* **`CNPC_VMingXiaoTentacle`** (`0x1039e310`): here the dead arm is real — `m_NPCState == 7` **or**
  `m_IdealNPCState == 7` short-circuits to 7 (line `0x486`) and never reaches the enemy test, which
  is otherwise the same `GetEnemy() ? 2 : 1` (lines `0x48d` / `0x491`).

Retail's `NPC_STATE` ordinals here are the ones `0x1027e660`'s name table and `0x1026e3e0`'s switch
give: 1 IDLE, **2 COMBAT, 3 ALERT**, 7 DEAD. The camera's `3` is therefore alert, not combat.

**Unrecovered:** nothing. **Not built:** no registered `npc_*` classname in this runtime resolves to
any of the three classes, so the species arm is recovered and never taken.

## The two flee requests — `0x102ad260`, `0x102ad2d0` (2026-09-13)

Two bodies `PreSelectIdealState` (slot 460, `0x102ad340`) calls, identical but for their gate and
their source line. Each: `HasInterruptCondition(cond)` — `0x10269d30`, which needs an INSTALLED
schedule whose mask lists the condition **and** the condition standing, not the bare `HasCondition`;
then, when `m_NPCState != 8`, `m_bfAINPCFlags |= 0x100` (`INITIAL_FLEE`); then the file/line trace
(`AI_BaseNPCTroika.cpp` line `0x4468` / `0x447d`); then `m_IdealNPCState = 8` and `return 8`. A
failed gate returns 0 and writes nothing.

`0x102ad260` gates on `SUPERNATURAL_FLEE_LEVEL` `0x21`; `0x102ad2d0` on `CRIMINAL_FLEE_LEVEL` `0x1f`.
The interrupt form is load-bearing: a gathered law level with no program asking for it does not make
an NPC flee. `CAI_BaseNPCTroika::BuildScheduleTestBits` (`0x102ad140`) is what puts both conditions
into the mask of every non-busy, non-investigating NPC.

**Unrecovered:** nothing. **Not built:** retail state 8 (FLEE). This runtime's `EElysiumNpcState`
has no member for it, so the write stores the raw retail id and the typed ideal state is left alone;
the `m_NPCState != 8` test can therefore never be false and `INITIAL_FLEE` is always armed.

## The combat-condition refreshers — `0x102b2570`, `0x1028e700` (2026-09-13)

**`0x102b2570`** is the only producer of `SHOULD_STEPBACK` `0x0e`, `SHOULD_KICK` `0x0f` and
`TOO_FAR_FOR_MELEE` `0x09` in the whole closure. In order:

1. Clear `0x0e` and `0x0f` **unconditionally**, before any gate.
2. `m_bInMelee` (`+0x6078`) clear: return. A body out of melee ends the pass with neither.
3. `COND_ENEMY_TOO_FAR` `0x55` standing (plain `HasCondition`): set `TOO_FAR_FOR_MELEE` `0x09` and
   **return**; no draw is taken.
4. The stepback window: `m_flLastMeleeStepbackTime (+0x606c) < m_flLastAttackTime (+0x5d9c)` **or**
   `m_flLastMeleeStepbackTime + 3.0 / m_flSpeedScale (+0x1488) < curtime` (`_DAT_10449258` = `3.0f`).
   Inside it, two arms: `TOO_CLOSE_TO_ATTACK` `0x5f` with `RandomInt(0,99) < 20`; **or** the frenzy
   bit `m_bfNPCFrenziedFlags & 0x400` clear, `TOO_FAR_FOR_MELEE` `0x09`, `TOO_FAR_TO_ATTACK` `0x60`
   and `CAN_MELEE_ATTACK1` `0x51` all clear, and `RandomInt(0,10) == 0`. Either sets `0x0e`.
5. The kick arm, evaluated whether or not step 4 fired, four terms in order:
   `ConditionInterruptsCurrentSchedule(SHOULD_KICK)` (`0x10269c70` — the MASK alone, never the
   condition); `m_flNextAttack (+0x1564) < curtime`; `TOO_CLOSE_TO_ATTACK` standing; and an active
   weapon whose flag word (weapon vtable `+0x5a0`) carries bit 30. Then set `0x0f`.

**`0x1028e700`** is the occlusion debounce and it is a **suppressor**, not a producer. Given a
condition and a caller-owned float: the condition not standing resets the float to the sentinel
`_DAT_104454c4` = `0.0f`; standing with the sentinel in place arms it to
`m_flOccludedDelay (+0x62c8) + curtime`; and while `curtime` is below that stamp the condition is
**cleared again**. So a raise only survives once it has persisted for the authored delay, and
exactly at the deadline it stands (the compare is strict).

**Unrecovered:** the name of weapon flag bit 30. **Not built:** `m_flNextAttack` has no writer in
this runtime, and `FElysiumWeapon` carries no retail flag word at all — the capability answer is two
named bits and deliberately not a register — so `SHOULD_KICK` can never be raised here.

## The disturbed latch — `0x1037bb20`, `0x1037b6e0` (2026-09-13)

`CNPC_VGhoulCroucher::IsDisturbed` (`0x1037bb20`) is 121 bytes of AI scope-trace push and pop around
one read: `return m_bWasDisturbed` (`+0x6666`). It is the producer the stealth-kill gate names as
absent.

`CNPC_VGhoulCroucher::OnDisturbed` (`0x1037b6e0`) is the whole of the write side, and every line of
it is under one latch — a second disturbance writes nothing and fires nothing. On the first:
`m_bWasDisturbed = 1`, `m_bUnawareExited = 0` (`+0x6667`), then a split on the disturber's cached
`CBasePlayer*` (`+0xa8`). A **non**-player runs `SetClosestPlayer` (`0x10293a80`, the nearest live
player within 20000 units, or `-1`) and fires `m_OnDisturbed` (`+0x6674`); a player instead writes
its own handle straight into `m_hClosestPlayer` (`+0x628c`) and fires `m_OnDisturbedByPlayer`
(`+0x668c`). Then, and only when `m_hClosestPlayer` now resolves to a live entity,
`AddEntityRelationship(player, D_HT, 10)`.

Note the re-read: on the non-player arm the relationship is written against whatever player the
sweep just found, which need not have anything to do with the disturbance. **Unrecovered:** nothing.

## The cop and hunter pursuit counters — `0x1017f650`, `0x1017f7b0`, `0x1017f830` (2026-09-13)

These three are `CSActs` bodies on the **player**, not on the NPC: `CNPC_VCop::vfunc597`
(`0x10372cc0`) calls `0x1017f650(player + 0xa8)`, and the counters are `+0x1d10`
(`m_iCopsInPursuitCount`) and `+0x1d14` (`m_iHuntersInPursuitCount`).

`OnCopPursuitStart` (`0x1017f650`) tests the **pre-increment** count against zero and, on that
edge, runs two hooks before incrementing: `0x103705e0`, which walks the global NPC list and puts
every body whose `GetState()` is **14** into state 2 via `0x102ae840(npc, 2, false)`; and
`0x1017f980`, which fires the game-rules output at `+0x480` with the player as activator and caller
and then zeroes `player + 0x1d1c`. `OnHunterPursuitStart` (`0x1017f7b0`) is the same shape with one
hook, `0x1017fa40` (game-rules output `+0x4e0`).

`OnHunterPursuitStop` (`0x1017f830`) is **not** symmetric: it decrements FIRST and tests the
post-decrement count, calling `0x1017fa70` (game-rules output `+0x4f8`) on reaching zero. Retail
does not clamp, so an unbalanced stop drives the counter negative and the next start then has no
zero-to-one edge to fire. All three end in a `DevMsg` under dev logging.

**Unrecovered:** `player + 0x1d1c`, which the census does not name. **Not built:** retail state 14,
so `0x103705e0`'s sweep has no body it can find.

## The alternate-AI door transaction — `0x10298800`, `0x10290040` (2026-09-13)

`CAI_BaseNPCTroika::RunAlternateAI` (`0x1028fd80`) ends in `switch (m_eAlternateAI)` over four
modes. **Mode 1 is the door-opening transaction**, and `0x10298800` is what enters it:
`m_bShouldMove = 0` (`+0x1a40`), stop the motor chain (`0x102ee2a0` on `m_pNavigator +0x5d34`),
`m_eAlternateAI = 1` (`+0x644c`).

`0x10290040` is the mode-1 arm. A dead `m_hOpeningDoor` (`+0x5d24`) is the ONE place the transaction
lets go: it resets the mode to 0 and answers false. Otherwise it asks the door's own vtable `+0x3d8`
for a facing point, passing `m_bOpeningDoorWait` (`+0x5d30`); a refusal (`iStack_10 == -1`) answers
false **without** clearing the mode, so it asks again next think. With a point in hand it resets
steering (`0x102e0b40`), turns the direction into a yaw (`0x101d2c70`) and sets the motor's ideal
yaw with an unlimited rate (`0x102e1c10(motor, yaw, -1.0)`). Only once `FacingIdeal` (`0x10278c80`)
agrees does it advance: with `m_bOpeningDoorWait` set it goes to mode 2 with
`m_flAlternateAIExpireTimer = curtime + 1.0` (`_DAT_104454c0`); otherwise it runs `0x10298840` (the
open itself) and, if that succeeds, mode 2 with `curtime + 5.0` (`_DAT_10454110`). Either way it
answers true — the transaction still owns the body.

**Unrecovered:** nothing in the body. **Not built:** the door's NPC-open point (`+0x3d8`), the motor
yaw pair, and `0x10298840`'s own four calls. The **door query is what stops it**, and it runs first:
the transaction is entered, asks the door where to stand, is refused, and answers true again next
think for as long as the mode stands. The facing gate below it is not the obstacle —
`FacingIdeal` is a real ported body whose motor seam stands at retail's aligned `0`, so it answers
**true** on a body that has not turned. Neither mode-2 advance can therefore be reached until a door
carries an NPC-open point, whatever the yaw says.

## The Werewolf's two condition bodies — `0x103d02b0`, `0x103cc890` (2026-09-13)

**`CNPC_VWerewolf::GatherAttackConditions` (`0x103d02b0`)** is the only species override of slot 561,
and it is a **suppression**, not an addition — `thunk_FUN_10269b50` is `ClearCondition`. With
`m_iZoneFlags` (`+0x66e8`) carrying bit `0x4` **or** bit `0x100`, a live enemy (`GetEnemy()`
`+0x29c`) and the absolute difference between the two origins' Z strictly greater than
`_DAT_10462950` = **`40.0`** Source units, it clears `CAN_MELEE_ATTACK1` `0x51` and
`CAN_MELEE_ATTACK2` `0x52` and **returns**: the base gather (`0x1026dd10`) does not run that pass.
Otherwise it is a plain forward. The Z terms are read fresh off `GetAbsOrigin` (`+0x364`), not off
the cached `m_flEnemyHeightDiff`.

**`CNPC_VWerewolf::UpdateConditionDeathTriggered` (`0x103cc890`)** reads as a one-shot latch and is
not one. The body opens with an **unconditional** `ClearCondition(0x7a)`; the
`if (!HasCondition(0x7a))` that guards the output therefore always passes, so
`m_OnConditionDeathTriggered` fires on **every** pass while `m_Activity` (`+0xfec`) is `0x11d` and
`m_DoorState` (`+0x6680`) is `1`, not once. The output carries `GetEnemy()` as activator after a
`0x10265a90` self-reference write, and the body always ends by setting `0x7a` again.

Condition `0x7a` is above the base registrar's `0x76` and belongs to a Werewolf-line table the census
has not decoded.

**Unrecovered:** condition `0x7a`'s name, and the `0x4` / `0x100` zone bits' names. **Not built:**
`m_Activity`'s retail id — this runtime names activities — so the death-triggered gate cannot pass
here; and no registered classname is a werewolf, so neither body is reached in play.

## The two melee attack bands — `0x1026d9a0`, `0x1026da90` (2026-09-13)

Slots 555 and 556, `MeleeAttack1Conditions(flDot, flDist)` and `MeleeAttack2Conditions`. Both open
by dispatching `GetEnemy()` (slot 167) and caching the enemy's `m_pCombatCharacter` (`+0x9c`), then
run a three-rung distance ladder: past an outer band the answer is `COND_TOO_FAR_FOR_MELEE` (`0x09`),
past `_DAT_10451acc` (**64** Source units) it is `COND_TOO_FAR_TO_ATTACK` (`0x60`), and inside it a
dot below `_DAT_104492d0` — read as a **double**, and SDK 2013's twin states the literal **0.7** —
answers `COND_NONE`. A body that gets past the dot asks the enemy's combat character slot 327
(vtable `+0x51c`, base body `0x10345460`, `return 1`) and refuses when it says no.

The two differ in exactly three places. The outer band is `_DAT_1044ddb0` (**256**) for attack 1 and
`_DAT_1044c3a8` (**180**) for attack 2. Attack 1 re-dispatches `GetEnemy()` a second time as a null
gate *after* the dot and refuses when there is none; attack 2 has no such gate and answers
`COND_CAN_MELEE_ATTACK2` (`0x52`) with no enemy at all. Attack 1 then dispatches `GetEnemy()` a
*third* time and reads `GetFlags() & FL_ONGROUND`, answering `COND_CAN_MELEE_ATTACK1` (`0x51`) only
for a grounded enemy and `0` otherwise — retail computes that branchlessly as `-(flags & 1) & 0x51`.
Attack 2 never reads the ground flag.

**The four cells, read from the file (2026-09-20).** The corpus names the cells but does not hold
their bytes; `.rdata` maps RVA `0x445000` to raw `0x445000`, so the file offset is the address minus
the image base `0x10000000`. `_DAT_1044ddb0` = `00 00 80 43` = float **256.0**; `_DAT_10451acc` =
`00 00 80 42` = float **64.0**; `_DAT_1044c3a8` = `00 00 34 43` = float **180.0**; `_DAT_104492d0` =
`66 66 66 66 66 66 E6 3F` = double **0.7**. Each width is the listing's own (`FCOMP float ptr` at
`0x1026d9be`, `0x1026d9db`, `0x1026daaa`, `0x1026dac5`; `FCOMP double ptr` at `0x1026d9f8`,
`0x1026dae0`). Every distance test is `AND EAX,0x4100` / `JNZ` past the return, so each is a strict
`>`: attack 1 answers `0x09` on `(256, ∞)`, `0x60` on `(64, 256]`, and reaches the dot only at
`≤ 64`; attack 2 the same with 180. Both rungs are reachable in both bodies. `_DAT_1044ddb0` is a
pooled literal, not this function's own — sixteen other bodies read it (`CNPC_Crow::
GatherEnemyConditions 0x10357680`, `CNPC_VMingXiaoTentacle::GatherConditions 0x1039ec10`,
`CNPC_VPedestrian::SelectSchedule 0x103a29f0`, `CAI_BaseNPC::StartTask 0x102827f0` among them) — so
256 here says nothing about what it means there.

**Unrecovered:** nothing.

## The victim-side reaction slots — `0x1029f800`, `0x1029f850`, `0x1029f890`, `0x1029f8f0`, `0x1029fb70` (2026-09-13)

Five Troika-line bodies filling slots 21, 22, 23, 27 and 317, and four of them are one shape. Each
calls the detected-attack notice `0x102bf5d0` with the argument entity as the attacker, then **sets**
condition `0x0a` `COND_BEING_ATTACKED` through `SetCondition` (`0x10269a20`). The bare
`(*DAT_10924a6c)->vtable+4` call that sits beside every `SetCondition` in the decompiled C is the
AI-debug ConVar the setter consults, **not** a game event — a `.data` object with 130 readers, no
writer and one dispatched slot.

Slot 21 additionally increments `m_iHitBuildupCount` (`+0x6064`), and it is the only one of the four
that does; the ranged hit path `0x10267b60` dispatches it on the victim immediately before the
attacker's slot 24. Slot 22 is the same body without the increment and `CBasePlayer::Replenish`
(`0x10168320`) dispatches it on a feed target with the player as the argument. Slot 23 is
byte-identical to slot 22 and has no dispatch site in the decompiled corpus at all. Slot 27 adds a
fourth step after the condition: slot 600 (vtable `+0x960`) on **itself** with the attacker, the
melee-coordinator slot request.

Slot 317 (`0x1029fb70`) is the odd one. It never reads its argument, sets `COND_BEING_ATTACKED`, and
then asks `ConditionInterruptsCurrentSchedule(0x0c)` (`0x10269c70`) — "does the running program's
interrupt mask list `COND_SHOULD_DODGE`". Only if it does are the dodge bit set and `true` reported;
otherwise the body answers `false` and the dodge bit is never raised. So the dodge is a property of
the installed program's mask and not of the hit.

**Unrecovered:** what distinguishes slots 21, 22 and 23 — three slots carrying one body, with a
dispatch site for only two of them — and slot 317's parameter type, which no call site observes.

## Story 29c-1, family TroikaHelpers

### The alert-level rungs and their grade letters `0x102b8980`

_Recovered 2026-09-13, story 29c-1._

A three-rung ladder over `m_eAlertLevel` (`+0x63f4`), written through the bare setter `0x102b5dc0`
and answering a single character. `m_bFullInvestigate` (`+0x6340`) forces the level to 3 **before**
the switch reads it, so a full-investigate NPC always lands on the top rung. The switch: any level
other than 1, 2 or 3 writes 1 and answers `'L'`; level 1 writes 2 and answers `'M'`; levels 2 and 3
write 3 and answer `'Q'` plus one when `m_afMemory & 0x8000000` (`+0x5d8c`) stands, i.e. `'R'`.

**Unrecovered:** what the letters are a grade OF — no reader of the return is in the corpus — and the
name of memory bit `0x8000000`.

### The discipline cooldown gate `0x10330020`

_Recovered 2026-09-13, story 29c-1._

Slot 334 asks whether a discipline may be cast. `m_iCurFrenzyCount` (`+0x0ec0`) above zero refuses
outright, before anything else; note the return there is `count & 0xffffff00`, whose low byte is
zero, so it answers **false** and not the count. Otherwise the global byte `DAT_10937cf2` is cleared,
the discipline id and its level are looked up in `DAT_10739a4c` (`0x101e1250`), and a miss answers
true. On a hit it compares the record's cooldown float (`+0x2c`) against `curtime -
m_fDisciplineTimers[row]` (`+0x146c`): a cooldown still greater than the elapsed time sets
`DAT_10937cf2` and answers **false**; an elapsed one answers true with the global left clear.

**Unrecovered:** what reads `DAT_10937cf2`, and the layout of `DAT_10739a4c`'s records beyond the
cooldown at `+0x2c`.

### The state expression pairs `0x102adfe0`

_Recovered 2026-09-13, story 29c-1._

Slot 610 is called with the new `NPC_STATE` and resolves two facial expressions plus a blend weight.
State 2 (COMBAT) resolves `"Anger"` into `m_idxDefExpression` (`+0x10b4`) and `"Anger_No_Deform"`
into `m_idxNoDeformExpression` (`+0x64d0`) with weight `1.0`. States 3, 0xb and 0xe resolve **the
same pair** with weight `0.5` — the weight is the entire difference between the two Anger arms.
States 8 and 10 resolve `"Fear"` / `"Fear_NoDeform"` at `1.0`. Every other state reads the
disposition table `DAT_10924980` keyed on `m_nCurrDisposition` (`+0x64d4`) through `0x100ec360`,
`0x100ec2e0` and `0x100ec3d0`, taking the row's own two indices and its own weight.

**Unrecovered:** what consumes `+0x10b8`; the four names are literals in `.rdata` and are exact.


### `0x102953e0` — `CanSeekCover`, and `CNPC_VLasombra`'s `0x103893c0`

_Recovered 2026-09-13, story 29c-1._

Slot 592's Troika-line body is three arms in order, and the order is the whole of it. First,
`HasCondition(COND_ENEMY_OCCLUDED 0x48)` answers **true** on its own, before the clock is read at
all — an NPC whose enemy is behind something may always look for cover. Second,
`m_flCanSeekCoverTimer` (`+0x607c`) at or before `curtime` answers true. Third, a slop window: if
`m_flCanSeekCoverTimer - 1.0f <= curtime` **and** `COND_CAN_RANGE_ATTACK1` (`0x4f`) is **clear**,
true; otherwise false.

**The slop window is one second, not five.** `0x10295414` is `FSUB dword ptr [0x104454c0]` and
`_DAT_104454c0` is the image's shared `1.0f` — the same cell `animation_and_movers.md:659` reads out
of the listing as `1.0f`. Story 29c's one-line walk called it "a retail 5-second slop window";
that is wrong and the port carries one second. The last arm's `(a < b) != (a == b)` is the FPU flag
pair for `a <= b`, so a timer exactly one second out is still inside the window.

`CNPC_VLasombra::vfunc592` (`0x103893c0`) prefixes one arm: `if (curtime < m_flCoverDisableOverride
(+0x6664)) return TRUE`, else delegate to the Troika body. **The polarity is the permissive one** —
the body returns the FPU flag word for `curtime < override`, which puts the comparison result in AL
— so while the override stands a Lasombra may always seek cover without consulting the rule above.
29c's walk reads it as a refusal; corrected here. The field name is `m_flCoverDisableOverride` and
what it disables is the *rule*, not the seeking.

### `0x1029f940` — `OkToInterruptForMelee`, its gate `0x1028a190`, and `0x103ab400`

_Recovered 2026-09-13, story 29c-1._

Slot 590 is a gate then a whitelist. The gate is `0x1028a190`, shared with the knockback start
(`0x102a01b0`); its decompilation is damaged (an unrecovered jump table) and it is walked off the
listing: if `GetState()` (slot 464) is `4` (`NPC_STATE_SCRIPT`) **and** `m_hCine` (`+0x5d74`)
resolves, then `CCineNPC::CanInterrupt` (`0x101a8930`) on that cine must answer true; then
`m_bInChoreoScene` (`+0x5bc4`) must be clear; then `m_bfAINPCFlags2 & 0x1000` must be clear; and the
body tail-calls slot 158 `IsAlive` (`JMP [EDX+0x278]` at `0x1028a21d`).

**`0x1028a190` is a reader of `MADE_OBLIVIOUS`.** `TEST AH,0x10` at `0x1028a213` against
`[ESI+0x14bc]` is bit `0x1000` of `m_bfAINPCFlags2`. `Source/ElysiumUE/Private/Substrate/
ElysiumNpcFlags.h` records that bit as having "ZERO readers anywhere in retail — a scan of every
access to `+0x14bc` finds no test of `0x1000`"; this address is the counter-example, and the bit
therefore does have a behaviour of its own: an oblivious body cannot be disturbed for melee and
cannot be knocked back.

Past the gate, the body whitelists `m_Activity` (`+0xfec`). The compiler split the set three ways —
a jump table below `0x52`, a `< 0xd26` band and a tail — and the members are `1`, `9`, `0x13`,
`0x30`, `0x4b`, `0x4d`, `0x51`, `0x73`..`0x8a`, `0xcb5`, `0xd25`, `0x1121`, `0x1157`..`0x1158`.
**`0x51` is a member**: the hoisted `if (uVar1 != 0x51)` guards entry to the jump table and falls
*through* to the accept when the activity is `0x51`. 29c's walk lists the set without it.

`CNPC_VSabbatLeader::OkToInterruptForMelee` (`0x103ab400`) is an exception, not a replacement:
activity `0x1141` answers true outright — **skipping the gate entirely**, so a Sabbat leader in a
choreographed scene is still interruptible in that one activity — and every other activity forwards
to the Troika body above.

## Story 29c-1, family Dialogue — the payphone gate and the pedestrian crosswalk

### `CPayphone::CanTalk` — `0x101aaee0` (2026-09-13)

Slot 295's `CPayphone` override, 119 bytes: seven arms, every one a refusal, and the answer is the
last arm's negation.

1. the activator is null;
2. `m_iDialog` (`+0x0128`, keyfield `dialogname`) is zero — the phone authors no conversation;
3. `m_bScriptHidden` (`+0x00f4`), read through the seven-byte getter `0x100b5190`;
4. `m_bWillTalk` (`+0x1088`) is clear, whose only writer is `InputWillTalk` (`0x103418f0`);
5. `m_bfNPCStateFlags` (`+0x5b64`) bit 2 is set — the per-state busy bit `0x1026e3e0` gives to
   retail states 2, 7, 8, 9, `0xa`, `0xb`, `0xd` and `0xe`;
6. `m_bfAINPCFlags & 0x00080000` — `NO_DIALOG`;
7. `IsInDialog` (`0x102c1170`).

29c's walk calls arm 3 "the alive test". It is not: `docs/vtmb/npc-kernel/fields.md` names `+0x00f4`
`m_bScriptHidden`, written by `ScriptHide` (`0x100a8710`) and `ScriptUnhide` (`0x100a8990`). A
script-hidden payphone refuses conversation; a *dead* one is never asked, because the payphone
override tests no liveness at all.

**What the payphone does not test**, against the Troika-line body `0x102c21c0`: the two `IsAlive`
dispatches, `IsUnconscious`, the player-side `0x10175180` and `0x10146b20`, `IsBusyWithDiscipline`,
`NO_DIALOG_PERSISTENT` (word two, `0x10000000`), the menu global's `+0x4ac` and slot 406
(`+0x650`). Seven arms against fourteen — a payphone is a simpler gate than a person, and
`HasDialogSuppressFlag`'s two-bit reading is the Troika line's, not this one's.

### The pedestrian crosswalk — `0x102a0d20`, `0x102a0bc0`, `0x102a0b90` (2026-09-13)

Three bodies and one rule. The signal is four bits in a navigation link's flags word (`link+0x64`),
`0x10`, `0x20`, `0x40` and `0x80`, and the live phase is `0x10 << (((int)curtime >> 4) & 3)` — a
64-second cycle in four 16-second phases, shared verbatim by the first two bodies. It is not a
per-crossing timer: every crosswalk in a map changes on the same global clock.

`CAI_BaseNPCTroika::UpdatePedestrianInfo` (`0x102a0d20`, 313 bytes) runs from `RunAI`
(`0x1028fcc0`) and is the whole rule. Its only outer gate is `GetPathType(m_pNavigator +0x5d34) == 8`
(through `0x102ee620`, itself `path+0x30`). Past it, `SHOULD_INTERACT` (`0x10`), `CROSSWALK_WALK`
(`0x12`) and `CROSSWALK_DONTWALK` (`0x13`) are cleared **on every pedestrian pass**, whether or not
the NPC is at a crossing — so none of the three survives into the next one. Then `AT_CROSSWALK`
(`m_bfAINPCFlags & 0x4`) must be set and `m_flNextCrosswalkUpdateTime` (`+0x6318`) must be strictly
below `curtime`; the stamp is re-armed at `curtime + _DAT_104454c0` = **1.0 s** *before* the signal
is read, so the throttle applies to both arms equally. 29c's walk says 5 s; the listing shows
`FADD float ptr [0x104454c0]` at `102a0deb`.

The two arms read the right way round. The phase bit **set** in `m_pCrosswalkLink (+0x630c)->+0x64`
raises `CROSSWALK_DONTWALK` and **leaves `AT_CROSSWALK` standing** — the pedestrian keeps waiting.
The bit **clear** raises `CROSSWALK_WALK` and clears `AT_CROSSWALK` — the pedestrian is released and
the crossing is given up. Each arm is preceded by the discarded `(*DAT_10924a6c)->vfunc1()` read
that stands in front of every `SetCondition` in this family.

`0x102a0bc0` (178 bytes) is the other end: the navigator's waypoint-advance (`FUN_102f0400`) hands
it `path->CurWaypoint` and it decides whether that waypoint *is* a live crossing. Six gates, all
required — a non-null waypoint, `waypoint+0x10 >= 0` (its NODE id), `waypoint+0x30 != NULL` (the
NEXT waypoint — `102a0bdc MOV EBX,[ESI+0x30]`), `waypoint+0x28 & 0x4`, the same path type `8`, and
the LINK from this waypoint's node (`network->nodes[id]`, `102a0c11`) to the next waypoint's node id
(`102a0c26 MOV EDX,[EBX+0x10]`), found by `0x102f96e0(node, nextId)`, whose `+0x64` carries the live
phase bit. _(Corrected 2026-09-19: the 09-13 walk read `+0x10` as an entity index and `+0x30` as a
hint record.)_ Only then
`0x102a0b90`, which is two statements and no gate: `m_bfAINPCFlags |= 0x4` and
`m_pCrosswalkLink = node`. The waypoint's own bit 2 at `+0x28` and `AT_CROSSWALK`'s bit 2 share a
number by coincidence; the second is written only on success.

**What holds the body — the movement sink's slot 1, `0x10298340` (2026-09-19).** The Troika
override of `CAI_DefMovementSink` slot 1 (the sub-object at `NPC+0x19b0`; the SDK's
`OnCalcBaseMove` position) runs before the base `0x1027dc10`. **Corrected 2026-09-19 (review
against the listing): the `AT_CROSSWALK` bit it tests is the OBSTRUCTING NPC's, not the
mover's** — `[arg1+0x60]` is the move trace's obstruction entity, `[+0x98]` that entity's Troika
self-pointer, `[+0x14b8] >> 2 & 1` its flag (the base body reads the same `+0x60` and then
`+0xa4`, the door self-downcast, which places this slot at the SDK's obstruction callback, not
`OnCalcBaseMove`). So the rule is "I am blocked by an NPC that is waiting at a crossing": then
it re-runs the arrival test `0x102a0bc0` on MY navigator's current waypoint: still red →
`*pResult = 0` and "handled", so the move is swallowed for this frame and I queue behind it;
otherwise, within **32 units** of my waypoint (`d² <= 1024.0`, `0x1045d650`, 3-D) → it advances
the path (`0x102f0400`) itself and answers handled; otherwise the base. The waiter's own stop is
the schedule's; this body is what keeps the walkers behind it from shoving through. Also: `OnStateChange 0x102ae140` clears `AT_CROSSWALK` on every call; `Save` writes the
link as its two endpoint node ids and `OnRestore 0x102998c0` finds it again through
`0x102f96e0`; and the throttle test is STRICT (`102a0de0 AND EAX,0x4100 / JNE` skips on
`curtime <= stamp`) — one opencode walk read it as inclusive and was wrong.

**Closed 2026-09-19 (0018 story 7; `navigation-jump-links.md` § "What the shipped graphs and maps
actually use").** The waypoint's `+0x28` bit 2 is the SDK's `bits_WP_TO_NODE`: BOTH route builders
put it on every graph-node waypoint (`102fcc41 PUSH 4` in the primary `0x102fcbd0`; base 4 in the
pedestrian `0x102fcd00`), so the gate only excludes the goal and detour waypoints. What the
pedestrian builder adds for a crosswalk pair is `0x20`, the SDK's `bits_WP_DONT_SIMPLIFY`, on the
waypoint and the one AFTER it on the route (`102fcd98 OR AL,0x20` hits the waypoint built just
before, and the chain is built goal → start by prepending; corrected 2026-09-20). The phase bits are neither authored per link nor
per phase: `0x102f97c0` (from `CAI_Hint::InputWalk` / `InputDontWalk`) is their only writer and
sets or clears all four at once, and no shipped AIN carries any, so the 64-second clock never
changes an answer — the cycle is the map's `logic_timer` (`sm_hub_1`, `hw_hub_1`: 40 s).

`DAT_10924a6c` is the ConVar `ent_trace_conditions` — the static object at `0x10924a68` (registrar `0x1028bde0`: name `0x105d7ad0`, default `"1"`, help "When ent_trace is on, this will dump info about conditions also."), whose `+4` word is the pointer every condition setter reads a value through (slot 1) and discards: a debug-trace read, no game state (closed 2026-09-19). **Not built:** this runtime has no
navigator path type, no node graph and no navigation link, so all three bodies refuse at the seam
that answers for them, and `RunAI` (slot 432) is still a generated stub, so nothing calls the rule.

### Naming a condition, long and short — `0x102cc300`, `0x1027ede0`, `0x1027e7f0` (2026-09-13)

Retail names a condition twice, through two different tables, and story 29c-1 read both out of
`.rdata` rather than out of a decompiled summary.

`CAI_BaseNPC::ConditionName` (slot 458, `0x102cc300`, 52 bytes) is two steps. An id below
1,000,000,000 — or -1 — is a CLASS-LOCAL id and is translated through this class's
`CAI_ClassScheduleIdSpace` condition sub-space at `+0x30` (`0x102ea2d0`); an id at or above that
constant is already global and skips the translation. The global id is then handed to
`CAI_GlobalNamespace::IdToSymbol` over the one condition namespace `DAT_109203dc` (`0x102ea020`),
which answers `"<<null>>"` for -1 and a NULL pointer for an id it does not carry — which retail
then passes to `printf` as a `%s`. `CAI_BaseNPC::TaskName` (slot 449, `0x102cc350`) is the same 52
bytes against the TASK sub-space at `+0x18` and the task namespace `DAT_109203d4`.

The condition namespace's contents are static: `0x102c8ce0` registers exactly **119** symbols at
local ids `0x00`..`0x76` into `DAT_1090ff38`, which is `CAI_BaseNPC`'s id space `DAT_1090ff08` plus
`0x30`. `COND_NONE` is registered first at 0, so `0x102ea130`'s "expand an empty space" arm sets the
local base to 0 and the last registration raises the local top to `0x76`; the space is its
namespace's root (`0x1030c4e0` constructs it with `isRoot = 0` against `DAT_109203dc`), so the
global base is 0 and **the translation is the identity for every base condition**. The registrar
calls two ids out of sequence — `COND_ENEMY_TOO_FAR` (`0x55`) between `0x49` and `0x4c`, and
`COND_HEAR_BUGBAIT` (`0x6c`) between `0x6b` and `0x6e` — and the namespace is a red-black tree
keyed by id (`0x10249c70`), so call order does not shift any name.

`CAI_BaseNPC::GetShortConditionName` (slot 408, `0x1027ede0`, 16 bytes) forwards to `0x1027e7f0`, a
dense switch over the same 119 ids answering a **three-letter** abbreviation each, with `"***"`
(`0x105cd454`) as the `default:`. The strings sit four bytes apart descending from `0x105cd62c`
(id 0, `"non"`) to `0x105cd458` (id `0x76`, `"ufz"`); the single outlier is id `0x73`, whose
`"fog"` lives at `0x1058b0a0`. Each abbreviation is its `COND_*` name compressed — `fai` is
`COND_TASK_FAILED`, `bmp` is `COND_WAS_BUMPED`, `piv`/`pcf`/`pca`/`psf`/`psa` are the five
player-level rungs — which is what confirms the two tables against each other. Unlike the
navigation-type table beside it, `0x1027e7f0` has **no** -1 case, so -1 takes the `"***"` arm.

Three classes override slot 408, and all three are the same shape: a contiguous block starting at
`0x77`, the id straight above the base table's last, and a `default:` forwarding to
`0x1027ede0`. `CNPC_VMingXiao` (`0x103951d0`) adds eight, `0x77`..`0x7e`:
`xfr xfl xmr xml xbr xbl xsp xmh`. `CNPC_VMingXiaoTentacle` (`0x1039ece0`) adds three,
`tfl tsc tpe` — and its three strings ASCEND in memory where every other block descends.
`CNPC_VWerewolf` (`0x103d0640`) adds five, `ww0`..`ww4`, and is the only one of the three that
wraps itself in a scope-trace push keyed on `m_iName`, popped on every arm including the forward.

**Unrecovered:** the task namespace's 441 `TASK_*` symbols (`0x10316ff0` registers them at runtime)
and therefore the base task sub-space's own range; what each species condition `0x77`+ MEANS, since
only the abbreviation survives. **Not built:** this runtime's task vocabulary carries no registered
numbers, so `TaskName` translates nothing and answers `"<<null>>"` for every id.

## Conditions10 — the flag-word writers, `IRelationType` and `TaskFail`

_Recovered 2026-09-14, story 29d._

The fifteen layer 10–18 bodies over 64 bytes that write or read the two NPC flag words, decide a
disposition, or run a species arm of `TaskFail`. Every constant below was read from the pinned
image's listing or `.rdata`, not from the decompiler's folded output; where it disagrees with the
checklist's one-line walk, the correction is stated at the arm.

### `CAI_BaseNPCTroika::IRelationType` `0x10299da0`

_Recovered 2026-09-14, story 29d._

Slot 404, 541 bytes, and the widest-read body of this band: 5 direct, 9 virtual and 22 chained
callers. Two guards, then three forwarding arms, then the table.

The guards are `candidate == this` (`10299daa`) and `candidate == NULL` (`10299db7`), both `D_ER`,
and both ahead of everything — which is why a species arm that answers `D_ER` on null changes
nothing for null and everything for the arms behind it. `EBX` is then loaded with the candidate's
`+0x9c` (`m_pNPC`, `CBaseEntity`'s self-downcast cache) at `10299dc4` and stays live to the last arm.

**Arm A, the INSANE forwarding (`10299dd2`).** When that NPC carries `D_INSANE`
(`m_bfAINPCFlags2 & 0x20000`, tested as `AND EAX,0x20000 / CMP EAX,0x20000`) and `m_hClosestPlayer`
(`+0x628c`) resolves to a live entity, the answer is `D_HT` if either `this->IRelationType(player)`
— slot 404 again, dispatched VIRTUALLY, so a cop asks its own species body — is `D_HT`, or if
`this->GetEnemy()` is that player. That second call is `vtable +0x2a0`, slot **168**, the Troika
line's mutable `GetEnemy` with the `m_hLastEnemy` fallback. Every failure inside the arm falls
through to arm B rather than answering.

**Arm B, the candidate's boss (`10299eaa`).** Retail reads the candidate's `+0x98`
(`m_pCombatCharacter`) and then `+0x647c` off it. `+0x647c` is a `CAI_BaseNPCTroika` member, so the
read is only meaningful when that combat character is an NPC; for the player it lands on an
unrelated word of `CBasePlayer`. With a resolving boss the arm is the same pair as arm A —
slot 404 virtually, then slot 168 — and answers `D_HT` on either.

**Arm C, my own boss (`10299f0f`).** `m_hFollowerBoss` (`+0x647c`) is read RAW rather than through
slot 293 (the same resolve either way), then taken through its own `+0x9c`. No boss, or a boss that
is not an NPC, chains `CBaseCombatCharacter::IRelationType` and the walk ends. A boss that IS the
candidate answers `D_LI` unconditionally, table or no table. Otherwise `EDI` takes the BOSS's
slot-404 answer toward the candidate; `D_HT` returns at once, and so does the boss's `vtable +0x29c`
— slot **167**, the CONST `GetEnemy` with NO last-enemy fallback, an asymmetry against the two arms
above it — being the candidate. A candidate that is not an NPC then returns the boss's answer.

**The correction.** `10299f84` is `MOV EDI,EAX`: when the candidate IS an NPC, the return register
is REASSIGNED with the candidate's own slot-404 answer toward the boss, and the two tests that
follow (`D_HT`, and the candidate's slot 168 being the boss) are about that second call. So the
fall-through answer for an NPC candidate is **the candidate's relation toward my boss**, not the
boss's toward the candidate. The checklist's walk states the latter, which holds only for a non-NPC.

**Unrecovered:** what the player's `+0x647c` actually holds, which arm B reads blindly.

### `CNPC_VCop::IRelationType` `0x10372b70`

_Recovered 2026-09-14, story 29d._

170 bytes, three arms in front of a direct thunk to `0x10299da0`. A null candidate answers `0`
(`D_ER`) rather than whatever the base would say. `DAT_1093ac3c` is the cop class's SHARED provoker
handle — written only by `CNPC_VCop`'s own `0x10370560` and `0x103705b0` plus its static init, read
by this body and `CNPC_VCop::DrawDebugGeometryOverlays` — and while the candidate IS that entity and
`curtime` is below the paired expiry `_DAT_1093aca8`, the answer is `D_HT`. It is one timed grudge
every cop in the map shares, not a per-cop memory. Then, when the candidate carries a player record
at `+0xa8`: `0x1017f8d0` (`curtime < m_flHeightenedAlertExpireTimer`, `+0x1d1c`) answers `D_HT`, and
`0x1017f770` (`m_iCopsInPursuitCount`, `+0x1d10`) answers `D_HT` when it is greater than zero.

The chain is a DIRECT thunk, so the base body's own three virtual `+0x650` calls re-enter this arm.

**Unrecovered:** nothing.

### `CNPC_VHunter::IRelationType` `0x10388bb0`

_Recovered 2026-09-14, story 29d._

109 bytes, and strictly smaller than the cop's: a null candidate answers `D_ER`, the hunter class's
own static provoker handle `DAT_1093b650` with expiry `_DAT_1093b658` — written only by `0x10387fd0`
and read by nothing but this body — answers `D_HT`, and everything else chains `0x10299da0`. It has
**no** player-side arms: no heightened-alert timer and no cops-in-pursuit count, so a hunter's extra
hostility comes only from that one shared thirty-second timer.

**Unrecovered:** nothing.

### `CAI_BaseNPCTroika::CanBeFedUponBy` `0x102c4a60`

_Recovered 2026-09-14, story 29d._

Slot 342, 77 bytes, three arms. `m_bInvincible` (`+0x63d8`) refuses immediately — retail clears only
the low byte of `EAX` (`AND EAX,0xffffff00`), so the upper bits are stale and the bool is false.
Then `GetFollowerBoss()` (slot 293, `vtable +0x494`): when the boss stands AND the boss IS the
feeder, the feed is refused unless `HasMiscFlag(0x40000)`, which is name **18** of the 22 in the
table at `0x10619ec8` — `No_Resist_Feeding`. So your own follower or ghoul may only feed on you while
that flag stands, and the early return again carries only the cleared low byte. Everything else
defers to `CBaseCombatCharacter::CanBeFedUponBy` (`0x10339800`) with the feeder.

That base body **never reads the feeder**. Its five terms are all about the victim, in order:
`CanBeFedUpon()` (`0x10339a90`, whose whole body past the scope-trace push is
`GetCharTemplate(this)->+0x95 == 0`); `m_bfAINPCFlags2 & 0x8000000` (`NOT_FEEDABLE`) clear; no live
grapple — `m_GrapplePartner` (`+0x1538`) resolving together with `m_GrappleRole` (`+0x153c`) not
being -1 is the refusal; slot 158 `IsAlive()`; and `!IsUnconscious()` (`0x10341aa0`, `m_iMiscFlags`
bit 0, name **0** of the same table). Which is why the Troika override has to make the follower test
itself: the base has no idea who is feeding.

**Unrecovered:** the char template column at `+0x95`.

### `CAI_BaseNPCTroika::CanWitnessSupernatural` `0x1028ef20`

_Recovered 2026-09-14, story 29d._

Slot 587, 118 bytes. The `int` argument is dead in every arm. Five refusals in this exact order,
each answering false on its own: `IsKindred()`; `m_iDialog` (`+0x0128`) non-zero; `m_iIsOblivious`
(`+0x5bb4`) greater than zero; bit `0x10` of `m_bfNPCFrenziedFlags` — the "does not witness" bit
`DoFrenzy`'s `0x9fbd` word carries, and this body is its ONE reader; and `IsBusyWithDiscipline()`.

Past all five the body answers true when `m_iPLSupernaturalFleeLevel` (`+0x6354`) is below 3, and
otherwise returns whether `m_iPLSupernaturalAttackLevel` (`+0x6358`) is below 3. Both are the RAW
authored keyfields — retail does not resolve a negative to the authored-disable 6 here — so a body
authored with flee 3 or more AND attack 3 or more can never witness a supernatural act at all.

**Unrecovered:** nothing.

### `CAI_BaseNPCTroika::vfunc532` `0x10290570`

_Recovered 2026-09-14, story 29d._

Slot 532, 145 bytes, the door-failure cleanup. A jump table at `0x10290604` covers `param_1 - 1` in
`[0,7]`; anything outside falls straight to the tail.

* **Case 1** (`10290587`): `m_eAlternateAI` (`+0x644c`) in `[1,4]` runs the navigator arm, then the
  clear. Outside that window, nothing.
* **Cases 2 and 4** (`10290598`): only `m_eAlternateAI == 4`. `vtable +0x700` is slot **448** — the
  call at `102905a7` is `TaskFail(0xe)`, which runs the WHOLE failure chain from inside the door
  cleanup and BEFORE `m_hOpeningDoor` (`+0x5d24`) is set to -1 and `m_bOpeningDoorWait` (`+0x5d30`)
  to 0. Then the clear.
* **Case 8** (`102905c0`): `m_eAlternateAI` of 0 or less takes nothing; **1..3 takes the clear
  alone** (`102905ca CMP EAX,0x3 / JLE 0x102905ea`); exactly 4 also takes the navigator arm; 5 and
  above take nothing. The checklist's walk says "no clear for values 1-3", which the listing
  contradicts.

The navigator arm (`102905d4`) is `m_pNavigator->IsGoalSet()` (`0x102ee2e0`,
`m_pPath(+0x30)->GoalType(+0x10) != 0` — distinct from `0x102ee680 IsGoalActive`, the
current-waypoint test) gating the door cleanup `0x102bf7e0`, which tests `IsGoalSet` a SECOND time,
calls `StopMoving` and sets `m_bShouldMove` (`+0x1a40`). Every path then chains
`CAI_BaseNPC::vfunc532` (`0x1027e0f0`), whose entire body is the two door words and `return 1`.

**Unrecovered:** what the eight reason bits mean individually; only 1, 2, 4 and 8 have arms.

### `CAI_BaseNPCTroika::UpdateBurstShootPause` `0x102c5500`

_Recovered 2026-09-14, story 29d._

Slot 419, 69 bytes. `GetActiveWeapon()` first: with a weapon, `m_flBurstShootPauseMin` (`+0x5bbc`)
takes `0x102c5780` — the weapon-data word at `wpndata + 0x264` — and `m_flBurstShootPauseMax`
(`+0x5bc0`) the word at `+0x268`, both scaled by `0x102c5570` with the data resolved by `0x102517e0`.
With NO weapon the two retail literals `0x3e99999a` (0.3) and `0x3f000000` (0.5) are written instead
and the body returns.

`0x102c5570` has to be read from the listing: the decompiler turned its x87 compare chain into a
`ushort` of status-word bits and invented a return-storage parameter. The scale seeds at
`_DAT_104454c0` = 1.0 and stays there when `wpndata + 0x26c` is at or below `_DAT_104454c4`. Past
that it measures the distance from this body to `m_hShootTargetOverride` (`+0x5ba8`) or, failing
that, to `GetEnemy()`'s body target, and the scale becomes `sqrt(distance / range)` when the distance
exceeds the same threshold and the distance itself when it does not; with no enemy at all the seeded
1.0 is what gets divided, giving `sqrt(1.0 / range)`. The answer is `scale * (value - wpndata+0x260)`.

**Unrecovered:** the four weapon-data columns' names.

### `CNPC_VAsianVampire::TaskFail` `0x10362390`

_Recovered 2026-09-14, story 29d._

Slot 448, `CNPC_VAsianVampire` only; 117 bytes, most of which is the scope-trace push and pop. The
whole arm is `if (0xb < code && code < 0x10) m_bPathBlocked (+0x66d4) = 1;` — failure codes 12..15 —
followed by the unconditional chain to `CAI_BaseNPCTroika::TaskFail` (`0x1029adb0`) with the code
unchanged. Relative to the base, the AsianVampire contributes exactly that one write.

**Unrecovered:** which failure codes 12..15 are by name.

### `CNPC_VChangBros::TaskFail` `0x1036d1d0`

_Recovered 2026-09-14, story 29d._

Slot 448 for `CNPC_VChangBros`, `CNPC_VChangBrosBlade` and `CNPC_VChangBrosClaw`, sharing one body.
The same 12..15 gate as the AsianVampire arm, but the write is `m_failSchedule` (`+0x5c54`) `= 0x15d`
rather than the path-blocked flag — the same schedule id `SelectFailSchedule`'s Chang arm answers —
then the unconditional chain to `0x1029adb0`. Three retail classes contribute this single arm.

**Unrecovered:** nothing.

### `CNPC_VGargoyle::TaskFail` `0x10379060`

_Recovered 2026-09-14, story 29d._

Slot 448, 77 bytes. In COMBAT (`m_NPCState == 2`, `+0x5cc0`) with `TASK_FAILED` (`0x5c`) standing as
an INTERRUPT condition — `0x10269d30`, which needs an installed schedule and the bit in BOTH the
condition set and the custom mask, not the plain `HasCondition` — it clears the top bit of
`m_afMemory` (`+0x5d8c &= 0x7fffffff`). Then `m_iShunnedFindPillar` (`+0x6680`) is zeroed and the
body chains `0x1029adb0`.

The middle pair is where the checklist's walk stops: it records `1037908c CALL 0x10006613` as
reached with a `this` that "has no visible prior assignment in the decompile (likely a lost this
alias rather than a confirmed retail bug — needs an asm check before this arm is ported)". The asm
check settles it. `0x10379040` is four instructions —
`MOV EAX,[ECX+0x14b8] / SHR EAX,4 / AND AL,1 / RET` — and never writes `ECX`, so `ECX` still holds
`this` from `10379081`. **There is no bug.** Both bodies are plain `m_bfAINPCFlags` accessors for bit
`0x10`, `FINDING_BODY`: `0x10379040` reads it and `0x10379000` writes it, so the arm is
`if (IsFindingBody()) SetFindingBody(false)`.

**Unrecovered:** nothing.

### `CNPC_VHengeyokai::TaskFail` `0x10380510`

_Recovered 2026-09-14, story 29d._

Slot 448, 130 bytes, and the template for the Tzimisce arm below it. In order: the same combat-plus-
`TASK_FAILED`-interrupt clear of `m_afMemory`'s top bit; then, when `FINDING_BODY` stands
(`0x10381be0` is `(m_bfAINPCFlags >> 4) & 1`, NOT a species word), `0x10382970(this,
m_hPickupTarget)` runs and `0x10381ba0(this, false)` clears that same bit; then, when `CARRYING_BODY`
is CLEAR (`0x10381c80` is `(m_bfAINPCFlags >> 5) & 1`), `0x102c43b0(this, 0.75)` re-arms
`m_flIgnoreCollisionTimer` (`+0x6458`) and `m_hPickupTarget` (`+0x6664`) goes to -1; then
`m_iShunnedFindFish` (`+0x6678`) is zeroed and the body chains `0x1029adb0`.

**`0x10382970` does not release the pickup target.** It is a `CUtlVector<BlacklistedEntity_t>`
grow-and-append — the standard 4 / double / step growth, then a `memmove` of the tail and a
`(handle, float)` pair written at the insertion point — onto `m_BlacklistedEntities` (`+0x66a4`),
with the float `curtime + _DAT_1044eb0c` and `_DAT_1044eb0c` = **20.0**. The target is SHUNNED for
twenty seconds; the release is the separate `FINDING_BODY` clear on the next line. The checklist's
walk calls it a release.

`0x102c43b0` itself is `if (GetIgnoreCollisionEntity()) { m_flIgnoreCollisionTimer = curtime +
delay; 0x102c43f0(this); }`, and `0x102c43f0` is `if (timer <= curtime) { <clear the ignored
entity>; timer = FLT_MAX; }` — so a zero or negative delay expires in the same call.

**Unrecovered:** nothing.

### `CNPC_VTzimisce::TaskFail` `0x103ba350`

_Recovered 2026-09-14, story 29d._

Slot 448, 130 bytes, the Hengeyokai arm with its own words. `0x103be090` and `0x103be130` are
byte-identical to `0x10381be0` and `0x10381c80` — `FINDING_BODY` and `CARRYING_BODY` on
`m_bfAINPCFlags` — and `0x103be050` is the same bit-`0x10` writer. `0x103bf200` is the same
twenty-second blacklist append against `m_FailedPickupTargets` (`+0x6690`). The species words are
`m_hPickupTarget` at `+0x6670` and `m_iShunnedFindBody` at `+0x66b8`.

**Unrecovered:** nothing.

### `CNPC_VMingXiao::TaskFail` `0x10394090`

_Recovered 2026-09-14, story 29d._

Slot 448, 88 bytes, a switch on `m_eThrowableObjectMode` (`+0x673c`). Modes **3** and **4** call
`0x102e0a60(m_pMotor, 0x43340000)` — `m_pMotor + 0x1c = 180.0f`, the same steering reset the Troika
body itself makes — and touch nothing else, so the mode and the throw handle both survive. Every
other mode calls `0x10398d90(this, 0)` and then sets `m_hThrowObject` (`+0x6718`) to -1.

`0x10398d90`'s whole body is `m_eThrowableObjectMode = arg`. It is the MODE setter, not a
throwable-prop clear as the checklist's walk has it, so the default arm's first act is to put the
mode back to 0.

**Unrecovered:** what the throwable-object modes are by name.

### `CNPC_VSheriffMan::TaskFail` `0x103b0290`

_Recovered 2026-09-14, story 29d._

Slot 448, leaf only, 100 bytes. Apart from the scope-trace push and pop the entire body is a single
direct call to `CAI_BaseNPCTroika::TaskFail` (`0x1029adb0`) with the incoming code unchanged. The
recovered fact is the ABSENCE of a species override: unlike the Hengeyokai, Tzimisce and MingXiao
variants elsewhere in this pack, the sheriff carries no additional `TaskFail` arms at all and needs
only correct dispatch.

**Unrecovered:** nothing.

### `0x1028d990` — the AI trace line

_Recovered 2026-09-14, story 29d._

906 bytes, no slot; the formatter slots **17** and **18** push every trace message through. Retail's
signature is `(this, const char* message, int indent, char* out, int size)` and it writes into the
caller's buffer.

`1028d9a0` / `1028d9a8`: a null `out` or a `size` of zero or less writes **nothing at all** — not
even an empty string — and the entire body is skipped. A null message is replaced by a local NUL at
`1028d9c1`, and a negative indent is clamped to 0 at `1028d9d4`.

**The `CONDS:` block** (`1028da06`) is gated only by the schedule-debug ConVar `DAT_10924a6c`, on the
usual idiom: `cv->vtable[4]()` must answer 0 (the object is a variable, not a command) and the int at
`cv + 0x2c` must be **greater than** zero. It `sprintf`s `"CONDS:"` (`0x105d8908`), then walks
`[0, GetLastSharedCondition())` — slot 409, re-read on EVERY iteration at `1028da73` — appending
`" %s"` (`0x105a3060`, the space is LEADING) with `GetShortConditionName(id)` (slot 408) for each id
whose `HasCondition` (`0x10269aa0`, the raw set at `+0x5c5c`) stands, and closes with `"\n"`
(`0x10547e40`).

**Two ladders, not three.** The checklist's walk says three. `1028dac6` renders 32 glyphs of
`"PIS__PF_T_L__TTEPLM________ICCCC"` (`0x105d88e0`) over `m_afMemory` (`+0x5d8c`) and `1028db20`
renders 30 of `"RSCPFCNFIPCDHVAEFSBDSLIAMFDPOIO_"` (`0x105d88b8`) over `m_bfAINPCFlags` (`+0x14b8`),
a set bit taking the legend's character at that index and a clear bit `'.'` (`0x2e`). The third
buffer (`1028db56`) is only ever NUL-terminated and never written, so the fifth `%s` of the full
format always prints nothing. The two loops differ only in their test spelling — `TEST EAX,EDX / JZ`
against `AND ESI,EAX / CMP ESI,EAX / JNZ` — which for a single bit is the same question.

**The `NAV` pair** (`1028db6b`) needs the listing too; the decompiler dropped both arguments.
`GetNavType()` (`0x1027d990`, `m_pNavigator(+0x5d34)->+0x18`) is called FIVE times. Unless it is 3 or
1 the buffer is emptied and nothing prints. The first `%s` of `"NAV %s %s"` (`0x105d888c`) is
`"CLIMB"` (`0x105d88a0`) on nav type 3 and five spaces (`0x105d8898`) otherwise; the second is
`"JUMP"` (`0x105d88b0`) on nav type 1 and four spaces (`0x105d88a8`) otherwise. So a climbing body
prints `NAV CLIMB` and a jumping one `NAV` then four blanks then `JUMP`, in aligned columns.

**The three format strings**, selected by the two debug BYTES `DAT_10920534` and `DAT_10920535`:

| `534` | `535` | `.rdata` | Format |
|---|---|---|---|
| set | set | `0x105d8868` | `%6.2f : %*s %s\n%s%s %s%s %s\n\n` |
| set | clear | `0x105d8854` | `%6.2f : %*s %s\n` |
| clear | — | `0x105d8828` | `%-20s  %6.2f : %*s %s\n%s%s %s%s %s\n\n` |

The `%6.2f` is `gpGlobals->curtime`; the `%*s` takes the indent as its WIDTH and the empty string as
its value, so the field is that many spaces; the leading `%-20s` of the third arm is
`GetDebugName()`. The four optional blocks are built when `DAT_10920534 == 0 || DAT_10920535 != 0`,
which is exactly the two arms that consume them — the short arm builds none of them.

**Unrecovered:** what each glyph of the 32-character memory legend stands for (the 30-character flag
legend is confirmed by the `0x1030cbd0` name table); and whether `DAT_10920534` and `DAT_10920535`
have recovered console names.

### `0x102c54c0` — the fake-reload reroll

_Recovered 2026-09-14, story 29d._

43 bytes and under the walk threshold, recorded here because the reroll site is what makes
`m_iFakeReloadCount` (`+0x65f0`) mean anything. `0x10207c40` is `GetCharTemplate(this)` followed by
`0x101d5e80` on the template manager `DAT_10738d10`; the engine random stream's `vtable +8`
(`RandomInt`) is then called with the template ints at `+0x34` and `+0x38`, and the draw is stored.
Its two direct callers are the AsianVampire's reload arms; nothing reaches it virtually.

**Unrecovered:** the names of the template columns at `+0x34` and `+0x38`.

## Story 29d, family Combat10 — ranged-combat selection and the ideal-state pre-select

Slot 605 is six bodies, not one: a Troika line, a shared human arm that fills 36 species slots, and
four species arms. Every one of them is an **ordered** body — the arms are tried in retail's order
and the first that answers wins — and every arm stamps `+0x1b30` with its own `.cpp` name and
`+0x1b34` with the line, which is how the six were told apart. Three helpers sit between the arms
(`0x102b8620`, `0x102b7370`, `0x102b7cf0`) and a fourth (`0x102b7f40`) decides the dodge; which of
the four a body offers, and in what order, is most of what separates the six.

Every body answers a **raw retail schedule number**. Of the two dozen those numbers name, this
runtime registers three (`0xb1`, `0xb9`, `0xec`).

### `CAI_BaseNPCTroika::SelectScheduleRangedCombat` `0x102b7fc0`

_Recovered 2026-09-14, story 29d._

677 bytes, `CAI_BaseNPCTroika#605` plus twenty more, and the arm a spawned `npc_VCop` reaches (its
census classname list is null, so its `RetailClass()` is null and every species lookup falls
through). In order:

1. `102b7fc6` — `m_bInMelee` (`+0x6078`) gives `0xe3`, line 0x5d98.
2. `102b7fe6` — `COND_TOO_CLOSE_FOR_RANGED` (`0x08`) **and** slot 307 `HasUsableMeleeWeapon`
   (`vt+0x4cc`) **and** slot 599 (`vt+0x95c`) on `GetEnemy()` (slot 167) gives `0xe3`, line 0x5db2.
3. `102b8027` — `COND_WEAPON_THROUGH_WALL` (`0x3c`) gives **`0xb8`**, line 0x5db7. This is the one
   arm `CNPC_VAsianVampire` diverges on.
4. `102b8047` — the weapon pre-pass `0x102b8620` wins whenever it answers non-zero. The door helper
   and the taunt prologue are **not** offered on this line.
5. `102b8056` — the split. Neither `COND_TOO_CLOSE_TO_ATTACK` (`0x5f`) nor `COND 0x08`, **and**
   `0x101e3f50(&DAT_10739a4c, this)` false: slot 606 (`vt+0x978`) wins if non-zero, else
   `COND_EXTENDED_BLOCKED_BY_FRIEND` (`0x2e`) gives `0xbd` (0x5dff), else `COND_TOO_FAR_TO_ATTACK`
   (`0x60`) gives `0xb1` (0x5e04), else **0**.
6. `102b8121` — otherwise, neither `COND_WAITING_ATTACK_TIME` (`0x2f`) nor
   `COND_WEAPON_BLOCKED_BY_FRIEND` (`0x63`): `0x102b7f40` gives `0xef` (0x5dd8), else slot 307
   **and** `RandomInt(0,99) < 0x19` **and** slot 599 gives `0xe8` (0x5ddc), else `0xf0` (0x5de0).
7. `102b818c` — else the same pair with `0xb8` in place of `0xef` (0x5de9), **no roll** before slot
   599 giving `0xe8` (0x5ded), else `0xb9` (0x5df1).

Note the discipline gate's subject: the Troika line passes **`this`**, the human arm passes the
enemy's `+0x9c`.

**Unrecovered:** `DAT_10739a4c`'s discipline identity, and the console name of the melee-range
convar `DAT_10924a1c` the pre-pass thresholds on.

### `CNPC_VHuman::SelectScheduleRangedCombat` `0x10386560`

_Recovered 2026-09-14, story 29d._

802 bytes, filling 36 species `#605` slots and **no Troika-line slot** — it replaces the Troika body
wholesale and never chains it. It differs from `0x102b7fc0` in three places: a cover-hint arm in
second position, two extra helpers in step 4, and the discipline gate on the ENEMY.

1. `10386566` — `m_bInMelee` gives `0xe3`, line 0x6cf.
2. `10386588` — the cover hint. An empty `m_pShootAtHint` (`+0x6444`) is filled from slot 609
   (`vt+0x984`, argument `0`); when it is **still** empty the arm is skipped entirely, otherwise
   `!COND_WAITING_ATTACK_TIME` gives `0xec`, line 0x6e4.
3. `103865d7` — the `COND 0x08` / slot 307 / slot 599 triple gives `0xe3`, line 0x6e9.
4. `1038661d` — `COND 0x3c` gives `0xb8`, line 0x6ee.
5. `1038663d` — three helpers, first non-zero wins, **in this order**: `0x102b8620`,
   `0x102b7370 SelectDoorObstructionSchedule`, `0x102b7cf0`.
6. `10386675` — the split, with `0x101e3f50` applied to `GetEnemy()->+0x9c` (its combat-character
   self-downcast, so the entity itself for a combat character and null otherwise). Then slot 606,
   `0xbd` (0x736), `0xb1` (0x73b), or 0.
7. `103866f2` / `10386720` — the same dodge/spacing pair as the Troika body, lines 0x70f, 0x713,
   0x717 and 0x720, 0x724, 0x728.

**Unrecovered:** nothing in the body; the hint store slot 609 searches is family Hints' seam.

### `CNPC_VAsianVampire::SelectScheduleRangedCombat` `0x103620d0`

_Recovered 2026-09-14, story 29d._

546 bytes inside a scope-trace frame, `CNPC_VAsianVampire#605` only. It replaces the Troika base and
is the shortest of the six: no dodge helper, no slot-606 arm, no cover-hint arm, and neither the
door helper nor the taunt prologue — the weapon pre-pass is the only helper in front of it.

1. `1036213a` — `m_bInMelee` gives `0xe3`, line 0x26a.
2. `10362163` — the `COND 0x08` triple gives `0xe3`, line 0x284.
3. `103621c2` — `COND 0x3c` gives **`0xf0`**, line 0x289. The Troika base answers `0xb8` here; this
   is the whole of the divergence and it is one number.
4. `103621f4` — `0x102b8620`.
5. `10362210` — `COND 0x5f` **or** `COND 0x08`, and neither `COND 0x2f` nor `COND 0x63`, gives
   `0xf0`, line 0x2af. A body that fails the inner pair falls through rather than answering.
6. `10362253` — `COND_SEE_ENEMY` and not `COND_ENEMY_OCCLUDED` and not `COND_TOO_FAR_TO_ATTACK`
   gives `0xf0`, line 0x2e5.
7. `103622b1` — `COND_ENEMY_UNREACHABLE` takes `GetJumpSchedule` (line 0x2ce), else `0xe8` (0x2d2).

**Unrecovered:** nothing.

### `CNPC_VBach::SelectScheduleRangedCombat` `0x103642f0`

_Recovered 2026-09-14, story 29d._

413 bytes, `CNPC_VBach#605` only, and the one arm of the six that **chains**: its classname tests
fall through to `CNPC_VHuman`'s body `0x10386560` through a direct non-virtual call, and its own
tail then rewrites that answer. Three of its conditions — `0x79`, `0x7a`, `0x7b` — live above the
base registrar's `0x76` in a Bach-line table the census does not carry, so they have no names.

1. `103642f6` — `COND 0x7b` stamps `+0x6690` with `curtime + _DAT_10463584` (**15.0**) and answers
   `0x15a`, line 700 (decimal in the listing, unlike the rest).
2. `1036433d` — `GetActiveWeapon()` is fetched once and `COND 0x7a` tested once, before the split.
3. `10364355` — with **no** weapon: `COND 0x7a` gives `0x158` (0x2da), `COND 0x79` gives `0x159`
   (0x2de).
4. `1036439b` — with a weapon and `COND 0x7a`: a classname that is not `item_w_katana`
   (case-insensitive) gives `0x158` (0x2c8); the katana itself **dispatches slot 604**
   `SelectScheduleMeleeCombat` (`vt+0x970`) and returns its answer.
5. `103643f7` — `COND 0x79`: a classname that is not `item_w_rem_m_700_bach` gives `0x159` (0x2d3);
   the matching rifle falls through.
6. `10364447` — `CNPC_VHuman::SelectScheduleRangedCombat`.
7. `1036445a` — **unconditionally**, an `m_NPCState` (`+0x5cc0`) that is neither `4` nor `0xc`
   clears `+0x6444`. This runs whatever the human body answered.
8. `1036447a` — a human answer of `0` with `COND_SEE_ENEMY` standing is rewritten to `0x15f`
   (0x2ed); anything else is handed back unchanged.

**Unrecovered:** the names of conditions `0x79`, `0x7a` and `0x7b`, and of the word at `+0x6690` on
the Bach line (`CNPC_VScurrying` owns the same offset as `m_flDetectionDistance`, which is a
different word on a different line).

### `CNPC_VMingXiao::SelectScheduleRangedCombat` `0x103967d0`

_Recovered 2026-09-14, story 29d._

794 bytes, `CNPC_VMingXiao#605` only — the human skeleton with **three** things removed or changed:

* there is **no `COND 0x3c` arm** at all, so a Ming Xiao with its weapon through a wall falls to the
  split instead of answering `0xb8`;
* there is **no `0x101e3f50` discipline gate** in front of the slot-606 branch, which is therefore
  entered on the two conditions alone;
* the dodge decision is **inlined** and is only the FIRST arm of `0x102b7f40` —
  `SelectWeightedSequence(0x10, -1) != 0`, `!COND_STOP_BACKUP (0x2c)`, `RandomInt(0,99) < 0x4b` — at
  the same `0x4b` threshold. The helper's discipline arm and its `COND 0x3c` arm are not there.

Lines: 0xb12, 0xb27, 0xb2c, 0xb6e, 0xb73, 0xb49, 0xb4d, 0xb51, 0xb58, 0xb5c, 0xb60. The three
helpers (`0x102b8620`, `0x102b7370`, `0x102b7cf0`) are offered in the human's order.

**Unrecovered:** nothing.

### `CNPC_VSheriffMan::SelectScheduleRangedCombat` `0x103afdb0`

_Recovered 2026-09-14, story 29d._

984 bytes, `CNPC_VSheriffMan#605` only, and the longest of the six because it offers the three
helpers as three separate tests rather than as one chained condition. Its own two differences:

* **no slot-606 arm at all.** Where every other body reaches `vt+0x978`, the sheriff's branch is
  `COND_SEE_ENEMY` and not `COND_ENEMY_OCCLUDED` and not `COND_TOO_FAR_TO_ATTACK`, which
  **declines** (`103aff56`); otherwise `COND_ENEMY_UNREACHABLE` gives **`0x15a`** (line 0x314), else
  `0xe8` (0x31a). `0x15a` is a number no other slot-605 body names.
* the same inlined first-arm-only dodge as Ming Xiao's, at `0x4b`.

Lines: 0x2b1, 0x2cb, 0x2d0, 0x314, 0x31a, 0x2ed, 0x2f1, 0x2f5, 0x2fc, 0x300, 0x304.

**Unrecovered:** nothing.

### `FUN_102b8620` — the ranged weapon pre-pass

_Recovered 2026-09-14, story 29d._

688 bytes, no slot and no verdict row of its own, and the first helper every one of the six
selectors offers. It owns reload `0xc4`/`0xc6`, the reload-cover pair `0xc2`/`0xc3`, the draw
`0xe9`, the melee switch `0xe3`, the spacing trio `0xe4`/`0xe5`/`0xe7` and the no-weapon `0x98`. In
order:

1. `102b8626` — the convar `DAT_10923d3c` (its `vtable +0x04` bool CLEAR and its `+0x2c` int
   non-zero), a live `GetActiveWeapon()`, its `vtable +0x5a0` capability word carrying `0x6000`, and
   `m_iFakeReloadCount` (`+0x65f0`) below 1: `thunk_FUN_102c54c0(this)`, then `m_pHintNode`
   (`+0x5ddc`) empty gives `0xc4` (0x5e8f), else `0xc6` (0x5e93).
2. `102b86ba` — a weapon whose `+0x74c` first magazine entry is **above zero** declines the whole
   pre-pass. This is the ordinary path for an armed, loaded body.
3. `102b86e5` — an empty weapon whose `vtable +0x460` admits a reload and whose reserve
   (`thunk_FUN_103346c0` on its `+0x744` ammo type) is at least 1 takes cover to reload:
   `COND_SEE_ENEMY` or `COND_NEW_ENEMY` gives `0xc2` (0x5ecb), else `0xc3` (0x5ecf).
4. `102b87b3` — slot 308 `HasUsableRangedWeapon` (`vt+0x4d0`): `GetEnemy()`, slot 601 (`vt+0x964`,
   which clears `m_bInMelee`), `0xe9` (0x5ea2).
5. `102b87e8` — slot 307 `HasUsableMeleeWeapon`: slot 599 on the enemy gives `0xe3` (0x5ea8); else
   the melee-range convar `DAT_10924a1c` (`0.0` when its bool is SET, its `+0x28` float otherwise)
   plus `_DAT_104492b8` (**200.0**) below `m_flEnemyDist` (`+0x6268`) gives `0xe7` (0x5eae); else
   **`RandomInt(0,99)` is drawn** and, only when it is above `0x18` **and** the distance is at or
   beyond the bare convar, `0xe5` (0x5eba); else `0xe4` (0x5eb6). The roll is consumed either way.
6. `102b88b8` — no usable weapon at all gives `0x98` (0x5ec1).

**Unrecovered:** the console names and defaults of `DAT_10923d3c` and `DAT_10924a1c`, and the
identity of the weapon vtable slots `+0x5a0`, `+0x460` and `+0x450`.

### `FUN_102b7f40` — the dodge test

_Recovered 2026-09-14, story 29d._

93 bytes, no slot, and **three arms rather than one**. An earlier one-line walk (of Ming Xiao's
inlined copy) described only the first:

1. `102b7f46` — `SelectWeightedSequence(ACT 0x10, -1)`. Retail tests it **`!= 0`, not `!= -1`**, so
   a body that authors no such sequence still passes this gate.
2. `102b7f5f` — `!COND_STOP_BACKUP (0x2c)` **and** `RandomInt(0,99) < 0x4b` gives true.
3. `102b7f85` — `0x101e3f50(&DAT_10739a4c, this)` gives true. **Reached even when `COND 0x2c`
   stands**, because arm 2's refusal does not exit.
4. `102b7f96` — `COND_WEAPON_THROUGH_WALL (0x3c)` gives true, on the same footing.
5. `102b7fa6` — otherwise false.

`CNPC_VMingXiao` and `CNPC_VSheriffMan` inline arm 1 and arm 2 only, which is why they are not calls
to this body.

**Unrecovered:** `DAT_10739a4c`'s discipline identity, as above.

### `FUN_102b7cf0` — the taunt-and-cover prologue

_Recovered 2026-09-14, story 29d._

462 bytes, no slot, offered third by the human, Ming Xiao and Sheriff-man selectors.

**One arm sits outside everything else**: `102b7cf6`, `COND_LOST_ENEMY` (`0x47`) gives `0x10`, line
23844. Everything below is inside **one** block opened at `102b7d2a` on
`GetEnemy() != 0 && CanSeekCover()` (slot 592, `vt+0x940`) — the cover offer, the convar gate, the
`0x800` taunt arm and the `SEE_ENEMY` arm alike. A body with no enemy answers `0`, whatever its
flags say. (An earlier one-line walk read the convar-gated half as a sibling of that gate; it is
not.) Inside it, in order:

1. `102b7d4a` — `0x102b7690(1, 1, 1, 1)`, the entrenched cover / kick-prop selector; any non-zero
   answer wins.
2. `102b7d69` — the convar `DAT_109248f4`, read the same way as `DAT_10923d3c` above.
3. `102b7d8a` — `m_bfAINPCFlags & DODGING (0x800)` is **consumed**, then `RandomInt(0,99)`: above
   `0x45` advances `m_flNextDodgeTime` (`+0x65a4`) by `_DAT_10463584` (**15.0**, read out of the
   pinned image) and answers `0x8f` (line 23891); otherwise it sets `FORCED_OCCLUDE` (`0x10000000`)
   and answers `0x8e` (line 23884). The advance is an **add**, not a re-base on `curtime`.
4. `102b7e12` — five terms, all required: `COND_SEE_ENEMY`; `m_flNextDodgeTime` past `curtime`;
   `m_bfNPCFrenziedFlags` (`+0x5b84`) carrying `0x2000`; `m_bfAINPCFlags2` (`+0x14bc`) **lacking**
   `D_INSANE` (`0x20000`); and `m_bStayEntrenched` (`+0x6435`) clear. It re-arms
   `m_flNextDodgeTime = curtime + RandomFloat(10.0, 20.0)` and rolls again: above `0x3b` gives
   `0x8d` (line 23909), else `0x8c` (line 23905).

**Unrecovered:** the console name and default of `DAT_109248f4`, and the name of the frenzied-word
bit `0x2000` (no name table covers `+0x5b84`; both `DoPossession`'s `0x3b1c` and `DoFrenzy`'s
`0x9fbd` carry it, so it reads as "under one of the two AI disciplines").

### `CAI_BaseNPCTroika::PreSelectIdealState` `0x102ad340`

_Recovered 2026-09-14, story 29d._

Slot 460, 629 bytes, and the one body that decides an NPC's ideal state ahead of `SelectSchedule`.
`+0x1b38` is set to `2` unconditionally at entry and each arm then stamps `+0x1b3c` with
`AI_BaseNPCTroika.cpp` and `+0x1b40` with its line.

1. `102ad34f` — `m_eForcedState` (`+0x65cc`) non-zero: copied into `m_IdealNPCState` (`+0x5cc4`),
   the forced word cleared, line 0x4495, and it is **returned** — nothing below runs.
2. `102ad37a` — the cover timer, which is not an answer: `HasInterruptCondition(COND_SEE_ENEMY)`
   **and** `m_flCanSeekCoverTimer` (`+0x607c`) exactly `0.0` arms it to
   `curtime + RandomFloat(1.0, 3.0)`; otherwise plain `HasCondition(COND_ENEMY_OCCLUDED)` zeroes it.
   The asymmetry between the interrupt test and the plain test is retail's.
3. `102ad3c6` / `102ad3d8` — the two flee helpers `0x102ad260` and `0x102ad2d0` in that order, each
   of which writes `m_IdealNPCState` itself and whose non-zero answer means it already did.
4. `102ad3ef` — `HasInterruptCondition(COND_SEE_FEAR)`: the flag write is **nested**, armed only
   while `m_NPCState` is not already `8`, and the ideal state is then written **unconditionally** to
   `8` (line 0x44b8). (A one-line walk read the two as alternatives; the listing nests them.)
5. `102ad429` — `HasInterruptCondition(COND_SUPERNATURAL_ATTACK_LEVEL)` gives `2`, line 0x44be.
6. `102ad459` — `HasInterruptCondition(COND_CRIMINAL_ATTACK_LEVEL)`; with none of the above and not
   this one, the body returns `0` **without touching the ideal state**.
7. `102ad4d9` — the obfuscated `m_iPLCriminalLevelWitnessed` (`+0x6364`) is unscrambled inline and
   decoded through `0x1042fe90`, then compared against the same decode of the literal `0x3cf445af`,
   which evaluates to **2**. At or below 2, with `m_NPCState` neither `2` nor `0xe`: resolve
   `m_hCriminalOffender` (`+0x638c`) and, when slot 404 `IRelationType` on it answers `1` (`D_HT`),
   fall straight to the combat tail; otherwise `m_bAllowCriminalSuspicion` (`+0x65f8`) writes
   `m_iSubState` (`+0x63f8`) to `0` and answers `0xe`, line 0x44d3.
8. `102ad58a` — the shared tail: `2`, line 0x44cb. It is reached from four different failures.

**`+0x65cc` is `m_eForcedState`, a raw `NPC_STATE`.** `0x102ae840` — the scripted-order push — takes
an `NPC_STATE` as its first argument and stores it there beside `m_bForceStateChange` (`+0x1b28`)
and `CHOOSE_NEW_SCHEDULE`; this slot is its only consumer and it consumes it as the ideal state.

**Unrecovered:** the retail names of `NPC_STATE` `0xe` (the criminal-suspicion window) and of
`m_iSubState`'s value space.

## Hints10 — `FValidateHintType` and the Werewolf's hint endpoints

_Recovered 2026-09-14, story 29d._

Story 29d, family **Hints10**: which hint an NPC is allowed to use, and where a hint's far end is.
Seven rows. The port is `Substrate/ElysiumNpcKernelHints10.{inl,cpp}`, the suite is
`Elysium.Substrate.NpcKernelHints10.`, and every seam these bodies read through was built by story
29c-1's family Hints (`ElysiumNpcKernelHints.inl` — there is no `CAI_Hint` on this substrate, and
`FHintWords` is the typed view of one hint's own datamap words).

### `CAI_BaseNPCTroika::FValidateHintType` `0x10295c20`

_Recovered 2026-09-14, story 29d._

Slot 566, `vtable +0x8d8`, the canonical Troika-line fill (~51 census classes). 544 bytes past its
scope-trace prologue.

A null hint takes the `XOR AL,AL` tail at `10295c9a`. Otherwise the body gates on the HINT GROUP
before it looks at the type at all: `hint->m_iGroupID` (`+0x470`) AND `this->m_iHintGroups`
(`+0x62e4`) must be non-zero. **Both words are 32-bit SETS, not ids** — one bit per group, which is
why `0x102989e0` parses `hint_groups` into a mask and why an unauthored list is `0xffffffff`. When
the AND is zero the body refuses, and only when the globally tracked debug NPC (`DAT_10925444`
through `PTR_DAT_10566458`) **is this body** does it first format the two masks and `DevMsg`
`"Hint group id %s usable %s"` (`0x105d8dd0`) onto the hint through `0x102d0ab0`. Each mask is
rendered by a `0x20`-iteration loop that appends the format at `0x105a1814` — which the pinned image
reads as `" %d"` — for every set bit, with the **1-based** index (`10295cf0 LEA EDX,[ESI + 0x1]`),
so bit 0 prints `" 1"`.

An admitted hint is then dispatched on `m_nHintType` (`+0x5dc`) by numeric range. Both boundaries
that story 29d's checklist had only inferred are confirmed at the listing:

| `m_nHintType` | Arm | Listing |
|---|---|---|
| `< 0x64` | false | `10295d92 CMP EAX,0x64 / JL` |
| `0x64 .. 0x65` | `0x102974f0` (`IsHintCoverValidLoose`) | `10295d97 CMP EAX,0x65 / JLE` |
| `== 0x2774` | **true**, outright | `10295dac MOV AL,1` |
| `== 0x27d8` | `0x10297430` (`IsHintCoverValid`) | `10295d8d JZ` |
| `> 0x27d8` and `< 0x283c` | false | `10295df2 CMP EAX,0x283c / JL` |
| `0x283c .. 0x283d` | `0x10295ed0` (the quiet cover rule) | `10295dfd CMP EAX,0x283d / JLE` |
| `== 0x28a0` | `0x102961a0` (the verbose cover rule) | `10295e04` |
| anything else | false | `10295d69` |

The `0x2774` accept sits **inside** the `99 < t` block, so the structure is `t > 0x27d8` /
`t == 0x27d8` / `t >= 0x64` / else-false rather than a flat switch. All four callees were already
ported — two by family Hints (`IsHintCoverValid`, `IsHintCoverValidLoose`) and two by family
BaseHelpers (`FUN_10295ed0`, `FUN_102961a0`) — and this body calls them.

**Port convention settled here**: the generated slot signature spells the hint `void*` because
retail's parameter is `CAI_Hint*` and this substrate has no such entity. The `void*` **is** a
`const FHintWords*`; a caller holding a `ScheduleHost::HintNode`-shaped index goes through
`FValidateHintTypeNode`.

**Unrecovered:** nothing.

### `CNPC_VManBat::FValidateHintType` `0x1038e480`

_Recovered 2026-09-14, story 29d._

Slot 566's one **replacement** body — the other eleven census overrides are constants or type ranges
that story 29c-1 tabled. It reads neither `m_iGroupID` nor the range switch.

`1038e48b`: only `m_nHintType == 20000` is considered; everything else refuses at `1038e5b3`. The
body then decodes an obfuscated word at `m_field_0x6670` into a mode index:

```
EAX = ((word & 0x00710935) ^ 0x00148739)
EAX = (EAX + 0x004094ab) & 0x018ef6ca
EAX = EAX ^ word ^ 0x412a96ec
mode = FUN_1042fbf0(EAX)                  // p ^ ((((p & 0x67c8c535) ^ 0xdcb8cc14)
                                          //      + 0x18e71cec) ^ 0x82aa05e1) & 0x98373aca
                                          //   ^ 0xea3e269c
```

`1038e4c7 DEC EAX / CMP EAX,7 / JA` then indexes the jump table at `0x1038e5c0`, which holds eight
entries for modes 1..8 and sends 5, 6 and 7 to the default label. The five name templates, at their
`.rdata` addresses in the pinned image:

| mode | template | address |
|---|---|---|
| 1 | `ManBat Landpoint` — copied **raw** by a byte loop, no `sprintf`, no index | `0x10642c74` |
| 2, 4 | `ManBat Divepoint %d` | `0x10642ca8` |
| 3 | `ManBat Divepoint %d Bottom` | `0x10642c88` |
| 8 | `ManBat Script Node %d` | `0x10642c58` |
| default (0, 5, 6, 7, 9+) | `ManBat %d` | `0x10642c4c` |

The `%d` is `m_field_0x6674`, a plain int beside the scrambled word. The built name is then matched
against the hint's `m_iName` (`+0x26c`) with `__strcmpi`, or — when the template's last character is
`*` — with `__strnicmp` over `strlen - 1` characters (`1038e590 DEC ECX`), which excludes the `*`
itself. **Retail's empty-template arm is worth naming**: `1038e556`, when the built name is
zero-length, does not compare strings at all; it loads the hint's name **pointer** into `EAX` and
tests it for zero, so an unnamed hint matches an empty template and a named one does not. None of
the five templates can be empty, so the arm is unreachable through this body.

The descramble is invertible over the ladder's masked bits: the `+0x6670` words that produce modes
1..8 are `0xbb2782fb`, `0xbb2782fa`, `0xbb2782f9`, `0xbb2782fc`, `0xbb2782f7`, `0xbb2782fe`,
`0xbb2782f5` and `0xbb2782f0`, and the zero word decodes to `0xbb258278`. The suite drives the whole
body with them.

**Unrecovered:** what `+0x6670` and `+0x6674` are authored or written by. No producer in the port
sets either, so the ported body always takes whatever the default word decodes to.

### `CNPC_VWerewolf::GetHintEndEntity` `0x103d6390`

_Recovered 2026-09-14, story 29d._

A cache in front of `FindHintEndEntity` (`0x103d6520`, already ported by family Hints). It scans the
record array at `+0x6714` (count `+0x6720`, stride `0x48` = 18 ints) for the row whose word at
`+0x04` is this hint, and answers that row's word at `+0x00` — a cached `EHANDLE` to the hint's end
entity, resolved through `PTR_DAT_10566458` with the usual `index & 0x1fff` / `serial >> 0xd` check.

Two corrections. **First**, the hit arm does not answer "a second cached handle at the same slot": it
re-reads `base[i * 0x12]`, which is the *same* word `+0x00`, and re-validates it, answering null if
that second resolve fails. The redundancy is retail's and there is one handle. **Second**, this fixes
the record layout story 29c-1 recorded for `GetHintGroundpoint` (`0x103d6770`): that body starts its
cursor at `field_0x6714 + 4` (`103d6779`) and matches there too, so the hint pointer is at `+0x04`,
the cached end entity at `+0x00`, and the groundpoint at `+0x08`. `FWerewolfHintGroundpoint` carries
all three now.

A row whose cached handle does not resolve is **skipped** by the loop guard rather than answered, so
the scan keeps walking; a miss or an empty array falls through to `FindHintEndEntity`.

**Unrecovered:** the producer that fills `+0x6714`. Nothing in the port writes it, so the fallback is
the only arm reached.

### `CNPC_VWerewolf::GetHintEndpoint` `0x103d6650`

_Recovered 2026-09-14, story 29d._

A null hint answers `DAT_1070d1b0/b4/b8`, and `staticinit_101370b0` writes **zero** into all three —
it is `vec3_origin`, not "a fixed global point". Any other hint resolves its end entity through
`GetHintEndEntity` and copies that entity's `GetAbsOrigin` (vtable `+0x364`) into the caller's
`Vector`, **with no null check**: retail faults on a hint whose end entity does not resolve. The port
carries a named crash guard that answers the same `vec3_origin` the null-hint arm answers.

**Unrecovered:** nothing.

### `CNPC_VWerewolf::GetForwardHintForHint` `0x103d7090`

_Recovered 2026-09-14, story 29d._

The switch at `103d70dd` names **fourteen** hint types that are handed straight back: `15000`
(`0x3a98`), `0x3a99`, `0x3a9c`, `0x3a9f`, `0x3aa0`, `0x3aa1` and `0x3aa3 .. 0x3aaa`. **`0x3aa2` is
not among them** — a `0x3a9f .. 0x3aaa` run would be fifteen — and neither are `0x3a9a`, `0x3a9b`,
`0x3a9d` or `0x3a9e`.

Every other type resolves the input's end entity through `GetHintEndEntity` once, then walks the
global hint list from its head `DAT_10925450` through the next link at index `0x176` (`+0x5d8`),
looking for a hint whose own type is `0x3a9c` and whose own end entity is the **same object**. That
hint is the "forward" partner. On no match the loop cursor is null, and retail `DevWarning`s with the
format at `0x10662f98` — `"Could Not find forward hint for hint: %s (%s) \n"`, **two** `%s`, both
supplied by `CBaseEntity::GetDebugName` — and returns that null.

**Unrecovered:** the global hint list. The port's `GlobalHintList()` is a seam answering an empty
list, so the no-match arm is the one reached.

### `CNPC_VWerewolf::IsValidTeleportHint` `0x103d8300`

_Recovered 2026-09-14, story 29d._

Seven ordered refusals, then one negated test. In the listing's order:

1. the hint is null;
2. `field_0x66e8 & 0x4` — the same Werewolf bit word `IsImperativeTeleportHint` reads;
3. `IsHintUnusable(hint)` (`0x102d14c0`, family Hints' three-arm rule);
4. slot 566 `FValidateHintType` through `vtable +0x8d8` — **virtually**, so a Werewolf takes its own
   species row (`0x103d7ce0`, 15000..15018 except 15007) and not the Troika dispatcher;
5. the hint's type is in `0x3aa3 .. 0x3aa8`, six separate `CMP`s at `103d83c8`..`103d8430`;
6. `hint->+0x470 == 1` **and** `field_0x66e8 & 0x40`. Note the **equality** compare against 1 on the
   same word slot 566 treats as a bit set — retail's own asymmetry, reproduced.

If every gate passes, the body resolves the hint's end entity through `GetHintEndEntity` and answers
the **negation** of `thunk_FUN_100b5190(endEntity)`. `0x100b5190` is a seven-byte getter of `+0xf4`,
which `docs/vtmb/npc-kernel/fields.md` names `m_bScriptHidden` — it is the script-hidden test, not an
"entity-busy/occupied" test as the checklist walk read it. A teleport hint is valid only when its far
end is not script-hidden.

**Unrecovered:** nothing in the rule. The endpoint's `m_bScriptHidden` has no source here (there is
no entity behind a hint node), so the port's seam answers `false`, which is the **admitting** value:
retail negates it, so nothing is silently refused.

### `CNPC_VVampireBoss::SelectHintNode` `0x103c59d0` — read as `present`

_Recovered 2026-09-14, story 29d._

Read again against `0x10365780`, the address the port's `FElysiumNpc::FindHintNode`
(`ElysiumNpcKernelHints.cpp`) cites. The two retail bodies are identical arm for arm:
`FindHintNear(hintType, flags, 5000.0, null, null)` through `0x102d1af0`, the result written to
`m_pHintNode` (`+0x5ddc`) on **both** paths, a hit calling `0x10273e80(this, 0)`
(`TaskComplete(false)`) and returning true, a miss writing an assert file string and line into
`+0x1b44` / `+0x1b48` and then `vtable +0x700 (4)` (`TaskFail(4)`) and returning false.

The difference is **not only the line number**, as the checklist recorded: the file string differs
too — `0x1065ec20` `"E:\Vampire\main\dlls\hl2_dll\npc_VVampireBoss.cpp"` line `0xd1` here against
`0x1062eadc` `"E:\Vampire\main\dlls\hl2_dll\NPC_VBach.cpp"` line `0x48a` there. Both halves are the
same deliberately unmodelled assert pair: `ElysiumNpcKernelShapeMap.cpp` records `+0x1b44`
**ABSENT** port-wide, a decision already accepted for the sibling body. Retail keeps two functions
where the port has one method and both dispatch to the same observable behaviour. **`present`
stands.**

**Unrecovered:** nothing.

## Story 29e, family State19 — `SetState` `0x1026e340`, `SelectIdealState` `0x1026f660` / `0x102ad660` (2026-09-14)

`CAI_BaseNPC::SetState` (`0x1026e340`) is not a vtable slot. It reads `m_NPCState` (`+0x5cc0`) at
entry; when the target differs it stamps `m_flLastStateChangeTime` (`+0x5cc8`) from
`gpGlobals->curtime`; when the target is 1 IDLE and slot 167 `GetEnemy` is live it calls
`SetEnemy(NULL)` (`0x10279a50`) and `DevMsg(2, "Stripped")`. Both `+0x5cc0` and `+0x5cc4` then take
the target. Slot 463 is dispatched only when the **re-read** of `+0x5cc0` after the strip differs
from the target, and the arguments are `(oldAtEntry, new)` — the listing at `1026e37e` re-reads the
word, the dispatch still uses the value saved at entry.

`CAI_BaseNPCTroika::SelectIdealState` (`0x102ad660`) writes `+0x1b38 = 2` and switches on
`m_NPCState`. Every arm but the four flee arms (`0x21` / `0x1f` in idle and alert) and case `0xe`'s
three damage arms uses `HasInterruptCondition` (`0x10269d30`), which answers 0 when `+0x5c38` holds
no schedule. A committed enemy with no program therefore answers **nothing**. The flee arms use
bare `HasCondition`, skip `m_bNoAlertState`, and omit the `m_NPCState != 8` guard the
`0x102ad260` / `0x102ad2d0` pair apply. Fallthrough is `CAI_BaseNPC::SelectIdealState`
(`0x1026f660`), whose case 1 also uses the interrupt form.

`CNPC_VHuman::SelectIdealState` (`0x103851e0`): the hunt arm is `IsCommand() == false &&
DAT_1092447c[0xb] != 0`, not a `GetBool`. Its no-enemy ladder is also reached **with a live enemy**
whenever interrupt `LOST_ENEMY 0x47` stands. `CNPC_VSabbatLeader::SelectIdealState` (`0x103a7450`)
gates on `m_bActivated`; `m_NPCState == 2` is COMBAT. The twelve fifteen-byte slot-461 bodies write
`SelectIdealStateSelector` (`+0x1b38`) and chain to `0x103851e0` or `0x10387380`.

The base's hear arms read slot 474 `GetBestSound`, which on the Troika line (`0x102b4520`) is
`return &m_BestSound` and is therefore **never null** — the null arm at `1026f77a` is retail's own
dead branch for every `npc_V*` class. The type word is `CSound +0x4`, and the base admits only 1, 8
and 0x10 where `CNPC_VTzimisce` (`0x103bd690`, `103bd8ba`) additionally admits **4**. Case 3's hear
arm ends in an unconditional `thunk_FUN_101e3d70(&DAT_10739a4c, this)` (`1026f91b`) — the
discipline manager's break-on-notice sweep, whose only other caller is `SetEnemy` (`0x10279a50`).

**The four species ladders that are not a prologue.** `CNPC_VGuard1` (`0x1037d290`) runs four law
arms per state (`0x21`/`0x1f` → FLEE, `0x22`/`0x20` → COMBAT), each gated on the closest player
BEING that channel's offender, each latching `0x1037e2d0` and dispatching slot 596; `INVESTIGATE_LEVEL
0x1e` promotes to the guard's own state `0xc`, whose ladder ends in an unconditional IDLE that never
reaches the chain; state `0xb` leaves on **interrupt** `SEE_PLAYER 0x5a` only while `m_fHatesPlayer`
stands. `CNPC_VCop` (`0x103726c0`) runs the pre-pass `FUN_103723f0` from idle and alert — seven
tests, every one the BARE form, clearing `m_bCondTookDamage` whenever damage stands regardless of
the attacker — and writes its answer as the ideal state; its state `0xc` carries the same four law
conditions with **no** offender compare. `CNPC_VPedestrian` (`0x103a2e30`) answers FLEE from every
arm it takes: three damage interrupts, a combat-sound ladder against the closest player and then
against `m_hLastHeardEnt`, and a bullet-impact tail, with `_DAT_10483aac` = **512.0** and
`_DAT_1049dfe4` = **262144.0** (= 512²) read from the pinned image. `CNPC_VTzimisce` (`0x103bd690`)
is the base ladder with HUNT `0xb` where the base answers ALERT, `SEE_UNKNOWN` through slot 586
`GetBestSeeUnknown`, and `ENEMY_DEAD 0x58` joining "no enemy" on the combat fallback.

`CNPC_VDog` (`0x103743c0`) state 1's `SEE_UNKNOWN` and `PLAYER_SNARL_RANGE` arms **tail-jump to
`CNPC_VAnimal`** (`10374602`, `103746d3`), not to Troika, and the snarl arm additionally snapshots
`+0x667c` into `+0x6680` and stamps `+0x6678` with `curtime`. Its state 3 carries five guards that
all land on `FUN_103747c0(1)` (whose whole body is `return 0`, so each means "go to `CNPC_VAnimal`
now") ahead of three arms that do write: `DOG_COMBAT_LATCH 0x78` → COMBAT and
`DOG_IDLE_FROM_ALERT 0x7c` / `0x7d` → IDLE.

`CNPC_VCop::OnStateChange` (`0x10371c20`) releases the pursuit through `0x1017f6e0`, which
DECREMENTS the pursued player's `+0x1d10` and on the zero crossing runs `0x10370630` and
`BeginHeightenedAlert` — the port's `CopSlot597Prologue` is the matching increment. Both of its
tails chain `CNPC_VHumanCombatant::OnStateChange` (`0x103871c0`) with `(old, new)`, whose weapon
half is unconditional: new 1 hides the active weapon (`+0x108`), new 2 / 3 / `0xb` unhides it
(`+0x10c`).

**Unrecovered:** slot 495 (`vtable +0x7bc`) on the idle combat roll; `0x1027d0a0`'s cine-release
body; `0x101e3d70`'s discipline sweep (no port accessor for "which disciplines break on notice");
`CNPC_VDog`'s `+0x667c` source word (the class carries no datamap and nothing reads the offset);
the crime level of the player's active weapon at `weaponData +0x3c4` (`0x102517e0`), which
`CNPC_VPedestrian`'s two gunshot arms report. **Not built:** retail states 8, 0xb, 0xe have no
`EElysiumNpcState` member; the raw id is stored beside the typed word. The `m_SelectIdealStateTrace`
`__FILE__`/`__LINE__` pair (`+0x1b3c`/`+0x1b40`) is ABSENT in the shape map; only the selector tag
`+0x1b38` is stood.
