# Brief S1 — the Green Room "cover" scenario (spike, 0018/8 live check)

Read `CLAUDE.md`. Goal: a CONTROLLED stage where a ranged Troika human, hostile to the player,
runs the tactical cover search (`0x102b7110` → `FindHintByClassMask(8, 1, CoverRadius)`) and claims
an `ai_hint`, driveable from the console (and so from MCP `elysium_console_exec`). You edit ONLY:
- `Source/ElysiumUE/Private/Debug/ElysiumArenaSpec.{h,cpp}`, `ElysiumArenaBuilder.{h,cpp}`,
  `ElysiumGreenRoomArena.cpp`, `ElysiumArenaCast.{h,cpp}`, `ElysiumGreenRoomConsole.{h,cpp}`
- `Source/ElysiumUE/Private/Tests/ElysiumMovementTests.cpp` only for the existing
  `Elysium.Substrate.ArenaSpec` case if the spec's shape change breaks it
Do NOT build or run. Another agent is editing `Debug/ElysiumEntityDebugSubsystem.cpp` and
`Debug/ElysiumMcpTools.cpp` — do not touch those. Report ≤300 words.

Facts (from the survey): `uv run elysium gr --arena` → `-ElysiumGreenRoom -GreenRoomLab
-GreenRoomArena` (`ElysiumGreenRoomRun.cpp:121`); the stage world runs `EntityWorld->Load(FElysiumEntityDefs())`
with zero defs (`ElysiumMapActorLifecycle.cpp:176`); `BuildArena` (`ElysiumGreenRoomArena.cpp:34`)
builds floor, walls, one cover block, a NavMesh (`ElysiumArenaBuilder.cpp:79,133-158`); the spec is
`ElysiumArena::Build()` (`ElysiumArenaSpec.cpp:87-130`): solids, 8 `intersting_place` anchors, 5
pads, player feet / yaw. `ElysiumArenaCast::Spawn` → `SpawnRuntimeEntity` with `FSpawnRequest`
(`ElysiumArenaCast.h:59-90`, keys written at `.cpp:149-166`). The place set is never adopted in the
stage world (`AdoptMapPlaces` is the map actor's, `ElysiumMapActorLifecycle.cpp:850`), so the AI
network is empty and a runtime `info_node*` def (which `SpawnRuntimeEntity` turns into an `ai_hint`,
`ElysiumEntityWorld.cpp:579-607`, binding `m_nNodeID` through `SpawnNodeRow`) would land outside it.
Tests adopt rows with `World.Places().AdoptRows(Rows, …)` (`ElysiumNpcTestFixture.h:217-224`,
`FElysiumPlaceRow` in `Public/ElysiumMapPlaces.h:15-39`: `NetworkIndex`, `Type` 2, `OriginCm`,
`YawDeg` Unreal-native, `ZOffsetCm[22]`, `HintBspIndex`).

## Deliver

1. **Spec: cover nodes.** Add to `FSpec` a `Nodes` array (`FNode { FString Name; FVector FeetCm;
   float YawDeg; int32 HintType; int32 GroupId = 1; }`) and author FOUR in `Build()`: two type 101
   (`cover_low`) at the cover block's north and south faces, facing OUT (the same posture as the
   `cover_north` / `cover_south` anchors, set back from the face by the same `FaceAnchorSetback`),
   and two type 10200 (`cover_corner`) at the NE and NW corners facing the middle. Their
   `hint_rating` is the authored default (3 → 2.5 after `NPC_Cover_Distance_Scalar`),
   `target_dist_min` / `max` / `angle_range` left to `CAI_Hint::Spawn`'s per-type defaults (do not
   author them). Keep `Elysium.Substrate.ArenaSpec` passing (extend it with the node count).
2. **Builder: the network.** In `BuildArena` (or the builder), BEFORE any node spawns: build
   `TArray<FElysiumPlaceRow>` from `Spec.Nodes` (`NetworkIndex` = position, `Type` 2, `OriginCm` =
   feet, `YawDeg`, `ZOffsetCm` all 0, `HintBspIndex` = INDEX_NONE) and `EntityWorld.Places().AdoptRows(...)`
   then `BeginMapSpawn()` if `Load` already ran (read `ElysiumPlaceSet.h` for the order the map
   actor uses in `AdoptMapPlaces`, and mirror it). Then spawn each node as a runtime def with
   classname `info_node` (or the `info_node_cover_*` classname the hint census uses — check
   `ElysiumNodeEntity::ApplyHintReplacement` for which classnames become hints) and keys
   `hinttype`, `group_id`, `angles "0 <yaw> 0"` (Source yaw = −Unreal yaw; check how
   `ElysiumNodeEntity` reads angles), `targetname` = the node's name, so the hint is nameable from
   the console. Confirm after spawn that `FElysiumHint::NodeId` == the row index and `ClassMask`
   is 1 (log one line per node: name, index, node id, class mask).
3. **Cast: arbitrary keys.** Add `TMap<FString, FString> ExtraKeys` to `FSpawnRequest`, written
   after the fixed keys; the scenario uses it for `hint_groups` = `"1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32"`,
   `stay_entrenched 0`, `allow_kick_hint_use 1`, `percent_occluded_cover 100` (so the occluded
   reaction ladder picks cover; read `ElysiumNpcSchedule.cpp` / the occlusion ladder `0x102b8320`
   for the key names and what makes COVER win).
4. **Console verbs** (`ElysiumGreenRoomConsole.cpp`, registered like `elysium.gr_mode`):
   - `elysium.gr_scenario cover [weapon]` — stage the scenario: ensure arena mode, adopt the
     network and spawn the four nodes (idempotent: a second call clears spawned nodes/NPCs first),
     spawn ONE `npc_VHumanCombatant` on pad `north` with `Weapon` = `item_w_thirtyeight` (or the
     argument), `PlayerReaction` Hate 5, `Perception` 3, the extra keys above, `TargetName`
     `arena_gunman`; place the player at `PlayerFeet` facing the pad. Print what it did.
   - `elysium.gr_hints` — one line per hint on `World.HintList()`: index, name, type, class
     mask, node id, disabled, owner (entity index or none), next use, origin.
   - `elysium.gr_los <on|off>` — a toggle that moves the player behind the cover block
     (`behind_cover` pad's mirror on the player's side: the point on the player's side of the
     block, ~200 cm from the face) or back to `PlayerFeet`, so the enemy goes OCCLUDED and
     `CanSeekCover` raises. Use the existing player-placement helper the arena uses.
   Every verb prints through the console log so MCP `elysium_console_exec` can read it.
5. Keep `ElysiumArenaCast::Spawn`'s activation contract (the world must be ACTIVE; the stage world
   is). If something in `Load(FElysiumEntityDefs())` → `Places()` order makes adopting rows after
   `Load` impossible, say exactly which line and propose the smallest change; do not touch
   `ElysiumMapActorLifecycle.cpp`.
