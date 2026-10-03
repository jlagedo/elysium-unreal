# Brief S3 — the Green Room stage loads its scenario through the map path (spike)

Read `CLAUDE.md`. Goal: a FAITHFUL NPC spawn in the arena — the scenario's nodes and gunman go
in as authored entity defs through the same sequence a real map takes (places adopted → `Load`
→ the activation barrier → `Activate`), not one-at-a-time through `SpawnRuntimeEntity`. You edit
ONLY:
- `Source/ElysiumUE/Private/Map/ElysiumMapActorLifecycle.cpp` and the map actor's header
  (`Source/ElysiumUE/Public/ElysiumMapActor.h` or wherever `BuildStageWorld` is declared)
- `Source/ElysiumUE/Private/Debug/ElysiumGreenRoomConsole.cpp` (the `gr_scenario` verb's call site)
- `Source/ElysiumUE/Private/Debug/ElysiumArenaBuilder.{h,cpp}` (only to expose the node rows /
  defs the scenario already builds, if `gr_scenario` needs them as data)
Do NOT build or run (the game is running from the last build; the orchestrator rebuilds).
Report ≤300 words.

What S1 built (read `brief-S1-arena-scenario.md` and its landing in the files): the spec's four
cover nodes, `StandCoverNetwork` (adopts rows, resets the counter, spawns the nodes with map-row
keys), `ElysiumArenaCast::SpawnAuthored` (a ready map-row def through `SpawnRuntimeEntity`),
`gr_scenario cover [weapon] [body]`, `gr_hints`, `gr_los`. S1's blockers for the map path:
1. `ElysiumMapActorLifecycle.cpp:~225`: the stage's only `Load` is hard-coded to an empty def
   list on a privately owned entity world.
2. `ElysiumEntityWorld.cpp:~156`: `Load` never clears the entity list / indices and assigns
   handles by array index, so a second `Load` on a world already holding the player and anchors
   would reuse handles.
3. `ElysiumEntityWorld.h:~852`: `Teardown` is private.

Deliver `AElysiumMapActor::RebuildStageWorld(FElysiumEntityDefs Defs, TArray<FElysiumPlaceRow> Rows)`
(name it as the file's conventions want): tear the stage's entity world down the way the map
actor does on a map change (find the existing teardown path — `EndPlay` / travel — and reuse it;
if `Teardown` must become callable from here, make the smallest access change and say so),
recreate the entity world exactly as `BuildStageWorld` does (same seams: embodiment, clock,
collision payload null, `Bodies->SetMap`), `SetPlaceSetPending(true)` → `Places().AdoptRows(Rows)`
(→ pending false the way `Adopt` clears it — check), then `Load(Defs)`, then `SpawnPlayer` /
the pending spawn as `BuildStageWorld` sets it, then re-enter `WaitingForPrerequisites` so the
normal activation barrier runs `ActivateRuntime` → `EntityWorld->Activate(Now)` → `bSpawnDone`.
The arena's own anchors (`intersting_place`) and the player entity must come back too: include
them in `Defs` (read how the arena stands its anchors today — `ElysiumGreenRoomArena.cpp` — and
turn that into defs in the same `Defs` list, or re-run that step after `Load` if it is not
def-based; say which). The NavMesh / floor actors are Unreal actors, not entities: leave them.

Then point `gr_scenario cover` at it: build `Rows` (S1's node rows) and `Defs` = the arena's
anchor defs + the four node defs + the gunman def + whatever the stage's player entity needs
(look at how the empty stage gets its player entity: `BuildStageWorld` says "the activation
barrier requires both a world and a player entity in it"), call `RebuildStageWorld`, and print
what it did. `gr_scenario` must be idempotent (a second call rebuilds).

Keep `SpawnAuthored` as the fallback path behind a flag `gr_scenario cover --runtime` so the two
spawn paths can be compared in the same session. State every ordering difference you know of
between the two paths in the report.
