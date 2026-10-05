# V7 settling packet — inputs and their consumers

Planner read on 2026-10-05. All retail addresses below are **vampire.dll**, image base
0x10000000. VERIFIED means a corpus decompilation/listing or pinned PE read made in this
planning session; INFERRED means a port mapping or authored reachability conclusion.
No runtime measurement, source edit, build, test, arena, bake or commit was performed.
Read AGENTS.md first. `uv run elysium research kernel --check` returned 7/7 current
before the ledger lookups. Main HEAD was eb6d9f94 (documentation after V4c d0f79574);
its working tree contains V4d edits. Function names, not historical line numbers, locate sites.

## 1. Verdicts on the supplied leads

| lead | verdict after this session's reads |
|---|---|
| scout's “19 unregistered” | STALE count. The named 19 remain the acceptance set; Faint is already registered in NpcClasses::BuildNpcClass by d0f79574. Add only missing names, never a second Faint thunk. The generated 33-name inventory is not a registration implementation. |
| N5 and V4c Bool staging | VERIFIED remaining defect. d0f79574:Arena/scenarios/world/input_disablethink.json still stages STRING "1"/"0" and retains known_red. V4c's damage_knockout_one_hit / damage_cower_one_hit / damage_high_health_control use Bool true. Those corrections do not implement wire conversion. Npc::InputDisableThink still accepts only Bool; EntityWorld::DeliverInputTo still copies raw Event.Param. |
| N6 | VERIFIED store + choose-new, not immediate install. Additional correction: first character '-' skips the store, preserving an existing forced word; it does NOT clear it. Invalid/empty names can store -1; every call still ORs 0x82000000. |
| N11 | VERIFIED ConVar and croucher bypass omissions. Scout's claim that the disturbed producer is absent is REFUTED: GhoulCroucher::Spawn/GatherConditions/OnDisturbed/IsDisturbed already carry it. |
| movement multiplier | VERIFIED two scalar writes; “below minimum resets” is imprecise: shared setter resets only ordered negative values, at threshold 0.0, not 0.001. The input separately floors its low arm to 0.001. Ground-speed multiplication is proved in assembly, not inferred from the field reader index. |
| no witness-map firing for all 19 | REFUTED by SetFollowerBoss. Literal input-name absence in both BSP lumps is verified but insufficient: hub dialogue and resetHos call it through Python. |
| sol-V7 scripts classification | VERIFIED method-call hits and hub wires, INFERRED absence of other reachable witness paths. No live execution proves the dialogue conditions. Four names have other-map script hits; twelve have no method-call hit; two investigate calls are dormant in the searched authored corpus. |
| scout's 10d must wait for V10 | REFUTED for this wave's mirror proof. The seven cached sound records can be staged in a debug fixture and the real CommitBestSound called. V10 owns sound-life/expiry, not these copies. |
| scout's DontFace/FallToGround consumers absent | REFUTED. NpcDialogue::StartTalking reads spawnflags bit 8; NpcThink::Think19NormalSet2 reads DONT_FALL_TO_GROUND. Retail's latter callee is empty. A further DATAMAP/HANDLER mismatch for DontFace is verified below. |
| “one-line” scope of 12a | UNDERSTATED. Full comparison found friendly-sound suppression and combat proximity arms missing too. All are this predicate's behavior; do not repair just StayEntrenched. |
| 10i | runtime branch already correct (local 0x12f, weight 20, bypass FloatSound); comments and arm fixture are stale. |
| 25a | dead NPC latch is verified by whole-Source search: declaration, implementation and generic runner reads, no producer. Delete the latch mechanism; retain direct ClearSchedule callers and their timing. |
| 16b / 21c | VERIFIED flat-table gameplay readers and disposition/schedule auto-accept substitute. Stealth HUD counterpart is not established; it is presentation and requires separate treatment below. |

## 2. Typed delivery: the declared type comes before the handler

Read CBaseEntity::AcceptInput **0x100abc90** whole, VariantConvert **0x100d05d0** whole,
VariantToString **0x100d0a00** plus assembly. AcceptInput walks the class datamap case-insensitively,
records activator/caller, converts mismatched types, invokes only on conversion success.
Failure reports a bad link and returns false; it does not call the handler with a fabricated value.
FIELD_VOID clears its payload; FIELD_INPUT (18) accepts without conversion.
For V7's scalar destinations the verified matrix is:

| source | Bool 5 | Int 4 | Float 1 | String 2 |
|---|---|---|---|---|
| same type | unchanged | unchanged | unchanged | unchanged |
| String | atoi != 0; null false | atoi; null 0 | atof; null 0 | unchanged |
| Int | !=0 | unchanged | float cast | fails |
| Float | !=0 | __ftol toward zero | unchanged | fails |
| Bool | unchanged | fails | fails | fails |
| Void | fails | 0 | 0 | null string |
| Vector / Handle | fails | fails | fails | Handle→String returns success **without changing type/payload**; vector fails |

Do not change Python truthiness/FElysiumVariant::ToBool. A successful but still-Handle
String conversion reaches the string handler's VariantToString fallback. Bool stringification
in that FALLBACK is "true"/"false", float is "%g", vector is "[%g %g %g]", invalid handle
"(null entity)"; default "No conversion to string". This is distinct from the conversion matrix.

The replay `research/ghidra/types/datamap_records-vampire.dll.json` was read for the 33 inputs.
The builder **0x1028cd70** finishes by pointing the datamap at **0x105ce4b4**, count 0xfa.
Pinned PE row **0x105d0d18 = 0x105ce4b4 + 235*0x2c** contains type **2**, name pointer
0x105d5780 InputSetDontFacePlayerInDialog, external pointer 0x105d5760,
flags INPUT and thunk 0x1000cd01. The corpus did not index that thunk; an in-memory PE read
found bytes e9aa672b00, E9 jump to0x102c34b0. Its handler assembly **0x102c34b4 CMP …,5**
expects Bool. Thus ordinary String "1" CLEARs bit 8; Bool true cannot convert to String
and never invokes it. Preserve this retail bug. A raw-handler Bool arm may set bit 8;
it must not be described as the wire behavior. No pipeline/regenerator change is needed:
manual registration can carry an optional declared type, with class lookup resolving thunk
and type from the SAME descriptor. Limit newly applied metadata to this acceptance set,
DisableThink and ChangeSchedule; do not silently coerce unrelated untyped inputs.

## 3. The 19 input contracts

Every row's handler was read with vtmb_code; scalar/branch disputes additionally used vtmb_asm.
Types are the retail input datamap types, not what a handler happens to test.
All names below need a record; the plan maps each to one.

| name | declared type; retail address | verified behavior / existing port site |
|---|---|---|
| AllowAlertLookaround | Bool; 0x102c2b90 | Bool byte to +0x6434, other raw-handler type false; Npc::bAllowAlertLookaround, selector reader exists. |
| AllowKickHintUse | Bool; 0x102c2c10 | Bool byte +0x6436; ScheduleHost.AllowKickHintUse and existing hint/kick readers. |
| AllowOpenDoors | Bool; 0x102c3540 | true CapabilitiesAdd(0xd00), otherwise CapabilitiesRemove(0xd00); preserve other bits. |
| Faint | Void; 0x1029f250 | slot614 reset → cause/source +0x1b30/+0x1b34 line0x26c2 → SetSchedule(0xfa,false). Existing BuildNpcClass thunk does this. |
| FleeAndDie | Void; 0x1029f210 | same order, source line0x26b5, schedule0x6f,false. |
| MakeInvincible | Bool; 0x102c2a30 | byte +0x63d8; existing damage/stealth guards read it. |
| SetBloodShieldDiscipline | Bool; 0x102c32d0 | resolve Thaumaturgy_Bloodshield via0x101e1590; true0x101e3380(self,self,id); false0x101e1870 sets high id bit then RemoveEffect(false). See judge dependency, not generic Use. |
| SetBossMonster | Bool; 0x102c3500 | byte +0x6496; UpdateCharacterRetail's registry reader exists. |
| SetDefaultDialogCamera | String; 0x102c2910 | string/null/fallback → Q_trimspace buffer260 → only nonempty intern/write +0x64c4. Empty/whitespace preserves previous camera. |
| SetDontFacePlayerInDialog | **String**; 0x102c34b0 | handler Bool true OR8, every other raw type AND~8; ordinary wire behavior as §2. StartTalking0x102c0270 reads bit3 and skips slot306 when set. Reader already exists. |
| SetFallToGround | Bool; 0x102c3470 | true AND0x7fffbfff, else OR0x80004000 in flags2. Think0x10293311 tests bit0x4000 before0x102bfdf0, which is RET4/empty. Do not invent a fall mover. |
| SetFollowerBoss | String; 0x102c3350 | call SetFollowerBossName0x102c4430 → SetFollowerBoss0x102c44e0 THEN write +0x6478 name even on refused resolve. Existing Werewolf2 bodies. |
| SetFollowerType | String; 0x102c33a0 | SetFollowerType0x102c4640 → loader/clamp0x102c4680 → name+0x6480. Existing Squad body lacks rule row read. |
| SetInvestigateMode | Int; 0x102c2ab0 | valid0..6 writes+0x6338; outside/raw wrong type logs and preserves. |
| SetInvestigateModeCombat | Int; 0x102c2b20 | same, +0x633c. |
| SetMovementMultiplier | Float; 0x102c3580 | exactly ±1 → trail0, setter(-1); ordered >1 → trail3, setter(value); low arm trail0, setter(max(value,.001)); raw wrong type treated0. Assembly also sends NaN through trail3 unchanged. |
| SetSpeechVolume | Float; 0x102c2680 | clamp[0,1], raw wrong type0, write+0x6550; only talking+0x64c0 calls engine sound update, channel5, flags4, pitch100 (0x102c278d..27a2). Do not restart a voice/timer/dialogue. |
| StayEntrenched | Bool; 0x102c2bd0 | byte+0x6435; ShouldInvestigate early refusal. |
| WalkToNode | String; 0x1029e840 | DEAD7 no-op; buffer256 (255 payload bytes), strtok delimiters **comma and space only**, first two tokens, further tokens ignored. Missing tokens/node logs, no path mutation. Resolve schedule0x102c47e0 then node0x102d2900; success BuildPatrolPath0x1029f460(one node,-1 terminator,repeat0,type0,Replace=true). |

SetSchedule chain **0x102ae750 → 0x102ae780**: local→global, reject current/ideal DEAD,
require IsAlive unless forced, then ForceScheduleChange and SetSchedule. Preserve this chain in
Faint/Flee, including refusals; do not set dead actors' schedules directly.
BuildPatrolPath **0x1029f460** was read whole: replace clears its list, sets repeat/type,
appends nodes, rewinds; only nonzero supplied schedule overwrites the path schedule, and a
nonzero retained path schedule clears/installs via the existing helpers. The adapter must
call that body, not reimplement a generic patrol command with different tokens.

Follower resolution **0x102c44e0** uses slot559 FindNamedEntity, writes handle, rejects null/self,
rejects connected squad (retail fatal Error; port has an EXISTING named log/refusal divergence),
then ResetAiState(false,false) **0x102b52a0** before flags+0x5b84 OR0x3008.
ResetAiState clears enemy/target, memory mask0xf7fc7fff and navigator; optional false arms
do not clear all conditions/change ideal state. Do not simplify setter to handle storage.
Follower type loader **0x101e8c90** case-folds authored rows, missing/empty falls back to FIRST
row, then **0x102c4680** clamps walk≥back+10, run≥walk+10. PE constant0x1044e664=10.
Deployed rules.txt already has Default64/100/150, Combat64/100/150,
CombatNonCombatant384/500/550. FElysiumRules already parses nested blocks at runtime;
retain child order there and read these values through it. No content pipeline or bake.

Bloodshield true was followed to **0x101e3380 → 0x101e3560 then 0x101e3730**:
status gate/AddDiscFlag, modifiers/misc/possible COMBAT sound; target-effect replacement,
tier evaluation, hitgroup apply and timed/direct removal. The existing
ElysiumDisciplines::Use has learned/cost/eligibility gates absent from this input.
EndBloodshield alone is not the false RemoveEffect path. This service boundary is the judge's,
not a V7 discipline lane; registration may expose a named unavailable hook only.

## 4. N5, N6, N11 and shared movement

N5: **0x1029f2a0** handler passes Bool else false to **0x1029f300**. Only true→false
runs slot614 reset BEFORE storing false; repeated false and true do not reset. Disabled
NPCThink **0x10292de0** exits before task execution. Keep string-wire and native-Bool records distinct.

N6: **0x102c33f0**, assembly0x102c3418 checks FIRST byte0x2d; other names
resolve **0x102c47e0** (raw, then SCHED_ prefix, then global→local; failure -1),
store **0x102ae7f0** at+0x65c8, always flags2 OR0x82000000.
**0x102ae920** resets InvestigateSound before checking forced!=0; clears forced0 then returns it
ahead of normal selection. Port ElysiumNpcSelect.cpp::FElysiumNpc::PreSelectSchedule already
consumes this word, whose
port carrier is a registered/global id; convert valid local ids through the existing id space,
keep retail 0 and -1 sentinels. Do not call StartScheduleId in ChangeSchedule.
NPC StartSchedule is an existing K1 extra alias without an authored NPC caller; keep its
immediate semantics and describe it, rather than asserting an unverified retail NPC input.

N11 **0x102c2300**, assembly read whole: hidden → dialog pointer(+0x128) → invincible →
ConVar branch → HEAR_PLAYER0x6f → SEE_PLAYER0x5a → IsAlive slot158.
ConVar command/non-int arm is treated0; nonzero permits any state except DEAD7;
zero permits virtual GetNPCState values1/0xd. Constructor **0x1028c680** pushes default
string0x10539978, PE reads "1". Use existing live
ElysiumNpcTunables::ConVarInt(DebugAllowNonIdleAutoSk), not a baked constant.
DialogName is this port's +0x128 carrier; do not substitute bIsTalking/session ownership.

GhoulCroucher **0x1037bbc0** asks **0x1037bb20** (read+0x6666 only), returns TRUE immediately
when undisturbed, before ALL base guards including IsAlive. Disturbed delegates base.
Writers read: Spawn **0x1037b040** disturbed key+0x6664→both+0x6666/6667;
GatherConditions **0x1037b570** NEW_ENEMY→OnDisturbed(self) once;
**0x1037b6e0** once-latches, clears unaware-exited, distinguishes player output/closest-player,
adds D_HT priority10 to resolved closest player. Port already has these bodies.
Other slot294 species read: Rat **0x103ad680** always true; Payphone **0x101aa8d0**
always false. Keep real dynamic dispatch; do not put a class-name switch in the verb.

Constructor **0x1008b230**, assembly push1 at0x1008b29d and call0x1008b319
through thunk0x1000fae7, establishes GroundSpeedScalar1.
Shared scalar **0x1008d230** calls SetGroundSpeedScalar **0x1008d0f0** THEN writes
playback+0x6f4. Ordered negative resets both1; zero/nonnegative store argument; NaN stores.
GetGroundSpeedScalar **0x1008d190** returns+0x564. The corpus field-reader index misses
its assembly-only FMULs: **0x10091595** normal sequence speed = move distance*scalar/duration;
**0x100915f6** interval branch also multiplies scalar. StudioFrameAdvance
**0x1008f2fa/0x1008f306** writes that result+0x654. Thus multiply GroundSpeed once in
WriteSequenceSpeedWords; playback independently advances cycle/events/interval movement.
ResetSequenceInfo **0x10090950 / 0x10090a23** resets playback to1 but NOT ground scalar.
Keep kernel words authoritative; the existing SetBodySequencePlaybackRate embodiment
door propagates changes without restart. No motor speed multiplication a second time.

## 5. Adjacent old items, fully compared

**25a, ClearSchedule0x10280d30:** stores 0 at+5c48,+5c4c,+5c44,+5c3c,+5c38,+5c40 in
that order, then Troika flags1 AND~8, slot435 with already-zero schedule.
Existing direct door is ElysiumSchedule::ClearSchedule / NpcBase::ClearSchedule.
Delete RequestClearSchedule/TakeClearScheduleRequest/bClearScheduleRequested plus generic
runner's polling mechanism and obsolete synthetic latch tests. Retain direct script clear and
StartTask same-loop versus RunTask next-think behavior. Read MaintainSchedule0x102817c0:
slot442 StartTask at0x10281e10, then status0/4 skips RunTask at0x10281f26..37 and may
loop0x102821a8 to0x1028193b; slot444 RunTask at0x1028202c then status!=4 exits
at0x1028212e..37. ClearSchedule writes status0, hence that timing distinction.
Also read reset6140x102c23f0: +0x17c,+0x6244/48/4c/50 all stamp curtime.
GetLocalScheduleId0x101a6620→0x102ea280 walks inclusive ranges, skips local-base9999,
preserves -1 on miss; the comfort record must use a real registered schedule.
The archived old25a cut names dead
0x10084260/0x102c6ff0 and later camera/task work; don't reassign those to V7.

**10i0x1027a420:** five refusals (state1/3, gag2, BCCTargetable+0x1480, dialog partner,
busy discipline) → schedule local0x12f gives weight20 and SKIPS FloatSound → other programs
call slot510, true invokes507 then returns false → otherwise inclusive RandomInt(0,weight)==0.
Do not turn “weight20” into an unconditional success. Port runtime has local0x12f;
remove obsolete “all subspaces9999, Local=-1” comment/fixture. BCCTargetable source remains
an existing unrecovered seam: add/name its accessor, no fabricated always-true fact.

**12a0x102b3270**, plus helper **0x102b8cd0** (relation neither D_HT1 nor D_FR2):
flags1 mask0x4000080 → StayEntrenched → null → resolved follower boss → current
mutable enemy(slot168) true → friendly-source branch → modes.
Friendly-source branch uses InvestigateSound's TYPE at+0x60e0:
nonzero: false when !combat or type==1/0x10, else modes;
zero+combat: candidate's CONST enemy(slot167) nonnull and !=me, hated by me,
admits if candidate OR its enemy is within XY distance²<=65536 Source² and absZ<=80.
Assembly0x102b337a/337f and0x102b33ad/33b0 takes EQUALITY; use 256*2.54 cm and80*2.54 cm.
Then modes0 never;1 hated players;2 nonneutral players;3 any player;4 hated;
5 nonneutral;6 anything; unknown logs false. Both mode terms and proximity relations
dispatch slot404. No source is missing: committed sound record, virtual enemies and origins exist.
Add nullable overload if needed, keep reference callers forwarding.

**10d0x102b4090:** first matching condition wins in order
0x6d(COMBAT,+6160),0x70(BULLET_IMPACT,+618c),0x72(FLINCH,+6210),
0x6f(PLAYER,+61b8),0x6a(DANGER,+6108),0x71(PHYSICS_DANGER,+6134),
0x6e(WORLD,+61e4). Copy into BestSound+60b0; NO matching condition retains old BestSound.
Unconditional tail copies source to+5b78, then full record into InvestigateSound+60dc.
Copy helper **0x102b3d90** writes+0,4,8,c,10,14 byte,18,1c,20,24,28.
Port ladder is already correct; its early return omits mirror/tail on no-match.
Copy the complete FElysiumGameSoundEvent representation, not only Source/Position.
Synthetic cached records prove this without CSound expiry. Production selector caller already exists.

**16b0x10299da0** slot404 composition already landed:
self/null0; candidate insane with closest player → virtual relation/enemy;
candidate boss; own boss → own boss target LIKE3, hate/enemy1, candidate NPC's reverse
relation answer; base table tail. Keep species virtual overrides and const167/mutable168
asymmetry. Route gameplay flat reads in Conditions::ShouldInvestigate/GatherSight/GatherSounds
and FeedSchedules::BeginPostFeedTrance through IRelationTypeOf.
Post-feed guard verified in **0x1033a9e0**: victim NPC virtual404 toward feeder,
!=1 then reset614, source stamp0x24c6, SetSchedule(0xfb,false).
Stealth::PublishObservers is an Unreal HUD projection, no retail counterpart established;
using composed relation there is an INFERRED consistency change of the existing presentation
modernization, not a claim to have recovered a retail HUD reader.
Diagnostic ResolvePriority/base-table tails remain direct. Retail blindly reads player
candidate+0x647c in arm B: what that unrelated word holds remains unrecovered.

**21c0x10168910:** victim ideal activity+0xff0, exactly0x104e,0x1068,0x1069,0x1098
automatically succeeds BEFORE ResistsFeeding/opposed roll; other values take existing
resistance/roll/stealth fallback. This is ideal, not current activity, schedule or disposition.
Non-NPC activity accessor must represent the same CBaseCombatCharacter word, answering
nothing where its substrate is absent, not assuming a player cannot be a victim.
Preserve the rest of AttemptFeed and V4c's paired transaction/event chain.

## 6. Witness provenance

Independently searched all 41 deployed .py files for the 19 method names and read both
patch-first BSP entity lumps in memory (offset/length from byte8). No literal19 names in
sm_hub_1 or sp_tutorial_1. Read vamputil.py4390/4398/4421 and prostitute.dlg52/82.

Hub: prostitute_1 dialogue binding at lump19797 (other prostitutes29482/32909);
dialogue52 calls makeFollower→vamputil4398 SetFollowerBoss("!player").
Dialogue82 resetHos→4421 SetFollowerBoss(""); disbandFeed4390 similarly.
OnDeath resetHos at19799/29484/32911; 21 OnChangeLevel resetHos wires, e.g17575.
Retail Python bridge **0x10195510** returns PyCFunction for input descriptor;
**0x101962a0 assembly0x10196434..3c** sends descriptor name via AcceptInput slot118.
The hub record must exercise an authored script/dialogue chain, not relabel synthetic fire
as map provenance. Conditions/money/setup are measurement steps.

Other-map-only script names: Faint(chinatown723), FleeAndDie(chinatown307,
giovanni268..280, warehouse68..107), MakeInvincible(hollywood762,
santamonica1052), SetDefaultDialogCamera(ventrue40/43). Scout's concrete
other-map wires were not all independently rewalked; label that part INFERRED from sol packet.
Dormant: SetInvestigateMode/Combat(vamputil3290/3291), helpers AnimalFriendship/AnimalRadar.
Twelve others no active method-call hit. Absence is bounded corpus evidence, not proof
against native/console calls. These18 are Green Room records, stage "arena".
Existing N5/N6/N11 are synthetic Green Room witnesses even where they copy retail map rows.

## 7. Remaining measurements and judge boundary

- No live hub dialogue path, response index, affordable player money/quest condition, or resetHos
  path was measured. Integrator inspects the authored choices and performs them on the fresh map,
  records exact dialogue rows/commands and resulting input trace; never skips to a fake map verdict.
- No actual NPC speech stream's owner/session resolution and gain change was measured. Read
  InputSetSpeechVolume assembly (channel5/flags4), LineService Active/session routes,
  Entity::PlayDialogFile, WorldDialogue CurrentVoice and IElysiumAudio::SetVoiceVolume/SetGain.
  Integrator measures same handle/cursor/end deadline at gain0/.5/1, decode-pending and silent
  cases. The listing settles no audible Unreal outcome; no new code policy is justified by that.
- No multiplier ratio/cycle/animation-event phase was measured on a cooked moving NPC. Listing
  settles the writes and multiplication; integrator measures unchanged sequence identity and
  scaled GroundSpeed, cycle slope, motion and event count with a fixed nonresetting sequence.
- The player's unrelated+0x647c word in slot404 remains unrecovered: code0x10299da0 and
  matching oracle explicitly expose the blind read; no live retail instance was inspected.
  Preserve/name the existing absent accessor and measure retail before extending player behavior.
- Bloodshield's status/target service cannot be equated to generic Use from the listing/port.
  The required behavior is recovered, but port service ownership is a judge decision.
  Binding availability is NOT a green effect verdict. See README For the judge.
- BCCTargetable's native writers/default were not recovered here; code0x1027a420 shows the gate,
  port names no producer. Preserve explicit input seam and isolate the comfort-weight test.
- No headless Ghidra needed. Read driver README; used corpus listing and PE reads in memory.
  The first PE datamap-header read found zero runtime-initialized table/count; builder0x1028cd70
  supplied table0x105ce4b4, whose static row settled the mismatch. No scratch project or PE dump
  was written. Module::address spelling was rejected by tools; bare queries were checked for
  vampire.dll sections (100d05d0/10168910 also returned client.dll, ignored).
