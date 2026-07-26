# Save architecture — the persistence design

**Status: adopted** (`decisions.md` 2026-07-26 cont. 4). Roadmap **9.5** builds it as **11.9** —
the playable path's PP5 rung.

How Elysium saves and restores a run. The VtMB-facts counterpart is **`savegame_format.md`** — the
`.sav` container, the five block handlers, and the exact inventory of state the original persists;
this doc owns the Unreal side: the container we write, the block model, how state is enumerated, and
what makes a save deterministic. The pairing is the same as `controls.md` ↔ `input-architecture.md`.

Retail `.sav` **import is a non-goal** (RE7, parked). What is reproduced is the *shape* of what a save
must hold, because that shape is a property of the game, not of the file format.

---

## 1. The five things a save must hold

Read off `savegame_format.md`, mapped onto the objects in `runtime-architecture.md`:

| Tier | VtMB | Elysium home | Block |
|---|---|---|---|
| slot metadata | the `.sav` global stream | `UElysiumSaveSubsystem` | header (uncompressed, readable without loading) |
| session state | `G` + `G.morgue` (pickled per map), quests, elapsed time | `FElysiumSessionRecord` | `Session` |
| the player | 277 datamap fields on the player entity + inventory as owned entities | `FElysiumPlayerRecord` | `Player` |
| per-map frozen world | `.HL1`/`.HL2`/`.HL3` per visited map | `FElysiumMapSnapshot` per visited map | `Maps` |
| in-flight time | `EventQueue` + think times | the substrate's own queue + per-entity next-think | inside each `Maps` entry |

Two structural choices of VtMB's are kept because they are load-bearing, and one is dropped:

- **Keep entity-as-inventory.** An item is a full entity with a datamap and script hooks; its
  condition, ammo and state persist for free, and `+use` semantics stay uniform. This is why
  `runtime-architecture.md` §5 makes the player an entity — inventory persistence *is* entity
  persistence.
- **Keep the per-map snapshot model.** A save holds the current map plus a frozen snapshot of every
  other map visited this run, so walking back into Santa Monica finds it as you left it. It is also
  exactly what the adopted OpenLevel hard-travel lifecycle produces at every travel boundary.
- **Drop the 16383-slot symbol table.** It exists because Source interned datamap field names at
  runtime; we name fields directly. The **zero-value-omission rule is kept** — it is most of why
  those files are small, and it costs one comparison per field.

## 2. The container

```
UElysiumSaveGame : USaveGame          // the only UPROPERTY-reflected part
{
    FElysiumSaveHeader Header;        // map, label, clan, level, playtime, timestamp, thumbnail
    TArray<uint8>      Payload;       // our own versioned, compressed block stream
};
```

`USaveGame` buys slot management, platform-safe paths and `UGameplayStatics::AsyncSaveGameToSlot`
without owning the content. The payload is written by `FElysiumSaveArchive` (an `FArchive` wrapper),
because **none of the game state is UPROPERTY-reflected** and none of it should become so: the
substrate is plain C++ precisely so that serialization, determinism and travel teardown stay in our
hands (`engine-core.md` R1).

- **Versioning** is an engine custom version (`FCustomVersionRegistration` + `Ar.UsingCustomVersion`),
  so a payload carries its own schema id and a reader can branch. A save from an older version either
  loads through declared upgrade paths or is rejected with a readable reason — never silently
  half-read.
- **Compression** is `FArchiveSaveCompressedProxy`/`Oodle` over the whole payload. VtMB's own
  per-section zlib exists to bound memory on a 2004 machine; one stream is simpler and smaller.
- **The header is outside the compressed payload**, so the load menu lists slots without inflating or
  loading anything — the reason `userName`, `comment` and `mapName` sit in VtMB's global stream too.

`.sav` files live under `Saved/SaveGames/`. They are the player's own data, gitignored like everything
else generated.

## 3. The block model

Four blocks, all plain structs we own, each with its own version tag:

```
Session   G (variant map) · G.morgue · quests · elapsed time · RNG stream state · app/story flags
Player    FElysiumPlayerRecord — sheet (base+current) · money · humanity · blood · masquerade
          · law counters + timers · quest journal · XP ledger · effects · email flags
          · inventory as item records · equipped handles
Maps      per visited map:  entity states · absent-entity set · event queue · think times
          · lightstyle phase · decals · physics poses · NPC memory (when AI exists)
World     the visited-map graph: landmark adjacency, the current map + player placement
```

`Session` and `Player` are the session record (`runtime-architecture.md` §1) written straight out.
`Maps` is the interesting one.

## 4. Enumeration is a walk, not a list (S9)

Adding save support to a class must not mean editing a save function. `engine-core.md` R2 already
gives every registered class a **field table** used by I/O, the Python datamap walk, keyvalue
application and the inspector; persistence is the fifth consumer.

```cpp
// FElysiumClassDesc field registration gains one flag
D.Field(TEXT("m_iHealth"), &FElysiumCombatCharacter::Health, EElysiumField::Save);
D.Field(TEXT("skin"),      &FElysiumProp::Skin,              EElysiumField::Key | EElysiumField::Save);
```

Saving an entity is: walk its class chain base-first, emit every `Save`-flagged field whose value is
non-default, tagged by name. Restoring is the mirror, matched **by name, never by position**, so a
field added to a base class does not invalidate existing saves. That is VtMB's own property — its
saves are self-describing datamap dumps — and it is what makes 9.4/9.8/9.9/9.10 free: a system that
registers its state as fields is saved the day it lands.

Three things do *not* fit the field walk and get explicit hooks:

| State | Hook |
|---|---|
| a leaf's derived runtime state (a mover's phase, a conversation cursor) | `virtual void FElysiumEntity::Serialize(FElysiumSaveArchive&)`, called after the field walk |
| bodies (meshes, MIDs, cables, constraints) | **not saved**. Bodies are disposable presentation (R1); they rebuild from the def + the restored entity state |
| anything an engine object owns (timers, tick handles, actor transforms) | **not saved**, and nothing game-visible is allowed to live there (R4/S1) |

## 5. The map snapshot lifecycle

A snapshot is produced by exactly the same code path a save uses, which is what keeps travel and save
from drifting apart.

```
enter map      →  is there a snapshot for this map in the session?
                    yes → build entities from the .ents defs, then apply the snapshot
                    no  → build entities from the .ents defs (first visit)
                  then hydrate the player entity from the player record
leave map      →  dehydrate the player record; freeze the map to a snapshot; store it in the session
save           →  freeze the current map too; write session + player + every snapshot
load           →  restore the session; travel to Header.Map; the enter-map path above does the rest
```

**Entities that travelled out with the player are absent, not duplicated.** VtMB's `.HL3` is a list
of save ids that left the map (the player plus everything carried); on restore the engine skips them,
because they now live wherever the player is. Our equivalent is one set per snapshot,
`AbsentEntities`, produced at dehydrate time from the item records moved onto the player record.
Without it, walking back into a map re-materialises the entire inventory — the failure mode the
original explicitly solves.

**Identity is the def index.** `FElysiumEntityHandle`'s index is the entity's position in the `.ents`
array — stable across runs, never reused within a map load (R3) — so it is also the save id, with no
extra id space. Runtime-spawned entities (`npc_maker.Spawn`, `CreateEntityNoSpawn`) live past the def
array and serialize their synthesized def alongside their state, so they restore as themselves.

## 6. The event queue and think times

VtMB saves both, and the reason is visible in its own data: a `logic_relay` mid-delay, a
`ScheduleTask`'d Python source string, and a discipline cooldown are all pending entries on the one
queue (`savegame_format.md` → `EventQueue`). A save that dropped them would silently break the
tutorial's beat machine.

This is why R4 bans `FTimerManager`: our queue is `TArray<FElysiumIOEvent>` of plain data —
`{FireTime, Target, Input, Param, PythonSrc, Activator, Caller, Serial}` — and serializing it is
writing the array with absolute times rebased to the restored clock. Per-entity `NextThink` rides
along in the entity field walk.

The one subtlety: **handles inside events**. `Activator`/`Caller` carry an epoch that will not match
the restored world's. They serialize as `{index, bWasValid}` and re-resolve against the new epoch at
load; an entity that no longer exists resolves to Invalid, which is the same falsy value a killed
entity already produces and which every call site already handles.

## 7. `G`, the morgue, and the script VM

`G` and `G.morgue` are the story layer — 1,226 distinct flags across 3,782 assignments, and 208 of the
345 flags level scripts read are written only by `.dlg` files (`savegame_format.md`,
`python_bridge.md`). They already live in C++ (`FElysiumGlobalMap`, case-sensitive, default-0 on
miss), so they serialize as a variant map with no pickling and no interpreter involvement.

**The CPython VM's own namespace is not saved**, and does not need to be:

- `__main__`'s level-script names are re-imported per map at load (the map's `worldspawn.levelscript`
  imports before the spawn pass, 9.3a), so they are derived, not state.
- Every durable value a script writes goes to `G`, the quest map, or an entity field — because those
  are the only writable surfaces the bindings expose. A script that stashed state in a module global
  would lose it across a save, exactly as it does in retail.
- Deferred script work is a queue entry carrying its **source string** (§6), which is how
  `ScheduleTask` survives a save in the original too.

`G.morgue` is a plain string set; it is *the story's* record of who is gone and does not imply the
entity is mechanically dead in a snapshot (`savegame_format.md`), so it is saved as written and never
reconciled against entity state.

## 8. Determinism

A save is only worth as much as the run that follows it, so three rules hold on every system:

- **All game-visible time comes from `FElysiumGameClock`** (S1). A system that reads
  `FPlatformTime::Seconds` or `World->GetTimeSeconds` for gameplay is a save bug waiting to surface.
- **RNG is an owned, seeded stream.** `OneOfSet` (589 dialogue gates), the d10 resolver
  (`recovered/dice-system.md`), the ambient idle picks and the RandomSound scheduler draw from named
  `FRandomStream`s whose state is in the `Session` block. `FMath::Rand()` is banned in gameplay code
  for the same reason `FTimerManager` is.
  *(`script_api.md` leaves open whether VtMB's `OneOfSet` counter is a per-call RNG or a frame
  counter; whichever it resolves to, ours is a named stream so the answer is a seeding decision, not
  an architecture change.)*
- **Iteration order is stable.** Saving walks entities by index and fields by registration order, so
  two saves of the same state are byte-identical — which is what makes the round-trip test in §10 a
  digest comparison rather than a semantic diff.

## 9. Slots, autosave, and the surfaces that trigger them

| Kind | Trigger | Slot |
|---|---|---|
| Manual | pause menu → Save Game (8.6's item exists, disabled) | `Elysium-NNN` |
| Quick | `save quick` — a registered command, so the key binding is the player's (`controls.md`) | `Quick` |
| Auto | `trigger_autosave` (a live class since 4.5, currently inert), map travel, and a chargen/act boundary | `Auto0..4`, a rotating ring like VtMB's |

`UElysiumSaveSubsystem` owns the slot list, the ring, and the async write. **Writing is off the game
thread**: freeze to a payload synchronously (it is a memory walk, and it must be atomic with respect
to the frame), then compress and write asynchronously via `AsyncSaveGameToSlot`. Loading always goes
through `UElysiumGameFlowSubsystem::LoadGame` → `Loading` state → travel, so there is exactly one
restore path and it is the one travel already uses.

**A save is refused, loudly, rather than written wrong** — mid-travel, during a choreographed scene
with no safe resume point, or when the payload version cannot be produced. VtMB refuses saves in
similar states; a broken slot is worse than a missing one.

## 10. Verification

Persistence is the system where a silent bug surfaces hours later, so it gets the strongest harness in
the project:

- **Substrate tier** — round-trip a bare `FElysiumEntityWorld`: build, mutate through the real
  chokepoints, freeze, rebuild, apply, and assert the two digests match. No RHI, no content; possible
  because of `runtime-architecture.md` §7's injected services.
- **Substrate tier** — schema tests: a field added to a base class still loads an older payload; an
  unknown field name in a payload is skipped with a warning, not a failure; a version bump below the
  supported floor is rejected with a readable message.
- **Content tier** — freeze/thaw every exported map's real `.ents` world and assert entity counts,
  name indices and queue contents survive.
- **Play tier** (`runtime-architecture.md` §12) — the one that matters: run a beat script to step *N*,
  save, load, run the *same* script from *N* to the end, and assert the same beats fire. This is what
  turns "save/load works" from a claim into a gate, and it reuses the harness the tutorial acceptance
  already needs.
- **A `elysium.save.diff` verb** — dump a payload as readable name/value lines and diff two of them,
  so a "why did this not persist" investigation is a text diff rather than a debugger session.

## 11. Open

- **Thumbnail capture.** The load menu wants a per-slot image; `ElysiumScreenshot::Request` already
  produces one, but capturing at save time costs a frame. Defer until the load screen exists (9.5's
  UI half), and store nothing rather than store the wrong frame.
- **Decals as save state.** VtMB persists bullet holes and blood — 212 in a well-played Santa Monica
  hub — and nothing models them yet. The `Maps` block reserves the slot; the system that fills it is
  the combat-impact task (10.7).
- **Snapshot size.** A full session holds one snapshot per visited map; VtMB's equivalent runs to a
  few MB across six maps. Measure at the first ten-map run, and only then consider per-map
  compression or dropping unmodified entities (the zero-omission rule already drops most of them).
