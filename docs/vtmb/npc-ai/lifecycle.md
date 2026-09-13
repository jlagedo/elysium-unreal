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
   actor's activation with the next actor's keyvalue load.
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
  `SetSchedule`). `InputStartPlayerDialog` `0x1029ef80` (line `0x2677`),
  `InputStartPlayerDialogRemote` `0x1029f060`, `InputStartPlayerDialogUnforced` `0x1029f120`:
  `FinishTalking`, reset, `m_bForceDialogStart = 1`, `SetSchedule(0x6d)`. `InputFleeAndDie`
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
map-wide `SetAIEnabled` (`FElysiumEntityWorld::SetAiEnabled`, the slot-584 form on every NPC, with
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
its setter `0x101aa750` has no caller and its only reader is the same function — never set.
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
