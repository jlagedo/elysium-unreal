# UI-resource GLB seam

This document defines one binary glTF 2.0 unit for each VtMB interface resource file: the VGUI2
`.res` layouts and schemes, the VGUI1 dialog scripts, the HUD sprite tables, the key-binding
tables, the launcher and options scripts, the localized string table and the main-menu particle
scene. Shared rules are owned by `seam_map_unit_contract.md`; the facts about which of these the
shipped game reads are owned by `docs/vtmb/vtmb-ui.md`, `docs/vtmb/m0_menu_build.md`,
`docs/vtmb/controls.md` and `docs/vtmb/audio_pipeline.md` §10.

The generic KeyValues tree shape (`tree`, `nodes[]`, `keys[]` with offsets, quoting and casing)
is owned by `seam_map_vdata.md`; this document names only the typed projection each resource
adds on top of it.

## Unit identity

```text
<VTMB>/Vampire/pack*.vpk -> resource/<name>.res
  -> vtmb:ui-resource:resource/<name>.res
  -> $ELYSIUM_EXPORT_V2_ROOT/ui-resources/resource/<name>.res.glb

<VTMB>/Vampire/pack*.vpk -> scripts/dialog_main
  -> vtmb:ui-resource:scripts/dialog_main
  -> $ELYSIUM_EXPORT_V2_ROOT/ui-resources/scripts/dialog_main.glb
```

The key is the install-relative path with its extension, because `scripts/` mixes extensions
and eleven of its members have none. Every member resolves UP-first. One file produces one unit.

```text
uv run elysium export_v2 ui-resource-glb <path>
uv run elysium export_v2 ui-resources-glb
```

## Source closure

| Source member | Count | Grammar | Typed projection | Live |
|---|---:|---|---|---|
| `resource/vampirescheme.res`, `trackerscheme.res`, `trackerscheme-uhd.res`, `vampirece2scheme.res`, `scripts/launcherscheme.res` | 5 | KeyValues `Scheme { Colors, BaseSettings, Fonts, Borders }` | `scheme` | `vampirece2scheme.res` is not loaded |
| `resource/<dialog>.res` | 20 | KeyValues, one control block per child | `layout.controls[]` | GameUI.dll dialogs |
| `resource/gamemenu.res` | 1 | KeyValues numbered items with `label`, `command`, `SubMenu` | `menu.items[]` | GameUI.dll's unshown menu |
| `scripts/dialog_*` | 11 | VGUI1 KeyValues rooted `"tf2/scripts/<name>"` | `layout.controls[]` | dormant |
| `scripts/320_hud.txt`, `640_hud.txt` | 2 | KeyValues `SpriteData { <name> { <res> { file, x, y, width, height } } }` | `hud.sprites[]` | dormant |
| `scripts/titles.txt` | 1 | `$directive` lines and `Name { text }` blocks | `titles` | dormant |
| `scripts/kb_act.lst`, `kb_act - hunter.lst`, `kb_act - vampire.lst`, `kb_def.lst` | 4 | tab-separated quoted pairs, `//` comments | `rows[]` | options UI and default binds |
| `scripts/kb_keys.lst` | 1 | `keynum "name" "name" COLOR` rows | `rows[]` | key-name table |
| `scripts/kb_trans.lst` | 1 | the same rows in UCS-2 LE with BOM | `rows[]`, `encoding` | key-name table |
| `scripts/launcher.txt`, `scripts/game.txt` | 2 | KeyValues with `$key` substitution and `#include` | `substitutions[]`, `strings[]` | launcher |
| `scripts/settings.scr` | 1 | `VERSION`, `DESCRIPTION <name> { "cvar" { "Prompt" { type info } { default } } }` | `options[]` | dormant |
| `scripts/liblist.gam` | 1 | `key "value"` lines | `keys[]` | launcher |
| `scripts/rooms.lst`, `scripts/woncomm.lst` | 2 | line list; `Name { host:port … }` blocks | `rows[]`, `servers[]` | dormant |
| `resource/gameui_english.txt` | 1 | UCS-2 LE with BOM, KeyValues `lang { Language, Tokens { … } }` | `strings.tokens[]`, `encoding` | GameUI strings |
| `resource/mainmenuparticles.txt` | 1 | KeyValues `MainMenuParticles { camera_*, default_skybox, music, Particle { emitter, origin, angle } }` | `menuScene` | the shipped menu backdrop |

`materials/fonts/fontlist.txt` is the font seam's. `scripts/liblist.gam~` is authoring residue
and belongs to the corpus index. A unit whose file the shipped binaries never read carries
`identity.dormant: true` with the owning document's evidence; dormancy changes nothing about
its decode.

`scripts/settings.scr` carries its own grammar in its header comment — `Version [float]`, then
option descriptions `"cvar" { "Prompt" { type [type info] } { default } }` with types `BOOL`,
`STRING`, `NUMBER min max` and `LIST` — and the unit decodes to exactly that grammar.

## GLB structure

Every unit is scene-less with no BIN chunk: the source is text and every value is a name, a
number or a string.

```json
{
  "extensionsUsed": ["ELYSIUM_vtmb_ui_resource"],
  "extensionsRequired": ["ELYSIUM_vtmb_ui_resource"],
  "extensions": {
    "ELYSIUM_vtmb_ui_resource": {
      "schemaVersion": "1.0.0",
      "identity": {},
      "sourceResolution": {},
      "encoding": "latin-1",
      "grammar": "keyvalues",
      "tree": {},
      "scheme": null,
      "layout": null,
      "menu": null,
      "hud": null,
      "titles": null,
      "rows": null,
      "substitutions": null,
      "strings": null,
      "options": null,
      "menuScene": null,
      "dependencies": [],
      "comments": [],
      "anomalies": [],
      "omissions": [],
      "coverage": {}
    }
  }
}
```

`grammar` is one of `keyvalues`, `tab-rows`, `titles`, `settings-scr`, `line-list`,
`won-lists`, `key-value-lines`. `tree` is present for every KeyValues grammar and is the
authority; each typed projection names the tree node it was read from, so nothing is stated
twice.

## Typed projections

| Projection | Fields |
|---|---|
| `scheme` | `colors[]` (name, RGBA as four integers, raw text), `baseSettings[]` (name, value, the colour it resolves to when the value names one), `fonts[]` (alias name, one row per `<n>` tier with `name`, `tall`, `weight`, `antialias`, `yres` range and every other key), `borders[]` (name, every edge's image and colour rows) |
| `layout` | `controls[]`: `fieldName`, `controlName`, position and size keys as integers, every other key verbatim, child controls |
| `menu` | `items[]`: index, `name`, `label` token, `command`, nested `subMenu` |
| `hud` | `sprites[]`: name, resolution, `file`, rectangle |
| `titles` | `directives[]` (`$position`, `$effect`, `$color`, `$color2`, `$fadein`, `$fadeout`, `$holdtime`, `$fxtime`) in order, `captions[]` (name, text lines, the directive state in force) |
| `rows` | for the `.lst` tables: `columns[]` and one row per line with each cell's text, quoting and offset; for `kb_keys`/`kb_trans` the keynum, both names and the colour token |
| `substitutions` | `$key` definitions in file order; `strings[]` with the raw value and the value after substitution |
| `strings` | `language`, `tokens[]` (token name, UTF-8 text, the UCS-2 code-unit offset and length) |
| `options` | `version`, `sections[]` (name, `options[]` with cvar, prompt, type, type info, default) |
| `menuScene` | camera fields, `defaultSkybox`, `music`, `particles[]` (emitter, origin, angle) |

Numbers are decoded with the C `atof`/`atoi` semantics the engine uses and the raw text is kept
beside each. A key that repeats inside one block resolves last-wins and records a
`repeated-key` anomaly.

## Encoding

`resource/gameui_english.txt` and `scripts/kb_trans.lst` are UTF-16 LE with a byte-order mark;
every other member is single-byte text decoded as Latin-1. The unit states `encoding` and
carries every offset in source bytes, so a UTF-16 token's offset is even and its length counts
code units twice. The BOM is `mapped` under `bom`.

## Dependencies

| Role | Produced by |
|---|---|
| `material` | `material`, `image`, `file` keys whose value names art below `materials/` (HUD sprites are `materials/<file>`) |
| `font` | a scheme `Fonts` tier joined to a `vtmb:font:` unit by face, size, weight and flags; the row carries `resolved` and the matched ID |
| `particle` | `menuScene.particles[].emitter` |
| `texture` | `menuScene.defaultSkybox` faces, resolved through the skybox material rule |
| `sound` | `menuScene.music` |
| `ui-resource` | `#include` and `#base` |
| `ui-resource` | the label tokens a layout or menu names (`#GameUI_*`), joined to `resource/gameui_english.txt` with `resolved` per token |

A font tier that matches no `.fnt` (the engine rasterizes from the installed system face when
the atlas is absent) is `resolved: false` and warns. A `#GameUI_*` token the string table lacks
warns, because client.dll falls back to a hard-coded English string.

## Anomalies and omissions

| Row | Meaning |
|---|---|
| `anomalies[] repeated-key` | a key stated twice in one block |
| `anomalies[] unterminated-block` | a brace the file never closes |
| `anomalies[] mixed-line-endings` | CRLF and LF in one file |
| `anomalies[] non-ascii-latin1` | bytes above 0x7F in a single-byte file, with the decoded text |
| `omissions[] empty-member` | a zero-length member |

## Byte ledger owners

| Owner | Range |
|---|---|
| `bom` | the byte-order mark |
| `tree.nodes[i]` | a block name token and its braces |
| `tree.nodes[i].keys[j]` | one key/value pair including its quotes |
| `rows[i]`, `rows[i].cells[j]` | one row, one cell |
| `titles.directives[i]`, `titles.captions[i]` | one directive line, one caption block |
| `options.sections[i].options[j]` | one option description |
| `comments[i]` | one `//` comment |
| `whitespace` | insignificant whitespace and line endings between tokens |

Every byte is `mapped-text`, `mapped` (the BOM) or claimed by `comments`/`whitespace`.

## Coverage and validation

A complete unit has zero `unresolved` and zero `unsupported` rows. Export-time validation
re-parses the member independently of the writer and compares the tree, every projection row
and every dependency; for the two UTF-16 members it re-decodes the code units and checks each
token's UTF-8 text against its stated offset and length. Standalone validation checks the
scene-less rule, the absence of a BIN chunk, the ledger and the encoding statement.
