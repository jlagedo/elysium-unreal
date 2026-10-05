# V6 settling packet — session, clock and lifecycle

Planner read, 2026-10-05. Baseline: V4c `d0f79574`; V4d and V5b must land before implementation.
Main-checkout source observations are provisional: other workers were editing it during this read.
No build, test, arena, bake, commit or source change was made. Only new V6 briefs were written in
`E:/elysium-work/worktrees/coord`. Addresses below are vampire.dll unless explicitly engine.dll.

## Evidence method

Read main AGENTS.md first, spec.md's V6 box, TRACKER.md's V6 box, triage N9/N10/N14,
Arena/README.md, V5 README, weapon-reload coder brief and integrator brief, V4d's brief,
and `git show -s d0f79574`. Looked addresses/fields up with `vtmb_where` before using oracle prose.
The mutable ledger was used for discovery, not as proof of the current port.

Independent corpus reads: code/assembly named below, with the module banner checked on every
bare-address reply. Attempts with `vampire.dll::0x...` and `engine.dll:...` returned no match;
bare addresses resolved and banners disambiguated them. Read the Ghidra driver README; no
headless Ghidra was needed. Read PE constants directly in an in-memory Python section walker
(no dump files), from `.elysium.local.env`'s VtMB root, `Vampire/dlls/vampire.dll`, base
`0x10000000`. Did not use sol-ghidra/pe.py because that script writes dump files.
Read raw `research/ghidra/types/datamap_records-vampire.dll.json` in the work root for SAVE/type
flags. Its type 15 TIME is authoritative over corpus `vtmb_fields`' misleading display.

Verified below means instructions, raw datamap records or a directly read port body.
Inferred means an interpretation or a reachability consequence. An arena observation is never
claimed from a static listing. All retained uncertainties have an integrator measurement below.

## 1. Clock: settled, including the engine half

**Verified.** Engine `0x200eef40` passes global-vars `0x212af2cc` to ServerGameDLL slot 1;
server `0x1011a0c0` stores argument 4 in `DAT_1070b228`. Thus server `gpGlobals+0xc`
aliases engine float `0x212af2d8`. Engine frame `0x200f7e40` copies double server time
`0x212b0760` to it, dispatches GameFrame, adds frame delta, publishes the new time.

Fresh spawn `0x200f55f0`: assembly `0x200f5bb4` writes low double word zero,
`0x200f5bba` writes high word `0x3ff00000`, and `0x200f5bc4` writes float
`0x3f800000`: **initial time 1.0**. This is not an arena choice.
Engine `0x2008f120` saves the departing level through CSaveRestore slot 13 on transition,
then spawns the destination and calls LevelInit with restoration enabled.
Server `0x1011a7a0` asks engine slot 115 (`+0x1cc`); missing saved level parses fresh entities.
Engine `0x2010ac90` forwards to CSaveRestore slot 9 `0x200975f0`.
Header writer `0x200962c0` captures server time; reader `0x20097280` copies header `+0x14`
to save-data `+0x1300`. `0x200975f0` publishes header time before entity restore and restores
the double server time afterward. Explicit load `0x20096010` calls `0x2008f2e0(map,1)`.
Therefore first visit = 1.0; return = destination's frozen time; load = saved current-map time.

**Verified startup caveat.** `0x200f5170` runs two `0x200f7e40` frames at double delta 0.1
when client count is nonzero. **Inferred:** a later interactive-ready observation can be near
1.2 or saved T+0.2. Do not assert 1.0 at an arbitrarily late ready broadcast. Assert the
pre-entity initialization fence and separately record activation/first-think time.

**Port divergence verified:** GameClock lives on the session and is documented as surviving
travel; BeginNewGame resets to default zero. Map Travel retains that value. Session load resets
to Session.ClockNow, and map snapshot FrozenAt is descriptive rather than a selected clock.
V4c ArenaStage::Stage resets to zero before Load. Keep its reset-before-Load and shared seed
ordering, change the epoch to 1.0. NPCInit `0x10273390` uses curtime <=1 to defer init by
0.1 (PE double `0x104493d0`); StartNPC `0x10273ad0` uses <=1 for RandomFloat(0.1,0.4),
else due-now. A correct 1.0 boot can reach StartNPC after 1.0 and consume fewer draws than
V4c's zero boot. Measure this; do not preserve the old stream by injecting a draw.

## 2. Time conversion: type-specific, not every timestamp

**Verified.** `CSave` slot 25 `0x101a0a80` writes field minus context base; `CRestore`
slot 24 `0x101a2a30` assembly loads context `+0x12e8` and adds it to decoded floats.
Context is save-data+0x18, so this is save-data+0x1300. Transition importer engine
`0x20097d00` replaces adjacent-section base with destination server time before import.

Raw SAVE rows checked:

| owner | TIME rows needed here | rows which are NOT TIME |
|---|---|---|
| CBaseEntity | PrevAnimTime +0x170, AnimTime +0x174, LastThink +0x178, NextThink +0x17c | NextThinkSR +0x180 FLOAT; think +0x118 and script-saved think +0xe4 FUNCTION |
| AIScheduleState_t embedded +0x5c40 | timeStarted +8, timeCurTaskStarted +0xc | iCurTask +0, status +4, failure +0x10 INT |
| CAI_BaseNPC | cache-interrupt +0x1b24, next-door +0x5b60, friend timers +0x5b88/+0x5b8c, last-state +0x5cc8, sound-wait +0x5ce8, move-wait +0x5cf0, last-damage +0x5d98, last-attack +0x5d9c, next-weapon +0x5da0, wait +0x5db4 | durations and condition masks are not converted merely because they are numeric |
| AI_EMemory_t | flLastTimeSeen +0x28, through custom ops 0x102df010/0x102df090 | positions +0/+0xc POSITION, hEnemy +0x24 EHANDLE |
| CBaseAnimating | effect-start +0x5a4, LastEventCheck +0x658 | sequence +0x6f0 INT; playback +0x6f4/cycle +0x6f8 FLOAT |
| CBaseAnimatingOverlay | flinch expiry +0x80c/+0x828/+0x844 | all four layers' cycle/playback/last-event-check are FLOAT |
| CAI_MoveAndShootOverlay | next-move-shot +0x18 | burst counts INT; pauses/initial delay FLOAT |
| CBaseCombatWeapon | weapon-idle +0x894 | two m_flNextAttack stamps +0x730/+0x734 FLOAT; reload/jam/interrupt bytes BOOL SAVE |

**Surprising verified retail fact:** base LastEventCheck is declared TIME even though
`0x10091880` consumes it as a cycle/lookahead bound. Layer LastEventCheck is FLOAT. Keep that
distinction, including any transition oddity; do not normalize it into a new rule.
Only flLastTimeSeen was found, not a field named m_flLastEnemyTime. Do not invent that alias.
Leaf archives and accessor walks need the same explicit base context; never subtract twice.
Queue FireTime is the port's delayed-I/O equivalent and needs the same remaining-delay contract.

Special policies are read in `0x101cf250/0x101cf2f0`: mode1 negative, mode2 exactly -1,
mode3 exactly zero, mode4 exactly FLT_MAX encode as 1e11, decode above the sentinel threshold.
NPC Save/Restore `0x1027bc60/0x1027c160` applies mode3 to wait, mode4 to friend extension;
motor `0x102e0b60` mode2; move/shoot `0x102e8aa0/0x102e8ac0` mode4 to +0x18.
Entity `0x100a9f70/0x100aa140` carries exceptional NextThink through FLOAT NextThinkSR.
These operations are verified; a tag preserving the same sentinel value is an implementation
choice, not permission to shift zero/-1/never as ordinary deadlines.

## 3. Schedule, references, movement and animation restoration

**Verified cursor correction:** embedded AIScheduleState at +0x5c40; cursor +0x5c40,
status +0x5c44, start times +0x5c48/+0x5c4c, failure +0x5c50. TaskFail `0x10273fc0`
and OnRestore `0x1027bf50` write/clamp failure, not cursor. Current port OnRestore clamps
Schedule.TaskIndex against RestoreTaskIndexCeiling: a separate concrete divergence to fix.

Save `0x1027bc60` writes extended header version1, enemy/target/path liveness bits, schedule
name, raw eight-byte-task CRC, then inherited datamap. Restore `0x1027c160` reads the header
before inherited restore. OnRestore `0x1027bf50`: SCRIPT requires valid cine; saved enemy/target
bits require resolved handles; version/name/CRC gate schedule retention; failure >=0x2a becomes1;
valid schedule sets saved-path flag, invalid invokes `0x1027be60`. Give-up clears goal/schedule,
clears conditions only with no enemy, and sets missing-cine SCRIPT to IDLE/ideal IDLE.
No cursor reset on valid arm: **inferred retention from the read write-set plus saved datamap**.

Final raw-row audit disproves the port comment that m_bConditionsGathered+0x5ca4 is unsaved:
it is BOOL SAVE. The six condition words have no saved row here. The port represents the bool
by Cognition.GatheredAt's sign. Save that BOOL meaning, not a timestamp under a bool name;
restore its sign at the selected base, stop forcing false in OnPostRestore. RunAI0x1026f110
legitimately clears it at next entry, GatherConditions0x1026ec30 sets it; GetNewSchedule0x102814d0
gathers if false before selection. This adds a snapshot/first-consumer check, not new cognition.

Path restore `0x102ee1e0` calls `0x102f1dc0`: invalidate transient waypoints; re-find goal route
through `0x102f2330`; on success clear retry memory0x20 and run the arrival hook; failure with
zero retry duration fails, otherwise arm retry/timeout; existing retry times determine future
attempts. DoFindPath's type1 follows target, type2 remembered enemy, type3 path-corner chain,
types4/5/6/9 use stored goal; invalid required handles fail. This wave saves semantic goal/type,
flags, target, movement/arrival/retry words and reconnects the existing navigator/motor. No raw
UE path pointer or StartTask replay. Existing RefindPostRestorePath always returns false.
Recast remains the project's existing navigation modernization; route choice need not reproduce
Source's node graph, but failure/order/cursor and preserved destination must.

Troika OnRestore `0x102998c0`: validate/release two patrol paths; base restore; LAST matching
place scan `0x102db5e0`; pedestrian link rebind; shoot-at timer reroll; follower/type and combat
activity rebind; spawn-called; consistency `0x10299a80`; hidden NULL think/FLT_MAX; slot593.
Server post-restore loop `0x1011a620` runs slot130 after restoration and in reverse restored-list
order. **Port divergence:** ApplyEntityRecord invokes hooks while later records are undecoded,
then overwrites NextThink after the hook, defeating legitimate hidden/restore rearming.
Split instantiate/decode/rebase/post-restore/presentation/activation; preserve later retail writes.

Animation verified in `0x1008f120`, `0x10091880`, `0x10098c80`: cycle/rate/time/finished,
past-half, ground/yaw speed, base event lookahead, four layers in order. Datamap carries all four
layer records (stride0x30), finished/flags/sequence/cycle/rate/weight/blends/activity/auto-kill/
event cursor. Move/shoot datamap carries +0x10..2c only: +0x0c is **not SAVE**; outer +4 must
rebind, not persist as pointer. This closes that scout uncertainty without inventing a field.
Animating OnRestore `0x1008df10` rebuilds caches/wind/bone-follow state, does not restart the
sequence. Combat restore `0x10323b60` calls it then restores Presence list conditionally;
Presence production remains0006, not an excuse to reset animation.

Current port FElysiumNpc::OnPostRestore calls RestartRestoredSchedule, RestorePatrolAndAmbient
clears bMoveIssued/reclaims a port-only set, and base Serialize lacks the schedule/animation/
move-shoot blocks. Remove the restart only after all supporting words and body adapters exist.
K1 SaveBlockReason rejects possessing cine; the separate saved director already has an archive.
`0x10299a80` never touches cine or cursor: it cannot justify K1. Removing K1 is this wave's
authorized end state, conditioned on its successful possession round-trip record.

## 4. N14: reservation writer and place ownership

**Verified:** PickSpotFor `0x102da0d0` samples bounds, tests existing markers/hull, and calls
AddMarker `0x102da860` on success (including the shipped exhausted-clearance success arm).
AddMarker stores occupant plus TWO absolute bounds vectors in stride0x1c, increments +0x588
under allocated capacity +0x584. ClaimMarker `0x102da7c0` finds an existing row and increments
in-use +0x564; it does not write occupant. Release `0x102da600` stores last place on NPC,
clears/fires conditional output, decrements count/in-use and swap-removes the full row.
Custom ops `0x102d9240/0x102d9320` save allocation/count, each handle and both POSITION vectors,
restore/rebind handles and clear spare rows. In-use is a separate word, not the saved row count.
Additional dependencies read whole:0x102d9fa0 sampler (assembly used for damaged argument flow),
0x102da9e0 overlap (assembly preserves the shipped mins.x used for lower Y/Z bounds),
0x102d9ed0 failed-box ring,0x102a0fb0 IsAreaClear. Sampler: origin+RandomFloat(local min−hull
min, local max−hull max) for X/Y, no Z draw for passed flag1. PickSpotFor pads hull X/Y by1
unit; after >8 occupied resamples increments failed-attempts and returns false; two clearance
attempts, records failed bounds with now+2, then warns and still AddMarker/returns true on the
exhausted-clearance arm. Constructor0x102d99d0 zeroes failed count/ring/in-use/markers and links
place at global list HEAD. Existing ClaimAmbientSpot simply takes place origin and StartTask
FIND overwrites destination with it; adding only a marker there is insufficient. Port this writer
chain as N14's prerequisite. min_bounds/max_bounds keys are raw authored bounds, currently
UNBOUND in KernelBindings; InfraActor::BuildDefKeys preserves AuthoredKeys. No pipeline change
is established. Missing actually baked keys is a concrete judge/content issue, not guessed bounds.
`0x102db5e0` scans the linked list without stopping: last matching place wins.
`0x10299a80` validates list membership then occupant; either refusal logs and clears +0x62ec.
`0x102b53d0` validates before release and returns if invalid.

**Current port verified:** FMarker contains only Occupant, Spawn zeroes it, Claim manages another
set, InterestingPlaceMarkerOccupant answers null. TroikaOnRestore scans Markers; later
RestorePatrolAndAmbient can reclaim a saved index. Thus the old claim “always loses its place”
is too broad; genuine missing writer/row-save/reader equivalence remains. Fix the coherent writer/
row transaction, not a load-only Claim hack. Raw capacity can close N17 as a dependency consequence;
report that evidence to R2 without taking its broader selection/default work.

## 5. Hidden lifecycle, relationship and dialogue

**Verified:** base ScriptHide `0x100a8710` saves callback +0xe4 and solid/move/effects, installs
NULL think/FLT_MAX, relinks/transmits; ScriptUnhide `0x100a8990` only if hidden reinstalls saved
callback and sets NextThink=NOW, restores physical words and clears hidden. No floor teleport.
Troika hide `0x102c1ce0` cancels SCRIPT/live cine, forces0x6b except DEAD, then base hide and
weapon hide; damaged tail settled by assembly JMP weapon slot77. Unhide `0x102c1ec0` has four
getter dispatches before base, then slot614, weapon unhide, conditional six-word cine handback,
clear cine-hidden latch. Getters are move-type/move-collide/solid/solid-flags; no ground write.
Port uses saved deadline rather than NOW, and its Troika tail uses bHidden in place of the
separate cine latch, with stale zero-effects commentary. Correct only evidenced differences.

Ground startup belongs to StartNPC `0x10273ad0`: normal non-fly/non-swim, non-capability4,
non-spawnflag4 body sweeps full hull down256 units via `0x102e7880`, warns on miss and writes
result origin; other arm clears ground flag. Later off-ground selection goes through0x73/0x3e
and motor gravity. Hidden admission must still build/load the body and perform the lawful
activation; hidden callback stays parked after admission. Do not fix by a bespoke unhide snap.
N10 is already closed into red5; no new Animal selector recovery is owed.

Relationship `0x10273790` parses/writes entity/class priority rows; no immediate forced schedule.
QuerySeeEntity `0x102b38b0` checks sense-off/frenzy friend, player unconditional acceptance,
NPC D_HT/D_FR; Look `0x1030ff10` refreshes lists at its cadence then OnLooked;
LookForPlayers `0x1030fff0` runs live QuerySee through `0x1030ffa0` then stores its sight list.
PerformSensing `0x10310710` gates on +0x80, Look then Listen, no conversation test.
**Caller-chain correction to every scout:** base wrapper0x1026e4f0 additionally gates oblivious<1;
it is reached inside GatherConditions0x1026ec30. Troika RunAI0x1028fcc0 calls base
RunAI0x1026f110, which clears gathered then skips ordinary GatherConditions while a LIVE
m_hDialogPartner+0xfe8 resolves (also skips reduced passes). GetNewSchedule0x102814d0 may
still gather when a program needs reselection and gathered is false. “No gate in PerformSensing”
does NOT prove continuous enemy Look/Listen in a stable dialogue hold.
NPCThink0x10292de0 calls SetPlayerLOS at0x10293095 before RunAI; talking check is later.
SetPlayerLOS0x10291610 independently maintains PVS/LOS: state bit8/frenzy bit8 force true;
otherwise2s cache, distance<=512 Source units bypass, farther trace mask0x4091, PVS-loss false,
same-PVS loss keeps LOS while now-last-clear<8. PE floats0x10452dc4=2,0x10483aac=512,
0x1045597c=8. No dialog term. Upkeep0x102c1400 has no distance/LOS release test.
**Port read:** TickSight recomputes SeenByChannel into SeenThisPass; PerformSensing has its
live can-sense gate and no dialogue test. Existing relationship record is green at V4c.
Do not assert pre-flip player absence from Sighted: neutral players can be sighted in retail;
ban SEE_ENEMY/combat before hate, then measure next-look enemy admission. Dialogue witness
proves LOS upkeep during hold, ordinary enemy gather held with live partner/stable program,
then next eligible gather sees the new relationship after release. Reselection can lawfully
gather earlier; trace that caller. The live port already carries RunAi19DialogPartnerLive's skip.

## 6. Damage: values, arms and the existing port

Read `0x10265ed0` whole; assembly numeric comparisons and unseen-memory argument flow.
PainSound first, clear INCOVER; base apply; zero result exits. OnDamaged when last stamp !=now,
OnHalfHealth at health<=truncated max/2. No FL_NPC or null attacker returns1 without record;
nonplayer/non-NPC attacker mask0x2080 skips record/memory/conditions to sound tail.
Qualified attacker: cone AND visibility; BOTH arms write inflictor origin or self origin +
death-throw vector*64. Unseen, current enemy + unknown attacker + no SEE_ENEMY updates
**current enemy**, via GetEnemy slot167 at `0x1026612c`, not null. Else lookup again and pass
known attacker or null. Live enemy motor reset/last-known-facing follows both arms.
Then last-damage attacker, took-damage, virtual light/heavy, sum/time, repeated, sound.

Width-correct PE constants independently confirmed: float0 at0x104454c4, float20 at0x1044eb0c,
double1 at0x10449280, double0.3 at0x1047b868, float64 at0x10451acc. Base virtual thresholds
`0x10266630/0x10266660` are strict >0/>20. Elapsed>=1 replaces sum; otherwise adds;
sum>max*0.3 strictly raises repeated. Sound is type1, duration0.2 (immediate at0x1026634a).
Current BaseDamage2.cpp already carries this corrected memory arm and scalar logic. A
TakeDamage I/O scalar with absent attacker need not fill last-damage record; do not force it
to do so. Arena's existing damage_packet submits a real attacker and reaches the qualified arm.
Runtime sound-table values and discarded-convar poll remain named existing seams; do not
invent values to make this wave pass. Flinch's slot141 producer is0005.

## 7. Corpse callback and retained weapon state

**Verified:** `0x1032c0e0` static/no-ragdoll hidden original installs SUB_Remove +0.5;
ordinary body SUB_PVSRemove +10, burning SUB_Remove +10. `0x102696f0` re-arms +10 only
when a player passes cone/PVS/visibility, else removes. Pedestrian override0x103a38c0 NULL
think; later fade0x10265d72 →0x102695d0 →0x10269960 wins over earlier callback.
CBaseEntity callback +0x118 and saved callback+0xe4 are FUNCTION SAVE raw rows.
PE widths confirmed:0x104454d0 float0.5,0x1044e664 float10,0x1044fac0 double10,
0x10449198 double0.2; sentinel decode0x10482fac float1e10; hull-padding0x104454c0 float1.
Snapshot currently stores deadlines but not callback identity; dead NPC Think with missing
name returns forever. Persist the selected callback, including null/fade state, without
replaying death/weighted pick or installing a fresh ten-second timer at load.
V4d supplies visual handoff/release. Save logic observes entity at death spot; Chaos pose
reconstruction is presentation smoke, not a retail-save pose promise.

V5b retained single-round state confirmed from slot322 assembly `0x1025506f..77`: requires
owner PLAYER subobject +0xa8, so NPC returns retaining bInReload after TASK_RELOAD sets it
at0x1028918d. Raw weapon datamap saves +0x898/+0x899/+0x89a. Serialize all three without
calling FinishReload on load; keep +0x730/+0x734 raw FLOAT, idle time TIME.
Ordinary NPC event shots do not empty a clip; this persistence witness needs an explicit
Green Room fixture of WeaponFinishReload on a single-round NPC weapon, not fake-reload text.

## 8. N9 and coordinator limits — measurement, not guessed fixes

Maker `0x1034b580` gates: bypass; positive full live cap; global0x106c8a41; enabled player
NPC-clip/view-cone/truncated-distance; flat occupied box at maker Z. Assembly box
`0x1034b692..0x1034b737` uses X/Y±34 (PE float0x1049ffac), mask0x2080, max2.
Enumerator `0x101cc9e0` (vtable CFlaggedEntitiesEnum slot0) filters flags and appends, no
alive test. DeathNotice `0x1034bc90` latches once, output/refund then live decrement/clamp.
MakerThink `0x1034bbf0`: success or live-ceiling refusal => frequency; other refusal => random1..2.
Factory `0x1034b7b0` makes a new class instance; no dead-template gate is established.

**Not settled:** actual old maker_respawn refusal. Read record, scout and validator: no attempt-time
live count/global/occupancy snapshot. Static branches do not select an observed branch. Integrator
M1 logs gate/live/global/box and every candidate's flags/life/entity bounds on the first refused
post-death attempt. If retained corpse occupies it, correct record and triage, then remove corpse
via normal Kill and prove second spawn; never add an alive filter. If another arm is wrong,
fix only that demonstrated retail divergence in integrator-owned Maker/Map geometry sites.

Coordinator constructors `0x1025d880` allocate three cap2 lists; cleanup `0x1025d940` frees them.
`0x1028d770` invokes constructor and `0x1028d800` cleanup. CWorld Precache0x1023c020 calls
empty0x1028d7e0, NOT constructor. Corpus callers of constructor/cleanup (including thunks)
do not establish exactly when same-map retail load resets the lists. No coordinator datamap.
**Unsettled M2:** same-map retail array lifetime across load. Do not serialize/rebuild membership
from guesses. Instrument port pre-decode/post-restore/first melee gather; if an accessible retail
debugger session can measure constructor/destructor/admission at the same-map load, record it.
Without that retail measurement retain existing fresh-world empty lists as an explicitly
inferred seam, and do not claim exact list equivalence. Arena guards capacity/idempotency on
the restored world and records the empty-list policy, not fabricated saved membership.

## 9. Entry surfaces and harness — no UI required

Verified port RequestSave → BuildPayload → storage and Load → ApplyPayload → Maps::Travel
exist. The live source also already publishes operation-id Capturing/Writing/Written/Failed
through PublishSaveResult/OnSaveResult; reuse this, adding exact capture/load fences only.
Command bus elysium.cmd dispatches GameFlow save/load; console step and MCP console
both reach GEngine::Exec. The scout's broad “no reachable save/load” interpretation is false.
Direct elysium.save exists; direct elysium.load is absent. Add the direct alias, not a new backend.
Map debug surface is elysium.map / elysium.reload; map_load is brief vocabulary, not an existing
literal command. Define fresh-load separately from normal Travel so revisit snapshots survive.

No completion-aware Arena save/load action, checkpoint equality, or runner world-rebind exists.
Runner currently aborts when its installed world disappears. Map run owner is GI-scoped
FElysiumArenaRun through MapSubsystem, which can keep a transaction across OpenLevel; its
FHost/trace bindings must rebind. Green Room worlds use stage-only custom defs with no ordinary
baked-map payload identity, so the production Load baked-map gate cannot simply be skipped.
Lane3 must implement an explicit arena checkpoint transport using normal codec/storage and
common snapshot applier plus saved stage defs/network/seat; map records use actual public Load.
Keep harness provenance outside released gameplay saves. This is harness staging, not a named
gameplay divergence. No Python multi-boot orchestration is necessary with that GI owner.

M3: measure CanSave gates, exact capture fence, activation latency and runner survival on both
hosts. Fail refused/timeout operations explicitly; a console trace alone never proves save/load.
The ordinary map record proves elysium.load alias independently of arena checkpoint transport.

## Lead verdicts

| lead | verdict |
|---|---|
| scout-V6 packet | cursor correction confirmed; clock uncertainty settled; no-save reachability refuted; N14 wording/writer corrected; no floor teleport; restore/callback gaps confirmed; continuous dialogue sensing inference REFUTED by RunAI caller gate |
| val-V6-clock packet | cursor/console correction confirmed; its clock limit superseded by engine listing; valid-cursor retention remains explicitly inferred |
| val-V6-corpse packet | callback/save flags and N9 limit confirmed; N14 overclaim refuted, full row deficiency confirmed; memory argument now verified as GetEnemy; constants settled by PE |
| sol-V6-clock packet | map1/frozen-return/saved-load and TIME base verified; startup-ready1.2 remains inference; raw weapon/layer FLOAT exception added; last-enemy alias unproved |
| new-V6-saveload packet | existing backends and missing completion harness confirmed; command dispatch question settled yes; Green Room identity/rebind work included |
| sol-ghidra packet §§3–5 | damage constants/memory, place helper (no cine), corpse-permitting occupancy confirmed; actual N9 scene remains M1 |
| V4c zero-clock lead | reset-before-Load correct; zero value superseded by engine1.0; integration owns resulting record changes |
| V5b retained reload / V4d corpse-save handoffs | both accepted: bytes/callback/deadlines restored; no new reload continuation or Chaos serialization promised |

Additional raw BSP lump0 reads from the patch maps confirmed tutorial thug_3 is StartHidden1,
HumanCombatant, spawnflags4, thirtyeight/knife; Hunter1 has hunter1.dlg; tutorial's authored
sSabbat_distracted targets thug_1 with knockback_normal_high_back and move-to1. Hub
pedestrian_female authors use_interesting1/groups1 31; bus_stop is Idle/max_npcs4. These are
staging facts, not proof of baked availability. The Green Room's RebuildStageWorld deliberately
leaves Defs.MapName empty; its production payload cannot identify that stage as a baked map.

No further retail reading assignment is hidden in a coder lane. M1/M2/M3 and concrete asset
availability/timing are measurements. New behaviour discovered beyond this packet is reported
with address; it is not adopted as an unnamed modernization.
