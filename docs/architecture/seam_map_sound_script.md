# Sound-script GLB seams

This document defines the entry-per-unit seams cut from the audio script tables below
`scripts/`: named game sounds, sentences and DSP presets. Each table is cut the way
`seam_map_surface_property.md` cuts `surfaceproperties.txt` — one entry is one unit, the table
is proven to partition into entry spans, comments and whitespace before any unit is published,
and a name declared twice refuses the table. Shared rules are owned by
`seam_map_unit_contract.md`; audio facts by `docs/vtmb/audio_pipeline.md` §8 and §10.

| Table | Unit kind | Identity | Output |
|---|---|---|---|
| `scripts/game_sounds_surfaceproperties.txt` | game sound | `vtmb:sound-script:<name>` | `sound-scripts/<name>.glb` |
| `scripts/game_sounds_manifest.txt` | manifest | `vtmb:sound-script-manifest:game_sounds_manifest` | `sound-scripts/game_sounds_manifest.glb` |
| `scripts/sounds.txt` | game sound, dormant | `vtmb:sound-script:<name>` | `sound-scripts/<name>.glb` |
| `scripts/soundscapes.txt` | soundscape, dormant | `vtmb:soundscape:<name>` | `soundscapes/<name>.glb` |
| `scripts/sentences.txt` | sentence | `vtmb:sentence:<name>` | `sentences/<name>.glb` |
| `scripts/dsp_presets.txt` | DSP preset | `vtmb:dsp-preset:<id>` | `dsp-presets/<id>.glb` |

Names fold to lower case; the source spelling is published beside the key. `vtmb:sound-script:`
is the namespace `seam_map_surface_property.md` already references for `impact` and `scrape`
(`vtmb:sound-script:concrete.impact`). `scripts/hl2_scripts.dsp` is a Visual Studio project file
and belongs to the corpus index as residue.

```text
uv run elysium export_v2 sound-script-glb <name>
uv run elysium export_v2 sound-scripts-glb
uv run elysium export_v2 sentence-glb <name>
uv run elysium export_v2 sentences-glb
uv run elysium export_v2 dsp-preset-glb <id>
uv run elysium export_v2 dsp-presets-glb
```

Every table resolves UP-first as one member; `game_sounds_surfaceproperties.txt` is patch-overridden.

## Game sounds

An entry is a quoted name followed by a KeyValues block:

```text
"Metal_Barrel.Impact"
{
    "soundlevel"    "SNDLVL_75dB"
    "volume"        "0.6"
    "pitch"         "98,100"
    "rndwave"
    {
        "wave"      "Surfaces/Metal_Barrel/Impact1.wav"
        "wave"      "Surfaces/Metal_Barrel/Impact2.wav"
    }
}
```

The vocabulary is `channel`, `volume`, `pitch`, `soundlevel`, `wave` and `rndwave { wave … }`.
`channel` is a `CHAN_*` symbol or its integer; `soundlevel` an `SNDLVL_*` symbol or a number;
`pitch` a `PITCH_*` symbol, a number or a `min,max` range; `volume` a number or range. The symbol
tables are the ones the file's own header comment lists, and the unit publishes both the source
token and its resolved value. A `wave` beside a `rndwave` block is a pool member too; the engine
picks at random among every wave the entry names, so `waves[]` is one ordered list carrying each
row's origin (`direct` or `rndwave`). A key outside the vocabulary is `unsupported`.

A wave path may open with a Source channel prefix — `*` stream, `#` music, `^` distance-variant,
`@` player, `>` doppler, `<` direction, `)` spatial, `(` no-spatial, `!` sentence, `?` voice.
The prefix is stripped for the dependency and recorded on the row as `prefix`; a `!` prefix names
a sentence, not a file, and yields a `sentence` dependency.

`sounds.txt` uses the same grammar. Its 177 HL2 entries and the 8 soundscape blocks of
`soundscapes.txt` are **dormant**: `game_sounds_manifest.txt` precaches only
`game_sounds_surfaceproperties.txt`, and no map spawns `env_soundscape`. Their units publish
`dormant: true` with that evidence, decoded like any other, because a dormant entry is still
data whose meaning is known.

## Manifest

`game_sounds_manifest.txt` is one unit: a `game_sounds_manifest` block whose live keys are
`precache_file` rows, each a `sound-script-table` dependency, with the commented-out HL2 rows
carried in `comments[]` so a reader sees what the manifest declines to load.

## Soundscapes

A soundscape entry is a quoted name and a block of `dsp` plus `playlooping`, `playrandom` and
`playsoundscape` sub-blocks carrying `volume`, `pitch`, `time`, `wave`, `rndwave`, `position`,
`soundlevel` and `name`. The eight shipped entries are Valve's `d2_depot` and `cabin` samples
with their waves commented out; they decode to blocks with empty pools and `dormant: true`.

## Sentences

The file's own header states the grammar: no tabs, single spaces between the sentence name and
its wave definition, names of at most 23 characters. A line is

```text
<NAME> <path>[ {Len <seconds>}]
```

where `<path>` is relative to `sound/` without extension in stock Source and with `.wav` in every
shipped row, and `{Len n}` is VtMB's per-line duration. Stock HL1 sentence syntax — a directory
prefix ending in `/`, comma-separated words, per-word `(pNNN)` pitch, `(vNNN)` volume, `(sNNN)`
start, `(eNNN)` end and `(tNNN)` time modifiers, and `,` and `.` pause tokens — is parsed and
published where it appears and recorded as `tokens[]` per row so a modifier the corpus does not
use still has a field. A sentence that repeats a name with a trailing digit (`SPI_DIES0`,
`SPI_DIES1`) is two units; the engine's `!NAME` lookup chooses among the numbered group, and the
group membership is a corpus-index join over the `vtmb:sentence:` namespace.

## DSP presets

The file is a comment header — the processor types, their parameters and the symbol tables
(`PROCESSOR TYPE`, `FILTER TYPE`, `FILTER QUALITY`, `DELAY TYPE`, `LFO TYPE`, `ENVELOPE TYPE`,
`PRESET CONFIGURATION TYPE`) — followed by preset blocks:

```text
{   4   LINEAR  0.2 0.8     0.0 0.0 70  0.75
        {  DFR  1.0 3       0.1483 }
        {  RVA 100.0 30.0   4   0.95 1.8 4000 1  0 0 0 0 0 0 0 0 0 }
}
```

A preset is `{ <id> <configuration> <mix-min> <mix-max> <p1> <p2> <p3> <p4> <processors…> }`.
Each processor is a brace list opening with a processor-type symbol (`RVA`, `DFR`, `DLY`, `FLT`,
`CRS`, `AMP`, `MDY`, `LFO`, `EFO`, `PTC`, `ENV`) or `0` for a pass-through, followed by that
type's parameters in the order the header documents. The unit publishes each processor as
`type`, `parameters[]` (source token, resolved number, and the parameter name the header assigns
to that position) and the preset-level fields by their header names; the resolved parameter
count is compared with the type's declared count and a mismatch is `anomalies[]
parameter-count-mismatch` with the raw tokens kept. Ids 0–140 ship, with 134–139 as pass-through
placeholders. The preset id is the key, and it is what `SchemeParams.RoomDSP` and the soundscape
`dsp` key reference.

The header comments are the vocabulary's authority for parameter order and are carried as
`comments[]` on the table, claimed by the table's partition, not by any one unit.

## GLB structure

Every unit is scene-less and has no BIN chunk: every datum is a name, symbol or number the source
wrote as text.

```json
{
  "extensionsUsed": ["ELYSIUM_vtmb_sound_script"],
  "extensionsRequired": ["ELYSIUM_vtmb_sound_script"],
  "extensions": {
    "ELYSIUM_vtmb_sound_script": {
      "schemaVersion": "1.0.0",
      "identity": {},
      "sourceResolution": {},
      "kind": "game-sound",
      "dormant": false,
      "parameters": [],
      "record": {},
      "dependencies": [],
      "comments": [],
      "anomalies": [],
      "omissions": [],
      "coverage": {}
    }
  }
}
```

`kind` is `game-sound`, `manifest`, `soundscape`, `sentence` or `dsp-preset`; the same extension
name is used across the kinds because they share the entry-cut contract, and `record` holds the
kind's typed projection. `parameters` carries every key/value pair or token the entry declares in
source order with its casing, quoting and offset within the entry; every field of `record` names
the parameter index it came from.

## Extension reference

| Key | Contents |
|---|---|
| `kind` | the unit kind |
| `dormant` | `true` for `sounds.txt` and `soundscapes.txt` entries, with `dormantEvidence` |
| `parameters` | ordered `index`, `key`, `sourceKey`, `value`, `quotedKey`, `quotedValue`, `offset` |
| `record` | game sound: `channel`, `volume`, `pitch`, `soundLevel`, `waves[]`; sentence: `path`, `length`, `tokens[]`; DSP preset: `id`, `configuration`, `mix`, `parameters`, `processors[]`; manifest: `precacheFiles[]`; soundscape: `dsp`, `blocks[]` |
| `comments` | comments inside the entry span |

## Dependencies

| Role | Produced by |
|---|---|
| `sound` | every wave path, prefix stripped, spelled as authored, `resolved` against the UP-first index |
| `sentence` | a `!`-prefixed wave |
| `dsp-preset` | a soundscape `dsp` key |
| `sound-script-table` | a manifest `precache_file` |

The surface-property units that name a game sound declare that edge themselves; this seam does
not declare the inverse.

## Table partition and ledger

Before any unit is cut the exporter proves the table is entry spans plus comments plus
insignificant whitespace, with nothing left over; the header comment block of `dsp_presets.txt`
and the banner comments of `sentences.txt` are the table's, claimed as `comments`. A unit's
ledger is gapless over its entry span alone.

| Owner | Range |
|---|---|
| `entry.name` | the name token and its quotes |
| `entry.braces` | the opening and closing braces, or the sentence line's own extent |
| `entry.parameters[i]` | one key/value pair or token |
| `entry.block[j].parameters[i]` | one pair inside a `rndwave`, `playlooping` or processor block |
| `entry.comments[i]` | a comment inside the span |
| `entry.whitespace` | insignificant whitespace inside the span |

## Coverage and validation

A complete unit accounts for every byte of its span and has zero `unresolved` and zero
`unsupported` rows. The vocabulary of each table is closed and documented by the file's own
header, so a key with no field is `unsupported` rather than an anonymous pair. Validation
re-parses the entry and compares every resolved symbol, number, range, pool and processor
parameter with the published record, and checks that every dependency was produced by a
reference the unit publishes.
