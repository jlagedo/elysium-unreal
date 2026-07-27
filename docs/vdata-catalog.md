# `vdata/` catalog — VtMB's plain-text rulebook

VtMB keeps its entire RPG/rules layer as Valve-KeyValues **text** under `vdata/` in the
install — the character sheet, dice tables, clans, disciplines, quests, items, weapons,
vendors, stealth, NPC social rules, sound schemes, UI strings, dialogue-camera framing, and
the hacking minigame. The engine (`vampire.dll`) loads each by name at runtime; nothing here
is compiled or map-scoped. This doc is the **inventory + consumer map**: what each table is,
which system reads it, and which roadmap task wires it up.

## Import

`tools/UE_extract_vdata.py` mirrors the tree **verbatim** (no parse/transcode) into
`out/vdata/`, resolved patch-first (patch loose > retail loose > VPK — the engine's own search
order), same bring-your-own-game posture as the script/dialogue/sign mirrors (gitignored,
regenerable). `export_all.py` runs it once at end of a run (`--no-vdata` to skip). Roadmap
**PL5b**. Two exclusions: `vdata/signs/` (owned by `UE_extract_signs.py`, which also decodes
the sign art → `out/signs/`) and `stealth.xls` (a design-source spreadsheet, not engine data).
Counts: **465** files mirrored (`system/` 97, `items/` 244, `camerashots/` 66,
`hackterminals/` 57, `precache/` 1); `signs/` 278 land in `out/signs/`.

## How consumers were established

The authoritative consumer is the engine loader in `vampire.dll` — e.g. `DiceRolls.txt` is
read by `FUN_101d92b0` (see `recovered/dice-system.md` / RE5), found by grepping the
decompile for the filename/root-key string. No existing runtime parser covers these tables, so
there is no prior implementation to check field usage against — only a reusable Source-KeyValues
parser pattern and prose schemas for the core sheet tables in `docs/game_runtime.md`. The
"roadmap" column below is this project's design intent; the exact engine loader/schema for each
is RE'd when its consumer is built, the way RE5 did for the dice tables.

## `vdata/system/` (97) — the rulebook

### Character sheet & core rules → **9.4** (data), engine-loaded

| Root key | File(s) | What | Notes |
|---|---|---|---|
| `StatData` | `stats.txt` | attribute/ability/discipline/background trait tree: `Min`/`Max`/`Default`, `Costs {New, Raise}`, `NameMapping` | `stats.txt` ≡ `stats - vampire.txt`; `- hunter` variant vestigial |
| `FeatData` | `feats.txt` | derived feats: `Base0`+`Base1` (attribute+ability) → feat; `PCWeighting`/`NPCWeighting` name a `DiceRolls` table | ties feats → the RE5 weighting tables |
| `TraitEffectsData` | `traiteffects000.txt` | per-trait effects (clan banes, frenzy effects) | vampire/hunter variants |
| `RuleData` | `rules.txt`, `rules_tables.txt` | frenzy / humanity / masquerade / blood constants + shared tables | Unofficial-Patch-tuned (`changed by wesp`) |
| `DiceRollData` | `dicerolls.txt` | d10 resolver `TableWeightings` + `HealthModifiers` + tier strings | **RE5 done** — `recovered/dice-system.md`; loader `FUN_101d92b0` |
| (pipe-delimited) | `experience_table.txt` | quest-reward → XP (`XP = floor(value/100)`) | not KeyValues; `>`-comments |
| `LevelingTemplateList` | `levelingtemplate_000.txt` | ordered auto-level templates | NPC / quick-level |

### Clans & character creation → **10.7** chargen

| Root key | File(s) | What |
|---|---|---|
| `ClanDataTables` | `clandoc000.txt` | clan definitions — disciplines, bonuses, banes, and the per-clan body models (`M_Body0..5`/`F_Body0..5`, the seed **PL13 [x]** exports the PC bodies from; `M_Hands`/`F_Hands` is PL14) |
| `ClanDataTables` | `npctemplate000.txt`…`025` + named (`_tutorial`, `_malkmansion`, …) — ~40 | per-clan / per-map NPC stat templates |
| `HistoryDataTables` / `HistoryData` | `histories000.txt`, `history.txt` | the History background-trait system |
| `CharCreateWizard` | `charcreatewizard.txt` (78 KB) | chargen personality-quiz → clan scoring |
| `CharEditor` | `chareditor.txt` | char-editor config (stub) |

### Disciplines (vampire powers) → **10.7** powers

| Root key | File(s) | What |
|---|---|---|
| `DisciplineTgtList` | `disciplinetgt_000.txt`…`004` (~300 KB) | discipline power targeting + effects |

### Quests → **9.4** (quest map live since 1.1)

| Root key | File(s) | What |
|---|---|---|
| `QuestTable` | `quests_santamonica/downtown/hollywood/chinatown/main.txt` | per-hub quest + objective definitions |

### NPC social / AI → **10.7** AI

| Root key | File(s) | What |
|---|---|---|
| `DispositionTable` | `dispositiontable.txt` | NPC disposition matrix |
| `ReactionsData` | `reaction.txt`, `reactions000.txt` | NPC reaction rules |

### Items & economy → **10.7** combat/economy

| Root key | File(s) | What |
|---|---|---|
| `ItemTypeData` | `items.txt` | item-type + inventory-section definitions |
| `VendorData` | `vendors.txt` | shop inventories |

### Stealth → **10.7**

| Root key | File(s) | What |
|---|---|---|
| `StealthData` | `stealth.txt` | detection model |
| `StealthKillRules` | `stealthkillrules.txt` | insta-kill eligibility |

### Audio / FX → **6.x / 8.x**

| Root key | File(s) | What |
|---|---|---|
| `SoundSchemeTables` | `sndscheme_char/computer/openable/switch/wpn.txt` | per-**category** entity sound resolution — **distinct** from the map ambience `sound/Schemes/*` handled by PL5a |
| `SoundVolumeTable` | `sound_volume_table.txt` | per-sound volume |
| `ParticleImpactTable` | `particleimpacttable.txt` | surface → impact particle/decal |

### UI / strings / localization → **8.6** UI

| Root key | File(s) | What |
|---|---|---|
| `StringData` | `strings.txt`, `strings_internal.txt` | display strings (trait name-mappings, UI) |
| `MapNames` | `mapnames_localized.txt` | map display names |
| `LoadingTips` | `loadingtips.txt` (114 KB) | loading-screen tips |
| `InfoBarMessages` | `infobartypes.txt` | HUD info-bar messages |
| `KeyNames` | `keynames.txt` | input-key display names |
| `InterestingPlaceTypeList` | `interestingplacetypelist.txt` | map POI types |
| `DialogData` | `dialog.txt` | dialogue-subsystem config (stub) |
| `ErrorMessages` / misc | `engineerrors.txt`, `credits*.txt`, `masquerade.txt`, `charaction_sounds.txt` | engine errors, credits, masquerade text, a note file |

### Ambient content → **10.7**

| Root key | File(s) | What |
|---|---|---|
| `RadioData` | `radio_data.txt` | in-game radio |
| `NewsData` | `newscaster_main.txt`, `newscaster_side.txt` | TV news content |

## Other `vdata/` subtrees

| Path | Count | Root key | What | Roadmap |
|---|---|---|---|---|
| `items/` | 244 | `WeaponData` | per-weapon/armor stats (view/world models, damage, `Magazine`, worth, `sound_group`) — "loaded by both Game and Client DLLs" | 10.7 combat |
| `camerashots/` | 66 | `CameraShotTable` | per-scene conversation/cutscene camera framing (keyed by NPC/scene name) | Feel track (dialogue camera) |
| `hackterminals/` | 57 | `TerminalDefinition` (+ `keypad_strings`) | hacking-minigame computer content | 10.7 |
| `precache/` | 1 | `PreCacheData` | `entities.txt` — entity precache list | — |
| `signs/` | 278 | `SignData` | sign/popup panels — **already imported** via `UE_extract_signs.py` → `out/signs/` (PL5c) | 4.10 [done] |

## The hunter / vampire / base split

Several `system/` and `items/` files ship as a triplet: `<name>.txt`, `<name> - vampire.txt`,
`<name> - hunter.txt` (`stats`, `strings`, `traiteffects000`, `credits`, and 4 item files).
`<name>.txt` is **byte-identical to the `- vampire` variant** (what the shipped game loads);
the `- hunter` variant is the cut companion-mode data. All are mirrored verbatim; **consumers
read the base (unsuffixed) file**.
