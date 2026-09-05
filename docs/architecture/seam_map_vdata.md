# VData GLB seam

This document defines one binary glTF 2.0 unit for one file of VtMB's plain-text rulebook below
`vdata/`: the system tables, item definitions, camera-shot tables, terminal definitions, sign
panels and the precache list. Shared rules are owned by `seam_map_unit_contract.md`; the
catalogue of tables and their engine consumers by `docs/vtmb/vdata-catalog.md`, with the prose
schemas in `docs/vtmb/game_runtime.md`, `docs/vtmb/computer-terminals.md` §5,
`docs/vtmb/wielded_weapons.md` §1, `docs/vtmb/disciplines.md` and `docs/vtmb/stealth.md`.

## Unit identity

```text
<VTMB>/Vampire/pack*.vpk -> vdata/<subtree>/<name>.txt
  -> vtmb:vdata:<subtree>/<name>
  -> $ELYSIUM_EXPORT_V2_ROOT/vdata/<subtree>/<name>.glb
```

The key is the lower-cased path below `vdata/` without `.txt`; a file name may carry spaces and
a ` - vampire` / ` - hunter` suffix, both preserved. One file is one unit whatever its root key.
The member resolves UP-first.

```text
uv run elysium export_v2 vdata-glb vdata/<subtree>/<name>.txt
uv run elysium export_v2 vdata-glb <subtree>/<name>
uv run elysium export_v2 vdatas-glb
```

The merged install resolves 743 units across six subtrees: `system` 97, `items` 244,
`camerashots` 66, `hackterminals` 57, `signs` 278, `precache` 1. `vdata/system/stealth.xls` is a
design spreadsheet the engine never opens and is a corpus-index residue row.

## Source closure

| Source member | Role | GLB destination |
|---|---|---|
| `vdata/<subtree>/<name>.txt` | unit-selecting | `tree`, `projection`, `rows` |

A file is one member; a unit never reads another vdata file. Cross-table names (a clan id, a quest
title, a feat name, an NPC template's `ParentTemplateName`) are joins the corpus index checks,
not members of this unit.

## GLB structure

The unit is scene-less and declares no accessor. The BIN chunk it does carry is the **source
capsule** alone (`seam_map_unit_contract.md`, "Source capsule"): buffer 0 holds the `.txt` file's
own bytes, one `bufferView` addresses them, and `sourceResolution` declares
`"capsule": {"encoding": "raw"}` with the member row naming that view. An empty source file
capsules to nothing and that unit carries no BIN chunk at all. `uv run elysium import vdata`
deploys those bytes to `Content/ElysiumCorpus/vdata/**` reading nothing but the units, except for
the `signs/` subtree: it is excluded and left on the legacy flat export until its own slice.

```json
{
  "extensionsUsed": ["ELYSIUM_vtmb_vdata"],
  "extensionsRequired": ["ELYSIUM_vtmb_vdata"],
  "buffers": [{"byteLength": 4096}],
  "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 4096}],
  "extensions": {
    "ELYSIUM_vtmb_vdata": {
      "schemaVersion": "1.1.0",
      "identity": {},
      "sourceResolution": {},
      "grammar": "keyvalues",
      "rootKey": "WeaponData",
      "tree": {},
      "rows": [],
      "projection": {},
      "comments": [],
      "dependencies": [],
      "anomalies": [],
      "omissions": [],
      "coverage": {}
    }
  }
}
```

`grammar` is `keyvalues` for a Valve KeyValues file and `delimited` for the pipe-delimited
`experience_table.txt`; `tree` is populated for the first, `rows` for the second, and the other
is empty.

## Generic decode

`tree` is the KeyValues document with nothing folded away. Every node carries:

| Field | Meaning |
|---|---|
| `index` | source order among siblings |
| `key`, `sourceKey` | the key lower-cased for lookup, and as spelled |
| `quotedKey`, `quotedValue` | whether each token was quoted |
| `value` or `children` | a scalar node's value as spelled, or a block's ordered child list |
| `offset`, `length` | the node's byte span within the file |
| `comments[]` | `//` comments attached to the node they precede |
| `escapes` | `\"` sequences the value carries, so the decoded and spelled forms are both stated |

A quoted value may span lines (`clandoc000.txt` opens a `ShortDescription` with a newline), so
the lexer scans by character with quote state carried across lines. A repeated scalar key inside
one block is decoded as two nodes and records `anomalies[] repeated-scalar-key`; the projection
resolves it last-wins, which is the engine's `KeyValues` behaviour. Repeated block keys are
ordered siblings. A `#include` or `#base` directive is a node of kind `directive`; no shipped
vdata file authors one, and a unit that finds one records it under `typedUnidentified`.

## Typed projection

`projection` restates the tree under the root key's own vocabulary. Each section names its
vocabulary as `closed` — every key the engine loader reads is listed and a key outside it enters
`typedUnidentified` — or `open`, where the loader is not transcribed and the projection is a
faithful re-shaping of the tree with no completeness claim beyond the tree's.

| Root key | Files | Vocabulary | Source of the vocabulary |
|---|---|---|---|
| `WeaponData` | `items/*` (244) | closed | `FUN_10259f80` and `FUN_1025b930`, `docs/vtmb/wielded_weapons.md` §1 |
| `CameraShotTable` | `camerashots/*` (66) | open | — |
| `TerminalDefinition` | `hackterminals/*` (56) | closed | `CPropHacking::LoadFromFile` at `0x1021cba0`, `docs/vtmb/computer-terminals.md` §5 |
| `keypad_strings` | `hackterminals/prop_keypad.txt` | closed | `CPropKeypad::LoadTextStrings` at `0x1021da40` |
| `SignData` | `signs/*` (278) | open | — |
| `PreCacheData` | `precache/entities.txt` | closed | the one row shape the file authors |
| `StatData` | `stats.txt` and variants | closed | `docs/vtmb/game_runtime.md` → "How a trait is addressed" |
| `FeatData` | `feats.txt` | closed | `docs/vtmb/game_runtime.md` → "Feats" |
| `TraitEffectsData` | `traiteffects000.txt`, `traiteffect.txt` and variants | closed | `docs/vtmb/game_runtime.md` → "Trait effects" |
| `RuleData` | `rules.txt`, `rules_tables.txt` | open | — |
| `DiceRollData` | `dicerolls.txt` | closed | `FUN_101d92b0`, `docs/recovered/dice-system.md` |
| `LevelingTemplateList` | `levelingtemplate_000.txt` | closed | `docs/vtmb/game_runtime.md` |
| `ClanDataTables` | `clandoc000.txt`, `npctemplate*` (36) | closed | `docs/vtmb/game_runtime.md` → "The 7 clans" |
| `HistoryDataTables`, `HistoryData` | `histories000.txt`, `history.txt` | open | — |
| `CharCreateWizard` | `charcreatewizard.txt` | closed | `docs/vtmb/game_runtime.md` → "Chargen" |
| `CharEditor` | `chareditor.txt` | closed | the two keys the file authors |
| `DisciplineTgtList` | `disciplinetgt_000..004.txt` | closed | `docs/vtmb/disciplines.md` |
| `QuestTable` | `quests_*.txt` (5) | closed | `docs/vtmb/game_runtime.md` → "Quests" |
| `DispositionTable` | `dispositiontable.txt` | closed | `FElysiumDispositionTable` |
| `ReactionsData` | `reaction.txt`, `reactions000.txt` | open | — |
| `InterestingPlaceTypeList` | `interestingplacetypelist.txt` | open | — |
| `ItemTypeData`, `VendorData` | `items.txt`, `vendors.txt` | open | — |
| `StealthData`, `StealthKillRules` | `stealth.txt`, `stealthkillrules.txt` | open | `docs/vtmb/stealth.md` |
| `SoundSchemeTables` | `sndscheme_char/computer/openable/switch/wpn.txt` | open | — |
| `SoundVolumeTable`, `ParticleImpactTable` | `sound_volume_table.txt`, `particleimpacttable.txt` | open | — |
| `StringData` | `strings.txt`, `strings_internal.txt` and variants | closed | `docs/vtmb/game_runtime.md`; two files share the root key and merge by index |
| `MapNames`, `LoadingTips`, `InfoBarMessages`, `KeyNames`, `DialogData`, `ErrorMessages` | the UI tables | open | — |
| `RadioData`, `NewsData` | `radio_data.txt`, `newscaster_*.txt` | open | — |
| (delimited) | `experience_table.txt` | closed | `docs/vtmb/game_runtime.md` → "XP & leveling" |

A root key not in this table is decoded through `tree` alone with `projection.kind: "open"` and
`projection.rootKey` stating what the file declares; the file is still complete at the tree
level, and the missing vocabulary is a corpus-index census fact rather than a unit failure.

### `WeaponData`

The four model roles are kept apart and never merged:

| Key | Role |
|---|---|
| `viewmodel` | first-person geometry |
| `playermodel` | the loose ground model |
| `wieldmodel_m`, `wieldmodel_f` | held geometry per wielder sex |
| `infomodel` | inventory/UI geometry |

Each is published with the raw spelling (`models\weapons\katana\wield\w_m_katana.mdl`), the
normalized path and the `vtmb:model:` ID. `models/w_null.mdl` and `models/weapons/w_null.mdl`
are shipped zero-bone models and resolve like any other; an empty string is a distinct value
(`present: false`) and produces no reference. The remaining keys — `anim_prefix`, the damage
block, `Magazine`, worth, `sound_group`, `impact_snd_group`, `bucket`, `bucket_position`,
`weight`, `item_flags`, `BitFlag_CantBeLast`, `BitFlag_Discipline_Tgt`, `reload_single`, the
four `ZoomSway*` floats, `camera_class`, `is_visible_in_hud`, `shows_view_model`,
`hides_hands_model`, `item_type` — are published with the loader's default beside an absent key,
so a projection value always says whether it was authored. `camera_class` keeps its string and
the enum it compares into; `melee` and `force_3rd` both state `0x10`.

### `TerminalDefinition`

The grammar is the one `docs/vtmb/computer-terminals.md` §5 transcribes: the root scalars
(`"screen saver"` with its authored space, `brackets`, `email_password`, `email_username`),
`LogonScreen` lines, `SubDir` blocks with `Function` children, `Email` blocks. `autodelete` is
published and marked `readByLoader: false`. The authoring limits the shipped `hack_charlimits.txt`
states are checked and an overrun is `anomalies[] terminal-field-over-limit`.

### `SignData`

`BackgroundImage.Name` is a material reference; `XPos`/`YPos`/`Wide`/`Tall` are published as
spelled, including the empty-string values the patch's `ws-fix` edits leave behind, and a key
that survives only inside a trailing comment (`//1, ws-fix`) is `anomalies[] commented-key`
with the commented value, because the comment documents an authored value the engine no longer
sees. `TextBlock` rows keep their embedded line breaks.

### `experience_table.txt`

`rows[]` carries every line: `key`, `description`, `value` for a data row, `kind: "comment"` for a
`>` line, `kind: "skipped"` for a line under three characters, and `kind: "header"` for the
advisory `Total Experience Value` lines, each with its byte span.

## Extension reference

| Key | Kind | Contents |
|---|---|---|
| `schemaVersion` | string | `1.1.0` — 1.1.0 added the source capsule |
| `identity` | object | `asset`, `vdataPath`, `subtree`, `variant` (`base`, `vampire`, `hunter` or null), `sourcePolicy` |
| `sourceResolution` | object | the member table, its `capsule` declaration, and each member's `capsule` view |
| `grammar` | string | `keyvalues` or `delimited` |
| `rootKey` | string | the declared root key as spelled |
| `tree` | object | the KeyValues document |
| `rows` | array | the delimited-table rows |
| `projection` | object | `kind`, `vocabulary` (`closed`/`open`), `evidence`, and the typed sections |
| `comments` | array | every comment with offset, joined to the node it precedes |
| `dependencies` | array | the reference table |
| `anomalies`, `omissions` | arrays | see below |
| `coverage` | object | the coverage object |

## Dependencies

| Role | Produced by |
|---|---|
| `model` | the item model roles; `ClanDataTables` body and hand models |
| `material` | `SignData.BackgroundImage.Name` below `materials/` |
| `sound` | a scalar naming a `.wav`/`.mp3` (`CharEditor.Music`, `SoundVolumeTable` rows) |
| `sound-group` | `sound_group` and `impact_snd_group`: a directory join to `sound/usable/<category>/<group>/`, published with the resolved member list and their `vtmb:sound:` IDs |
| `particle` | `ParticleImpactTable` rows naming a definition |
| `dialogue` | a scalar naming a `.dlg` |
| `script` | a `QuestTable` `Event` or terminal `runscript` naming a `python/` module |

A cross-table symbolic name — a quest `Title`, a `NameMapping` group, a `DiceRolls` table name,
`ParentTemplateName` — is published in `projection` with its target unit ID where the catalogue
fixes the owning file, and the corpus index verifies it; it produces no `dependencies` row because
no file path is authored.

## Anomalies and omissions

| Row | Meaning |
|---|---|
| `anomalies[] repeated-scalar-key` | one block declares a scalar key twice; last wins |
| `anomalies[] unbalanced-braces` | a brace depth that does not return to zero |
| `anomalies[] unquoted-token-with-space` | a bare token the lexer cut at whitespace where the intent reads as one value |
| `anomalies[] non-ascii-byte` | a byte above 0x7F with its offset |
| `anomalies[] commented-key` | a key present only inside a comment |
| `anomalies[] duplicate-declaration` | two blocks of one name in one file (`Test_Mle5_Def1_Sok5`) |
| `anomalies[] terminal-field-over-limit` | a terminal field longer than the shipped authoring limit |
| `anomalies[] variant-divergence` | a ` - vampire` or ` - hunter` twin that is not byte-identical to its base |
| `omissions[] empty-member` | a zero-byte file |

## Byte ledger owners

| Owner | Range |
|---|---|
| `tree.<path>.key` | one key token, `mapped-text` |
| `tree.<path>.value` | one value token, `mapped-text` |
| `tree.<path>.braces` | the `{` and `}` of one block |
| `tree.directives[i]` | one `#include`/`#base` line |
| `rows[i]` | one delimited line including its terminator |
| `comments[i]` | one `//` comment through end of line |
| `whitespace` | insignificant whitespace between tokens |
| `bom` | a leading UTF-8 BOM |

`<path>` is the node's index path from the root (`0.3.1`), so the ledger names the same node the
tree does.

## Coverage and validation

A complete vdata unit has zero `unresolved` and zero `unsupported` rows; for a closed-vocabulary
root every authored key is in `projection` or `typedUnidentified`. Validation re-lexes the file
independently of the writer, checks that the owners concatenate to the source bytes, rebuilds the
tree and compares every node, and re-derives the projection from the tree.


### Clan body projection

`ClanDataTables` retains the complete open `sections` reshape and adds an ordered `clans[]`
projection. Each clan carries `index` and `bodies`, the authored `General.M_Body`, `F_Body` and
numbered variants, with raw path, model asset id, authored/present and resolution fields.
Repeated clan blocks stay separate; repeated body scalars resolve last-wins while the source
tree retains every occurrence. Only those fields assign `character-body` in the corpus index;
`DeathGib` and other model-valued scalars remain dependencies with their own consumer meaning.
For item models, `playermodel` assigns `ground-item`, `wieldmodel_m/f` assign `wield`, and
`viewmodel` assigns `view-model`. `infomodel` is an information-display model and assigns none
of those roles.
