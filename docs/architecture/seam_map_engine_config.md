# Engine-config GLB seam

This document defines one binary glTF 2.0 unit for each engine-level configuration and tool
table in the install: the console scripts under `cfg/`, the compiler tables `lights.rad` and
`detail.vbsp`, the map load order, the VPK packer configuration and its localized-file list, and
three small binary state files. Shared rules are owned by `seam_map_unit_contract.md`; the
binding model and the `cfg` load order are owned by `docs/vtmb/controls.md`, the console-to-Python
bridge by `docs/vtmb/python_bridge.md`, the VPK build by `docs/vtmb/vpk_format.md` and the save
container by `docs/vtmb/savegame_format.md`.

## Unit identity

```text
<VTMB>/Unofficial_Patch -> cfg/user.cfg
  -> vtmb:engine-config:cfg/user.cfg
  -> $ELYSIUM_EXPORT_V2_ROOT/engine-config/cfg/user.cfg.glb

<VTMB>/Vampire/pack*.vpk -> lights.rad
  -> vtmb:engine-config:lights.rad
  -> $ELYSIUM_EXPORT_V2_ROOT/engine-config/lights.rad.glb
```

The key is the install-relative path with its extension. Every member resolves UP-first. One
file produces one unit.

```text
uv run elysium export_v2 engine-config-glb <path>
uv run elysium export_v2 engine-configs-glb
```

## Source closure

| Source member | Grammar | Typed projection |
|---|---|---|
| `cfg/*.cfg` (`default`, `config`, `user`, `autoexec`, `language`, `multiplayer`, `skill1`) | console script | `commands[]`, `bindings[]`, `aliases[]`, `cvars[]`, `scriptExpressions[]` |
| `cfg/valve.rc` | console script | the same |
| `cfg/dummy.txt` | console script or empty | the same |
| `lights.rad` | tab rows `texture r g b intensity` | `textureLights[]` |
| `detail.vbsp` | KeyValues `detail { <type> { density, Group<n> { alpha, Model<n> { model, amount } } } }` | `detailTypes[]` |
| `maps/loadorder.txt` | line list with `//` comments | `maps[]` |
| `pack_values.txt` | `key = value` lines with `//` comments | `packer` |
| `localized_list.txt` | category header lines followed by backslash paths | `categories[]` |
| `vidcfg.bin` | 20 bytes | `binary` |
| `voice_ban.dt` | 4 bytes | `binary` |
| `hl2.tmp` | block-structured save fragment | `saveFragment` |

`cfg/joystick.cfg` is named by `valve.rc` and ships in neither retail nor the patch; the `exec`
dependency records it `resolved: false`. Members under `cfg/` whose names begin `elysium_` are
this project's own capture scripts written into the patch tree, not shipped content; the corpus
index classifies them `foreign-file`, and this seam does not export them.

`hl2.tmp` is a unit because it is decodable, and the corpus index still lists it as residue: no
shipped code path reads it. It carries `identity.residue: true`.

## GLB structure

Every unit is scene-less with no BIN chunk.

```json
{
  "extensionsUsed": ["ELYSIUM_vtmb_engine_config"],
  "extensionsRequired": ["ELYSIUM_vtmb_engine_config"],
  "extensions": {
    "ELYSIUM_vtmb_engine_config": {
      "schemaVersion": "1.0.0",
      "identity": {},
      "sourceResolution": {},
      "grammar": "console-script",
      "commands": null,
      "bindings": null,
      "aliases": null,
      "cvars": null,
      "scriptExpressions": null,
      "textureLights": null,
      "detailTypes": null,
      "maps": null,
      "packer": null,
      "categories": null,
      "binary": null,
      "saveFragment": null,
      "dependencies": [],
      "comments": [],
      "anomalies": [],
      "omissions": [],
      "coverage": {}
    }
  }
}
```

`grammar` is one of `console-script`, `rad-rows`, `keyvalues`, `line-list`, `key-equals-value`,
`localized-list`, `binary`, `save-fragment`.

## Console scripts

A console script is a sequence of commands separated by line ends or `;`, with `//` comments
and double-quoted arguments; `unbindall`, `bind`, `alias`, `exec` and `stuffcmds` are commands
like any other. `commands[]` is the complete ordered list — `index`, `name`, `args[]` with
quoting, `offset`, `length`. The typed rows below name the command index they were read from:

| Row | Fields |
|---|---|
| `bindings[]` | `key` (as authored, with the `SEMICOLON` alias where `;` cannot be written), `command` string, and the string split into commands |
| `aliases[]` | `name`, `body` verbatim, and the body split into commands |
| `cvars[]` | `name`, `value` text, whether it appears to be an archived cvar (a two-token line that is neither a known command nor an alias) |
| `scriptExpressions[]` | every argument or alias body of the form `__main__.<expr>` or `<name>(…)`, with the Python text |

`unbindall` and the trailing `+mlook`/`+jlook` that `Host_WriteConfiguration` emits are ordinary
`commands[]` rows. An alias body that names another alias is a reference recorded in
`aliases[].uses[]`; an unresolved name there is a join to the engine's command table and warns.

## Tables

`textureLights[]` is one row per `lights.rad` line: `texture` as authored, the material ID it
names, RGB and intensity as integers, raw text. `detailTypes[]` is the `detail.vbsp` tree typed
as `name`, `density`, `groups[]` with `alpha` and `models[]` with `model` (raw backslash path and
the normalized `models/…` path) and `amount`. `maps[]` is one row per `loadorder.txt` map with
its `vtmb:map:` ID and `resolved`; the leading `// not included: sp_genesisdevice_1` is a
`comments[]` row and a `maps[]` row with `included: false`. `packer` carries every
`pack_values.txt` key: `pack_folder`, `max_size`, `exclude[]`, `skip[]`, `separate[]`,
`localized_file`. `categories[]` is one row per `localized_list.txt` header line (`DLG`,
`vdata`) with `paths[]`, each resolved to the unit kind its extension names and `resolved`
against the install index.

## Binary members

| Member | Reading |
|---|---|
| `vidcfg.bin` | 20 bytes carried as `binary.bytes` with a candidate reading — a 16-byte GUID followed by a 32-bit value — listed in `coverage.typedUnidentified` until the writer in `engine.dll` is identified |
| `voice_ban.dt` | one little-endian `int32` equal to 1, read as a count with zero following entries, listed in `coverage.typedUnidentified` until the writer is identified |

## Save fragment

`hl2.tmp` is 81,920 bytes of the `+header`/`-header` block stream the save system writes
(`docs/vtmb/savegame_format.md`), naming `vampire`, `maps/sp_taxiride.bsp` and `pier` before a
zlib body. `saveFragment` decodes the block headers and the symbol-bearing preamble as far as
that document's block grammar reaches — block names, sizes, the map and landmark strings — and
records the zlib span as `derived` where the save decoder (`formats/sav.py`) inflates and walks
it, otherwise as a `typedUnidentified` range with its digest. The trailing allocation past the
last block is `padding-zero` when zero and `omitted-proven trailing-fill` otherwise.

## Dependencies

| Role | Produced by |
|---|---|
| `engine-config` | `exec <file>` |
| `material` | `lights.rad` texture names |
| `model` | `detail.vbsp` model paths |
| `map` | `loadorder.txt` rows; the `hl2.tmp` map string |
| `dialogue`, `vdata`, `material`, `texture` | `localized_list.txt` paths, by extension |

## Anomalies and omissions

| Row | Meaning |
|---|---|
| `anomalies[] unterminated-quote` | a quoted argument the line never closes |
| `anomalies[] repeated-bind` | one key bound twice in one file; last wins |
| `anomalies[] repeated-alias` | one alias defined twice in one file; last wins |
| `anomalies[] mixed-line-endings` | CRLF and LF in one file |
| `anomalies[] malformed-rad-row` | a `lights.rad` line with other than five fields |
| `omissions[] empty-member` | a zero-length member |
| `omissions[] trailing-fill` | non-zero bytes past the last decoded record of a binary member |

## Byte ledger owners

| Owner | Range |
|---|---|
| `commands[i]` | one command including its arguments and separator |
| `textureLights[i]`, `maps[i]`, `packer.keys[i]`, `categories[i].paths[j]` | one row |
| `tree.nodes[i].keys[j]` | one KeyValues pair (`detail.vbsp`) |
| `binary.bytes` | the whole binary member |
| `saveFragment.blocks[i].header`, `.body` | one block |
| `comments[i]`, `whitespace` | comments and insignificant whitespace |

Text members are `mapped-text`; binary members are `mapped`, `derived`, `padding-zero` or an
evidence-backed omission.

## Coverage and validation

A complete unit has zero `unresolved` and zero `unsupported` rows; the `typedUnidentified`
ranges of the two small binary members are carried, not dropped. Export-time validation
re-tokenizes every console script, re-splits every alias body and binding string, and compares
every table row and dependency with the writer's output. Standalone validation checks the
scene-less rule, the absence of a BIN chunk and the ledger.
