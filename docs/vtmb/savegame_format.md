# VtMB savegames — container format, on-disk structures, and the game state they hold

What a *Vampire: The Masquerade – Bloodlines* `.sav` file contains, how every byte maps back
to the engine's `CSaveRestore` machinery and the Source datamap, and the inventory of
**game state a rebuild must persist**.

**Status: verified by decoding.** Every structure below was read out of real save files with
`pipeline/src/elysium_pipeline/formats/sav.py`; the field names are the game's own datamap strings, carried inside the save's
symbol table, not reconstructions. Class names come from MSVC RTTI in `Vampire/dlls/vampire.dll`
(`research/tooling/ghidra/driver/run.ps1 -Script DumpGrep -ScriptArgs "cls=SaveRestore listcls=1"`). Open items are
listed at the end. The dated witness audit, port comparison and implementation requirements are
in `docs/specs/0011-save-game/spec.md`; that document is not a complete datamap type dictionary.

Related: `docs/vtmb/python_bridge.md` (the CPython embedding and `G`), `docs/vtmb/entity_io.md` (the datamap and the
I/O bus), `docs/vtmb/level_transitions.md` (the three spawn mechanisms), `docs/vtmb/vdata-catalog.md` (the rulebook tables the sheet indexes).

## Where saves live

`<install>/Unofficial_Patch/save/` on a patched install, `<install>/Vampire/save/` on retail —
the patch's save directory shadows retail exactly like every other asset class
(`pipeline/CLAUDE.md` → *Asset resolution*).

| File | Role |
|---|---|
| `Vampire-000.sav` … | manual save slots, allocated in order |
| `autosave.sav`, `autosave01…04.sav` | the rotating autosave ring |
| `Vampire-999.sav` | a **zero-byte** placeholder slot shipped with the game |

Saves are **not** game-install-derived content — they are the player's own. They are still not
committed here: the copies used for this analysis live under the gitignored `$ELYSIUM_WORK_ROOT/research/saves/`.

## Container: the `.sav` file

Little-endian throughout. One save file holds the **whole world**: the current map plus a frozen
snapshot of every other map the player has visited this playthrough.

```
'JSAV'                     magic
int      version           117 (0x75) for VtMB
int      dataSize          bytes of the global field stream below
int      tokenCount        symbol-table slots — always 16383
int      tokenSize         bytes of the symbol blob
char[tokenSize]            tokenCount NUL-terminated slots ('' = unused slot)
byte[dataSize]             CSave field stream: GameHeader + GLOBAL
section*                   mapCount sections, ASCII-sorted by name
```

A **section** is one per-map state file, embedded whole:

```
char  name[260]            e.g. "sm_hub_1.HL1"; MAX_OSPATH, NUL-terminated,
                           padded with uninitialised stack garbage
int   rawLen               total inflated size
chunk*                     until rawLen bytes have been inflated
    chunk := int compLen; byte zlib[compLen]      each chunk inflates to <= 512 KiB
```

**The zlib chunking is a Troika change.** Stock Source 2004 embeds these sections uncompressed;
VtMB deflates each one (`Bin/zlib1.dll`), splitting anything over 512 KiB into consecutive
independent zlib streams. A 1.35 MB `sm_hub_1.HL1` ships as three streams and compresses to
about 17 % of its size. A reader must loop on `rawLen`, not assume one stream — the single
biggest trap in the format.

### The global field stream

Six fields, and they are the entire save-slot header:

| Field | Type | Meaning |
|---|---|---|
| `GameHeader` | int | field count of the group that follows (4) |
| `mapCount` | int | number of embedded sections = *maps visited* × 3 |
| `mapName` | char[32] | the map to load on restore |
| `comment` | char[80] | `maps/<map>.bsp` + elapsed play time `MM:SS`, column-padded |
| `userName` | char[80] | the label the load menu shows — `"Santa Monica (The Asylum)"`, `"Autosave"` |
| `GLOBAL` | int | `CGlobalState` entry count — **always 0 in VtMB** |

`userName` is a VtMB addition to Source's `GAME_HEADER`. `GLOBAL` being permanently zero is the
structural tell that **VtMB does not use Source's global-entity state at all**: the persistent
story layer lives in the Python block instead (see below).

## The field-stream primitive

Everything inside a save — headers, entity data, block tables — is the same flat record stream
that `CSave::BufferField` emits:

```
short size;       // bytes of data
short token;      // index into the symbol table
byte  data[size];
```

Three rules govern reading it:

- **Names come from the symbol table, not the schema.** `token` indexes a 16383-slot sparse hash
  table written at the head of the file. Slot *i* is the *i*-th NUL-terminated run in the blob;
  unused slots are a bare NUL. A whole save typically uses a few hundred to ~1000 slots, so the
  table is ~34 KB of mostly zeros — which is why it compresses to nothing.
- **Zero-valued fields are omitted.** The writer skips any field whose bytes are all zero, so a
  group's field set is sparse and a reader must match by name, never by position. This is why
  `worldspawn` has no `id` field (its id is 0) and why the `Entities` block header carries no
  `locBody` (it is 0).
- **A group is `int count` then `count` records.** Every repeated structure — the entity table,
  the block table, the player's quest list — is prefixed by its element count, and each element
  starts with a record whose *name is the structure's own name* and whose value is that element's
  field count (`ETABLE`, `SaveRestoreBlockHeader_t`, `ASSIGNED_QUEST`, `DECALLIST`, `LIGHTSTYLE`).

Field names are literal Source datamap `externalName` strings — `m_iHealth`, `m_vecAbsOrigin`,
`m_hActiveWeapon` — including per-element array names, which VtMB spells out one slot at a time:
`m_iVAttributesBase[ v_attribute_strength ]`, `m_AnimOverlay[0].m_flCycle`. A save is therefore a
**self-describing dump of the datamap**, and reading one requires no header at all. That property
makes the format tractable: 1522 distinct field names appear across a single mid-game save, all
spelled out.

## Sections: `.HL1`, `.HL2`, `.HL3`

Each visited map contributes three sections. Both `.HL1` and `.HL2` open with `'VALV'` and a
version, but the **server and client write different headers** — the token count (always 16383)
is what identifies which layout you have.

### `.HL1` — server state (the big one)

```
'VALV'                     magic
int   version              117
int   tokenSize
int   tokenCount           16383
int   headersSize          block-handler header area (counts its own leading length int)
int   dataSize             block-handler body area
char[tokenSize]            symbol table
byte[headersSize]          headers area
byte[dataSize]             body area
```

The headers area starts `int selfSize; int bodySpan; int blockCount;` then the block table.
`bodySpan` is the body length **measured from the block set's own start** — Source's
`baseFilePos`, which the `ISaveRestoreBlockSet` interface passes around explicitly. So:

```
baseFilePos = dataSize - bodySpan
```

and every `locBody` in the block table is relative to `baseFilePos`, while every `locHeader` is
relative to the headers area start. `baseFilePos` is exactly where the global preamble ends and
the first entity begins — verified against all six maps in a save: `baseFilePos` equals the first
entity's `location` in every one.

### The global preamble (body `[0, baseFilePos)`)

Written by the server before it hands the buffer to the block set. Stock Source, unchanged:

| Group | Fields |
|---|---|
| `Save Header` | `skillLevel`, `connectionCount`, `lightStyleCount`, `time` (map-local game time), `mapName[32]`, `skyName[32]` |
| `ADJACENCY` × `connectionCount` | `mapName[32]`, `landmarkName[32]`, `pentLandmark` (the landmark entity's save id), `vecLandmarkOrigin` (Vector) |
| `LIGHTSTYLE` × `lightStyleCount` | `index`, `style[64]` (the `"mmnmmommommnonmmonqnmmo"` animation string) |

`ADJACENCY` is the **level-transition graph as the save sees it**: which maps this one connects
to and through which landmark. It is what makes a `trigger_changelevel` land the player at the
right offset in the destination — the same landmark mechanism `docs/vtmb/level_transitions.md` describes,
persisted.

### `.HL2` — client state

```
'VALV'                     magic
int   version              117
int   dataSize             client block-set body
int   headersSize          client block-set headers
int   decalSize            the decal list that follows the body
int   tokenSize
int   decalCount
int   tokenCount           16383
char[tokenSize]            symbol table
byte[headersSize]          headers (the client ENTITYTABLE)
byte[dataSize]             body
byte[decalSize]            decalCount DECALLIST records
int   len; char name[len]  trailer: the player's name ("Noa")
```

The client section registers exactly **one** block, `Entities`, whose table mirrors the server's
by `saveentityindex` and whose element datamap is `C_BaseEntity`. `baseFilePos` is 0 here — the
client writes no preamble.

Each `DECALLIST` group holds four fields in datamap order:

| Field | Size | Meaning |
|---|---|---|
| `position` | 12 | Vector — world space for a world decal, **entity-local** when `entityIndex` is set |
| `name` | 128 | material path — `decals/structural/parkingdsan`, `decals/signs/number7` |
| `entityIndex` | 2 | `short` — the `saveentityindex` of the entity the decal is stuck to |
| `flags` | 1 | `1` on every map-authored decal, `4` on every runtime impact decal (`decals/hits/*`) |

So a world decal is **161 bytes** (`8 + 16 + 132 + 5`) and an entity-attached one **167**. That
reproduces every map's decal region to the byte: `210×161 + 2×167 = 34144` for the Santa Monica
hub, and likewise for all five maps that have decals.

`entityIndex` is absent on world decals purely because of the **zero-omission rule** — the world
is entity index 0, so the field is all-zero and the writer skips it. (Data alone cannot separate
"omitted because zero" from "written only when attached" — the bytes are identical either way —
but zero-omission needs no extra rule and holds everywhere else in the format.)

Every attached decal found resolves to a **brush entity**: `func_door_rotating` with a `*N` model,
carrying signage — `decals/signs/number7` on a pawnshop door, `signs/exit` in the theatre,
`decals/signs/thomas` on a hub shopfront. That is what the attachment is *for*: the decal rides
the door when it swings, which is why its position is stored local (a pawnshop sign at
`(1.5625, -21, 24)` on a door whose world origin is `(-2084.5, -2559, 199)`).

A well-played Santa Monica hub carries 212 decals. **Bullet holes, blood splatter, and
map-authored signage are savegame state**, and they are the only thing in the client section that
is not derivable from the server section.

### `.HL3` — the transition list

The whole file:

```
int count; int ids[count]
```

These are `id` values in that map's `.HL1` entity table, and they name the entities that
**travelled out of that map with the player**: the player himself plus every carried item. The
current map's `.HL3` is always empty — nothing has left it yet (verified across all nine saves
inspected).

Every listed entity carries `FENTTABLE_PLAYER (0x80000000)` or `FENTTABLE_MOVEABLE (0x20000000)`
in its table flags, but the list is a **subset** of the flagged set, not equal to it: `MOVEABLE`
marks *eligible to cross a transition*, which is true of every NPC's own `item_w_unarmed` entity
too. `sp_genesisdevice_1` flags 117 entities moveable and lists 3 in `.HL3` — the player and the
two unarmed weapons he actually carried out. Where the player is the only armed character in a
map, the two sets coincide, which is the common case.

On restore the engine skips the listed entities, because they now live in whichever map the
player is standing in. Without this list, walking back into Santa Monica would duplicate the
entire inventory.

## The five save-restore blocks

Both section kinds carry a block table — a `CUtlVector<SaveRestoreBlockHeader_t>` written through
the generic vector ops, hence the `uv` (element count) / `elems` (element group) field names:

```
SaveRestoreBlockHeader_t { char szName[32]; int locHeader; int locBody; }
```

`.HL1` registers five handlers, each a class recovered from RTTI in `vampire.dll`:

| Block | Handler class | What it holds |
|---|---|---|
| `Entities` | `CEntitySaveRestoreBlockHandler` | the entity table (header) + every entity's field stream (body) |
| `EventQueue` | `CEQ_SaveRestoreBlockHandler` | pending delayed I/O events |
| `Physics` | `CPhysSaveRestoreBlockHandler` | VPhysics object state, keyed by entity + field name |
| `AI` | `CAI_SaveRestoreBlockHandler` | NPC memory, patrol paths, interesting-place markers |
| `Python` | **`CPython_SaveRestoreBlockHandler`** | the pickled script namespaces — **VtMB-only** |

Supporting custom field ops, also from RTTI: `CPyObjStrSaveRestoreDataOps` (marshals a `PyObject`
to a string), `CVTSDependencySaveRestoreDataOps`, `CAI_MemoryListSaveRestoreOps`,
`CAI_NPCPatrolPathSaveRestoreOps`, `CAI_InterestingPlaceMarkersSaveRestoreOps`,
`CBitStringSaveRestoreOps<CBitString>`, and eight `CSafeDiscSaveRestoreOps_0…7` stubs left by the
copy-protection wrapper.

### `EventQueue`

One record per pending event, each a straight serialisation of an entity-I/O firing in flight:

```
PEvent { m_flFireTime, m_iTarget, m_iTargetInput, m_pActivator, m_pCaller, ... }
```

e.g. `m_flFireTime 4.969  m_iTarget "streetlight_state_yellow_3"  m_iTargetInput "Trigger"`.
Everything a `logic_relay` scheduled with a delay, mid-flight, survives the save — target,
input, activator, caller, and the remaining time. The event queue also carries **Python** entries
(the engine's event-type enum is `Entity` / `Python` / one more), which is how `ScheduleTask`'s
deferred source strings persist.

### `Physics`

`PhysObjectHeader_t { type, hEntity, fieldName }` — the entity handle plus the *name of the
datamap field* the physics object hangs off (`m_pPhysicsObject`), then the vphysics blob. Ragdolls
and thrown props keep their pose and velocity.

### `Python` — the script layer

A chain of records, outside the field-stream convention: `byte tag; int len; byte pickle[len]`,
each a **CPython 2.1 protocol-0 pickle** of a dictionary. Most flag values are integers, but
lists and strings also occur; it is not restricted to `str -> int`. An integer example is
`(dp1\nS'Tut_Elev'\np2\nI1\ns…`. The two dicts are `G` and `G.morgue`; the block handler, its
vftable, and the full `G`/morgue mechanism belong to `docs/vtmb/python_bridge.md` ("`G` is the save unit").
What follows is specific to the decoded saves themselves, written flag-dict first:

1. **`G`** — the global story namespace. A mid-tutorial save carries `Jack_Faction: 2`,
   `In_Downtown: 0`, `Tut_Elev: 1`, `Story_State: -5` among 56 keys, growing as the playthrough
   advances.
2. **`G.morgue`** — the dead-character registry, keyed by NPC targetname → `1`: `Sire2`,
   `thug_1`, `stealth_victim`, `sabbat_redshirt_3`, `bum`, `rat_2` in that same save. Because
   entries are written by script and dialogue rather than by an engine death hook, a morgue entry
   does not imply the entity is mechanically dead in that map's saved state: of the 15 names in a
   mid-game save, `thug_2`/`thug_3` are `m_lifeState 1`, while `bum` is alive at 46/100 health.

**Each map section carries its own frozen copy of both dicts** — the snapshot as of the last time
the player was in that map. The live values are whichever the current map's section holds.

## The entity model

### The table

`Entities.locHeader` points at `int count`, then `count` `ETABLE` groups:

| Field | Meaning |
|---|---|
| `id` | ordinal save id — the entity's identity within this map's save |
| `edictindex` | edict slot to force on restore; `-1` for anything but the player and world |
| `saveentityindex` | index at save time; how the client section cross-references |
| `location` | byte offset of this entity's field stream in the body area |
| `size` | byte length of that stream |
| `classname` | the spawn classname — `npc_VHumanCombatant`, `func_door`, `item_g_lockpick` |
| `flags_upper32` / `flags_lower32` | a 64-bit flags word, split in two |

The flags word is Source's `entitytable_t::flags` widened to 64 bits. Observed values, over a
1448-entity Santa Monica hub:

- `0x80000000` upper — `FENTTABLE_PLAYER`, exactly one entity.
- `0x20000000` upper — `FENTTABLE_MOVEABLE`, "may cross a transition": the 14 carried items here,
  but 117 in `sp_genesisdevice_1`, where every NPC's own unarmed-weapon entity qualifies.
- `0x7fffff` lower — the 23-bit level-connection mask, set on those same movable entities.
- Everything else: zero, hence omitted entirely.

### The data

Each entity's stream is its datamap walked base-class-first. Class boundaries are visible: a
record whose *name is a class name* and whose value is a count introduces that class's fields.
For the player:

```
CBaseEntity 41 · CBaseToggle 1 · CBaseAnimating 15 · CBaseAnimatingOverlay 0 ·
CBaseFlex 1 · CBaseCombatCharacter 139 · CBasePlayer 67 · CHL2_Player 5
```

277 fields on one entity. A whole save runs to 3816 saved entities across six maps and 131
distinct classnames — dominated by `env_sprite` (574), `ai_hint` (346), `prop_dynamic` (249),
`camera_keyframe` (187), `keyframe_rope` (185).

## What the player entity holds

All of it is on the `player` entity, almost all of it on `CBaseCombatCharacter` — Troika hung the
World-of-Darkness sheet on the combat-character base, not on the player class, which is why NPCs
share it.

**The sheet** (`vdata/` supplies the rulebook these index into — `docs/vtmb/vdata-catalog.md`):

- `m_iVAttributesBase[ v_attribute_* ]` / `m_iVAttributesCurrent[ … ]` — **35** slots each
  (`stats.txt`'s whole Attributes container; the writer omits the all-zero ones, which is why a
  save shows fewer): `attrib_order` at 0, the nine WoD attributes (strength … wits), then every
  derived and bookkeeping stat through `experience` at 34 — `clan`, `gender`, `bloodpool`,
  `bloodpool_max`, `faithpoints`, `health`, `health_aggravated_dmg`, `max_health`, `generation`,
  `armor_rating`, `level`, `frenzy_check_mod`, `soak_pool`, `humanity`, `masquerade`,
  `experience_modifier`, `starting_equipment`, `autolevel_template`, and the rest.
- `m_iVAbilitiesBase[ v_ability_* ]` / `…Current[ … ]` — talents/skills/knowledges;
  only the non-zero ones are written (`intimidate`, `subterfuge`, `stealth`, `investigation`,
  `academics`).
- `m_iVDisciplinesBase[ v_discipline_* ]` / `…Current[ … ]` — all 13 slots, `-1` for a discipline
  the clan cannot take, `0`+ for one it can: `animalism`, `auspex`, `blood_healing`, `celerity`,
  `corpus_vampirus`, `dementation`, `dominate`, `fortitude`, `obfuscate`, `potence`, `presence`,
  `protean`, `thaumaturgy`. `stats.txt`'s Disciplines container is **17** — the four Numina powers
  follow — so the file's slot space is wider than what the save writes.

The **Base/Current split is the whole buff system**: base is the character sheet, current is the
sheet plus every active modifier, and both are persisted.

**Progression and identity:** `m_iVHistoryID` (the chosen history), `m_iGender`, `m_iMoney`,
`m_iBloodStolen`, and:

- `m_QuestList` — `CUtlVector<ASSIGNED_QUEST { szTitle[48], idxQuestTable, idxState, iOrder }>`,
  stride **0x40** (`player+0x1D68`, count `+0x1D74`). The quest journal: name, quest, which state
  within it, display order. Two readings the field names invite and the code refuses
  (`docs/vtmb/game_runtime.md` → "Quests"): **`idxQuestTable` is the flat quest index across all five loaded
  `quests_*.txt`**, not the index of the file; and `idxState` is a **0-based ordinal into the
  quest's completion-state array in file order**, not the authored `"ID"`, which the loader never
  reads. The record also carries a **byte at `+0x3c`, set to 1 on every write** — the journal's
  unread marker — leaving `+0x3d..0x3f` as padding. Nothing in the image ever *reads* that byte, so
  what would clear it is unrecovered. Rows are matched by case-insensitive title and replaced in
  place, so one quest never holds two.
- `m_iCurrQuestLogArea` — `int` at `player+0x1dac`. The quest log's selected hub tab, saved with the
  character: the panel reads it, but it is the player's field, not the panel's.
- `m_ExpList` — `CUtlVector<EXPERIENCE_ENTRY { szTitle[48], nAmt }>`. The XP ledger, itemised by
  award, not a running total.
- `m_tEffectList` — a plain length-prefixed string list, outside the field-stream convention:
  `int count;` then `int len; char[len]` per entry — `"clan (tremere)"`,
  `"history (affinity for magic)"`. The active passive-modifier set.
- `m_GlobalEmailFlags` — `CUtlVector<GLOBAL_EMAIL { szEntityName[64], EmailFlags[512] }>` — per
  computer terminal (`haven_pc`), 128 ints of email state (512 bytes). Terminal-side semantics
  and the closed global-wins-on-entry / local-wins-on-exit reconciliation belong to
  `docs/vtmb/computer-terminals.md` §12.

**Masquerade and law** — the systems that make VtMB's world react: `m_LevelCriminalAct`,
`m_iCriminalActCount`, `m_iSupernaturalActCount`, `m_flCriminalActTimer`,
`m_flSupernaturalActTimer`, `m_flMasqueradeTimerNext`, `m_flSpawnResponseCopsTimer`,
`m_hSpawnResponseCopsNPC`.

**Combat and disciplines:** `m_iDisciplineFlags2`, `m_fDisciplineTimers[]` (per-discipline
cooldowns), `m_iDisciplineCastCounter`, `m_bitsObfuscateRules`, `m_hProteanTransformOther`,
`m_vDiscBloodType`, `m_flNextFeedPulse`, `m_flFeedStartTime`, `m_fCurrentRangedAccuracy`,
`m_aCurWpnActivity`.

**Stealth and light** — the lighting-driven visibility model, persisted per-limb:
`m_flLightOnFeet`, `m_flLightOnCenter`, `m_flLightOnHead`, `m_flLightOnMe`,
`m_flNextStealthUpdate`, `m_flStealthVisionScalar`, `m_flStealthVisionCone`,
`m_flStealthHearingDist`, `m_nNextLightPositionTest`. Their update and observer semantics are in
[stealth.md](stealth.md); restoring a sample triplet without its derived generation is invalid.

**Movement calibration:** six 8-float tables — `m_flRunForwardSpeeds`, `m_flRunSideSpeeds`,
`m_flWalkForwardSpeeds`, `m_flWalkSideSpeeds`, `m_flSneakForwardSpeeds`, `m_flSneakSideSpeeds` —
the per-direction speeds `docs/vtmb/source_movement.md` covers, saved rather than recomputed.

**Body and camera:** `m_ModelName`
(`models/character/pc/male/tremere/armor0/tremere_Male_Armor_0.mdl` — clan, gender and armour tier
are baked into the model path), `m_nSequence`, `m_flCycle`, `m_flPoseParameter[]`, `m_idxHeadBone`,
`m_vecHeadLocalForward`, `m_vEyeLookTarget`, `m_hCameraViewEntity`, `m_hCameraTargetEntity`.

### Inventory is entities, not a list

There is no independent array of copied item objects. Each carried item is a **full entity in the entity table** —
`item_g_lockpick`, `item_w_tire_iron`, `item_a_lt_cloth`, `item_g_bloodpack` — with
`m_hOwnerEntity` set to the player's save id and `FENTTABLE_MOVEABLE` in its table flags. The
player holds only handles into that set: `m_hMyWeapons[]`, `m_hActiveWeapon`, `m_hLastWeapon`,
`m_hLastMeleeWeapon`, `m_hActiveIArmor`, `m_pViewWeapon`, `m_hViewModel[]`.

Consequence for a rebuild: **inventory persistence is entity persistence**. An item's condition,
ammo, and script state ride along for free because the item *is* an entity with a datamap; but it
also means the save carries an `item_w_unarmed` entity 117 times across six maps, one per
character that can punch.

## What is *not* in the save

- **No world geometry, textures, or lighting.** The `.bsp` is reloaded and the save patches
  entity state onto it. `LIGHTSTYLE` strings persist; baked lightmaps do not.
- **No `CGlobalState`.** `GLOBAL` is 0 in every save inspected. Source's `env_global`/global-entity
  mechanism is unused; `G` replaces it.
- **No settings.** Key binds, video and audio options live in `cfg/*.cfg` (`docs/vtmb/controls.md`), not here.
- **No `vdata` values.** The sheet stores *indices* into the rulebook tables; the tables
  themselves are read fresh from `vdata/` at load. A patched rulebook therefore re-applies to an
  existing save.
- **No dialogue text or `.dlg` state** beyond what the scripts wrote into `G`.

## Tooling

`pipeline/src/elysium_pipeline/formats/sav.py` is the decoder (container, both section kinds, symbol table, field streams, block
table, entity table, decal list). `research/tooling/probes/probe_sav.py` is the CLI over it:

```
uv run elysium research probe_sav <file.sav>                   # container + section summary
uv run elysium research probe_sav <file.sav> --map sm_hub_1    # preamble, blocks, classname census
uv run elysium research probe_sav <file.sav> --entity player   # every field of matching entities
uv run elysium research probe_sav <file.sav> --python          # the pickled namespaces
uv run elysium research probe_sav <file.sav> --extract DIR     # inflated .HL1/.HL2/.HL3 sections
```

Both read a save file the user points them at and touch no game install. Pickles are loaded
through a restricted unpickler that refuses any class construction.

`research/tooling/probes/sav_to_json.py` decodes a whole save into one JSON document with every
byte accounted for (`uv run elysium research sav_to_json <file.sav> <out.json> [--audit]`);
`docs/specs/0011-save-game/save_example.json` is its output for a three-map tutorial save (`Vampire-015.sav`,
`sp_tutorial_1` current, `sp_theatre` and `sp_genesisdevice_1` frozen).

## Typing a record, and the raw writes between records

A record carries no type. The datamap does: VtMB's `fieldtype_t` has no `QUATERNION` or `TICK`
slot, so its numbering is `1 FLOAT, 2 STRING, 3 VECTOR, 4 INTEGER, 5 BOOLEAN, 6 SHORT,
7 CHARACTER, 8 COLOR32, 9 EMBEDDED, 10 CUSTOM, 11 CLASSPTR, 12 EHANDLE, 13 EDICT,
14 POSITION_VECTOR, 15 TIME, 16 MODELNAME, 17 SOUNDNAME, 18 INPUT, 19 FUNCTION` (the corpus
field ledger's `fieldType N` notes; `m_usSolidFlags` is the lone 6, `m_pfnScriptSavedThink` a 19).
Consequences for a reader:

- `EMBEDDED` and most `CUSTOM` payloads are a nested record stream opening with their own
  class header (`m_Collision` → `CCollisionProperty 7`, `m_Local` → `CPlayerLocalData`).
- `TIME` is written as `value − baseTime` (`CSave` slot 25, `101a0a80`), so every saved time
  is relative to `Save Header.time`; `1e11` is a "never" sentinel.
- `FUNCTION` is the handler's **name** — `"CChangeLevelTouchChangeLevel"` — not an address.
- `char[N]` fields are copied whole: bytes after the first NUL are uninitialised memory.

`ISave` has two families of write, and the save interleaves them. The **named** writes produce
records; the **unnamed** ones append raw bytes with no header. From `CSave`'s vftable
(`10476fbc`): `+0x08` WriteAll(obj, datamap), `+0x0c` WriteFields(name, …) (a `name, count`
header record then the fields), `+0x10`/`+0x18` StartBlock()/EndBlock() (a record with token 0
wrapping its content), `+0x20` raw `short`, `+0x28` raw `int[]`, `+0x30` raw bytes (slot 12,
`101a07b0`, the primitive under all of them), `+0x44` a named string record, `+0x64` raw time,
`+0x80` raw entity index, `+0x90` raw EHANDLE → save id (slot 36, `101a1080`).

### Block bodies

| Block | Retail `Save` | Body layout |
|---|---|---|
| `EventQueue` | `100cfee0` → `100cfd00` | `WriteFields("EventQueue")` (`m_iListCount`), then `WriteFields("PEvent")` per pending event |
| `Physics` | `10043c70` | per object `WriteAll(PhysObjectHeader_t)` + StartBlock(); per sub-object StartBlock(), raw vphysics object pointer, `vphysics_save_*` fields; EndBlock() |
| `AI` | `1030bfd0` | raw `short` squad count; per squad a token-0 string record (name) + `WriteAll(CAI_Squad)`; raw `short` count; per NPC raw EHANDLE + `WriteAll(CAI_Memory)` |
| `Python` | `1019adc0` | per namespace raw `bool` present, raw `int` length, cPickle protocol-0 dump (`G`, then `G.morgue`) |

Every block's header area opens with a raw `short` version (`WriteSaveHeaders`, slot 3):
EventQueue 1, Physics 5 (followed by `PhysBlockHeader_t`), AI 1, Python 1.

### Entity streams end with raw writes

`CBaseEntity::Save` (`100a9f70`, vtable slot 126) writes the datamap chain; an override may then
append raw data after the last class group:

- **Player** (`CBasePlayer` `1016ea00` → `FUN_10299c60`): raw `bool` (`DAT_109253f8`, the
  `CAI_CsActList` singleton, is allocated — `FUN_102ca030`, size `0xa0c`);
  `CAI_CsActList::Save` (`102ca130`: `int n` at `+0xa04`, `n × WriteFields("CSAct")`, a
  trailing `int` from the stack); `FUN_103707e0`; then raw int `DAT_1092053c`. 29 bytes
  when the act list is empty. `CAI_CsAct` (stride `0x28`) is one criminal/supernatural act:
  `m_hIgnoreEnt`, `m_vecLocation`, `m_iLevel`, `m_flExpirationTime`, `m_hOffender`,
  `m_hOwner`, `m_bIsCorpse`. The four `FUN_103707e0` globals: EHANDLE `1093ac3c` is the
  entity cops currently treat as the response target (`FUN_10370560` writes
  `GetRefEHandle()`, `FUN_103705b0` clears to `-1`; `CNPC_VCop` slot 404 `10372b70`
  short-circuits while `curtime` is before TIME `1093aca8`); `1093acac` is the count of
  alive `CNPC_VCop` with `m_bCountedAlive` (spawn slot 103 `10371a20` / remove `10371a90`);
  `1093acb0` is cops committed to a player-response schedule (`SelectSchedule` slot 438
  `10371ee0` refuses another if `alive − committed < 4`). `1092053c` bit 0 is `ai_disable`
  (`FUN_10265680`; `FUN_1030c560` also ORs it on `ERROR: Mistake in default schedule`).
- **Troika NPCs** (`CAI_BaseNPCTroika` `102993c0`): raw `bool` (`this+0x630c` non-null),
  then two raw ints from that pedestrian-info record (`+4`, `+8`) when set. Restore
  (`10299700`) reads them into `+0x6310` / `+0x6314`; `OnRestore` `102998c0` rebuilds the
  pointer via `FUN_102f96e0`. `UpdatePedestrianInfo` `102a0d20` tests lane bits at
  `record+0x64`. The one trailing `00` on every NPC in the tutorial save (pointer always
  null there).
- `CAI_BaseNPC` (`1027bc60`) writes `WriteAll(AIExtendedSaveHeader_t)` **before** the chain,
  so an NPC's stream opens with that group rather than `CBaseEntity`.

### Custom field ops

| Op | Retail | Payload |
|---|---|---|
| `variant_t` | `CVariantSaveDataOps` `100d0c90` | raw `int fieldType`, then `WriteFields(<field>)` holding the one typed value (empty when zero) |
| `EntityOutput` (`m_On*`) | — | raw `int` event count, the `Value` variant, then one `EntityOutput` group per event (`m_iTarget`, `m_iTargetInput`, `m_iScript`, `m_nTimesToFire`, `m_iIDStamp`, …) |
| `m_Relationship` | `CRelationshipSaveDataOps` `10349100` | raw `int` count; per non-null slot EHANDLE + four ints: `+4` `Class_T` (`CLASS_PLAYER = 1`; `0` on entity rows), `+8` `Disposition_t` (`D_HT=1` … `D_NU=4`), `+0xc` `IRelationPriority`, `+0x10` dialog reaction modifier (`10332fc0` / `103330f0`). Entity `-1` is a class-wide row |
| `m_pMarkers` | `CAI_InterestingPlaceMarkersSaveRestoreOps` `102d9240` | raw `int` (`+0x584`), raw `int` count; per marker EHANDLE + two vectors |
| `m_sppPatrolPath*` | `CAI_NPCPatrolPathSaveRestoreOps` `1028cc00` | raw `bool`; when set `WriteFields("PatrolPath")` |
| `m_pPyObj` (PEvent) | `CPyObjStrSaveRestoreDataOps` | raw `bool`, raw `int` strlen, the Python source string + NUL |

The current implementation comparison and import requirements are in
`docs/specs/0011-save-game/spec.md`. Decoder byte coverage does not establish the meaning or
type of every key: the research decoder still uses name/size heuristics where datamap typing
is unavailable.

## 2026-09-10 audit — exact `Vampire-015.sav` and restore semantics

Input: `E:\elysium-work\Vampire-015.sav`, 320,883 bytes, SHA-256
`3d478bdcdb863e2897968df885639af796c45b87b4b12b7f08075faef199b38d`.
Rerunning `uv run elysium research sav_to_json` produced a byte-identical copy of
`docs/specs/0011-save-game/save_example.json`: 7,288,381 bytes, SHA-256
`4a0b2feb6782c58f4079860078a44ded3c72097d96294433211a039a7fc10292`.
Output/report scratch is `E:\elysium-work\research\saves\0011-audit\`.
The decoder reports zero unparsed bytes; its `--audit` still reports type fallbacks.

This witness uses the retail engine with **Unofficial Patch content**: `Patch_Plus=1`, `PP=1`,
history `eldritch prodigy`, and queued `AThingOfSomeKind()` from the patched `vamputil.py`.
These identify patch content, not its precise release or the originating install's hashes.
The older illustrative values elsewhere in this document are not substitutes for this file's
actual values. The implementation spec pins its full witness inventory and map counts.

### Two independent save-selection questions

The server `CEntitySaveRestoreBlockHandler::Save` (handler slot 2, `0x101a37c0`) walks
the 0x30-byte entity-table records. For a valid live entity it calls ObjectCaps (entity slot
117, `+0x1d4`) and, if the signed result is nonnegative, calls Save (slot 126, `+0x1f8`).
There is no test of the across-transition bit `0x2` in this ordinary save selection.
The source is reached through the handler's block interface; most calls are virtual, not
direct named calls. `vtmb_callees(101a37c0)` reports these two dispatch slots explicitly.

`CBaseCineCam::ObjectCaps` (slot 117, `0x1006d8f0`) calls its base and clears `0x2`.
It inherits `CBaseEntity::Save`/`Restore` at slots 126/127. The witness nevertheless contains
normal camera records, including theatre `camera_track` id 75 (`cinematic_shot_1`, 542 bytes).
Thus “does not travel” cannot justify dropping its map state from an ordinary save.
This is a concrete divergence in the current Elysium `Freeze` filter, not a modernization.

Handler Restore (slot 7, `0x101a2e40`) first builds identities from table records, rejecting
empty classname/zero-size entries and entries with the upper-word `0x40000000` exclusion flag.
It then seeks each record and calls Restore (slot 127), followed by capability-dependent
reconstruction (`+0x19c`/`+0x1a0`). Interpret these operations through each class's virtual
implementation; a universal “replay Spawn on everything” or “never run restore hooks” rule
is not the retail contract. Identity allocation precedes cross-reference use.

### NPC progress survives valid restoration

`CAI_BaseNPC::Save` (`0x1027bc60`, entity slot 126) writes `AIExtendedSaveHeader_t` before
the inherited chain. It records version 1, flags for required references/path state, the active
schedule name and a CRC of the task array. The inherited datamap also carries
`m_ScheduleState`, wait times and navigator/motor state. Restore (`0x1027c160`, slot 127)
reads the extended header and inherited chain, then restores special time sentinels.

OnRestore (`0x1027bf50`) validates scripted-state/cinematic relationship, header version/name
and the references marked required by saved flags. If valid, it resolves the schedule by name
(`0x1030f350`), checks its task-array CRC against the saved value, and retains the restored
task state when compatible. Missing schedule, bad CRC or failed prerequisites instead call
the fallback at `0x1027be60`. The saved path flag controls navigator reconstruction; its
failure also takes that fallback. A task/status value above the checked bound is normalized
by this function. Do not replace this whole branch structure with unconditional task restart.

`CAI_BaseNPCTroika::OnRestore` (`0x102998c0`) checks both patrol-path objects, calls that
base OnRestore, resolves interesting-place/pedestrian references, rebinds follower data and
combat-start activity, and performs explicit timer/state updates. In particular it draws a
2.0–2.5 second interval for one post-restore timer. Therefore byte-identical pre/post capture
is not a complete oracle for every field; intentional retail restore transformations matter.

Concrete records: tutorial `sentry2` id 1009 has
`SCHED_TROIKA_FOLLOW_PATROL_PATH_WALK`, flags 4 and task index 3. `rat_2` ids 450 and 678
both have `SCHED_VSCURRYING_DO_INTEREST_ACTIVITY`, task index 3. Jack id 870 has a
remaining wait of 2.4788208 seconds. The port's unconditional schedule restart is observable
against these saved states, even if its own incomplete serializer round-trips successfully.

### Python values and the first queued witness

`CPython_SaveRestoreBlockHandler` Save/Restore (`0x1019adc0` / `0x1019b130`, slots 2/7)
write and replace the flag dict `0x1072b370` and morgue dict `0x1072b374` separately.
The tutorial section has 31 flags and four morgue entries, while genesis/theatre hold earlier
copies (1/5 flags, no morgue entries). Explicit load takes the current section's live globals.

The queued tutorial event is `AThingOfSomeKind()` at relative TIME `0.5272217`. This
install's `Unofficial_Patch/python/vamputil.py`, function at line 2751, reads
`G.Pos_One[0]` and `[1]` to compare the player's movement. Its producer `IsIdling` stores
`pc.GetOrigin()` at line 2699. The saved list has three floating-point numbers; converting
it to a textual repr changes the program. `vamputil.MarkAsDead` / `IsDead` likewise require
the real morgue mapping, not a missing-global integer zero. These are source-script witnesses
for the typed-global and morgue gaps, not conjectures based only on serialized masks.

### Time and identity are semantic conversions

`CSave::WriteTime` (slot 25, `0x101a0a80`) subtracts the section's base. The special restore
helper `0x101cf2f0` maps the large encoded sentinel back to `-1` for policies 1/2, zero for
policy 3, or maximum float for policy 4. Preserve the owning field's policy; a blanket
`1e11 -> now`/zero conversion is incorrect. Exact inactive-map aging and engine transition
base selection remain separate recovery work.

The player's `m_hMyWeapons[224]` contains ids 1160, 1157, 1158, 1159, 213, 1154 and 1161
at slots 0, 21, 28, 29, 30, 76 and 77. This sparse saved arrangement needs to be reconciled
with the compact-on-removal recovery in `inventory.md` before a converter renumbers it.
`ETABLE.id`, client `saveentityindex`, source map ordinal and native def index are distinct.
The two `rat_2` names also rule out unique-targetname matching.

Native entity HP is 56 while both sheet health slots are 44; bloodpool is 15 with maximum 10.
Those are the observed saved values, not data to repair. The 221 tutorial decals include 103
runtime impacts and 32 attached records. These are explicit behavioral/visual witnesses, while
the empty act list and absent populated mail state do not establish those systems' coverage.

## Open questions

- **Decal `flags` bits.** `1` marks authored decals and `4` impact decals in every record seen;
  what other bits exist, and what the client does with them, is unrecovered.
- **`ASSIGNED_QUEST+0x3c`.** Written `1` on every journal write (unread / "new" marker);
  nothing in the image reads it, so what would clear it is unrecovered.
- **`m_vDiscBloodType` writer** and **`m_sPlayerName` writer** (the setter `10170ad0` has no
  recovered callers; live name is `pl.netname` and the `.HL2` trailer).
