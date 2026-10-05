# Brief V6-3 — transactions the arena can observe

Read main AGENTS.md, Arena/README.md, V6 README and packets. Existing console actions reach
elysium.cmd save/load, but do not fence completion and runner aborts on world replacement.
Implement the harness dependencies now; records belong to integrator, not this lane.

Exhaustive files (under Source/ElysiumUE except the final path):

```text
Private/Debug/ElysiumArenaScenario.h
Private/Debug/ElysiumArenaScenario.cpp
Private/Debug/ElysiumArenaScenarioRunner.h
Private/Debug/ElysiumArenaScenarioRunner.cpp
Private/Debug/ElysiumArenaRun.h
Private/Debug/ElysiumArenaRun.cpp
Private/Debug/ElysiumArenaStage.h
Private/Debug/ElysiumArenaStage.cpp
Private/Tests/ElysiumV6ArenaPersistenceTests.cpp
Arena/README.md
```

## Numbered jobs

1. **Strict transaction schema** — retail engine0x20096010/0x200975f0, server0x1011a620;
   `ElysiumArenaScenario.{h,cpp}::ReadActionName/ReadAction/ReadProbe/ActionName`.
   Add save/load(slot), fresh_map(map), travel(map, optional landmark) and snapshot comparisons.
   Proposed checkpoint: save action names checkpoint and an explicit list of typed witness fields;
   restore_compare action references that checkpoint and field list. Validate unknown fields,
   duplicate checkpoints, missing slot/map, incompatible probe types and unsupported hosts.
   Extend ordinary spawn to map hosts through SpawnRuntimeEntity (same authored-row parsing,
   explicit map coordinates); this stages a disposable runtime entity/relay for the fresh-world
   witness. cast/rows/from_map retain their existing host rules; this is the explicit added door.
   Names may be agreed with lanes1/2, but the integrator must receive the final exact JSON schema.
   Captures are bound to runtime save/apply fences, not action scheduling time. Probe assertions
   that need “same as saved” reference checkpoint, with exact scalar/handle/name match or specified
   numeric tolerance; never compare opaque archive bytes containing changed epochs/caches.

2. **Completion and runner survival** —0x200975f0/0x1011a620;
   `ElysiumArenaScenarioRunner.cpp::RunAction/Tick/LiveWorld/RecordEvent/TraceRemovals`,
   `ElysiumArenaRun.{h,cpp}::Tick/GetEntityWorld/BeginNextRecord`, stage `FHost`.
   Runner/record/labels/actions/counters/checkpoint state remain owned through GI-scoped ArenaRun.
   Subscribe to lane1 operation id/captured/write-result/applied/ready; pending operation blocks
   dependent actions. Refusal, decode failure, wrong map or wall timeout yields script error with
   exact reason. Stop touching retired world immediately; rebind host/map/world epoch and sink
   BEFORE restoration/activation traces can fire. Do not call Start again and replay zero-player
   staging, scripts or expectations. Clear old removal tracking on intended replacement (no
   synthetic “every saved entity was removed”); actual removals still trace normally. Unplanned
   world loss remains error. Match labels exactly once and preserve `never` counts across load.

3. **Two time coordinates** — engine0x200f5bc4/0x200975f0;
   `ElysiumArenaScenarioRunner.h::FEvent/ScenarioTime`, runner `Tick/RecordEvent`,
   `ElysiumArenaStage.cpp::Stage`.
   Reset fresh Green Room clock to1.0 before Load, preserving V4c SeedAll ordering and no injected
   draws. Scenario elapsed time uses accumulated simulation intervals/segment offsets and cannot
   go backward when world clock rewinds or changes map. Pending no-world gap uses bounded wall
   timeout and no fabricated simulation elapsed. Trace carries scenario elapsed plus world time/
   epoch/map at transaction fences. First map clock asserted at pre-entity fence1.0; late-ready
   value reported separately. Do not reseed at restore: saved RNG state is restored normally.

4. **Green Room save transport** —0x1027bc60/0x1027c160/0x1011a620;
   `ElysiumArenaStage.{h,cpp}::Stage/FHost`,
   `ElysiumArenaRun.cpp::StandArena/BeginNextRecord/Tick`, runner `RunAction`.
   Stage-only defs do not identify an ordinary baked map. Persist checkpoint provenance in a
   nonshipping harness envelope: original stage defs/network/seat plus ordinary session/map
   snapshot via lane1 common codec/storage/apply services. On load rebuild actual stage world
   and model admissions, then use the SAME entity decode/rebase/post-restore core. Never copy
   NPC members as restore, bypass production baked-map checks, or treat a generic console line
   as an accepted write. Normal map host save/load uses RequestSave/Load including ordinary
   CanSave and hard-travel gates. If Green Room lacks in-session/player setup, report exact
   GameFlow initialization line owed; integrator stages a real harness session before capture.
   Pending write uses a dedicated arena slot, never the user's latest slot. Do not delete saves.

5. **Typed read-only witnesses** —0x1027bf50/0x102998c0/0x102d9240/0x10265ed0/
  0x1032c0e0/0x10255077; `ElysiumArenaScenarioRunner.cpp::ReadProbe`, scenario `ReadProbe`.
   Support clock/map/world-generation, current task id/index/status/start/deadlines, callback and
   saved callback, hidden/script owner, native base phase/finish/event window, each layer's complete
   saved record, move/shoot words, weapon owner/reload/jam/interrupt/next stamps, navigator semantic
   goal/target, enemy-memory position/last-seen, last-damage attacker/position/sum/time, place identity/
   marker handles/bounds/count/capacity, can-sense/Sighted/dialog-open/live partner, gathered latch/
   pass and independent player-LOS/PVS/cache/last-clear, and coordinator membership.
   Read the substrate, not a guessed animation label or debug string. Snapshot fields that are
   intentionally re-derived (shoot-at reroll, raw pointers, epochs) are typed exclusions stated in
   schema/docs. Restore comparison occurs BEFORE first think, followed by ordinary behavior
   expectations; tolerance only for native float conversion, not a frame of lost time.

6. **Controlled unreachable arms** —0x10255077/0x1028918d,0x1027bf50,0x10299a80,
  0x10265ed0; `ElysiumArenaScenarioRunner.cpp::RunAction`, scenario parser.
   Add narrowly named arena-only fixture operations: NPC single-round WeaponFinishReload; saved
   checkpoint header corruption (CRC/missing cine/target/path) for invalid-restore branches;
   invalid marker membership for consistency tests; tagged different-base restore context;
   no-ragdoll-death MiscFlag0x80000 fixture for the static original's callback; damage_packet
   optional named inflictor or absent attacker and
   a named setup of current-enemy memory/SEE_ENEMY for the unseen arm. No generic arbitrary member
   poke exposed as a retail input. Each reports its setup in trace and invokes the normal runtime
   consumer afterward. Only a Green Room fixture may reach these setup doors. Existing damage_packet
   supports real attacker and zero; keep it. Harness recipe comments state why the map cannot
   reach the arm (NPC event shot doesn't spend clip, invalid header is not ordinary player action).
   If the runtime consumer needs a callable adapter outside this lane, report exact owed lines.

   Also add narrow reserve_spot setup/consumer door for PickSpotFor's full/occupied/exhausted
   clearance controls (0x102da0d0), and startnpc_ground_gate for fly/swim/capability4 controls
   (0x10273ad0). Live hull/collision/motor services must answer, never mocked consumer results.
   Ordinary authored bounds/capacity/ground-drop cases use row keys directly.

7. **Admission measurements** —0x1034b580/0x101cc9e0/0x1034bc90;
   runner `RecordEvent/ReadProbe` and Arena/README.md. Consume integrator's synchronous makerattempt
   tap at CanMakeNPC refusal with live/global/gate/box and every enumerated candidate's flags,
   alive/life/bounds. Runner must not sample it a tick later. Distinguish same-name old corpse/new
   child by stable index; end probe may not choose the corpse merely by ambiguous targetname.
   No alive filter in the consumer, no tracing-induced RNG draw. Coordinator record exposes
   existing empty-list policy/capacity; do not present it as a verified retail saved list.

8. **Harness verification, no coder runs** — same addresses;
   `Tests/ElysiumV6ArenaPersistenceTests.cpp` under Elysium.Arm.V6.ArenaPersistence.
   Parser refusal/unsupported host/missing checkpoint; accepted versus failed asynchronous write;
   wrong-map/timeout; monotonic scenario clock; expected world replacement preserves labels,
   never-counts and sink; restoration equality failure cannot pass; no replay of fired action.
   Document full schema/examples/transport/time bases in Arena/README.md, replace the zero-clock
   paragraph with verified1.0 initialization and ready-time caveat. Keep first NONE edge semantics.

## Owed integration lines

Lane1 supplies context, capture/apply and result fences; lane2 provides state serialization and
typed witness accessors. Integrator supplies makerattempt/map geometry tap, real phase seek/motor
adapters, any nonshipping MapActor declarations for stage restoration, and all JSON/oracle/verdict
edits. No pipeline lane: GI-owned in-record travel/rebind stays in one launch/report. Report any
actual pipeline-host failure to judge; do not edit arena_suite.py or launch a second process here.

The three self-tests are persistence_refused_save, persistence_missing_load and
persistence_world_rebind; integrator authors them from your schema. The first two expect_fail
and assert exact failed script/result, not “nothing happened”. The third executes a transaction
with an after-label never count before/after and verifies no second action/zero-player apply.

## Coder rules

Write only the listed lane files, in the coordinator-named worktree, by absolute path. Read and
run read-only tools from `E:/dev/elysium-unreal`; write nothing in main. Never build, bake, run
game/tests/arena, commit or push. No git clean/reset/stash/checkout/worktree operations or deletion.
Every changed line cites its retail consumer address; a harness-only staging/transport line names
its harness purpose alongside that address. Generated *Slots.cpp is never hand-edited; a needed
hand body outside the lane is owed to integrator, who records kernel_verdicts.tsv. Report new
divergences, never adopt them. C4458/C4459 shadowed locals are errors; inspect include/declaration/
duplicate-definition seams. Report under350 words: files/functions/addresses, exact JSON/API
contract, read-only checks and exact lines owed outside this lane. No report*.md file.
