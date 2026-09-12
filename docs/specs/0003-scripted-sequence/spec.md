# 0003 scripted-sequence — the cutscene beat as the NPC's own schedule

## Witness
`sp_theatre`'s courtroom walk-out: `scene_over_relay` → `walk_out_relay` starts seven
`scripted_sequence` beats (`walk_out_people_walk`, `_1`, `_2`, `_3`; `m_fMoveTo 1`) that send
Isaac, Therese, VV, Skelter and Nines ~19 m from the stands to the exit while the camera watches,
`walk_out_people_walk_3.OnEndSequence → prince_walk_teleport.Teleport`, then
`walk_out_fade_relay` kills the beats. `sp_tutorial_1`'s Jack chain (`script_1b` … `script_7b`,
`sJack_from_elevator`), the sabbat loop (`sSabbat1_0 → …`, `m_fMoveTo 3`) and the security guard's
feed (`sTalkguy_move`, `_move1`, `_die`, `sTalkguy_engage`), whose `OnEndSequence` wires open the
doors and start the conversations, and whose last beat's cleanup leaves Jack standing on the
ground (the float 0004 reports). Played against retail: a beat on an NPC whose AI is disabled
waits with nothing fired; a beat whose route fails idles and walks again; `OnBeginSequence` fires
when the NPC stands on its mark, not when the input arrives; a beat targeting an NPC that does
not exist yet looks for it every second and fires nothing.

## Scope
`scripted_sequence` and `aiscripted_sequence` (`CCineNPC`, `CCineAI`) from `BeginSequence` to
cleanup, and the NPC side that runs them: `NPC_STATE_SCRIPT`, `m_scriptState`, the scripted
schedules and their tasks.

Owned elsewhere and consumed here: the think cadence and the body hold — **0002** (15); the
kernel's failure route and the random wait — **0002** (25); the motor, its facing, the jump
links and the route refusal — **0002** (19, 24); choreographed scenes, their cast claim and the
`npc_VPlayerController` stand-in — **0010**; dialogue's right to cancel a beat — **0004**; the
oblivious refcount — **0002** (7).

## Sources
- Oracle: `docs/vtmb/entity_io.md` § "Scripted sequences", `docs/vtmb/npc-ai-reverse-engineering.md`
  (the schedule host, `MaintainSchedule`, `DELAY_INTERRUPTS`, `NPC_STATE_SCRIPT`),
  `docs/vtmb/animation_and_movers.md` § "Scripted travel speed is the resolved clip's own ground
  speed". Each story names its section.
- Authored data, V2 seams under `$ELYSIUM_WORK_ROOT/exports_v2/`: `maps/sp_theatre.entities.glb`,
  `maps/sp_tutorial_1.entities.glb`. Over all 108 maps `m_fMoveTo` is authored 702 times
  (`{0: 165, 1: 198, 2: 206, 3: 52, 4: 62, 5: 19}`); 88 wires leave these entities, 48 of them
  `OnEndSequence`.

## Witness data
**Retail, input to cleanup** (recovered 2026-09-12; Troika NPCs, which every VtMB NPC is).
- `BeginSequence` (`0x101a7390`): the 0.05 s re-trigger throttle, `FindEntity`, the priority
  (`0x200`) refusal, then `PossessEntity` — `CCineNPC::vfunc583 0x101a7880` (`scripted_sequence`,
  102 of 108 authorings); `CCineAI 0x101a9080` is the `aiscripted_sequence` twin. Possess writes
  `m_hCine +0x5d74`, `m_hTargetEnt`, captures movetype/movecollide/solid/solidflags/effects and the
  weapon's flag word (`+0x5f78..+0x5f8c`; `0x1000` ORs `0x40`), makes a NOINTERRUPT (`0x20`) NPC
  oblivious (`m_iIsOblivious++`), sets `m_scriptState +0x5d70` by `m_fMoveTo` (0/5 → 1 WAIT, 1 →
  4, 2 → 5, 3 → 6, 4 → placed inline then 1), calls `DelayStart(1)` for 1/2/3, and sets the ideal
  state `NPC_STATE_SCRIPT` (4). **The cine never thinks again**: `CineThink 0x101a8070` runs only
  while the NPC cannot be found (`CancelScript`, retry in 1 s).
- **Queue**: an NPC already holding a live `m_hCine` is not re-possessed — the new beat kicks the
  current cine's queued `m_hNextCine` (`SetTarget(next, NULL)`, `"kicking script … out of the
  queue"`), stores itself as `old.m_hNextCine`, returns. `m_hNextCine` is otherwise bound at
  `Activate` from `m_iszNextScript` and cleared at possess when that key is empty.
- **Selection**: `CAI_BaseNPC::SelectSchedule 0x1028a380` case 4 → `SCHED_AISCRIPT 0x2e` while
  `m_hCine` is live, else `"Script failed for %s"` → `CineCleanup 0x1027d170` →
  `SCHED_IDLE_STAND 1`. `TranslateSchedule 0x102cc080` splits `0x2e` by `m_fMoveTo` into
  `SCRIPTED_WALK 0x2f / _RUN 0x30 / _CUSTOM_MOVE 0x31 / _WAIT 0x32 / _FACE 0x33`;
  `CAI_BaseNPCTroika::TranslateSchedule 0x102b12f0` remaps them to `SCHED_TROIKA_SCRIPTED_WALK
  0xf2 / _RUN 0xf4 / _CUSTOM_MOVE 0xf6 / _WAIT 0xf8 / _FACE 0xf9`. All of it runs inside `RunAI`,
  behind `m_bDisableAI` and the console gate.
- **Programs** (blobs `0x105e7e00` walk, `0x105e7a10` run, `0x105e7608` custom; `_FAILED`
  `0x105e7c58` / `0x105e7870` / `0x105e7448`):
  - WALK / RUN / CUSTOM_MOVE: `TASK_SET_FAIL_SCHEDULE <own _FAILED>` · `TASK_SET_TOLERANCE_DISTANCE
    2` · `TASK_WALK_TO_TARGET | TASK_RUN_TO_TARGET | TASK_SCRIPT_CUSTOM_MOVE_TO_TARGET` ·
    `TASK_WAIT_FOR_MOVEMENT` · `TASK_PLANT_ON_SCRIPT` · `TASK_FACE_SCRIPT` · `TASK_ENABLE_SCRIPT` ·
    `TASK_WAIT_FOR_SCRIPT` · `TASK_PLAY_SCRIPT` · `TASK_PLAY_SCRIPT_POST_IDLE`; interrupts
    `COND_LIGHT_DAMAGE, COND_HEAVY_DAMAGE, COND_SEE_ENEMY, COND_SQUAD_SEE_ENEMY, COND_SEE_FEAR,
    COND_NEW_ENEMY`.
  - WAIT: `TASK_STOP_MOVING` · `TASK_WAIT_FOR_SCRIPT` · `TASK_PLAY_SCRIPT` ·
    `TASK_PLAY_SCRIPT_POST_IDLE`; FACE inserts `TASK_FACE_SCRIPT` after `TASK_STOP_MOVING`. Both
    declare **no** interrupts.
  - `_FAILED`: `TASK_SET_ACTIVITY ACT_IDLE` · `TASK_WAIT 1` · `TASK_WAIT_RANDOM 1` ·
    `TASK_SET_SCHEDULE <the move schedule>`; the same six interrupts. A failed route idles
    1 s + `RandomFloat(0.1, 1.0)` and walks again, forever. Nothing teleports; no output fires.
- **Tasks** (`StartTask 0x102827f0`, `RunTask 0x10288780`; ids from `0x10316ff0`): 8/9/10 share
  one arm — no `m_hTargetEnt` → `TaskFail 1`; within 1 unit → complete; activity `ACT_WALK` /
  `ACT_RUN` / the custom-move activity; nav goal `GOALTYPE_TARGETENT` on the cine, arrival
  direction = the cine's angles; no route → `TaskFail 0xc`. `TASK_PLANT_ON_SCRIPT 0x65` sets the
  origin to the cine's. `TASK_FACE_SCRIPT 0x66` sets the ideal yaw to the cine's and completes on
  `FacingIdeal`. `TASK_ENABLE_SCRIPT 100` → `DelayStart(0)`. `TASK_WAIT_FOR_SCRIPT 0x60`: start
  plays `m_iszIdle` (the pre-idle; playback frozen when it names `m_iszPlay`) else
  `SetActivity(ACT_IDLE)` unless `m_scriptState == 6`; run waits `IsTimeToStart 0x101a7540`
  (`m_iDelay < 1 && m_startTime ≤ curtime`), then fires **`OnBeginSequence`** (`FUN_101a81a0`) and
  `StartSequence(m_iszPlay, completeOnEmpty)` — an empty play is `SequenceDone` at once and
  `ClearSchedule`; a dead cine is `"Cine died!"` + complete. `TASK_PLAY_SCRIPT 0x62`: start
  `MOVETYPE_NONE`, state 0; run waits `m_bSequenceFinished` → `SequenceDone`.
  `TASK_PLAY_SCRIPT_POST_IDLE 99`: state 2; holds while unfinished and no `m_hNextCine`, else
  `Finish`. `TASK_WAIT_RANDOM 0x67` draws `RandomFloat(0.1, arg)`.
- **`SequenceDone 0x101a8460`**: `(no m_iszPostIdle || m_hNextCine live) ? Finish : (state 2,
  StartSequence(m_iszPostIdle))`, then **`OnEndSequence` fires unconditionally** with the stored
  activator. **`Finish 0x101a8640`**: with `m_iszPostIdle` and spawnflag `0x100` and no next cine
  → `"Post Idle %s finished"`, state 2, replay, return (cleanup and chain held; `OnEndSequence`
  already fired); else self-remove unless REPEATABLE (`0x4`), `CineCleanup`, `FixScriptNPCSchedule`
  (`m_iFinishSchedule 0` → `ClearSchedule`), and a live `m_hNextCine` (`next != this ||
  REPEATABLE`) is given the NPC and possesses it.
- **`CineCleanup 0x1027d170`**: state 0; oblivious−− for NOINTERRUPT; restore movetype,
  solidflags, effects and the weapon flags (`m_saved_solid` is never read back); detach `m_hCine`
  and the target; when `m_iszPlay && m_sequenceStarted`: spawnflag `0x2000` → `MoveToBoneOrigin
  ("Bip01")`, else unless `0x80` → the bone-0 position, `+1` Z, `FL_ONGROUND`, drop-to-floor
  probe; ideal state IDLE (7 when dead). **Bits `0x80` and `0x2000` are read here**, outside the
  class range the oracle searched.
- **`CancelScript 0x101a8c30`** (the `CancelSequence` input and the NPC's `Event_Killed` arm) →
  `ScriptEntityCancel FUN_101a7170` on every same-named entity: only when the NPC's state is
  SCRIPT → state 3, `CineCleanup`; `m_iDelay = 0`. No output. **`DelayStart 0x101a8cf0`** counts
  `m_iDelay` on every same-named entity whose classname is literally `scripted_sequence`; release
  to 0 sets `m_startTime = curtime + 0.05`.
- **The pre-idle is played by `TASK_WAIT_FOR_SCRIPT`**, after possession and travel; `Spawn
  0x101a6f10` and `Activate 0x101a8de0` resolve names and play nothing.

**The port today**: fires `OnBeginSequence` at the input; drives travel from the beat's own think
with a 4 s stall cap and a distance deadline, teleports onto the mark and fires `OnEndSequence` on
failure; plays the pre-idle at `Activate`; withholds `OnEndSequence` under flag 256; chains
`m_iszNextScript` as a queued input; runs any target, NPC or not, as a timing shell; overwrites a
standing claim instead of queueing. `docs/vtmb/entity_io.md` carries the 256 and bit-reader
errors above.

## Stories
In build order. A story is done when every behaviour it lists is in the substrate and its
recovery is written in the oracle section it names. Numbers are stable ids cited by other
documents; a split keeps the number and adds a letter. Each open story carries the retail
contract the code must match, the job, what it consumes or provides, and a size (XS–XL) with the
model / effort tier recommended for it.

- [ ] **1. The kernel vocabulary.**
  Retail: tasks `TASK_WALK_TO_TARGET 8`, `TASK_RUN_TO_TARGET 9`, `TASK_SCRIPT_CUSTOM_MOVE_TO_TARGET
  10`, `TASK_PLANT_ON_SCRIPT 0x65`, `TASK_FACE_SCRIPT 0x66`, `TASK_ENABLE_SCRIPT 100`,
  `TASK_WAIT_FOR_SCRIPT 0x60`, `TASK_PLAY_SCRIPT 0x62`, `TASK_PLAY_SCRIPT_POST_IDLE 99`; schedules
  `0xf2 / 0xf4 / 0xf6 / 0xf8 / 0xf9` and the three `_FAILED`, task lists and interrupts as above.
  Job: the identities in `EElysiumTask` / `EElysiumScheduleId` with runner verbs that default to
  failing by name; the eight programs registered verbatim in their own registration file beside
  the feed and combat families. `TASK_SET_TOLERANCE_DISTANCE 2` rides the existing operand; the
  `+0x688` scaler is unrecovered, so the motor's acceptance floor stays and is named. The
  task-side `ClearSchedule` and `TASK_WAIT_RANDOM`'s 0.1 floor are 0002/25's.
  Consumes: 0002/25.
  Provides: the vocabulary 2 runs and 4 reuses.
  Oracle: § "Schedules and tasks" (the scripted family), § `TASK_WAIT_RANDOM`.
  Size: M. Effort: Sonnet / high.
- [ ] **2. The NPC in `NPC_STATE_SCRIPT`.**
  Retail: `m_scriptState +0x5d70` (0 none, 1 wait, 2 post-idle, 3 cleanup, 4/5/6 walk/run/custom;
  `ShouldThinkFrequently 0x102c2430` reads 4/5/6); `m_hTargetEnt` = the cine; `SelectSchedule
  0x1028a380` case 4 and its `"Script failed"` exit; the translation by `m_fMoveTo`; the task arms
  above; `CineCleanup 0x1027d170`; `m_bSequenceFinished` raised by the animation advancing inside
  the think, so a stopped clock never raises it.
  Job: the state and target fields on the NPC (`ScheduleHost.MoveTarget` is `m_hTargetEnt`);
  `SelectSchedule`'s Scripted arm translating by the cine's mode, else cleanup and the idle
  disposition (named: `SCHED_IDLE_STAND` is not registered and Troika's next selection is 0x6b);
  the think routing that ticks the program for a cine-owned body while a scene-owned one keeps
  its early return; the runner verbs reusing `BeginScriptMove` for the route, its authored ground
  speed and travel cycle, `WaitForMovement`, the motor's `Face`, and the montage-slot segments for
  pre-idle, play and post-idle; `CineCleanup` with the oblivious decrement, the claim release and
  the bone snap as a stated seam until the animation driver exposes the played clip's root; the
  watchdog that released an abandoned move retired (the tasks' `"Cine died!"` and selection's
  `"Script failed"` are retail's exits); a segment-finished read on the embodiment that a held
  animation clock does not raise; the stall, deadline and crowd-settle constants removed.
  Consumes: 0002/15's hold (a route on a held body parks at the motor); 0002's motor and route
  refusal (24), so a refused route is `TaskFail 0xc` → `_FAILED` exactly where retail's is.
  Oracle: § "`NPC_STATE_SCRIPT`, walked" (new), entity_io § "Scripted sequences".
  Size: L. Effort: Opus / high.
- [ ] **3. The cine.**
  Retail: `BeginSequence 0x101a7390`, `PossessEntity 0x101a7880` (queue, NOINTERRUPT, `m_hNextCine`,
  the captures, the state switch, `DelayStart(1)`, ideal state), `CineThink 0x101a8070`,
  `IsTimeToStart 0x101a7540`, `DelayStart 0x101a8cf0`, `OnBeginSequence` from `TASK_WAIT_FOR_SCRIPT`,
  `StartSequence` (slot 584), `SequenceDone 0x101a8460` (`OnEndSequence` always), `Finish
  0x101a8640`, `CancelScript 0x101a8c30` / `ScriptEntityCancel FUN_101a7170`; the pre-idle played
  in `TASK_WAIT_FOR_SCRIPT`; `m_iszLinkedSequence` (no exported map writes it).
  Job: a cine interface in its own header, implemented by the beat and read by 2's verbs;
  `BeginSequence` as throttle → find (unresolved: cancel and retry in 1 s) → priority refusal →
  possess, with no output at the input; the queue instead of a claim overwrite; the possess
  captures as the port's claim (`ScriptOwner`, the body arbiter, ignore-collision for `0x1000`);
  `IsTimeToStart`, `DelayStart` over same-named `scripted_sequence` entities, `FireBeginSequence`,
  `StartSequence`, `SequenceDone`, `Finish` (256 hold, self-remove unless REPEATABLE, cleanup,
  `FixScriptNPCSchedule`, the next cine possessed directly), cancel without output; the `Activate`
  pre-idle removed; the saved shape (`m_iDelay`, `m_startTime`, `m_hNextCine`,
  `m_sequenceStarted`, possessing) with saves still refused while a beat owns an NPC. The
  `entity_io.md` corrections: `OnEndSequence` under 256, bits `0x80`/`0x2000`, `PossessEntity`'s
  naming (`0x101a9080` is the `CCineAI` twin, not `StartSequence`), pre-idle timing, the queue.
  Consumes: 0004's cancel for dialogue (through `CancelScript`).
  Provides: `CineCleanup`'s grounding to 0004/3; the possess-shaped claim 0010/6 reuses for a
  scene's cast.
  Oracle: entity_io § "Scripted sequences".
  Size: L. Effort: Opus / high.
- [ ] **4. Targets retail never has.**
  Retail: `!playercontroller` is a real `npc_VPlayerController` with AI; a bodiless NPC does not
  exist.
  Job: the port's motor-only stand-in and a bodiless record run the same task list from the
  cine's own think over 1's verbs on the scripted character, with retail's failure retry and no
  give-up; a bodiless record is placed on the mark — the one modernization, named at the site.
  Consumes: the `npc_VPlayerController` stand-in 0010 keeps.
  Oracle: entity_io § "Scripted sequences" (Port).
  Size: M. Effort: Sonnet / high.

## Seams
- Provides: the cine (possess, cancel, outputs) to 0004's dialogue start and to 0010's scene
  hand-offs; `NPC_STATE_SCRIPT` to every program family that tests the state byte (0002's
  `m_bfNPCStateFlags` script `0x08`); `CineCleanup`'s grounding to 0004.
- Consumes: the body hold and the cadence from 0002 (15), the kernel's failure route and the
  random wait from 0002 (25), the motor and the route refusal from 0002 (24), the oblivious
  refcount from 0002 (7), the montage-slot run, its claim and the player stand-in from 0010.
- Open at the 0010 border: what retail does when a beat possesses an actor a choreographed scene
  holds — recovered under 0010/6 before either side is chosen.
- Open recoveries, stated in code and oracle rather than guessed: the `m_startTime` write in
  `BeginSequence 0x101a7390` for beats that do not travel; `_DAT_104493d0`, the self-remove delay;
  the `+0x688` tolerance scaler; `CCineNPC::vfunc586 0x101a8840` (its own `FixScriptNPCSchedule`);
  the `TASK_SET_SCHEDULE` / `TASK_STOP_MOVING` `StartTask` arms; spawnflag `0x40` OVERRIDESTATE's
  reader; how plus_jenny and the prophet hold their level-start poses once the `Activate` pre-idle
  goes.
