# Map load/unload architecture

How Elysium-Unreal represents, loads, unloads, and travels between VtMB maps. The design
goal: **one authored `.umap`, reused** — a single empty shell that every VtMB map builds its
runtime content into, and the same machinery scales from today's debug viewer to the finished
game (menu → new game → hub traversal → save/load).

## The lifecycle model: OpenLevel hard travel

Map change uses UE5 **standard hard travel** — `UGameplayStatics::OpenLevel` → `UEngine::LoadMap`
— into the one reused shell `.umap`. `LoadMap` tears down the current `UWorld` and runs GC; the
fresh world's `AElysiumMapActor` reads the target VtMB map + landmark from GI-scoped state and
builds all content in code on `BeginPlay`. Cross-map state lives at GameInstance scope and
survives the travel; per-map state dies with the world. This is the UE5 standard for a
discrete-map single-player game, and it *is* VtMB's own model (`trigger_changelevel` + loading
screen). Owner call, with the options weighed and the migration scope: `decisions.md` 2026-07-24
(roadmap 10.8). *Level streaming and World Partition are rejected outright:* both stream
*authored `.umap` assets*, and Elysium maps are built at runtime from the pipeline
intermediates, so there is no asset to stream.

The one `.umap` in the project is `Content/Elysium.umap`: an empty level (generated from source
by `tools/make_boot_map.py`) that exists only because Unreal must open *some* level to create a
`UWorld`. It is never edited and never multiplied — every travel re-opens it.

## The travel model is VtMB's own

The `.ents` sidecar for every map already contains the original game's transition graph:

- `trigger_changelevel` — a volume with `{map, landmark}` keys. The export includes its
  convex hull vertices in engine space, so it is directly constructible as an overlap
  volume. sp_tutorial_1 has 14 of them.
- `info_landmark` — a named spawn point (`targetname`, origin, angles). Travel lands the
  player at the landmark named by the trigger that fired. sp_tutorial_1 has 12.
- `point_teleport` / `info_teleport_destination` — same-map teleports, same pattern.

So "advancing in the game" is: player enters a changelevel volume → `Travel(map, landmark)`.
No invented mechanism; the data drives it.

## Components

Implemented today: `UElysiumGameInstance`, `UElysiumMapSubsystem` (Travel/OpenLevel/ExportedMaps
plus `elysium.map` / `elysium.maps` engine-console mirrors), `AElysiumMapActor`, and the
dev console. `trigger_changelevel` volumes + scripted `ChangeMap` drive landmark travel through
the same seam. Loading is synchronous except collision (async Chaos cook; the pawn is held frozen
until ground exists under the spawn, ~0.4s); the time-sliced build is roadmap 10.4.

```
UElysiumGameInstance            process lifetime — session state
 └─ UElysiumMapSubsystem        game-instance subsystem — the map machine
     └─ AElysiumMapActor        one per loaded map — owns everything map-scoped
         ├─ WorldMesh / SkyMesh (PMC sections per material)
         ├─ collision, lights, props, coronas…        (M1+)
         └─ entity actors (changelevel triggers, …)   (M1+)
```

- **`UElysiumGameInstance`** — anchors the GI-scoped subsystems, the state that must survive a
  travel. A save file is (GameInstance state + current map + landmark/position).
- **`UElysiumMapSubsystem`** (`UGameInstanceSubsystem`) — the only owner of map lifecycle.
  API: `Travel(FString Map, FString Landmark)`, `GetCurrentMap()`. `Travel` stows the target in
  GI-scoped `PendingMapLoad` and `OpenLevel`s the shell (or, on cold boot, spawns the map
  directly in the already-empty shell); `UElysiumGameFlowSubsystem::NotifyWorldReady`, called from
  the fresh world's game mode, calls `SpawnPendingMap`.
  Registers the `elysium.map <name> [landmark]` console command.
- **`AElysiumMapActor`** — "one loaded VtMB map", spawned by the subsystem (via
  `SpawnPendingMap`) into the fresh world. Everything map-scoped is a component of it or a
  UPROPERTY / plain member it owns, so **the actor dying with its world unloads the map** —
  meshes, MIDs, and the map's texture cache die with it. No manual teardown lists.

The game mode stays thin: pawn + HUD classes, and one `NotifyWorldReady` on BeginPlay. The app state
machine behind that call is what builds the pending map (post-travel) or runs the boot decision
(menu / New Game / a bare dev load) — `runtime-architecture.md` §10.

## Ownership and memory across transitions

`OpenLevel` (`UEngine::LoadMap`) tears down the current `UWorld` and runs GC; everything the map
actor owns — meshes, materials, collision, entity actors, and the per-map texture cache — is
released with it, no bespoke flush. The **texture cache is a plain member of the map actor**
(`FElysiumTextureCache`), a per-map decoded-texture dedup index holding strong refs; those refs
drop when the actor is destroyed, so GC reclaims the textures. It is not a process-wide cache —
under hard travel only one map is resident at a time, so nothing is shared across maps (assets
that genuinely need to persist, like UI, live in an explicit global scope instead).

## Async loading (removing the load hitch)

The time-sliced build is **roadmap 10.4** (not built yet); it lands on the OpenLevel foundation —
the heavy build runs in the fresh shell world's `BeginPlay` behind a loading screen, not inside
`LoadMap`. Map load splits into two stages:

1. **Task threads** — file IO, OBJ/MTL/sidecar parsing, PNG decode. Pure data, fans out
   across cores.
2. **Game thread, time-sliced** — `CreateTransient` texture uploads, mesh-section
   creation, actor spawns, budgeted per frame behind the loading screen so the world
   never hard-freezes.

A travel that happens to hit an already-loaded map (later: cached parse results) just
runs stage 2.

## Evolution to the final game

| Stage | What lands on this skeleton |
|---|---|
| M1 | `.hulls` brush collision, `.lights` rig, props — all built inside `AElysiumMapActor` |
| M1.5 | Entity spawn from `.ents`: changelevel/teleport volumes live → real in-game map traversal |
| M2 | Water, decals, coronas, 3D-sky polish — map-actor components |
| M3 | Menu as the boot travel target; New Game = `Travel("sp_tutorial_1", …)` |
| M4+ | Entity I/O dispatch, story scripting, saves = GameInstance serialization |

Exported maps available today: `ch_hub_1, hw_hub_1, hw_redspot_1, la_hub_1, sm_hub_1,
sp_ninesintro, sp_tutorial_1, testbox` (the pipeline exports more on demand).
