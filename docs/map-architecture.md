# Map load/unload architecture

How Elysium-Unreal represents, loads, unloads, and travels between VtMB maps. The design
goal: **one Unreal level, ever** — every VtMB map is runtime data inside it, and the same
machinery scales from today's debug viewer to the finished game (menu → new game → hub
traversal → save/load).

## Why not Unreal levels

UE's level streaming and World Partition manage *authored `.umap` assets*. Elysium maps are
built at runtime from the pipeline intermediates, so there is no asset to stream. VtMB is
also a discrete-map game — Source `trigger_changelevel` + loading screen, not seamless
streaming — so a single persistent world with explicit load/unload matches both the engine
reality and the original game's own model.

The one `.umap` in the project is `Content/Elysium.umap`: an empty persistent level
(generated from source by `tools/make_boot_map.py`) that exists only because Unreal must
boot into *some* level to create a `UWorld`. It is never edited and never multiplied.

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

Implemented today: `UElysiumGameInstance`, `UElysiumMapSubsystem` (Travel/unload/ExportedMaps
plus `elysium.map` / `elysium.maps` engine-console mirrors), `AElysiumMapActor`, and the
dev console (`UElysiumConsoleSubsystem`, Slate drop-down on ` / ', with `maps`, `map <name>`,
`map next`, Tab completion, and Up/Down history). Loading is synchronous except collision
(async Chaos cook; the pawn is held frozen until ground exists under the spawn, ~0.4s).
Landmark travel and changelevel volumes await entity decoding.

```
UElysiumGameInstance            process lifetime — session state
 └─ UElysiumMapSubsystem        game-instance subsystem — the map machine
     └─ AElysiumMapActor        one per loaded map — owns everything map-scoped
         ├─ WorldMesh / SkyMesh (PMC sections per material)
         ├─ collision, lights, props, coronas…        (M1+)
         └─ entity actors (changelevel triggers, …)   (M1+)
```

- **`UElysiumGameInstance`** — state that must survive map changes: the pending travel
  request, and later the character sheet, quest/story state, and save-game serialization.
  A save file is (GameInstance state + current map + landmark/position).
- **`UElysiumMapSubsystem`** (`UGameInstanceSubsystem`) — the only owner of map lifecycle.
  API: `Travel(FString Map, FString Landmark)`, `GetCurrentMap()`. Runs the transition
  state machine: `Active → FadeOut → Unload → Load (async) → Spawn → FadeIn → Active`.
  Shows the loading screen (UMG overlay; later the VtMB loading art). Registers the
  `elysium.map <name> [landmark]` console command.
- **`AElysiumMapActor`** — "one loaded VtMB map", spawned by the subsystem, never by the
  game mode. Everything map-scoped is a component of it or a UPROPERTY it owns, so
  **destroying the actor unloads the map** — meshes and MIDs die with it, and owned
  transient textures become GC-collectable. No manual teardown lists.

The game mode stays thin: pawn + HUD classes, and on BeginPlay asks the subsystem to run
the boot travel (today: straight into a map; with M3: into the menu instead).

## Ownership and memory across transitions

The texture cache stays a process-wide *index*, but **ownership moves to the map**: each
`AElysiumMapActor` holds strong (UPROPERTY) references to the textures decoded for it; the
cache itself holds weak entries. Unload = destroy the actor → GC frees its textures; assets
shared across maps (UI, NPCs later) live in an explicit global scope instead. Meshes,
materials, collision, and entity actors are components/outer'd objects of the map actor and
need no accounting at all.

## Async loading (removing the load hitch)

Map load splits into two stages:

1. **Task threads** — file IO, OBJ/MTL/sidecar parsing, PNG decode. Pure data, fans out
   across cores (the Godot viewer's `Prewarm` equivalent).
2. **Game thread, time-sliced** — `CreateTransient` texture uploads, mesh-section
   creation, actor spawns, budgeted per frame behind the loading screen so the world
   never hard-freezes.

The subsystem drives both from its `Load` state; a travel that happens to hit an
already-loaded map (later: cached parse results) just runs stage 2.

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
