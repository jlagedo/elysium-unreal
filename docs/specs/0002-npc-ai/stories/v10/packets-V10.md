# V10 + V12 — settled reading packet

Planner reads, 2026-10-05, main checkout `E:\dev\elysium-unreal`. No source edit,
build, test, arena or commit. `uv run elysium research kernel --check` passed all
seven checks before using the ledger; its normal query log is outside the repository.
V4c baseline is `d0f79574`, not a claim about the installed binary. V4d is editing
the main checkout now; V5b will also land before this wave. Locate functions anew.

## Evidence method and limits

Read AGENTS.md first, spec V10/V12, TRACKER boxes, triage N4/Q-V3bf1, Arena/README,
V5 README, `brief-V5b-1-fake-reload.md`, `brief-V5-integrator.md`, V4d brief and all
four supplied scout packets. Used indexed `vtmb_where` for the hearing/player,
full-investigate, maker, ambient and cadence leads, then the actual corpus C/assembly.
Every listing below was checked to be the **vampire.dll** result. Module-qualified
`vampire.dll::0x...` references were rejected by this tool; bare references returned
the module banner. At 0x101baf80 and 0x1009e280 it also returned unrelated client.dll
bodies: those bodies are excluded. No headless Ghidra was needed.

Read `Vampire/dlls/vampire.dll` directly with an inline read-only Python PE section
reader, image base 0x10000000, for constants. Did not run `sol-ghidra/pe.py` or
`maps.py`: those helpers write output. Read BSP lump 0 (offset/size at byte 8) in
both `Unofficial_Patch/maps/sp_tutorial_1.bsp` and `Vampire/maps/sp_tutorial_1.bsp`.
Read the already deployed `Content/ElysiumCorpus/vdata/system/sound_volume_table.txt`;
loose copies under the two game directories were absent. Table values are authored
data corroborating listing lookups, not constants guessed from the executable.

Labels: **verified** = directly read code/listing/data; **inferred** = consequence
of those reads without a runtime observation; **measurement** = integration must
record it. Scouts are leads, not primary evidence.

## P1. Finite sound lifecycle and listener freshness

**Verified:** `CSoundEnt::InsertSound 0x101bac90` obtains a record, writes origin,
raw type, integer volume, `time=curtime` at sound+0xc, `expiry=curtime+duration` at
+0x10, occlusion byte +0x14, then owner (or invalid handle). Allocation
`0x101bab50` takes the free-list head and prepends it to the active list; failure
returns -1 and insertion writes no stimulus. No wall-clock stamp is involved.

**Verified:** `CSoundEnt::Spawn 0x101ba6f0` initializes and arms cleanup at
curtime+1.0. Cleanup `0x101ba890` rearms at current time+0.3, not old deadline+0.3.
Assembly 0x101ba899..0x101ba8ae reads **f64** 0x1047b868; bytes
`33 33 33 33 33 33 d3 3f` decode to 0.3. The expiry test at
0x101ba8bd..0x101ba8e6 loads +0x10, adds **f32** 0x10450aa0, compares to curtime,
and excludes sentinel -1 (`0xbf800000`) from removal. 0x10450aa0 bytes
`00 00 80 40` = 4.0. Finite sound is removed when expiry+4.0 **<=** current cleanup
time, including equality; before a due cleanup, crossing the threshold alone
does not remove it. Long-duration sounds are not removed four seconds after insertion.
0x104454c0 is f32 1.0. Reading the first four bytes of the f64 recurrence constant
as f32 gives a meaningless tiny number; the assembly settles its width.

**Verified:** `CAI_Senses::Listen 0x1030f940` clears its audible head, obtains the
virtual interest mask (slot 473), walks every active record, gates on `type & mask`
and `CanHearSound`, prepends admitted records to its own list, calls OnListened
slot 470, and **then** stamps senses+0x84 with curtime. A zero mask still calls
OnListened and stamps the listen time. It does not use an event serial cursor.
Active traversal is newest allocation first; the admitted-list prepend reverses
that order for OnListened. Rewriting a reserved record does not move its list position.

**Verified:** `CanHearSound 0x1030f7b0` resolves the owner, refuses the targetability/
owner guards, self, and any start time **<=** last listen; reads ear position,
hearing sensitivity*volume, owner stealth adjustment, occlusion adjustment, then
slot 467. It never reads sound+0x10 (expiry). The radius comparison at
0x1030f8b3..0x1030f8c8 admits equality. Keep these existing audibility gates;
removing expiry is not permission to bypass QueryHearSound or targetability.
`PerformSensing 0x10310710` gates on senses+0x80 and calls Look before Listen.

**Verified:** `OnListened 0x1026a5e0` rebuilds hearing words, walks admitted sounds
in list order, switches on the exact type (a combined mask is not two stimuli),
draws reaction delay per admitted recognized record, submits the delayed-condition
list, promotes due entries, then emits OnHearWorld/OnHearPlayer/OnHearCombat.
`0x1026a8a0` slot 471 draws 0.2..0.9; flinch draws 0..0.5 in OnListened.
`0x102cc6c0` tests count<8 BEFORE duplicate search; `0x102cc590` min-updates an
existing deadline with curtime+delay. Preserve this downstream chain and RNG order.
Output time is not the first Listen time. No general R1 investigation rewrite is owed.

**Verified port:** `ElysiumNpcSenses.cpp::TickHearing` calls EventsSince(Cursor),
advances Cursor before admission, and rejects expiry<Now as well as time<=last.
`ElysiumGameSound.cpp::Emit/Evict` drops on expiry and insertion-age four seconds,
only when another emission arrives; it trims a prefix and may miss an expired
later element behind an unexpired long-lived one. These are separate divergences:
listener expiry, insertion-age pruning, cleanup trigger/order, and cursor-driven
list access. V10 repairs lifecycle/access; it keeps EventsSince for unrelated
event consumers. A stable allocation identity and a changing revision serial
must be separate for the reserved slot.

**Boundary:** initializer `0x101baf80` constructs **64** pool entries and reserves
one per client; `0x101bab50` refuses an empty free list. Port MaxRetained=128 and
overflow eviction are inherited R1 allocator work. All this wave's records stay
below capacity. This is not a new modernization or a claim of whole-CSound parity;
judge choices are in README. Reserved records must not be age/expiry evicted.

## P2. N4 — defect verified, intermittent cause not established

**Verified port order:** `ElysiumArenaScenarioRunner.cpp::FireDueActions` schedules
the record's t=2 PlaySound through queued input. `ElysiumEntityWorld.cpp::Tick`
calls RunThinks before ServiceEvents. `ElysiumAmbientGeneric.cpp::EmitAiSoundEvent`
inserts with owner origin before the port audio-availability gate. Retail ambient
dispatcher `0x101ad470` reads backend duration, clamps below 1 to 1, passes raw
type and the level's volume, non-occludable, owner to InsertSound. The port's
duration is max(1, backend duration); the actual installed wave duration is unread.

**Verified:** `git show -s d0f79574` reports final arena 132 records: 113 pass,
1 fail, 16 expected-fail, **2 unexpected-pass**. It explicitly keeps the two N4
hearing intermittents. interest_mode_never moved UP->EF->UP during V4c due to
the repaired initial-state ban; hear_world_investigate remains the other UP.
Fresh per-stage zero time fixed RNG admission/draw leaks; it did not close N4.
Commit tests: default 169/0, arm 1624/0. Do not present these as a new run.

**Verified listing/port arithmetic:** `CalcNextNormalThink 0x10290b60` and
`CalcNextAIThink 0x10291230`, compared with ThinkCadence.cpp NormalInterval/AiInterval:
at 768 units, no LOS, in PVS, ordinary normal=0.3 s, AI=(768-512)/896=0.285714 s;
LOS pins 0.1; out-of-PVS normal=1.0. NPCThink's normal-due gate precedes reduced/
full AI; full GatherConditions also has state/PVS/sense-enable gates.
**Inferred:** even a two-normal-pass bound near 0.6 s is below minimum D=1 at
60 Hz, so ordinary in-PVS cadence alone cannot explain a delayed expiry rejection.

**Measurement, not settled:** A: last Listen at t, queued insert afterward also
time t, so next Listen rejects strict freshness. B: strictly fresh insertion,
but first eligible Listen later than expiry (or intervening bus eviction).
Retail rejects A too; the four-second grace does not cure it. Historical traces
and scouts log input/promotion, not Listen/PVS/gates/actual D. No failing boot was
replayed by the planner. First integration record must collect sound identity,
revision, insert time, duration, expiry, last listen at insertion, each actual
Listen entry/exit, old/new stamp, candidate rejection reason, each cleanup/
eviction and think/full-gather gates. Diagnose **before** behavior hunks land.
If neither A nor B fits, retain the trace and recover the measured chain. Never
relax freshness, add epsilon, bump duration, change seed or reorder world IO to
make the record green. A measured IO-order divergence needs actual retail engine
delivery evidence (judge); no such divergence is authorized by this packet.

## P3. V12 is the reserved player sound, not a footfall insertion

**Verified:** `CAI_BaseNPC::HandleAnimEvent 0x10274e30`, final cases 0x802/803,
calls `0x1026d460(this,0)`; 0x804/805 calls mode 1. Complete step helper listing
selects a surface WAV and emits audible channel 4/CHAN_BODY, with no InsertSound.
Player handler assembly `0x10178a10`, 0x10178a4d..0x10178a5d, routes 2050..2053
straight to return 0x10178c63 after observer/source guards. Do not create AI
footstep sounds on either animation-event path.

**Verified:** `UpdatePlayerSound 0x1016b480` obtains ClientSoundIndex and
SoundPointerForIndex, reports absent reserved slot and returns; otherwise rewrites
that record. Initializer `0x101baf80` sets each reserved expiry=-1. Producer writes
owner, origin (slot 220/+0x370), raw type **4**, integer volume and time; no expiry
write and no InsertSound. `CBasePlayer::PostThink 0x1016be10` calls it after its
animation advance, dispatch and UpdateCharacter. PostThink's game-over/locked/
not-alive/observer gates precede this branch. Those existing named gate seams
are not silently treated as recovered inputs.

**Verified mode priority** from C plus asm 0x1016b4b8..0x1016b655:

1. FL_NOTARGET 0x8000 writes volume=0 and **returns before restamping**.
2. IN_JUMP from player+0x2088 &2 wins even off ground.
3. Otherwise only FL_ONGROUND permits landing/movement category: animation 8 soft,
   10 or 11 hard; else nonzero **3-D** absolute speed, ducked sneak, strict
   speed>PLAYER_RUN_SPEED run, otherwise walk. Stationary/air without jump target 0.
4. Write target volume; rise immediately; when decreasing compute
   `int(oldVolume - frametime*250.0)` (ftol toward zero), clamp at target.
5. m_fNoPlayerSound +0x22a0 zeroes volume AFTER decay, then ordinary owner/origin/
   type/time writes proceed; zero radius does not release the reserved record.

250.0 is PE f32 0x104704b8 (`00 00 7a 43`). Deployed authored table:
sneak/soft-land=180 units; walk/run/jump/hard-land=240, all occludable;
PLAYER_RUN_SPEED=128.0. Table lookup is verified in the listing, numerical rows
are verified in deployed data. No finite player freshness interval exists.

**Verified port:** PlayerEntity.cpp Think calls UpdatePlayerSound on its ~0.1 s
pre-move heartbeat; existing post-move PostThinkAnimation is per frame in World::Tick.
UpdatePlayerSound samples movement, floats its decay and substitutes JumpHoldRemaining
for raw IN_JUMP. It refreshes a type-4 0.2-second bus event; silence retires it.
GameSoundBus Refresh retires/re-emits, moving allocation order and changing identity.
The real raw Jump source **already exists**: PlayerController.cpp publishes Jump
through World::SetPlayerButtons; World::GetPlayerButtons exposes the retained level.
The wave moves only this producer to the existing PostThink tail, reads that bit,
uses frame delta and integer decay, and rewrites one sentinel record in place.
Keep unrelated player think/stealth/law clocks unchanged. Landing latch/animation
publisher limits stay explicitly named; do not claim complete player SetAnimation.

**Inferred verdict:** “missing producer” in spec V12 is stale for this checkout.
Existing tutorial trace already heard type 4. Its producer fidelity defects are
real, but they are not N4's WORLD-sound defect. No content import/re-bake is needed.

## P4. Q-V3bf1 is a record error, settled from maker to alert ladder

**Verified data:** both retail BSPs contain targetname thug_maker, npc_maker,
NPCType npc_VVampire, NPCTargetname thug_1, investigate_mode=4,
investigate_mode_combat=4, **full_investigate=0**. These are individual rows,
not population aggregates. Patched row uses TutorialShovelhead and Shovelhead;
never copy the earlier TutorialThug arena fixture onto the witness map.

**Verified replay:** maker `0x1034b7b0`, asm 0x1034b928..0x1034b95d copies raw
map data +0x76cc to +0x66cc and calls child slot 107 ParseMapData. Vtable resolves
slot 107 to `CBaseEntity::ParseMapData 0x1009e280`; its loop passes EVERY key to
virtual KeyValue (+0x1b8). Datamap `CAI_BaseNPCTroika` identifies investigate_mode
+0x6338 and full_investigate +0x6340 as separate integer/boolean keyfields.
Base maker ChildPreSpawn `0x1034af30` and ChildPostSpawn `0x1034af50` are empty.
Port MakeNPC replays def keys, and KernelBindings binds full_investigate to
FElysiumNpc::FullInvestigate. No maker lane, pipeline lane or bake is warranted.

**Verified policy:** ShouldInvestigate `0x102b3270` reads +0x6338/633c and its
other refusal/relationship terms; it writes neither mode nor full-investigate.
Mode 4 admits hated owner (relationship result 1); it does not force alert rung 3.
`0x102b9060` on INVESTIGATE_SOUND prioritizes combat/bullet, then WORLD (direct
0x51), then PLAYER/DANGER (CommitBestSound, tail to `0x102b8980`). That ladder:
if full-investigate, set 3 first; default/0 sets 1 and returns **0x4c**; 1 sets 2
and returns 0x4d; 2/3 sets 3 and returns 0x51 or 0x52 via memory bit 0x8000000.
Port SelectSoundAlertSchedule/AdvanceAlertLevelGrade reproduce that body.

**Inferred, backed by recorded live trace:** first tutorial hearing should select
SCHED_TROIKA_ALERT_TURN_TO_SOUND (0x4c), not a regex matching INVESTIGATE. Correct
that one record assertion and its prose; retain its later real sight/enemy leg.
Integration additionally probes actual child FullInvestigate=0 and alert before/
after the hearing so baked-data provenance or another sound cannot masquerade as
this proof. If deployed row differs from the two read BSPs, record precise data
path/value and refer an import/bake to the judge instead of rewriting map content.

`vtmb_readers` for class+0x6340 returned **zero** typed and untyped accesses,
despite the verified untyped ladder read; `vtmb_grep /0x6340/` returned ladder
and thunk only, `/full_investigate/` none. These indices are not proof of no
writes. Q is settled by separate keyfields, concrete replay, mode body and ladder,
not a claim of exhaustive offset-writer coverage.

## Lead verdict table

| lead | verdict and implication |
|---|---|
| First scout: no CanHearSound expiry, grace four | Confirmed P1, plus exact equality, 1.0 initial / 0.3 recurring cleanup and long-duration arm. |
| Adversarial hearing packet | Confirmed defect; its causation caution retained. Expiry+grace is cleanup-driven, not an Emit-time filter. |
| First scout / footstep validator: no NPC/player event AI insertion | Confirmed P3. Do not add footfall producers. No exhaustive claim about unrelated collision chains. |
| Player reserved type4, sneak180/walk-run240, no duration | Confirmed P3. Port 0.2 seconds is not retail. Also repair PostThink placement, raw Jump, integer frame decay. |
| First scout: tutorial full-investigate unresolved | Superseded by concrete BSP/replay P4: 0, separate from mode4; 0x4c first rung. |
| sol-V10-n4: ordinary cadence insufficient; A/B remain | Confirmed as inference P2. V4c still leaves two UP; exact D, listen stamps and failing-pass gates remain measurements. |

## Still unmeasured, with exact attempted evidence

N4 cause and current WAV D: read producer/backend accessor and historical packet
traces; read V4c commit and current runner/world/think/senses. No live failure or
installed baked wave was run/read through Unreal. Integrator's diagnostic record
collects actual D, stamps, gates and evictions before repair and after repair.
Listing cannot establish installed binary revision, current baked maker receipt,
map movement/cadence or final deadlines. Record those during integration, not as
new generic retail-reading jobs. Pool pressure, full landing animation publisher,
and restore semantics are explicitly outside this wave, with judge alternatives.

The read CSoundEnt callback does not establish engine scheduling priority against
an NPC whose Listen is due at the same cleanup threshold. Integration must log
the actual port phase and inspect existing thinker ordering; the explicit
cleanup-before/after-Listen fixture controls pin the callback contract. If that
priority changes a gameplay verdict, recover/measure retail engine ordering or
refer it to the judge before claiming equivalence. No arbitrary pre-Listen phase
is a verified retail fact.
