# Scene GLB seam

This document defines one binary glTF 2.0 unit for one VtMB choreographed scene — a Faceposer
`.vcd` below `sound/`. Shared rules are owned by `seam_map_unit_contract.md`; format and runtime
facts by `docs/vtmb/choreographed_scenes.md`.

## Unit identity

```text
<VTMB>/Vampire/pack*.vpk -> sound/<path>.vcd
  -> vtmb:scene:<path>
  -> $ELYSIUM_EXPORT_V2_ROOT/scenes/<path>.glb
```

The key is the normalized path below `sound/` without `.vcd`. The member resolves UP-first;
5,444 scenes resolve in the merged install (5,300 in the VPKs). A `logic_choreographed_scene`
entity and a `.dlg` line reach a scene by that path, and those are their units' dependencies on
this one.

```text
uv run elysium export_v2 scene-glb sound/<path>.vcd
uv run elysium export_v2 scenes-glb
```

## Source closure

| Source member | Role | GLB destination |
|---|---|---|
| `sound/<path>.vcd` | unit-selecting | `scene` |

## Grammar

Plain ASCII, CRLF. Every line is a whitespace-separated word list — quoted words keep spaces —
optionally followed by a `{ … }` block, and that one rule parses actors, channels, events and
every sub-block. The decoder is a general reader of that grammar, so a token the parser
recognises and the corpus never uses (`tags`, `absolutetags`, `relativetag`, `flextimingtags`,
`flexanimations`, `resumecondition`, `loopcount`, `yaw`, `targethead`, `mapname`, `ramp`,
`range`, `combo`, `disabled`, `samples_use_time`) is decoded into `extras[]` on its owner with
its words and block verbatim rather than refused. A word outside both sets is `unsupported`.

The live token set is `actor`, `channel`, `event`, `time`, `param`, `param2`, `fixedlength`,
`sequenceduration`, `event_ramp`, `bonerename`, `faceposermodel`, `active`, `fps` and `snap`.
The opening `// Choreo version 1` comment is present on 5,434 files and absent on 10; version 1
is the only version shipped, and a unit whose file has no version line publishes
`version: null`.

## Timeline is not a glTF animation

The unit is **scene-less**. A choreographed scene is an event list keyed by actor name; it has
no node, no skeleton and no sampled channel, and a glTF `animations` entry needs a node target.
Stating events as animation samplers would invent targets the file does not have. The timeline
lives entirely in the extension, and the unit declares no accessor.

The BIN chunk it does carry is the **source capsule** alone (`seam_map_unit_contract.md`, "Source
capsule"): buffer 0 holds the `.vcd` file's own bytes, one `bufferView` addresses them, and
`sourceResolution` declares `"capsule": {"encoding": "raw"}` with the member row naming that
view. An empty `.vcd` capsules to nothing and that unit carries no BIN chunk at all.

```json
{
  "extensionsUsed": ["ELYSIUM_vtmb_scene"],
  "extensionsRequired": ["ELYSIUM_vtmb_scene"],
  "buffers": [{"byteLength": 1284}],
  "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 1284}],
  "extensions": {
    "ELYSIUM_vtmb_scene": {
      "schemaVersion": "1.1.0",
      "identity": {},
      "sourceResolution": {},
      "version": 1,
      "fps": 60,
      "snap": false,
      "actors": [],
      "scriptExpressions": [],
      "dependencies": [],
      "comments": [],
      "anomalies": [],
      "omissions": [],
      "coverage": {}
    }
  }
}
```

## Extension reference

| Key | Contents |
|---|---|
| `version` | the `Choreo version` integer, or `null` |
| `fps`, `snap` | the trailing editor-grid values; `fps` is Faceposer's grid, the runtime reads seconds |
| `actors[]` | `name`, `active`, `facePoserModel`, `boneRenames[]` (`from`, `to`), `channels[]`, `extras[]`, `offset` |
| `actors[].channels[]` | `name`, `active`, `events[]`, `extras[]`, `offset` |
| `actors[].channels[].events[]` | the event record below |
| `scriptExpressions[]` | every `python` event's `param` and `param2` verbatim with the event's path |
| `comments[]` | every `//` comment with its offset |

Channel names are free text with no runtime semantics; they are carried as authored.

### Event record

| Field | Meaning |
|---|---|
| `type` | the token, and `typeId` the enum value: `section` 1, `expression` 2, `lookat` 3, `moveto` 4, `speak` 5, `gesture` 6, `sequence` 7, `face` 8, `firetrigger` 9, `flexanimation` 10, `subscene` 11, `loop` 12, `silence` 13, `loud` 14, `python` 15, `cameramove` 16, `camerashot` 17, `camerarestore` 18, `bodysound` 19 |
| `name` | the quoted event name |
| `start`, `end` | seconds from scene start; `end` is `-1` on an instantaneous event |
| `param`, `param2` | the two payload strings, verbatim; there is no `param3` |
| `fixedLength` | present on 5,743 events: the length is the asset's, not the authored range |
| `sequenceDuration` | on 56 `gesture` events |
| `ramp[]` | `event_ramp` rows of `time`, `value` |
| `extras[]` | recognised-but-unused tokens with their words and blocks |
| `offset` | the event line's offset |

Per-type decoded fields sit beside the raw strings:

| Type | Decoded |
|---|---|
| `speak` | `sound` (the dependency row index), `level` from `param2` (`"70dB"` parsed as a float) |
| `expression`, `flexanimation` | `expressionTable` (the `param` stem), `expressionName` (`param2`) |
| `gesture`, `sequence` | `clipLabel` (`param`) |
| `firetrigger` | `trigger` (`atoi(param)`, 1–4) |
| `silence`, `loud` | `markerDuration` (`param` parsed as a float) |
| `bodysound` | `sound`, `level` from `param2` (default 80, floored at 75) |
| `python` | `expression` index into `scriptExpressions` |
| `cameramove`, `lookat`, `face` | `entityNames[]` — `param` and `param2` as authored |

`camerashot` is decoded like any other event and carries `unhandled: true`, because the shipped
dispatcher has no case for type 17.

## Dependencies

| Role | Produced by |
|---|---|
| `sound` | `speak` and `bodysound` `param` |
| `expression-table` | `expression` and `flexanimation` `param`, as `vtmb:expression-table:<stem>` |

A `speak` dependency records the **mp3-first** resolution the engine applies: the authored path
is normalized below `sound/`, both the `.mp3` and the authored-extension candidates are listed,
and the one the install holds is the `asset`:

```json
{
  "role": "sound",
  "sourcePath": "character/dlg/main characters/jack_tutorial/line191_col_e.wav",
  "resolution": "mp3-first",
  "candidates": [
    {"asset": "vtmb:sound:character/dlg/main characters/jack_tutorial/line191_col_e.mp3", "resolved": true},
    {"asset": "vtmb:sound:character/dlg/main characters/jack_tutorial/line191_col_e.wav", "resolved": false}
  ],
  "asset": "vtmb:sound:character/dlg/main characters/jack_tutorial/line191_col_e.mp3",
  "resolved": true
}
```

Whether `expressionName` names a row of the referenced table is a cross-unit check the corpus
index performs; the unit records the name and the table identity. A `gesture` or `sequence`
`clipLabel` resolves against the actor's model at runtime — the animation set names the clip's
owner, not the actor's model — so no dependency is declared; the label is carried for the corpus
index to join. `python` payloads are carried verbatim and declare nothing. A `speak` whose
neither candidate resolves warns; the scene is complete without the audio member.

## Anomalies

| Row | Evidence |
|---|---|
| `degenerate-time` | `end` earlier than `start`, or a range far outside the audio (one event ends 86 s before it starts; one runs to 1.5 million seconds) |
| `marker-duration-mismatch` | a `silence` or `loud` whose `param` disagrees with `end − start` (21 of 21,898) |
| `missing-version-line` | the 10 files with no `// Choreo version` comment |
| `inactive-block` | an `active 0` actor or channel (3) |
| `snap-on` | the one file with `snap on` |

Times are carried as authored; nothing is clamped.

## Byte ledger owners

| Owner | Range |
|---|---|
| `header.version` | the version comment line |
| `actors[i].line`, `actors[i].braces` | the `actor` line and its braces |
| `actors[i].boneRenames[j]`, `actors[i].facePoserModel`, `actors[i].active` | one actor-level line |
| `actors[i].channels[j].line`, `.braces`, `.active` | one channel's lines |
| `actors[i].channels[j].events[k].line`, `.braces` | one event's `event` line and braces |
| `actors[i].channels[j].events[k].<token>` | one `time`, `param`, `param2`, `fixedlength`, `sequenceduration` line |
| `actors[i].channels[j].events[k].ramp` | the `event_ramp` block |
| `…extras[n]` | one recognised-but-unused token line or block |
| `footer.fps`, `footer.snap` | the trailing lines |
| `comments[i]` | one comment |
| `whitespace` | blank lines and indentation |

## Coverage and validation

A complete scene unit accounts for every byte of the file and has zero `unresolved` and zero
`unsupported` rows. Validation re-parses the word-list grammar independently of the writer and
compares every actor, channel, event, time, payload string, ramp row and rename with the
published tree, re-derives every per-type decoded field from the raw strings, and checks that
every dependency was produced by an event the unit publishes.

## Import (2026-09-06)

`uv run elysium import dialogue` deploys the `.vcd` corpus together with the `.dlg` corpus, out of
the published units and nothing else (`pipeline/src/elysium_pipeline/importers/dialogue.py`, on
the shared `importers/corpus_deploy.py`). Each unit's source capsule is lifted, weighed against
its published `byteLength`/`sha256`, and written to

```text
Content/ElysiumCorpus/scenes/<path>.vcd
```

A scene's install path is `sound/<rel>.vcd` and the runtime addresses it by `<rel>` with that
prefix already stripped and folded (`ElysiumScene::NormalizeSceneRel`), so the lane strips it too
— exactly the legacy `scenes/` mirror's shape. On 2026-09-06 the deploy produced 5,444 `.vcd`
files against 5,444 in `$ELYSIUM_EXPORT_ROOT/scenes`, with zero path differences and zero byte
differences.

The lane's properties — recipe stamps, per-unit failure isolation, byte-equality verification,
pruning, `import_report.json` — are listed once in `seam_map_dialogue.md` → "Import".

Reader flip: `ScenesDir`/`SceneFile` move from `FElysiumContentPaths::Root()` to `CorpusRoot()` in
the C++ half of this slice; the deployed tree is already in place for it.
