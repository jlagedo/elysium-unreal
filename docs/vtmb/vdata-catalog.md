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
| `StealthKillRules` | `stealthkillrules.txt` | stealth-kill victim/deaf-zone eligibility; native consumer recovery remains open |

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
