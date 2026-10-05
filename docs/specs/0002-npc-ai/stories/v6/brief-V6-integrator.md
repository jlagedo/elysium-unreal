# V6 integrator — clocks, restoration and executable witnesses

Read main AGENTS.md and all five V6 planning files. V4d/V5b must be landed first. Coordinator
names the integration branch/worktree and each coder's worktree. The planner wrote docs only;
do not mistake its source observations or V4c acceptance for the installed binary's baseline.

## Compile first, then measurement and acceptance

1. Reconcile incoming V4d/V5b changes and the three disjoint coder reports. Apply core interfaces
   before dependent calls. First activity after integrating source is a compile pass: up to
   **six builds, budget about two minutes each**, `uv run elysium build --arm` when new arm units
   require it. Inspect C4458/C4459, include/declaration/duplicate-body/link seams before each.
   Wait for real completion; log wall time and first error. No tests/arena during this pass.
   If six are exhausted with failure, report exact unresolved compile site; do not claim acceptance.
2. After successful compile, **`uv run elysium test`**. Record actual default totals and report path.
   Then run M1/M2/M3 and the wave's records **by name**, looping freely to settle staging and fix
   the demonstrated retail divergence. Every new record runs alone AND after another record in
   the same boot. Do not reseed/reset a resumed checkpoint or widen regex/deadline to hide a miss.
3. After named records, **`uv run elysium test arm`**.
4. **`uv run elysium research kernel --check`**. If stale, amend owed verdicts, regenerate with
   `uv run elysium research kernel`, check again. Compile-impacting generation returns to build
   and affected validation; final default/arm/check must describe final sources.
5. **Full arena once**, `uv run elysium arena`, after last source/build/record change. Every moved
   verdict has before/after, first_unmet, trace and retail reason. A subsequent change invalidates
   this final run; explain and renew final validation rather than cite an obsolete run.
6. Update recovery/triage/spec/tracker only with demonstrated acceptance. One commit, explicit
   paths only, never `git add -A`/`.`; verdict table in message, no push. No report*.md files.

Runtime fixes consume existing assets; no content import/re-bake/pipeline lane. Judge items are
README § For the judge. The inherited Recast and V4d Chaos modernizations stay named; no new
event/state-order modernization is authorized. M2's list-lifetime inference stays explicit.

## Integrator-owned seam work

These are serial integration jobs outside the coder manifests, with retail address and port site:

1. **Presentation continuation adapter** —0x1008df10/0x1008f120/0x10091880/0x10098c80;
   `Public/ElysiumWorldServices.h::IElysiumEmbodiment`, `Public/ElysiumMapActor.h`,
   `Private/Map/ElysiumMapActorEmbodiment.cpp` body animation adapter,
   `Private/Visual/ElysiumEntityBodies.{h,cpp}` clip/layer driver. Consume lane2's exact native
   phase record; bind and seek existing clip/base/four layers without event dispatch, weighted
   pick, activity change or cycle reset. Restore live motor goal/velocity through the existing
   navigator service; never copy UE path pointers. These files overlap V4d; preserve its rig,
   handoff/capability/terminal ReleaseNpcVisual. Needed declarations in another body/animation
   driver file are exact report-owed edits, staged explicitly; do not rewrite the visual system.
2. **Maker admission tap and conditional repair** —0x1034b580/0x101cc9e0/0x1034bc90;
   `Private/Substrate/ElysiumNpcMaker.cpp::CanMakeNPC/MakerThink/DeathNotice`,
   `Private/Map/ElysiumMapActor.cpp::IsNpcMakerSpawnAreaOccupied`,
   `Private/Map/ElysiumNpcMakerGeometry.h`. Emit attempt-time gate/live/global/box/candidate
   snapshot synchronously, behind arena trace, every post-death attempt. Include stable index,
   flags, alive/life state, logical origin/bounds, not only drawn Chaos position. No alive filter,
   expanded box, count tweak or template-state heuristic. Fix a branch only after M1 identifies
   a difference from packets §8; if corpse occupation is retail, change record/triage only.
3. **Read-only restore/damage taps** —0x1027bf50/0x10299a80/0x10265ed0;
   existing AI trace sink and lane-owned lifecycle functions via coder reports. Ensure restore
   comparison and branch traces occur before next think, with old/new epoch but stable names.
   If a damage mismatch is demonstrated, `ElysiumNpcBaseDamage2.cpp::OnTakeDamage_Alive` /
   `.inl` are integrator-owned for the exact retail correction. The current body already has
   correct memory arguments, thresholds and order; do not patch it just to produce a trace.
4. **Existing fixtures/declarations and verdict generation** — affected function's addresses;
   existing save/session/kernel/weapon/lifecycle tests and `ElysiumTestServices.h`. Retain V5b
   bytes/deadline writers; fix outdated absolute-time/cursor/zero-clock assertions to raw types
   and1.0, with reason per changed test. Add `research/tooling/ghidra/driver/kernel_verdicts.tsv`
   rows for each changed hand body (targets name *SlotBodies.cpp if needed); generated *Slots.cpp
   is never hand-edited. Review runtime bind signatures before regeneration.
5. **Oracle and scope correction** — addresses in packets;
   `docs/vtmb/game_runtime.md` map-clock chain; `savegame_format.md` TIME/FLOAT/functions/phase;
   `npc-ai/lifecycle.md` hidden/callback/N9/N14; `npc-ai/schedule-kernel.md` cursor/failure/path;
   `npc-ai/programs.md` marker writer/resume; `npc-ai/conditions-and-states.md` damage;
   `combat-and-damage.md` retained reload/corpse-save. Amend spec0002 V6/C4+0x5c40 wording,
   triage N14 AddMarker writer and N9 measurement verdict, TRACKER V6 after acceptance, and
   matching `stories/v1/divergences.md` K1/restart rows. Preserve incoming oracle edits.

## Harness contract and measurements

Lane3 supplies final JSON spellings; the semantic recipes below are not pretend-ready JSON.
Transactions emit explicit captured/written/applied/ready labels and preserve ordered expectations
and `never` counters through world changes. Dedicated `arena_v6_*` slots, never user's latest.
Do not delete slots. Ordinary map host uses RequestSave and direct elysium.load backend;
Green Room uses documented nonshipping envelope plus normal storage/codec/common applier.

Preflight M3 after compile/default: confirm live OnSaveResult already exists, subscription observes
the right operation id, inline failure cannot be overwritten by Writing, normal load restores
RNG/player/current/visited snapshots, and blocked/missing-slot self-tests fail as designed.
Assert capture1.0/pre-entity at fresh boundary separately from ready-time settling. Record
pre/post first-think curtime, draw position and native sequence; V4c's former zero boot is not
retail evidence. Preserve NONE establishment trace/state-ban rule.

Snapshot compare at applied-before-think: saved values exact, TIME restored as saved delta plus
destination base, handles compare stable id/name, raw epoch/pointers excluded. Ordinary same-map
load restores saved base so semantic TIME values match exactly. Numeric tolerance only for known
float↔double conversion. A route/clip/event proof follows the comparison; equality alone is not
resume. Troika shoot-at reroll is intentionally excluded and measured separately.

M1 first: run maker_refusal_measurement, save attempt trace and candidate snapshot, compare against
maker_respawn's baseline. A corpse in box at live0 closes N9 as record correction. Keep corpse
until witness logs refusal, then normal Kill to clear it and observe next1..2 retry/spawn. If no
refusal now, log success/gates and restate the obsolete red with evidence. “Live0” is never proof
that spawn must succeed; all earlier gate arms remain real.

M2: record port coordinator lists at pre-decode, applied fence and first melee gather. If an
available retail debugger session can bracket0x1028d770/0x1028d800/0x1025d880/0x1025d940 and
admission on same-map load, obtain that measurement. Corpus direct/virtual/thunk searches and
CWorld Precache did not locate exact lifecycle. Without retail observation, retain fresh-world
empty-list policy as **inferred**, with capacity guard record, and report this limit explicitly.
Do not serialize guessed membership or demand “same members” in an expectation.

## Record recipes — stage, staging, expect and never

All map records are `shares_map:true` only with an explicit first fresh_map/reset action and
rebinding. Coordinates in final JSON are Unreal centimetres, converted from authored Source
origins; never paste Source inches as cm. Record duration/seed starts with donor value, then
windows are measured from labels and justified by retail. Use survival `events_player`/existing
MakePlayerUnkillable staging wherever real shots require a live enemy. Read donor event names,
not guessed strings. Existing after/within/at_most are sufficient for replay bans.

### Clock, fresh-world and real command records

**session_map_load_fresh_world — map:sp_tutorial_1.** Fresh-load tutorial through new explicit
fresh API. Lane3's extended map spawn stages `v6_runtime` logic_relay and one-shot output into
another disposable relay; fire it once, record decremented output count, kill authored thug_3.
After nonzero elapsed, fresh-load SAME map. Expect new epoch, pre-entity clock1.0, runtime entity
absent, authored thug_3 restored hidden, output counter not carried, no old queued output.
Never old handle resolution or duplicate authored activation. `0x200f55f0/0x2008f120`,
`0x1011a7a0`; source `FreshLoad/Teardown/LoadMap`. This proves boundary, not just a console log.

**session_map_clock_revisit — map:sp_tutorial_1**, travel to sm_hub_1 and return in same process.
Capture tutorial T and a delayed relay/think remaining interval before travel; first hub's
pre-entity base1.0. Let hub advance well past the remaining interval. Return tutorial, compare
to its departing frozen T and remaining delay, then expect single delayed fire after its remaining
interval. Never firing due to away-time or inheriting hub time. engine0x2008f120/0x200975f0,
TIME0x101a0a80/0x101a2a30. The host keeps a single record/report across both maps.

**session_load_saved_clock — map:sp_tutorial_1.** Save after at least3 scenario seconds, wait
Written, let time advance another2, issue real `elysium.load arena_v6_clock` through console
while arming load fence first. Expect applied clock=T_saved, payload/player/globals/queue restore
comparison, then ready and continuation. Never success-on-enqueue, inherited later clock or
stale outgoing snapshot replacing payload. engine0x20096010/0x200975f0. This independently proves
direct public alias; ordinary save is not replaced by Green Room transport.

**session_time_rebase — arena.** A named different-base checkpoint fixture captures wait,
NextThink/queue, memory LastSeen, last-damage time, base LastEventCheck and layer/weapon raw
FLOATs at baseT; restore onto explicit baseT+100 through common applier, then compare expected
delta+newbase and sentinel values. Arm target wait through normal schedule, not just serialization
math. Expect remaining deadlines fire once; FLOAT attack/layer fields unchanged and named sentinel
unchanged. Never blanket shift, double rebase, overdue immediate duplicate. TIME0x101a0a80/
0x101a2a30, transition engine0x20097d00, raw datamap exception table. This controlled arm proves
the already supported carrier's conversion, not a new full cross-map entity importer.

### Cursor, cine and event continuation

**save_restore_mid_path — arena.** Unpark the existing cover/hint record into .json; correct
cursor+0x5c40, obsolete after/count gap note and known_red. It is a controlled hinted cover
program; the witness maps do not reliably select that hinted path under the same combat cast.
Use donor gunman/seat/seed/survival setup. Save immediately after TASK_WAIT_FOR_MOVEMENT
while goal live and cursor>0; wait write, load. Expect capture/applied equality of task/status,
wait/animation/hint/goal, then arrived→taskdone WAIT_FOR_MOVEMENT→SNAP_TO_HINT. Never schedule
install, get-path task, second hint+ or hint- between applied and arrival; never death/taskfail.
`0x1027bc60/0x1027bf50/0x102ee1e0/0x102f1dc0`. No “restart is close enough”.

**save_cine_possession_resume — map:sp_tutorial_1.** Authored sSabbat_distracted targets thug_1,
plays knockback_normal_high_back, move-to1, spawnflags4 (patch BSP lump0 read in packets).
Use BeginSequence after player safely staged out of hostile interference; capture after actual
possession/PLAY_SCRIPT and a positive interior cycle, before its finite clip completes. If too
short, capture on its native sequence-start fence before advancing; do not slow retail playback.
Expect accepted+Written save, saved cine/script state/task/native phase equal at apply, clip ends
and one OnEndSequence/release. Never K1 refusal, second OnBeginSequence after load, repeated
possession/task-start, premature cleanup or duplicate end. `0x101a7880/0x1027bf50/0x1008df10`.
This map reaches it; don't replace with an easier synthetic cine if an existing asset is missing
without sending exact asset evidence to judge. Also retain a mid-WAIT_FOR_SCRIPT arm in the
Green Room donor script_walk_to_mark via checkpoint in persistence_world_rebind.

**save_restore_move_shoot — arena.** cover_move_shoot donor: save at first active move/shoot
gesture after its first3031 while route still live and next-shot time future. Compare all four
layers, base phase/event cursor, move-shot count/deadline, weapon raw attack stamps and semantic
route at apply. Expect remaining route/next event-shot in its restored interval. Never replayed
3031/hit-set from before checkpoint, overlay re-add/weighted re-pick or early shot. Count actual
event sequence and shot stamps, not “at least one shot”. `0x10098c80/0x10091880/0x102e8aa0`,
`0x102e8560/0x102387b0`. Maps cannot guarantee an interior overlay/path capture; controlled donor.

**save_restore_invalid_schedule — arena.** Healthy finite control_sequence actor, valid nonzero
cursor snapshot; five isolated checkpoint copies/setup cycles for wrong CRC, missing cine in
SCRIPT, missing saved enemy, missing saved target, failed saved path. Narrow invalid-header
fixtures trace their setup and run normal restore. Expect matching give-up/goal clear, missing
cine IDLE/ideal IDLE, no cursor restart from invalid header. Separately valid cursor>42 remains
valid, while failure>=42 clamps to1 (arm test when no retail long-program fixture exists).
Never valid-arm ClearSchedule or fallback for an unchanged valid control. `0x1027bf50/0x1027be60`.
Each invalid arm's output/result is labeled in JSON, never one broad “any fallback” match.

### Marker save: actual witness map, then controlled invalid arms

**save_restore_interesting_place_visit — map:sm_hub_1.** Fresh hub; cast pedestrian_female;
player placed to keep its chosen route/arrival in PVS without blocking it. After FIND_INTERESTING_
PLACE and live WAIT_FOR_MOVEMENT, capture reserved marker occupant+bounds/count, current place,
goal/cursor. Save/load and compare at apply; expect same destination, arrival, interest program
and later leave once. Never second find/re-pick, rejection, taskfail, claim/arrival/leave replay
between applied and lawful release. `0x102da0d0/0x102da860/0x102d9240/0x102d9320/
0x102db5e0/0x10299a80`. Record exact selected authored place from capture; bus_stop Idle/cap4
is a known authored example, not a forced seed outcome. Preserve route/PVS blockers as retail.

**save_restore_place_activity — map:sm_hub_1.** Same actor/map but capture in DO_INTEREST_ACTIVITY
after arrival, interior INTO/idle phase with wait still future. Expect unchanged marker/cursor/
wait and clip phase then one OUTOF/release/left. Never replay ClaimMarker/in-use increment,
duplicate arrival/left or a new wait draw at restore. `0x102da7c0/0x102a9f40/0x102aa210/
0x102da600`. Separate from walking save to distinguish reservation count from in-use.

**restore_place_invalid_marker — arena.** places_pedestrian_visit donor with real reserved row;
after valid restore apply two narrow fixtures: place missing from live list, then live place
without this NPC occupant. Invoke real consistency/release consumer with the saved place word
(ordinary valid OnRestore's scan cannot fabricate either malformed reference). Expect each
specific rejection clears place, release returns without duplicate outputs; valid control
retains same row. Never invented claimant on decode. `0x10299a80/0x102b53d0`. Include LAST-match
and row swap-removal in arm evidence; broader rating/default/selection work remains R2.

**place_marker_reservation — arena.** from_map hub bus_stop bounds/type, ordinary pedestrian
reservations, plus separate explicit capacity0/disabled/full rows. Use reserve_spot door to
invoke actual PickSpotFor on controls: completely occupied sampled region (>8 overlaps),
stationary hull blocked on both allowed clearance attempts, and a clear positive case. Staging
uses actual measured hull extents and room solids to bound a small region; final JSON records
those bounds/positions, no mocked success/failure. Expect AddMarker occupant/both absolute bounds
before ClaimMarker, raw capacity refusals, failed-attempt increment on overlap exhaustion,
four-slot failed-box now+2 on clearance failure, and the shipped warning+SUCCESS/AddMarker
after second failed clearance. Never origin-only destination overwrite, overlap predicate
“correction”, extra sampling draw or ClaimMarker writing occupant. `0x102d9fa0/0x102da0d0/
0x102da9e0/0x102d9ed0/0x102da860/0x102da7c0`. Include post-save row equality here. N17 consequence
goes to judge/R2 bookkeeping; reservation writer is an immediate N14 dependency, not a re-bake.

### Corpse save — six callback outcomes

Use existing corpse records' exact cast, flags, damage path and visibility seats; save after
death and before deadline (static captures at synchronous corpse fence to fit0.5s). Save after
fade has begun to distinguish alpha state from start-delay. Freeze/apply compare callback,
NextThink remaining interval, life/death/render/solid/alpha; presentation restores via V4d once.
All six forbid duplicate OnDeath/corpse creation/weighted pick, NPC schedule/AI after restore,
and any new death-relative timer at load. Dead logical origin stays the saved death spot.

| record | stage/staging | expect / additional never | citations |
|---|---|---|---|
| save_restore_corpse_unseen | map:sp_tutorial_1, thaw thug_3, lethal packet, player out of corpse cone/PVS/visibility | original remaining +10 deadline, one removal; never early removal |0x1032c0e0/0x102696f0|
| save_restore_corpse_seen | map:sp_tutorial_1, same but player keeps cone/PVS/FVisible, then turns away after rearm fence | survives first due with next deadline+10, then one removal; never removed while all three visibility predicates true |0x102696f0|
| save_restore_corpse_burn | arena, corpse_kindred_burns donor | SUB_Remove at original+10 regardless of sight; never restarted burn sound or PVS extension |0x1032c32f..d7|
| save_restore_corpse_static | arena, regular_cop + named No_Ragdoll_Death0x80000 fixture | hidden original's SUB_Remove at original+0.5; never NPCThink or new static-copy creation at load |0x1032c0e0/0x101c0b10|
| save_restore_corpse_fade | arena, corpse_fades donor, checkpoint after first alpha decrement | fade callback/alpha survives, next decrement7 and terminal Remove+0.2; never alpha reset255 or prior burn/PVS callback winning |0x102695d0/0x10269960|
| save_restore_corpse_pedestrian | map:sm_hub_1, pedestrian_female lethal with later fade bit absent | NULL callback remains, corpse exists beyond original+10; never removal/ordinary AI |0x103a38c0|

The map predicates are probed/logged, not assumed from rendered visibility. Exact physics rest
pose across load is not required: V4d modernization only gets body simulation/release smoke.
If static-copy creation is an absent later-spec substrate, the half-second original callback
remains executable; record precisely the absent visual/static copy under judge0014.

### Hidden, ground and perception

**save_restore_hidden_unhide — map:sp_tutorial_1.** thug_3 born hidden, no pre-unhide AI; save/load
at2s, compare hidden NULL/never callback and saved callback/physical words at apply. Fire ScriptUnhide
only after ready. Expect restored callback due-now, body exists, grounded/fall as actual authored
origin requires, next senses/combat. Never hidden schedule/SEE_ENEMY or re-running NPCInit/save
callback as an ordinary think. `0x100a8710/0x100a8990/0x102998c0/0x102c1ec0`.

**map_tutorial_unhide_thug3 — map:sp_tutorial_1.** Direct original defect witness: StartHidden1,
thirtyeight/knife, spawnflags4. Player placed on accessible visible floor near converted origin
(-1725,471,0 Source); ScriptUnhide at2. Probe actual body/capsule/ground and sequence, expect
SEE_ENEMY→START_COMBAT→range attack. Never AI before unhide, no-body success or indefinite
FALL_TO_GROUND. `0x100a8990/0x102c1ec0/0x10273ad0/0x1028a2a0`. If baked body absent, judge
evidence; a ground teleport is not a fix. Relabel existing lifecycle_unhide_fights/animal rollcalls
only after their true hidden nevers pass.

**lifecycle_unhide_fall_to_ground — arena.** Existing hidden HumanCombatant donor explicitly at
[far_ne XY, floor+100cm], spawnflags4 (skips startup floor drop), StartHidden1. Unhide2. Expect
0x73→0x3e/task fall, motor lands/ground flag, fall completes, combat proceeds. Never AI while
hidden, artificial instantaneous unhide origin reset or taskfail. Do NOT ban all sight while
airborne: gather can see before selector chooses fall. `0x1028a2a0/0x10273ad0`, fall task/motor.
Also arm-test ordinary spawnflag0 startup full-hull down256 sweep versus fly/swim/capability4/flag4.

**lifecycle_startnpc_ground_drop — arena.** regular_cop at floor+100cm with spawnflags0 versus
flag4 control. Capture startup/activation before first fall movement: ordinary StartNPC's full
hull sweep snaps to traced floor and synchronizes body; flag4 keeps authored elevated origin,
clears ground then later falls. Narrow startnpc_ground_gate controls separately invoke normal
fly/swim/capability4 branches with live bodies. Expect performed/skipped counters, logical and
motor origin equality and correct later grounding; never unhide-as-floor-snap or a guessed floor
when TraceHull misses. `0x10273ad0/0x102e7880`. Collision absence answers a named missing input,
not a successful trace. This reaches startup's branch family the authored thug3 flag4 does not.

**lifecycle_relationship_flip / input_setrelationship — existing arena donors.** Keep neutral→
hated visible-player staging and survival. Expect next-Look Sighted/SEE_ENEMY/NEW_ENEMY then
combat, never hostile conditions before flip; neutral player can be sighted already.
`0x10273790/0x102b38b0/0x1030ff10`. These are guards, not presumptive source repair.

**dialogue_perception — map:sp_tutorial_1.** Hunter1's hunter1.dlg (authored witness). Use direct
StartPlayerDialogRemote to obtain TASK_RUN_DIALOG, preserve actual conversation screen hold.
At hold fence change visible player to hated with SetRelationship; sample live partner/task,
gather latch/pass and Sighted/SEE_ENEMY/NEW_ENEMY. With stable0xb9/live partner expect ordinary
RunAI gather skipped, no new hostile sight from that skipped pass. Separately teleport player
behind a real wall at distance>512 Source units, same PVS, hold beyond8s and next2s LOS check:
expect independent LOS falls then rises on return (record exact safe map positions). If state
bit8 forces LOS true, assert that arm and stage normal-IDLE dialogue separately.
Return player visible, normal dialog_choose end, one OnDialogEnd, next full eligible gather
produces SEE_ENEMY/NEW_ENEMY. Never fabricated sensing inside skipped gather or blanket LOS
freeze. `0x1026f110/0x1026ec30/0x102814d0/0x10291610/0x10293095/0x10310710`.
If program reselects during hold, GetNewSchedule can legitimately gather: trace its caller and
stage a stable hold for ordinary-skip assertion. No save of open dialogue is added.

### Last damage, weapon handoff and maker/coordinator measurements

**damage_last_record — map:sp_tutorial_1.** thug_3 unhidden/alive, player behind/off-view,
damage_packet1 with real player attacker and a named live inflictor at a distinct known position
(extended map spawn info_target if needed). Expect last attacker=player, attack-position=inflictor,
stamp=current map clock, sum1, LIGHT_DAMAGE, no death. Save/load and compare same record/TIME.
Repeat no-inflictor control expecting self+64*live death-throw direction, not attacker origin.
`0x10265fb5/0x10266052/0x102661c3..0x10266333`. No assumption that scalar TakeDamage input
without attacker should record damage. Finish with genuine health/wounds probes, since damage
trace alone can describe a refused packet.

**damage_record_branches — arena.** Same base virtual thresholds (HumanCombatant inherits
slots576/577): separate labeled victim/control setups for no attacker, non-NPC info_target
attacker, seen qualified, unseen known attacker, unseen unknown +current enemy/no SEE_ENEMY,
and unknown/no enemy. Read memory record chosen and motor face target. Qualified packets0,20,
21 prove strict0/20; seed known high health/max then sum exactly30% and just-over, intervals
just-below1/equal1 for accumulation/reset; same-frame pair proves at-most-one OnDamaged.
Expect precisely gated record/sound/output/condition writes, current-enemy memory in special
unseen arm; never null enemy substitute, record on null/non-NPC attacker, death. `0x10265ed0/
0x1026612c/0x10266630/0x10266660/0x102662a8/0x10266307`. Only missing input producer outside
wave is named; do not mock the response being asserted. Sound expiry race belongsV10.

**save_restore_single_round_reload — arena.** A real single-round NPC weapon selected from
existing baked item data; narrow WeaponFinishReload fixture at task's admitted finish sets
bInReload then slot322 returns at owner-player gate. If no such baked NPC item mode, select its
same existing weapon data object and explicitly fixture bReloadSingle as nonauthored arm setup,
recording classname/mode; do not re-bake. Set jam/interrupt via narrow fixture too so persistence
is nontrivial. Save/load; compare three bytes/owner/clip/reserve/raw attack stamps. Expect
retained bInReload, no reload completion call during restore, no clip/reserve debit. Never fake
reload counting as proof of this arm. `0x1028918d/0x1025506f..77`, raw SAVE weapon rows.

**maker_refusal_measurement — arena.** Keep hw_hub_1 ratmaker_2 from_map donor, enable1,
kill first rat only after it thinks, first post-death attempt captured with stable child id.
Expect first spawn→death→one OnNPCDied→makerattempt carrying live0/gate/box/candidates. Never
second OnNPCDied or more than one spawn while child lives. The measurement may log admission
instead of refusal if the old red no longer occurs; do not make refusal itself the desired fix.
`0x1034b580/0x1034bc90/0x1034bbf0`. hw_hub_1 is the donor, not one of this spec's two witness maps.

**maker_respawn — same arena donor, corrected after M1.** If corpse box is cause, expect occupied
refusal at live0 and no child2 until the blocking old entity is removed with Kill, then retry
within measured1..2 and new child spawn/think. If another gate is cause, stage its lawful release
and use that retail cadence. Ban extra OnNPCDied and spawn count>1 before first death. Remove
old unconditional “within SpawnFrequency after death” claim/known_red only with M1 evidence.
Never add alive test. `0x1034b692..737/0x101cc9e0/0x1034bbf0`.

**save_restore_melee_coordinator — arena.** chase_melee donor plus three melee NPCs sharing
one coordinator and a high-health/un-killable target; capture after admissions, normal load,
read fresh-world empty arrays at apply, then ordinary re-admission/retarget/remove. Expect cap2,
idempotent add/no duplicate handle and legal subsequent melee decisions. Never cap overflow
or stale-epoch members. `0x1025d880/0x1025db70/0x1025dca0/0x1025ddd0`; explicitly label empty
restore policy as port inference pending M2. This guards it without declaring retail equality.

### Harness self-tests and suite isolation

**persistence_refused_save — arena, expect_fail.** Stage a real CanSave block (active grapple,
using existing verbs donor), request named save. Expect exact script refusal/result reason,
never successful written/applied label. Engine load chain0x20096010; retail grapple block
0x10174f80. K1 is removed, so active cine is not the refusal fixture.

**persistence_missing_load — arena, expect_fail.** Load a unique slot never created by the run.
Require exact missing-slot script failure, never applied/ready-success.0x20096010 missing-file arm.

**persistence_world_rebind — arena.** script_walk_to_mark donor, capture during WAIT_FOR_SCRIPT,
write/load, after-label/count assertion spanning both worlds, then play/end once. Expect trace
sink reinstalled before restore events, monotonic scenario time, new epoch, no replayed save/
load/zero-player actions. `0x1011a620/0x1027bf50`. No expected-fail field.

Each new arena record: `uv run elysium arena <name>`, then
`uv run elysium arena control_sequence <name>` in one boot. Each map record alone then after
another record of SAME initial stage, with both marked shares_map and target starting fresh:
tutorial predecessor session_map_load_fresh_world (for that record itself use map_tutorial_idle
with shares_map/fresh staging adjusted explicitly); hub predecessor map_hub_idle similarly.
During the record, travel may visit another map, but it returns to initial host before finishing.
Stage-local boot planning stays unchanged. Record final verdict/first_unmet/trace evidence for
every pair, not just successful launcher exit. No old-clock seed repair.

Named guard run additionally: lifecycle_unhide_fights lifecycle_relationship_flip input_setrelationship
rollcall_vanimal rollcall_vdog rollcall_vscurrying places_pedestrian_visit script_walk_to_mark
script_dialog_hold cover_move_shoot ranged_open_fire corpse_removed_unseen, plus V5b's
ranged_fake_reload/ranged_step_back_holds when their landed files exist. Changed/rebroken
records are yours; never transfer a V6 failure to H11 or a vague “asset issue”.

## Closure evidence and commit

Record every new/changed record's stage, seed, measured capture/applied/continuation times,
alone/paired results and final full-run verdict. Oracle recovery accompanies code; fix spec/C4
cursor, N14 writer, N9 classification, K1/restart divergence rows. Keep M2's explicit unresolved
retail lifecycle and README judge owners in final report. No blanket “V6 owes nothing”.

Stage by explicit path on the named integration branch. One commit, no push. Suggested subject:
`fix(npc): V6 map clocks and coherent lifecycle restore`.
Message includes verdict table (record, before→after, retail reason), default/arm totals,
kernel gate, build wall times, changed tests/why, exact remaining inferred seams/judge owners.
Final integrator report under350 words; no report*.md artifact. Wait for actual completion of
all runs. Planner does not perform any of these steps.
