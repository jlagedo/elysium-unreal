# Sound-scheme GLB seam

This document defines one binary glTF 2.0 unit for one VtMB map SoundScheme — a
`sound/schemes/*.txt` file that an `ambient_soundscheme` entity names. Shared rules are owned by
`seam_map_unit_contract.md`; the system's facts by `docs/vtmb/audio_pipeline.md` §5.

The `vdata/system/sndscheme_*.txt` tables are a different thing — entity sound-group
vocabularies — and are `vtmb:vdata:system/sndscheme_<domain>` units under `seam_map_vdata.md`.
Nothing in this seam reads them.

## Unit identity

```text
<VTMB>/Vampire/pack*.vpk -> sound/schemes/<stem>.txt
  -> vtmb:sound-scheme:<stem>
  -> $ELYSIUM_EXPORT_V2_ROOT/sound-schemes/<stem>.glb
```

The key is the lower-cased stem below `sound/schemes/`. The member resolves UP-first; 174
schemes resolve in the merged install (147 in the VPKs). An entity references a scheme by
install path (`"scheme_file" "sound/Schemes/ch_cloud.txt"`), case-insensitively, and that
reference is the entities unit's dependency on this one.

```text
uv run elysium export_v2 sound-scheme-glb sound/schemes/<stem>.txt
uv run elysium export_v2 sound-schemes-glb
```

## Source closure

| Source member | Role | GLB destination |
|---|---|---|
| `sound/schemes/<stem>.txt` | unit-selecting | `parameters[]`, `scheme` |

## Grammar

A KeyValues file rooted at the literal `SoundScheme`, holding these blocks:

| Block | Cardinality | Keys |
|---|---|---|
| `SchemeParams` | 0..1 | `RandomSoundCount`, `RoomDSP` |
| `Music` | 0..1 | `Filename`, `Volume`, `Dry`, `NoPause` |
| `Combat` | 0..1 | `Filename`, `Volume`, `Dry`, `NoPause` |
| `Alert` | 0..1 | `Filename`, `Volume`, `Dry`, `NoPause` |
| `Ambient` | 0..1 | `Filename`, `Volume` |
| `RandomSound` | 0..n | `Filename`, `PitchMin`, `PitchMax`, `Volume`, `Frequency`, `AudibleRadius`, `DistMin`, `DistMax`, `HeightMin`, `HeightMax`, `AngleMin`, `AngleMax` |

Volumes are 0–100. `Dry` routes to the dry bus and `NoPause` keeps the stem playing while paused.
A `RandomSound` places each one-shot on a ring `[DistMin, DistMax]` at a random height in
`[HeightMin, HeightMax]` and azimuth in `[AngleMin, AngleMax]` (wrapping) around the entity
origin, attenuated to `AudibleRadius`, at most `RandomSoundCount` at once. Filenames are relative
to `sound/`, in either slash direction and any case.

A key outside this vocabulary is `unsupported`. A repeated `RandomSound` block is a list entry;
a repeated scalar key inside one block resolves to its last value and records a
`repeated-scalar-key` anomaly; a second `Music`, `Combat`, `Alert`, `Ambient` or `SchemeParams`
block is a `repeated-block` anomaly and likewise resolves last-wins.

## GLB structure

The unit is scene-less and carries no BIN chunk.

```json
{
  "extensionsUsed": ["ELYSIUM_vtmb_sound_scheme"],
  "extensionsRequired": ["ELYSIUM_vtmb_sound_scheme"],
  "extensions": {
    "ELYSIUM_vtmb_sound_scheme": {
      "schemaVersion": "1.0.0",
      "identity": {},
      "sourceResolution": {},
      "parameters": [],
      "scheme": {
        "params": null,
        "music": null,
        "combat": null,
        "alert": null,
        "ambient": null,
        "randomSounds": []
      },
      "dependencies": [],
      "comments": [],
      "anomalies": [],
      "omissions": [],
      "coverage": {}
    }
  }
}
```

`parameters` carries every key/value pair in source order with its block path, casing, quoting
and offset; every field of `scheme` names the parameter index it came from. A block the file
does not declare is `null`, and `randomSounds` is empty rather than absent.

## Extension reference

| Key | Contents |
|---|---|
| `parameters` | ordered `index`, `block` (path such as `RandomSound[3]`), `key`, `sourceKey`, `value`, `quotedKey`, `quotedValue`, `offset` |
| `scheme.params` | `randomSoundCount`, `roomDsp` |
| `scheme.music`, `.combat`, `.alert` | `file` (as authored), `asset`, `volume`, `dry`, `noPause` |
| `scheme.ambient` | `file`, `asset`, `volume` |
| `scheme.randomSounds[]` | `file`, `asset`, `pitch` (`min`, `max`), `volume`, `frequency`, `audibleRadius`, `distance` (`min`, `max`), `height` (`min`, `max`), `angle` (`min`, `max`) |
| `comments` | every comment with its offset |

Numeric fields keep their source token beside the parsed number so that `"NoPause" "0"` and an
absent `NoPause` stay distinguishable.

## Dependencies

| Role | Produced by |
|---|---|
| `sound` | every `Filename`, normalized to `vtmb:sound:<path>` with the authored spelling in `sourcePath` and `resolved` against the UP-first index |
| `dsp-preset` | `SchemeParams.RoomDSP`, as `vtmb:dsp-preset:<id>` |

A filename the install lacks warns rather than failing the unit: the reference is to another
seam's data. The mp3-first rule does not apply here — a scheme names the member it plays — so
`resolution` is not recorded.

## Anomalies

| Row | Meaning |
|---|---|
| `repeated-scalar-key` | a scalar key repeated inside one block |
| `repeated-block` | a singleton block declared twice |
| `range-inverted` | a `Min` greater than its `Max` |
| `volume-out-of-range` | a volume outside 0–100 |
| `unresolved-file` | a `Filename` with no member in the install |

## Byte ledger owners

| Owner | Range |
|---|---|
| `root.name`, `root.braces` | the `SoundScheme` token and its braces |
| `blocks[i].name`, `blocks[i].braces` | one block's name and braces |
| `blocks[i].parameters[j]` | one key/value pair |
| `comments[i]` | one comment |
| `whitespace` | insignificant whitespace |

## Coverage and validation

A complete scheme unit accounts for every byte of the file and has zero `unresolved` and zero
`unsupported` keys. Validation re-parses the KeyValues tree and compares every block, key,
resolved number and file reference with the published `scheme`, and checks that every
dependency was produced by a reference the unit publishes.
