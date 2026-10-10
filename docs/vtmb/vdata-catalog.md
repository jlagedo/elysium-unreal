# `vdata/` catalog — VtMB's plain-text rulebook

VtMB keeps its entire RPG/rules layer as Valve-KeyValues **text** under `vdata/` in the
install — the character sheet, dice tables, clans, disciplines, quests, items, weapons,
vendors, stealth, NPC social rules, sound schemes, UI strings, dialogue-camera framing, and
the hacking minigame. The engine (`vampire.dll`) loads each by name at runtime; nothing here
is compiled or map-scoped. This doc is the **inventory + consumer map**: what each table is
and which system reads it.

## Import

`pipeline/src/elysium_pipeline/exporters/UE_extract_vdata.py` mirrors the tree **verbatim** (no parse/transcode) into
`$ELYSIUM_EXPORT_ROOT/vdata/`, resolved patch-first (patch loose > retail loose > VPK — the engine's own search
order), same bring-your-own-game posture as the script/dialogue/sign mirrors (gitignored,
regenerable). `uv run elysium export bundle vdata` runs it directly, while complete
export profiles schedule the bundle once. Two
exclusions: `vdata/signs/` (owned by `UE_extract_signs.py`, which also decodes
the sign art → `$ELYSIUM_EXPORT_ROOT/signs/`) and `stealth.xls` (a design-source spreadsheet, not engine data).
Counts: **465** files mirrored (`system/` 97, `items/` 244, `camerashots/` 66,
`hackterminals/` 57, `precache/` 1); `signs/` 278 land in `$ELYSIUM_EXPORT_ROOT/signs/`.

## How consumers are established

The authoritative consumer is the engine loader in `vampire.dll` — e.g. `DiceRolls.txt` is
read by `FUN_101d92b0` (see `recovered/dice-system.md` / RE5), found by grepping the
decompile for the filename/root-key string. The exact engine loader/schema for each is RE'd
when its consumer is built.

**Fourteen of these families now have a runtime reader** — `stats`, `feats`, `rules` +
`rules_tables`, `traiteffect` + `traiteffects000`, `clandoc000` + `npctemplate*`, `histories000`,
the five `quests_*`, `experience_table`, `levelingtemplate_000`, `charcreatewizard` and
`strings` + `strings_internal`, in
`Source/ElysiumUE/Private/Substrate/ElysiumRulebook.{h,cpp}` — with the quest and
charcreatewizard readers in `Substrate/ElysiumQuestTables.{h,cpp}` and
`Substrate/ElysiumChargenWizard.{h,cpp}` beside it — behind `UElysiumRulebookSubsystem`
(plus `dispositiontable.txt`, read separately by `FElysiumDispositionTable`). The row counts quoted
below are asserted against the exported files by `Elysium.Content.Rulebook`, so a re-export that
drifts is a test failure rather than a quietly smaller table. The remaining families have no
parser, and for those there is no prior implementation to check field usage against — only this
catalogue and the prose schemas in `docs/vtmb/game_runtime.md`.

## `vdata/system/` (97) — the rulebook

### Character sheet & core rules — engine-loaded

| Root key | File(s) | What | Notes |
|---|---|---|---|
| `StatData` | `stats.txt` | four flat trait containers: `Min`/`Max`/`Default`, `Costs {New, Raise}`, `NameMapping`, `IncPredependency`. **The engine indexes each container by block position with the leading `*_Order` block at 0** — Attributes is 35 slots (every derived stat through `Experience`), Abilities 13, Disciplines and Active_Disciplines **17** each. Also carries 29 nested `Table` lookups, among them the chargen **priority-tier** pools on the `*_Order` stats (`Subpool_Attribute_Primary_Secondary_Tertiary` 2/1/0, `Subpool_Ability_…` 3/2/1, plus `_Kine` variants) — the clan-keyed half lives in `rules_tables.txt` | `stats.txt` ≡ `stats - vampire.txt`; `- hunter` variant vestigial. **RE24 done** — `docs/vtmb/game_runtime.md` → "How a trait is addressed". The last four discipline slots (`Shield_of_Faith`, `Divine_Vision`, `Holy_Light`, `Mind_Shield`) are the Numina powers, which ship in `stats.txt` proper; the **save array is 13** (`docs/vtmb/savegame_format.md`) |
| `FeatData` | `feats.txt` | derived feats: a **variable-length** `Base%d` list (0–3 entries; a base may carry a `/ N` or `* N` modifier) summed into the feat rating, plus `Automatic%d` and the dead `Display2nd%d`; `PCWeighting`/`NPCWeighting` name a `DiceRolls` table | 23 feats; **RE24 done** — ties feats → the RE5 weighting tables (all 23 name `Normal`) |
| `TraitEffectsData` | `traiteffects000.txt` | per-trait effects (clan banes, frenzy effects, History backgrounds): `TraitEffectCategory` → `TraitEffectGroup` → `TraitEffect { Trait, Modifier \| Costs }` — **5 categories / 169 groups / 457 effects** (+5 `UNUSED_TraitEffect`, 75 `Costs`) | vampire/hunter variants (`traiteffects000.txt` ≡ `- vampire`); **RE25 done** — the loader + operator set, `docs/vtmb/game_runtime.md` → "Trait effects". A group may name one trait twice (Brujah's `Animalism` takes a `Costs` effect *and* a `Max 3`), so a group is an ordered list, not a per-trait map |
| `TraitEffectsData` | `traiteffect.txt` | the **`ModifierNames` operator enum** the trait-effect parser indexes (`0 +`, `1 *`, `2 /`, `3 Max`, `4 Min`, `5 %`, `6 Value`, `7 Cost`, `8 BloodCost`, `9 Damage`, `10 Duration`) | 27 lines; the only thing in it |
| `RuleData` | `rules.txt`, `rules_tables.txt` | frenzy / humanity / masquerade / blood constants + shared tables; `rules_tables.txt` also holds the clan-keyed chargen `Subpool_*` pools (all zero bar `Subpool_Disciplines` = 1) | Unofficial-Patch-tuned (`changed by wesp`) |
| `DiceRollData` | `dicerolls.txt` | d10 resolver `TableWeightings` + `HealthModifiers` + tier strings | **RE5 done** — `recovered/dice-system.md`; loader `FUN_101d92b0` |
| (pipe-delimited) | `experience_table.txt` | quest-reward → XP: `key \| description \| value`, 190 rows, `XP = floor(value/100)` with the **sub-100 remainder carried** across awards; give-once is the player's `m_ExpList` ledger, not the trailing `01` | not KeyValues; `>`-comments, lines under 3 chars skipped, the `Total Experience Value` headers are advisory (36/68 agree). **RE24 done** |
| `LevelingTemplateList` | `levelingtemplate_000.txt` | ordered auto-level templates — **16 templates / 86 `LevelGroup` / 1,260 `Level` steps**; eight are `*_CharGen`. A `Level { "<Trait>" "<N>" }` block carries one pair whose *key* is the trait name, and the list order is the buy order | NPC / quick-level. Blocks sit at column 0 inside their parent and `Level` is written inline on one line, so brace depth is the only structural signal |

### Clans & character creation

| Root key | File(s) | What |
|---|---|---|
| `ClanDataTables` | `clandoc000.txt` | clan definitions — disciplines, bonuses, banes, and the per-clan body models (`M_Body0..5`/`F_Body0..5`, the source the PC-body export draws from; `M_Hands`/`F_Hands` are separate hand models) |
| `ClanDataTables` | `npctemplate000.txt`…`025` + 10 named (`_tutorial`, `_malkmansion`, …) — **36 files, 150 declarations / 149 distinct names** | per-clan / per-map NPC stat templates. The `Attributes` block is the flat Attributes container, so **`Max_Health` is the NPC's authored life capacity**. The patch-first census resolves 114 literal declarations, 23 through a parent, and 13 through `stats.txt`'s `Default` 100, with an effective range **1…1400**: `Scurrying`/`Rat` 1, `NPCGeneric`/`CivilianGeneric` 22, ordinary officers 100, `Bach` 440, `SheriffMan` 570, `Gargoyle` 800, `Tutorial_Jack` 819, `ManBat` 880, `Hengeyokai` 968, `MingXiao` 1400. **`ParentTemplateName` is single-parent inheritance** resolving across files (64 non-empty), so an absent trait key means *inherit*, not zero — `npctemplate_cdc.txt` is the minimal case, one `General` key over an empty `Attributes`. The duplicate declaration is identical-health `Test_Mle5_Def1_Sok5`; `npctemplate019/021.txt` are empty `ClanDataTables`. Reproduce with `uv run elysium research npc_health_census <export>/vdata/system` |
| `HistoryDataTables` / `HistoryData` | `histories000.txt`, `history.txt` | the History background-trait system |
| `CharCreateWizard` | `charcreatewizard.txt` (78 KB) | chargen personality-quiz → clan scoring (`Traits`/`TraitCombinations`/`TraitOrderings`, **85** `Popup`s in 10 groups, `Clan_Tables.ClanNode` + the 3×3 `ConnectionScores` matrix) — **RE25 done**, read by `client.dll`. 78 popups author a bare `Popup` line and 7 carry a trailing `// restored by wesp` / `// added by wesp`, so a count by line shape undercounts exactly the patch's restorations. A group's `Defaults` block is popup-shaped and every member inherits from it field by field, **including the positional `Action` list**. Parsed by `FElysiumWizard` |
| `CharEditor` | `chareditor.txt` | char-editor config — two keys, `Music "music/Vampire_Theme.mp3"` + `Music_Volume "1.0"`; nothing else |

### Disciplines (vampire powers)

| Root key | File(s) | What |
|---|---|---|
| `DisciplineTgtList` | `disciplinetgt_000.txt`…`004` (~300 KB) | ordered Discipline targeting, strata mappings, hit effects, projectiles, interruption and helper casts — native interpreter and full power catalog: `docs/vtmb/disciplines.md` |

### Quests

| Root key | File(s) | What |
|---|---|---|
| `QuestTable` | `quests_santamonica/downtown/hollywood/chinatown/main.txt` | per-hub quest + objective definitions — **75 quests / 435 completion states / 161 `AwardXP`**. `quests_main.txt` holds only its header comment and no quests |

Schema, from the files' own header comment:

```
QuestTable { Quest { "Title" "DisplayName"
    CompletionState { "ID" "Description" "Type" "AwardXP" "AwardMoney" "Event" } } }
```

- **`Title`** is the key dialogue and scripts use — `pc.SetQuest("Arthur Knox", 2)`. `DisplayName`
  is the journal heading; `Description` is the journal body; both are localized.
- **`ID`** is documented as the unique numeric completion state, and state **0 = unassigned** — but
  the loader **never reads the key**: `SetQuest(title, N)` addresses the N-th `CompletionState` in
  **file order** (`docs/vtmb/game_runtime.md` → "Quests"). All 435 shipped rows author `ID` equal to their own
  position, so the two readings coincide; the ordinal is the mechanism. A quest is capped at **20**
  states, and is absent from the journal until it holds one.
- **`Type`** is `success` / `failure` / `incomplete` and drives the entry's font and colour. The
  engine matches it by **substring**, and knows a fourth value — `botch` — that no shipped row
  authors and that refuses any further state change on that quest.
- **`AwardXP` names an `experience_table.txt` key, not a number** (`"AwardXP" "Carson01"`) — the
  shipped header comment calls it "how many experience points", and the data contradicts it.
  `AwardMoney` *is* a number — but **`AwardMoney` and `Event` are authored in zero shipped rows**
  across all five files, so the only award path with data behind it is `AwardXP`.
- **`Event`** is script data — a flag assignment or a call — handed to the script interpreter when
  the state is reached. It is a script-dispatch path beside the four in `docs/vtmb/python_bridge.md`.

### NPC social / AI

| Root key | File(s) | What |
|---|---|---|
| `DispositionTable` | `dispositiontable.txt` | NPC disposition matrix |
| `ReactionsData` | `reaction.txt`, `reactions000.txt` | NPC reaction rules |
| `InterestingPlaceTypeList` | `interestingplacetypelist.txt` | ambient-NPC place types: repeated `InterestingPlaceType` rows identify a `Name`, weighted `Into_Activities` / `Activities` / `Outof_Activities`, and weighted `AcceptedClasses` entries that may name an NPC classname or stat template |

The map entity spelling is **`intersting_place`** (missing the second `e`) in shipped data. Its
keyfields select the table `type`, `group_id`, `max_npcs`, `min_time`/`max_time`,
`match_orientation`, `enabled`, `rating` and `testflags`; NPCs author a space-separated
`interesting_place_groups` allowlist and opt in with `use_interesting`. In the exported
`sm_hub_1`, 17 placed `npc_VPedestrian` records opt in, 76 places supply the destinations, and the
group relation keeps separate street/asylum/sewer populations from crossing zones. Nine of those
places start disabled, so enable state is behavioral data rather than an editor-only hint.

### Items & economy

| Root key | File(s) | What |
|---|---|---|
| `ItemTypeData` | `items.txt` | item-type + inventory-section definitions |
| `VendorData` | `vendors.txt` | shop inventories |

### Stealth

| Root key | File(s) | What |
|---|---|---|
| `StealthData` | `stealth.txt` | selected 11×11 light/Sneaking visual-range and cone matrices plus hearing-distance and light-threshold vectors; runtime transaction: `stealth.md` |
| `StealthKillRules` | `stealthkillrules.txt` | deaf-zone table, `FindVictim` admission; HUD/commitment remain 0002/3c |

### Audio / FX

| Root key | File(s) | What |
|---|---|---|
| `SoundSchemeTables` | `sndscheme_char/computer/openable/switch/wpn.txt` | per-**category** entity sound resolution — **distinct** from the map ambience `sound/Schemes/*` |
| `SoundVolumeTable` | `sound_volume_table.txt` | per-sound volume |
| `ParticleImpactTable` | `particleimpacttable.txt` | surface → impact particle/decal |

### UI / strings / localization

| Root key | File(s) | What |
|---|---|---|
| `StringData` | `strings.txt`, `strings_internal.txt` | named `Name<N>` lists under one `Strings` block — the groups a stat's `NameMapping` points at. Both files share the root key and their group name spaces overlap by design, so a reader must MERGE by index rather than replace (`strings.txt` itself authors `UIOccultStrs` twice). `AttributeOrder`/`AbilityOrder` are what turn a clan template's symbolic `Attrib_Order` into an index; `AttributeGroup`/`AbilityGroup`/`StatCategoryTitles` are the sheet's own headings |
| `MapNames` | `mapnames_localized.txt` | map display names |
| `LoadingTips` | `loadingtips.txt` (114 KB) | loading-screen tips |
| `InfoBarMessages` | `infobartypes.txt` | HUD info-bar messages |
| `KeyNames` | `keynames.txt` | input-key display names |
| `DialogData` | `dialog.txt` | dialogue-subsystem config (stub) |
| `ErrorMessages` / misc | `engineerrors.txt`, `credits*.txt`, `masquerade.txt`, `charaction_sounds.txt` | engine errors, credits, masquerade text, a note file |

### Ambient content

| Root key | File(s) | What |
|---|---|---|
| `RadioData` | `radio_data.txt` | in-game radio |
| `NewsData` | `newscaster_main.txt`, `newscaster_side.txt` | TV news content |

## Other `vdata/` subtrees

| Path | Count | Root key | What |
|---|---|---|---|
| `items/` | 244 | `WeaponData` | per-weapon/armor stats (the four model roles — `viewmodel`, `playermodel`, `wieldmodel_m`/`_f`, `infomodel` — plus `anim_prefix`, damage, `Magazine`, worth, `sound_group`) — "loaded by both Game and Client DLLs" |
| `camerashots/` | 66 | `CameraShotTable` | per-scene conversation/cutscene camera framing (keyed by NPC/scene name) |
| `hackterminals/` | 57 | `TerminalDefinition` (+ `keypad_strings`) | computer content; grammar and behavior: `docs/vtmb/computer-terminals.md` |
| `precache/` | 1 | `PreCacheData` | `entities.txt` — entity precache list |
| `signs/` | 278 | `SignData` | sign/popup panels — **already imported** via `UE_extract_signs.py` → `$ELYSIUM_EXPORT_ROOT/signs/` |

## The hunter / vampire / base split

Several `system/` and `items/` files ship as a triplet: `<name>.txt`, `<name> - vampire.txt`,
`<name> - hunter.txt` (`stats`, `strings`, `traiteffects000`, `credits`, and 4 item files).
`<name>.txt` is **byte-identical to the `- vampire` variant** (what the shipped game loads);
the `- hunter` variant is the cut companion-mode data. All are mirrored verbatim; **consumers
read the base (unsuffixed) file**.

## The KeyValues reader (`VKeyValues`, `vampire.dll`) [recovered 2026-10-10]

The retail grammar every `vdata/`, `.res` and `.vmt` text is read with; the walk is
`docs/specs/layers/L0-entity/walks/L0-r003.md`, the port `Source/ElysiumUE/Private/ElysiumKeyValues.{h,cpp}`
(records `Arena/scenarios/audio/l0_keyvalues_lexer.json`, `l0_keyvalues_tree.json`).

- **Tables.** `FUN_1023eff0` zeroes a 256-byte membership table and marks each byte of a delimiter string
  (signed index). `FUN_102473e0` builds two once (flag `0x10753640`): A `0x10753440` from `{}()'`
  (`0x105794c0`) and B `0x10753540` from `{}()':` (`0x105c4ef8`). The selector byte `DAT_10753641` lives
  in `.bss` and nothing in the image writes it, so **table B is the live table: `:` is a delimiter**
  (`a:b` is three tokens) and table A is dead.
- **Tokenizer `FUN_10247280`** `(cursor, out, quoted*) -> next cursor | NULL`. `*quoted = 0`; a NULL
  cursor returns NULL with `out` untouched; `*out = 0`. Whitespace is every byte whose signed value is
  `<= 0x20` (controls, space, and all bytes `>= 0x80`); NUL in the skip returns NULL with `out = ""`.
  At token start only: `//` to `\n`/NUL, `/*` to the first `*/` at or after the byte following `/*`
  (`/**/` closes, `/*/` does not; unclosed → NULL at EOF). `"` starts a string (`*quoted = 1`): raw
  bytes, `\"` → `"` (both consumed; any other backslash kept literally), newlines kept; `"` ends with
  the cursor after it, NUL ends with the cursor **one past the NUL** (the next call reads the heap). A
  table byte is a one-byte token (cursor past it). Otherwise a bare word runs while the next byte is
  neither in the table nor `<= 0x20`; `"` and `/` do not end a word (`a//b`, `ab"cd"`, `a/*c*/b` are
  words). `out` (`0x1073AA80`, 8 KiB to the next global, declared size unrecovered) has no bound check.
- **Wrapper `FUN_101f2f30`** `(char** cursor, fs unused, quoted*)`: tokenizes `*cursor` into the shared
  buffer, writes the cursor back, returns the buffer (never NULL). Reached through three JMP thunks
  `0x10004273 -> 0x10247430 -> 0x1000b947 -> 0x10247280`.
- **Block parser `FUN_101f2360`** `(node, cursor)`: loop: `key = wrapper(NULL)`; empty key (EOF, or a
  quoted `""`) or `"}"` (`0x10547B64`, quote not asked) returns; `child = 0x101f2cf0(key)` (always a new
  node, appended at the tail of `+0x1C`/`+0x18`); `val = wrapper(&quoted)`; `"{"` (`0x10547B78`, quote
  not asked) recurses into the child; else `child+0x0C = intern(val)` (`0x101f2f70`) for every leaf, and:
  quoted → type 1; else `e1 = end of strtol(val, 10)`, `e2 = end of strtod(val)`: `e2 > e1` → `+0x08 =
  (float)strtod`, type 3; `e1 <= val` (nothing consumed) → type 1; else `+0x08 = strtol`, type 2.
  `_strtol` clamps on overflow (`99999999999` → 2147483647); the VC6 `_strtod` (`___strgtold12`
  `0x1043B1BD`) has no hex and no inf/nan state (`0x1A` → int 0; `inf`/`nan` → strings), accepts `D`/`d`
  as exponent letters. A value missing before `}` takes `}` as its text and the block runs to EOF.
- **File reader `FUN_101f2180`** `(name, fs)`, ECX = target node or NULL: Open `rb` (`0x105596CC`),
  Size, `malloc(size+1)`, Read, Close, NUL; then loop: `key = wrapper`; cursor NULL → done (return 1);
  target set → `Clear 0x101f2c60` + `SetName 0x101f2090` on it, else a new node linked after the previous
  root (`0x101f2cd0`, the `+0x18` chain); `tok = wrapper`; cursor NULL → done; `"{"` → block parse,
  target = NULL; else `DevMsg("ERROR: parsing KeyValue in file %s, expecting {, got %s\n", name, "{")`
  (the constant, not the token) and the node is reused for the next key. A top-level `}` is an ordinary
  root key; an empty file returns 1 with the target untouched. Caller: `FUN_101f2e20(name, fs)` --
  `keycache_Lookup` (VSTDLIB, body outside the corpus) hit returns the cached node's IKeyValues
  (`+0x30`); miss creates the node named after the file, parses with it as target, `keycache_Add`s it.
  The cache key is the file name; the node's name becomes the first root key. Names are stored verbatim;
  the in-tree name compare `FUN_101f26f0` uses `__strcmpi` (`0x1043E780`).
- **Node** (0x38 bytes, pool `0x1073D280`): `+0x04` refcount, `+0x08` int/float bits, `+0x0C` value
  text, `+0x10` type (0 block, 1 string, 2 int, 3 float), `+0x14` name, `+0x18` next sibling / next root,
  `+0x1C` first child, `+0x28` IKeyCacheable, `+0x30` IKeyValues (21 slots). UNRECOVERED: `+0x20`,
  keycache case folding, the DevMsg gating, `___strgtold12` state 11 (`12-34`).
- **Port.** Byte-for-byte except two named representations: the lexer runs on TCHARs (a TCHAR `>= 0x80`
  classifies as a byte `>= 0x80` does; quoted text keeps either verbatim) and a cursor past the NUL reads
  NUL, not the heap. The lookup index (`Values`/`Pairs`/`Kids`, lowercase keys) stands for the
  case-insensitive compare; `Children` is the retail child list with the retail types. No keycache: every
  load reads the file.

## The KeyValues file loader (the second class, `0x102480f0`, `vampire.dll`) [recovered 2026-10-10]

vampire.dll carries a SECOND KeyValues class over the same tokenizer `0x10247280`: the 0x1C-byte node of
the file loader `FUN_102480f0`, read by the sound schemes (`CSoundScheme::Precache 0x1022a4b0` ->
`FUN_1022a930`), the soundscapes (`CSoundscapeSystem::Init 0x101ae050`), the signs, keypads, terminals
(`CPropHacking::LoadFromFile 0x1021cba0`), radios, quest journal, sound-volume table, disposition table,
interesting places and stealth-kill rules (16 callers, all pathID 0 and cache 1). Walk
`docs/specs/layers/L0-entity/walks/L0-r004.md`; port `Source/ElysiumUE/Private/ElysiumKeyValuesLoader.{h,cpp}`
and the accessors in `ElysiumKeyValues.{h,cpp}`; records `Arena/scenarios/audio/l0_scheme_file_load.json`,
`l0_keyvalues_access.json`.

- **Node** (0x1C bytes, `operator_new`, not zeroed): `+0x00` name, `+0x04` value text, `+0x08` int or float
  bits, `+0x0C` type **0 string / 1 int / 2 float / 3** (formatted as int by GetString), `+0x10` next sibling,
  `+0x14` first child, `+0x18` fallback chain. The layout and the type codes are the 2003 vgui2 `KeyValues`
  (`m_sValue`, `m_iValue`/`m_flValue`, `m_iDataType` `TYPE_STRING, TYPE_INT, TYPE_FLOAT, TYPE_PTR`, `m_pPeer`,
  `m_pSub`, `m_pChain`); the class name is not in the corpus. The ctor `0x10247ba0` and the name setter
  `0x10247cf0` (copy the name; zero `+0x14 +0x10 +0x04 +0x18`; clear `DAT_10753f68[0]`, the FindNext name)
  never write `+0x08` / `+0x0C`: a block's type word is heap garbage. `CreateChild 0x10248870` appends at the
  tail of `+0x14` through `+0x10`.
- **Wrapper `FUN_102482b0`** `(char** cursor, char* quoted)`: the tokenizer over `*cursor` into the shared
  buffer `0x10754fa8` (distinct from `VKeyValues`' `0x1073AA80`), cursor written back, returns the buffer.
- **Block parser `FUN_10248510`** `(node, cursor, fs)` (`fs` forwarded, never read): `key = read(NULL)`; empty
  (EOF, or a quoted `""`) or `"}"` (`0x10547b64`) returns; `child = CreateChild(key)` BEFORE the value;
  `val = read(&quoted)`; `"{"` (`0x10547b78`, 2-byte compare, quote ignored) recurses; else `+0x04 = copy(val)`
  and: quoted -> type 0; else `ei = strtol(val, &end_i, 10)`, `ef = strtod(val, &end_f)`, unsigned compares:
  `end_f > end_i` -> `+0x08 = (float)ef`, type 2; `end_i > val` -> `+0x08 = ei`, type 1; else type 0. `strtol`
  saturates (`10000000000` -> 2147483647, type 1), `strtod` is decimal-only (`0x10` -> int 0; `1d3` -> float
  1000). A `}` in value position is a string value; a key at EOF is a `""` string leaf.
- **File loader `FUN_102480f0`** `(this, filename, fs, pathID, cache)` -> AL 1|0: `cache && strstr(filename,
  ".txt")` (`0x10546e4c`) -> the text cache `FUN_102482f0`; a pointer skips the loader's own open. Else
  `fs->Open(filename, "rb" 0x105596cc, pathID)` (slot `+0x00`), fail -> return 0; `Size` (`+0x18`),
  `malloc(n+1)`, `Read` (`+0x08`), `Close` (`+0x04`), `buf[n] = 0`. Then EVERY top-level token is a root: the
  first renames the caller's node (name setter: children, value and chain dropped, type word kept), each next
  is `new` + ctor, linked after the previous through `+0x10` (`0x10248a50`); the token after a root opens a
  block when its FIRST BYTE is `{` (`CMP byte [EAX],0x7B` at `0x10248211`, so `"{abc"` qualifies) and is
  dropped otherwise (`Name "x" { k v }` yields roots `Name`, `{`, `v`). Returns 1 after any successful text
  source, empty text included (zero roots, node unrenamed); parse success is not reported. `fs` is
  `DAT_1070b238` (written once by `CServerGameDLL::vfunc1 0x1011a0c0`); which engine method each slot is stays
  unrecovered. `FUN_10248430` is the buffer twin (same root loop over text in hand; no caller).
- **Text cache `FUN_102482f0`** `(name, fs)` -> text|NULL: **CRC-32 of the name** (`0x1023f040` init
  `0xffffffff`, `0x1023f0c0` table step with the standard table at `0x10496f58` -- entry 1 `0x77073096`, the
  reflected IEEE polynomial --, `0x1023f060` final complement); linear search of `DAT_10753768[0..DAT_10856fa8)`,
  hit -> `DAT_107547a8[i]` (the key is the hash alone: a second load of the name returns the first text
  whatever the file now holds); miss -> `Open(name, "rb", 0)` (pathID 0, not the caller's), fail -> NULL;
  `n+1 >= DAT_105c530c` (the 1 MiB arena, initial `0x100000`) -> `Close`, NULL; else `Read` into the arena,
  NUL, record, `remaining -= n+1`. Tables of 1040 hashes / 512 pointers, no bound check. It touches only `fs`
  and its own statics -- nothing above L0 -- although `hooks.tsv:325` classes it as an L0 -> L4 hook and
  `audit.tsv` calls it a string pool; the port builds it at L0.
- **Accessors.** `FindKey FUN_10248900` `(this, key, create)`: first child of `+0x14` whose name matches by
  `__strcmpi`, in list order; else `FindKey(+0x18, key, 0)` (the chain, recursive, never creating); else, when
  `create`, `CreateChild(this, key)`; else NULL. No non-NULL writer of `+0x18` was found (the vgui2
  `ChainKeyValue`). `SetString FUN_102490e0` `(this, key, value)`: `FindKey(key, 1)`; `free(+0x04)`, `+0x04 =
  copy(value)`, `+0x0C = 0`; `+0x08` untouched. `GetString FUN_10248cd0` `(this, key, default)`: `key` NULL ->
  `this`, else `FindKey(key, 0)`; absent -> `default`; type 1 or 3 -> `Q_snprintf(buf[64], 0x40, "%d"
  0x105461f0, +0x08)`, type 2 -> `"%f"` (`0x10554f28`) of the float as double, then `SetString(this, key,
  buf)` (the same search, the text written back, type 0); returns `+0x04` (NULL for a block). `GetInt
  FUN_10248bb0` `(this, key, default)`: same lookup; type 0 -> `atoi(+0x04)` (`_atol 0x104313bc`: wraps in 32
  bits, no clamp); type 2 -> `__ftol(float +0x08)` (`0x10431320`, truncate, low dword; `1e10` -> 1410065408);
  else `+0x08` raw. The typed setters `FUN_10249160 / 102491a0 / 102491e0` (not ported) store the number into
  the child and the TYPE into `this` (retail bug; asm `0x1024917a`, `0x102491fa`).
- **`FUN_1022a930`** (the scheme parser, not this run's) reads the first root's children in order, matching
  each name with `strstr` against `SchemeParams`, `Ambient`, `Music`, `Combat`, `Alert`, `RandomSound`, and
  reads with `GetString("Filename")`, `GetFloat("Volume")` (`0x10248c60`), `GetInt("Dry", 1)`,
  `GetInt("NoPause", 1|0)`, `GetInt("Frequency", 10)`, `GetInt("PitchMin|Max", 100)`,
  `GetInt("RandomSoundCount", 2)` clamped 0..6, `GetInt("RoomDSP", 0)` clamped 0..255.
- **Port.** `ElysiumKeyValuesLoader` shares `FKvNode` with the `VKeyValues` reader (the type is semantic;
  `FileTypeCode` spells 0/1/2/unset at the sites); a block's unwritten type reads as `Block`; the engine file
  system is `IKvFileSystem` (slots Open/Close/Read/Size) over the deployed corpus with ONE search path (named
  modernization: every retail caller passes pathID 0); the cache is process-global and CRC-32 keyed as retail's;
  its two tables grow instead of overrunning. `GetString` of a block returns `""` where retail returns NULL;
  `FindKey` with a NULL key compares `""` where retail would fault on a node with children. UNRECOVERED: the
  engine methods behind the four `fs` slots, the token buffer's declared size (ceiling 0x2000), who links
  `+0x18`, the reach of `FUN_10248430`.
