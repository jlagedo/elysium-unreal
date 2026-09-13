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

`CCineNPC::Spawn` (`0x101a6f10`, shared with `CCineAI`): `SetSolid(0)`, `AddSolidFlags(flags | 4)`,
`m_bIsBCCTargetable = 0`, `m_bIsAlive = 0`. Then the auto-remove think, armed when the cine is
**unnamed** or spawnflag `0x10` is set — and only a **named** cine that armed it also takes
`m_startTime = curtime + _DAT_10449e10`. Spawnflag `0x20` **clears** `m_interruptable`; its absence
sets it. Finally `Relink`, `m_sequenceStarted = 0`, `m_hNextCine = -1` and `AddFlag2(0x10)`.

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

**Unrecovered:** `_DAT_10449280` and `_DAT_10449e10` (the cine's two delays), `_DAT_1044e658` (the
standoff goal's), `_DAT_104454d0` and `_DAT_1049b998` (the hint's angle scale and bias). None has a
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

**Unrecovered / seams:** `_DAT_10497cb0`, the goal-distance floor (two readers, both in this body).
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

**Unrecovered:** `_DAT_10497530`, the elapsed-seconds threshold both the re-roll and the blocked
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

`0x103937d0` (271 bytes) is the swing task: clear the navigator's path, resolve `m_hMeleeWeapon`'s
owner (`+0xa0`), run `+0x610`, and **TaskFail `0x1f` when there is no active weapon**. Otherwise ask
the weapon for the activity matching the requested one (`+0x5a4`), run `+0x5e0`, and choose a melee
sequence through slot 331; a refusal or a negative activity fails the task and a success sets the
activity through `+0x4dc`. Either way it then stamps `m_rflAttackTimers[t]` (`+0x66c4`) with
`curtime + FUN_103983d0(...)`.

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

## The `CNPC_VPlayerController` line's damage and death arms — `0x10376ae0`, `0x10376b10`, `0x10376b50`, `0x103a4950` (2026-09-13)

Four slot arms on `CNPC_VFrenzyShadow` / `CNPC_VPlayerController` / `CNPC_VWolfMorph`, all routed
through `+0x184`, the controller's stored sub-object. `OnTakeDamage` (slot 142, `0x10376ae0`)
forwards the packet to that object's slot `0x238` and **always answers 0**. `OnTakeDamage_Alive`
(slot 390, `0x10376b10`) forwards one indirection deeper — through the object's own `+0x9c` to slot
`0x618` — and also always answers 0. `Event_Killed` (slot 144, `0x10376b50`) is **three bytes**: an
empty body that ignores its parameter, so a frenzy shadow's death handling is fully suppressed
versus `CAI_BaseNPC::Event_Killed` — no ideal-state change, no corpse, no outputs.
`Event_TookLife` (slot 300, `0x103a4950`, shared by all three classes) is guarded on BOTH the
controller object and its AI component (`+0xa8`); inside both it builds the killed entity's debug
name and dispatches `thunk_FUN_1017e150(component, 4, -1.0, source)` — AI event type 4 at priority
-1.0, tagged with the retail literal `"CNPC_VPlayerController::Event_TookLife"`.

**Unrecovered:** `+0x184` itself. No word of this substrate stands for the controller object, so all
four arms take their guarded path.

### The expresser factory `0x10312cd0`

_Recovered 2026-09-13, story 29c-1._

Slot 424 on `CAI_BaseHumanoid` and `CAI_ExpressiveNPC`. The base gate `thunk_FUN_1027cae0(this)`
must pass; then vtable `+0x91c` (slot 583) creates the expresser and the result is stored at
`m_pExpresser` (`+0x5f48`) **before** the null test, so a failed factory still overwrites the word.
A null result answers false. Otherwise the expresser's `+0x04` is back-linked to this NPC, its
`+0x10` to `&this->+0x5f44`, `thunk_FUN_10312b20` finishes the setup, and it answers true.

**Unrecovered:** the expresser object's layout beyond those two back-links, and what `+0x5f44` is —
the census leaves both unbound.

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
fleshpile maker ignores all five. Everything after admission — the classname lookup, the `+0x98`
Troika check with its `"Non Troika Ent in NPCMaker!\n"` / `"NULL Ent in NPCMaker!\n"` warnings, the
roughly twenty field copies onto the child (view offset, move dir, physics update time, the
spawnflags with a view-cone bit at `+0x66c2`, several float pairs at `+0x63xx`), `Spawn`, the
rename from `m_ChildTargetName` (`+0x66bc` on the maker) and the ownership notice — is the base
body's, and the last thing it does is `m_iActiveRunnerCount += _DAT_104454c0` = **1.0**.

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
