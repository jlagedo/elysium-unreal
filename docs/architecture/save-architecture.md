# Save architecture — the persistence design

How Elysium saves and restores a run. The VtMB-facts counterpart is **`docs/vtmb/savegame_format.md`** — the
`.sav` container, the five block handlers, and the exact inventory of state the original persists;
this doc owns the Unreal side: the container we write, the block model, how state is enumerated, and
what makes a save deterministic. The pairing is the same as `docs/vtmb/controls.md` ↔ `docs/architecture/input-architecture.md`.

Retail `.sav` **import is a non-goal** (RE7, parked). What is reproduced is the *shape* of what a save
must hold, because that shape is a property of the game, not of the file format.

---

## 1. The five things a save must hold

Read off `docs/vtmb/savegame_format.md`, mapped onto the objects in `docs/architecture/runtime-architecture.md`:

| Tier | VtMB | Elysium home | Block |
|---|---|---|---|
| slot metadata | the `.sav` global stream | `UElysiumSaveSubsystem` | header (uncompressed, readable without loading) |
| session state | `G` + `G.morgue` (pickled per map), quests, elapsed time | `FElysiumSessionBlock` | `Session` |
| the player | 277 datamap fields on the player entity + inventory as owned entities | `FElysiumPlayerRecord` | `Player` |
| per-map frozen world | `.HL1`/`.HL2`/`.HL3` per visited map | `FElysiumMapSnapshot` per visited map | `Maps` |
| in-flight time | `EventQueue` + think times | the substrate's own queue + per-entity next-think | inside each `Maps` entry |

Two structural choices of VtMB's are kept because they are load-bearing, and one is dropped:

- **Keep entity-as-inventory.** An item is a full entity with a datamap and script hooks; its
  condition, ammo and state persist for free, and `+use` semantics stay uniform. This is why
  `docs/architecture/runtime-architecture.md` §5 makes the player an entity — inventory persistence *is* entity
  persistence.
- **Keep the per-map snapshot model.** A save holds the current map plus a frozen snapshot of every
  other map visited this run, so walking back into Santa Monica finds it as you left it. It is also
  exactly what the adopted OpenLevel hard-travel lifecycle produces at every travel boundary.
- **Drop the 16383-slot symbol table.** It exists because Source interned datamap field names at
  runtime; we name fields directly. **Omission is kept and generalised**: instead of comparing each
  field against zero, a snapshot diffs each entity against a **post-Load baseline** — what a fresh
  build of the same `.ents` produces after Construct, Spawn and PostSpawn (§4). It is most of why
  these files are small, and it costs one field walk per map load plus one comparison per field.

## 2. The container

```
UElysiumSaveGame : USaveGame          // the only UPROPERTY-reflected part
{
    // the header, as loose UPROPERTYs ahead of the payload so a slot lists without inflating
    int32 PayloadVersion;  FString Map, Label, ClanName;  int32 Clan;
    double PlaytimeSeconds;  FDateTime Timestamp;  FString Kind;   // "manual" | "quick" | "auto"

    TArray<uint8> Payload;            // our own versioned, compressed block stream
};
```

`FElysiumSaveHeaderData` is the same set as a plain struct, which is what `ReadSlotHeader` hands the
load menu. There is no thumbnail (§11).

`USaveGame` buys slot management, platform-safe paths and `UGameplayStatics::AsyncSaveGameToSlot`
without owning the content. The payload is written by `FElysiumSaveArchive` (an `FArchive` wrapper),
because **none of the game state is UPROPERTY-reflected** and none of it should become so: the
substrate is plain C++ precisely so that serialization, determinism and travel teardown stay in our
hands (`docs/architecture/engine-core.md` R1).

- **Versioning** is an engine custom version (`FCustomVersionRegistration` + `Ar.UsingCustomVersion`),
  so a payload carries its own schema id and a reader can branch. A save from an older version either
  loads through declared upgrade paths or is rejected with a readable reason — never silently
  half-read. A raw memory archive carries no custom-version container of its own, so the payload
  opens with a short **uncompressed prologue** — `{'ELYS', version, MinSupported, uncompressed size}`
  — read before anything is inflated. The four refusals are: not an Elysium payload (magic),
  below the floor, from a newer build, and failed decompression.
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
Player    FElysiumPlayerRecord — name · sheet (base+current) · money · humanity · blood · masquerade
          · law counters + timers · quest journal + the log's hub tab · XP ledger · effects
          · email flags · inventory as item records · equipped handles
Maps      per visited map:  entity states · absent-entity set · event queue · think times
          · lightstyle phase · decals · physics poses · NPC memory (when AI exists)
World     the visited-map graph: landmark adjacency, the current map + player placement
```

`Session` and `Player` are the session record (`docs/architecture/runtime-architecture.md` §1) written straight out.
`World` is the level-connection/landmark table (VtMB's `ADJACENCY`), which is what
`docs/architecture/map-architecture.md`'s OpenLevel travel model needs to place the player on arrival. `Maps` is the
interesting one.

## 4. Enumeration is a walk, not a list (S9)

Adding save support to a class must not mean editing a save function. `docs/architecture/engine-core.md` R2 already
gives every registered class a **field table** used by I/O, the Python datamap walk, keyvalue
application and the inspector; persistence is the fifth consumer.

```cpp
// FElysiumClassDesc field registration carries one flag word; the default is Key | Save
D.Field(TEXT("m_iHealth"), &FElysiumCombatCharacter::Health, EElysiumField::Save);
D.Field(TEXT("skin"),      &FElysiumProp::Skin);   // ElysiumFieldDefault
```

`FElysiumClassRegistry::SaveFields` is the walk: derived→base over the class chain, first
registration of a name wins, `Save`-flagged and round-trippable (both a getter and a setter) only,
sorted lexically so the order is stable (§8). Saving an entity emits every one of those whose value
differs from the baseline, tagged by name. Restoring is the mirror, matched **by name, never by
position**, so a field added to a base class does not invalidate existing saves; an unrecognised or
no-longer-`Save` name is skipped with a warning and the rest of the record still applies. That is
VtMB's own property — its saves are self-describing datamap dumps — and it is what makes
9.4/9.8/9.9/9.10 free: a system that registers its state as fields is saved the day it lands.

**The baseline is post-Load, not zero.** A per-world `TArray<FElysiumEntityState>` captures every
entity once, at the end of the map build (after Construct, Spawn and PostSpawn) and for a runtime
entity at the end of its spawn; a restore never updates it. Comparing against a *fresh-constructed*
default would be wrong in both directions: `Origin` is not a registered field at all (Construct
applies every key that has one, and the def's `origin` key is still the raw Source-space string), and
an entity whose `Spawn()` arms `NextThink` and whose first think disarms it back to `NEVER` would
compare equal to a default and be omitted — so a restored map would re-run every `logic_auto`
ignition. Per-entity state outside the field walk (origin, `bDead`/`bHidden`, `NextThink` and the
`ScriptUnhide` think, the remaining output fire counts) is captured beside the fields and diffed the
same way.

The cost is one `FElysiumEntityState` per entity in memory. The payoff is measured in §11.

Three things do *not* fit the field walk and get explicit hooks:

| State | Hook |
|---|---|
| a leaf's derived runtime state (a mover's phase, a conversation cursor) | `virtual void FElysiumEntity::Serialize(FElysiumSaveArchive&)`, called after the field walk; the blob is dropped from the record when it matches the baseline's |
| bodies (meshes, MIDs, cables, constraints) | **not saved**. Bodies are disposable presentation (R1); they rebuild from the def + the restored entity state |
| anything an engine object owns (timers, tick handles, actor transforms) | **not saved**, and nothing game-visible is allowed to live there (R4/S1) |

**A leaf blob carries the schema it was written at.** That blob is opaque to the payload — the
entity is written through its own `Serialize` into a private memory archive and the bytes are
stored — and a raw memory archive has no version container. A leaf's `Ar.Version()` gate is
therefore meaningful only if the version the blob was *written* at travels with it, so
`FElysiumMapSnapshot` records it and replay honours it: a snapshot frozen in memory stamps
`Latest`, because that is what capture writes at, and a snapshot read from a file older than the
field defaults to **that file's own version**, which is exact — every blob in it was written by
the build that wrote the file. Without that, replaying an old blob through a `Latest` archive
reads fields the writer never emitted and byte-shifts everything after them.

It is also what makes a leaf-block addition additive instead of a floor raise: a field added
behind its own version does not force `MinSupported` up, because an older blob is replayed
through gates that answer for the build that wrote it. `Latest` is **27** — `WeaponAnimEvent`
(26) records which route a staged weapon transaction is waiting on, its own clip's sequence event
or the queued contact estimate; `WeaponSwingClipOwner` (27) appends the bank that owns that
swing's resolved clip. A pre-27 payload restores a swing with no clip owner, and its blocked
reaction resolves identically off the attacking body's stem and the swing's clip label — only the
diagnostic line loses the bank name.

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
`AbsentEntities`, produced at freeze time from `virtual bool FElysiumEntity::TravelsWithPlayer()` —
the entity's own answer, so the inventory task fills the set by overriding one predicate. The player
itself is neither recorded nor listed: it is the `Player` block's, and the map build spawns it.
Without the set, walking back into a map re-materialises the entire inventory — the failure mode the
original explicitly solves.

**Identity is the def index.** `FElysiumEntityHandle`'s index is the entity's position in the `.ents`
array — stable across runs, never reused within a map load (R3) — so it is also the save id, with no
extra id space. Runtime-spawned entities (`npc_maker.Spawn`, `CreateEntityNoSpawn`) live past the def
array and serialize their synthesized def alongside their state, so they restore as themselves: the
apply is two passes, one that re-creates them in index order before anything is written, one that
writes every record. Index alignment holds because the def array always fills `0..DefCount-1`, the
player always takes `DefCount`, and runtime entities replay in append order; a `DefCount` mismatch or
a per-record classname mismatch is logged and the record skipped rather than mis-applied.

The absent set is applied **after** the records, so a stale record for the same index cannot
resurrect an entity the set says is gone.

## 6. The event queue and think times

VtMB saves both, and the reason is visible in its own data: a `logic_relay` mid-delay, a
`ScheduleTask`'d Python source string, and a discipline cooldown are all pending entries on the one
queue (`docs/vtmb/savegame_format.md` → `EventQueue`). A save that dropped them would silently break the
tutorial's beat machine.

This is why R4 bans `FTimerManager`: our queue is `TArray<FElysiumIOEvent>` of plain data —
`{FireTime, Target, Input, Param, PythonSrc, Activator, Caller, Serial}` — and serializing it is
writing the array out and back. **Times need no rebasing**: the clock is part of the `Session` block,
so a load resets it to the saved value and every absolute time still means what it meant. The
restored entries keep their saved serials and the queue's next-serial counter is restored with them,
so a same-time ordering survives too. Per-entity `NextThink` rides along in the entity record.

The one subtlety: **handles inside events**. `Activator`/`Caller` carry an epoch that will not match
the restored world's. They serialize as `{index, bWasValid}` and re-resolve against the new epoch at
load; an entity that no longer exists resolves to Invalid, which is the same falsy value a killed
entity already produces and which every call site already handles. The re-stamping is the *world's*,
not the archive's — the archive is a byte layer with no world to ask, so `ApplySnapshot` walks the
restored queue and rebases each handle itself.

## 7. `G`, the morgue, and the script VM

`G` and `G.morgue` are the story layer — 1,226 distinct flags across 3,782 assignments, and 208 of the
345 flags level scripts read are written only by `.dlg` files (`docs/vtmb/savegame_format.md`,
`docs/vtmb/python_bridge.md`). They already live in C++ (`FElysiumGlobalMap`, case-sensitive, default-0 on
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
entity is mechanically dead in a snapshot (`docs/vtmb/savegame_format.md`), so it is to be saved as written and
never reconciled against entity state. **Nothing models it yet** — no runtime surface writes or reads
a morgue — so the `Session` block carries `G` and the quest map only. When the morgue lands it is a
key in `G` like any other and needs no block change.

## 8. Determinism

A save is only worth as much as the run that follows it, so three rules hold on every system:

- **All game-visible time comes from `FElysiumGameClock`** (S1). A system that reads
  `FPlatformTime::Seconds` or `World->GetTimeSeconds` for gameplay is a save bug waiting to surface.
- **RNG is an owned, seeded stream.** `ElysiumRng` owns one `FRandomStream` per named stream —
  `OneOfSet` (589 dialogue gates), `LogicTimer`, `LogicCase`, `Dice` (`recovered/dice-system.md`),
  `Ambient`, `Reaction` (the damage flinch's head/torso coin, its ±30° jitter and its
  weighted-sequence draw) — each seeded off one session seed by a fixed hash, so a new game seeds
  once and every stream follows. Their state is in the `Session` block: each stream saves
  `{initial, current}` and restores by re-initialising to the saved *current* value, which is the
  only writer `FRandomStream`
  exposes and which continues the sequence exactly. `FMath::Rand()` is banned in gameplay code for
  the same reason `FTimerManager` is.
  *(`docs/vtmb/script_api.md` leaves open whether VtMB's `OneOfSet` counter is a per-call RNG or a frame
  counter; whichever it resolves to, ours is a named stream so the answer is a seeding decision, not
  an architecture change.)*
- **Iteration order is stable.** Saving walks entities by index and fields in sorted name order; the
  `Maps` map, `G` and the quest map serialize as key-sorted arrays rather than straight out of their
  hash containers. Two saves of the same state are byte-identical — which is what makes the
  round-trip test in §10 a digest comparison rather than a semantic diff.

## 9. Slots, autosave, and the surfaces that trigger them

| Kind | Trigger | Slot |
|---|---|---|
| Manual | pause menu → Save Game (8.6's item exists, disabled) | the requested name, or the next free `Elysium-NNN` |
| Quick | `save` — a registered command, so the key binding is the player's (`docs/vtmb/controls.md`) | `Quick` |
| Auto | `trigger_autosave`, and a chargen/act boundary | `Auto0..4`, a rotating ring like VtMB's |

`UElysiumSaveSubsystem` owns the slot list, the ring, and the async write. **Writing is off the game
thread**: freeze to a payload synchronously (it is a memory walk, and it must be atomic with respect
to the frame), then compress and write asynchronously via `AsyncSaveGameToSlot`. Loading always goes
through `UElysiumGameFlowSubsystem::LoadGame` → `Loading` state → travel, so there is exactly one
restore path and it is the one travel already uses.

**A save is refused, loudly, rather than written wrong.** `CanSave` returns the reason as text, and
`elysium.save.cansave` prints it. The refusals: no session running, a map load in flight, the menu
backdrop is the world, no entity world or no player, an open sign panel, an open conversation, a
computer-terminal session, a choreographed scene, a scripted sequence, a paired/feed action, or an
authored legacy/Sequencer camera track. Dialogue, terminal and scripted sessions may own branch
cursors, local input, body-owner tokens, transient animation state, and camera handles that the
payload deliberately does not model. Ordinary idle, locomotion, combat, and reaction clips remain
saveable. A broken slot is worse than a missing one. The terminal's durable state serializes after
its transient session ends (`docs/architecture/computer-terminal-architecture.md`).

Loading checks the target map is exported **before** it touches the session, so a payload naming a
map this install cannot build fails with the session intact rather than half-torn-down.

## 10. Verification

Persistence is the system where a silent bug surfaces hours later, so it gets the strongest harness in
the project:

- **`Elysium.Substrate.SaveRoundTrip`** — round-trip a bare `FElysiumEntityWorld`: build, mutate
  through the real chokepoints, hide an entity, spawn a runtime one, leave a delayed event pending,
  freeze, rebuild, apply, assert each of those came back, and assert the two digests match. Then the
  absent-entity case. No RHI, no content; possible because of `docs/architecture/runtime-architecture.md` §7's
  injected services.
- **`Elysium.Substrate.SavePayload`** — the container: every block populated, write → peek version →
  read, two writes byte-identical, the four integrity refusals, and `Describe`'s readable lines.
- **`Elysium.Substrate.SaveSchema`** — the field walk reaches leaf and base names, sorted and
  duplicate-free; a non-save field is in neither the key nor the save set; an unknown field name is
  skipped and the rest of the record still applies; a classname mismatch skips the record; a restored
  RNG stream continues the saved sequence.
- **`Elysium.Content.MapSnapshot`** — freeze/thaw every exported map's real `.ents` world through the
  real container and assert entity counts, name indices and queue contents survive, with a byte
  digest and a `Describe` diff printed on mismatch. It also reports the snapshot sizes.
- **Play tier** (`docs/architecture/runtime-architecture.md` §12) — the one that matters: run a beat script to step *N*,
  save, load, run the *same* script from *N* to the end, and assert the same beats fire. This is what
  turns "save/load works" from a claim into a gate, and it reuses the harness the tutorial acceptance
  already needs.
- **A `elysium.save.diff` verb** — dump a payload as readable name/value lines and diff two of them,
  so a "why did this not persist" investigation is a text diff rather than a debugger session. Either
  side may be `live`, which builds the payload the current session would write. Its neighbours:
  `elysium.save.slots`, `elysium.save.cansave`, `elysium.save.delete`.

## 11. Open

- **Thumbnail capture.** The load menu wants a per-slot image; `ElysiumScreenshot::Request` already
  produces one, but capturing at save time costs a frame. Defer until the load screen exists (9.5's
  UI half), and store nothing rather than store the wrong frame.
- **Decals as save state.** VtMB persists bullet holes and blood — 212 in a well-played Santa Monica
  hub — and nothing models them yet. The `Maps` block reserves the slot; the system that fills it is
  the combat-impact task (10.7).
- **`G.morgue`.** Nothing writes or reads it yet (§7).
- **Snapshot size — measured, and not a problem.** A freeze of a just-loaded map records 58 of 1,869
  entities on `sp_tutorial_1` (1.7 KB compressed), 13 of 469 on `sm_pawnshop_1` (0.6 KB) and 67 of
  2,598 on `sm_hub_1` (2.0 KB). A played map records more, but the baseline diff keeps the growth
  proportional to what the player actually changed. Re-measure at the first ten-map run.
