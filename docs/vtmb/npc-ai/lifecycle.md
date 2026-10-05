# NPC AI — Lifecycle: spawn, activation, the think

Part of the [NPC AI oracle](./README.md). Sections moved verbatim from
`npc-ai-reverse-engineering.md` on 2026-09-13; their headers are the citation keys.

## Native NPC object and state

The recovered `CAI_BaseNPC` datamap is rooted at `0x105c9814`. Important recovered state includes:

| Offset | Field | Function in the recovered loop |
|---:|---|---|
| `0x1a40` | `m_bShouldMove` | Movement intent/gate |
| `0x1a94` | `m_hLastEnemy` | Previous enemy handle |
| `0x1b28` | force-state-change storage | Forces reconsideration of state |
| `0x5b68`-`0x5b74` | last seen hate/fear/dislike/nemesis handles | Relationship-category memory |
| `0x5b78` | last-heard storage | Auditory memory |
| `0x5b7c`, `0x5b80` | last damage and took-damage state | Damage stimulus memory |
| `0x5c3c` | `m_IdealSchedule` | Requested schedule identity |
| `0x5c40` | `m_ScheduleState` | Active schedule execution state |
| `0x5c54` | fail schedule | Recovery path when a task/schedule fails |
| `0x5ca4` | conditions-gathered state | Prevents inconsistent re-gather within a decision pass |
| `0x5cc0` | `m_NPCState` | Current high-level state |
| `0x5cc4` | `m_IdealNPCState` | Desired high-level state |
| `0x5cdc` | senses component | Sensory gathering |
| `0x5ce0` | enemy handle | Current enemy |
| `0x5ce4` | target handle | Current non-enemy target/goal |
| `0x5cec` | capabilities | What movement/combat actions the NPC may perform |
| `0x5d34` | navigator | Route and movement-goal ownership |
| `0x5d3c` | pathfinder | Path construction |
| `0x5d44` | motor | Physical facing and locomotion execution |

The object also stores delayed conditions, sound conditions, enemy-occlusion state, ideal
sequence/activity/weapon activity, and the last state-change time. This separation matters: an
enemy handle, an alert/combat state, a schedule, a movement goal, and a playing animation are
related but not interchangeable pieces of state.

### `TeleportToEntity` and the next AI admission

`TeleportToEntity` is a `FIELD_EHANDLE` input on `CAI_BaseNPCTroika`, not on the base
`CAI_BaseNPC` map. Its full name conversion and transform contract is owned by
`docs/vtmb/entity_io.md`. The AI-specific tail is virtual slot `+0x998`. All three concrete
receiver families in the current corpus resolve that slot through thunk `0x10010f0f` to
`FUN_102c23f0`: Jack's `CNPC_VVampire`, `CNPC_VHumanCombatant` (vtable `0x104b7ff4`, slot 614),
and `CNPC_VHunter` (vtable `0x104b9784`, slot 614). The function assigns the current game time to
exactly five deadlines:

| Offset | Recovered field | Write |
|---:|---|---|
| `0x17c` | `m_flNextThink` | `curtime` |
| `0x6244` | `m_flNextUpdateThink` | `curtime` |
| `0x6248` | `m_flNextNormalThink` | `curtime` |
| `0x624c` | `m_flNextMoveThink` | `curtime` |
| `0x6250` | `m_flNextAIThink` | `curtime` |

These are scheduling writes, not an AI reset. The handler does not touch the navigator at `+0x5d34`,
the active or ideal schedule, movement goal, enemy/target handles, NPC state, conditions, activity,
sequence, velocity, or the authored `teleport_move_timer` field at `+0x65dc`. For an ordinary queued
map output, `GameFrame` has already completed its think phase before the teleport input is serviced,
so the newly due work begins on the next server frame. The input also forces network transmission for
one second, which makes the discontinuous placement observable without changing decision state.

### Map creation, spawn, activation, and first AI admission

RE47 pins the general bootstrap to the retail DLL instead of borrowing the similar Source SDK
implementation. For a normal fresh map, the recovered order is:

1. `CServerGameDLL::LevelInit` (`0x1011a7a0`) enters the map-entity loader. The loader reads one
   block, resolves its `classname`, constructs the registered native class, and sends the complete
   block through the entity's map-data/keyvalue path (`0x10136b40`).
2. Unparented entities enter `DispatchSpawn` (`0x101d1280`) immediately. Parented entities are
   collected, hierarchy-sorted, attached, and spawned afterward. `DispatchSpawn` invokes virtual
   slot `+0x19c`, rejects an entity deleted or marked for deletion by `Spawn`, then completes its
   post-spawn bookkeeping.
3. A `npc_VVampire` uses `CNPC_VVampire::Spawn` at `0x103c4ef0`. Its recovered chain enters the
   Human/Troika spawn body at `0x10298d30`, which performs model, hull/solid, capability, equipment,
   cached-transform and Troika state setup around `CAI_BaseNPC::Spawn` at `0x10273200`. The base
   spawn performs its AI-admission gate, optional equipment path and base combat-character spawn.
4. Only after creation/spawn finishes does `ServerActivate` (`0x1011aaf0`) iterate every surviving
   server entity and invoke virtual `Activate` (`+0x1c4`) before post-entity systems run. Spawn and
   activation are therefore distinct passes; BSP entity order is not permission to interleave one
   actor's activation with the next actor's keyvalue load. The walk is `NextEnt` (`0x100f7060`)
   over the active list; it re-reads each node's next link, and `CBaseEntityList::AddEntityAtSlot`
   (`0x100f9fc0`) appends at the tail, so an entity created by an earlier entity's `Activate` is
   activated later in the same pass, after all entities already listed. The one skip is
   `m_iEFlags (+0x268) & EFL_DORMANT (0x2)` (`0x100a8220`). Only `CBaseEntity::MakeDormant`
   (`0x100a8060`) sets that bit: `DispatchSpawn` (`0x101d1280`) and the two restores
   (`0x101a2e40`, `0x101a3c40`) for an entity whose `globalname` belongs to another map (no shipped
   map authors one), and `camera_animated`'s `EndCamera` (`0x10071660`); ScriptHide/StartHidden
   never do, so a hidden entity is activated. `EFL_KILLME` is not tested: the map's removals are
   purged before the pass (it prints "ERROR: Entity delete queue not empty" otherwise), and an
   entity `UTIL_Remove`d during the pass is still listed and still activated
   (2026-09-25, 0019 story 5 step 2 review).
5. The NPC initialization think at `0x10273aa0` first applies an authored relationship override
   when present, then invokes virtual `+0x698` followed by `+0x694`. The `CAI_BaseNPC` `+0x698`
   body (`0x10273ad0`) performs readiness work: clears initialization flags, repairs ground
   placement where applicable, resolves an optional target, installs the ordinary AI think, and
   applies target/spawnflag-driven state/schedule changes. `CNPC_VVampire` inherits the Troika
   override at `0x1029a8b0`: it calls that base body, reinstalls the AI think, resolves follower
   activity/distance data, and records the closest player handle. It does not turn toward that
   player. The base `+0x694` body at `0x101a6540` is empty.
6. Ordinary `RunAI`, condition gathering, state/schedule selection, motor work and activity
   maintenance occur only after that admission path.

The recovered concrete `npc_VVampire` path contains no turn-to-player operation and no direct call
to `SetActivity`/`SetIdealActivity` during the traced Spawn → Activate → concrete `NPCInitThink`
chain.
This is a negative call-path result, not proof that the character remains static: the subsequent
AI schedule, motor, interesting-place, gaze, dialogue and scripted-scene owners can all change
facing or presentation. Static recovery also does not identify the exact sequence visible on the
first rendered frame or the exact frame of the first ordinary AI schedule. Those require a
hash-gated live capture; a rebuild must not fill the gap by inventing an unconditional idle or
face-player action.

### AI update loop

`CAI_BaseNPC::RunAI` at `0x1026f110` begins a decision pass by clearing the gathered marker. Unless
a script owner prevents native gathering, it calls the virtual condition-gathering path, updates
state/decision policy, maintains or runs the schedule through the path at `0x102817c0`, and clears
transient conditions at the end of the pass. The base `GatherConditions` path at `0x1026ec30`
integrates senses, sound, damage, and current state into condition flags.

Schedule selection is not a direct switch from one stimulus to one animation. The wrapper at
`0x102814d0` ensures conditions are gathered, invokes the selector, resolves global schedule IDs
through the owning schedule space, and stores the ideal schedule. The active schedule then runs
its tasks over subsequent AI updates until it completes, fails, or an interrupt condition forces
reselection.

The base selector at `0x1028a380` switches on the high-level state. Recovered cases are:

| Case | State |
|---:|---|
| 0 | none/uninitialized; warns rather than behaving normally |
| 1 | idle |
| 2 | combat |
| 3 | alert |
| 4 | scripted |
| 6 | prone |
| 7 | dead |
| 12 | class-specific/fallback idle-like branch; exact semantic label unresolved |

The numbering is directly constrained twice: `CAI_BaseNPC::SelectIdealState` (`0x1026f660`) emits
`Combat state with no enemy` from case 2 and falls back to state 3, while the concrete human
selectors enter their weapon/combat policy only for state 2. The combat branch further tests enemy
state, damage, attack capability, range/occlusion, and other conditions before choosing a
schedule. Derived VtMB NPC classes override or extend the selection and task machinery, which is
why the native class is a load-bearing part of authored NPC identity.

## The think cadence, decoded (2026-09-08)

All eight stamps are `CAI_BaseNPCTroika`-only: `m_flNextUpdateThink +0x6244`, `NextNormal
+0x6248`, `NextMove +0x624c`, `NextAI +0x6250`, and `m_flLast{Update,Normal,Move,AI}Think`
`+0x6254..+0x6260`. Inputs: `m_flPlayerDist +0x6264`, `m_hClosestPlayer +0x628c`,
`m_bInPlayerPVS +0x6278`, `m_bInPlayerLOS +0x6279`, `m_flLastInPlayerLOS +0x6280`,
`m_flNextPlayerLOS +0x6284`. Names from VProf literals: `CalcNextUpdateThink 0x10290720`,
`CalcNextNormalThink 0x10290b60`, `CalcNextMoveThink 0x10290fc0`, `CalcNextAIThink 0x10291230`,
`SetClosestPlayer 0x10293a80`, `SetPlayerLOS 0x10291610`, `UpdateCharacter 0x10298070` (slot
312). `0x1029bd40` is the `ai` debug distance overlay, not a writer.

**Due test** `IsThinkDue(stamp)` `0x10290660`: `(stamp − curtime) ≤ frametime` (equality is due;
`FCOMP` + `TEST AH,0x41`). Its four per-stamp thunks are update `0x102906a0`, normal `0x102906c0`,
move `0x102906e0` (`return true`) and AI `0x10290700`.

**The three gated laws are SELF-GATING** (2026-09-12). `CalcNextUpdateThink` opens with
`IsThinkDue(NextUpdate)` and returns without writing anything when it is false; the normal and AI
laws do the same against their own stamps. `CalcNextMoveThink` alone has no gate. This is
load-bearing rather than an optimisation: `m_flNextThink` is `min(NextUpdate, NextNormal)` and the
update law floors at 0.03 s against the normal law's 0.1 s, so most wakeups are the update clock's
alone — and without the self-gate each of those would push the normal and AI stamps forward too,
and the NPC would stop gathering conditions entirely.

**Writers.** `NPCInit` `0x1029a0b0` sets all eight to `curtime` and seeds `PVS = LOS = 1`
(a fresh NPC is due on every clock). The four `Calc*`. **`TaskFail` `0x1029adb0`** sets the four
`Next` stamps to `curtime` (not the `Last` ones): a failure forces a full think next frame.
**`0x102c23f0` = slot 614 (`+0x998`) `ResetThinkTimers()`**, 39 bytes and nothing else: `FLD
curtime; FST m_flNextThink +0x17c; FST +0x6244; FST +0x6248; FST +0x624c; FSTP +0x6250` — the four
`Next` plus the engine's own next-think := curtime, so the effect takes hold on the same frame. It
has no direct caller and is reached only through the slot. Two `CAI_BaseNPCTroika` virtuals wrap
it. **Slot 583 `+0x91c` = `0x1028d860`**: `dist²(GetAbsOrigin(), point) <= 2048²`
(`_DAT_1049adfc` = `4194304.0`; `FCOMP` + `TEST AH,0x41` + `JP`, so equality resets) → slot 614.
**Slot 584 `+0x920` = `0x1028d910`**: slot 614, then `m_flLastThink` and
`m_flLast{Update,Normal,Move,AI}Think` := `curtime` — the only writer of the `Last` stamps outside
`NPCInit` and the four laws. Neither is ever dispatched on one NPC: `0x1028d820(point)` walks the
entity list `&DAT_106eb5d8` (filter `0x40`) and fires slot 583 on every entity's Troika pointer
`+0x98`, and `0x1028d8d0()` does the same for slot 584. `0x1028d820`'s callers are
`CBasePlayer::Teleport` `0x101606a0` (slot 181, after `CBaseEntity::Teleport`, only when a
destination was given), the `teleport_player` console command `0x101803a0` (all three arms — by
entity name, by `X Y Z`, by `X Y Z` plus angles) and `CPointTeleport::InputTeleport` `0x1018dc00`:
a teleport wakes every NPC within 2048 units of the destination. `0x1028d8d0`'s one caller is
`0x102f6a50`, the node-graph rebuild think (`"Node Graph out of Date. Rebuilding..."`) — every NPC
in the map is re-based once the graph is valid again. Subclass writers:
`CNPC_VNewscaster::vfunc431` `0x103a05b0` is the newscaster's think, and before calling
`CAI_BaseNPCTroika::NPCThink` it pins `+0x624c` and `+0x6250` to `curtime + 1.0` and their `Last`
stamps to `curtime` — a hard 1 s floor on move and AI (the arithmetic previously UNRECOVERED).
`CNPC_VCamera` `0x103692c0` is that class's `NPCInit` override, the same all-eight := `curtime`;
`CNPC_VCamera` `0x10369120` is its think and runs none of the laws — `CacheInterruptConditions`,
the AI console gate `0x1026c3d0` (refusal → `m_flNextThink = curtime + 0.1`), `RunAI(0)` (slot
432), then `Last := Next` for all four and all four `Next` plus `m_flNextThink` := `curtime + 0.2`.
That is the whole ledger: `vtmb_grep` on the literals `0x6244|0x6248|0x624c|0x6250` returns exactly
`NPCInit`, `TaskFail`, `NPCThink`, the four laws with their `IsThinkDue` thunks, `0x102c23f0` and
`0x103a05b0`, and nothing else. `vtmb_readers +0x6244 cls CAI_BaseNPCTroika` is *not* that ledger —
it misses `0x102c23f0` (ECX-relative, untyped) and every subclass-typed access (`0x103a05b0`,
`0x10369120`, `0x103692c0`); ask for the subclass by name, or grep the literal. `m_flNextThink
+0x17c` is a `CBaseEntity` field with engine-wide writers (`ThinkSet`); inside this hierarchy they
are `NPCThink`, `0x102c23f0`, `CNPC_VCamera` `0x10369120`, and the post-spawn pass `0x102998c0`
(`ThinkSet(0)` + `m_flNextThink = 0x7f7fffff` when `0x100b5190` parks the NPC). The `0x102c1ec0`
row `vtmb_readers +0x17c` returns is not a write at all — it is the vtable dispatch at `+0x17c` in
that function.

**Slot 614 dispatch sites (2026-09-12).** `vtmb_grep` `\+ 0x998\)\)` over `vampire.dll` gives 53
functions, 19 of them ILT/COMDAT thunk copies: **34 distinct sites**, and every one of them is a
moment where the NPC's whole clock is re-based so the work queued alongside it runs on the same
frame. Grouped by owner, with the receiver:
- *Species thinks that never sleep* (receiver `this`, at the tail of slot 431 `NPCThink`).
  `CNPC_VWolfMorph` / `CNPC_VPlayerController` `0x103a4700` — 19 bytes: `CALL` the base
  `NPCThink`, then `JMP dword ptr [EAX + 0x998]`; both classes think every frame forever.
  `CNPC_VWerewolf::NPCThink` `0x103cb590` has two: the same tail at `0x103cb71c`, and an early-out
  at `0x103cb644` that resets and returns *before* the base think, behind an unnamed ConVar-shaped
  global `0x1093f95c` (the `vfunc1()` false and `+0x2c` non-null idiom used for debug cvars
  elsewhere) — that arm only draws hint debug (`0x103cb4b0` walks the hint list from `DAT_10925450`
  and switches on the cvar's value 1–6 over the werewolf's `Is{Imperative,Valid}*Hint` predicates),
  so the whole real think is skipped and the cadence stamped anyway. The tail is reached by every
  other path including the `ai_disable` one: the decompiler's damage is the `GetTick() % 5` jump
  table at `0x103cb754`, whose five arms (`UpdateConditionCanTeleport`, `…EnemyUnreachable`,
  `…DeathTriggered`, `…CanSpecialMove`, `CheckStuck(1)` — one expensive condition per frame) all
  rejoin at `0x103cb6cf`, not return. `CNPC_VZombie::NPCThink` `0x103dfa20`: after the base think,
  `FLD [ESI+0x17c]` →
  `__ftol` → `TEST EAX,EAX` / `JG`, so `(int)m_flNextThink <= 0` → reset — a guard against a
  cleared clock. `CNPC_VZombie::vfunc301` `0x103dfbb0`, the death hook: not gibbing, not
  ragdolling, and a weighted sequence exists for activity `0x21` → `SetSchedule(0x162, true)`,
  `ThinkSet`, reset.
- *Entity inputs* (receiver `this`; all the same recipe — reset, stamp `AI_BaseNPCTroika.cpp`,
  `SetSchedule`). `InputStartPlayerDialog` `0x1029ef80` (line `0x2677`): `FinishTalking`, reset,
  `m_bForceDialogStart = 1`, `0x102ae750(0x6d, 0)`. `InputStartPlayerDialogRemote` `0x1029f060`
  (line `0x268e`): the same with `0x102ae750(0x6e, 0)` (`PUSH 0x6e` at `0x1029f0c7`).
  `InputStartPlayerDialogUnforced` `0x1029f120` (line `0x26a7`): `m_bForceDialogStart = 0`,
  `0x102ae750(0x6d, 0)`. **[corrected 2026-10-04, V3r]** this item said all three set
  `m_bForceDialogStart = 1` and install `0x6d`; the listing shows Remote installs `0x6e` and
  Unforced clears the byte. The full walk is `game_runtime.md` § Runtime / branching. `InputFleeAndDie`
  `0x1029f210` (line `0x26b5`) → `SetSchedule(0x6f)`. `InputFaint` `0x1029f250` (line `0x26c2`) →
  `SetSchedule(0xfa)`. `InputTeleportToEntity` `0x102c24a0`: origin and angles from the target,
  `Relink`, reset, `ForceTransmit`.
- *Troika NPC methods* (receiver `this`). `SetDisableAI(bool)` `0x1029f300` — on the `1 → 0` edge
  only, then `m_bDisableAI +0x6080 = value`. Knockback start `0x102a01b0` (line `0x29b5`): gated on
  `0x1028a190` and `!IsInDialog`, `SelectHeaviestSequence`, `m_knockbackType`, reset, then
  `SetSchedule(0x14d)` or `(0x14c)`. `RunTask` `0x102aacf0`, task **`5` `TASK_WAIT_PVS`** (name
  registered at `0x10317086`; the arm is jump-table target 2 at `0x102aad7e`, index-table byte 3 →
  id `3 + 2`): spawnflag `0x400` or `ShouldThinkFrequently()` → `TaskComplete(false)` at once; else
  the engine PVS test `0x101d1a90(m_hClosestPlayer, this)` — false leaves the task running, true
  dispatches slot 614 at `0x102aadde` and then stamps `m_flLastThink` and all four `Last` (the
  slot-584 body inlined) before `TaskComplete(false)`, so an NPC the player walks up on does not
  fire a burst of overdue thinks on the frame it wakes. `LeaveGrappleState` `0x102b5d90` (slot 380,
  19 bytes): base `0x1026ce30` then `JMP` slot 614; reached from `CBaseCombatCharacter::EndGrapple`
  `0x10329560`. The spoken-line player `0x102c0520` (`m_bIsTalking +0x64c0`, talk-end `+0x64cc`):
  reset at the end. `ScriptUnhide` `0x102c1ec0`: `CBaseEntity::ScriptUnhide`, reset, re-arm the
  active weapon, then save solid/movetype/effects/`m_bfAINPCFlags` back into `m_hCine` at
  `+0x5f78..+0x5f8c`. **`0x102c51a0` is `DoPossession`** (line `0x6e42`), the discipline arm named
  by its own vdata key: `0x101dfa00` parses the per-level record with
  `GetBool("DoPossession") → +0x370` and `GetBool("DoFrenzy") → +0x371`, and `0x101dfc20` dispatches
  `0x102c51a0` or `0x102c5310` on `victim + 0x98`. It tears the victim down and rebuilds it as a
  thrall — misc flag `0x800`, `0x1026d050`, `0x102b52a0(1,1)`, `flags2 |= 0x80840000`,
  `SetRelationship("player D_LI 99")`, follower boss `"player"` / type `"Combat"`,
  `SetIdealState(1)` `0x1026e340`, `SetEnemy` `0x10279cc0`, caster handle → `+0x60ac` — then
  resets, so the new brain re-evaluates on the next tick instead of honouring the timers of the
  schedule it just destroyed; `+0x5b84 = 0x3b1c`, slot 595. Its sibling **`DoFrenzy` `0x102c5310`
  (line `0x6e7f`) runs the same teardown and never resets**, so a frenzy waits for the cadence.
- *On another entity's Troika pointer `+0x98`.* `CBaseCombatCharacter::FeedInterrupt` `0x1033a9e0`
  — the victim, before `SetSchedule(0xfb)`. `CBasePlayer::PlayerUse` `0x10167850` — the used
  entity's NPC when slot 287 (`+0x49c`) allows; stamps `player.cpp` line `0x1508` then
  `SetSchedule(0x6a)`. The discipline-effect applier `0x101de660` (`v_discipline.cpp`) — the
  victim, when the effect record names a schedule at `+0x34`: reset, then `0x1029f370` (schedule by
  name) → `0x102ae750`. *(This is the site the oracle previously called "HitGroup apply"; it is
  not.)* `0x100847e0` — walks a name list, resolves each through slot `0x42c`, `SetDisableAI`, then
  reset, `PhysicsRunThink`, and repairs `m_flNextThink` when it has fallen to or below
  `m_flLastThink`; recurses into child lists of type `0xb`. `CAI_InterestingPlace` `0x102daac0`
  (`AI_Interest*.cpp` line `0x309`) — every registered visitor: `DISAPPEAR 0x20000000` →
  `flags2 |= 0x80000008`, else `TaskFail(0x23)` (slot 448); the reset runs on **both** arms.
- *Bodies created and immediately clocked* (receiver is the new entity's Troika pointer).
  `GetControllerNPC(classname)` `0x10161a70` and the lazy `npc_VPlaceholder` body `0x10161d20`,
  both in `player.cpp`: create, copy origin/angles/model/hull from the owner, `m_fEffects |= 0x60`,
  reset. The Hengeyokai transformation `0x103831c0` (`npc_VHengeyokai`, `SetSchedule(0x16f)`), the
  Ming Xiao transformation `0x1039a750` (`npc_VMingXiao`, `SetSchedule(0x157)`, two transform
  emitters) and `CNPC_VVampireBoss::TransformationStart` `0x103c60a0` — the same recipe. The Ming
  Xiao tentacle spawn `0x10397410` (from `0x10395750`): `SetSchedule(0x16c)`, `IsAreaClear`, create
  from the `TentacleGenerator` pool `&DAT_1093bb9c` (registered `0x10390d00`), `SetAbsVelocity`,
  reset, handle → `+0x66a8[i]`. The tentacle → proxy hand-off `0x10397c70` (from `0x1039ef10`):
  create from the `ProxyGenerator` pool `&DAT_1093bc54` (`0x10390d80`) at the tentacle's origin,
  reset, then remove the tentacle. The `scripted_sequence` possess path `CCineNPC::vfunc583`
  `0x101a7880` and `CCineAI::vfunc583` `0x101a9080` — the target's Troika pointer, saving its
  `+0x52e` flags.
- *Broadcasts.* The two slot 583/584 helpers above, plus the console `SetAIEnabled(true)`
  `0x10265680` (`"AI Enabled.\n"` / `"AI Already Enabled.\n"` / `"AI Disabled.\n"` /
  `"AI Already Disabled.\n"`): on the re-enable edge it clears bit 0 of the AI mask `0x1092053c` —
  the same mask the AI gate `0x1026c3d0` reads — then walks `&DAT_106eb5d8` (list selector `0x20`,
  against `0x40` for the teleport broadcast) and, for every entity with a Troika pointer,
  dispatches slot 614 and stamps `m_flLastThink +0x178` and `+0x6254..+0x6260` := curtime (the
  slot-584 body inlined a third time); entities without one get `m_flNextThink = curtime − 0.1`
  instead. Disabling AI only sets the bit and touches no clock. Its callers matter: `"AI Enabled"`
  is not only a console word — `CWorldEvents::InputAIEnable` `0x1023e290`,
  `CBaseCombatCharacter::FeedBegin` `0x10339d90` (disables) and
  `CBaseCombatCharacter::FeedInterrupt` `0x1033a9e0` (re-enables) drive it, so a feed both freezes
  and, on interrupt, re-bases the clock of **every** NPC in the map — on top of the single-victim
  reset `FeedInterrupt` already does through `+0x98`.

**Negatives.** `aiscripted_schedule` never resets. `CCineAISchedule::vfunc586` `0x101a98c0` (its
StartSchedule) → `SetIdealState` `0x1026e340`, then mode 1/2 `ScheduledMoveToGoalEntity`
`0x102800c0`, mode 3 `SetEnemy` + `SetCondition(0x54)`, mode 4/5 `ScheduledFollowPath`
`0x102801e0`; all of them land in `SetSchedule` `0x10280de0` → `0x10280e50`, which writes
conditions and schedule fields only. `ForceScheduleChange` `0x102ae490` writes no stamp either. So
a schedule installed from outside a think waits for the cadence — only the 34 sites above cut the
wait.

**The corpus limit that hid this.** `vtmb_callers 0x102c23f0` reports 0 callers and `vtmb_slot 614`
finds no vtable, because the corpus captured vtables only to slot 600; the sites come from
`vtmb_grep` on the dispatch text instead. The same index caveat runs the other way at slots
583/584: `+0x91c` and `+0x920` also carry `PossessEntity()` and
`StartSequence(pTarget, iszSeq, bCompleteOnEmpty)` on the `CCine*` script entities
(`CCineNPC::InputMoveToPosition` `0x101a72b0`, `CCineNPC::InputBeginSequence` `0x101a7390`, the
script's NPC-acquisition think `0x101a8070`, the sequence-done path `0x101a8460`/`0x101a8640`) and
the expresser factory on `CAI_BaseNPC` (`0x10312cd0`, storing into `+0x5f48`; base bodies
`0x10312d40` and `0x10260dc0`). Those are POSSIBLE callers of `0x1028d860`/`0x1028d910` in
`vtmb_callers` and reach neither: the only receivers proven to be Troika NPCs are the `+0x98`
dereferences inside `0x1028d820` and `0x1028d8d0`.

**UNRECOVERED.** The two ConVar-shaped globals the werewolf think reads have no captured
constructor, so their *names* are unknown even though their roles are not: `0x1093f95c` is the
hint-draw selector (values 1–6) behind the early-out, and `0x1093f73c` gates `EnableDebugStuff`
`0x103dbad0` and is also read by `CNPC_VWerewolf::TeleportOut` `0x103d4a60`, `TeleportIn`
`0x103d4d60` and `UpdateFakeHull` `0x103d93b0`. `CNPC_VWerewolf` slot `+0x960` (index 600), called
with `GetEnemy()` just before the tail reset, is past the corpus's vtable cut and has no name. The
meaning of the entity-list selector (`0x20` vs `0x40`) passed to the walker at `&DAT_106eb5d8`, the
value `0x3b1c` written to `+0x5b84` by `DoPossession`, and the identity of `0x100847e0`'s owner (a
name-list container walked through `0x1007a2a0`/`0x1007a2c0`, recursing on element type `0xb`) are
likewise not yet named.

**The interval laws** (constants from the DLL; epilogue for each: `*out = Next − Last; Last =
Next; Next = max(Next, curtime) + i`):
- Update: no closest player → `0.03`; else `v = (m_flPlayerDist − 512) / 704`, `v > 8 → 8 +
  RandomFloat(0, 0.8)`, `v < 0.03 → 0.03`, `v == 8 → v + RandomFloat(0, 0.8)`; then `!PVS → ×10
  cap 16`, `!LOS → ×5 cap 12`; `ShouldThinkFrequently()` → `0.03`.
- Normal: `ShouldThinkFrequently()` → `0.01`; `frenzied & 0x8` or `flags2 & 0x4
  SCHEDULE_CHANGED` or `LOS` → `0.1` (no PVS scaling); else no player → `0.1`, else `v =
  (dist − 2048) × 3/4096`, `v > 3 → 3 + RandomFloat(0, 0.3)`, `v < 0.1 → 0.1`; `!PVS → ×10 cap 16`,
  `!LOS → ×3 cap 6`.
- AI: `frenzied & 0x8` or `SCHEDULE_CHANGED` or `LOS` or no player → `0.1`; else `v = (dist −
  512) / 896`, `v > 4 → 4 + RandomFloat(0, 0.4)`, `v < 0.1 → 0.1`. No PVS scaling, no
  `ShouldThinkFrequently`.
- Move: `curtime + 0.001`, always.
`ShouldThinkFrequently()` `0x102c2430` = `IsInDialog()` (`0x102c1170`: `m_bIsTalking +0x64c0`,
`m_szDialogQue[0] +0x64ec`, `m_hDialogPartner`, handle `+0x6554`) ∥ `m_scriptState +0x5d70 ∈
{4,5,6}` ∥ (`curtime > m_flTeleportMoveTimer +0x65dc ? m_bForceFrequentThink +0x63f0 : true`).
Note the asymmetry: out of LOS lengthens the update think, while being in LOS pins normal and AI
to 0.1 s. `m_NPCState` enters none of the laws; state changes the rate only through LOS,
`SCHEDULE_CHANGED` (set by `SetSchedule`, cleared at the top of every think), `TaskFail`, and
dialogue/script. There are no `ai_think_*` cadence cvars.

**`SetClosestPlayer`**: nearest player by 3-D distance, seed `20000.0`, → `m_hClosestPlayer`,
`m_flPlayerDist`. **`SetPlayerLOS`**: forced `PVS = LOS = 1` when `m_bfNPCStateFlags & 0x8`
(UNRECOVERED name), `frenzied & 0x8`, or no player; else at most every 2.0 s: `PVS = engine
PVS test 0x101d1a90`; in PVS and `dist ≤ 512` (equality takes this arm) → `LOS = 1` without a
trace; else eye-to-eye trace
mask `0x4091`; then `if (!LOS && PVS && curtime − m_flLastInPlayerLOS < 8.0) LOS = 1`.

**One `NPCThink` (`0x10292de0`), in order**: `NPCThinkDebugPre`; `flags2 &= ~SCHEDULE_CHANGED`;
`if (m_bDisableAI +0x6080) return`; `normalDue = IsThinkDue(NextNormal)` — the master gate; when
due: `SetClosestPlayer`; enemy distance/height/last-known (`+0x6268/+0x626c/+0x6270`, `5000.0`
when none) and the `MOVE_FACE_ENEMY` facing under `debug_allow_move_facing`; `AngleVectors` →
`+0x6290/+0x629c`; `SetPlayerLOS`; `CacheInterruptConditions`; `AutoMovement` under
`ANIM_MOVEMENT 0x4000`; hint upkeep (`m_flOccludedDelay` from cover/normal; invalid hint, or
`!stay_entrenched && m_hHintCoverObject == enemy && (0x2e || 0x48)` → `ClearHintNode(5.0)` +
`SetCondition(0x29)`); shoot-target override; a 1 % `"Scream_Death"` under `frenzied & 0x8000`;
`updateDue`; `ResolveStandingOnHead`; fall-to-ground unless `DONT_FALL_TO_GROUND`; `move_yaw`
pose; `DISAPPEAR 0x20000000` removal when out of the player's PVS or unseen; the AI console gate
`0x1026c3d0` (2026-09-12, walked: with the node graph built (`DAT_1093408c`) and `g_AIDisabled`
bit 0 set, the refuse arm dispatches **slot 310 `SetActivity(1 = ACT_IDLE)`** — Troika
`0x10295750` falls through to `CAI_BaseNPC::SetActivity 0x102725d0`, which writes the ideal, resets
the sequence and fires slot 465 `OnChangeActivity` through `SetActivityAndSequence 0x10272490` — and
returns false; `NPCThink` then returns without writing `m_flNextThink`, so every NPC snaps to idle
and holds position and pose until `SetAIEnabled(true)` re-bases it. With the graph NOT built the
gate draws its 5 s overlay and takes the same `SetActivity` arm, and `NPCThink` writes
`m_flNextThink = curtime + 0.1` instead. The `ai_step` bit-1 arm refuses without the idle write);
**`bReduced = !IsThinkDue(NextAI)`**; `RunAlternateAI(bReduced)` (`0x1028fd80`) and, if it returns 0,
`RunAI(bReduced)` (slot 432); `PostRun` → `PerformMovement(interval)`; `CalcNextMoveThink`;
`CalcNextAIThink`. Then (whether or not normal was due): if `updateDue` → `UpdateCharacter`
(slot 312) and `FinishTalking` when `m_bIsTalking` and `0x102c0aa0` answers false (not
`IsInDialog` `0x102c1170`: it is "the dialogue handle `+0x6554` resolves and its `+0x498` byte is
set, or `curtime < m_flTalkEnd +0x64cc`"); `CalcNextUpdateThink`;
`CalcNextNormalThink`; **`m_flNextThink = min(NextUpdate, NextNormal)`**; `m_bJumping +0x6498` →
`curtime + 0.01`; the debug overlay.

**`RunAI(bReduced)` `0x1026f110`**: `m_bConditionsGathered = 0`; `GatherConditions` (slot 433)
only if `!bReduced && m_hDialogPartner invalid`; the head probe `0x1026ab50`; `PrescheduleThink`
(slot 434); `MaintainSchedule(this, bReduced)` — bound 10, or 1 when reduced (`0x1028190e`);
`if (!bReduced)` clear `LIGHT_DAMAGE 0x4c`, `HEAVY_DAMAGE 0x4d`, `WAS_BUMPED 0x38`. The base
`CAI_BaseNPC::NPCThink` `0x1026ca80` is a flat `curtime + 0.1` with `RunAI(0)`; no Troika NPC
runs it.

**Two corrections from the walk (2026-09-12).**

1. **`updateDue` is computed UNCONDITIONALLY**, between the two normal-due blocks and on both
   paths — `updateElapsed = curtime − m_flLastUpdateThink` then `IsThinkDue(NextUpdate)`
   (`0x102906a0`), before the branch that skips the rest of the body. Its position in the listing
   above is an artifact of the linearised decompilation. It has to be: the update clock floors at
   0.03 s against the normal clock's 0.1 s, so `min(NextUpdate, NextNormal)` is the update stamp
   most of the time, and a wakeup on it alone would otherwise advance the update clock without ever
   running the work it exists to schedule. `UpdateCharacter` takes `updateElapsed` as its argument,
   read before `CalcNextUpdateThink` overwrites the local with `Next − Last`.
2. **`0x10269a20` is `SetCondition(int)`**, not `TaskFail` — it ORs `1 << (id − 1000000000)` into
   the condition bitfield at `+0x5c5c`, through the same global/local id resolver
   (`0x102ea2d0`) every condition call uses. So the hint-release arm above raises condition `0x29`;
   it does not fail the task. The sibling readers are `HasCondition` `0x10269d30`,
   `ConditionInterruptsCurrentSchedule` `0x10269c70` (which additionally requires
   `m_pSchedule +0x5c38` and tests the schedule mask `+0x5c74` then the test-bits overlay
   `+0x5c8c`) and the clear `0x10269b50`.

**`COND_WAS_BUMPED` 0x38's producer (2026-09-12).** `0x10147690`, the player's touch handler:
`if (other->+0x9c) { this->AddMiscFlag(0x100 /* Obf_Bumped_Object */); if (this->+0xa8 /* this is
a player */ && (npc = other->+0x98) /* other is a Troika NPC */) { if
(ConditionInterruptsCurrentSchedule(npc, 0x38)) { ai-trace hook; SetCondition(npc, 0x38); } } }`.
The guard is the recovered behaviour and it is observable: the bit is never raised on an NPC whose
running program does not list `WAS_BUMPED` in its interrupt mask. Its one reader in the image is
`CAI_BaseNPCTroika::GetSchedule` `0x102ae920`, in combat. Source raises `Touch` out of the player's
own move step, every frame a move sweep blocks on the other entity, in either direction.

*Port (2026-09-12).* The contact is recorded on the NPC body (`AElysiumNpcBody::NotifyHit`, which
the engine reaches for a swept blocking hit whether the player's hull moved into the capsule or the
capsule into the hull) and drained once per frame through `IElysiumEmbodiment::
DrainPlayerTouchContacts` by `FElysiumPlayer::PollTouchContacts`, right after `SyncFromBody`; per
touched NPC it writes the player's `Obf_Bumped_Object`, runs `ElysiumDisciplines::NotifyBumped`
on the player (the port's stand-in for the flag's retail reader, 0006's) and calls
`FElysiumNpc::OnBumped`, which carries the mask guard; when the guard admits the bit, the same
reader runs on the NPC, because every authored `ShouldRemove_OnWasBumped 1` row (`disciplinetgt_*`:
Nightwisp Ravens, Bloodsucker Communion, Hysteria, Trance, two more) is a target effect and the
effect that breaks sits on the touched NPC — whether retail keys that removal on the condition or on
a misc flag of the victim's own is 0006's to recover. Verified in `sm_hub_1` 2026-09-12: one log line
per contact frame, `0x100` on the player, the NPC bit refused by every shipped program's mask. Unrecovered: whether retail's `CategorizePosition` also touches a non-world
ground entity (a player standing still on an NPC's head); non-NPC touchers (`+0x9c` set, `+0x98`
clear) take only the first arm and are not drained.

**Port (0002 story 15).** `Substrate/ElysiumNpcThinkCadence.h` carries `IsDue`, the four laws over
`FElysiumNpcScheduleHost`'s eight stamps, and `ShouldThinkFrequently`; `FElysiumNpc::Think` is
`NPCThink`'s shape and the only writer of `NextThink`. Three things differ and each is stated at
the code:

- **The record is synced on the frame, not the think.** Retail's entity origin IS the body —
  `PerformMovement(interval)` integrates the whole elapsed interval inside `NPCThink`, so a far NPC
  hops but is never wrong. This runtime's bodies are integrated by a movement component on the
  actor tick while `Origin` is written only from a think, and an unseen body may not think for
  seconds. `FElysiumEntityWorld::SyncMovingNpcRecords` refreshes every NPC's record once per frame,
  from `Tick` immediately before the player's own `SyncFromBody` (2026-09-12: it had been called
  from `Activate` alone, so a hidden body's record went stale for its whole cadence interval).
- **The silence is stated to the body.** For the same reason, a retail think that returns early
  (`m_bDisableAI`, the AI console gate) freezes its body by running no `PerformMovement` and no
  `PostRun`, while this runtime's body would walk on. `IElysiumNpcMotor::SetHeld` (the path
  follower paused with its request kept, velocity zeroed on the ground, collision and the crowd
  agent intact — not `SetFrozen`, which is the scene's `SOLID_NONE`) and `SetAnimationHeld` (the
  mesh's `GlobalAnimRateScale` to 0, under which the cinematic proxy's explicit seeks still land)
  are set by `FElysiumNpc::Think` on the two early returns and cleared by the first pass that
  gets through both gates. The `m_bDisableAI` arm holds both; the gate arm plays `ACT_IDLE`
  (`PlayActivity`, through claim arbitration) and holds movement only, so the port's idle LOOPS
  where retail holds its first frame — a named visual-only modernization, because a stopped clock
  would park the blend into idle mid-stride and would stop the feed victim's feeder-synced clip.
  `OnKilled` and dormancy release both holds.
  A route laid on a held body parks at the motor the same way (`AElysiumNpcBody::MoveTo` pauses
  the new request). The hold's first cut killed the very request it parked: after `PauseMove`
  it zeroed the velocity with `StopMovementImmediately`, whose `StopActiveMovement` aborts the
  outstanding request (`bStopMovementAbortPaths`, `Aborted[UserAbort MovementStop]` in the
  follower's own report), so a scripted beat's walk on a held body died with its whole distance
  left and the beat teleported the NPC (sp_theatre's walk-out, sp_tutorial_1's `thug_1` under
  `AIEnable 0`, 2026-09-12). The velocity is now zeroed through `PauseMove(Reset)` alone
  (`StopMovementKeepPathing`), airborne bodies keep theirs (`Keep`). Two engine finishes that
  happen inside the request call are also read now — `AAIController::MoveTo`'s `AlreadyAtGoal`
  is answered as arrived, and a follower the crowd component aborted synchronously in
  `SetMoveSegment` (empty corridor, nav-data mismatch) under a successful result is answered as
  refused — and the engine's own finish (`OnRequestFinished`) is kept as `Last move result` in
  the NPC debugger. A request the controller refuses outright (`Invalid[]`) is asked why
  (`AElysiumNpcBody::DescribeRefusedRoute`, appended to `Last move result` and a Verbose
  `refused:` line): the goal off the navmesh, this body off it, or the mesh not connecting them
  (the partial path's length and its distance short). Confirmed in game (sp_tutorial_1, `thug_1`, 2026-09-12): under `AIEnable 0`
  the beat's route parks with its whole distance left and no finish reported; on `AIEnable 1` the
  same request resumes, the body walks the 683 cm, settles at the mark and the beat fires
  `OnEndSequence` on arrival. The one divergence of the hold that remains is not the hold's: the
  `scripted_sequence` beat drives its travel from its own think, where retail's walk is a
  schedule inside `NPCThink` behind both gates — spec 0003 owns it.
- **A reduced pass re-derives the damage lane.** Retail sets `LIGHT_DAMAGE`/`HEAVY_DAMAGE` inside
  the damage transaction, so the bit is live on a reduced think; this runtime rebuilds its whole
  condition set each full pass and reconstructs the one-pass life from `Cognition.GatheredAt`,
  which a frozen set cannot express. `ElysiumNpcCond::GatherDamage` and `GatherBump` therefore run
  on a reduced pass too, without advancing `GatheredAt`.
- **A corpse is on no clock.** `ThinkDead` runs ahead of the cadence and polls its own death
  program at a named 0.1 s, because routing it through the distance laws would delay the ragdoll
  handoff for a body the player is not near.

**Port, the reset sites (2026-09-12).** After the dispatch-site recovery above, the port's
`ResetThinkTimers` calls were reconciled one by one. Kept, each at its retail site: `SetDisableAi`
(the 1→0 edge, `0x1029f300`), `InputTeleportToEntity`, `LeaveGrappleState` (`0x102b5d90`),
`OnDormancyChanged`'s wake (`ScriptUnhide` `0x102c1ec0`), the dialogue body session's start (the
three `StartPlayerDialog*` inputs and `PlayerUse`, four retail doors onto one port door), the
spoken-line player (`FElysiumNpc::OnDialogFilePlayed`, `0x102c0520`, which also carries
`m_bIsTalking` as `TalkingUntil`), the discipline applier's `AI_Schedule` arm and `FeedInterrupt`'s
trance install (both "slot 614, then `SetSchedule`", now at the callers rather than inside
`StartNamedSchedule`, because the `ChangeSchedule`/`StartSchedule` inputs are not sites),
`TASK_WAIT_PVS`'s completion (`FElysiumNpc::WaitPvs`, the slot-584 form), the teleport broadcast
(`FElysiumEntityWorld::WakeNpcsNear`, slot 583 from `point_teleport` and `teleport_player`), the
map-wide `SetAIEnabled` (`FElysiumEntityWorld::SetAiEnabled`, the slot-584 form on every NPC —
the makers included since story 5 fold A4, whose pulled think is their installed one — with
its three producers: a player's `FeedBegin` under the area-type and 3 s unobserved gate,
`FeedInterrupt`, `events_world`'s `AIEnable`, and the level-change fade), the scene cast
(`SetDisableAi(true)` at `position_start`, restored with slot 614 at `OnSceneFinished`), and the
maker's `DisableThink` inherited onto its children. Removed, because no retail site exists:
`aiscripted_schedule`'s pushes, the `ChangeSchedule` input, the disposition-transition miss, the
patrol and interesting-place inputs, the dialogue END, and `OnKilled`. The AI console gate
`0x1026c3d0` is in `Think` (`World->IsAiEnabled()`), the state byte is derived
(`FElysiumNpc::NpcStateFlags`), `DISAPPEAR` is run, and the enemy triple is written on every
normal-due think. `m_bfNPCStateFlags` bit 3 is no longer unrecovered: it is the state byte's own
bit, carried by combat, alert, script and the hunt/flee pair. Removed later the same day: four
port-only resets on the load path (the patrol and interesting-place restore branches and the
`bRemoveOnHearCombat` restore arm of `FElysiumNpc::Serialize`) — retail restores its saved stamps
and resets nothing, and three of the four were dead code under the `ScheduleHost` block that
restores the eight stamps last.

**The remaining items, closed (2026-09-12).** `m_flTeleportMoveTimer` `+0x65dc` has no code
writer at all: the literal-offset grep over every decompiled function returns only its reader,
`ShouldThinkFrequently` `0x102c2430`, so the earlier "a `StartTask` arm" note was wrong. Its one
writer is the datamap keyfield `teleport_move_timer` (`0x105d6034`), an absolute curtime; the V2
entity exports carry it on 827 NPC rows across 75 maps, `0` on 824 and `2` on three (one in
`la_crackhouse_1`, two in `sm_diner_1`), so the frequent-think window is open for those three
bodies during their map's first two seconds and for nobody else. `m_bForceFrequentThink` `+0x63f0`:
its only reader is the same function; its writers are the player controller line alone —
`CNPC_VPlayerController::Spawn` `0x103a4510` stores 1 directly and its `NPCInit` `0x103a4580`
dispatches the setter `0x101aa750` (slot 416, `vt+0x680`) with 1 (`CAI_BaseNPC::LeaveGrappleState`
`0x1026ce30` dispatches it with 0). **Corrected by 0019 story 5 fold A2**: the earlier "no caller,
never set" missed both controller bodies.
`UpdateCharacter` (slot 312): the Troika body `0x10298070` is the boss registry — a
`m_bIsBossMonster` body whose slot-464 answer is 2 stores its handle in the two-slot global
`DAT_109247e0` (`DAT_10924fb8` counts, `+0x6497` remembers), and a registered body that is no
longer a boss compacts itself out — then `CBaseCombatCharacter::UpdateCharacter` `0x103246d0`:
`UpdateDisciplineVisuals`, slot 313, `UpdateVampHeal_HOT`, `UpdateExpressions` (a player, or an
NPC whose slot-513 word carries `0x800000`), `MaintainScriptedEyeDirection` or slot 333, slots 314
and 315, and the `m_nRenderFX` `0x1a` / `0x25` expiries `_DAT_10449198` seconds after
`m_flEffectStartTime`. `m_bIsBCCTargetable`: set to 1 by `NPCInit` `0x1029a0b0`,
`CNPC_VWerewolf::Spawn` and `CNPC_VPlaceholder`; cleared only by `CNPC_VCamera`'s init
`0x103692c0` and by the cine entity's own `0x101a6f10` (not an NPC) — every real NPC reads 1. The
console toggle `0x10085180` is **`ai_disable`** (registered by `0x100851f0`, help "Bi-passes all
AI logic routines"); `ai_step` / `ai_resume` (`0x10085890` / `0x10085950`) drive bit 1. The
werewolf gates are `cvar_werewolf_show_debug` (`0x1093f738`, read through its parent pointer
`DAT_1093f73c` in `NPCThink`, `TeleportIn`/`Out`, `UpdateFakeHull`) and `cvar_werewolf_draw_hints`
(`0x1093f958`, the 1–6 selector over the six hint predicates in `0x103cb4b0`); the other seven
`werewolf_*` ConVars are `disregard_player_vision`, `draw_nodes`, `draw_last_teleport`,
`draw_last_move`, `draw_nearest_hint`, `print_haspath`, `teleport_out_time`,
`force_teleport_in_time`, `teleport_in_time`, `pursuit_distance`,
`translated_enemy_position_tolerance`, `footstep_shakes`. Slot 578 (`RemoveFromSquad`'s survivor
callback) is `CAI_BaseNPC::FUN_101a6cc0`, an empty virtual on all 77 classes. Slot 168's Troika
body `0x102b5360` is `GetEnemy()`, or `m_hLastEnemy` when that is null and `m_bfNPCStateFlags`
bit 6 is set (only `0x7f`: the hunt/flee pair) — the enemy the squad producer walks in the hunt
state. `CAI_BaseNPC+0x98` is the entity's own `CAI_BaseNPCTroika*` (null on a non-Troika entity;
`+0xa8` the same for `CBasePlayer*`), by every read in this section.

Still stated as seams, each owned elsewhere: the `NPCThink` arms listed at `FElysiumNpc::Think`
with their owning stories (hint upkeep and `COND_HINT_INVALID`, the shoot-target override, the
death-scream roll, `ResolveStandingOnHead`, fall-to-ground, `move_yaw`, the boss registry), and
the `MOVE_FACE_ENEMY` (`flags2 0x400`) move-facing under `debug_allow_move_facing` (default `"1"`,
so live in retail): slot 517 `0x10278d90` forwards `(enemy, its last-known position, 1.0, 0.8, 0)`
to `CAI_Motor` slot 12, the motor's facing target for a leg in flight — 0002's motor, with the
turn pose. `WAS_BUMPED`'s bump event is no longer a seam (see the touch handler above). The
scripted beat's travel, outputs and failure retry belong to the NPC's own schedule in retail
(`SCHED_TROIKA_SCRIPTED_*`, `NPC_STATE_SCRIPT`) and run from the beat's think here — spec 0003.

## Dormancy: `ScriptUnhide`'s Troika tail and its species tails — `0x102c1ec0` (2026-09-13)

Slot 78 for 62 classes. Past `CBaseEntity::ScriptUnhide` the Troika body does three things, in
order: it dispatches slot 614 `ResetThinkTimers` (vtable `+0x998`), so an unhidden body is due on
every clock on the frame it wakes; it dispatches slot 78 on the **active weapon**, which is an
unhide and not a holster; and, when `m_bCineScriptHidden` (`+0x5d78`) stands and `m_hCine`
(`+0x5d74`) resolves, it writes six words of its own state onto that cine — slot 94 `GetMoveType`
into `+0x5f78`, slot 95 `GetMoveCollide` into `+0x5f7c`, slot 92 `GetSolid` into `+0x5f80`, slot
211 `GetSolidFlags` into `+0x5f84`, `m_fEffects` into `+0x5f88` and `m_bfAINPCFlags` into `+0x5f8c`.
`m_bCineScriptHidden` is then cleared on **both** arms, whether or not a cine took the record.

`CNPC_VWerewolf::ScriptUnhide` (`0x103d4a20`) calls that base and then writes four words in this
order: `+0x66ec := curtime`, then `+0x66d8`, `+0x66d4` and `+0x66a4` to zero — the morph-timer
block. `CNPC_VGhoulCroucher::ScriptUnhide` (`0x1037c2f0`) calls the base and then resolves
`m_hBurningParticle` (`+0x6670`) and dispatches **its** slot 78 when it resolves to a live entity;
retail does not clear the handle afterwards. `CAI_Hint`'s pair (`0x102d0860` / `0x102d0890`) is the
base plus `m_iDisabled` (`+0x05e8`) `:= 1` / `:= 0`, and **`CAI_Hint::Kill` (`0x102d08c0`) is
`JMP [[this]+0x134]` — vtable `+0x134` is slot 77, so a hint node's `Kill` IS its `ScriptHide`**,
not the entity teardown every other class runs at slot 119.

**Unrecovered:** the four vtable dispatches the listing shows *ahead* of `CBaseEntity::ScriptUnhide`
— the same slots 94/95/92/211 in the same order, with their results discarded. Either the compiler
hoisted the reads or retail restores those four words from the cine first; nothing in the corpus
settles which, and the port reproduces only the sequence whose destination is stated.

### `CAI_Hint::vfunc5` — `0x102d2f00` / `0x102d3040` (2026-09-13)

The hint node's deleting destructor, and the one place a hint's *destruction* writes to an NPC.
`0x102d2f00` is MSVC's two-liner — `thunk_FUN_102d3040(this); if (flags & 1) operator delete(this);`
— and `0x102d3040` is the destructor proper. Everything the kernel can observe is in its first arm
and is done **to the owning NPC**: with `m_hOwner` (`this[0x178]`) resolving and that entity's
`+0x98` NPC pointer non-null, it calls `SetCondition(owner, 0x29)` (`0x10269a20`) and then
`CAI_BaseNPCTroika::ClearHintNode(owner, 0.0)`. The **order** is the fact: the condition is raised
*before* the hint reference is dropped, so a selector running on the same pass sees both the bit and
the cleared node. The **0.0** is the second: every other caller of `ClearHintNode` passes a reuse
delay (5.0 s from `TaskFail` and from the state-change path), and this one passes zero, because the
node is being destroyed and there is nothing left to hold a cooldown against. Condition `0x29` has
no recovered name.

The rest of `0x102d3040` is the engine bookkeeping: unlink from the global hint list (head
`DAT_10925450`, next at `+0x5d8`, the two cursors `DAT_10925454` / `DAT_1092545c`, the count
`DAT_10925458`), then destruct the node's `CUtlVector`s and free. **Unrecovered:** what the
`(**(DAT_10924a6c + 4))()` call ahead of the two writes is — an allocator or scope hook with no
arguments and no NPC state in reach.

## `CAI_BaseNPC::FindNamedEntity` — `0x10279090` (2026-09-13)

Slot 559, the target-selector string dispatch, `__strcmpi` throughout. In order: `!player`,
`!nearestfriend` and `!friend` all resolve `UTIL_PlayerByIndex(1)` — **there is no friend search**;
`!playercontroller` resolves the player and then `thunk_FUN_101618a0` on it, falling to the tail
when there is no player; `!enemy` reads vtable `+0x29c` twice (a null test and the answer); `!self`
and `!target1` are **matched and then deliberately unhandled**, so they reach the tail. Two retired
literals follow: the bare `self` (`0x105a1cb0`) prints
`ERROR: "self" is no longer used, use "!self" in vcd instead!` while the post-incremented counter
`DAT_10920558` is under 5 — four messages, not five — and answers `this`; the bare `Player`
(`0x10547404`) prints the matching `"player"` text under `DAT_1092055c` and answers the **player**.
Anything else is `CGlobalEntityList::FindEntityByName`. Every arm that does not resolve falls to one
tail, `return this`, which is what keeps a mistyped `.vcd` actor name pointing at a live entity.

## `CBaseEntity::KeyValue(const char*, const char*)` — `0x1009e430` (2026-09-13)

Slot 110 for the five AI-helper classes (`CAISound`, `CAI_Hint`, `CAI_InterestingPlace`,
`CAI_InterestingPlaceConverstation`, `CAI_StandoffGoal`). **The first thing it does is truncate the
key at a `#`**, so `origin#2` is `origin`. Then, in order: `rendercolor` / `rendercolor32` write the
three `m_clrRender` bytes and `renderamt` its alpha through `atoi`; `disableshadows` ORs `0x20` into
`m_fEffects` when non-zero and `disablereceiveshadows` ORs `0x80`; `mins` and `maxs` each set the
collision bounds with the other bound read back from `m_Collision`; `angle` is `atof`'d and
**rewritten as `angles`** — a value at or above `0.0` composes `"<pitch> <value> <roll>"` from
`GetAbsAngles()`, replacing the yaw alone — and then re-enters the cascade; `angles` and `origin`
dispatch vtable `+0x368` and `+0x360`. Anything unmatched walks the datamap chain (`+0x148`
`GetDataDescMap`, following `baseMap`) offering the key to each level's `ParseKeyvalue`; a hit
returns true, an exhausted chain returns false.

**Unrecovered:** the literal the `angle < 0` arm copies with `Q_strncpy` — the source is folded away
in the decompilation, and Source's own `angle -1` / `-2` convention is not evidence about this
image. Not ported: the `ent_debugkeys` cvar arm (`DAT_106cf424`), a console diagnostic that `Msg`s
every matched and unmatched key for one classname.

## Slot 104 `Precache`, by species — `0x102dbcb0`, `0x1034aa40`, `0x103689c0` (2026-09-13)

`CAI_InterestingPlaceConverstation::Precache` (`0x102dbcb0`) precaches `m_iszSoundLoop` and then
`m_iszSoundOnce`, and **only the loop is warned about when its name is empty** — it is then not
precached at all, while the one-shot is precached unconditionally and never warned. The asymmetry is
retail's. `CGenericNPC::Precache` (`0x1034aa40`) walks the weapon-sound table
`PTR_s_weapons_ar2_ar2_fire1_wav` for `0xc` bytes at stride 4 — **three** entries — then precaches
its own model through the model precacher (`*DAT_1070b22c+0x34`) rather than the sound one.
`CNPC_VCamera::Precache` (`0x103689c0`, shared with `CNPC_VCameraSecurity`) falls the model key back
to `models/null.mdl` when it is unset or empty, precaches it, dispatches slot 452 (`+0x710`) and
**rejects the spawn outright** with `Msg("ERROR: Rejecting spawn of %s as e...")` when that answers
false, then zeroes `m_iInterestingPlaceGroups` (`+0x62dc`) and runs the AI-node link-table integrity
check that `DevMsg`s `"... is being spawned after links have been..."`.

**Unrecovered / not ported:** the camera's tail. This substrate has no AI node graph and no link
table, so there is nothing to check, and the rejection arm needs slot 452, which is a later story's.
The second and third rows of the weapon-sound table are named by index only — the corpus names the
array by its head.

## Slot 103 `Spawn`, by species — `0x101a6f10`, `0x102cd2d0`, `0x102d0b60`, `0x102dbc80` (2026-09-13)

`CCineNPC::Spawn` (`0x101a6f10`, shared with `CCineAI`; `CCineAISchedule::Spawn` `0x101a9730` calls
it DIRECTLY, then prints "no schedule or state has been set!" when both are 0): `SetSolid(0)`,
`AddSolidFlags(flags | 4)`, slot 93 `SetMoveType(0, 0)`, `m_bIsBCCTargetable = 0`, `m_bIsAlive = 0`.
Then `CineThink` (`0x101a8070`, not an "auto-remove" think — corrected 0019/5 fold A3), armed at
`curtime + 1.0` when the cine is **unnamed** or spawnflag `0x10` is set — and only a **named** cine
that armed it also takes `m_startTime = curtime + _DAT_10449e10` (1e6, "wait for BeginSequence").
`CineThink` searches for the NPC (`FindEntity`, the radius gate) and possesses it, or runs
`CancelScript` and retries in 1.0 s. Spawnflag `0x20` **clears** `m_interruptable`; its absence sets
it. Finally `Relink`, `m_sequenceStarted = 0`, `m_hNextCine = -1`, `m_pLastFoundEntity (+0x5f98) = 0`
and `AddFlag2(0x10)`. No base `Spawn`, so a director never runs `NPCInit` or `NPCThink`; its only
thinks are `CineThink` and, after a non-repeatable beat, `SUB_Remove` (`Finish` `0x101a8640`, +0.1 s).
Port: `FElysiumScriptedSequence::Spawn`.

`CAI_StandoffGoal::Spawn` (`0x102cd2d0`) is `ThinkSet(&LAB_10006672, 0, NULL)` and
`m_flNextThink = curtime + _DAT_1044e658`; the think function is a stub label with no body in the
corpus, so the clock is all that is recovered.

`CAI_Hint::Spawn` (`0x102d0b60`) is `SetSolid(0)`, `Relink`, then a per-hint-type default block —
`100..101` and `0x283c` take `60 / 256 / FLT_MAX / 3`, `0x27d8` takes `17 / 256 / FLT_MAX / 3`,
`0x283d` takes `10 / 64 / 256 / 3` (the only finite max distance) and `0x28a0` takes
`10 / 64 / FLT_MAX / 3` — each float written **only** if it still holds the unset sentinel, so an
authored value always wins. The angle range is then scaled by `_DAT_104454d0` (or, for `0x27d8`
alone, biased by `_DAT_1049b998`) and turned into its own dot test through `fcos`, a category bitmask
is written at `+0x474` (`1`, `1`, `4`, `8`, `0x10` respectively) and the lazily-loaded
`NPC_Cover_Distance_Scalar` rule value is consulted. The `100..101` row carries retail's one
duplicated test — a still-unset `m_flTargetDistMin` re-writes `m_flHintRating`, not the distance,
and is unreachable after the fill above. Last, and for **every** hint including one with no default
row, `m_iGroupID` `1..32` folds to `1 << (id-1)` and anything else to `-1`.

`CAI_InterestingPlaceConverstation::Spawn` (`0x102dbc80`) is its own field init and then
`JMP [[this]+0x1a0]` — **the spawn's last act is its precache**, which is the reverse of every other
class.

**Unrecovered:** `_DAT_10449280` (= **1.0**, float64; read 2026-09-21, `rdata-cells.md`) and `_DAT_10449e10` (= **1,000,000.0**, float64; read 2026-09-21, `rdata-cells.md`) (the cine's two delays), `_DAT_1044e658` (= **0.01**, float64; read 2026-09-21, `rdata-cells.md`) (the
standoff goal's), `_DAT_104454d0` (= **0.5f**, float32; read 2026-09-21, `rdata-cells.md`) and `_DAT_1049b998` (= **43.0f**, float32; read 2026-09-21, `rdata-cells.md`) (the hint's angle scale and bias). None has a
reader in the corpus that pins its value.

## `CAI_InterestingPlace`'s spawn and restore — `0x102d9c20`, `0x102d9dd0`, `0x102dbbc0` (2026-09-13)

`Spawn` (`0x102d9c20`), in order: when `m_sType` is **non-empty**, look it up in the place-type table
(`thunk_FUN_102dd9c0`) into `+0x548` and, on a miss, `Warning("Could not find InterestingPlaceT...")`
and `UTIL_Remove` — **and return**, so nothing below runs. Then a `DevMsg` above 20 markers, a
`m_iMarkersAllocated * 0x1c` table allocated and its first dword zeroed per row, `+0x588 = 0`, the
group fold, and last the rating clamp. The clamp is asymmetric: a negative rating becomes 0 and
**returns**, so the high-side test never runs on the same pass; a rating over 5 becomes 5.

**The group fold here is not the hint's.** `m_iGroupID` `1..32` folds to `1 << (id-1)` and anything
else to the **literal `1`** — `CAI_Hint::Spawn` folds an out-of-range id to `-1` / `0xFFFFFFFF`,
which is every group, and the place folds it to group 1. The note under
`authored-control.md` § "The navigation and reaction keyfields" that attributes `0xFFFFFFFF` to
`CAI_InterestingPlace::Spawn` is reading the hint's rule; the place's body at `0x102d9c20` writes 1.

`OnRestore` (`0x102d9dd0`, slot 130) is `CAISound::OnRestore` and then the **same** type lookup and
self-destruct, at `DevMsg` rather than `Warning` severity, followed by `thunk_FUN_102d9eb0()` on the
surviving arm.

**`CAI_InterestingPlaceConverstation::vfunc5` (`0x102dbbc0`) is not a reset and not a constructor.**
Slot 5 is the lifetime slot, and the body is MSVC's scalar deleting destructor verbatim: destroy the
two sub-objects (`thunk_FUN_102dce10` / `102dce80`), destroy the six `COutput`s
(`thunk_FUN_100cd2d0` on `m_OnPlayerLeftRadius`, `m_OnOneOffSoundComplete`, `m_OnPlayerTooClose`,
`m_OnConversationEnd`, `m_OnNewTalker`, `m_OnConversationStart`), chain the base destructor
(`FUN_1000cb8a`), and `if (flags & 1) operator delete(this)`. **Bit 0 of the argument is "free the
storage", not a hide.**

**Unrecovered:** `thunk_FUN_102d9eb0`, the restore's tail call — it takes no argument in the listing
and the corpus does not settle what it registers.

## `CAI_InterestingPlaceConverstation::Activate` — `0x102dbde0` (2026-09-13)

Slot 113 on that class. `+0x04fc` (the admitted count) is zeroed first. Then
`m_iszInterestingPlaces` (`+0x0450`) is walked as a **targetname**, with each
`FindEntityByName` continuing from the previous hit rather than from the head, so one name can admit
many places. A hit is admitted when `___RTDynamicCast` to the conversation interesting-place type
succeeds **and** `field[0x161] == 1`; it is then appended to a growing array
(`thunk_FUN_102dcec0` / `102dce30`) and back-registered on the place (`thunk_FUN_102db740`).
Everything else is `DevWarning`ed (`"%s %s is not a valid target for ..."`) and dropped. The tail is
`m_bEnabled`: set re-arms the conversation think (`thunk_FUN_102dc3e0`), clear turns the think off
outright (`ThinkSet(NULL, 0, NULL)`).

## `CAI_BaseNPCTroika::Activate` — `0x1028e310` (2026-09-13)

Slot 113 for 59 classes. After `CBaseEntity::Activate`, when slot 138 `Classify()` answers non-zero
the NPC applies `m_sDefaultDisposition` (`+0x6558`) — or, when the keyfield is unset, the **empty
string**, which retail substitutes explicitly — through a `SetDisposition(name, 1)` equivalent
(`thunk_FUN_102c0f70`). The level is the literal `1`. `Classify()` answers 0 for the inert helper
classes that share the `CAI_BaseNPC` vtable (`CAI_Hint`, `CAI_TestHull`, `CScriptedTarget`), which is
what the gate is for.

## `CAI_BaseNPC::Restore` and `UpdateOnRemove` — `0x1027c160`, `0x1027ca30` (2026-09-13)

`Restore` (slot 127): the `IRestore` vtable `+8` reads an `AIExtendedSaveHeader_t` into `+0x19b4`,
then `CBaseCombatCharacter::Restore` chains, then two runs of saved `FIELD_TIME` floats are re-based
onto the restored clock (`thunk_FUN_101cf2f0` — **four** from `m_flExtendedBlockedByFriendTimer`
`+0x5b8c`, **three** from `m_flWaitFinished` `+0x5db4`), and last the motor
(`thunk_FUN_102e0b80`, only when `m_pMotor` stands) and the move-and-shoot overlay
(`thunk_FUN_102e8ac0`) are re-linked.

`UpdateOnRemove` (`0x1027ca30`) is slot 180 for the **non-Troika** branch (`CAI_BaseNPC`,
`CScriptedTarget`, `CNPC_Bullseye`, `CNPC_Crow`, the test hulls) and is a different body from the
Troika override `0x1028d6e0`. Its order is: ask the squad at `+0x5da4` whether this NPC is a member
(`thunk_FUN_10315ec0`) and unlink it if so (`thunk_FUN_103158f0`); release the hint node `+0x5ddc`
with a **zero** reuse delay (`thunk_FUN_102d1420(hint, 0.0)`) and null the pointer; dispatch this
class's own vtable `+0x7fc` (slot 511); chain `CBaseCombatCharacter::UpdateOnRemove`.

`CAISound::OnRestore` (`0x100aa5a0`, slot 130) is three instructions:
`MOV EAX,[ECX]` / `MOV [ESP+4],0` / `JMP [EAX+0x18]`. `+0x18` is slot 6 `SetCheckUntouch`, and the
argument is **overwritten with 0** before the jump, so **a restore always lands as
`SetCheckUntouch(false)` however it was called**.

## `CAI_BaseActor::PickLookTarget` — `0x1025f1c0` (2026-09-13)

The body that occupies `ProcessTweakParam`'s physical slot (585) in the `CAI_BaseHumanoid` branch is
not a tweak-param handler at all: it is `PickLookTarget(bExcludePlayers, minTime, maxTime)`, which
`MaintainEyeDirection` dispatches with `(0, 1.5, 2.5)` — the SDK-2013 defaults. Three arms, first
match wins. **The enemy**: `GetEnemy()` (`+0x29c`), admitted when `FVisible` (mask `0x2804091`)
passes **or** `RandomInt(0,3) == 0`, and then `ValidHeadTarget` (`+0x930`) on its eye point; the pick
takes importance `RandomFloat(0.7, 1.0)` and the caller's durations. Falling past this arm
**re-draws** the durations to `RandomFloat(0.5, 0.8)` and `0.2`, and the arms below use those. **The
navigation goal**: a navigator with a path, `RandomInt(1,10) < 4`, and a goal further than
`_DAT_10497cb0`; importance is `RandomFloat(0.2, 0.4)` on `RandomInt(1,10) < 6` and
`RandomFloat(1.0, 2.0)` otherwise. **The scan**: skipped outright when slot 464 `GetState()` is 2
(COMBAT) and `RandomInt(1,10) <= 8`; otherwise a 1024-unit entity query around `GetAbsOrigin()` that
rejects self, rejects a player when `bExcludePlayers`, prefers a character (`GetFlags() & 0x80`)
passing `FVisible` and `ValidHeadTarget`, and otherwise scores each candidate against
`RandomInt(1,100)` scaled ×10 for a live entity and ×100 for one that answers slot 152. The chosen
target is handed to `AddLookTarget` (`+0x860`).

**Unrecovered / seams:** `_DAT_10497cb0` (= **96.0**, float64; read 2026-09-21, `rdata-cells.md`), the goal-distance floor (two readers, both in this body).
There is no entity-in-radius query on this substrate's NPC leaf, no navigator goal beyond the feet
destination of the move in flight, and no head-aim cone, so the scan arm answers nothing and
`ValidHeadTarget` answers retail's accepting arm.

## `CAI_Senses::PerformSensing` — `0x10310710` (2026-09-13)

Past the VProf scope the whole body is one gate and two calls: **`senses+0x80`
`m_bCanPerformSenses`**, then `Look(m_LookDist)` (`0x1030ff10`) and `Listen()` (`0x1030f940`), in
that order. The port's `FElysiumNpcSenses::Tick` ran its three halves unconditionally; the gate now
stands in front of them, defaulting set — which is every NPC — with no ported producer clearing it.

## `CNPC_VMingXiao`'s proxy gate and the werewolf search timer — `0x10397b40`, `0x103d1ca0`, `0x103d1d60` (2026-09-13)

`FUN_10397b40`, arm by arm: a null argument answers false; `curtime < m_flProxyReadyTimer`
(`+0x66a4`) answers false; the argument's own slot index (`arg+0x6660`) must index back to the
argument through `this+0x66a8 + slot*4`, or the answer is false; a slot whose `m_rbProxyRegistered`
byte (`+0x6684 + slot`) is already 1 answers **true at once**; otherwise the six slots are counted —
a slot counts when its handle resolves live **or** its registered byte is set — and only a count of
**zero** registers this slot and answers true. One proxy at a time.

**CORRECTION (story 5 step 4).** The index-back test reads `this+0x66a8`, which is
`m_rhSeveredTentacles`, not `m_rhProxies`, and the slot index is the argument's own
`CNPC_VMingXiaoTentacle::m_iTentacleID` (`+0x6660`), which the port had read as a seam answering
nothing. `FUN_10397a50`, which stamps `m_flProxyReadyTimer` from `Event_Killed`, takes a PROXY — a
`CNPC_VMingXiao`, whose own id is `+0x6674` — not a tentacle.

`CNPC_VWerewolf::StartSearchTimer` (`0x103d1ca0`) stamps `rdtsc` into `DAT_1093d638`/`DAT_1093d63c`
and `ReportSearchTimer` (`0x103d1d60`) reads `rdtsc` again, subtracts the pair **in place** so the
statics hold the elapsed cycles, and passes its second argument through unchanged. The pair is a
**file static**, shared by every werewolf on the map rather than kept per NPC; that is the recovered
fact, not an accident of the decompilation.

## `CAI_StandoffBehavior::vfunc13` — `0x102c7600` (2026-09-13)

A standoff selector, gated at the top on `m_NPCState == 2` (`+0x5cc0`, COMBAT); anything else falls
straight through to `CAI_Behavior::vfunc13`. Then, in order: condition `0x40` **or** `0x3f` answers
`0x29` minus the `+0x24` byte (so `0x29` or `0x28`) — one answer, two gates. The `+0x4c` latch is
consumed on read and, with an enemy standing, answers `0x17`. Condition `0x4c` gated on
`RandomInt(0,99) <= m_iChanceThreshold` (`+0x38`, note `<=`) min-s the claimed hint's `+0x9c` timer
down to `curtime` and collapses `+0x48` to `count > 1 ? 1 : 0`. An exhausted `+0x48` that has been
idle longer than `_DAT_10497530` re-rolls `RandomInt(0, max-min) + min` from the `+0x30`/`+0x34`
pair. A `+0x48` of exactly 1 re-stamps `+0x3c` — `+0x44 == 0.0` means "no range" and the delay is
`+0x40` alone, otherwise `RandomFloat(+0x40, +0x44)`. A `+0x48` at or below zero answers `0x17` and
on the way out writes the posture from the hint's `m_nHintType` (`0x65` → 2, else 0) and, on
`RandomInt(0,99) < 0x50`, min-s the hint timer again. Condition `0x48` promotes posture 2 to 3 and
answers `0x25`, or latches `+0x54` when `+0x64` is older than `_DAT_10497530`. Conditions `0x4f` and
`0x51` each **suppress** the `0x60` arm, which otherwise answers `0x21` — unless `0x48` also stands
and `RandomInt(0,99) > 0x31`, which falls through to the base.

**Unrecovered:** `_DAT_10497530` (= **-0.001**, float64; read 2026-09-21, `rdata-cells.md`), the elapsed-seconds threshold both the re-roll and the blocked
latch compare against — two readers, both in this body. `CAI_StandoffBehavior` has no counterpart in
this substrate at all, so the rule is ported pure over the behaviour's own datamap words.

## `CAI_Motor`'s reset to default — `0x102e1110` (2026-09-13)

Slot 5 on `CAI_Motor`. Four steps: resolve the navigator (`thunk_FUN_102e2610`) and its move type
(`thunk_FUN_102ee3f0`) and push that type onto the outer NPC through vtable `+0x4d8`; reset the
motor's own state (`thunk_FUN_102e2840`); zero the facing/move vector (`thunk_FUN_102e2690` against
`DAT_1070d1b0`, the same always-zero global slot 210 `GetGroundVelocityToApply` answers); set
`npc+0x3ec` to `1.0` — gravity back to default. Only the last has a destination on this substrate.

## `CNPCMaker::ParseMapData` — `0x1034b3c0` (2026-09-13)

Slot 107. The maker's own map-data string is copied into `field_0x66cc` character by character until
a `\0` or a literal `}`, and then a `}` is written at the cursor **unconditionally** — so an input
with no brace still ends in one, and an empty input yields the single character `}`. The buffer is
not NUL-terminated after that write. `m_sRefMapDataBuffer` (`+0x76cc`) is then set from the buffer's
first byte (`-(buf[0] != 0) & buf`), which the `}` just written makes non-zero on every path:
**the "empty means null" arm is unreachable**. `CBaseEntity::ParseMapData` follows.

## Two unnamed reads and two unnamed tables — `0x10160680`, `0x100e58b0`, `0x1037b870`, `0x1037b890` (2026-09-13)

`FUN_10160680` multiplies `*(float*)(this+0x1ddc)` by the compiled constant `DAT_10725c9c`.
`FUN_100e58b0` answers true when the byte at `+0x30e9` is non-zero and otherwise answers
`*(int*)(this+0x2830) == 0` — the byte short-circuits, so the word is only read with it clear.
`FUN_1037b870` and `FUN_1037b890` index the static tables `DAT_1063abcc` and `DAT_1063abdc` by
`CNPC_VGhoulCroucher::m_nUnawareType` (`+0x6668`).

**Unrecovered:** all four. `DAT_10725c9c`'s value, the retail names of `+0x1ddc`, `+0x2830` and
`+0x30e9` and the classes that own those offsets, and both unaware tables' contents and purpose
(message, sound or activity selection). Each body has one direct caller and no vtable slot; the
indexing and the arm order are what is recovered, and the values are named seams.

## `CDialog::clan_offset` — `0x100e65d0` (2026-09-13)

1,650 bytes, almost all of it lazy initialisation: seven `DAT_106e866c` bits each guard hashing one
Camarilla clan template name — `Player_Brujah`, `Player_Gangrel`, `Player_Nosferatu`,
`Player_Toreador`, `Player_Tremere`, `Player_Ventrue`, `Player_Malkavian` — into its own cache on
first use. The body then takes the speaker's `GetCharTemplate` hash (`+0x32f4`, cached at `+0x32f0`)
and compares it against the seven caches **in that order**, answering `0..6` for the first match and
`-1` when none matches. The hash comparison is a tamper check (`thunk_FUN_1042ffe0` over a fixed
mask/xor chain), not a name compare, and the ORDER is the one the seven `.dlg` clan text columns are
written in — which is **not** the sheet's own `2..8` clan encoding. A non-vampire speaker, and every
NPC (whose template is an `npctemplate*` row), answers `-1`. The port's
`ElysiumDlgClan::OffsetFromSheetClan` is this body.

## Death effects: the eight emitter bodies — `0x103c6df0`, `0x103c6eb0`, `0x103c7010`, `0x103c7150`, `0x1036e8c0`, `0x103ab110`, `0x1035e1a0`, `0x1035e3c0` (2026-09-13)

Story 29c-1, family **Damage**. Four boss species carry named particle emitters through the same
four retail calls: `thunk_FUN_100fbc90(name, origin, angles)` creates one, vtable `+0x3cc` attaches
it (mode 1 = a one-shot on the caller, mode 2 = attached at an entity or a named bone), `+0x3c4`
starts it and `+0x3c8` stops it. Their DECISIONS differ and that is what is recovered.

`CNPC_VVampireBoss` keeps four per-body-region names at `m_pBodyEmitterNames` (`+0x6684`) and four
live handles at `m_hParticleEmitters` (`+0x66a0`). `SetBodyEmitterName` (`0x103c6df0`) is one store
with **no bound check** — retail indexes the four-element array with the caller's word as given.
`ClearBodyEmitterNames` (`0x103c6eb0`) writes all four to null, unconditionally, as four separate
stores. `SpawnBodyEmitter` (`0x103c7010`) refuses a null attach entity first and an unset region name
second, then one-shots on ITSELF when the region is exactly 3 (`+0x3cc(this, 1)`) and attaches at the
given entity otherwise (`+0x3cc(this, 2, attach)`) — and **it never calls `+0x3c4`**, unlike every
other emitter body in this family, so the root is created and attached but not started.
`KillBodyEmitters` (`0x103c7150`) walks all four words and, for each that still resolves, calls
`+0x3c8` then `thunk_FUN_100fbbb0(entity, 0.1)`, the 0.1 s fade-and-remove; **the handles are not
cleared**, so a second call walks the same four words again. `CNPC_VChangBros::KillCenterEmitter`
(`0x1036e8c0`) is the same pair on the single `m_hCenterEmitter` (`+0x66f4`) and it too leaves the
handle standing.

`CNPC_VSabbatLeader::SpawnBloodPoolEmitter` (`0x103ab110`) takes X and Y from this NPC's own origin
(slot 217) and Z either from the optional second parameter's origin or, with none, from
`thunk_FUN_101d08e0(origin, z)` — a floor-drop lookup. It then creates the named emitter and starts
it. Its create-failure arm dereferences a null pointer at `+0x3c4`; the port refuses instead, which
is a named divergence.

`CNPC_VAndreiBlood::StartBloodEmitter` (`0x1035e1a0`) and `StartSummonEmitter` (`0x1035e3c0`) are
ONE body written twice: refuse a null name; release the previously cached handle
(`thunk_FUN_101cd940`) and set it to `-1` if it still resolves; create at this NPC's origin; store
the new handle, or `-1` on failure; attach; start. The whole difference is the word and the attach —
the blood arm holds its handle at `+0x66e0` and PARENTS the emitter to this entity
(`thunk_FUN_100faf60`), the summon arm holds its handle at `+0x66e4` and attaches at the bone
`Bip01_R_Hand` (`+0x3cc(this, 2, name)`).

**Unrecovered:** `+0x66e0` and `+0x66e4` are past `CNPC_VAndreiBlood`'s last datamap row
(`m_iHitMax`, `+0x66dc`) and have no retail names; they were read off the listing
(`MOV EAX, dword ptr [ESI + 0x66e0]`) and are typed by use alone. The floor-drop lookup
`thunk_FUN_101d08e0` was not walked.

## `CNPC_VSheriffMan::KillSheriff` — `0x103b10f0` (2026-09-13)

409 bytes, two halves, and the second does not depend on the first. The relay half looks up BOTH the
entity named `logic_zap_player` (RTTI-cast to `CLogicRelay`) and the entity named `sheriff`, and
fires `InputTrigger` on the relay **only when both resolve** — the `sheriff` lookup's result is never
used for anything else, so it is a presence test and a map missing that name does not zap the
player. The weapon half then runs unconditionally: the NPC's active weapon, if it has one, takes
`m_fEffects |= 0x20` (`EF_NODRAW`), `AddSolidFlags(4)` (`FSOLID_NOT_SOLID`) and a `Relink` — **the
sword goes invisible and non-solid, it is not removed**, so the corpse nominally still holds it.

## `CNPC_VVampireBoss::CausePlayerAOEDamage` — `0x103c7230` (2026-09-13)

753 bytes. Everything is inside `m_hClosestPlayer` (`+0x628c`) resolving. The gate is the LENGTH of
the vector from the centre to the player being strictly less than the radius, not its square. It
then traces from the centre to the player with `CTraceFilterWorldOnly` and mask 1 — and **never
tests the result**, which it hands to `DispatchTraceAttack` as the hit record. The descriptor is
`CVDmg_t::Set(1, 0x40, (int)|delta| squared)`: family LETHAL, `DMG_BLAST`, and **the damage input is
the SQUARED distance**, so a player deeper inside the radius takes less and one near its edge takes
more. That is retail's own arithmetic and is ported verbatim. Finally it picks an impact sound off
the victim's own `+0x50c` answer — `0x79` by default, `0x7a` on 1, `0x7b` on 3 — and plays it
through `+0x500`.

**Unrecovered:** the victim's `+0x50c` and `+0x500` slots; the port's seam answers 0, the default arm.

## `CNPC_VChangBros::SpawnEnergyBall` — `0x1036dd20` (2026-09-13)

447 bytes. It takes the muzzle attachment's angles (slot 219, `+0x36c`), turns them into a basis with
`AngleVectors` (`0x10139610`), and pushes this NPC's origin along it by the constant triple
(`_DAT_104ada24`, `_DAT_104ada28`, `_DAT_104ada2c`) = **forward 50, up 40, right -10** — the three
accumulations in the listing pair each basis row with the same three cells. `CBaseEntity::Create`
then stands an `item_w_chang_energy_ball` there, and **only when both that create succeeds and
`m_hClosestPlayer` resolves** does it fire the projectile through its own `+0x5d0` with speed
`DAT_104ada30` = 800.

## The two thrown props — `0x10365860`, `0x1038f2c0` (2026-09-13)

`CNPC_VBach`'s grenade (`0x10365860`, 413 bytes) is gated first on
`curtime - m_flLastGrenadeTime (+0x6680) >= 5.0` (`_DAT_10454110`), inclusive. A missing named target
leaves the cooldown UNSTAMPED, so the next think retries at once; a present one stamps it BEFORE the
create, so a failed create still spends it. The grenade is created at the target's origin with
`m_takedamage = 2`, `m_iHealth = 1` and a null touch function — a grenade a single point of damage
detonates and that nothing triggers by touch — its physics initialised if it has none (a failure
`Msg`es `"No physics data for grenade"` and removes it), the target's FORWARD vector scaled by the
caller's force applied as a velocity with zero angular velocity, and both its own `+0x7c` word and
`m_flNextThink` armed at `curtime + 3.0 + 0.01` (`_DAT_10449258`, `_DAT_10450aa4`). `m_bCamperFlag`
(`+0x66a0`) is cleared LAST, on the thrower, past the create guard.

`CNPC_VManBat`'s model throw (`0x1038f2c0`, 285 bytes) is named by its own `DevMsg` literal
`"ManBat is throwing model %s"`. It creates a `prop_physics` at this NPC's origin without spawning
it, sets its model, spawns it, looks a bone up **by the same string it just passed to `SetModel`** —
a retail quirk, reproduced as written — makes a corpse-shaped ragdoll from it
(`thunk_FUN_10157da0`), removes the template prop, arms the ragdoll's think at `curtime + 20.0`
(`_DAT_1044eb0c`), optionally parents and owns it to the second parameter, attaches it through the
ManBat animlink arm (`0x1038f430`) and stores its handle in `m_hPickupTarget` (`+0x668c`).

## `CNPC_VMingXiao`'s throw chain — `0x103937d0`, `0x10396bc0`, `0x10398fd0`, `0x103990c0` (2026-09-13)

Four bodies 29c mapped onto one target name; they are four behaviours.

`0x103937d0` (271 bytes) `(task, tentacle, activity)` is the swing task: clear the navigator's
path, resolve `m_hMeleeWeapon`'s weapon (`+0xa0`), run slot 388 `+0x610` `(weapon, 0)`, and
**TaskFail `0x1f` when there is no active weapon**. Otherwise (re-read from the listing 2026-10-04,
spec 0002 V11; the decompile loses the arguments): `cc = GetEnemy() ? enemy+0x9c : 0`;
`translated = this->slot 376 (+0x5e0)(active->slot 361 (+0x5a4)(activity))`; `out = -1`; slot 331
`ChooseMeleeAttackSequence 0x10347180` `(active, cc, activity, &out)` -- handed the **untranslated**
activity (`0x10393881 PUSH EBX`). True with `out >= 0` -> slot 311 `+0x4dc`
`ForcePreTranslatedSequenceAndActivity(activity, translated, out)`; else **`RestartIdealActivity
0x10289ee0(activity)`** (`0x103938b6`) -- a refusal does not fail the task (this paragraph said it
did). Either way it then stamps `m_rflAttackTimers[t]` (`+0x66c4`) with `curtime +
FUN_103983d0(...)`.

`0x10398030`'s `param_2` tail (`0x10398295..0x103982f4`) is the same question asked of
`m_hMeleeWeapon` itself: the handle unset, stale or with no `+0xa0` answers false; else
`act = this->slot 376(weapon->slot 361(EBX))` with `EBX` the `0x112a`..`0x112d` the switch chose,
and slot 331 `(weapon, cc, act, &out)` -- here the **translated** activity -- false or `out < 0`
answers false.

`0x10396bc0` (385 bytes) is the pickup search. It answers 0 while `m_hThrowObject` (`+0x6718`) is
still LIVE, and while either of two curtime-gated cooldowns has not elapsed — `+0x66d8` is tested
first and `+0x66d4` second. Past both it clears condition 9 unconditionally, draws `RandomInt`
against the tuning record's `+8` cell and runs the pedestal search (`0x10398b20`) only on a draw
strictly below it. A live object then starts ignoring that object's collision, sets the throwable
mode to 1, stamps the selector trace with line `0xbc3` and answers schedule `0x167`; anything else
answers 0.

`0x10398fd0` (145 bytes) is the cleanup, and it is `0x103990c0`'s tail standing alone: remove and
clear `m_hPhysicsAnimlink` (`+0x6738`) if it resolves, then — **outside that guard** — clear
`m_hThrowObject`, re-arm the collision ignore at 0.75 s and set the throwable mode to 0.

`0x103990c0` (1,074 bytes) is the release, the largest body in the family. Order: drop the animlink;
then, only with a live enemy, take the held object's CENTRE (`+0x370`) as the launch point, solve a
lead point with the gravity cvar, add the enemy's own per-frame position delta
(`+0xa0..0xa2` minus `+0x9d..0x9f`) scaled by `_DAT_104454d0` = 0.5 **to the lead's Z alone**, and —
when `UTIL_AngleDiff` of the lead's yaw against this NPC's own leaves `[-20, +20]` — re-aim the XY at
exactly `yaw -/+ 20` while preserving Z. The speed is `quadratic * dist^2 + constant`, answering
`1000.0` (`_DAT_10447ee0`) when that sum is at or below 1000 and the sum itself otherwise — **1000 is
a floor, not a cap** — and a third cvar's `term * dist^2` is added to the Z after the XY scale. The
impulse goes through `CRagdollProp`'s `+0x428` or the physics object's `+0xa0`/`+0x9c` fallback. The
cleanup tail then runs on EVERY path, including the one with no enemy at all.

**Unrecovered:** `DAT_1093bbcc`, `DAT_1093bc14` and `DAT_1093b9fc` — the quadratic, constant and Z
terms of that speed. All three pointer cells live past `.data`'s raw size and no corpus function
constructs them; the port's seam answers `0.0f`, which makes every real call take the 1000 floor.
`thunk_FUN_102c36d0`'s ballistic solve was not walked either.

## `CNPC_VMingXiao`'s slot 166 and slot 100 — `0x10397000`, `0x10399fe0` (2026-09-13)

Slot 166 is `CanStandOn(CBaseEntity*)`, and `0x10397000` (152 bytes) is MingXiao's override. It walks
indices 0..5 reading TWO words per step — `m_rhProxies` (`+0x668c`, reached as `puVar5[-7]`, seven
dwords below) and `m_rhSeveredTentacles` (`+0x66a8`) — and answers FALSE on the first resolved entity
that equals the candidate. **MingXiao cannot stand on its own proxies or its own severed tentacles.**
On a full miss a non-null candidate is asked its own `IsStandable` (slot 164, `+0x290`) and a false
there answers false; a NULL candidate skips that call entirely and answers TRUE.

Slot 100 is `TestHitboxes(Ray_t&, unsigned, trace_t&)`, and `0x10399fe0` (383 bytes) is MingXiao's.
Three refusals in order: no model; `m_bHasTransformed` (`+0x6678`) clear; fewer than 7 hitbox sets
(`studiohdr+0x100`). Then hitbox set 0 — the master box — is tested first and unguarded, and a hit
ends the body; then the loop runs SEVEN times with the set offset walking `0xc, 0x18, ... 0x48` and
the tentacle index walking `0, 1, ... 6`, each iteration gated by `thunk_FUN_10398000(this, index)` —
"tentacle *n* is not severed". **Retail's fallthrough answers TRUE even when nothing was hit**: the
body's answer is "I handled the hitbox test", not "I hit".

## `CNPC_VTzimisce`'s pickup release — `0x103bf170` (2026-09-13)

99 bytes, and 29c's row named its target `VGargoyleGibCleanup` on the strength of the offset shapes
while flagging the owning species unconfirmed. The offsets settle it the other way: `+0x6670` is
`CNPC_VTzimisce::m_hPickupTarget` and `+0x6684` its `m_hPhysicsAnimlink`, and `thunk_FUN_103be0b0` is
the Tzimisce `CARRYING_BODY` flag write — the same shape as `CNPC_VHengeyokai`'s and
`CNPC_VManBat`'s release halves. In order: clear the pickup target to `-1`; re-arm the collision
ignore at 0.75 s; resolve, `UTIL_Remove` and clear the link handle — **the removal is called even
when the handle does not resolve**, on a null pointer, which is retail's own unguarded call; then
clear the carrying-body flag.

## The player controller — `CNPC_VPlayerController`, `CNPC_VFrenzyShadow`, `CNPC_VWolfMorph` (0019 story 5 fold A2)

`CNPC_VPlayerController` is a whole `CAI_BaseNPCTroika` under `CNPC_VVampire`, the player's scene
stand-in. `CBasePlayer::GetControllerNPC` (`0x10161a70`) builds it and writes, in order:
`spawnflags |= 4`, origin, angles, `SetOwnerEntity(player)` (`0x10161be1`),
`CopyAnimationDataFrom`, `m_flSeekDistBase (+0x63b4) = 4096`, `SetModel`, `DispatchSpawn`,
`m_fEffects |= 0x60` **on the controller** (`0x10161c21`; `0x40` is `EF_NODRAW`), slot 614
`ResetThinkTimers`, and the handle into `m_hControllerNPC` (`+0x1db0`). Its own bodies:

- `Spawn` `0x103a4510`: `CNPC_VVampire::Spawn` (direct), `AddClassRelationship(1, 3, 0)` (the
  player's class, liked), `m_bForceFrequentThink = 1` (a direct byte store), `SetName("playercontroller")`,
  `AddFlag2(0x10)`. Shared by `CNPC_VWolfMorph`; `CNPC_VFrenzyShadow`'s `0x10375c50` adds
  `CapabilitiesAdd(0x200000)` first.
- `NPCInit` `0x103a4580`: Troika `NPCInit` (direct), `+0x641c = 0`, the five law thresholds 999999,
  the dead `SecureType` bytes (`+0x6360/6361/6364`), `+0x6368 = 0`, investigate modes 6/6, slot 416
  `SetForceFrequentThink(1)`, `m_hFriendPlayer` = the owner or -1, `+0x5b84 = 0`, senses off,
  `+0x1480 = 0`.
- `NPCThink` `0x103a4700`: Troika `NPCThink` (direct), then a tail JMP to slot 614
  `ResetThinkTimers` — the stand-in thinks every frame. Shared by `CNPC_VWolfMorph`.
- `PreSelectSchedule` `0x103a46b0`: `+0x1b2c = 2`; IDLE answers `0x6b` (107, resolved up the
  `CNPC_VVampire` space to `SCHED_TROIKA_IDLE_DISPOSITION`), every other state tail-jumps to the
  Troika body. Shared by both children.
- Slot 72 answers false (no discipline targets it), `Classify` 2, the four view/aim cones false,
  the ten sound slots 488-497 bare `RET`s, `IRelationType` `0x103a48b0` (`social.md`), and the
  forwarding arms 245/246/300 below.

`CNPC_VFrenzyShadow` restores the base cones (362-365), answers `Classify` 3 and carries its own
`GatherConditions` (local condition 121 on more than one hostile), `SelectSchedule` (353
`SCHED_VFRENZYSHADOW_FEED` in COMBAT), `StartTask` (task 330 `ATTEMPT_FEED` asks the owner's
`CBasePlayer::Replenish(1)`), `BestEnemy` (`senses.md`) and a melee quartet 599-602 with every gate
dropped. `CNPC_VWolfMorph` answers every activity with `ACT_WOLF_MORPH` (`0x1145`), selects local 344
`SCHED_VWOLFMORPH_MORPH` until it is running it, and empties slot 588. Each child loads its own
schedule space (local 344 names a different schedule in each); the controller shares
`CNPC_VVampire`'s.

**Which body is drawn — the pawn (recovered 2026-09-27).** `m_fEffects |= 0x60` puts `EF_NODRAW`
(`0x40` in this build) on the stand-in, and `CBaseEntity::ShouldTransmit` (`0x100ab020`, reached
through `CBaseCombatCharacter::ShouldTransmit` `0x103407b0`) returns false on it, so the controller
never reaches the client (the only exceptions are a future stamp at `+0x90` and a non-zero
`m_clientAuraCount`, neither with a producer here). The pawn is drawn instead, carrying the stand-in's
state: `CBasePlayer::PostThink` (`0x1016be10`) at `0x1016c510`..`0x1016c672` — after `LAB_1016c41d`,
so it runs even when the `m_iPlayerLocked` / not-alive / `IsObserver` gate skipped the live body —
resolves `m_hControllerNPC` and, with `!m_Local.m_bWolf` (`+0x1edc`), copies controller -> pawn
`m_nSequence` `+0x6f0`, `m_flAnimTime` `+0x174`, `m_flCycle` `+0x6f8`, `m_flPlaybackRate` `+0x6f4`,
0x30 dwords of `m_AnimOverlay[]` from `+0x734` and 0x15 dwords of `m_Flinch[]` from `+0x7f4`
(`0x1016c5be`..`0x1016c612`); then slot 64 `SetLocalAngles(ctrl.GetAbsAngles())`, `SetAbsVelocity(ctrl
+0x3bc)` (after `CalcAbsoluteVelocity` when the controller's `m_iEFlags` bit 12 is set) and slot 62
`SetLocalOrigin(ctrl.GetAbsOrigin())`; then slot 277 `SetViewtarget(ctrl.slot 278 GetViewtarget())`.
In wolf form the three transform writes run the other way (pawn -> controller) and nothing else is
copied. Both arms end in `Relink(pawn)` and the frenzy-grapple check: `ctrl.Classify() == 3 &&
m_iCurFrenzyCount > 0 && m_bFrenzyHunger && Replenish(1)` -> `m_bIsFrenzyGrapple = 1;
0x1033f6d0(player)` (the frenzy end). The client draws the local pawn only per
`C_BasePlayer::ShouldDrawLocalPlayer` (`0x100a7a50`): an adopted cine camera's `m_bDrawPlayer`, else
third person — a controller scene adds no term. Input: `CHL2_Player` slot 462 (`0x10351090`) zeroes the
whole usercmd while the controller resolves and `!m_bWolf` (wolf: only `+0x20`/`+0x24`/`+0x28`).
Release: `0x101618e0(player, copyAnim, copyXform)` makes the same animation copy and the same transform
copy (no view target), then `ThinkSet(ctrl, SUB_Remove 0x101c0b10)` at `curtime + 0.01` and
`m_hControllerNPC = -1` — no effects, model, skin or disposition write. (1,1) callers:
`InputRemoveControllerNPC` `0x102272b0`, `Event_Killed` `0x10163af0`, the wolf returns `0x1016bd50` and
PostThink's wolf arm; (0,0): `GetControllerNPC`'s class-mismatch arm, the frenzy end `0x1033f6d0`,
the player teardown `0x10170090`. The stand-in is drawn only in Protean wolf form (`0x60` cleared on
`npc_VWolfMorph`, render fx `0x1f` / pawn `0x1e` crossfade); the frenzy shadow `0x10161fc0` keeps
`EF_NODRAW`. **Unrecovered:** the `CUserCmd` field names behind `0x10351090`'s offsets; whether the
client's own `ShouldDraw` honours `EF_NODRAW` on an aura-transmitted controller.

**Port shape.** `FElysiumNpcPlayerController` / `FElysiumNpcFrenzyShadow` / `FElysiumNpcWolfMorph`;
`events_player.CreateControllerNPC` runs the sequence above in
`FElysiumEntityWorld::CreatePlayerControllerEntity`. `FElysiumNpc::IsTransmitted` is the
`ShouldTransmit` early-out over the kernel `EffectsWord`: the stand-in's skeletal body stays live — its
animation host, motor and hull — and is never drawn (the pose layer's clip-commit reveal honours the
gate through `ElysiumNpcVisual::UntransmittedBodyTag`). `FElysiumEntityWorld::UpdatePlayerFromController`
is the PostThink block, run on the post-move tick after the weapon frame and the melee walk; the pawn's
body draws the stand-in's evaluated pose through `IElysiumEmbodiment::SetPlayerBodyPoseSource` (the
presentation of `m_nSequence`/`m_flCycle`/`m_flPlaybackRate`, which the player carries no member for),
and the kernel words the player does carry (`AnimTime`, `AnimOverlay`, `Flinch`, `Viewtarget`,
`Velocity`, the transform) are copied. The usercmd wipe is applied where the port builds the frame's
command (`AElysiumPlayerController::TickActor` -> `FElysiumPlayer::ApplyControllerUserCmdWipe`, ahead
of `SetupMove`'s immobilize pair): the whole command, `IN_USE` and the look included, and the move
words only in wolf form. It is NOT `IsMobile` (`m_bIsImmobilized`), which also stands for
`m_iPlayerLocked`'s skip of the live PostThink arm — retail still runs `ItemPostFrame` and the
melee-movement stop during a controller scene, on the empty command. `bWolf` and the frenzy-grapple
arm are seams answering nothing.
`RemovePlayerControllerEntity(EElysiumControllerRelease)` is `0x101618e0`, called (1,1) by
`RemoveControllerNPC` and the player's `OnKilled`, (0,0) by the class-mismatch arm. Named
modernization: the stand-in's Unreal motor ignores other character capsules (no retail solidity step;
the pawn is pinned inside it). Named divergences: removal kills the stand-in in the frame of the request
rather than through `SUB_Remove` (`0x101c0b10`) 0.01 s later; and the per-frame pin writes the pawn's
view as well as its entity angles (`TeleportPlayer` -> `SetControlRotation`, because the pawn's body
yaw follows its view), where retail's slot 64 writes entity angles only and a teleport's eye re-aim
(`0x10178590`) survives PostThink.

**The pose follow is a presentation swap with its state readers wired.** The pawn's body drawing the
stand-in's pose stands for the three scalar words the player has no member for, and the readers of
those words read through it: `UElysiumEntityBodies::GetBodyClipPhase` resolves a following body to its
leader (`ElysiumNpcVisual::PoseHostOf`), so the player's own anim-event walk, attack lock and grapple
readers see the stand-in's clip — retail's pawn runs `StudioFrameAdvance` (slot 250, `0x1016c2bf`) and
`DispatchAnimEvents` (slot 258, `0x1016c2e5`) on the sequence PostThink copied onto it, so the
stand-in's clip events (the 4050/4051 camera ids, weapon and combat ids, footfalls 2050-2053) reach
`CBasePlayer::HandleAnimEvent` `0x10178a10` as well as the stand-in's own handler. The port's live think pass (`FElysiumNpc::Think`) dispatches none of slots 431, 433, 437 or
442 for any class, so `NPCThink`/`GatherConditions`/`PreSelectSchedule`/`StartTask` are reached by
their slot callers and tests. The visible consequence: retail's `GetNewSchedule` `0x1028a260` asks
slot 437 before slot 438 (and 438 only when 437 answers 0), so an idle retail stand-in takes `0x6b`;
the port's maintenance pass goes straight to 438, and a live idle stand-in runs the port's generic
selector instead (story 8 residue).

## The `CNPC_VPlayerController` line's damage and death arms — `0x10376ae0`, `0x10376b10`, `0x10376b50`, `0x103a4950` (2026-09-13)

Four slot arms on `CNPC_VFrenzyShadow` / `CNPC_VPlayerController` / `CNPC_VWolfMorph`, all routed
through `vt+0x184` — **slot 97 `GetOwnerEntity`**, not a stored sub-object: the controller is OWNED
by the player it stands in for (`CBasePlayer::GetControllerNPC` `0x10161a70` calls
`SetOwnerEntity(player)` at `0x10161be1`), so every forward reaches the PLAYER. `OnTakeDamage`
(slot 142, `0x10376ae0`, the frenzy shadow's) forwards the packet to the owner's slot 142 (`+0x238`)
and **always answers 0**. `OnTakeDamage_Alive` (slot 390, `0x10376b10`) forwards one indirection
deeper — through the owner's `+0x9c` combat character to its slot 390 (`+0x618`) — and also always
answers 0. `Event_Killed` (slot 144, `0x10376b50`) is **three bytes**: an empty body that ignores
its parameter, so a frenzy shadow's death handling is fully suppressed versus
`CAI_BaseNPC::Event_Killed` — no ideal-state change, no corpse, no outputs. `Event_TookLife`
(slot 300, `0x103a4950`, shared by all three classes) is guarded on the owner and on its `+0xa8`
(`m_pPlayer`, the self-downcast only `CBasePlayer` fills); inside both it builds the killed entity's
debug name (or `"UNKNOWN"`) into `"CNPC_VPlayerController::Event_TookLife %s"` and calls
`0x1017e150(player, 4, -1.0, reason)` — the PLAYER's criminal-level setter, level 4 with a derived
duration. A kill the stand-in makes is the player's crime.

**Corrected by 0019 story 5 fold A2** (both findings from the listing): `+0x184` is slot 97, so the
"unrecovered controller object" is the owner; and `0x1017e150` is `SetCriminalLevel`, not an AI
event dispatch of type 4. The port stands the four bodies on `FElysiumNpcPlayerController` /
`FElysiumNpcFrenzyShadow` and routes `Event_TookLife` through `ElysiumLaw::SetCriminalLevel`
(`Elysium.Substrate.NpcKernelDamage.PlayerControllerLine`).

### The expresser factory `0x10312cd0`

_Recovered 2026-09-13, story 29c-1._

Slot 424 on `CAI_BaseHumanoid` and `CAI_ExpressiveNPC`. The base gate `thunk_FUN_1027cae0(this)`
must pass; then vtable `+0x91c` (slot 583) creates the expresser and the result is stored at
`m_pExpresser` (`+0x5f48`) **before** the null test, so a failed factory still overwrites the word.
A null result answers false. Otherwise the expresser's `+0x04` is back-linked to this NPC, its
`+0x10` to `&this->+0x5f44`, `thunk_FUN_10312b20` finishes the setup, and it answers true.

**Unrecovered:** the expresser object's layout beyond those two back-links, and what `+0x5f44` is —
the census leaves both unbound.

## The NPC makers — `CNPCMaker`, `CNPCMaker_Fleshpile`, `CNPCMaker_Zombie` (0019 story 5 fold A4)

_Recovered 2026-09-27, story 5 fold A4._

**A maker IS a Troika NPC.** `CNPCMaker`'s constructor chain runs `CAI_BaseNPC`'s (`0x1027c300`: the
AI-list add on `DAT_1090fe10` through `0x10265310`, flag `0x20` at `+0x4c`, `m_pBaseNPC +0x94 =
this`) and the Troika's (`0x1028d230`: flag `0x40`, `m_pTroika +0x98 = this`). Its vtables
(`0x1049f404`, `0x1049ffe4`, `0x104a0bbc`) carry 21 / 15 / 14 own bodies — `classes.md`'s 17 / 12 /
10 is the name-prefix count and misses the unnamed fills of the four slots the maker adds past the
Troika's table, 617 `MakeNPC` (returns the child), 618 `CanMakeNPC`, 619 `ChildPreSpawn` and 620
`ChildPostSpawn`. The port stands them as `FElysiumNpcMaker : FElysiumNpc`,
`FElysiumNpcMakerFleshpile` and `FElysiumNpcMakerZombie` beneath it.

**What makes it a marker is its own slot table**, and nothing else:

| Question | Maker's answer | Retail body |
|---|---|---|
| Spawn (103) | `SetSolid(SOLID_NONE)`, live = 0, slot 104, InfChild => Fade, the think installation, Relink, `m_flGround` = 0; no `SetModel`, no base `Spawn` | `0x1034afe0` / `0x1034c020` / `0x1034cc60` |
| Precache (104) | the model, then `CAI_BaseNPC::Precache` `0x1027bb50` DIRECT (not the Troika's) | `0x1034b160` / `0x1034c180` / `0x1034cde0` |
| Activate (113) | empty — no disposition, relationship, senses, admission or `NPCInit` | `0x1034b140` |
| Transmit (86) | false — never drawn | `0x1034af10` |
| Cones (362-365), witness (587), discipline target (72) | false | `0x1034ae70`.., `0x1034aed0`, `0x1034aef0` |
| Think | the INSTALLED think (`m_pfnThink`), never `NPCThink` | `0x1034bbf0` / `0x1034c8b0` / `0x1034d2d0` / inert `0x101c0b60` / NULL |

So `NPCInit` and `NPCThink` never run on a maker: its `m_bDisableAI` and the console `g_AIDisabled`
bit (both `NPCThink`'s gates) never stop it — a maker keeps its cadence through a feed — and
`DisableThink` on a maker only reaches its children, through `MakeNPC`'s `SetDisableAI` copy.

**It is in every AI-list walk.** `SetAIEnabled(true)` `0x10265680` and `WakeNpcsNear` `0x1028d820`
(teleports, `point_teleport`, `teleport_player`) dispatch slot 614 `ResetThinkTimers`
(`0x102c23f0`, which no maker overrides) on it, pulling `m_flNextThink` to now; so do an unhide
(the Troika `ScriptUnhide` tail `0x102c1ec0`) and `SetDisableAI(false)` `0x1029f300`. The pulled
think is the INSTALLED one: an enabled maker attempts a spawn at once, a disabled one runs the
inert `RET` (base, fleshpile) or nothing (zombie), a depleted one nothing.

**The installed think.** `Spawn` installs the class's think when enabled — base `MakerThink`
`0x1034bbf0`, fleshpile `0x1034c8b0`, zombie `0x1034d2d0` (instruction for instruction
`MakerThink`'s code) — at `freq + curtime` (the zombie adds `RandomFloat(1, 2)`), and when disabled
the inert think (base, fleshpile) or NULL (zombie) without writing `m_flNextThink`. `Enable`
`0x1034b490` refuses a depleted maker, clears the latch and installs the **base** think on every
class (`0x1000696a`), `m_flNextThink = curtime`; `Disable` `0x1034b4d0` sets the latch and
`ThinkSet(NULL)`; `MakeNPC`'s depletion arm `ThinkSet(NULL)`s too. `MakerThink` dispatches slot 617
`MakeNPC(0)` virtually and re-arms at `freq` after a child or on a full live ceiling, else at
`RandomFloat(1, 2)`. The fleshpile's own think only re-arms — see `CNPCMaker_Fleshpile` below.

**`DeathNotice` (139, `0x1034bc90`).** Once per child (`+0x44c`): `child->IsAlive()` (slot 158)
refunds `m_iMaxNumNPCs` **unconditionally** (`1034bcb7 INC [ESI+0x6660]`, no `m_bInfChild` test),
else `OnNPCDied(child, this)`; then `OnLastNPCDied` when depleted; then `m_cLiveChildren - 1`,
clamped at 0. The fleshpile's `0x1034c8e0` moves Andrei's runner budget first (RTTI
`CNPC_VTzimisceRunner`, `0x10625270`) and then calls it directly.

**What the port suppresses, and by which retail word.** The port's own consumers that treat every
NPC as a body: the maker's `SOLID_NONE` (`FElysiumEntity::IsRetailNotSolid`) keeps it out of the aim
and swing contacts, the hull traces (`TracePlayerSolid`, the stealth-kill `TraceNpcHulls`), the
grapple stand test and the maker spawn box (its own included); slot 72 keeps it out of discipline
targeting (`0x101e1a60`'s arm, now asked by the port's `AcquirePrimary`/`BuildTargetSet`); the
targetable byte `+0x1480` (set only by `NPCInit`) keeps it out of the feed probe, the melee pick and
`BestEnemy`; it has no body or motor, so the per-frame body loops skip it. Left to run as retail's
walks: the AI toggle and the teleport wake above, and the other NPCs' `Look` candidate walk (retail
walks the AI list; whether a looker's relationship ever hates a class-4 maker is **unrecovered** —
brief open question 1).

**The world questions** (`Map/ElysiumNpcMakerGeometry.h`, corrected 2026-09-27 after the commit-A
map smoke). *Ground:* `MakeNPC` `1034b7ba`..`1034b8a3` traces from `GetAbsOrigin()` down 2048 units
(`_DAT_1046bacc`), `CTraceFilterSimple(this, 0)`, mask `0x2400b`, and stores `tr.endpos.z`
(`1034b881`); a start on the floor surface is a fraction-0 hit, so a maker on its floor caches its own
Z (a start inside the solid is `startsolid`, `endpos` = the start). **Named modernization:** Unreal's
line trace ignores the surface it starts on, so the port lifts the start by `DistEpsilon`, clamps the
answer to the origin's Z and answers the origin's Z on `bStartPenetrating`. `0x2400b` asks whether a
brush blocks an NPC, which the port asks on `ECC_Pawn` (the `ElysiumSig_*` world profiles block it
where retail's NPC-solid contents do), skipping character bodies (`CONTENTS_MONSTER` is not in the
mask); the first cut traced `ElysiumUse`, which every `ElysiumSig_*` world profile ignores, and fell
through the shipped floors. In game: `thug_maker` (authored 2 units above its floor) caches the floor,
-101.6 cm; `sp_giovanni_2b` `Zombie_Spawner_1` caches 0. *Box:* `CanMakeNPC`
`1034b686`..`1034b72b` builds `[origin.xy ± 34] x [origin.z, origin.z]` — flat at the maker's own Z —
and `UTIL_EntitiesInBox(.., 2, .., 0x2080)` counts only `FL_CLIENT | FL_NPC` entities (the player,
and NPCs `NPCInit` flagged); the port used to float the box at the cached ground and count props and
brush bodies. The port measures an `FL_NPC` body by its standing hull.

**Named divergences in the port.** *A child its own spawn removes.* Retail has no test after
`DispatchSpawn` (`1034ba7a`): a `UTIL_Remove` inside the child's spawn is deferred, so the child is
owned, named, counted (`m_cLiveChildren`, `m_iMaxNumNPCs`, the depletion `ThinkSet(NULL)`),
`m_bCameFromSpawner` is set and `MakeNPC` returns it; its end-of-frame `UpdateOnRemove` then dispatches
slot 139, whose `IsAlive` arm refunds the total. The port's `Kill` is immediate and latches the owner
notice before an owner exists, so the port drops such a child instead: `MakeNPC` answers null, nothing
is owned or counted, no `ThinkSet(NULL)`, and `MakerThink` re-arms at `RandomFloat(1, 2)` instead of
`+freq` (the fleshpile also leaves Andrei's budget alone). Unreachable in shipped content: the one
removal arm in an NPC's own spawn is `Precache`'s `LoadedSchedules` refusal, which never fires here.
*A hidden maker.* The port's `RunThinks` skips every inert (script-hidden or `StartHidden`) entity, so a
hidden maker never thinks, while retail's `Enable` on a hidden maker installs the base think at now
and it spawns while hidden — a pre-existing world rule, not this fold's (`calm_cop_front_5` in
`sm_hub_1` is such a maker: `StartHidden 1`, enabled). Retail's `ScriptHide` `0x100a8710` saves
`m_pfnThink` into `m_pfnScriptSavedThink` (`+0xe4`), installs NULL and parks `m_flNextThink` at
`FLT_MAX`; `ScriptUnhide` `0x100a8990` re-installs the saved think (`ThinkSet(saved)`), overwriting one
`Enable` installed meanwhile. The port keeps the maker's installed think across a hide (it carries no
`+0xe4`), which is the same answer once the maker is visible again.

**Unrecovered:** `+0x1f0`, which `MakeNPC`'s depletion arm zeroes after `ThinkSet(NULL)`
(`m_pfnUse` by the datamap layout; the port carries no use-function pointer on a maker), and
whether a sense walk ever admits a maker (above).

## `CNPCMaker_Fleshpile`'s two overrides — `0x1034c2d0`, `0x1034c8e0` (2026-09-13)

_Recovered 2026-09-13, story 29c-1, family Species._

Both bodies talk to the ONE `npc_VAndreiBlood` in the level through the file-static `DAT_10938040`,
which `MakeNPC` fills lazily by `FindEntityByClassname(NULL, "npc_VAndreiBlood")` plus a dynamic
cast the first time it is asked and never clears. Its two counters are named by the class's own
datamap: `m_iActiveRunnerCount` (`+0x66b8`) and `m_iKillCount` (`+0x66bc`). Both are declared
`FIELD_INTEGER` and every body that touches them uses x87 float instructions on them.

**29c's walk of `0x1034c8e0` reads the pair as "the global bounds ... grows/shrinks a global
volume". It is not a volume — it is a runner BUDGET**, and the three bodies that touch it are one
system: `0x1035e920` asks whether another runner may be made, `0x1034c2d0` refuses when the count
has reached the cap and increments it on success, and `0x1034c8e0` decrements it and counts the
kill.

### `0x1034c2d0` — slot 617 `MakeNPC`

The `param_1` bypass flag skips the **whole** admission, blood budget included — the same shape the
base `CNPCMaker::MakeNPC` has. The three admission terms the base does not have are:

1. the Andrei singleton must exist, else the body answers NULL outright;
2. `_DAT_10452dc4` = **2.0** `<= m_iActiveRunnerCount` refuses — at most two live runners;
3. a spawn-box overlap test (`0x101cca80`, mask `0x2080`) over a `+-_DAT_1049ffac` = **34.0** unit
   box around the maker, whose FLOOR is `m_flGround` (`+0x66b8` on the maker, a different class at
   the same offset) unless `m_bNoDrop` (`+0x66c4`) is set, in which case it is the maker's own Z.
   **`Flag_NoDrop` is consumed here and nowhere else in the binary** — the base maker declares the
   key and never reads it.

`m_flGround` is cached on first use by a downward trace of `_DAT_1046bacc` = **2048.0** units, and
the cache test is `== 0.0` rather than a sentinel, so a maker standing at exactly Z = 0 over ground
at exactly Z = 0 re-traces on every attempt.

The base's live-children, scene-block, visibility, view-cone and PC-distance terms are **absent**: a
fleshpile maker ignores all five. **Corrected 2026-09-27 (story 5 fold A4):** everything after
admission is this body's OWN, not the base's. The classname lookup and the `+0x98` Troika check with
its `"Non Troika Ent in NPCMaker!\n"` / `"NULL Ent in NPCMaker!\n"` warnings; `+0x1584`; `OnSpawnNPC`;
`SetAbsOrigin`/`SetAbsAngles` from the maker; the spawnflags **ORed** with 4, or `0x204` when
`m_bFade` (`+0x66c2` is `m_bFade`, not a view-cone bit); then the copy set — `m_spawnEquipment`
`+0x5dec`, `m_SquadName` `+0x5da8`, `SetHintGroup(+0x5db0)` (`0x102781e0`), `m_altEquipment`
`+0x1a98`, the perception triple (twice), `SetModel(GetModelName())`, `+0x64bc`, `+0x6558`
(disposition), `+0x64c4` (camera), the `+0x62d8` place-group string (not its parsed `+0x62dc`),
`+0x6338`/`+0x633c`/`+0x6344`, the five police thresholds, `+0x63ac`, `+0x63d9`, `m_nSkin` `+0x670`
and `m_statTemplate` `+0x10e4`; slot 619; `DispatchSpawn`; the owner; the optional `SetName`; slot
620. There is NO map-data replay, NO `m_cLiveChildren` / `m_iMaxNumNPCs` change and NO
`m_bCameFromSpawner`. The last thing it does is `m_iActiveRunnerCount += _DAT_104454c0` = **1.0**
(on the bypass path with no Andrei, a null dereference in retail; the port skips it).

**Its think does not spawn.** `CNPCMaker_Fleshpile`'s installed think `0x1034c8b0` is four
instructions: `m_flNextThink = m_flSpawnFrequency + curtime`. The spawner is Andrei:
`CNPC_VAndreiBlood::StartTask` `0x1035d1b0` case `0x154` (the summon: sound, activity `0x113a`,
`StartSummonEmitter`) finds the nearest `npc_maker_fleshpile` within 1024 units
(`FindEntityByClassnameNearest` `0x100f7d50`, squared distance strictly below, first-listed on a
tie), RTTI-casts it to `CNPCMaker_Fleshpile` (`0x1062520c`) and dispatches slot 617 `MakeNPC(0)`.
`Enable` installs the BASE think on a fleshpile too (`0x1034b490`), whose slot-617 call is this
body — so an enabled fleshpile maker does spawn, on the base cadence, within the budget.

### `0x1034c8e0` — slot 139 `DeathNotice`

RTTI-casts the dying child to `CNPC_VTzimisceRunner` and, when its `m_bDeathNoticeProcessed`
(`+0x6671`, datamap) is clear, decrements `m_iActiveRunnerCount`, increments `m_iKillCount` and
raises the flag. The once-only flag is what stops a corpse that is re-notified being counted twice.
The base `CNPCMaker::DeathNotice` (`0x1034bc90`) is then called on **every** path, cast or no cast,
so the maker's own live-children bookkeeping still runs.

## `CScriptedTarget::Spawn` — `0x1034d6e0` (2026-09-13)

_Recovered 2026-09-13, story 29c-1, family Species._

Slot 103 on a class `classes.md` gives NO entity classname, so no map stands one and the census can
never answer it for a spawned entity. The body, in order: a one-time global registration of
`GetInteractionID()` into `DAT_10938054`; a VPROF scope carrying `"CBaseEntity::SetSolid"` and this
entity's `m_iName` (`+0x26c`); `SetSolid(&m_Collision, SOLID_NONE)`; `Relink()`;
`m_vLastPosition` (`+0x5f44`, three floats) cached from `GetAbsOrigin()` (vtable `+0x364`, slot 217);
`thunk_FUN_1034d5b0(this)` only when `m_iDisabled` is zero; and `AddFlag2(0x10)`.

The ORDER matters: the position is cached before the enabled-arm think is armed, so the think reads
a position that is already current.

**Unrecovered:** what `AddFlag2`'s `0x10` bit means on `m_fFlags2`, and what `0x1034d5b0` does past
arming the think.

## Story 29c-1, family Closure — slot 355, the grapple-end callback — `0x1026cf90` (2026-09-13)

143 bytes, filled by all 77 classes of the family with the same body, and no call site in the corpus
names it — it is reached only through a vtable index. With the census's names for the three words it
touches (`+0x1538 m_GrapplePartner`, an EHANDLE; `+0x153c m_GrappleRole`, an int; `+0x1540
m_GrappleType`, an int) the whole of it is:

```
ent = m_GrapplePartner.Get();                        // null when the handle is stale
if (!(ent && m_GrappleRole != -1 && m_GrappleType == 8))
    m_OnFedUponEnd.FireOutput(ent, this, 0);         // +0x5c20, activator = the partner
FUN_10007ea0(this);
```

Two readings matter. The output fires on **`this`** — the fed-upon body — with the partner as
activator, so it is the victim's own `m_OnFedUponEnd` a map wire reaches. And the suppression is a
conjunction of all three words: a stale partner handle, an unset role, or any grapple type other than
8 all still fire. Feeding is grapple mode 0 (`CBasePlayer::Replenish` `0x10168320` reaches
`StartGrappleAttack(victim, 0)`), so an ordinary feed is not type 8 and does fire. Since the grapple
block is entered on BOTH parties, the slot is dispatchable on both.

**Unrecovered:** which grapple type 8 is, and what `FUN_10007ea0` does. The port fires
`OnFedUponEnd` from one place only — `FElysiumCombatCharacter::CompleteFeedTransaction`, on the
feeder, onto the victim — because a map that wires the output counts the fires; the slot forwards
there rather than becoming a second producer.

## Story 29d, family Precache10 — slot 104 `Precache`, the base, the Troika body and its twenty-three species arms — `0x1027bb50`, `0x10298ad0` (2026-09-14)

_Recovered 2026-09-14, story 29d._

Twenty-eight `rule` rows fill **two** retail functions on the NPC line plus **twenty-three** species
overrides of one slot and **three** `CNPCMaker*` bodies. The slot is 104; the two base functions are
`CAI_BaseNPC::Precache` `0x1027bb50` and `CAI_BaseNPCTroika::Precache` `0x10298ad0`, and the latter
is what the vtable carries.

**`CAI_BaseNPC::Precache` `0x1027bb50`, three steps.** `m_spawnEquipment` (`+0x5dec`) goes through
`UTIL_PrecacheOther` (`0x101d0ec0`) when it is non-null AND its first two bytes differ from
`DAT_105399a0` — which is the one-character string **`"0"`**, retail's authored "none" sentinel, so
the test is "set, and not the sentinel". Then slot 452 `LoadedSchedules` (vtable `+0x710`) decides
the rest: **false** prints `DevMsg("ERROR: Rejecting spawn of %s as error in NPC's schedules.")` with
`CBaseEntity::GetDebugName`, `UTIL_Remove`s the entity (`thunk_FUN_101cd940`) and **returns without
chaining the base at all**; true falls through to `CBaseCombatCharacter::Precache`, which is
`CBaseCombatCharacter::PrecacheOnce` `0x1033f750` — the once-guarded GLOBAL block of discipline,
damage-effect and HUD emitters every character shares, not an NPC-kernel row.

**`CAI_BaseNPCTroika::Precache` `0x10298ad0`, in order.** An unset or empty model keyfield (slot 9,
vtable `+0x24`) is replaced through slot 212 `SetModelName` (`+0x350`) with
`"models/error/error.mdl"`; the keyfield is `PrecacheModel`d (`*DAT_1070b22c+0x34`, preload 0) and
the returned index handed to slot 10 `SetModelIndex` (`+0x28`); `m_altEquipment` (`+0x1a98`) is
`UTIL_PrecacheOther`d unless it is `"0"` **or** the exact fifteen-byte literal `"item_w_unarmed"`
(two exclusions where the base body has one); `CAI_BaseNPC::Precache` is chained **directly**, with
no argument; a set `m_iDialog` (`+0x0128`) builds `"sound/character/%s"` into a `0x104` stack buffer,
is chopped, lowercased and glob-precached twice; `+0x64e8 m_iDispositionModelIndex` takes the
`CDispositionTable` row `0x100ec640` builds for this model; and slot 608 (`+0x980`,
`SetAttackCoordinator`) is dispatched with `"Normal"` last. **Every Troika NPC is bound to the
coordinator named `"Normal"` at precache**, which is what makes the coordinator's five entry points
reachable at all.

**The dialogue chop is FOUR characters, not five.** Read off the listing at `10298bf8`..`10298c04`:
`REPNE SCASB / NOT ECX / DEC ECX` leaves `strlen(buf)` in `ECX`, `SUB EDX,0x4` puts `buf - 4` in
`EDX`, and `MOV byte ptr [ECX + EDX*1],AL` writes the NUL at `buf[strlen - 4]`. `Q_strnlwr` then runs
over the **new** length. The checklist's walk claimed five and is corrected here.

**`0x101d0f10` takes FOUR arguments, and its three call sites pass three different pairs.** The
decompiler shows two; the listing shows `(dir, ext, starPrefix, soundFlag)`. A null or empty
directory answers 0, and so does one whose first three characters match `DAT_105a0410` or whose first
character matches `DAT_105a040c`; otherwise `FindFirst`/`FindNext` walk `"%s/*%s"` and every
non-directory hit is precached as a sound under `"*%s/%s"` (when `starPrefix`) or `"%s/%s"` of
**`dir + 6`** — retail skips the literal `"sound/"` every call site opens with — with `soundFlag` as
the precache flag, and the return is the number of hits. The Troika body passes `(1, 0)` and `.wav`
then `.mp3`; `CNPC_VNewscaster` passes `(0, 0)` and `.mp3` then `.wav`; `CNPC_VWerewolf` passes
`(0, 1)` and `.wav` twice. `DAT_10598a30` is `".wav"` and `DAT_10548ed4` is `".mp3"`, pinned by
`FUN_101b1120`, which is SDK-2013's `CSoundEmitterSystem::EmitSound` instruction for instruction
(`Q_stristr(name, ".wav") || Q_stristr(name, ".mp3") || name[0] == '!'`).

### The twenty-three species arms — `0x10358ec0` … `0x103df120`

_Recovered 2026-09-14, story 29d._

Every arm is an override of the SAME slot. The recovered fact that separates them is **where each
one chains** and **what it appends**. Sixteen chain the Troika body first; five chain a base last;
`CNPC_Crow` chains first and then hard-codes a model; `CNPC_VMingXiaoTentacle` writes its model
fallback before chaining. Four of the classes (`CNPC_Crow`, `CGeneric_NPC_bathack`,
`CGenericSabbat_NPC`, and `CGenericNPC`, which chains nothing) are `CAI_BaseNPC`-line classes, so the
body they reach is `0x1027bb50` and the Troika half never runs for one.

#### `CNPC_Crow` — `0x10358ec0`

24 bytes, and the only body in the band whose two calls are inverted: `CAI_BaseNPC::Precache` FIRST,
then `PrecacheModel("models/crow.mdl", 0)`. No model keyfield is read and no fallback is set, so a
crow's model is hard-coded and a map cannot override it.

#### `CGeneric_NPC` — `0x10359f70`, `CGeneric_NPC_bathack` — `0x1035ade0`, `CGenericSabbat_NPC` — `0x1035b5d0`

Three classes with three **separate** `.rdata` copies of the same four wav names
(`npc/metropolice/alert1|surprise1|die1.wav` and the four `npc/citizen/pain1..4.wav`), at
`0x10629a18`, `0x10629d80` and `0x1062a004`. `CGeneric_NPC` falls an unset model keyfield back to
**`models/character/npc/sabbat/sabbat_female.mdl`** (`0x10629c00`) — and `CGenericSabbat_NPC`'s own
pointer `0x1062a000` resolves to the *same string*. The bathack hard-codes `models/bats.mdl` and
reads no keyfield at all. `CGenericSabbat_NPC` opens with `thunk_FUN_10207e60`, which resolves this
entity's char template (`0x10207c40`) and precaches the template's `+0x78` (`0x101d4f20`). All three
chain LAST — the Troika body for `CGeneric_NPC`, `CAI_BaseNPC::Precache` for the other two.

**The decompiled C shows one of the bathack's four sound calls with a single argument. The listing at
`1035ae13` shows `PUSH 0x0` before all four.** There is no stack quirk; the same artifact appears in
`CNPC_VTest` and is corrected there too.

#### `CNPC_VAndreiBlood` — `0x1035cb90`

The Troika body, three sounds (`Character/Boss/Andrei/TeleportOut.wav`, `TeleportIn.wav`,
`Summon.wav`), then three emitters with preload **1**. **The emitter names carry a hyphen before
`Emitter`**: `Andrei_Teleport_Out-Emitter`, `Andrei_Teleport_In-Emitter`, `Andrei_Summon-Emitter`
(`0x1062b04c`, `0x1062b02c`, `0x1062b010`), each also named by `CNPC_VAndreiBlood::StartTask`. The
checklist's walk spelled all three with an underscore.

#### `CNPC_VAsianVampire` — `0x10360bc0`

A scope-trace frame, the Troika body, exactly one `UTIL_PrecacheOther("item_w_avamp_blade")`, and the
frame popped. That single weapon is the whole species payload.

#### `CNPC_VBach` — `0x103637b0`

The Troika body, then **three weapons before five sounds**: `item_w_grenade_frag`, `item_w_katana`,
`item_w_rem_m_700_bach`, then `Character/Boss/Bach/bach_grenade.wav`, `bach_shield.wav`,
`bach_camp_warn.wav`, `bach_holy_light.wav`, `snipe_warn6.wav`. The weapons-before-sounds order is
this arm's fact.

#### `CNPC_VChangBros` — `0x1036ae60`

One body fills three species slots (`CNPC_VChangBros#104`, `…Blade#104`, `…Claw#104`). Scope-trace
frame, the Troika body, seven emitters with preload 1 — `chang_teleport_in_emitter`,
`chang_teleport_out_emitter`, `chang_powerup_emitter`, `chang_spine_emitter`,
`chang_center_emitter`, `chang_blast_emitter`, `chang_ball_charge_emitter` — then four weapons:
`item_w_chang_claw`, `item_w_chang_blade`, `item_w_chang_energy_ball`, `item_w_chang_ghost`.

#### `CNPC_VGargoyle` — `0x10378470`

The Troika body, then **nine gib models with preload 1** in the body's own push order (descending
`.rdata`, `0x1063a808` down to `0x1063a550`): `garg_gibbs`, `gargoyle_head`, `gargoyle_L_foot`,
`gargoyle_L_hand`, `gargoyle_L_torso`, `gargoyle_pelvis`, `gargoyle_R_foot`, `gargoyle_R_hand`,
`gargoyle_R_torso`, all under `models/character/monster/gargoyle/gargoyle_gibbs/`. Then the four
`character/monster/gargoyle/stomp_1..4.wav` (`0x10639480`, to `0x10`), the three
`exert_heavy_1..3.wav` (`0x10639490`, to `0xc`), `roar2.wav`, and `item_w_gargoyle_fist`.

#### `CNPC_VGhoulCroucher` — `0x1037b1a0`

55 bytes, the shortest arm: the Troika body, `item_w_claws_ghoul`, then the two Malkavian-mansion
stalker models with preload 0 (`.../Stalker/stalker.mdl`, `.../Stalker_Female/stalker_female.mdl`).
Those two models are what makes the male/female split in its `SetModel` sibling (`0x1037b1f0`)
reachable.

#### `CNPC_VHengeyokai` — `0x1037f960`

The Troika body, the four `character/monster/hengeyokai/stomp_1..4.wav`, the three
`exert_heavy_1..3.wav`, `models/character/monster/Hengeyokai/hengeyokai.mdl` with preload 0, the
`Hengeyokai_freeze_emitter` with preload **0** rather than the 1 Andrei, Chang and the ManBat use,
and `item_w_hengeyokai_fist`.

#### `CNPC_VManBat` — `0x1038aec0`

280 bytes, the longest. The Troika body, a four-entry model table (`0x10640ce0`, preload 0) whose
first two entries are `models/character/monster/manbat/Throw_Objects/ThrowTaxi.mdl` and
`.../supportb.mdl`; four emitters with preload 1 — `Manbat_screechcone_emitter`,
`Manbat_player_emitter`, `HUD_Manbat_emitter`, `Manbat_blast_player` (the last with no `_emitter`
suffix), and exactly the four the screech-cone body `0x1038e9c0` spawns; three three-entry sound
tables (`character/male/sheriff_manbat/wingflap_1..3`, `exert_heavy_1..3`, `fly_by_1..3`); the two
singles `screech.wav` and `fall.wav`; **`sheriff_teleport_emitter` precached TWICE in a row from the
identical `.rdata` cell `0x10642adc`**, a retail duplicate that is kept; and `item_w_manbat_claw`.

#### `CNPC_VMingXiao` — `0x10392660`

The Troika body, then **eleven emitters all with preload 0**: `Ming_xiao_slimetrail_emitter`,
`…_emitter2`, `Ming_xiao_tentacle_damage_emitter`, `Ming_xiao_tentacle_burst_emitter`,
`Ming_xiao_death_emitter`, `…_emitter2`, `Ming_xiao_death_proxy_emitter`, `…_emitter2`,
`Ming_xiao_vomit_emitter`, `Ming_xiao_transform_emitter`, `…_emitter2`. Then one sound —
`character/monster/ming xiao/movement.wav`, whose directory carries a **space**, not an underscore —
and three weapons: `item_w_mingxiao_melee`, `item_w_mingxiao_tentacle`, `item_w_mingxiao_spit`.

#### `CNPC_VMingXiaoTentacle` — `0x1039c220`

The one arm whose model fallback runs **before** the chain: an unset keyfield takes
`models/character/monster/mingxiao/mingxiao_tentacle/mingxiao_tentacle.mdl` through slot 212, then
the Troika body runs and precaches it. Then three `PrecacheModel` calls whose returned indices are
STORED — `m_iModeIndexTentacleToGrub` (`+0x6664`), `m_iModeIndexGrub` (`+0x6668`) and
`m_iModeIndexGrubToProxy` (`+0x666c`). **The first two push the SAME string (`0x1064a2f0`,
`MingXiao_baby.mdl`), so retail's first two indices are equal**; the third is
`MingXiao_transformation.mdl`. Then three emitters with preload 0
(`Ming_xiao_tentacle_transform_emitter`, `Ming_xiao_baby_transform_emitter`,
`Ming_xiao_baby_death_emitter`) and two sounds, `character/monster/ming xiao/tentacle_hit_ground.wav`
then `tentacle_flopping_loop.wav` (the listing pins both pointer targets at `1039c30b`/`1039c31e`).

#### `CNPC_VNewscaster` — `0x103a03e0`

The Troika body, then `sound/character/conversations/news/tv` globbed twice — **`.mp3` first and
`.wav` second**, the reverse of the Troika body's pair, and both with `0x101d0f10`'s third and fourth
arguments 0 where the Troika body passes 1 and 0.

#### `CNPC_VSabbatLeader` — `0x103a6ab0`

Scope-trace frame, the Troika body, `models/character/monster/Andrei/andrei.mdl` and
`models/character/npc/unique/hollywood/andrei/andrei_no_mouth.mdl` both with preload 1, the
seven-entry `character/monster/andrei_transformed/step1..7.wav` table (`0x1064c480`, to `0x1c`), the
three-entry `exert_heavy_1..3.wav` table (`0x1064c49c`, to `0xc`), then seven singles in push order —
`ambient_run.wav`, `Leap_Down_Attack_1.wav`, `dive_in_splash.wav`, `dive_out_splash.wav`,
`splash_warning.wav`, `jump_retreat.wav`, `roar_1.wav`, all under
`Character/Monster/Andrei_Transformed/` — then `Andrei_powerup_emitter` and `Andrei_blast_emitter`
with preload 1, and `item_w_sabbatleader_attack`.

#### `CNPC_VSheriffMan` — `0x103ae540`

Scope-trace frame, the Troika body, `models/character/monster/manbat/manbat.mdl` with preload 1,
`sheriff_landblast_emitter` once and `sheriff_teleport_emitter` **twice** from the same `.rdata` cell
the ManBat arm reads, all preload 1, then `item_w_sheriff_sword`.

#### `CNPC_VTest` — `0x103b41e0`

Twelve sounds and only THEN the Troika body — base-last, like `CNPC_VTzimisce`. In order:
`death1`, `alert1`, `idle1`, the four-entry `pain1..4` table (`0x10652194`, to `0x10`), `fear1`,
`lostenemy1`, `foundenemy1`, `surprise1`, `knockout1`, all under `character/npc/test/`.
**The decompiled C shows the `surprise1` call with one argument; the listing at `103b427f` shows
`PUSH 0x0` before all twelve.** The checklist's "retail stack quirk" is a decompiler artifact and is
not reproduced. `knockout1` is precached and never spoken by any vocalization hook.

#### `CNPC_VTzimisce` — `0x103b8fa0`

Six `character/monster/spiderchick/spi_footstep_indiv_1..6.wav` (`0x106530fc`, to `0x18`), three
`spi_attack_swish_1..3.wav` (`0x10653114`, to `0xc`), `item_w_tzimisce_melee`, and only then the
Troika body. Base-last.

#### `CNPC_VTzimisceHeadClaw` — `0x103c1400`

The Troika body, four emitters with preload 1 — `Tzim2_powerup_emitter`, `Tzim2_blast_emitter`,
`Tzim2_player_emitter`, `HUD_Tzim2_emitter`, exactly the four the slot-332 grab body spawns by name —
then the fat guy's four footsteps as **two contiguous tables of two** (`0x1065ca68`, `0x1065ca70`)
and his three `Exert_Heavy_1..3.wav` (`0x1065ca78`), the two singles
`Character/Monster/TC_FatGuy/Sluge_Hit.wav` and `Sluge_Affected.wav`, then `item_w_tzimisce2_claw`
and `item_w_tzimisce2_head`.

#### `CNPC_VTzimisceRunner` — `0x103c31e0`

The Troika body, then four contiguous tables — `character/monster/TC_Runner/foot_steps_1..2`
(`0x1065d680`), `foot_steps_3..4` (`0x1065d688`), `Breath1..4` (`0x1065d690`) and `Exert_Heavy_1..3`
(`0x1065d6a0`) — then `item_w_tzimisce3_claw`. The 2+2 left/right split is the same one
`docs/vtmb/footsteps.md` records for this class.

#### `CNPC_VWerewolf` — `0x103cb2a0`

A scope-trace frame carrying `m_iName` (`+0x26c`, the literal **`"NULL ENTITY"`** with a space when
`this` is null, which C++ cannot reach); the Troika body; then `sound/Character/Monster/Werewolf` and
`sound/Area/Special/Observatory` globbed for `.wav` with `0x101d0f10`'s third argument **clear** and
its fourth **set** — the reverse of the Troika body's pair. Then the sound-group binding, in retail's
write order: `m_iVSoundTableIdx` (`+0x00bc`) `:= 2`, `m_iszVSoundGroup` (`+0x00c0`) `:= "Werewolf"`
(NULLed when the literal is empty, which it is not), and `m_iVSoundGroup` (`+0x00b4`) `:=
thunk_FUN_101f55a0(&DAT_1073dc28, this, group, 0)` — the same triple `CNPC_VZombie::SetModel` makes.
Then a four-entry footstep table (`0x1065f4d0`), `item_w_werewolf_attacks`, and the two singles
`dev/ww_tele_out.wav` and `dev/ww_tele_in.wav` (a **slash**, not the underscore the checklist's walk
spells).

**The werewolf's footstep table is the Tzimisce fat guy's.** The listing annotates the indexed load
at `103cb385` with `character/monster/TC_FatGuy/Foot_Step1.wav` and `Foot_Step2.wav`. Its own steps
come through the sound GROUP it binds three lines earlier; this table is a retail copy-paste and is
reproduced rather than corrected.

#### `CNPC_VZombie` — `0x103df120`

The Troika body, `zombie_headshot_death_emitter` and `zombie_headshot_dmg_emitter` with preload
**0**, then `item_w_zombie_fists`. The two emitters are the assets `CNPC_VZombie::OnTakeDamage`
(`0x103e06d0`) names.

### The three `CNPCMaker*` arms — `0x1034b160`, `0x1034c180`, `0x1034cde0`

_Recovered 2026-09-14, story 29d._

`classes.md` stands `CNPCMaker` as a `CAI_BaseNPCTroika` with 621 slots, so slot 104 IS the NPC
`Precache` virtual for a maker. All three bodies open with the same question — is the model keyfield
(slot 9) unset or empty — and diverge only in what the error arms carry.

`CNPCMaker::Precache` `0x1034b160` is the only one that checks BOTH keyfields and draws the developer
overlays. Model present: `PrecacheModel(model, 0)`, chain `CAI_BaseNPC::Precache`,
`UTIL_PrecacheOther(m_iszNPCClassname +0x665c)` and return. Model present but classname EMPTY:
`Warning("%s at %.0f %.0f %0.f missing NPCClassname", GetDebugName(), origin.x, origin.y, origin.z)`,
`UTIL_Remove(this)`, then — only when the `developer` cvar (`DAT_1070af4c`) answers `!IsCommand()`
and `GetInt() >= 1` — a `"%s: BAD NPC Classname"` overlay box at the collision `OBBMins`/`OBBMaxs`.
Model EMPTY: the same shape with `"missing modelname"` and `"%s: BAD MODEL NAME"`. Both error arms
return early when the developer gate refuses. The `%0.f` on the third float is retail's own typo and
prints the same as `%.0f`.

`CNPCMaker_Fleshpile::Precache` `0x1034c180` **drops the empty-classname check and the developer
overlay entirely**, keeping only the missing-model warning and the removal — so an empty classname on
a fleshpile maker reaches `UTIL_PrecacheOther("")` and its own `"NULL Ent in UTIL_PrecacheOther: %s"`
warning instead of removing the maker.

`CNPCMaker_Zombie::Precache` `0x1034cde0` is the fleshpile shape plus three zombie facts: **before**
the chain it zeroes `m_altEquipment` (`+0x1a98`) and `m_spawnEquipment` (`+0x5dec`), so an authored
equipment keyfield is discarded and the base chain's own `UTIL_PrecacheOther` arm can never fire;
and **after** the classname it also precaches `item_w_zombie_fists`.

**Unrecovered:** the two reject literals of `0x101d0f10` (`DAT_105a0410`, three characters, and
`DAT_105a040c`, one) — the corpus holds neither's bytes, so the gate is ported with empty constants
and refuses nothing, and no call site in this family passes a directory either could match; entries
2 and 3 of `CNPC_VManBat`'s four-entry model table (`0x10640ce0`), which the corpus names only by the
table's first two targets; `CGenericSabbat_NPC`'s char-template column `+0x78`, which has no
recovered name; and the two `.rdata` tables of `CNPC_VTzimisce` are told apart by the layout
convention (an array declared first carries the higher string addresses) rather than by a direct
read of the pointers.

## Story 29d, family SaveRestore10 — slots 126 `Save`, 127 `Restore`, 180 `UpdateOnRemove` and 106 `PostConstructor` — `0x102993c0`, `0x10299700`, `0x1028d6e0`, `0x1027bb20` (2026-09-14)

_Recovered 2026-09-14, story 29d._

Sixteen `rule` rows fill four Troika-line slots, two `CAI_BaseNPC` base bodies beneath two of them,
and nine species overrides. **The family is one mechanism with a retail rule wrapped round it**: a
sentinel codec that hides a `FIELD_TIME` stamp from the archive's own rebase, applied to a named
list of fields in a recovered order. Which field, which mode, and that the encode runs *before* the
archive call and the decode *after* it are all retail's — which is why the three species bodies a
batch verdicted `mechanism` were corrected to `rule` on review.

### The sentinel codec — `0x101cf250`, `0x101cf2f0`, `0x101b9840`, `0x101b9860`

_Recovered 2026-09-14, story 29d._

`0x101cf250(float*, mode)` rewrites a stamp to `1e+11` when it matches its mode's "unset" value;
`0x101cf2f0(float*, mode)` reverses it for any stamp at or above `_DAT_10482fac`. The two cells that
decide the whole thing were read out of the pinned `vampire.dll` (base `0x10000000`, `.rdata` VA
`0x10445000` at file offset `0x445000`): **`_DAT_104454c4` = 0.0** and **`_DAT_10482fac` = 1e+10**.
The sentinel therefore has a decade of headroom over any stamp a running game could hold.

| mode | encode fires when | decode writes |
|---|---|---|
| 1 | `*p < 0.0` (strictly) | `-1.0` |
| 2 | `*p == -1.0` | `-1.0` |
| 3 | `*p == 0.0` | `0.0` |
| 4 | `*p == FLT_MAX` | `FLT_MAX` |

Modes 1 and 2 **share a `case` label in the decode**, so mode 1 is not its own inverse: a `-0.5`
encoded by mode 1 comes back as `-1.0`. A mode outside 1..4 is retail's `default:` and writes
nothing. `0x101b9840` / `0x101b9860` are the nine-times-repeated wrappers whose whole body is the
codec at mode 2 over a `CSound`'s `+0x10 m_flExpireTime` (`FIELD_TIME` in that class's datamap).

**Unrecovered:** nothing. Both `.rdata` cells are read and every arm is in the listing.

### `CAI_BaseNPC::Save` — `0x1027bc60`

_Recovered 2026-09-14, story 29d._

Slot 126's body on the `CAI_BaseNPC` line and the body the Troika override calls through a direct
`thunk_`. **Two corrections to the checklist's walk, both from the listing.**

First: the walk's "convert 4 timers starting at `m_flExtendedBlockedByFriendTimer` and 3 starting at
`m_flWaitFinished`" reads the codec's *mode* argument as a *count*. `1027bc6c PUSH 0x4 / LEA
EBP,[ESI + 0x5b8c]` and `1027bc80 PUSH 0x3 / LEA EBX,[ESI + 0x5db4]` are two calls, on two fields,
at modes 4 and 3. `CAI_BaseNPC::Restore` (`0x1027c160`, band 5–9) carries the same pair and story
29c-1 read it the same wrong way; both are corrected here, and the `RebaseRestoredStamp` helper
29c-1 coined for a rebase retail does not perform is gone with it.

Second: `0x1023f040` / `0x1023f0c0` / `0x1023f060` are **CRC32**, not a bit-vector init/copy/finish.
`0x1023f040` writes `0xffffffff`, `0x1023f0c0` is an unrolled table-driven byte loop over
`DAT_10496f58` computing `crc = (crc >> 8) ^ table[(byte ^ crc) & 0xff]`, and `0x1023f060`
complements. So `AIExtendedSaveHeader_t`'s last word is a **checksum of the running schedule's task
array** — `schedule+0x20` for `schedule+0x24 << 3` bytes, an 8-byte `Task_t` per task — and not a
copy of its interrupt bits.

The body, in retail's order: encode `m_flExtendedBlockedByFriendTimer` (mode 4) and
`m_flWaitFinished` (mode 3); the motor pre-archive fix-up `0x102e0b60`, **guarded** on `m_pMotor`
(`+0x5d44`); the move-and-shoot overlay's `0x102e8aa0` at `+0x5cf4`, **not guarded**; build
`AIExtendedSaveHeader_t` on the stack — version `1` as a 16-bit word, then a flag word whose bit
`0x1` is slot `0x29c` `GetEnemy()` non-null, bit `0x2` is `m_hTargetEnt` (`+0x5ce4`) passing both
its `& 0x1fff` index and its `>> 13` serial check onto a live entity, and bit `0x4` is the navigator
goal (`0x102ee6a0`), then a `0x80`-byte `Q_strncpy` of `schedule+0x40` and that CRC; `WriteFields`
through `ISave` `+0x08` with `datamap_AIExtendedSaveHeader_t` (`0x105cabd0`);
`CBaseCombatCharacter::Save`, **whose answer is this body's return value**; then the two decodes in
the same order and the two post-archive fix-ups.

The no-schedule arm writes `name[0] = 0` and a literal zero CRC — and CRC32 over zero bytes is
`~0xffffffff == 0`, so the two arms agree on the checksum and differ only in the name.

**Unrecovered:** `DAT_10496f58` (= the standard reflected CRC-32 table, first 16 entries verified; read 2026-09-21, `rdata-cells.md`)'s 1 KB of table bytes. "This is the standard reflected CRC-32 table"
is an inference from the loop's shape, not a read; a different polynomial would change the
checksum's value and nothing else about the body.

### `CAI_BaseNPCTroika::Save` — `0x102993c0`

_Recovered 2026-09-14, story 29d._

Slot 126 for `CAI_BaseNPCTroika` and 60 more classes. It brackets `0x1027bc60` with an eleven-stamp
encode and decode plus nine `CAISound` expiry stamps, and writes three words of its own between the
passes.

The encode list, in the listing's order with the listing's modes (`102993c9`..`10299485`):
`m_flCanSeekCoverTimer` `+0x607c` **3**, `m_flSeeUnknownCheatVisionTime` `+0x6084` **2**,
`m_flMeleeHeightDiffTimer` `+0x6274` **2**, `m_flOccludedReportTimeE` `+0x62cc` **3**,
`m_flOccludedReportTimeT` `+0x62d0` **3**, `m_flOccludedReportTimeW` `+0x62d4` **3**,
`m_flInterruptTime` `+0x632c` **3**, `m_flNextInterestChangeTime` `+0x63d4` **2**,
`m_flWeaponScareTime` `+0x63dc` **2**, `m_flIgnoreCollisionTimer` `+0x6458` **4**,
`m_flEyeFidgetTime` `+0x657c` **4**. Then the nine sounds through `0x101b9840`: `m_BestSound`
`+0x60b0`, `m_InvestigateSound` `+0x60dc`, `m_LastSoundDanger` `+0x6108`,
`m_LastSoundPhysicsDanger` `+0x6134`, `m_LastSoundCombat` `+0x6160`, `m_LastSoundBulletImpact`
`+0x618c`, `m_LastSoundPlayer` `+0x61b8`, `m_LastSoundWorld` `+0x61e4`, `m_LastSoundFlinch`
`+0x6210`.

**The decode pass is not shifted where it matters.** The decompiled C renders its eleven pointers
one slot out and its nine sound pointers two slots out — EBX and stack aliasing — which is what the
checklist's walk flags. But the *modes* are pushed in the listing in their own right and read
`3,2,2,3,3,3,3,2,2,4,4` at `1029956f`..`102995e9`: identical to the encode list. The decode walks
the same eleven fields in the same order with the same modes, which `CAI_BaseNPCTroika::Restore`
(`0x10299700`) confirms independently — its own C is unaliased and names all eleven.

Between the passes: `ISave` `+0x30` writes one bool, `m_pPedestrianLink` (`+0x630c`) non-null, and
— **only when that bool is set** — `ISave` `+0x28` writes the two ints at `+0x630c+4` and
`+0x630c+8`. The return is the base body's result, stashed at `[ESP+0x60]` before the write pass and
reloaded at `10299642`.

**Unrecovered:** nothing in this body.

### `CAI_BaseNPCTroika::Restore` — `0x10299700`

_Recovered 2026-09-14, story 29d._

Slot 127. `CAI_BaseNPC::Restore` (`0x1027c160`) runs **first**, its answer is kept in EBX and
returned unchanged at `10299851`. Then `IRestore` `+0x44` reads one bool whose truth gates two
`+0x3c` reads into `m_iRestorePedLinkNode` (`+0x6310`) and `m_iRestorePedLinkDestNode` (`+0x6314`)
— the two ints the Troika `Save` wrote. Then the same eleven stamps and nine sounds, decoded in the
same order with the same modes.

Five species classes override it. `CScriptedTarget` (`0x1034e370`) adds one mode-3 decode on
`m_flPauseDoneTime` (`+0x5f64`) after the **base** body — it is a `CAI_BaseNPC`, so the Troika half
never runs for one. `CNPC_VMingXiao` (`0x10396000`) decodes six `m_rflRegrowTimers` (`+0x66f4`) at
mode 4 ascending, `CNPC_VMingXiaoTentacle` (`0x1039eda0`) `m_flPhaseExpireTimer` (`+0x6674`) at
mode 3, and `CNPC_VTzimisceHeadClaw` (`0x103c2860`) `m_flSlowedExpire` (`+0x6678`) at mode 3 —
each the exact decode twin of that class's own slot-126 body.

**Unrecovered:** nothing.

### `CNPC_VVampireBoss::Restore` — `0x103c5910`

_Recovered 2026-09-14, story 29d._

A **post-load reset**, not a restore, and the body the other bosses' own slot-127 overrides call as
their base (`CNPC_VAndreiBlood` `0x1035cf80`, `CNPC_VAsianVampire` `0x10360e10`, the Chang brothers
`0x1036b170`, `CNPC_VSabbatLeader` `0x103a6e80`, `CNPC_VSheriffMan` `0x103ae7f0`), so it sits
between the Troika body and those rows.

Inside a scope-trace frame carrying `"CNPC_VVampireBoss::Restore"` (`0x1065ec00`): the Troika body,
and then three writes in the listing's order — `103c5972` `m_pMonsterModelName` (`+0x6680`) := null,
`103c597c` `ClearBodyEmitterNames` (`0x103c6eb0`), `103c5981` `m_pszMonsterClassname` (`+0x6694`)
:= the literal `"npc_VVampireBoss"` (`0x1065e8dc`). A boss saved mid-transformation therefore comes
back wearing its default model name and its default classname whatever the archive held.

**Unrecovered:** nothing.

### `CAI_BaseNPCTroika::UpdateOnRemove` — `0x1028d6e0`

_Recovered 2026-09-14, story 29d._

Slot 180 for 61 classes, read off the listing. Six steps:

1. `ClearHintNode(this, 5.0)` — `1028d6e1 PUSH 0x40a00000`. A **5.0-second** reuse delay where the
   base `0x1027ca30` passes 0.0. That difference is the whole reason the two bodies are distinct.
2. slot `0x964` (**601**), dispatched with `this`, only when `m_pAttackCoordinator` (`+0x65e8`) is
   non-zero — the melee-coordinator release.
3. `0x102b53d0` called as `(0, "Leaving interesting place (UpdateOnRemove)")`.
4. `IsInDialog()` (`0x102c1170`) gating the dialogue stop (`0x102c0bb0`).
5. `0x1029f5d0` on `m_sppPatrolPath` (`+0x658c`) and then `m_sppPatrolPathHunt` (`+0x6594`).
6. a tail jump to `CAI_BaseNPC::UpdateOnRemove`.

**`0x102b53d0` is the interesting-place release, not "the grapple release".** The string argument at
`1028d702` settles it, and so does the datamap: `+0x62ec` is `m_pInterestingPlace`, `+0x62e8`
`m_bInterestingPlaceArrived` and `+0x6304` `m_eInterestingPlaceMode`. Its body, with a place held,
re-checks it (`0x10299a80`), emits two sounds through a `CPASAttenuationFilter` (channel 4, pitch
`0x24`, volume 100), detaches through `0x102da600` with a flag computed from
`m_bInterestingPlaceArrived` and `0x100cd660`, clears the place and the mode, strips
`m_bfAINPCFlags` bit `0x20000000` and `m_bfAINPCFlags2` bits `0x80000008` (`0x102b5534 AND
EDX,0x7ffffff7`; corrected 2026-10-04, V3b — the text said `0x08000008`), and calls `0x102ae310` —
and **always**, place or none, clears `m_bInterestingPlaceArrived` last.

The flag argument (`[ESP+0x4c]`, tested at `0x102b54e3`) gates the output: only with the argument
set and `+0x62e8` arrived does it fire this NPC's `m_OnInterestingPlaceLeft` (`+0x5f8c`, activator
the place, `0x102b5500`), once; `0x102da600(place, this, 1, fired)` (`0x102b551d`) gets "fired and
still arrived". `OnScheduleChange` (`0x102a099c`), `TaskFail` (`0x1029adb9`) and `RunTask`'s
wait-finished arm pass 1; this removal (`0x1028d707 PUSH 0`), `Event_Killed` and `NPCInit` pass
0, so a removed or killed visitor fires no "Left". Ported as `FElysiumNpc::FinishAmbientUse` (V3b);
the two sounds, `0x102ae310` and the marker half of the re-check `0x10299a80` are unrecovered
there.

Three species classes override slot 180. `CNPC_VCop` (`0x10371a90`) is read off the listing because
the C mis-renders its two census bytes as `this+1`: `DL = m_bCountedAlive (+0x6671)`, decrement
`DAT_1093acac` when set, **read `+0x6672` before clearing `+0x6671`**, decrement `DAT_1093acb0` when
that was set, clear `+0x6672`, then tail-jump to the Troika body. Both counters are program-visible
— `DAT_1093acac` is written by `CNPC_VCop::Spawn` and read by `SelectSchedule`, `0x103707e0` and
`0x10370850`; `DAT_1093acb0` by `OnStateChange` and `SelectSchedule`. `CNPC_VMingXiao`
(`0x10391230`) drops the carried throwable through `0x10398fd0` when `m_eThrowableObjectMode`
(`+0x673c`) is non-zero, then the Troika body either way — without it the thrown prop outlives the
boss. `CNPC_VNewscaster` (`0x103a03a0`) tears the two story queues down through `0x103a0d50` first.

**Unrecovered:** nothing in the Troika body itself. `0x102c0bb0`'s schedule `0xf1` has no registered
id in this port, so the install is counted rather than performed.

### `CAI_BaseNPC::PostConstructor` — `0x1027bb20`

_Recovered 2026-09-14, story 29d._

Twenty-seven bytes whose entire content is an **order**: `1027bb28` calls
`CBaseCombatCharacter::PostConstructor(name)` to completion, and only then does `1027bb31` dispatch
this object's own vtable `+0x6a0` — `0x6a0 / 4` is slot **424**, `CreateComponents`. The NPC-side
post-construct pass therefore observes everything the base pass built and nothing it has not, and no
state is written here directly.

**Unrecovered:** nothing.

### `CNPC_VMingXiao::Save` — `0x10395f80`

_Recovered 2026-09-14, story 29d._

The largest of the three species slot-126 arms, and the one that shows the codec's shape plainly: it
brackets the Troika body with a **six-element loop** rather than a single field. `m_rflRegrowTimers`
is a six-float array, and the body walks it twice with the pointer advanced one float per step —
`thunk_FUN_101cf250(&timer[i], 4)` six times **before** `CAI_BaseNPCTroika::Save 0x102993c0`, whose
`int` result is what this body returns, then `thunk_FUN_101cf2f0(&timer[i], 4)` six times after, over
the same array from the same base pointer. **Mode 4** is the `FLT_MAX` sentinel: the encode swaps a
value exactly equal to `FLT_MAX` for `1e+11` against retail's `FIELD_TIME` re-base, and the decode
swaps it back, so a limb whose regrow timer is "never" survives a save instead of being re-based into
a timer that fires.

Twelve in-place writes to NPC offsets around one archive call, each under a retail-chosen mode, with
the encode-before / decode-after order observable through the save — which is why this row is `rule`
and not the `mechanism` its batch first recorded, on the rubric's explicit "a retail-specific rule
around the mechanism" clause. Its two siblings `CNPC_VMingXiaoTentacle::Save` (`0x1039ed50`,
`m_flPhaseExpireTimer` `+0x6674`) and `CNPC_VTzimisceHeadClaw::Save` (`0x103c2810`, `m_flSlowedExpire`
`+0x6678`) are the same shape with one field and **mode 3** (the `_DAT_104454c4` = `0.0f` sentinel).

**Unrecovered:** nothing in the body. What `m_rflRegrowTimers`' six slots index — which limb is which
— is not recovered here; the array is carried whole and by position.

## Story 29d, family Lifecycle10 — the scripted-sequence activate, the alternate-AI door and the two maker helpers — `0x101a8de0`, `0x10290350`, `0x1034b7b0`, `0x1034d0a0`, `0x1034d140` (2026-09-14)

_Recovered 2026-09-14, story 29d._

### `CCineAISchedule::Activate` — `0x101a8de0`

_Recovered 2026-09-14, story 29d._

Slot 113 for `CCineAI`, `CCineAISchedule` and `CCineNPC`. `CBaseEntity::Activate` first; then a
`FindEntityByName` loop over `m_iszEntity` that walks **every** entity with the name and stops at
the first that carries a `CBaseAnimating` at `+0x94`, so a marker or trigger sharing the actor's
name is skipped rather than failing the beat.

Three arms come off that search. **No match at all** `DevMsg`s a divider (`0x105794f0`), the line
`"Could not find NPC %s in CCineNP..."` with `m_iszEntity` and `GetDebugName(this)`, and the divider
again — and **skips the precache entirely**, falling straight to the next-script resolution. **A
match whose `GetModelPtr()` is 0** prints the divider, `"NPC %s has no model in CCineNPC::"` and the
divider; note retail calls `GetDebugName` on the beat first and on the actor second and passes only
the second, discarding the first call's answer. **Otherwise** `0x10428880` precaches `m_iszPreIdle`,
`m_iszPostIdle` and `m_iszPlay`, in that order.

**Correction to the walk's field naming.** `m_iszPreIdle` is the field at `+0x5f44` whose *keyfield*
is `m_iszIdle` (`vtmb_fields CCineNPC`), which is the pre-idle a map authors and holds its actor in
— not the separate field whose keyfield is the literal `m_iszPreIdle`. And `0x10428880` is
**`PrecacheSequenceSounds`**: look the named sequence up on the actor's studio header, walk its anim
events, and precache every event id below `5000` that is a sound event as a script sound, warning
`"Bad sound event %d in sequence %s"` for an option string with no terminator.

The tail: `FindEntityByName(m_iszNextScript)` writes `m_hNextCine` (`+0x5f94`) from its ref handle
or `0xffffffff` when absent, and an `m_hNextCine` that does not resolve **clears** `m_iszNextScript`
(`+0x5f58`).

**Unrecovered:** the divider literal at `0x105794f0` and the two message strings are truncated in
the corpus's string table; the port carries the recovered prefixes.

### `RunAlternateAI` mode 4 — `0x10290350`

_Recovered 2026-09-14, story 29d._

The door-blocked arm of the transaction family Conditions carries `EnterAlternateAi` and
`RunAlternateAiOpeningDoor` (mode 1) for. In order:

1. `m_bForceMaintainActivity` (`+0x65fa`) := 1 across `CAI_BaseNPC::MaintainActivity`
   (`0x102727d0`, a `__thiscall` with no dispatch site — not a vtable slot), then := 0. The latch
   spans exactly that one call.
2. When `m_hOpeningDoor` (`+0x5d24`) still resolves **and** that door's `m_toggle_state` (`+0x4f8`)
   is 0: stop the motor (`0x102bf7e0`) and `m_eAlternateAI` (`+0x644c`) := 0. This arm deliberately
   does **not** clear the door handle or `m_bOpeningDoorWait`.
3. A hull trace from slot 217 `GetAbsOrigin()` to that origin plus `m_vecForward` (`+0x6290`) scaled
   by `_DAT_10451acc` — **64.0**, read out of the pinned image at file offset `0x451acc` — with mask
   `0x202400b` and radius `100.0` through the filter at `+0x5d40`. On a hit: stop the motor,
   `m_hOpeningDoor` := -1, `m_bOpeningDoorWait` (`+0x5d30`) := false, `m_eAlternateAI` := 0.
4. The test is `curtime < m_flAlternateAIExpireTimer` (`+0x6450`) and the **else** arm fires, so an
   expiry stamp *equal* to curtime has expired: slot `0x700` (**448**) `TaskFail` with `0xe`, and
   the same three fields cleared.

Always answers **true**, so the transaction keeps the body.

**Unrecovered:** nothing; every constant is a listing literal or a pinned `.rdata` read.

### `CNPCMaker::MakeNPC` — `0x1034b7b0`

_Recovered 2026-09-14, story 29d._

Slot 617 on `CNPCMaker`. **Four corrections to the checklist's walk, all from the listing**, and all
of them cases where Ghidra applied a wrong struct to the child pointer.

1. The body's **last** write is `1034baee MOV byte ptr [EDI + 0x65f4],0x1` — `m_bCameFromSpawner` on
   the child. The walk's "the child's `+0x2c4` latch" is the same byte seen through
   `this_00[0x17].field_0x2c4`.
2. The block at `1034ba0f`..`1034ba69` is **not** a script-state block. The listing copies plain
   offsets `+0x6420`/`+0x6424`/`+0x6428`/`+0x642c`/`+0x6430` as dwords and
   `+0x6434`/`+0x6435`/`+0x6436` as bytes, and the datamap names them
   `m_iPercentOccludedWait`/`Cover`/`Walk`/`Flank`/`Chase`, `m_bAllowAlertLookaround`,
   `m_bStayEntrenched` and `m_bAllowKickHintUse`. Nine inherited words, nothing about saved move
   collide, solid flags, effects, sound override or fake silence.
3. `1034b9ee MOV ECX,ESI` puts the **maker**, not the child, in the `this` register for
   `InitPerceptionDistances` (`0x1028fb70`) and `0x1028fc90`. So the child inherits the three
   authored perception words at `+0x63b0`/`+0x63b4`/`+0x63bc` and **nothing derives the resolved
   pair from them here** — a retail oddity, reproduced.
4. The `+0x1584` copy is `m_RelationshipString`, a `CBaseCombatCharacter` `string_t`, and it happens
   **before** the `OnSpawnNPC` output at `+0x6668` fires.

The rest of the walk holds: the ground-Z cache into `+0x66b8` from one downward trace (drop
`_DAT_1046bacc` = **2048.0**, mask `0x2400b`) taken the first time the field equals 0.0; `CanMakeNPC`
(`+0x9a8`); the child classname resolved through the entity factory with `"NULL Ent in NPCMaker!"`
and `"Non Troika Ent in NPCMaker!"` as its two refusals; the `m_sRefMapDataBuffer` (`+0x76cc`) copy
into `+0x66cc` and its replay through the child's `ParseMapData` (`+0x1ac`), `Precache` (`+0x1bc`)
and `SetClassname` (`+0x1e8`) — `+0x1bc` is slot **111** (`MemberSync`), not `Precache` (slot 104
is `+0x1a0`; corrected 2026-09-27); **assignment** of spawnflags 4 or `0x204`, not OR; the disable-AI copy; slot 619
`ChildPreSpawn`; `DispatchSpawn`; `SetOwnerEntity`; the optional `SetName` from `+0x66bc`; slot 620
`ChildPostSpawn`; `++m_nLiveChildren`; and, unless `m_bInfChild` (`+0x66c3`), `--m_iMaxNumNPCs` with
`ThinkSet(0)` and `+0x1f0 = 0` once `0x1034b430` reports depleted.

`CNPCMaker::ChildPreSpawn` (`0x1034af30`) and `ChildPostSpawn` (`0x1034af50`) are both a bare
`return;` — the base maker's hooks do nothing, which is a fact and not a gap.

The function **returns the child** (`EAX`): `MakerThink` tests it, `InputSpawnNPC` tail-jumps into
the slot and `CNPCMaker_Zombie::MakeNPC` continues on it (the ledger typed it void; corrected
2026-09-27). The optional `SetName` is skipped for a null `m_ChildTargetName` — and an authored
empty `NPCTargetname` IS null (`0x1042bff0` stores 0 for `""`) — so such a child keeps the targetname
the replayed block gave it: the maker's own.

**Unrecovered:** nothing in the body. The port replays the parsed form of the block (the def's
keys; it never holds the raw text) and `m_RelationshipString` has no port carrier at all.

### `CNPCMaker_Zombie::CanMakeNPC` — `0x1034d0a0`

_Recovered 2026-09-14, story 29d._

Slot 618. A non-zero bypass answers **true** before anything else. Otherwise, with a resolvable
player (`0x101cda50`), it measures the **Manhattan** distance `|dx| + |dy| + |dz|` between the two
`GetAbsOrigin`s, scales it by `_DAT_10450a9c` — **0.9**, read out of the pinned image at file offset
`0x450a9c` — and **refuses when that product exceeds `+0x76d8`**. A zombie maker refuses when the
player is too *far*, the opposite sense of the base's Euclidean minimum-distance arm at `+0x66c8`.
It then forwards to `CNPCMaker::CanMakeNPC` (`0x1034b580`) with a **hard-coded false** bypass, so
the base's live limit, global no-spawn byte, npcclip, view cone, minimum distance and
`±_DAT_1049ffac` hull-occupancy trace (mask `0x2080`) all still run.

**Unrecovered:** nothing.

### `CNPCMaker_Zombie::MakeNPC` — `0x1034d140`

_Recovered 2026-09-14, story 29d._

Slot 617. The base `MakeNPC` first; a null answer returns null. Then `Weapon_OwnsThisType(this,
"item_w_zombie_fists", 0)` — **asked of the maker** (`1034d16a MOV ECX,EDI`, and `EDI` is `this`),
not of the zombie it just spawned. On a false it looks the item up through the entity factory and,
on a miss, `UTIL_Remove`s the spawned zombie and answers null.

On success: `item->SetOrigin(this->EyePosition())` — vtable `+0x304` is slot **193 `EyePosition`**,
not `GetAbsOrigin`, so the item is placed at the maker's *eye*; the item's `+0xa0` word is read at
`1034d1bd` **before** `0x40000000` is OR'd into its flag word at `+0x204` (`1034d1ca`);
`DispatchSpawn`; and, when the item's own `+0x1d0` check answers false, `zombie->Weapon_Equip(item->
+0xa0, false)` through vtable `+0x5fc` (slot 383).

**The walk stops there; the body has four more steps.** An `___RTDynamicCast` on the child against
the type name at `0x1062567c` and, on a hit, `0x103e0980(child, this->+0x76d0)`; the
`"Zombies_spawning_emitter"` (`0x1062565c`) created at the maker's `GetAbsOrigin` and
`GetAbsAngles(-1.0)` with a **15.0**-second life (`1034d249 PUSH 0x41700000`); the child's vtable
`+0x10c` (slot 67 `Unhide`); and `SetDisableAI(child, false)` (`0x1029f300`), which **overrides**
the copy `MakeNPC` made from the maker's own `m_bDisableAI` — a zombie maker's child always thinks.

**A retail fault, recorded.** `XOR ESI,ESI` at entry leaves the item pointer null and
`1034d173 JNZ 0x1034d19c` skips the lookup that would fill it, so the *true* arm of
`Weapon_OwnsThisType` reaches `1034d1a3 MOV EBP,[ESI]` and dereferences null. The arm is unreachable
in retail because a maker owns no weapons; the port guards it and takes the refusal instead, which
is a named divergence.

**Resolved 2026-09-27 (story 5 fold A4):** the cast target `0x1062567c`'s name at `+8`
(`0x10625684`) is `.?AVCNPC_VZombie@@`, and `+0x76d0` is `m_iZombieAISpawnType` (datamap
`0x106253e8`, key `Flag_ZombieAIType`); the arm is ported. Its writer beside the keyvalue is
`SetZombieAIType` `0x1034cf20` (store `+0x76d0`; with the propagate flag, call `0x103e0980` on every
entity named `m_ChildTargetName` whose Troika's `m_pParent` is this maker), reached only from
`0x1034d000` / `0x1034d050` — input-shaped bodies with no datamap row and no corpus caller.

## Story 29d, family SpeciesLifecycle10 — the per-species spawn, touch, restore, destroy and think bodies — `0x101aabf0`, `0x1034afe0`, `0x1034c020`, `0x1034cc60`, `0x1035cd00`, `0x1035cf80`, `0x1036b170`, `0x1037a270`, `0x1037bf60`, `0x1037d020`, `0x10388880`, `0x103ca7c0` (2026-09-14)

_Recovered 2026-09-14, story 29d._

Twelve rows in four shapes: three maker `Spawn`s, two species `Restore`s, five species arms on slots
another family owns, and two destructors. Every body below was read off the listing where the
decompiler aliased or mislabelled something, and eight of the checklist's walks are corrected here.

Four `.rdata` cells were read out of the pinned `vampire.dll` at file offset = address −
`0x10000000`: `_DAT_10450aa4` = **0.01f**, `_DAT_1044bef8` = **0.25f**, `_DAT_104ada44` = **2.3f**
and `_DAT_1044ffd0` = **5.0** (a *double*, `1037c022 FADD double ptr`). Two more were read while
checking sibling rows this family does not own: `_DAT_104a9300` = **2.0f**
(`CNPC_VAsianVampire::Restore`'s jump gravity) and `_DAT_104c6148` = **2.0f**
(`CNPC_VSheriffMan::Restore`'s).

**Unrecovered:** the per-body gaps are named in each subsection below.

### `CPayphone::NPCThink` — `0x101aabf0`

_Recovered 2026-09-14, story 29d._

Slot 431, `CPayphone#431` only, 205 bytes. **It never calls `CAI_BaseNPCTroika::NPCThink`.** A
payphone runs no schedule, no senses and no motor; its entire behaviour is to copy its dialogue
partner's pose. In the listing's order:

1. slot **250** `StudioFrameAdvance(0.0)` (`101aabf8 CALL dword ptr [EAX + 0x3e8]`), its returned
   interval discarded by `101aac07 FSTP ST0`.
2. If `m_hDialogPartner` (`+0x0fe8`) resolves live: the dialogue upkeep tick `0x102c1400`
   unconditionally; then, **only when the partner's `m_IdealActivity` differs from mine**,
   `SetIdealActivity(theirs)` (`0x10272650`), `m_nSequence` (`+0x06f0`) =
   `SelectWeightedSequence(theirs, -1)` and `ResetSequenceInfo()` (`0x10090950`); then, **on every
   pass**, `m_flCycle` (`+0x06f8`) = the partner's `m_flCycle`; then `m_flNextThink` = curtime +
   `_DAT_10450aa4` (**0.01 s**). The body returns here.
3. Otherwise: `if (IsInDialog())` the same tick; then `SetIdealActivity(1)` (`ACT_IDLE`)
   **unconditionally**, outside that test; then `m_flNextThink` = curtime + `_DAT_1044bef8`
   (**0.25 s**).

**Correction.** `+0x0ff0` is `m_IdealActivity`, not `m_Activity`, and `+0x06f8` is `m_flCycle`
(`vtmb_fields CAI_BaseNPCTroika`). With both named, "mirrors its partner frame for frame" is literal
— the sequence is re-picked only on an activity change, but the *phase* is copied every pass, which
is exactly why the arm needs a 100 Hz clock while the idle arm runs at 4 Hz.

**Unrecovered:** `0x102c1400` itself, the dialogue upkeep tick both arms run — a 237-instruction
body (scene release, `FinishTalking`, the queued-line pump, the disposition switch to schedule
`0xf1`, `CDialog::ShowPlayerChoices`) that is no row of this family. The port counts the call so the
order around it stays assertable.

### `CNPCMaker::Spawn` — `0x1034afe0`, with `CNPCMaker_Fleshpile::Spawn` `0x1034c020` and `CNPCMaker_Zombie::Spawn` `0x1034cc60`

_Recovered 2026-09-14, story 29d._

`CNPCMaker#103` only — not a Troika-line slot. 261 bytes, and the fleshpile's is byte-identical but
for one pushed address. In the listing's order:

1. a scope-trace frame naming `"CBaseEntity::SetSolid"` and the maker's targetname, then
   `SetSolid(SOLID_NONE)` on `m_Collision` at **`+0x270`** (`1034b04c LEA ECX,[ESI + 0x270]`), then
   the frame pops;
2. `m_cLiveChildren` at **`+0x66b0`** = 0 (`1034b065`) — **before** the next step;
3. slot **104** `Precache` dispatched virtually (`1034b06f CALL dword ptr [EAX + 0x1a0]`);
4. `if (m_bInfChild (+0x66c3)) m_bFade (+0x66c2) = 1`;
5. on `!m_bDisabled (+0x66c0)`: `ThinkSet(0x1000696a)` — which resolves through the thunk table to
   **`0x1034bbf0`**, the maker's own think — and `m_flNextThink (+0x17c)` = `m_flSpawnFrequency
   (+0x6664)` + curtime; otherwise `ThinkSet(0x1000572c)` → **`0x101c0b60`, a bare `RET`**, and *no*
   next-think stamp at all;
6. `Relink` (`0x1001514a`);
7. `m_flGround` at **`+0x66b8`** = 0 — the last write on both paths.

**Correction, in all three walks.** `m_cLiveChildren` is `+0x66b0` and `m_flGround` is `+0x66b8`
(`vtmb_fields CNPCMaker`); the checklist has the two offsets the wrong way round. And `m_Collision`
is `+0x270`, not `+0x17c` — `+0x17c` is `m_flNextThink`, the *other* word the body writes.

`CNPCMaker_Fleshpile::Spawn` (`0x1034c020`) differs in exactly one instruction: `1034c0d6 PUSH
0x10010dd4`, which resolves to **`0x1034c8b0`**.

`CNPCMaker_Zombie::Spawn` (`0x1034cc60`) is 295 bytes — the same prologue plus two differences.
*One:* the enabled path installs `0x10015c4e` → **`0x1034d2d0`** and stamps `m_flNextThink` =
`RandomFloat(1.0, 2.0)` + `m_flSpawnFrequency` + curtime (`1034cd20 PUSH 0x40000000 / 1034cd25 PUSH
0x3f800000`), while the **disabled path calls `ThinkSet(0)`** — a *null* think, not the inert one —
and the two branches join at `1034cd51` so `Relink` and `m_flGround = 0` run on both. *Two:* it then
writes `0xf423f` (**999999**) into all five police thresholds on **itself** —
`m_iPLInvestigateLevel` (`+0x6348`), `m_iPLCriminalFleeLevel` (`+0x634c`),
`m_iPLCriminalAttackLevel` (`+0x6350`), `m_iPLSupernaturalFleeLevel` (`+0x6354`),
`m_iPLSupernaturalAttackLevel` (`+0x6358`) — so the maker entity can never cross a law threshold.

**What step 3 turns on.** Slot 104's missing-model arm (family Precache10's `0x1034b160`,
`0x1034c180`, `0x1034cde0`) warns and `UTIL_Remove`s the maker. Because `Spawn` dispatches it, **a
maker authored with no `model` keyfield does not survive its own spawn** — in retail and now in the
port. Six port fixtures were standing makers retail would have deleted; each now authors the key.

**Unrecovered:** nothing in the three bodies. The port's gaps are named instead: `Relink` is a seam
(no spatial partition). `npc_maker_zombie` has been a registered spawn leaf since 0018 story 2 and
its own class since story 5 fold A4.

### `CNPC_VAndreiBlood::Restore` — `0x1035cf80`, and `CNPC_VChangBros::Restore` — `0x1036b170`

_Recovered 2026-09-14, story 29d._

Both are slot-127 species overrides that chain `CNPC_VVampireBoss::Restore` (`0x103c5910`).

Andrei's is a scope-frame push, **one** `CALL` (`1035cfd4`) and a pop. Its `EAX` — the base's answer
— survives untouched to the `RET 0x4`. So this species row carries **no restore-time datum at all**,
which every one of its sibling bosses does carry. Recording it as an empty row matters because
omitting it would read as an unwalked body.

Chang's is shared by `CNPC_VChangBros`, `CNPC_VChangBrosBlade` and `CNPC_VChangBrosClaw`. It stashes
the base's answer (`1036b1e3 MOV EDI,EAX`) and returns it (`1036b210 MOV EAX,EDI`), and writes four
things: `m_fJumpGravity` (`+0x64b8`) = `_DAT_104ada44` = **2.3f** — issued between the `FLD` at
`1036b1ce` and the first call, so it lands first — then `SetBodyEmitterName(0,
"chang_powerup_emitter")`, `SetBodyEmitterName(1, "chang_powerup_emitter")` (the **same** literal at
`0x10630e54`) and `SetBodyEmitterName(2, "chang_spine_emitter")` (`0x10630e3c`). Index 3 is never
written; the base's `ClearBodyEmitterNames` is what empties it.

**Unrecovered:** nothing.

### `CNPC_VGargoyle::Touch` — `0x1037a270`

_Recovered 2026-09-14, story 29d._

`CNPC_VGargoyle#175`, 372 bytes — the gargoyle's pillar damage, and an **ordered** body whose writes
a retail program observes.

Two `FClassnameIs` compares of the toucher's `m_iClassname` (`+0x26c`), first `"pillar"`
(`0x1063a88c`) then `"central_pillar"` (`0x1063a894`). Both take the `__strcmpi` arm — a
case-insensitive **whole-name** compare — because neither literal carries the trailing `*` the
compare would honour as a prefix. This is the same predicate this class's slot-24 body
(`0x1037a450`) uses.

On a match: a stack `CVDmg_t`, `SetSrc(this)`, `Set(1, 0x80, 10)` — family **1** (lethal),
`m_bdmgTypes` **`0x80`** (`DMG_CLUB`), `m_iDiceAmt` **10** — and `m_iToHitSuccesses` (`+0x0c`) forced
to **1** (`1037a3ab MOV dword ptr [ESP+0x34],0x1`, where the descriptor sits at `ESP+0x18`). That is
wrapped by `0x101c26d0` into a `CTakeDamageInfo` whose **inflictor is the pillar itself**
(`1037a3a7 PUSH ESI`, argument 0) and whose attacker is the gargoyle (`1037a3a5 PUSH EBP`), at
damage **1.0** with `bitsDamageType` 0 and ammo type −1; then slot **142** `OnTakeDamage` is
dispatched **on the pillar** (`1037a3c1 CALL dword ptr [EDX + 0x238]`, `ECX = ESI`).

**Both paths** then run `CBaseEntity::Touch(other)` (`1037a3d0`).

**Unrecovered:** what a `pillar` does with slot 142 — it is a prop from a hierarchy the NPC census
does not carry, the same gap family Misc recorded for the slot-266 dispatch of `0x1037a450`.

### `CNPC_VGhoulCroucher::StartTouch` — `0x1037bf60`

_Recovered 2026-09-14, story 29d._

`CNPC_VGhoulCroucher#174`, 232 bytes, and the order of its writes against its dispatches is the
body:

1. `CBaseEntity::StartTouch(other)` — the base body **first** (`1037bfd0`).
2. `OnDisturbed(other)` when `other->m_pPlayer (+0xa8)` is non-null **or** `other->field_0x94` is
   non-zero. **Correction:** both offsets have already been recovered elsewhere — `+0xa8` is
   `CBaseEntity`'s player self-downcast cache and `+0x94` its cached `CAI_BaseNPC*` — so the gate
   reads **"the toucher is the player, or the toucher is an NPC"**. *Retail faults here on a null
   toucher*: `1037bfd9 XOR EBX,EBX / JMP 0x1037bfe7` falls into `1037bfe7 MOV EAX,[EDI + 0x94]` with
   `EDI` null.
3. When `m_bSpawnBurning` (`+0x6665`) is set, the `+0xa8` pointer from step 2 is non-null (so an NPC
   toucher, which reached step 2 through `+0x94`, cannot burn), and `m_flNextTouchBurnTime`
   (`+0x666c`) is **strictly** behind curtime (`FLD curtime / FCOMP [+0x666c] / AND EAX,0x4100 /
   JNZ`): restamp `m_flNextTouchBurnTime = curtime + _DAT_1044ffd0` (**5.0 s**, a *double*) and then
   `BurnPlayer(player, 5.0)`. The restamp is issued before the call (`1037c030 FSTP` precedes
   `1037c036 CALL`).

The **5.0** here is against the **10.0** (`0x41200000`) this class's own `OnVictimHitByMe`
(`0x1037be80`) passes to the same `BurnPlayer` (`0x1037c090`): standing in the ghoul's fire hurts
half as much as being hit by it.

**Unrecovered:** nothing in this body.

### `CNPC_VGuard1::OnStateChange` — `0x1037d020`, and `CNPC_VHunter::OnStateChange` — `0x10388880`

_Recovered 2026-09-14, story 29d._

Both are slot-463 species overrides whose **second** half is the shared holster/draw switch (case 1
draws through the weapon's vtable `+0x108`, cases 2/3/0xb holster through `+0x10c`, then the chain).
What each adds is a **pre-step that runs before the first `GetActiveWeapon`**.

**Correction, and it reverses both walks.** The checklist calls `vtable +0x29c` "a door reference
resolved twice" and `0x1037e2d0` "an obstructing-door hook". `+0x29c` is slot **167**, whose body on
the whole NPC line is `CAI_BaseNPC::FUN_101a67e0` (`0x101a67e0`) — six instructions that resolve
`m_hEnemy` through the global entity table. It is **`GetEnemy()`**. And `0x1037e2d0` sets `+0x6660`
and calls `InputSetRelationship(this, "player D_HT 10", 0)` — the literal at `0x1063bc28`, with
spaces and a lower-case `player`, unlike `CNPC_VCop`'s `"Player D_HT 10"` at `0x106366f4`. There is
no door anywhere in either body.

`CNPC_VGuard1`'s pre-step, **unconditional on both states**: `if (GetEnemy() && GetEnemy()->+0xa8)
0x1037e2d0(this)`. `GetEnemy()` is dispatched twice (`1037d026`, `1037d034`) and retail caches
neither call. So: *my enemy is the player, therefore hate the player at priority 10.*

`CNPC_VHunter`'s pre-step is two independent arms in this order, and both can fire on one call:

* `if (GetEnemy() && GetEnemy() && GetEnemy()->+0xa8 && NewState == 2)` — `0x10388c40(this)`, which
  is `0x1037e2d0` **minus the `+0x6660` latch byte** and is the only difference between the two
  20-byte bodies; then `0x1017f7b0(player)`; then `m_hPursuitPlayer` (`+0x6664`) = the player's own
  `EHANDLE`.
* `if (OldState == 2 && m_hPursuitPlayer resolves live && its +0xa8 is non-zero)` —
  `m_hPursuitPlayer` is cleared **first** (`1038891e`) and `0x1017f830(player)` released second.

State **2** is `COMBAT`. `0x1017f7b0` / `0x1017f830` are the hunter-pursuit refcount pair on the
**player** (`+0x1d14`), whose zero-crossings fire an output on the CSActs manager and whose DevMsg
(`"CSActs:    %6.1f - OnHunterPursuitStart - %d in pursuit"`, `0x10588818`) names them; family
Conditions already recovered the receiver and ported both.

The Hunter's tail chains `0x103871c0` rather than `0x102ae140` directly — that is
`CNPC_VHumanCombatant::OnStateChange`, the shared holster body, whose own tail is `0x102ae140`. The
two are the same sequence either way.

**Unrecovered:** the CSActs manager `0x1017fa40` / `0x1017fa70` fire into (`0x1023dcd0`'s singleton,
output list `+0x4e0`) — a soundtrack-act subsystem outside this closure.

### `CNPC_VAndreiBlood::Destructor` — `0x1035cd00`

_Recovered 2026-09-14, story 29d._

342 bytes, almost all of it allocator and C++ teardown. In order: restore its own vftable and the
secondary at `+0x19b0`; then, under a `"CNPC_VAndreiBlood::Destructor"` scope frame, `UTIL_Remove`
(`0x101cd940`) the handle at **`+0x66e0`** and then the one at **`+0x66e4`**, each only when the
handle resolves live in the global table, writing `0xffffffff` back **on that arm only**; then
destroy `m_OnTransformComplete` (`+0x6664`); then tail-jump to `~CAI_BaseNPCTroika` (`0x1028d610`).

**Correction.** The walk calls the pair "the two owned entity handles that sit past the class tail"
because the decompiler renders them as `this + 1` and `this[1].field_0x4`; the listing (`1035cd62`,
`1035cdd3`) gives the offsets, and both were already recovered — `+0x66e0` is the blood emitter
`CNPC_VAndreiBlood::StartBloodEmitter` (`0x1035e1a0`) owns and `+0x66e4` the summon emitter
`StartSummonEmitter` (`0x1035e3c0`) owns. The two removals are the whole retail-observable body:
Andrei's blood pieces do not outlive him.

**Unrecovered:** nothing.

### `CNPC_VWerewolf::~CNPC_VWerewolf` — `0x103ca7c0`

_Recovered 2026-09-14, story 29d._

484 bytes. In order: the two vftable restores; then, under the scope frame, `DAT_1093fac4 = 0` and
the `werewolf_show_debug` ConVar driven to 0 through ConVar slot 4 — **the only thing outside this
object the body touches**; then five outputs destroyed in this order, `m_OnTeleportIn`,
`m_OnTeleportOut`, `m_OnFinishCrushAnimation`, `m_OnBeginCrushAnimation`,
`m_OnConditionDeathTriggered`; then the `+0x6714` vector walked **backwards** from its `+0x6720`
count, destroying each `0x48`-byte record with `0x103dc5b0`, the count zeroed and `0x103dc220` run
over it; then the `CUtlMemory` teardowns of the `+0x6714`, `+0x668c` and `+0x665c` blocks under the
"grow size is not −1" test; then `~CAI_BaseNPCTroika`.

**Correction.** `+0x6714` / `+0x6720` is not an unnamed vector: it is the werewolf's **hint-data
array**, filled by `CNPC_VWerewolf::InitializeHintData` (`0x103d7710`) and read by
`GetHintGroundpoint` (`0x103d6770`), `GetHintTargetGroundpoint` (`0x103d68d0`), `GetHintEndEntity`
(`0x103d6390`) and `GetDataForHint` (`0x103d7620`). The `0x48`-byte stride the walk quotes is that
record's size.

**Unrecovered:** what the `+0x668c` and `+0x665c` `CUtlMemory` blocks hold — the destructor is their
only appearance in this closure, and nothing observable depends on them.

## Story 29d, family SpeciesMisc10 — the two spawn-side species bodies — `0x103567e0`, `0x103a38c0`

_Recovered 2026-09-14, story 29d._

The family's other thirty-nine rows are walked in [`shape.md`](./shape.md) (the per-species words)
and [`social.md`](./social.md) (the Newscaster queue, the cop's pursuit latch and the Sabbat
leader's round record) under the same family header.

### `CNPC_Bullseye::Spawn` `0x103567e0`

_Recovered 2026-09-14, story 29d._

Slot 103's species body — the aim-target dummy's whole spawn, which replaces the Troika line
outright. `CNPC_Bullseye` carries **no entity classname in the census**, so no map can stand one and
the arm is unreachable at runtime; it is ported and exercised by name.

In retail's order:

1. `103567e6` — slot `0x1a0` (`Precache`).
2. `1035680d` — `SetSize(-16,-16,-16 .. 16,16,16)` through `0x101cf390`. The MAXS triple is built on
   the stack first and the MINS handed as the first argument.
3. `10356818` — slot `0x174` with `(0, 0)`.
4. `1035681f` — `SetBloodColor(0xf7)`.
5. `1035682a`..`10356840` — `m_fEffects` (`+0x17c`) = 0, `m_flFieldOfView` (`+0x19c`) = **0.5**
   (`0x3f000000`), `m_flGravity` (`+0x1fc`) = 0.
6. `10356850` — `SetBloodColor` **again**: `0xf7` when spawnflag `0x80000` is set and `-1` otherwise.
   **The second call is what actually decides the blood colour.**
7. `1035685f` — `AddFlag(0x2000)`.
8. `1035686c` — `ThinkSet(LAB_100097fa, 0.0)` followed by
   `m_flNextThink = curtime + _DAT_104493d0`. That cell is a **DOUBLE reading 0.1**, read at file
   offset `0x4493d0` of the pinned `vampire.dll` — not the `0.0` the `ThinkSet` argument carries.
9. `103568b4` — `SetSolid(2)` and `AddSolidFlags(+0x2b4 | 0x10)`, each under its own
   `CBaseEntity::SetSolid` / `AddSolidFlags` scope-trace frame.
10. `10356906` — `AddSolidFlags(| 4)` only under spawnflag `0x10000`.
11. `10356916` — `m_takedamage` (`+0x3ec`) = 0 under spawnflag `0x20000`, else 2.
12. `10356928` — `Relink`, then `m_fEffects |= 0x40`, then `PhysicsCheckWater`, then
    `AddFlag2(0x10)`, in that order.

**Unrecovered:** what `LAB_100097fa` — the think function the spawn arms — does.

### `CNPC_VPedestrian::CreateCorpse` `0x103a38c0`

_Recovered 2026-09-14, story 29d._

Slot 301's species body, and the ORDER is the whole of it.

BEFORE the base, it snapshots the collision OBB through `m_Collision` (`+0x270`) vtable `+4` into
`m_vecPreDeathMins` (`+0x6660`) and vtable `+8` into `m_vecPreDeathMaxs` (`+0x666c`), three floats
each. That has to come first because `CBaseCombatCharacter::CreateCorpse` resizes the hull. Then the
base; then `ThinkSet(NULL, 0.0, NULL)`, which CLEARS the think function so a pedestrian corpse never
thinks again; then a scope-trace push naming `"CBaseEntity::SetSolid"` with `GetDebugName` from
`m_iName` (`+0x26c`) and `SetSolid(SOLID_NONE)` on the collision.

**Unrecovered:** none of the collision-property accessors on `m_Collision` (`vtable +4` / `+8`).
The two vectors are read by `CNPC_VPedestrian::OnRestore` `0x103a25a0` (story 29e, family
Lifecycle19) when resurrecting a dead pedestrian: `0x101cf390` restores them as the pre-death
bounds.

## Story 29e, family Lifecycle19 — `NPCInit`, `StartNPC`, `OnRestore`

_Recovered 2026-09-14._ `.rdata` cells from the pinned `vampire.dll` (the corpus holds none of
them): `_DAT_10449280` = **1.0** (double) — every `NPCInit`/`StartNPC` think arm is `curtime <=
1.0`, the map's first second; `_DAT_104493d0` = **0.1** (double); `_DAT_104704c0` = **512.0**
(double), so the Werewolf floor adds `sqrt(512)`; `_DAT_104d0080` = **2.0944 rad = 120°**, FOV
`cos = −0.5`. Corrections from the listing (not the checklist walks) are applied in place:
`0x10360ce0` `+0x66d0` is `m_iLastJumpPositionIdx`; `0x1036b050` teleport words sit at `+0x66bc` /
`+0x66c8` / `+0x66cc`; `0x103a25a0` pre-death bounds are `+0x6660`/`+0x666c`; `0x103cac20` opens
with `SetEnemy(NULL)` and accumulates the teleport floor; `0x10273ad0` `ThinkSet`s both arms of
the first-second test; capability mask **4**; FrenzyShadow tails into `0x10376c10`.

### `CAI_BaseNPC::NPCInit` `0x10273390`

Slot 420's base body. In order: `SetNPCTransparent(1)`; `m_hLastDamageEnt = -1`;
`m_bCondTookDamage = 0`; `ClearAllClientRagdolling`; `AddFlag(0x12000)`; `m_flGravity = 1`;
`m_takedamage = 2`; motor reset `0x102e0b40` then yaw from `GetAbsAngles[1]`, shifted by ±180
when motor `+0x28` is set, stored to motor `+0x34` directly when motor `+0x1c == 180.0` else
through `0x102e0a80`; `m_iMaxHealth = 100`; `m_lifeState = 0`; provenance `+0x1b3c`/`+0x1b40`
(ABSENT in the port) line `0x1af1`; `m_IdealNPCState = 1`; `SelectHeaviestSequence(ACT 1)` else
`0xf1` else `m_nSequence = 0`, the winner through `SetIdealActivity` `0x10272650`;
`m_bShouldMove = 0`; `m_iCollisionMask = 0x202400b`; navigator `+0x2c = DAT_1093407c`;
`ClearSchedule` `0x10280d30` (every class); navigator reset `0x102ee270`; `0x10095be0`;
`0x1008f540`; `m_pHintNode = 0`; `m_afMemory = 0`; `SetEnemy(NULL)`; `m_flDistTooFar = 1024`
with `SetDistLook(3072)`, replaced by `1e9` / `6000` on spawnflag `0x100`; `m_bKeepSound = 0`;
zero six words at `+0x5c5c`; `0x102cc7e0` on the delayed CONDITION list (`+0x1a9c`) and then on
the delayed SOUND list (`+0x1ae0`) — two different lists, not the same one twice; `0x10274ca0`;
`m_pfnUse = LAB_10004da9`; while `curtime <= 1.0` `ThinkSet(0x10273aa0)` and `m_flNextThink =
curtime + 0.1`, otherwise call `0x10273aa0` inline; then `m_bForceStateChange = 0`, `+0x5b58 =
0`, `m_bfNPCFrenziedFlags = 0`, `+0x5b5c = curtime`, friend-block timers, `m_flLastStateChangeTime
= 0`, `m_eEnemyOccludedCheck = 10`, `m_hShootTargetOverride = -1`, `+0x5b90 = 0`, cine/choreo
clears, `0x10270180(NULL, 0)`, and the last-damage / last-attack / sound-wait / eye-look /
weapon-search / wait-finished stamps zeroed last.

`m_bCineScriptHidden` (`+0x5d78`) is cleared here, and it is its OWN byte: the field ledger gives
it two accesses in the whole image (this clear) plus the read-and-clear pair in
`CAI_BaseNPCTroika::ScriptUnhide` (`0x102c1ec0`). It is NOT `m_bScriptHidden` (`+0x00f4`, the word
`0x100b5190` answers) and NOT `m_fEffects`'s `EF_NODRAW` (`+0x19c`); the port binds all three
separately.

**Unrecovered:** `DAT_1093407c` (the node network); motor `+0x1c` (the 180.0 sentinel); the
health cvar at `DAT_10923f14` — an unnamed `ConCommandBase*` this image never writes, whose four
referrers are `0x1029a0b0` and its three thunks, so the value it seeds `m_iHealth` with is not in
the corpus; `m_pfnUse`.

### `CAI_BaseNPCTroika::NPCInit` `0x1029a0b0`

Slot 420. Raises `DAT_10937cf1` (`1029a0b3`), zeros `m_fEffects` (`1029a0c0`), seeds `m_iHealth`
from the unnamed cvar object (`1029a0c6`–`1029a0ef`), `m_NPCState = 0` — **NPC_STATE_NONE**, the
state the body leaves behind until the first think transitions it (`1029a0f5`) —
`m_flNextListenTime = 0` (`1029a0fb`), `m_pfnTouch`, then the whole base body. Seeds PVS/LOS true
and all eight think stamps to curtime; the occluded delays from the tuning record;
`0x102b53d0(curtime)`; zeros the two `sPatrolPath` pairs (`+0x658c`/`+0x6590`, `+0x6594`/`+0x6598`)
and the goal-tolerance / interrupt-distance block; `m_hMoveTargetEnt = -1` then
`m_iHitBuildupCount = 0` (`1029a28b`, BEFORE the spawn-equip block) and `m_flWeaponScareTime =
-1.0`; spawn-equip of `m_altEquipment` then `m_spawnEquipment` behind slot 513 bit `0x200000`,
skipping a 2-byte `"0"` and 15-byte `"item_w_unarmed"`; the enemy store's back-pointer and its
`FreeKnowledgeDuration`; `Player %s` relationship; clears `DAT_10937cf1`; `0x102b5dc0(this, 0)`;
the cover/cower block and the two sound clocks `+0x6418` / `+0x623c` (`+0x641c` is **not** written
here); `m_bIsBCCTargetable = 1` only when `m_statTemplate` is non-empty; `m_bIsAlive = 1`; law
timers; `m_iPLCriminalActProcessed` / `m_iPLSupernaturalActProcessed = -1`; melee block; slot 593;
saved attack extents `(-1,-1,-1)`; `0x102c54c0`; `m_bJumping`/`m_fJumpGravity`; the four discipline
bit words `+0x0eb0`..`+0x0ebc` (`1029a589`); stat-list `Set(0xf, 0)` / `SetBase(0xc, BloodPool)`;
`m_bReturnToInitialPos`, the three see-unknown words and `m_flMeleeHeightDiffTimer`;
`InitPerceptionDistances`; `m_nCurrDisposition = DAT_10924984`; `m_hEyeLookTarget = -1`;
`m_RelativeEyeTarget = 0`; teleport-timer tail (`> 0` → `curtime + self + 4` and
`0x102ae7f0(0xfe)`, **whose whole body is `*(this + 0x65c8) = param_1`** — it writes
`m_iForcedSchedule` and installs no schedule at all; else the timer is zeroed).

`0x101e8c50` / `0x101e8c70` / `0x101e8c30` / `0x101e8bf0` are **not cvars**: each is a seven-byte
`__fastcall` getter of a float field on the process-global `CVFeatList_t` (`0x10739d08`,
`101e4d90`), which `0x101e6310` fills from `vdata/system/Rules.txt` through
`KeyValues::GetFloat(key, default)` — `+0x288 Npc_Combat_Info/OccludedDelayNormal` (image default
**0.5**), `+0x28c .../OccludedDelayCover` (**5.0**), `+0x284 .../FreeKnowledgeDuration` (**0.25**)
and `+0x27c Zombie_Grapple_Info/DelayInitial` (**20.0**, the shipped `rules.txt` authors 10.0).
The 3.4 / 10.0 / 0.5 triple is `CNPC_VCamera::NPCInit`'s own immediates, not this body's.

**Unrecovered:** the health cvar object `DAT_10923f14`; `DAT_10924984` (the disposition table's
default index — no writer in the image); the enemy-store object's identity at `+0x5d88`.

### `CAI_BaseNPC::StartNPC` `0x10273ad0`

Slot 422 base. Clears `m_bDidMaintainSchedule` and `+0x1b4c`. Drops to the floor unless movetype
is 5 or 6, or slot 513 has bit 2 (mask 4), or spawnflag 4: probe 256 units down,
`Warning("NPC %s stuck in wall--level design error\n")` (`105cc558`) on a miss, `SetOrigin` either
way; skipped path `RemoveFlag(FL_ONGROUND)`. `m_target` non-null → `FindEntityByName` into
`m_pGoalEnt`; miss `Warning("ReadyNPC()--%s couldn't find target %s")` (`105cc528`, THREE pushes:
the classname and the target name); hit `SetState(1)` then `SetSchedule(3)`. Slot
545 `InitSquad`. `ThinkSet` on **both** arms of `curtime <= 1.0`; stamp is `curtime +
RandomFloat(0.1, 0.4)` during the first second and `curtime` after. Arrival activity/sequence
cleared. Spawnflag `0x80` → `SetState(1)`, `SetActivity(1)`, `SetSchedule(0x2d)`.

**Unrecovered:** the move-probe `0x102e7880` (no `m_pMoveProbe` store).

### `CAI_BaseNPCTroika::StartNPC` `0x1029a8b0`

Base `StartNPC`, then `ThinkSet` again, `SetFollowerBoss` `0x102c44e0` (unported; seam),
`SetFollowerType` `0x102c4680`, `SetClosestPlayer` `0x10293a80`.

**Unrecovered:** `0x102c44e0` (FindNamedEntity / squad refusal / frenzy bits).

### `CAI_BaseNPC::OnRestore` `0x1027bf50`

Give-up unless: state is not SCRIPT or the cine handle resolves live; saved schedule name
non-empty and version word `+0x19b4 == 1`; flags bit 1 implies a live enemy; flags bit 2 implies
a live `m_hTargetEnt`. Task index `> 0x29` clamps to 1. A kept schedule is looked up by name
(`1027c019`) and then CHECKSUMMED: `0x1023f040` / `0x1023f0c0` over `schedule+0x20` for
`schedule+0x24 << 3` bytes / `0x1023f060`, compared against the save's own word at `+0x1a3c`
(`1027c064`); a mismatch writes `m_pSchedule = 0` (`1027c068`) and the body then takes the give-up
arm. Null or give-up → clear the refind latch and
`0x1027be60` (clear nav goal, clear schedule, zero the six-word block when no enemy, SCRIPT with
a dead cine → `SetState(IDLE)` and `DevMsg("Scripted Sequence stripped on level transition")`).
Else the refind latch takes flags bit 2. Then base combat-character restore, `+0x5b90 = 0`, and
either `0x102ee270` or `0x102ee1e0` (failure runs `0x1027be60`).

**Unrecovered:** the schedule-name registry walk `0x1030f350`;
`CBaseCombatCharacter::OnRestore` `0x10323b60`; path refind `0x102ee1e0`.

### `CAI_BaseNPCTroika::OnRestore` `0x102998c0`

Validate both patrol-path cells — `0x1029f610` per cell, each releasing through `0x1029f5d0` on
its own refusal, so the two routes are independent; base `OnRestore`; scan the interesting-place
list `0x102db5e0` into `+0x62ec`; rebind ped link `+0x6310`/`+0x6314` (OOB increments
`DAT_106c994c`); shoot-at-hint re-arm; follower boss/type; combat-start activity `0x1029f340`,
which is itself `if (name == NULL) return -1; return ActivityNameToId(name)` over `0x10412520`;
`+0x62e9 = 1` (`CBaseEntity`'s spawn-called byte); `0x10299a80`; script-hidden → `ThinkSet(NULL)`
and `m_flNextThink = FLT_MAX`; slot 593.

`0x102db5e0` walks every interesting place (`DAT_10927194`, `+0x540` next) and answers the LAST
one whose marker table (`+0x580`, `+0x588` records of stride `0x1c`, first dword the occupant)
holds this NPC — it does not break on the first hit. `0x10299a80` then re-checks that answer and
has two refusals, each a `DevMsg` and a cleared `+0x62ec`: the place is not on the live list at
all (`"ERROR: %s loc( %6.2f, %6.2f, %6.2f) thinks they are at an interesting place that no longer
seems to be a valid entity!\n"`, `105d93d8`), or it is on the list but its marker table does not
name this NPC (`"ERROR: %s loc( %6.2f, %6.2f, %6.2f) thinks they are at %s but that
InterestingPlace, loc(%6.2f, %6.2f, %6.2f) does not know that.\n"`, `105d9338`).

**Unrecovered:** the node network behind `0x1029f610`; ped-link search `0x102f96e0`.

### `CNPC_VCamera::NPCInit` `0x103692c0`

Replacement, never Troika. Snapshots origin; slot 74 of `DAT_1070ba0c` false → `UTIL_Remove` and
return — and that refuse arm carries **no** `MOV byte ptr [0x10937cf1],0`, so the process-wide
in-NPCInit latch is left SET when a camera deletes itself. Then a camera-shaped copy of the base
init (hard-coded occluded delays 3.4/10.0, report stamps 0, enemy-store interval the 0.5 literal)
that **stops** before the stat-list / disposition / teleport tail. Two places where it is NOT the
Troika body: it seeds only `m_bInPlayerPVS`, `m_bInPlayerLOS` and `m_flNextPlayerLOS`
(`103694c2`/`103694c9`/`103694d0`) and leaves the three stamps beside them at their spawn values;
and its tail runs `+0x6414`, `+0x6418`, `+0x623c`, `+0x60a4`, `+0x60a8`, `+0x6398`, `+0x639c`,
`+0x63a4`, `+0x63a8` and then jumps straight to `+0x6070`, never touching `+0x606c`.

**Unrecovered:** the engine query at `DAT_1070ba0c` slot 74 (seam, default admits).

### `CNPC_VWerewolf::NPCInit` `0x103caef0` and re-arm `0x103cac20`

Writes `"Werewolf"` into `m_statTemplate` before **and** after Troika `NPCInit`. Inventory
destroy, `GiveBaseFightingItems`, `AddClassRelationship(1, D_HT, 10)`, `m_takedamage = 1`, law
thresholds 999999, perception block (`m_flSeekDistBase = 4096`, hearing 3, `DistTooFar = 1e9`,
FOV `cos(120°)`, `SetDistLook(6000)`, `InitPerceptionDistances`, investigate 3), four
`CapabilitiesAdd`, six hint pointers cleared, then `0x103cac20`. That helper is twenty-three
writes, not two: `SetEnemy(NULL)`, `m_hClosestPlayer = -1`, the morph timers `+0x66a4`/`+0x66d4`/
`+0x66d8`, `+0x66a1`, `+0x66a8`, `m_bPlayFrustration +0x66a9`, the cached fake-hull point
`+0x66dc`/`+0x66e0`/`+0x66e4` ← `vec3_origin`, `+0x66ec` and `+0x66f4` ← curtime, the two hint
search starts `+0x66b8`/`+0x66ac` ← NULL, the hint-node cache `+0x6708` and
`m_iRandomMoveHintNodeZone +0x670c` ← -1, the hint-gate word `+0x66e8`, `+0x66f8`, `+0x66fc`, and
the nearest-node-to-player pair `+0x6700`/`+0x6704`; then `+= sqrt(hullHalfX² + hullHalfY²)` and
`+= sqrt(512)` onto `+0x66cc`/`+0x66d0` — the load path `0x103cabf0` runs the same helper, so the
floor grows on every save/load.

**`0x103cac20` walked again from the listing (0019 story 8 pass I, lane L12, 2026-09-27).**
No branch. `SetEnemy(NULL)`; `m_hClosestPlayer` = -1; `+0x66a4` = 0; bytes `+0x66a1`, `+0x66a8`,
`+0x66a9` = 0; `+0x66d4`, `+0x66d8` = 0; `+0x66dc..e4` = `vec3_origin`; `+0x66ec`, `+0x66f4` = curtime;
`+0x66b8`, `+0x66ac` = 0; `+0x6708`, `+0x670c` = -1; `+0x66e8`, `+0x66f8`, `+0x66fc`, `+0x6700`,
`+0x6704` = 0 (`0x103cac2a`..`0x103cacd5`). Then `+0x66cc += sqrt(dx² + dy²)` over the hull `+0x1568`'s
half-extents (`0x103cad59`) and `+0x66d0 += sqrt(512.0)` (`_DAT_104704c0`, a double, `0x103cad6d`).
Both are `+=` and the body runs from `NPCInit` and `OnRestore`: the floor grows with every load
(retail defect, reproduced).

**Unrecovered:** the hull half-extents `0x102d6100` / `0x102d6120` (the first `+=` term is 0
without a hull); the helper's retail name (the port's `WerewolfResetHuntState`); `+0x66f8`/`+0x66fc` meaning.

### `CNPC_VCamera::StartNPC` `0x10369930`

Replacement, not a Troika chain. Floor-drop only when the model is `models/null.mdl`
case-insensitive AND movetype is neither 5 nor 6 AND neither solid-flags bit 2 nor spawnflag
bit 2 is set. Warning on a miss is `"NPC %s stuck in wall, level designer"`. `m_target` gated
non-empty; miss Warning `"ReadyNPC : %s couldn't find target"`. `ThinkSet` on both first-second
arms, then a final `ThinkSet(LAB_1000f4e8, 0.0)` that does not restamp `m_flNextThink`.

**Unrecovered:** move probe `0x102e7880`.

### `CNPC_VFrenzyShadow::NPCInit` `0x10375c80`

Weapon create (`item_w_fists`, `0x10375c89`), then `Inventory_Can_Insert` ? slot 383
`Weapon_Equip(w, 0)` : `0x1024f7b0(w)`, and an unconditional `|= 0x40` into the weapon's `+0x19c`
(`0x10375cb6`, no null test — a failed create faults there) BEFORE
`CNPC_VPlayerController::NPCInit`. Then ideal state `0xb`, `SetState(0xb)`, frenzied flags
`0x5ddf`, senses on, `+0x6664 = +0x6668 = 0`, speed scale 8, nav-ignore physics, tail
`JMP 0x10376c10` (hostile recount).

The recount walks the static scratch list `DAT_1093ada8[0 .. DAT_1093ae9c)`. **Corrected by 0019
story 5 fold A2:** the list is NOT empty on the shipped path — `CBasePlayer::CheckForPlayerFrenzy`
`0x10161fc0` calls its producer `0x10376d00` (a ±1024/±1024/±128 box sweep around the player,
thunk call at `0x1016201a`) just before it creates the `npc_VFrenzyShadow` (`0x10162050`), so the
recount counts the entries that hate the friend player and seeds enemy memory with every entry.
The port has no body for the frenzy check, so its seam for the list answers empty
(`FElysiumNpcFrenzyShadow::HostileScratchList`).

**Unrecovered:** the port's weapon create (a seam: an entity created from `NPCInit` would grow the
list the spawn pass walks), and the producer `0x10376d00`'s filter words.

### `CNPC_VGhoulCroucher::NPCInit` `0x1037b290`

Scope-trace name is `"CNPC_VWerewolf::NPCInit"` (retail copy-paste). HumanCombatant first,
`Inventory_Destroy`, then `0x1021fe50` of `item_w_claws_ghoul` or `item_w_knife` off
`m_bSpawnDisturbed`, `AddMiscFlag(0x10)`, burning particle when `m_bSpawnBurning`, hate class 1.

**Unrecovered:** particle create `0x100fbc90`.

### `CNPC_VNewscaster::NPCInit` `0x103a0420` / `CNPC_VPlayerController::NPCInit` `0x103a4580`

Troika first. Law thresholds 999999, witnessed scramble, senses off, not BCC-targetable.
Newscaster investigate mode 0; PlayerController mode 6, `SetForceFrequentThink(true)`,
`m_hFriendPlayer` from owner or −1.

**Unrecovered:** the CSecureType tag bytes beyond the two the port already carries.

### `CNPC_VSabbatLeader::NPCInit` `0x103a6d40` / `CNPC_VSheriffMan::NPCInit` `0x103ae6c0`

SabbatLeader: VampireBoss first, then route-fail/activated/model/classname/emitters, last-attack
= curtime, roar budget 3. SheriffMan writes seven fields BEFORE VampireBoss (swarm handle,
teleporting/dead/activated, last teleport origin and time), then model/classname,
`0x103c6a00`, jump gravity 2.0, `DAT_109340d8 = 0x15`.

**Unrecovered:** nothing on the species writes.

### `CNPC_VTzimisce::NPCInit` `0x103b91d0` / `CNPC_VVampireBoss::NPCInit` `0x103c5840`

Tzimisce: Troika, first-enemy, path mode, expression-map reset, 3 s pounce/body windows,
pickup −1, collision-ignore re-arm. VampireBoss: Troika, clear model name and emitters,
classname `"npc_VVampireBoss"`, jumping 0, transform reset, jump gravity 1.

**Unrecovered:** expression-map reset `0x103b9f50`.

### `CNPC_VZombie::NPCInit` `0x103defc0`

Crawl-out latch FIRST, then Troika, `item_w_zombie_fists`, hate class 1, Hide on self, SetEnemy
and SetTarget of closest player, grapple timer, `SetSchedule(0x161)`.

**Unrecovered:** zombie-grapple cvar `0x101e8bf0`.

### `CNPC_VPedestrian::OnRestore` `0x103a25a0`

Troika first, then four gates: bool argument, `m_eLevelResetType != 2`, (alive OR type 0),
script-hidden clear. Inside: destroy inventory, restore pre-death mins/maxs when dead,
slot 420 `NPCInit`, clear follower-boss name, origin/angles from `+0x62a8`/`+0x62b4`,
collision writes, `SetMoveType(4)`, `SetHullSizeNormal(false)`, Relink, `CreateVPhysics`.

**Unrecovered:** Relink `0x101cf600`; pre-death collision OBB (the two vectors are the restored
words).

**Unrecovered (family):** `0x102c44e0` SetFollowerBoss; move probe `0x102e7880`; node network
`DAT_1093407c`; interesting-place list; ped-link search; combat-start activity names; camera
engine query; occluded-delay and zombie-grapple cvars; `CVStatList_t` type-0 list; expression-map
reset `0x103b9f50`; ghoul particle create `0x100fbc90`; `m_flNextListenTime` (bound private as
last-listen); `m_bCineScriptHidden` (`+0x5d78` bound to `bHidden`, not cleared on `NPCInit`).


## The death chain, kill to corpse -- `0x10265ad0`, `0x103392c0`, `0x1032c0e0`, `0x10286801` (2026-09-22)

Recovered for 0019/3 pass D, which needed bodies for base `DIE`'s second and third tasks. Every
dispatch claim below was re-derived from the `vampire.dll` jump tables rather than from the
decompiler's switch reconstruction, because the two functions at the centre of this chain
(`CBaseCombatCharacter::Die`, `GetDeathActivity`) are reached only through thunks and so report
**zero callers** in the call graph.

### There is no `CAI_BaseNPC::Die`

No such body exists. The generic force-death helper is **`CBaseCombatCharacter::Die`
`0x103392c0`** -- non-virtual, in no vtable, `RET 0xc` (three stack arguments), reached only through
the thunk `0x100034f9`, which has exactly six call sites: `0x101de7c2`, `0x102abc1e` (`TASK_DIE`),
`0x102abc7e` (`TASK_DIE_IMMEDIATE`), `0x10339165`, `0x1033951e`, `0x1033ad0e`.

Its body guards on `m_lifeState != 2` (`+0x200`), builds a synthetic `CVDmg_t` with `SetSrc(this)`,
`m_iDiceAmt = 1`, `m_iToHitSuccesses = 1` and damage `1.0f`, sets stat `(0xf, 0x11)`, then
dispatches **`Event_Killed` (slot 144, `+0x240`)** and **`Event_Dying` (slot 403, `+0x64c`)**. So
`Die` does not kill anything itself: it re-enters the ordinary kill path with a manufactured blow.

### The two death tasks

`DIE` (`0x2b`) is `TASK_STOP_MOVING 0x69`, `TASK_SOUND_DIE 0x49`, `TASK_DIE 0x5f`.
`SCHED_DIE_RAGDOLL` (`0x2c`) is the first two only.

Troika's `StartTask` dispatch (`0x102a1910`: `idx = id - 5`, bound `0x144`, byte table `0x102a7ab8`,
targets `0x102a77f8`) sends **both** `0x49` and `0x5f` to its default forwarder `0x102a77e2`, which
thunks through `0x10013ef3` to `CAI_BaseNPC::StartTask` `0x102827f0` (`idx = id - 1`, bound `0x11f`,
byte table `0x10287138`, targets `0x10286f8c`).

- **`TASK_SOUND_DIE` `0x49` -> base arm `0x10286843`.** Four instructions: call vtable slot 488
  (`+0x7a0`), then `TaskComplete(false)` through thunk `0x1000ac68` -> `0x10273e80`. It reads no
  float argument and writes no field. It has **no `RunTask` arm** -- base `RunTask`'s table maps
  `0x49` to the no-entry handler `0x102896f5`, which only DevMsgs "No RunTask entry for %s"
  (`0x105ce044`) and completes; normal execution never reaches it.
- **`TASK_DIE` `0x5f` -> base arm `0x10286801`,** shared with `TASK_DIE_IMMEDIATE` `0xdf` (both tag
  `0x4f`). Three instructions: clear the navigator goal through `0x10001d98` -> `0x102ee270`, write
  `m_lifeState +0x200 = 1`, return. **No `TaskComplete`, no `TaskFail`, no wait deadline** -- the
  program parks on this task.

### `TASK_DIE`'s `RunTask` arm is Troika's, not the base's

This is the correction that matters, because every VtMB NPC is `CAI_BaseNPCTroika`-derived.
Troika's `RunTask` (`0x102aacf0`: `idx = id - 2`, bound `0x147`, byte table `0x102ac844`, targets
`0x102ac760`) maps `0x5f` to **`0x102abb90`**, *not* to its default `0x102aad69`. The base arm
`0x10288fc4` -- `m_lifeState = 2`, `ThinkSet(null)`, `m_flPlaybackRate = 0`, collision collapse,
`ShouldFadeOnDeath` -> fade or carcass sound -- therefore **does not run on any ordinary NPC**.

The Troika arm (shared with `TASK_DIE_GIB` `0xe9` and `TASK_DIE_DUE_TO_PLAYER` `0xeb`):

1. Gate: `(IsActivityFinished() [slot 251, +0x3ec] && m_flCycle +0x6f8 >= 1.0 [0x10449280])
   || m_IdealActivity +0xff0 == 1 (ACT_IDLE)`. Otherwise return and keep waiting.
2. Resolve `m_hClosestPlayer +0x628c` through the entity table `0x10566458`; then for `0xe9` **and**
   `0x5f` alike, overwrite that target with `this` (`0x102abbf4`).
3. If `m_lifeState +0x200 == 1`, write it to `0`.
4. Call `Die(target, 0, 0)` through `0x100034f9`.

It never calls `TaskComplete` or `TaskFail`. **Nothing ends the `DIE` program.** What ends the NPC
is `CreateCorpse` taking the entity out of the world underneath it.

The `|| m_IdealActivity == ACT_IDLE` arm is load-bearing: it is why an NPC with no death performance
commits on its first poll instead of hanging forever on a clip that will never play.

### Nothing in base `DIE` plays a death animation

`GetDeathActivity` `0x10327e20` (returns `0x43` at `0x10327e91`) is **dead code**: zero direct
calls, its only thunk `0x1000b082` has zero call sites, and no vtable or data table contains its
address. It must not be ported.

Retail's visible death pose has exactly three producers, all outside base `DIE`:

- **`BecomeClientRagdoll` `0x10090180`** -- the ordinary case. When `forceBone == -1` it calls
  `SelectWeightedSequence(ACT_DIERAGDOLL = 0x21, -1)`, writes `m_nSequence +0x6f0` and
  `m_flCycle +0x6f8 = 0`, and runs `ResetSequenceInfo` `0x10090950` before handing to physics. It
  answers false when the model interface (`VModelInfoServer001` at `0x1070b250`, slot 18) reports no
  ragdoll, and then only collapses collision bounds. This is why `SCHED_DIE_RAGDOLL` needs no
  `TASK_DIE`: the seed pose *is* the death, and physics finishes it.
- **`TASK_PLAY_DEATH_SEQUENCE` `0x149`**, Troika `StartTask` arm `0x102a779d` -- the argument, then
  `ACT_DIESIMPLE 0x1d`, then `ACT_IDLE 1`, through `0x10295460` to `SetIdealActivity` `0x10272650`.
  Named only by the Discipline death schedules, **never** by base `DIE`.
- **`m_iInterestingDeathActivity +0x6308`** -- exactly four sites in the image: written and read in
  Troika `OnTakeDamage` `0x102beda0` (`0x102beecd`, `0x102beef2`), read by the Troika `StartTask`
  arm `0x102a650b`, cleared to `-1` at `0x102a6454`.

**So on the ordinary non-ragdoll `SCHED_DIE` path there is no death-activity setter at all.** The
NPC keeps whatever it was playing, `TASK_DIE` waits it out, and commits. This is a recovered
negative, not a gap.

### The ordered chain

1. `CBaseEntity::TakeDamage` `0x100a1250` -> slot 142 -> `CAI_BaseNPC::OnTakeDamage` `0x10265e90` ->
   `CBaseCombatCharacter::OnTakeDamage` `0x1032ef60`, which dispatches slot 144 at the health
   threshold.
2. **`CAI_BaseNPC::Event_Killed` `0x10265ad0`** (slot 144): refuse re-entry when the running
   schedule is `NPC_FREEZE 0x3a`; the scripted-sequence deferral (`m_hCine`, `spawnflags & 0x2080`);
   stop looping sounds (slot 511, base `0x1027caa0`); **`DeathSound` (slot 488) at `0x10265cb8`**;
   the `OnDeath` output; optional `BecomeDead` `0x10265a40`; then
   `CBaseCombatCharacter::Event_Killed` `0x1032b9b0`.
3. `0x1032b9b0` writes `m_lifeState = 1`, computes and clamps the death force, and calls slot 301
   **`CreateCorpse` `0x1032c0e0`** -- death bone or `Bip01 Spine2`; `BecomeClientRagdoll` for normal
   NPCs; `SpawnStaticCorpse` `0x1032be80` for `No_Ragdoll_Death`, burning and static cases. The
   ragdoll branch keeps the dying NPC as the corpse; the static branch spawns a second entity and
   schedules the original for removal. Cleanup at `curtime + 10.0` (`_DAT_1044e664`); static
   no-ragdoll at `curtime + 0.5` (`_DAT_104454d0`).
4. Back in `0x10265ad0`: `m_IdealNPCState +0x5cc4 = 7`, `COND_LIGHT_DAMAGE 0x4c`,
   `m_hLastDamageEnt +0x5b7c`, `m_bCondTookDamage +0x5b80 = 1`, squad vacate; then slot 552
   `ShouldFadeOnDeath` `0x1027a400` (spawnflag bit 9) choosing `SUB_StartFadeOut` `0x102695d0` or
   `SOUND_CARCASS 0x20` at volume 384 for 30 s through `0x101babc0`; finally `SetState(7)`
   `0x1026e340`.
5. **There is no next NPC think** (_corrected 2026-10-04, 0002 V4 packet S1; steps 5–6 as first
   written had every death pass through `SelectSchedule`_). `CreateCorpse` replaces the NPC's main
   think on every NPC arm, rig or no rig: `ThinkSet(SUB_PVSRemove 0x102696f0)` at `curtime + 10.0`
   (`0x1032c404`); for a burning death (`0x10207df0`) `ThinkSet(SUB_Remove 0x101c0b10)` at `+10.0`
   (`0x1032c32f`); for `MiscFlag 0x80000` `SUB_Remove` at `+0.5` on the NPC and the tail think on
   the static corpse. Two bodies replace it again afterwards: `SUB_StartFadeOut` `0x102695d0`
   (`ShouldFadeOnDeath`, spawnflag bit 9; `SUB_FadeOut` at `curtime + 10.0`, a double) and
   `CNPC_VPedestrian::CreateCorpse` `0x103a38c0` (`ThinkSet(NULL)`: a pedestrian's corpse has no
   think and this chain never removes it). The only skips are a NULL corpse and a player corpse.
   So step 4's `SetState(7)` is written and never read by a schedule: **an ordinary kill plays the
   death sound once, runs no `DIE` program and never reaches the `ACT_DIERAGDOLL` seed** (the
   corpse ragdolls from the pose it holds; `CreateCorpse` always passes a real bone).
6. **The state-7 fork** `0x1028a8ec-0x1028a92b` in `CAI_BaseNPC::SelectSchedule` `0x1028a380`
   (`BecomeClientRagdoll(vec3_origin, -1, 0) ? 0x2c : 0x2b`; `CAI_BaseNPCTroika::SelectSchedule`
   `0x102af660` has no state-7 case) is reached only by an NPC that is in state 7 **with its think
   intact**. The writers of 7 other than `Event_Killed`: `CineCleanup` `0x1027d170` with
   `m_iHealth < 1` (line `0x2b22`: ideal 7 and `COND 0x4c` — the resume of a kill `Event_Killed`
   deferred under a started script); `0x1027d0a0` from `SelectIdealState` case 4 (`0x1026f660`,
   `0x102ad660`) when `m_lifeState == 1`; `CNPC_VWerewolf::SelectIdealState` `0x103d0820` (dead or
   `COND 0x7a`); the tentacle `0x1039e310` and base case 7, which only sustain it; and
   `CNPC_VZombie::CreateCorpse`'s collapse arm, which keeps the NPC think. (`CineCleanup`'s
   `m_lifeState == 1` arm also sets 7 but fades the body or clears its think.) On that route a
   rigged model takes the `ACT_DIERAGDOLL` seed and loses its think inside `BecomeClientRagdoll`
   (no `OnDeath`, no removal think); a rig-less one runs `DIE`, whose `TASK_DIE` commits through
   `Die` -- re-entering step 2 -- and it is that `Event_Killed` whose `CreateCorpse` makes the
   corpse. Neither witness map (`sm_hub_1`, `sp_tutorial_1`) carries a Werewolf, tentacle or
   zombie; both carry `CCineNPC`, so the deferred script death is the one reacher there. The other
   two `BecomeClientRagdoll(…, -1, …)` callers in the image are not NPC paths (`0x1012b370`, the
   `raggib` entity; `0x102b5bb0`, no static caller).

### A death sounds more than once

An ordinary kill's slot-488 call at `0x10265cb8` plays **once**, guarded by the grapple-role/
partner test on `+0x153c/+0x1538`. CreateCorpse replaces NPCThink on every ordinary NPC arm.
Only the separate rig-less state-7 fork reaches SOUND_DIE -> DIE -> `Die 0x103392c0` ->
Event_Killed and the three-sound route; neither ordinary death arena record reaches that fork.
Troika's hook `0x10293ec0` caches the vdata sound-table entry named "Death"
(`0x105d8c30`) once in `DAT_10924d64` under the guard byte `DAT_10923f0d`, then plays it as sound
type 2 at volume `1.0` (`0x3f800000`) and pitch `1.25` (`0x3fa00000`) -- unconditionally, every
call. This is retail's behaviour and is reproduced.

_2026-10-04 (0002 V4 packet S1):_ the count above is the state-7 fork route's only ("The ordered
chain" steps 5–6). An ordinary kill has no `TASK_SOUND_DIE` and no second `Event_Killed`: its death
sound plays **once**.

### What the port does not have

`TASK_SOUND_DIE` and `TASK_DIE` are ported verbatim; slot 488 was already ported (story 29d,
`FElysiumNpc::DeathSound`). The rest of the chain is **unimplemented**, not diverged, and each body
is tallied through `ElysiumStub` at the commit so `elysium.stubs` names it: `CreateCorpse`
`0x1032c0e0`, `SpawnStaticCorpse` `0x1032be80`, `Event_Dying` `0x10339413`, `ShouldFadeOnDeath`
`0x1027a400`, `SUB_StartFadeOut` `0x102695d0`, `SOUND_CARCASS` `0x101babc0`, and the corpse removal
timers. Because retail's `TASK_DIE` never completes and this runtime has no entity removal, the
commit tears the program down in place of the corpse swap.

_Superseded in part 2026-09-28 (0019 story 8 wave 2, `a5b91c72`, merged as `d1d93773`):_ NPC death
now runs through slot 144. `CBaseCombatCharacter::Event_Killed` `0x1032b9b0` is ported, and so is
`CreateCorpse` `0x1032c0e0` with all three arms. The player self-cast (`+0xa8`, `0x1032c22d`) and
`MiscFlag 0x80000` (`0x1032c288`) arms spawn a static corpse through `SpawnStaticCorpse`
`0x1032be80`; the `0x80000` arm then runs slot 66 `Hide` and `SUB_Remove` at +0.5 s. Otherwise the
arm is `BecomeClientRagdoll`. The tail thinks `SUB_Remove` for a burning corpse, else
`SUB_PVSRemove` `0x102696f0`, both at +10 s, and fires OnDeath crediting `m_hLastEnemy` (`+0x1a94`,
`0x1032c1aa`). `TASK_DIE` commits through `Die` `0x103392c0` (slot 144, then slot 403). The stealth
kill builds `0x10165d90`'s packet, with the player as inflictor and attacker. `SUB_StartFadeOut`
`0x102695d0` stays a counted seam. The walks of the NPC-side bodies follow this section.

**No spec story owns this chain.** 0014 (ragdoll) scopes the rig, the impulse and the handoff, and
defers "the death family" to 0005; 0005 story 4 is closed and what it built was the port's invented
death ladder, which pass D deleted. The corpse chain above needs an owner.

**Unrecovered:** `SUB_FadeOut`'s body (label `0x100152b2`, identified through the `CBaseEntity`
datamap builder `0x1001311a`); the concrete `.wav` behind the vdata "Death" entry, whose trail ends
at the VSnd variant resolver `0x101f4600`; the death-force envelope composed in `0x1032b9b0`.

### `0x10265ad0` `CAI_BaseNPC::Event_Killed`, arm by arm (slot 144, 759 bytes)

_Recovered 2026-09-27, 0019 story 8 pass I (lane L08, Spawn19), integrated as `14697e2b`._ Walked
from the listing with `families-19-29/Spawn19-READING.md` beside it. Port: `ElysiumNpcBaseSpawn.cpp`.
It extends step 2 and step 4 of "The ordered chain" above.

1. `0x10265adf`/`0x10265aee` -- `m_pSchedule` is non-null and equals `0x102cc1f0(0x3a)`. That call
   goes through slot 440, then slot 446; a miss prints `"GetScheduleOfType(): No CASE for Schedule
   Type %d!"` (`0x102cc215`) and answers schedule 1. When the running schedule is `NPC_FREEZE`, the
   body returns at once and writes nothing.
2. `0x10265afb` -- `m_NPCState` (`+0x5cc0`) `== 4` SCRIPT and `m_hCine` (`+0x5d74`) resolves
   (`0x10265b15`). The cine's `m_sequenceStarted` (`+0x5f91`) is set and its spawnflags
   `& 0x2080 != 0x80` (`0x10265b2f`, `0x10265b51`): the whole `CTakeDamageInfo` is copied into
   `m_DeferredDeathInfo` (`+0x1a48..+0x1a92`, two vectors through `0x1003e4b0`) and the body RETURNS
   (`0x10265c04`). The NPC stays alive, and nothing reads the deferred packet back. Otherwise it
   calls `CancelScript` (`0x101a8c30`) on the cine. Then the impulse arm: the ConVar at
   `DAT_106bbaa4` (`IsCommand() ? 0 : m_nValue`, `0x10265c27`/`0x10265c34`) and `m_pPhysicsObject`
   (`+0x36c`, `0x10265c3e`) both set dispatch slot 39 on the physics object, passing slot 263
   `GetGroundSpeedVelocity()`.
3. `0x10265c78` -- slot 511 `StopLoopingSounds`.
4. `0x10265c85..0x10265cb2` -- slot 488 `DeathSound` plays when `m_GrappleRole` (`+0x153c`) is
   `-1`, or `m_GrapplePartner` (`+0x1538`) is `-1`, stale or null. A live grapple partner silences it.
5. `0x10265cc8` -- `0x10265a90(attacker)`: behind the `+0x5bd4` latch, `m_OnDeath` (`+0x5e24`)
   fires with the attacker as activator, then the latch is set.
6. `0x10265cd7` -- `GetFlags() & 0x2000`: `m_pfnTouch = NULL`, then `BecomeDead` (`0x10265a40`:
   `m_iHealth = m_iMaxHealth / 2`, `m_takedamage = 2`, `m_iMaxHealth = 5`, `SetMoveType(6, 0)`).
7. `0x10265ced` -- `CBaseCombatCharacter::Event_Killed(info)` (`0x1032b9b0`), DIRECT.
8. `0x10265cf2..0x10265d06` -- the ideal-state trace `AI_BaseNPC.cpp:0x217`, `m_IdealNPCState = 7`.
9. `0x10265d18` -- a global object's slot 1, called through `DAT_10924a6c`; the answer is discarded.
10. `0x10265d1f` -- `SetCondition(0x4c LIGHT_DAMAGE)`.
11. `0x10265d29..0x10265d3a` -- `m_hLastDamageEnt` (`+0x5b7c`) = the attacker's handle, or `-1`.
12. `0x10265d46` -- `m_bCondTookDamage` (`+0x5b80`) = 1. It does not stay 1: step 15's `SetState(7)`
    dispatches slot 463, and Troika's `0x102ae140` clears `+0x5b80` (`0x102ae272`).
13. `0x10265d4d` -- `0x1028ae60` vacates the squad slot; `0x10265d5a`: when `m_pSquad` is set,
    `RemoveFromSquad` (`0x103158f0`).
14. `0x10265d66`/`0x10265d70` -- slot 552 `ShouldFadeOnDeath` (spawnflag bit 9). True runs
    `SUB_StartFadeOut` (`0x102695d0`); false runs `InsertSound(SOUND_CARCASS 0x20, origin, 0x180,
    30.0, NULL)` (`0x101babc0`).
15. `0x10265d9c..0x10265dba` -- the trace `AI_BaseNPC.cpp:0x241`, `m_IdealNPCState = 7` again (the
    double write is retail's), then `SetState(7)` (`0x1026e340`).

Reads `+0x5c38`, `+0x5cc0`, `+0x5d74`, cine `+0x5f91`/`+0x204`, `+0x36c`, `+0x153c`, `+0x1538`,
`+0x5da4`, info `+0x2c`. Writes `+0x1a48..+0x1a92`, `+0x1ec`, `+0x1b3c/+0x1b40` (absent), `+0x5cc4`,
`+0x5b7c`, `+0x5b80`, and through its callees `+0x5bd4`, `+0x210`, `+0x1fc`, `+0x208`, `+0x5cc0`.

**Unrecovered:** the ConVar at `DAT_106bbaa4` (the SDK names it `npc_vphysics`; this image's name
and default are not read); the object behind `DAT_10924a6c` and its slot 1; `m_DeferredDeathInfo`'s
scalar words have no slot on the port's `FElysiumDmg`.

### `0x102bf340` `CAI_BaseNPCTroika::Event_Killed` (slot 144, 261 bytes)

_Recovered 2026-09-27, 0019 story 8 pass I (lane L08, Spawn19), integrated as `14697e2b`._ Port:
`ElysiumNpcSpawn.cpp`.

1. `0x102bf34d`/`0x102bf354` -- `0x101c2af0(info)` answers `info+0x49`. A zero answer broadcasts the
   death act through `0x102ca2a0` on `DAT_109253f8`, with type 1 (criminal), source this, the slot-217
   origin with Z `+ 16.0` (`_DAT_10451ad0`), level 3, radius `100000.0`, offender NULL, victim this,
   and 1. The store caps at 64 records (`+0xa04 < 0x40`).
2. `0x102bf3a8` -- `CAI_BaseNPC::Event_Killed` (above), DIRECT.
3. `0x102bf3b4` -- `ClearHintNode(5.0)`.
4. `0x102bf3be` -- slot 601 with `this`, unconditional.
5. `0x102bf3cd` -- `0x102b53d0(0, "Leaving interesting place (Event_Killed)")`.
6. `0x102bf3d4`/`0x102bf3df` -- when `IsInDialog()` is true: `0x102c0bb0` (stop the dialogue).
7. `0x102bf3ec..0x102bf3fa` -- when `m_iName` is set: `PyRun_SimpleString(va("MarkAsDead(\"%s\")", name))`.
8. `0x102bf40c..0x102bf438` -- when `m_hClosestPlayer` (`+0x628c`) is live: `0x101828b0(this,
   0x47c34ff3)` on that player. The player's closest-NPC cache is offered this NPC at
   `99999.8984375`.

**Unrecovered:** the port's damage packet carries no `+0x49` byte; the act record's radius, victim
and trailing flag have no port field; the Python import behind `[0x109f36fc]` is outside the corpus
(its identity as `PyRun_SimpleString` is from the verdict).

### The species `Event_Killed` bodies over 64 bytes (slot 144) — `0x1038e8c0`, `0x10395ba0`, `0x1039e900`

_Recovered 2026-09-27, 0019 story 8 pass I (lane L08, Spawn19), integrated as `14697e2b`._ Port:
`ElysiumNpcSpawnSpecies.cpp`. The Ming Xiao death entries are Boss19's bodies (§ "Story 8, family
Boss19" below), bound at the integration.

#### `CNPC_VManBat::Event_Killed` `0x1038e8c0` (159 bytes)

`0x1038f020(1)` releases the slowed victim, `0x1038c170(2)` sets fly mode, `0x1038f660` drops the
carried body and `0x1038fd40` turns the scared minions. When the screech-cone handle `+0x66a4`
resolves, the body runs `UTIL_Remove` on it and writes `-1`. Then it runs the Troika body.

#### `CNPC_VMingXiao::Event_Killed` `0x10395ba0` (156 bytes)

When `m_bPlayedDeathAnim` (`+0x6744`) is clear: `m_lifeState = 1` and `0x10395c70`
`MingXiaoEnterDeath` (the death animation, which sets the latch). The base is NOT reached. When it
is set: a live `m_hParentMingZhao` (`+0x6670`) runs `0x10397a50(parent, this)`. Then comes a gate:
the ConVar `ming_xiao_grub_death` (default `"1"`, read `IsCommand() ? 0 : m_nValue` through the
object's `+4`) must be on, and `0x10398870` must answer head (`m_iTentacleID == -1`). When it passes,
the two sweeps `0x10397e90` and `0x10397f00` run. Then the Troika body.

#### `CNPC_VMingXiaoTentacle::Event_Killed` `0x1039e900` (65 bytes)

When `m_bPlayedDeathAnim` (`+0x6699`) is clear: `m_lifeState = 1` and `0x1039e970`
`MingXiaoTentacleEnterDeath` (the death schedule), then return. When it is set: `0x1039ede0` resolves
the head and `0x103979d0(head, this)` runs. Then the Troika body.

**Unrecovered:** `0x1038c170`, `0x1038f660` and `0x1038fd40` have no port body; `0x103979d0` (the
head's own handling of a dying tentacle) has none either. `0x1039ede0` is 13 bytes and resolves
`m_hMingXiao` (`+0x665c`); the port's two tentacle seams answer null, so a dying tentacle does not
notify its head yet.

### `0x103dfbb0` `CNPC_VZombie::CreateCorpse` (slot 301, 363 bytes)

_Recovered 2026-09-27, 0019 story 8 pass I (lane L08, Spawn19), integrated as `14697e2b`._ Port:
`ElysiumNpcSpawnSpecies.cpp`.

1. `0x103dfbd7..0x103dfc86` -- COPY first. `m_vecDeathForceVector` (`+0x6680`) takes the force.
   The whole `0x13`-dword `CTakeDamageInfo` goes into `m_DeathDamageInfo` (`+0x668c..+0x66d6`)
   through a stack copy. The port types that word as `FElysiumTakeDamageInfo`, not `FElysiumDmg`.
2. `0x103dfc89` -- `SelectWeightedSequence(0x21, -1)`, asked once.
3. `0x103dfc96`/`0x103dfc9b` -- `m_bShouldGib` (`+0x66e0`) is clear AND there is a sequence. When
   `m_bShouldRagdoll` (`+0x6675`) is set, `CBaseCombatCharacter::CreateCorpse` runs with the SAVED
   words (`0x103dfcab`). When it is clear, the body stamps `NPC_VZombie.cpp:0x2eb`, installs `0x162`
   with force (`0x103dfcd5`), then `ThinkSet(LAB_1000f4e8, 0)` and slot 614. No corpse is made.
4. `0x103dfd02`/`0x103dfd09` -- gibbing, or no sequence: slot 394 `CorpseGib`, then
   `UTIL_Remove(this)`.

The zombie's own collapse path (`AddMiscFlag(0x80000)` at `0x103e0210`, then `CreateCorpse`
`0x103e0225`) reaches `0x1032c0e0`'s static-corpse arm, not the ragdoll arm (the L13 review; the
RunTask19 Zombie case asserts it).

**Unrecovered:** nothing in the body. At the L08 integration the port's sequence seam answered -1,
so only the gib arm was reachable. Since wave 2 the pick goes through the sequence bridge
(`SelectWeightedSequenceForActivity`), so a model with an `0x21` sequence reaches arm 3.

## Story 8, family Boss19 — Ming Xiao's tentacles and death, the Sabbat leader's transformation, the Tzimisce runner's slot 330 (2026-09-27)

_Recovered 2026-09-27, 0019 story 8 pass I: lane L12, integrated as `2eca094a`._

Each section is one `rule` row of `families-19-29/Boss19-READING.md` (and Damaged19's
`0x103c43b0`), walked arm by arm off the decompiled C and the listing. Port:
`ElysiumNpcMingXiao.cpp`, `ElysiumNpcMingXiaoTentacle.cpp`, `ElysiumNpcHengeyokai.cpp`,
`ElysiumNpcBossSpecies.cpp` (SabbatLeader), `ElysiumNpcDamagedSpecies.cpp` (TzimisceRunner).
The family's three Troika discipline helpers are in `authored-control.md` § "Story 8, family
Boss19, the discipline helpers".

`0x102ae750` is `SetSchedule(id, force)`: translate (slot 440), resolve (slot 446, fallback 1), refuse
in state 7, and — unless `force` — refuse when slot 158 `IsAlive` is false. The `+0x1b30`/`+0x1b34`
and `+0x1b3c`/`+0x1b40` file/line stamps are recorded in the schedule / mind traces.

### `0x10395c70` `MingXiaoEnterDeath` (79 bytes)

_Recovered 2026-09-27, 0019 story 8 pass I (lane L12)._

Stamp `NPC_VMingXiao.cpp:0x916` (`0x10395c73`); `0x10398870` (`m_iTentacleID != -1`, a proxy) picks
`0x16e`, else `0x16d` (`0x10395c92`); `SetSchedule(id, TRUE)` — forced past `IsAlive` (`0x10395ca0`);
`m_lifeState` = 1 (`0x10395ca5`), `m_bPlayedDeathAnim` (`+0x6744`) = 1 (`0x10395caf`), `m_bInvincible`
(`+0x63d8`) = 1 (`0x10395cb6`). `0x10395ce0` is the latch in front of it.
**Unrecovered:** nothing in the listing. In the port `m_lifeState` is `AnimEventLifeStateWord` (the
lane's `LifeStateRetail` was folded onto it at the StartTask19 integration); the port's `IsAlive`
does not read it yet (it reads the death latches).

### `0x1039e970` `MingXiaoTentacleEnterDeath` (171 bytes)

_Recovered 2026-09-27, 0019 story 8 pass I (lane L12)._

Switch on `m_ePhase` (`+0x6670`, `0x1039e97c`): 2 answers `0x16a` at line `0x590`; 3 asks
`0x10295460(0x1d, false)` — a sequence answers `0x16a` at `0x598`, none `0x16c` at `0x59d`
(`0x1039e9a3`); everything else first runs `0x1039f310` then answers `0x16b` at `0x589`. Install forced
(`0x1039e9fc`); `m_lifeState` = 1, `+0x6699` = 1, `m_bInvincible` = 1 (`0x1039ea01`..`0x1039ea12`).
`0x1039f310` emits `table[RandomInt(0, 0)]` of the ONE-entry table `0x106477cc`
(`character/monster/ming xiao/tentacle_flopping_loop.wav`) on channel 2 at volume 1.0, attenuation 0.8,
pitch 100, with flags `SND_STOP` (`PUSH 0x4`, `0x1039f399`): it STOPS the flop loop that `0x1039f1a0`
starts with flags 0 (integration review; the packet and the lane read it as playing a death sound). **Unrecovered:** the port has no retail-id sequence table (`0x10295460` seam answers -1).

### `0x103aa3b0` `CNPC_VSabbatLeader::StartTransformation` (146 bytes)

_Recovered 2026-09-27, 0019 story 8 pass I (lane L12)._

`m_bActivated` (`+0x66b8`) = 1 FIRST (`0x103aa409`) — slot 461's gate; `AddClassRelationship(1 player,
D_HT, 10)` (`0x103aa410`, `0x10013cf5` -> `0x10332aa0`, which overwrites the class row at any
priority); stamp `NPC_VSabbatLeader.cpp:0x549`; `SetSchedule(0x163, false)` (`0x103aa432`). The body
is the datamap INPUT `StartTransformation` itself (`RET 0x4`, the inputdata argument unread); the port
binds the input to it.

**Unrecovered:** nothing named by the walk.

### `0x10397410` `MingXiaoSpawnTentacle(index)` (981 bytes)

_Recovered 2026-09-27, 0019 story 8 pass I (lane L12)._

1. `+0x6714` = index (`0x10397424`); stamp line `0xcac`; `SetSchedule(0x16c, false)` (`0x1039743e`).
2. Throwable mode 1/2: the throw cleanup `0x10398fd0`; 3/4: only when index == `m_eThrowingTentacle`
   (`0x1039745f`); else nothing.
3. The burst emitter at the limb's bone (`0x10398680` names it, `0x102c42a0`, `0x10397480`); the bone
   position (`0x10398630`: `LookupBone`, bone 0 on a miss); my angles (slot 219); the hull `0x11`
   extents.
4. The search (`0x10397523`..`0x1039762c`): `IsAreaClear(spot, 0x2400b, mins, maxs)` first at the bone,
   then the compass `(+r,+r) (+r,0) (+r,-r) (0,-r) (-r,-r) (-r,0) (-r,+r) (0,+r)` with r from 20.0
   growing by 20.0 per ring; Z never changes; `DevMsg("ERROR:  FAILED TO FIND SPAWN LOC FOR TENTACLE.")`
   on every try past 31; never gives up.
5. `0x10310cf0`: the maker entity named `TentacleGenerator` (`0x10390d00` binds the static reference)
   makes one NPC (`MakeNPC(1)`) and places it at the spot; none: return NULL.
6. Its angles := mine (slot 218); its velocity := (normalize(bone - my origin) * 100 + (RandomFloat(-10,
   10), RandomFloat(-10, 10), RandomFloat(5, 20))) * 0.1 (`0x103976b4`..`0x10397766`); slot 614; its
   `m_iTentacleID` (`+0x6660`) = index; `ShareEnemyWithAlly` (`0x1039777e`); its `m_hMingXiao`
   (`+0x665c`) = me; `m_rhSeveredTentacles[index]` = it; mask `|= 1 << index`; connected count `-= 1`;
   `BodyGroup`; `m_flIdealRange` (`+0x6748`) = `0x103986b0` (`0x103977d4`).

`0x103986b0`: 400 for each of limbs 0/1 and 300 for each of 2/3 summed, divided by TWICE the count;
300 when none. **Corrections to the packet:** the random jitter is on the VELOCITY, not the spawn
spot; the two sub-object writes are `+0x6660` and `+0x665c` (`[0x17]+0x330`/`+0x32c` at a
`0x450`-byte stride). **Unrecovered:** bone positions (seam: origin); the port's `IsAreaClear` takes
the NPC's own extents, not the `0x11` hull.

### `0x10397e90` / `0x10397f00` — the six-handle sweeps (69 bytes each)

_Recovered 2026-09-27, 0019 story 8 pass I (lane L12)._

`m_rhSeveredTentacles[6]` (`+0x66a8`) -> each live tentacle's `0x1039ea60`; `m_rhProxies[6]`
(`+0x668c`) -> each live proxy's `0x10395ce0`. Six hard-coded; nothing cleared.

**Unrecovered:** nothing named by the walk.

### `0x10395750` `MingXiaoApplyTentacleDamage(index, info, weapon)` (696 bytes)

_Recovered 2026-09-27, 0019 story 8 pass I (lane L12)._

1. A weapon whose slot 360 word has `0x18000`: `m_flDamage` = tuning `+0x28` * magnitude
   (`0x10395766`..`0x103957a4`).
2. index <= -1 (`0x103957ae`): `-2` with at most 2 limbs (`0x10395926` / `0x10395932`) stamps line
   `0x8a5`, `SetSchedule(0x16b, false)` and runs the throw cleanup for modes 1..4, damage untouched;
   anything else rescales: `m_flDamage = (int)max((int)magnitude / tuning +0x24, 1.0)`.
3. A connected limb (`0x103957c0`): `m_rflHitPoints[i] -= magnitude`; at or below 0.0 (ordered,
   `0x103957f5`) the limb is lost (`0x10397410`); the damage emitter at the packet position with my
   angles; not invincible: `0x1023e4b0(this, (int)magnitude)`; `m_flDamage` = 0; `0x101c2b10(info, 1)`.
4. A severed limb: the rescale.

The tuning cells are the `Ming_Xiao_Info/General` rows of `Rules.txt` (`0x101e6310`): `+0x24`
"NohitDamageDivide" (default 4.0, `0x101e73d7`), `+0x28` "MeleeDamageScalar" (2.0, `0x101e73ee`);
`0x103952b0`'s `+0x2c` is "MeleeTentacleHitPercent" (int, 20, `0x101e7405`); `0x10397930`'s `+0x4` is
"TentacleHPRegrown" (160.0) and `+0x0` "TentacleHPInitial" (200.0). The weapon's slot 360 word is
read as the weapon record's family (melee -> `0x18000`), as `0x10395650`'s port reads it.
**Unrecovered:** `0x1023e4b0`'s consumer (a 25-slot global queue; recorded); the packet's `+0x1c`
position and `+0x4a` byte (no field). The initial `m_rflHitPoints` writer (`Spawn` `0x103927a0`,
lane L08) landed with Spawn19 and reads TentacleHPInitial (§ "Story 8, family Spawn19").

### `0x103c43b0` `CNPC_VTzimisceRunner::vfunc330` (42 bytes, Damaged19)

_Recovered 2026-09-27, 0019 story 8 pass I (lane L12)._

`shooter = info->+0x94 ? info->+0x94->+0x9c : 0` (`0x103c43b6`..`0x103c43c0`); overwrite the two stack
arguments with `(shooter, 0x79)` and tail-jump slot 320 `PlayerKnockbackReaction` (`0x103c43d4`).
The float is never read; there is no jump table.

**Unrecovered:** nothing named by the walk.

## Story 8, family Spawn19 — slot 103 `Spawn` (base, Troika, the species), slots 617 / 618 (the boss swap) (2026-09-28)

_Recovered 2026-09-27, 0019 story 8 pass I: lane L08, integrated as `14697e2b`._

Each section is one `rule` row of `families-19-29/Spawn19-READING.md`, walked arm by arm off the
listing (`vtmb_asm`), every body over 64 bytes, arms in retail order. Port:
`ElysiumNpcBaseSpawn.cpp`, `ElysiumNpcSpawn.cpp`, `ElysiumNpcSpawnSpecies.cpp`. The family's
slot-144 death bodies and `CNPC_VZombie::CreateCorpse` are walked under § "The death chain, kill to
corpse" above, because that section already owns `0x10265ad0`. `CNPC_VGhoulCroucher::ScriptHide`
`0x1037c1c0` is Script19's row (`authored-control.md` § "Story 8, family Script19, the script
directors"). The lane's duplicate body was folded into `GhoulCroucherScriptHide` at the integration.

### `0x10273200` `CAI_BaseNPC::Spawn` (slot 103, 312 bytes)

_Recovered 2026-09-27, 0019 story 8 pass I (lane L08)._

1. `0x10273272`/`0x1027327a` -- when `g_pGameRules` (`DAT_1070ba0c`) slot 74 (`FAllowNPCs`) is
   false: `UTIL_Remove(this)` and return. The port has one seam for it (`Spawn19GameRulesAllowNpcs`),
   shared with the camera's `0x103692f8`.
2. `0x1027329b` -- this arm runs only when `m_pBaseNPCTroika` (`+0x98`) is NULL. It needs
   `CapabilitiesGet() & 0x200000` (`0x102732ac`) and a non-null `m_spawnEquipment` (`+0x5dec`,
   `0x102732b6`) that is neither the 2-byte `"0"` (`0x102732d1`) nor the 15-byte `"item_w_unarmed"`
   (`0x102732f2`). It then calls `Weapon_Create` and, when a weapon comes back, slot 383
   `Weapon_Equip(weapon, false)`. The world's spawn pass is indexed over the map's own count,
   because a `Spawn` may now create entities (`0x10273306`).
3. `0x10273320` -- slot 223 `CreateVPhysics`.
4. `0x10273328` -- `CBaseCombatCharacter::Spawn` (`0x10323a90`: `AddToTeam` on a non-empty
   `m_sTeamName`, then `CBaseAnimating::Spawn`).

**Unrecovered:** nothing in the body; `CBaseAnimating::Spawn`'s own content is outside the family;
`Weapon_Create` `0x1032e120` is a seam answering no weapon.

### `0x10298d30` `CAI_BaseNPCTroika::Spawn` (slot 103, 1336 bytes)

_Recovered 2026-09-27, 0019 story 8 pass I (lane L08)._

1. `0x10298d3d` -- `m_bInitialized` (`+0x62e9`) = 1.
2. `0x10298d59..0x10298d93` -- a non-empty `m_statTemplate` (`+0x10e4`) goes to the template
   manager (`0x10206c30` on `DAT_1074f028`, which applies it through `0x10206aa0`).
3. `0x10298d9c` -- slot 104 `Precache` (`0x10298ad0`). With no model set, it names
   `models/error/error.mdl`.
4. `0x10298db1`/`0x10298db6` -- `AddToTeam(m_sTeamName)` on a non-empty string.
5. `0x10298dc4`/`0x10298dd4` -- slot 105 `SetModel(STRING(GetModelName()))`.
6. `0x10298dde`/`0x10298de8` -- `m_bloodColor = 0xf7`, `m_flFieldOfView = 0.2`.
7. `0x10298df2`/`0x10298dfe`/`0x10298e07` -- `CapabilitiesAdd` `1`, `0x800000`, `8`.
8. `0x10298e0c..0x10298e30` -- `m_HackedGunPos = (0, 0, 55.0)`, `m_pInterestingPlace = 0`,
   `m_bInterestingPlaceArrived = 0`.
9. `0x10298e8d`/`0x10298efe`/`0x10298f70`/`0x10298fd7` -- four calls on `m_Collision`, each in its
   own scope-trace frame: `SetSolidFlags(0)`, `AddSolidFlags(w | 1)`, `AddSolidFlags(w | 0x40)`,
   `SetSolid(SOLID_BBOX)`.
10. `0x10298fed`/`0x10298ff6`/`0x10298ffc` -- slot 93 `SetMoveType(4, 0)`, `SetHullSizeNormal(false)`,
    `Relink`.
11. `0x10299006` -- `CAI_BaseNPC::Spawn` (above).
12. `0x10299017..0x1029904d` -- `m_vecInitialPosition` = slot 217, `m_qaInitialAngles` = slot 219.
13. `0x10299057`/`0x1029905f` -- slot 420 `NPCInit`, then `ApplyDisciplineSpawnFlags`
    (`0x1033df80`), which ORs `1, 2, 4, 8, 0x10, 0x20` into `+0x0eac` for spawnflags bits 5, 6 and
    12..15.
14. `0x1029907c..0x102990ed` -- each `m_iPL…Level` (`+0x6348..+0x6358`) below 1 gets
    `DevMsg("Warning: Invalid m_iPL… ('pl_…' in world craft)")` and is set to 6.
15. `0x10299120` -- when the occluded sum is positive, chase = 100 and wait / cover / walk / flank
    become the CUMULATIVE `x * 100 / sum` ladder (truncating `IDIV`).
16. `0x10299194`/`0x102991d4` -- when chase is not 100: `Warning("Occluded target reaction
    percentages for %s (%f, %f, %f) do not add up to 100%")`, with the debug name and the initial
    position. If chase is still `<= 0`, the ladder resets to 10 / 40 / 50 / 70 / 100. The port
    reproduces the order: chase is forced to 100 before the check.
17. `0x10299216`/`0x1029922d` -- `0x10298910(m_sInterestingPlaceGroups)`, `0x102989e0(m_sHintGroups)`.
18. `0x10299244`/`0x1029924f` -- `m_iCombatStartActivity = 0x1029f340(m_sCombatStartActivity)`.
19. `0x10299255` -- `0x10207e60(this)`: `PrecacheModel(GetCharTemplate(this)->+0x78, 0)`.
20. `0x1029925e` -- `AddFlag2(4)`.

In the port, `FElysiumNpc::Spawn` is this walk (`TroikaSpawnBody`). So `NPCInit` runs at spawn, as
it does here, and `FElysiumNpc::Activate` no longer calls it or `Senses.ResolveTuning` (retail runs
`InitPerceptionDistances` inside `NPCInit`, `0x1029a6a4`). Step 5's slot-105 `SetModel` builds the
skeletal body and motor (`TroikaSetModel` `0x10298ce0` -> `SetRuntimeModel`, whose body-follow
hook builds them), so the presentation follows retail's model write. The camera's own `SetModel`
(`0x10368bab`) does the same for `CNPC_VCamera::Spawn`. `NPCInit` stamps from the spawn clock, so a
headless world spawns at `-0.1` (`ElysiumStandSpawnClock`).

**Unrecovered:** the template column `+0x78` that `0x10207e60` precaches; the meaning of
`m_fFlags2` bits 4, `0x10` and `0x20`; whether retail's `GetModelPtr` resolves `error.mdl`.

### The species `Spawn` bodies over 64 bytes (slot 103) — `0x10368b70` … `0x103a4510`

_Recovered 2026-09-27, 0019 story 8 pass I (lane L08)._ "The Troika spawn" below is `0x10298d30`.

#### `CNPC_VCamera::Spawn` `0x10368b70` (589 bytes)

`CapabilitiesAdd(0x4000000)`; slot 104; slot 105 with slot 9. `m_bloodColor = 0xf7`,
`m_fEffects = 0`, `m_iHealth = 1`, `m_flFieldOfView = 0.2`, `m_NPCState = 0`, `m_HackedGunPos = 0`,
`m_flNextListenTime = 0`, `m_pInterestingPlace = 0` (`0x10368bb1..0x10368bfb`). Then
`SetSolidFlags(0)`, `SetSolid(SOLID_NONE)`, `SetMoveType(4, 0)`, `SetHullSizeNormal(false)`,
`Relink`, slot 420, `InitPerceptionDistances` and `ApplyDisciplineSpawnFlags`.
`m_hEyeLookTarget = -1`, `m_RelativeEyeTarget = 0`. The five police-level repairs follow (identical
strings), then slot 66 `Hide` and `AddFlag2(0x10)`. There is no Troika spawn and no base spawn.

#### `CPayphone::Spawn` `0x101aa9c0` (357 bytes)

The Troika spawn, then `m_flNextThink = curtime + 0.1` (`_DAT_104491b4`). Then `SetMoveType(0, 0)`,
`SetSolid(SOLID_BBOX)`, `m_iHealth = 80000`, `m_takedamage = 0`, `AddFlag(0x10000)`,
`m_nSequence = 0`, `ResetSequenceInfo`, `m_flCycle = 0`, `AddSolidFlags(w | FSOLID_NOT_SOLID)`,
`RemoveFlag2(4)` and `AddFlag2(0x10)`.

#### `CNPC_VMingXiao::Spawn` `0x103927a0` (436 bytes)

Caps `0x4000000` and `0x200000`, then the Troika spawn and `AddMiscFlag(0x80000)`. FOV `-0.5`.
`m_hParentMingZhao = -1`, `m_iTentacleID = -1`, `m_bHasTransformed = 0`,
`m_iSeveredTentacleMask = 0`, `m_flProxyReadyTimer = 0`, and
`m_flSpitAttackTimer = 0x10397f70() + curtime`.

Six slots are seeded (`0x1039281c..0x10392850`): severed `-1`, registered 0, proxy `-1`, attack
timer 0, hit points and regrow timer `FLT_MAX`. The hit points come from the tuning record
`0x10739d08` (`Select19MingXiaoTuningField`): `+0x0` TentacleHPInitial is written into each limb's
`m_rflHitPoints` (`0x10392830 MOV ECX,0x10739d08`, `0x1039283f FSTP [EDI+0x50]`).

Then `m_iConnectedTentacleCount = 6`, `0x10398800` (the bodygroup), `m_hMeleeWeapon` from
`GetBestMeleeWeapon`, `m_hRangedWeapon` from slot 309, and slot 388 `Weapon_Switch(ranged, 0)`.
`SetAbsoluteAttackExtents(120, 120, 92)`, `m_bNeverMeleeOpponent = 1`. Throwable mode 0, coordinate
tentacle 0, `m_bPlayedDeathAnim = 0`. `m_flIdealRange = 0x103986b0()`, which is Boss19's
`MingXiaoIdealRangeFromLimbs`. Charge-ready 0, blocked-by-friend 0.

#### `CNPC_VMingXiaoTentacle::Spawn` `0x1039c380` (134 bytes)

Caps `0x4000000` and `0x200000`; the Troika spawn; `AddMiscFlag(0x80000)`. `m_ePhase = 0`,
`m_bInvincible = 1` (`+0x63d8`), the four timers 0, `m_bIgnoreCollision = 1` (`+0x6688`),
`m_bHitGroundSound = 0` (`+0x6698`), `m_bPlayedDeathAnim = 0` (`+0x6699`),
`m_flIgnoreCollisionTimer = curtime + 5.0` (`+0x6684`). Then `RemoveFlag2(4)`.

#### `CNPC_VTzimisce::Spawn` `0x103b9060` (132 bytes)

The Troika spawn FIRST. FOV `-0.5`. The head-forward basis `+0x1074` is rotated
`(x, y, z) -> (-y, x, z)`. Then `SetAbsoluteAttackExtents(60, 60, 100)`, `RemoveFlag2(4)` and
`m_bInMelee = 1`.

#### `CNPC_VTzimisceHeadClaw::Spawn` `0x103c1b90` (180 bytes) and `CNPC_VTzimisceRunner::Spawn` `0x103c3b30` (193 bytes)

BEFORE the Troika spawn, both set `m_statTemplate` (`TzimisceCreation2` / `TzimisceCreation3`),
`m_sPlayerReaction = "D_HT 10"` and occluded 0 / 0 / 0 / 0 / 100. After it, the head claw adds caps
`0x4000000` and `0x200000`; the runner adds only `0x4000000`. Both set
`m_bfNPCFrenziedFlags |= 0x80`. The runner also clears `+0x6672`, sets
`m_bAllowsInterpenetratingAttacks` and writes `m_hPotentialEnemy = -1`. Extents `(45, 45, 100)` /
`(50, 50, 82)`, then `RemoveFlag2(4)`.

#### `CNPC_VWerewolf::Spawn` `0x103caa30` (217 bytes)

`m_statTemplate = "Werewolf"` BEFORE the Troika spawn. After it: `AddFlag(0x2000)`,
`m_bIsBCCTargetable = 1`, `m_bPlayFrustration` (`+0x66a9`) `= 0`, `m_pTeleportHintSearchStart`
(`+0x66ac`) `= 0`, `m_DoorState = 0`. Then the `m_Collision` partition update (`0x100ddd90`),
`RemoveFlag2(4)` and `AddFlag2(0x10)`.

#### `CNPC_VDog::Spawn` `0x10374000` (90 bytes)

`RandomFloat(0, 1)` first, then `+0x6688 = 0` and `+0x6674 = curtime + that`. Caps `0x4000000`,
`0x200000` and `0x8000`, then `CNPC_VAnimal::Spawn`.

#### `CNPC_VZombie::Spawn` `0x103df170` (183 bytes)

Caps `0x4000000`, `0x200000` and `0x8000`. `m_altEquipment = m_spawnEquipment = NULL`,
`m_flNextFleeSoundTime` (`+0x641c`) `= 0`, and the five police levels (`+0x6348..+0x6358`) `999999`.
Then the encoded witnessed level (`0x1042fde0(0)`), whose two tag bytes are read from UNINITIALISED
stack (a retail defect; the port writes 0). `m_iPLSupernaturalLevelWitnessed` (`+0x6368`) `= 0`.
`CNPC_VAnimal::Spawn` runs LAST.

#### `CNPC_VAndreiBlood::Spawn` `0x1035cc20` (176 bytes)

`CapabilitiesAdd(0x200000)`, then `CNPC_VVampire::Spawn`. The runner and kill counts are 0; the
activated, dead and trigger-unhide flags are 0; `m_fTeleportWaitStartTime = curtime`;
`m_bForceTeleport = 1`; the hit counter is 0. Then `0x1035e950` rolls `m_iHitMax`.

#### `CNPC_VAsianVampire::Spawn` `0x10360c50`, `CNPC_VChangBros::Spawn` `0x1036afc0`, `CNPC_VSheriffMan::Spawn` `0x103ae630` (109 bytes each)

One `CapabilitiesAdd` (`0x201000`, `0x209000`, `0x201000`), then `CNPC_VVampire::Spawn`, in a
scope-trace frame. `CNPC_VVampire::Spawn` `0x103c4ef0` sets the player's class to hate through
`AddClassRelationship` (`0x103c4ef9` -> `0x10332aa0`, overwriting at any priority).

#### `CNPC_VBach::Spawn` `0x10363850` (184 bytes)

`CapabilitiesAdd(0x200000)`, `CapabilitiesRemove(1)`, then `CNPC_VVampire::Spawn`. Then twenty-two
seeds, all 0 except `m_bBachInStartingPosition` (`+0x66a1`) = 1. The zeroed seeds include
`m_flWaitFinished` (`+0x5db4`) and the three words of `m_vecLastOccludeOrigin` (SOURCE units).

#### `CNPC_VGhoulCroucher::Spawn` `0x1037b040` (267 bytes)

Caps `0x4000000`, `0x200000` and `0x8000`, and `m_altEquipment = m_spawnEquipment = NULL`. The
template is picked FIRST by `m_bSpawnBurning` (`+0x6665`, `MalkMansionStalkerBurning`), then
`m_bSpawnDisturbed` (`+0x6664`, `MalkMansionStalker`), else `MalkMansionCroucher`. Then
`CNPC_VHumanCombatant::Spawn`. With disturbed set again, `+0x6666 = +0x6667 = 1` and the stalker
model; otherwise both are 0 and the female model is used. Burning beats disturbed for the template,
but disturbed alone picks the model (retail, reproduced). Then `m_nUnawareType = RandomInt(0, 3)`
and the five police levels `999999`.

#### `CNPC_VHengeyokai::Spawn` `0x1037fa00` (70 bytes)

`CNPC_VVampire::Spawn`; extents `(50, 50, 100)`; `RemoveFlag2(4)`; `AddFlag2(0x20)`.

#### `CNPC_VSabbatLeader::Spawn` `0x103a6c80` (129 bytes)

`CapabilitiesAdd(0x209000)`, `m_statTemplate = "VampireSabbatLeader"`, then `CNPC_VVampire::Spawn`.

#### `CNPC_VPlayerController::Spawn` `0x103a4510` (69 bytes, landed before this story)

`CNPC_VVampire::Spawn`; `AddClassRelationship(1, 3, 0)` (`0x103a4519`); `m_bForceFrequentThink = 1`;
`SetName("playercontroller")`; `AddFlag2(0x10)`. The player controller's own section is
§ "The player controller — `CNPC_VPlayerController`, `CNPC_VFrenzyShadow`, `CNPC_VWolfMorph`".

**Unrecovered:** `GetBestMeleeWeapon`'s NPC pick (`0x10336f20`, a seam answering null); the names of
the Dog's `+0x6688` / `+0x6674` and the Scurrying's `+0x668c`; the Bach byte `+0x66a4` (bound as
`bBachShotLatch`, its retail name unread).

### `0x103c75f0` `CNPC_VVampireBoss::InputTransformModel` (slot 617, 135 bytes)

_Recovered 2026-09-27, 0019 story 8 pass I (lane L08)._ The `TransformModel` input is bound to slot 617.

1. `0x103c763f` -- `m_pszMonsterClassname` (`+0x6694`) = `"npc_VVampireBoss"`, UNCONDITIONALLY
   (a retail defect: the classname is set even with no morph model).
2. `0x103c7652`/`0x103c7659` -- when `m_MorphModelName` (`+0x667c`, null read as `""`) is non-empty:
   `m_pMonsterModelName` (`+0x6680`) = it, and `0x102ae750(0x159, false)`.

**Unrecovered:** nothing.

### `0x103c60a0` `CNPC_VVampireBoss::TransformationStart` (slot 618, 632 bytes)

_Recovered 2026-09-27, 0019 story 8 pass I (lane L08)._

1. `0x103c60fa`/`0x103c610a` -- the entity factory `0x10136580` runs on `m_pszMonsterClassname`, and
   the body takes the new entity's `+0x98`. A miss leaves NULL, and retail faults at `0x103c6110`;
   the port counts the miss and removes the orphan (a crash guard).
2. `0x103c611b` -- new `m_spawnflags |= 4`. Then `0x103c612e` slot 62 with our slot 217,
   `0x103c6143` slot 64 with our slot 221, `0x103c614e` slot 202 (owner = us) and `0x103c6157`
   `CopyAnimationDataFrom(us)`. `0x103c615e`: new `m_flSeekDistBase` (`+0x63b4`) = 4096.0.
3. `0x103c6171` -- slot 105 `SetModel(m_pMonsterModelName)`; `0x103c6178` `DispatchSpawn`.
4. `0x103c618d`/`0x103c6193` -- new `m_fEffects |= 0x60`, slot 614; `0x103c61a4` slot 105 AGAIN (a
   retail defect: `SetModel` runs on both sides of `DispatchSpawn`); `0x103c61bb`
   `m_fEffects &= ~0x60`, alpha 1, `m_nRenderFX = 0x1f`, `m_nRenderMode = 2`.
5. `0x103c61e0` -- new `m_hProteanTransformOther` (`+0x155c`) = us; `0x103c61fe`/`0x103c6204`
   `m_flProteanTransformStartTime` (`+0x1560`) = curtime on BOTH.
6. `0x103c620a..0x103c6234` -- new `m_NPCState = m_IdealNPCState = 5`, `m_bIsBCCTargetable`
   (`+0x1480`) = 1, traces `npc_VVampireBoss.cpp:0x205` / `0x206`; `0x103c623e`
   `0x102ae750(0x158, false)` ON THE NEW BODY.
7. `0x103c6243..0x103c62c4` -- the five police levels and the five occluded percents copied across.
8. `0x103c62ca`/`0x103c62e1` -- `__RTDynamicCast(new, CNPC_VVampireBoss)` (type descriptor
   `0x1062a654`, the same cast `WaitForTransformation 0x103c63c0` makes). On a hit, new `+0x66b0`
   (`m_hTransformPartner`) = us. A non-boss body casts to null and the write is skipped: the gate
   is retail, not a crash guard.
9. `0x103c62e7`/`0x103c62f1`/`0x103c6304` -- OUR `m_nRenderFX = 0x1e`, `m_nRenderMode = 2`, and
   `m_hProteanTransformOther` = the new body, LAST.

**Unrecovered:** nothing in the body. The port's `CopyAnimationDataFrom` carries only the words the
port has (model, skin, sequence, cycle, anim time, effects). The protean start time `+0x1560` is a
`CBaseCombatCharacter` word (`CBasePlayer::PostThink 0x1016be10` reads it) held on the port's
`FElysiumNpc`.

### `0x103ab310` `CNPC_VSabbatLeader::TransformationStart` (slot 618, 183 bytes)

_Recovered 2026-09-27, 0019 story 8 pass I (lane L08)._

`0x103ab36a` slot 105 `SetModel(".../andrei/andrei_no_mouth.mdl")`; `0x103ab377`
`SetIdealActivity(0x113e)`; `0x103ab38b` `m_fEffects |= 0x10`. Then `m_nRenderFX = 0`,
`m_nRenderMode = 0`, `m_eHull = 0`, `m_eDefaultHull = 0` (`0x103ab391..0x103ab3a3`);
`0x103ab3a9` `SetHullSizeNormal(true)`; `0x103ab3af` `Relink`; `0x103ab3b9`
`CNPC_VVampireBoss::TransformationStart`, DIRECT. The boss body's render writes land last.

**Unrecovered:** nothing.


### V4c C2 recovery and serial integration (2026-10-04)

No build, editor, arena or suite was run. All queries finished within 10 seconds. Mandatory
listing reads 0x102df320, 0x103167f0 and 0x103169a0, including owner storage/call arguments,
are recorded in senses.md.

**Picks:** 0x1008dc40 -> 0x10427fc0: miss -1, singleton/no draw, positive raw total
RandomInt(0,total-1), weight<=remainder subtraction in table order, nonpositive total uniform.
0x1008dd30 -> 0x104280f0: strict maximum, first tie, no draw. K4's resolved owner ruling:
all animation picks share advancing ElysiumRng::Stream(EElysiumRngStream::NpcSchedule), retail
*0x1070b244 slot2. No per-entity/hash/Reaction Variant seeds. SequenceForActivity and slot331
gather the same bare matching rows, each with a kernel number; no selected-clip cache.
Baked rows have owner/flags/raw index, but lack include-group boundaries and the last-local
flags&0x80 sentinel required by Studio_GetSequencesForActivity 0x10427df0. Pipeline owner must
supply provenance; this lane does not invent shadowing from labels or weights.

Site inventory: SequenceForActivity/SelectWeightedSequence 0x1008dc40/0x10272130;
PlayActivity 0x10272130; StartWalkingAnimation motor 0x10264680; StartTaskSlot442 0x102827f0;
RunAnimation 0x1026c540 (loop weighted, otherwise heaviest), commit 0x10260a50 keeps cycle;
ChangeStance/slot611 0x102c1230/0x102c12a0; feed attacker then victim 0x1032a2cc/0x1032a2de;
landed melee 0x10347180/0x10348100 unchanged; visual TryActivity/PickWeighted 0x1008dc40;
reaction 0x10099690/0x101606e0; weapon player 0x101644f0/0x1015fbb0, NPC 0x10272130,
weapon model 0x10253390/0x1024efa0; locomotion player heaviest 0x101644f0, cast 0x10272130;
prop Spawn/Think 0x1018df70/0x10190850. AnimationDriver's Variant cache is presentation reuse,
not meaningful as a weighted seed; it must never replace a retail selection transaction.
Kernel picks bypass it, including repeated same-activity commits. Exact-label replay/cache
pass-throughs spend no draw. The finished nonloop idle still resets at cycle1 every think,
redispatching its whole event table (S12's retail bug).

**Disposition:** 0x102c0f70 always writes tuning; miss -> Neutral/1 and old=-1. Index change
takes slot611 or 0x100ed150's named transition, _1_..._1, then new idle. Valid choice always
writes ideal0xf1/ideal sequence; DisableAI gates only sequence/cycle0/activity0xf1/animTime/reset.
No feed/body-owner gate. 0x100ecd30 aliases both blink outputs at +0x64d8 (max wins);
+0x6584/8 are eye-fidget intervals. +0x5b94/+0xe3c/+0x10b4/+0x64d0/+0x10b8 use existing
named gaze/expression bridges; three missing timing words were added. The index is represented
by name/level with bHasDispositionIndex for initial -1. 0x102c1230 redraws 0..2 until different,
writes stance/time only. Slot611 preserves alternate-chance then time-gated change-chance
then that retry loop; misses keep the current sequence.

**Corpse:** ordinary 0x1032c0e0 fires OnDeath before corpse creation and owns every replacement
think: hidden static source +0.5 SUB_Remove; mortal +10 SUB_PVSRemove; Kindred/burning +10
SUB_Remove. Pedestrian 0x103a38c0 snapshots OBB before the entire base, then clears think and
sets SOLID_NONE. Explicit clear stays cleared. A missing restored corpse function name never
infers a clock or resumes NPCThink; V6 owns saving/restoring actual function/render/collision
words. The DeathRestore test pinned synthetic HoldBodyFinalPose/PVS inference and was deleted.
Legacy RestoreDeathBodyState/CompleteDeathHandoff presentation remains outside this lane's
function scope; its owner must reconcile it before claiming restored-corpse parity.

0x10090180 checks rig capability first: no rig only zeroes bounds/returns false. Listing read
refines the brief: every rigged call picks ACT_DIERAGDOLL, but only bone-1 commits at 0x1009021a.
Real-bone calls retain pose/cycle while still advancing the stream when multiple seed rows
exist. Skipping that lookup would be a new stream divergence and was not adopted.
HasClientRagdollRig is the named false model-slot18 hook until V4d supplies capability;
CorpseForceBone is the named absent hitbox/Spine2 lookup. Ordinary missing bone never becomes
bone-1. Rigged ordinary handoff requires those inputs and the force/bone presentation adapter.
Seed reachers: deferred CineCleanup 0x1027d170 health<1, sibling 0x1027d0a0, Werewolf
0x103d0820, zombie collapse 0x103dfbb0. Neither death arena record reaches it. 0x102b5bb0 is dead.

**Fade/makers:** ordinary 0x1034b7b0 assigns 4/0x204; infinite-child Spawn0x1034afe0 forces fade.
Fleshpile 0x1034c2d0 ORs 4/0x204 and Spawn0x1034c020 has that implication. Zombie0x1034d140
calls ordinary maker and Spawn0x1034cc60 forces fade. Landed maker bodies match; no patch owed.
The existing Event_Killed 0x10265d66/0x10265d72 call and TASK_DIE share StartFadeOut0x102695d0:
mode0 -> mode2/alpha255, other modes keep alpha; solid4, angular zero, relink, think+10,
SUB_FadeOut thunk0x100152b2. 0x10269960 subtracts7/+0.1 while alpha>7; else zero/+0.2/
SUB_Remove. Later fade wins over Kindred removal and pedestrian clear. Alpha255: 36 decrements,
zero near death+13.6, removal near+13.8; visibility irrelevant. Alpha presentation and spatial
relink remain named visual/partition seams, not missing kernel clocks.

**Serial patches owed (not applied in concurrent files):**

1. ElysiumNpcBaseHelpers2.cpp::SelectHeaviestSequence, keep signature and replace body:
~~~cpp
(void)CurrentSequence; // 0x1008dd30
const FElysiumNpc* const HeaviestNpc = AsNpc();
return HeaviestNpc != nullptr
    ? const_cast<FElysiumNpc*>(HeaviestNpc)->SelectHeaviestSequence(Activity) : INDEX_NONE;
~~~
Include Substrate/ElysiumNpc.h if absent. Overlay's existing NPC forwarder stays exactly
return const_cast<FElysiumNpc*>(Npc)->SequenceForActivity(Activity);, non-NPC model-less -1.

2. ElysiumNpcTroikaHelpers.cpp::Slot611, replace body:
~~~cpp
return SelectDispositionStance(); // 0x102c12a0
~~~
Keep existing hand:FElysiumNpc::Slot611 verdict. No body is added to generated Slots.cpp.
The old ElysiumStance::ChangeStance single-draw shortcut is no longer a live NPC caller.

3. Visual/ElysiumAnimSubsystem.cpp::ActivitySequences: remove intent/selection/Resolve
temporaries (gather neither draws nor falls back). Keep Out.Reset and existing descriptor joins,
with these exact catalogue/guard/loop lines:
~~~cpp
const FElysiumAnimationCatalog Catalog = BuildCatalog(Request.Stem);
if (Catalog.Clips == nullptr || Request.Activity.IsEmpty()) { return; }
for (const FElysiumClipRef& Ref : Catalog.Clips->ByActivity(Request.Activity))
~~~
This is required before claiming one draw per transaction. Delete obsolete
Guard->SequenceResolveCache.Reset(); and F.Guard->SequenceResolveCache.Reset(); statements
in ElysiumNpcKernelOverlayTests.cpp::FElysiumNpcKernelOverlayAddGestureTest::RunTest,
ElysiumNpcKernelMoveAndShootTests.cpp::FMoveShootFixture, and
FElysiumNpcKernelMoveAndShootArmTest::RunTest (four sites).
Locate functions by name. No selected-clip cache remains in the kernel.

4. C1 ElysiumWeaponClasses.cpp::BuildActivityClipRequest: replace the Handle.Index assignment:
~~~cpp
Out.Variant = 0; // 0x101644f0/0x10272130: actual picker owns shared draw
~~~
Preserve each weighted/heaviest caller. Weapon-model looping re-pick at 0x1024efa0 uses
ElysiumAnimationPick::Weighted, never speculative Variant followed by a second weighted draw.

5. Prop interface beside AnimatedPropRestClip in Public/ElysiumWorldServices.h:
~~~cpp
virtual FString PickAnimatedPropRestClip(const FString& Stem) const { return FString(); }
~~~
Public/ElysiumMapActor.h:
~~~cpp
virtual FString PickAnimatedPropRestClip(const FString& Stem) const override;
~~~
Private/Visual/ElysiumEntityBodies.h:
~~~cpp
FString PickAnimatedPropRestClip(const FString& Stem) const;
~~~
Map/ElysiumMapActorEmbodiment.cpp:
~~~cpp
FString AElysiumMapActor::PickAnimatedPropRestClip(const FString& Stem) const
{ return Bodies ? Bodies->PickAnimatedPropRestClip(Stem) : FString(); }
~~~
Visual/ElysiumEntityBodiesProps.cpp includes Visual/ElysiumAnimationPick.h and adds:
~~~cpp
FString UElysiumEntityBodies::PickAnimatedPropRestClip(const FString& Stem) const
{
    const auto Prepared = ElysiumPreparedProps::ForOwner(GetOwner());
    FString Failure;
    const auto* ModelRow = Prepared
        ? Prepared->Model(ElysiumPreparedProps::ModelId(Stem), Failure) : nullptr;
    if (ModelRow == nullptr) { return FString(); }
    TArray<ElysiumAnimationPick::FCandidate> Candidates;
    for (int32 ClipIndex : ModelRow->RestCandidates)
    {
        if (!ModelRow->Clips.IsValidIndex(ClipIndex)) { return FString(); }
        Candidates.Add({ClipIndex, ModelRow->Clips[ClipIndex].Weight});
    }
    const int32 Picked = ElysiumAnimationPick::Weighted(Candidates);
    return ModelRow->Clips.IsValidIndex(Picked) ? ModelRow->Clips[Picked].Label : FString();
}
~~~
Raw weights stay raw. SelectRest remains only for BuildBody capability and
BuildAnimatedPropVisualWithStaticStem's initial draw-free preview. No catalogue re-bake.

6. ElysiumNpcBaseSelect.cpp::SelectBecomeClientRagdoll: keep counter and replace false return:
~~~cpp
FElysiumNpc* const RagdollNpc = AsNpc();
return RagdollNpc != nullptr
    && RagdollNpc->BecomeClientRagdoll(FVector::ZeroVector, INDEX_NONE, false); // 0x1028a8f7
~~~
Include Substrate/ElysiumNpc.h. This explicit state7 fork owns bone-1; ordinary kills do not.

7. research/tooling/ghidra/driver/kernel_verdicts.tsv: replace stale pedestrian row:
~~~tsv
103a38c0	rule	10-18	hand:FElysiumNpcPedestrian::CreateCorpse	[0002/V4c obs=timing: snapshot before complete base corpse, then NULL think/SOLID_NONE] 0x103a38c0 wrapper dispatches PedestrianCreateCorpse(force,info).
~~~
Regenerate dispatch/bindings serially. Keep base slot301's existing signature; no generated file
was edited or hand body duplicated. Refresh's abbreviated present verdict likewise becomes
rule for FElysiumNpcEnemyMemory::Refresh, with senses.md's owner/cursor evidence.
C3 owns team_name binding/world registry hooks. After ApplyEntityRecord:
~~~cpp
if (!RestoredNpc->TeamName.IsEmpty()) { RestoredNpc->AddToTeam(RestoredNpc->TeamName); }
~~~
Re-register names; no numeric symbol persists across levels.

Arm tests written: weighted/heaviest/miss/singleton/zero-total/interleaving; RunAnimation gates/
kept cycle; disposition ideal-vs-commit/tuning/stance retry; bone-1/real-bone/no-rig/miss;
pedestrian entire base/clear/solid; late fade/full clocks; BestEnemy NPC/player corpse rejection;
memory owner veto/free knowledge/LKP/cursor/notify/selected store; released-base disposition
maintenance; player alive gate/slot312 order. Removed Death's positive freeze/collision/hold-pose
stand-ins and deleted NpcCombat.DeathRestore. Compilation/runtime acceptance are integrator work.


Additional serial wires found while checking the direct collector:

8. ElysiumNpcTroikaHelpers.cpp::EntityWord0x200 is still a false seam, contrary to the brief's
claim that slot56's entire live gate landed. Replace its body with:
~~~cpp
return Entity.LifeState != 0; // 0x102b5120 reads the DWORD at +0x200, LIFE_ALIVE=0
~~~
This is required for MemoryFidelity's real Troika notification arm; do not fabricate the
Slot56 effect in Refresh. Refresh unlinks first; separate ClearMemory0x102dfaa0 was also read
and notifies BEFORE unlink, forwarding the caller's tag. A null tag is its named absent input.

9. ElysiumNpcBaseMotor.cpp::RetailCollisionExtents, inside the Npc != nullptr arm BEFORE the
hull-table return, add:
~~~cpp
if (Npc->SetSizeCalls > 0)
{
    OutMinsUnits = Npc->LastSetSizeMinsUnits;
    OutMaxsUnits = Npc->LastSetSizeMaxsUnits;
    return true; // 0x101cf390: latest UTIL_SetSize, including corpse zero bounds
}
~~~
The current reader ignores all UTIL_SetSize writes and synthesizes the live hull. C1 owns this
file concurrently; this is an integrator wire, not a second writer.

10. Bare gather must not lose the already-recovered name-keyed class/weapon translation.
ElysiumNpcBaseAnim.cpp::TranslateActivityNumber now calls a PURE adapter before collecting;
the existing native aim-gait early translation remains first. Add these exact declarations:

Public/ElysiumWorldServices.h (beside ResolveNpcActivityClip):
~~~cpp
virtual bool TranslateNpcActivityRequest(const FElysiumActivityClipRequest& Request,
    FString& OutTranslated, FString& OutFirstWeapon) { return false; }
~~~
Public/ElysiumMapActor.h:
~~~cpp
virtual bool TranslateNpcActivityRequest(const FElysiumActivityClipRequest& Request,
    FString& OutTranslated, FString& OutFirstWeapon) override;
~~~
Private/Visual/ElysiumEntityBodies.h:
~~~cpp
bool TranslateNpcActivityRequest(const FElysiumActivityClipRequest& Request,
    FString& OutTranslated, FString& OutFirstWeapon);
~~~
Visual/ElysiumAnimSubsystem.h (public):
~~~cpp
bool TranslateActivityRequest(const FElysiumActivityClipRequest& Request,
    FString& OutTranslated, FString& OutFirstWeapon);
~~~
Map/ElysiumMapActorEmbodiment.cpp:
~~~cpp
bool AElysiumMapActor::TranslateNpcActivityRequest(const FElysiumActivityClipRequest& Request,
    FString& OutTranslated, FString& OutFirstWeapon)
{
    return Bodies && Bodies->TranslateNpcActivityRequest(Request, OutTranslated, OutFirstWeapon);
}
~~~
Visual/ElysiumEntityBodies.cpp:
~~~cpp
bool UElysiumEntityBodies::TranslateNpcActivityRequest(const FElysiumActivityClipRequest& Request,
    FString& OutTranslated, FString& OutFirstWeapon)
{
    UElysiumAnimSubsystem* const TranslationAnims = GetAnims();
    return TranslationAnims
        && TranslationAnims->TranslateActivityRequest(Request, OutTranslated, OutFirstWeapon);
}
~~~
Visual/ElysiumAnimSubsystem.cpp:
~~~cpp
bool UElysiumAnimSubsystem::TranslateActivityRequest(const FElysiumActivityClipRequest& Request,
    FString& OutTranslated, FString& OutFirstWeapon)
{
    const FElysiumAnimationCatalog Catalog = BuildCatalog(Request.Stem);
    if (Catalog.Clips == nullptr) { return false; }
    const auto Translation = ElysiumAnimResolve::TranslateActivity(
        ElysiumAnimResolve::ActivityIntentFor(Request), Catalog);
    OutTranslated = Translation.Resolved;
    OutFirstWeapon = Translation.FirstWeaponActivity;
    return true; // 0x10271ff0, no Resolve/weighted pick
}
~~~
The false default is the named absent table hook, not a successful translation. Restored
player/cast/weapon presentation caches do not suppress kernel draws. The raw collection adapter
in item3 still looks up Request.Activity directly, with no translation or selection transaction.


Pedestrian's NEW slot301 forwarding definition is owed to the matching hand-body file,
Private/Substrate/ElysiumNpcPedestrianSlotBodies.cpp, not a generated Slots.cpp:
~~~cpp
#include "Substrate/ElysiumNpcPedestrian.h"

void FElysiumNpcPedestrian::CreateCorpse(const FVector& Force, void* InInfo)
{
    PedestrianCreateCorpse(Force, InInfo); // 0x103a38c0 slot301
}
~~~
The declaration and helper are C2's; this one definition and item7's hand verdict/regeneration
are integrator work, to keep the file boundary and avoid defining a slot body twice.

C2 edited only these brief-listed paths (prefix Private/Substrate unless stated):
ElysiumNpcAnim.cpp, ElysiumNpc.h, ElysiumNpcBaseAnim.cpp, ElysiumNpcBaseStartTask.cpp,
ElysiumNpc.cpp, ElysiumNpcBaseRunTask.cpp/.inl, ElysiumNpcBaseSpawn.cpp/.inl,
ElysiumCombatCharacter.cpp, ElysiumNpcPedestrian.h/.cpp, ElysiumPlayerEntity.cpp,
ElysiumFeed.cpp, ElysiumNpcBaseSenses10.cpp, ElysiumNpcBaseSenses.cpp,
ElysiumNpcEnemyMemory.h/.cpp, ElysiumNpcBaseConditions2.cpp, ElysiumProp.cpp;
Private/Visual/ElysiumAnimationResolve.cpp and new ElysiumAnimationPick.h/.cpp;
Private/Player/ElysiumAnimationIntent.cpp; Private/Tests/ElysiumPlayerPostThinkTests.cpp,
ElysiumNpcKernelSpeciesMisc10Tests.cpp, ElysiumNpcKernelAnimTests.cpp,
ElysiumNpcCombatTests.cpp, ElysiumNpcEnemyTests.cpp, ElysiumNpcKernelSenses10Tests.cpp;
docs/vtmb/npc-ai/lifecycle.md, senses.md, docs/vtmb/feeding.md.

C1 may read SequenceRows[Sequence].RawIndex directly in its interval-movement/swing descriptor
mapping; weighted gathers now retain every candidate's raw number. No second draw is needed.
The exact body-row expression is
SequenceNumber == 0 ? 0 : (Troika->SequenceRows.IsValidIndex(SequenceNumber)
    ? Troika->SequenceRows[SequenceNumber].RawIndex : INDEX_NONE).


C3's new TeamName member exposes an existing C4458 shadow in the other spawn body.
ElysiumNpcSpawn.cpp::TroikaSpawnBody, at its already-ordered join after Precache, replace
the three TeamName local references with these exact lines (no order change):
~~~cpp
const FString SpawnTeamName = Spawn19TeamName();
if (!SpawnTeamName.IsEmpty()) // 0x10298daa
{
    Spawn19AddToTeam(SpawnTeamName); // 0x10298db6
}
~~~
The owning constructor should call EnemyMemory.BindOwner(*this); BindOwner retains that identity
and attaches MemoryWorld when the same owner later reaches GetEnemies after world registration.

Exact Refresh verdict row, replacing its abbreviated present row:
~~~tsv
102df320	rule	0-4	FElysiumNpcEnemyMemory::Refresh	[0002/V4c obs=timing: actual owner/squad permission, unlink before notify, retained tracked-only free knowledge and successor-next predecessor splice] 0x102df320/0x102df50e..0x102df518; arguments and listing evidence in senses.md.
~~~


Restore join with the actual existing variable: in
ElysiumEntityWorldPersistence.cpp::FElysiumEntityWorld::ApplyEntityRecord, after
E->SetSavedNextThink(S.SavedNextThink) and before NotifyVisualChanged(*E), add:
~~~cpp
if (FElysiumNpc* const RestoredNpc = E->AsNpc())
{
    if (!RestoredNpc->TeamName.IsEmpty()) { RestoredNpc->AddToTeam(RestoredNpc->TeamName); }
}
~~~
This is C3/integrator's world restore hook; C2's own Spawn/Hydrate already join literal player.


Final static review also read CBaseEntity::Hide0x1009d2a0: it ORs EF_NODRAW0x40 into
m_fScriptSavedEffects when ScriptHidden, otherwise m_fEffects, then ForceTransmit. The base
Hide slot in ElysiumEntitySlots.cpp is still only counted; CreateCorpse correctly calls it,
but static-source hide presentation remains that slot owner's missing wire. It must not be
implemented as ScriptHide/bHidden, which would incorrectly suppress the +0.5 removal think.
No C2 patch is owed to a generated Slots.cpp; the integrator should route the real Hide body
to ElysiumEntitySlotBodies.cpp with a hand verdict when its saved-effects input is supplied.

Added NpcCombat.CorpseThinkInstall also pins mortal/Kindred/static replacement deadlines and
explicit NULL versus missing restored identity. There are eleven new V4c arm cases in C2's
six test files; DeathRestore is the sole whole-case deletion. No tests were executed.


Recording-service integration (Private/Tests/ElysiumTestServices.h, integrator only):
the new prop API must also be answered by the existing one-label-per-stem fixture. Beside
AnimatedPropRestClip, add:
~~~cpp
virtual FString PickAnimatedPropRestClip(const FString& Stem) const override
{
    return AnimatedPropRestClip(Stem, 0); // 0x10427fc0: fixed fixture candidate is a singleton
}
~~~
It spends no draw for the fixture singleton/miss; production uses the raw weighted adapter.
The recording services currently do not override NpcActivitySequences. Add a table-driven
answer beside GetBodyClipByRawIndex:
~~~cpp
virtual void NpcActivitySequences(const FElysiumActivityClipRequest& Request,
    TArray<FElysiumNpcClip>& Out) override
{
    Out.Reset();
    for (const auto& RawRow : BodyClipsByRawIndex)
    {
        if (RawRow.Value.Clip.Activity.Equals(Request.Activity, ESearchCase::IgnoreCase))
        {
            FElysiumNpcClip Row = RawRow.Value.Clip;
            Row.RawIndex = RawRow.Key;
            Out.Add(MoveTemp(Row));
        }
    }
    Out.Sort([](const FElysiumNpcClip& Left, const FElysiumNpcClip& Right)
        { return Left.RawIndex < Right.RawIndex; });
}
~~~
Fixtures that previously stood only bNpcActivitiesResolve/ResolvedNpcActivityLabel must seed
their actual activity/weight/raw-index rows explicitly. Do not make a bare miss succeed by
borrowing the full resolver's fallback answer. C2's new pick tests provide their own candidates
and explicit raw rows, independent of that old resolver double.

### V4c second-pass close measurement (2026-10-05; not green, no commit)

The three-build allowance is exhausted: build1 passed118.6s; build2 failed27.5s on two
fixture ResolveClipPair calls missing the required side argument; build3 passed25.2s.
No source changed after build3; temporary clock/sweep diagnostics were removed before it.
The lanes and all integration remain uncommitted and unstaged on spec-0002/step-2.
An external documentation commit advanced HEAD to947e57b6 (AGENTS/HANDOVER only).
Neither V4c nor V4s was ticked; no commit or push. Root200KB is absent.

Final default:169 executed,168 passed,1 failed (Stance.Driver),15.8s wall.
Final arm:1624 executed,1611 passed,13 failed,53.8s wall; no abort or unrun test.
Baseline default170 loses only the retired world-sweep MeleeBatch test, replaced by arm
character-clock coverage. Baseline arm1594 gains30 cases.
Full Arena ONCE after build3:132 records,110 pass/4 fail/16 expected-fail/2 unexpected-pass,
527.5s wall, report20261005T053609.881342Z. The eight new records all pass.
The earlier33-record named run was28 pass/4 fail/1 expected-fail; record-only cower/footstep
corrections passed by name and in the full run. After the full run, input_setrelationship's
unkillable staging passed by name (20261005T054700.512138Z); the full report still records
its original failure. No second full run. Kernel check7/7; no re-bake or corpus write.


Recovery and staging corrected in this pass:

- NPC event3031 stages at curtime; CommitTime>0 is not an estimate discriminator. The invalid
  guard was removed; BeginRangedShot still stages no NPC estimate. Retail0x10238160→0x10238320→
  0x102383b0→0x102387b0. cover_move_shoot and team_damage_gate pass again.
- Constructor0x1028d230 writes m_flStanceTime(+0x64e4)=curtime; this host writes it before
  Troika spawn's disposition lookup. NPCInit0x1029a6a9 seeds a valid default disposition index;
  bHasDispositionIndex now reflects that. These fixes remove spurious initial stance draws,
  but do not yet recover idle_lookaround/unknown_crouched_band's required verdicts. No reseed
  or expectation relaxation was made for either; the remaining draw-order audit is V4c.
- InputFaint0x1029f250 resets slot614, records line0x26c2 and SetSchedule(0xfa,false).
- Dead-enemy records originally killed at0.417, before network-init0.80. Retail0x1028d8d0
  walks every Troika (derived bit0x40) and slot5840x1028d910 resets all think timers, including
  that corpse's +10 deadline. Death is now after network-init; both controls pass. Quiet
  ALERT is probed before0x4b's five-second timeout, rather than promised through end8s.
- seed_health now recomputes before the initial probe: effective cap99999 is measured.
  The five-hit control ends alive at90 wounds. Knockout flags0x440a0000 kill positive18 at
  18 wounds; cower flags0x40000400 kill positive18 at19 wounds (including trigger1), both
  at0x102beea4, far below cap. Zero during cower raises LIGHT_DAMAGE at0x10265ed0 and interrupts
  the schedule/clears its flag; zero is staged before cower, and the positive packet atflag+0.45
  lands inside the unchanged0.5 deadline. No damage arithmetic or historical fifth-hit claim.
- The same-team contact control passes on identical negative-Y seats. No contact/impact/hit-list
  write reaches the matching teammate. The scalar cower trigger was replaced by a real player
  handle through damage_packet; the actual OnTakeDamage chain runs for zero too.
- ranged_open_fire and input_setrelationship use retail MakePlayerUnkillable: dead-enemy
  selection correctly drops a player who dies under their real bullets. Their expectations
  remain intact. Footstep staging now seats the player150cm north of the first patrol goal;
  the old ~700cm seat allowed one-second thinks against a0.939Hz walk, and0x10091880 lost loop
  windows. The near cadence0x10290b60 restores repeated2050/2051; event windows are unchanged.
- SharedPick/RagdollSeed/RunAnimation/interval-movement tests now supply models; the shared
  picker already returns-1 on empty0x10427fc0, and the test guards its array index. Session
  registry construction covers uninitialized headless fixtures. Other migrated tests author
  raw candidates, exact disposition levels, real feed cells/side, and the actual event shot.

Seams still named: D9 CalcPose-at-cycle / bone cache, m_flPoseParameter blending, slot247
seqdesc bounds, held weapon model clock/events/weights (0015), ordinary ragdoll rig/hitbone/
Spine2/force (V4d), render-alpha presentation, restored-corpse presentation (V6), real reload
slots322/323 (V5b), and TzimisceMelee+0x910. No pipeline/body-data/bbox import or re-bake.
Bounded silent inventory475 sidecars/9401 clips in0.404s found only male/female move_and_ranged
flamet_attack and flamet_attack_layer without3030..3044; both have no events. No timer fallback.


### V4c final integrator: establishment and record clocks (2026-10-05)

Closing reads of vampire.dll NPCInit `0x1029a0b0` / base `0x10273390`,
NPCInitThink `0x10273aa0` (listing), MaintainSchedule `0x102817c0` (listing at
`0x10281b5a..0x10281b89`) and SetState `0x1026e340` settle the initial edge.
Troika stores current NONE directly at `0x1029a0f5`; base init stores ideal IDLE
at `0x10273473`. NPCInitThink applies relationships, calls StartNPC (slot 422),
then tail-calls PostNPCInit (slot 421). The first AI MaintainSchedule invokes
SetState(ideal) at `0x10281b63` before GetNewSchedule. This is a real NONE -> IDLE
transition: SetState stamps the change clock, writes both state words, and calls
slot 463's state-change hooks. Admission must not replace it with IDLE.
Arena traces and ordered expectations retain the edge; state bans begin after
that establishing edge. Other kinds and later state edges are still judged.

Animation picks already share `ElysiumRng::NpcSchedule` and SeedAll resets it.
The surviving input was the boot clock: NPCInit and StartNPC branch on curtime
in the first second (`0x10273390` / `0x10273ad0`). A later rebuilt record skipped
startup delay draws and ran its selector at activation, while a named run waited
for startup. Arena staging now resets its game clock before Load, alongside
SeedAll, so the same seed gives the same initialization and draw order. This is
a harness correction; map hosts and runtime scheduling keep the retail clock.
Seeds and expectations for idle_lookaround and unknown_crouched_band are intact.

AndreiBlood Activate writes COMBAT at `0x1035dc93`; the CanPlaySequence arm's
two idle assertions now explicitly stage SetState(1), preserving spawn state.

FrenzyShadow NPCInit `0x10375c80` calls the controller init, sets ideal HUNT 0xb,
and calls real SetState(0xb). Its selector `0x10375d90` only intercepts COMBAT
with a live enemy; this ownerless row falls through Human to Troika `0x102af660`.
With no hunt path, MADE_HUNT_PATH clears; the default expired hunt timer selects
HUNT_FINISH 0x85. GET_PATH_TO_LASTPOSITION's listing `0x10285bb0` copies the stored
vector into a location goal without an enemy/position guard. Refusal calls
TaskFail(0xc) at `0x10285c87`. This arena's zero stored position is inside the
central cover block and has no route, so its roll call now asserts HUNT_FINISH,
the path task and NO_ROUTE, plus survival. The old generic taskfail ban depended
on invented IDLE. The production frenzy population/caster remains unrecovered
in the player substrate; this hand-authored row does not assert that seam.

The first full fresh-clock run exposed three additional record assumptions:

- lifecycle_relationship_flip's player died at 18.5 after seven real event shots;
  BestEnemy `0x10274475` correctly dropped it at 18.6, yielding ALERT. Stage the
  same retail MakePlayerUnkillable latch as the other combat records. The state,
  enemy and combat-program assertions are unchanged.
- SetSchedule `0x10280e50` clears all six condition words. RunAI's host trace
  compares the set entering GatherConditions to the set leaving it. When a
  program installation already cleared SEE_HATE between gathers, OnLooked's
  real clear (`0x1026a2cf`) has no falling delta to emit. memory_occluded_kept
  now probes SEE_HATE/SEE_ENEMY false at the original 1.3/2.1 deadlines; initial
  sight, LOS output ordering, debounce, memory and state assertions remain.
- FACE_ENEMY's listing `0x102aae61..0x102aaf24` uses the override or remembered
  enemy position, not an unseen player's current location. face_enemy_turn's
  6.0 teleport coincided with a new player scan, so no new LKP was written and
  facing the old LKP correctly completed immediately. At 5.9 (after a scan),
  the cached list's OnLooked `0x1026a2c0` writes the new LKP before rescan:
  NOT_FACING_ATTACK at 6.0, task starts 6.1, SEE_ENEMY rises during the turn at
  6.2, FacingIdeal completes at 6.3. The record keeps the 0.5 turn bound and
  probes SEE_ENEMY true at 7.0; demanding a new rising edge after completion
  incorrectly imposed event order retail does not promise. No runtime change.

### V4c closing worker: the first-pass random stream (2026-10-05)

Retail reads were vampire.dll 0x1028d230 (constructor listing), 0x10273390,
0x10272650 (listing required), 0x10272130, 0x1029a0b0, 0x10273aa0 (listing),
0x10273ad0, 0x1028e310, 0x102c0f70, 0x102c12a0, 0x1028fa50, 0x102b1a20,
0x102b3e00 and 0x102b15c0; integer generation is vstdlib.dll 0x10002e60.
No arena seed or expectation was changed.

The constructor stores the default name Neutral at 0x1028d42c, not an empty name.
NPCInit stores current state NONE at 0x1029a0f5 and ideal IDLE at 0x10273473,
then seeds the valid disposition index zero at 0x1029a6a9. Neutral is the first
retail disposition-table row. Activate's SetDisposition(Neutral,1) therefore
refreshes tuning without calling slot611. The host admission barrier must retain
those state words: its previous typed Idle write cleared the raw NONE overlay,
so the first base gather ran the idle-sound RNG before retail's state transition.

The locked idle_lookaround path, through its first selector, is:

| order | retail transaction | advancing draw | port after correction |
|---|---|---|---|
| 1 | NPCInit heaviest ACT_IDLE probe, 0x1008dd30; SetIdealActivity -> 0x10272130 -> 0x1008dc40/0x10427fc0 | one weighted integer for the multiple idle rows | same; heaviest itself draws nothing |
| - | ResetFakeReloadCount 0x102c54c0 | none for TutorialThug's equal 6/6 bounds | named missing-template input still answers 0/0; the write is retained, no draw |
| - | Activate -> SetDisposition 0x102c0f70 | none for unchanged valid Neutral index | same; constructor no longer supplies an empty lookup |
| 2 | StartNPC 0x10273ad0, curtime <=1 | Float(0.1,0.4) | same |
| - | first base GatherConditions 0x1026ecce sees NONE | no idle-sound draw | same; admission no longer overwrites NONE |
| 3 | Troika corpse gather 0x1028fa50 | Float(2,2.5) | same |
| 4 | comfort gather 0x102b1a20 | Float(0.2,0.4) | same |
| 5 | first idle selector 0x102af822 | Int(0,99), compare with 10 | same; seed4 selects 0x4f at t=0.350 |

Two additional former port draws came from AElysiumNpcBody's visual classifier
before the first AI pass. They have no retail selection transaction: the kernel
resolver owns the NPC pick. The classifier's interim cast preview now uses
heaviest selection, while actual kernel selections remain weighted and shared.
The stream was neither re-seeded nor split. The common picker also observes
vstdlib 0x10002e60's no-generation return when total positive weight is one,
even with several candidates (zero-weight rows included).

The rest of idle_lookaround's program uses the existing task order: look-around
activity lookup, WAIT3, WAIT_RANDOM3 (one float), SET_ACTIVITY idle and its
resolver maintenance, WAIT_RANDOM2 (one float), then the next idle selector.
Stance selections retain 0x102c12a0's alternate-idle chance, time-gated change
chance and 0x102c1230's destination retry loop. A miss, singleton, heaviest pick,
equal integer bounds or exact-label presentation consumes no value.

unknown_crouched_band has the same initialization without the look-around
selector roll. After maintenance installs IDLE, subsequent base gathers can
roll idle sound (0x1027a420), with corpse and comfort draws only when their
clocks are due. The first changed unknown handle at 0x102b3e00 draws the run
Float(10,20), then start Float(5,10), before the sweep's Roll B Int(0,99) at
0x102b15c0; a retained handle draws neither timer again. Selector/activity/task
picks follow that gather. Build5 measures SEE_UNKNOWN and INVESTIGATE_SIGHT at
0.750 and schedule0x59; the unchanged record passes. Before these corrections
it retracted SEE_UNKNOWN and selected IGNORE_UNKNOWN0x60.

The named startup-path audit now selects look-around, but idle_lookaround is
still red: its unchanged blanket never(state) rejects the now-visible retail None -> Idle transition
at0.350, immediately before the required0x4f. That initial transition was
previously hidden by the admission state overwrite. It was not suppressed and
the record was not relaxed. Thus this wave cannot be called green under the
locked expectation. Raw first-pass state remains observable as retail requires.

Remaining inputs are still named: template fake-reload +0x34/+0x38 (the loader
0x101d3f10 reads Min default8, Max defaultMin; TutorialThug authors6), and include
group boundaries/last-local flags0x80 at0x10427df0. The equal-bound seam consumes
no RNG; its returned count is not claimed to be the live retail count.

Fixture migrations retain their subjects. Stance.Driver now supplies the model's
ACT_DISPOSITION activity row for NPCInit's heaviest probe, not only named stance
clips. Task completion fixtures supply a ten-second nonlooping model row and
reset its clock before testing unfinished branches; AutoMovement advances it
before those gates. Move-and-shoot supplies ACT_WALK/RUN before early translation.
Controller fixtures supply the valid Neutral table row; static-corpse identity
is separated from the absence of a ragdoll request; gestures pin m_AnimOverlay;
NPC melee pins kernel base presentation while player claim assertions remain.
ActivityResolve pins the unchanged sequence/no replay on an unchanged activity.
Hull restoration uses the forced normal setter after a small-hull mutation.
Arm worlds explicitly stage established IDLE after their initialization think;
initialization tests re-run NPCInit, and State19 pins that admission retains
NONE/idealIDLE. A refused desired-state request pins no change from its initial
word rather than assuming that word was zero. No test was deleted by this worker.


### V4c closing worker: final binary and full-run qualification (2026-10-05)

Six builds passed (112.703 / 18.500 / 18.859 / 21.765 / 15.125 / 60.031 seconds).
The final default tier is169 pass/0 fail. The final arm tier is1623 pass/1 fail;
all thirteen failures from the previous integrator are fixed. The remaining
NpcKernelBosses.CanPlaySequence test constructs AndreiBlood, whose Activate
0x1035dc93/0x1035dc99 writes COMBAT2 to both state words. Admission previously
erased that write. The test calls CanPlaySequence(false,0) as though the actor
were idle; it must explicitly stage SetState(1) for those two idle assertions.
The unaffected pure state-arm and disregard-state assertions remain green.
This is a stale fixture, not a reason to restore the admission overwrite.

The sole full run on build6 is20261005T064949.706263Z, aggregated index.json:
132 records,105 pass/9 fail/17 expected-fail/1 unexpected-pass. Its arena-host
subreport has125 records; seven map-host records are in the aggregate. All eight
new V4c records pass. This result supersedes any claim of full-run RNG closure.
Named build6 unknown_crouched_band passes at0.750; the full run still retracts
SEE_UNKNOWN at0.700, raises IGNORE_UNKNOWN and installs0x60. Named startup
reaches the selector at0.250/0.350, whereas later full-run stages select at0.000.
The arena re-seeds before RebuildStageWorld but carries the game clock into
Activate; NPCInit/StartNPC branch on curtime<=1 (0x10273390/0x10273ad0), so that
later path omits the initialization delay/random-start transaction. This is an
evidenced difference in prerequisites; the complete full-run draw interleaving
remains unfinished. No new known_red or relaxed expectation masks either record.
In the full run idle_lookaround still answers0x6b as well as emitting None->Idle;
the record stops on the forbidden state event before its selection assertion.

Retaining species state also exposes rollcall_vfrenzyshadow's real NPCInit state
0xb (0x10375c80): HUNT_FINISH0x85 runs GET_PATH_TO_LASTPOSITION and fails0xc at
its first tick. The old admission erased that state. The arena has no usable
route for the goal0,0,0; a no-failure assertion needs its whole retail hunt path
and navigation prerequisite audited. It is newly red, not the baseline hidden
species problem. No navigation or selector shortcut was added.

The three damage records still measure cap99999; the high-health control is
alive at90 wounds after five18 commits; knockout and cower call0x102beea4 with
18 and19 wounds, respectively, flags0x440a0000 /0x40000400. These distinguish
the flag rule, not the historical fifth-hit attribution.
