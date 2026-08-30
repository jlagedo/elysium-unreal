# Dialogue GLB seam

This document defines one binary glTF 2.0 unit for one VtMB conversation — a `.dlg` file below
`dlg/`. Shared rules are owned by `seam_map_unit_contract.md`; the physical format and column
schema by `docs/vtmb/game_runtime.md` §5, the `dlgexpr` grammar by `docs/vtmb/python_bridge.md`
and the runtime parser by `Source/ElysiumUE/Public/ElysiumDlg.h`.

## Unit identity

```text
<VTMB>/Unofficial_Patch -> dlg/<path>.dlg
  -> vtmb:dialogue:<path>
  -> $ELYSIUM_EXPORT_V2_ROOT/dialogues/<path>.glb
```

The key is the normalized path below `dlg/` without `.dlg`; the path carries a hub directory
and may carry spaces (`main characters/jack_tutorial`). The member resolves UP-first: 147 loose
patch files shadow 138 VPK members, and a localisation swaps the whole file.

```text
uv run elysium export_v2 dialogue-glb dlg/<path>.dlg
uv run elysium export_v2 dialogues-glb
```

## Source closure

| Source member | Role | GLB destination |
|---|---|---|
| `dlg/<path>.dlg` | unit-selecting | `lines[]`, `expressions[]` |

## Physical format

Latin-1 text, one row per CRLF line. A row is thirteen cells, each `{` TAB `content` TAB `}`,
concatenated with no separator; the row is split on `}{` and each cell stripped of its brace and
tabs. One row corpus-wide carries fourteen cells (`kiki.dlg`); it is decoded with its extra cell
kept and flagged.

| Column | Field | Meaning |
|---:|---|---|
| 0 | `id` | line id, unique in the file; NPC lines are "tens" by convention |
| 1 | `textMale` | spoken text, male-PC variant — NPC subtitle or PC choice text |
| 2 | `textFemale` | female-PC variant; empty falls back to column 1 |
| 3 | `link` | `#` NPC line; integer N a PC choice jumping to NPC line N; `0` end; empty padding |
| 4 | `condition` | PC choice: the `dlgexpr` gate; NPC line: an action |
| 5 | `action` | actions run when spoken or chosen, `;`-separated |
| 6–11 | — | empty on every shipped row |
| 12 | `textMalkavian` | the Malkavian-PC variant, shown instead of column 1/2 when present |

Columns 6–11 are `reserved-empty`: the decoder verifies each is empty and a non-empty cell
anywhere becomes a `typedUnidentified` row carrying the column, the text and the offset, so an
unknown use of a spare column is carried rather than dropped.

## GLB structure

The unit is scene-less and carries no BIN chunk.

```json
{
  "extensionsUsed": ["ELYSIUM_vtmb_dialogue"],
  "extensionsRequired": ["ELYSIUM_vtmb_dialogue"],
  "extensions": {
    "ELYSIUM_vtmb_dialogue": {
      "schemaVersion": "1.0.0",
      "identity": {},
      "sourceResolution": {},
      "encoding": "latin-1",
      "lines": [],
      "expressions": [],
      "audio": {},
      "dependencies": [],
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
| `encoding` | `latin-1`; bytes are decoded one-to-one and the raw byte is kept for any code point above `0x7F` |
| `lines[]` | one row per source row, in file order |
| `expressions[]` | every non-empty column-4 and column-5 cell, tokenized |
| `audio` | the path convention the unit's audio joins follow |

### `lines[]`

| Field | Meaning |
|---|---|
| `index` | row index |
| `offset`, `length` | the row's byte span |
| `fields[]` | all thirteen (or fourteen) cells raw, each with its own `offset` |
| `id` | parsed column 0 |
| `textMale`, `textFemale`, `textMalkavian` | the raw texts, stage directions included |
| `stageDirections[]` | every `[...]` span of each text column, with `column`, `text`, `offset`; an unterminated `[` is not a direction |
| `link` | the raw column-3 cell |
| `role` | `padding`, `npc-line` or `pc-choice` |
| `linkTarget` | the integer for a PC choice, `0` meaning end; absent otherwise |
| `marker` | `auto-link`, `auto-end` or `starting-condition` when the row is one of Troika's control rows, else absent |
| `condition`, `action` | the raw cells; `conditionExpression` and `actionExpression` index into `expressions[]` |

A `pc-choice` whose trimmed male text equals `(Auto-Link)` or `(Auto-End)` (case-insensitive) is
a control-flow marker the editor emits, never a player response; a row whose raw text contains
`starting condition`, `starting-condition` or `starting_condition` is the engine's opener
sentinel. Both classifications are carried as `marker` and the text stays verbatim.

### `expressions[]`

Each entry carries `line`, `column`, `kind` (`condition` for a PC column 4; `action` for an NPC
column 4 and every column 5), the raw `text`, and `tokens[]` — the `dlgexpr` token classes the
runtime normalizer recognises: a skill-check apply (`<Skill> <threshold>`, implicit `>=`), the
condition joiners `&` and `|`, the action separator `;`, and a Python-expression segment carried
verbatim. The tokenization names segments and their offsets within the cell; the rewriting rules
that turn them into the evaluated form belong to the runtime normalizer and are not restated.

## Audio join

No column names an audio file. A line's voice, phoneme document and choreography resolve by
path:

```text
sound/character/dlg/<hub>/<dlg-stem>/line<id>_col_<lang>.{mp3,wav,lip,vcd}
```

where `<hub>/<dlg-stem>` is the unit's own key below `dlg/`, `<id>` is column 0, and `<lang>` is
`e` (English, 4,843 files), `f` (216), `m` (8) or `n` (48). Only NPC lines carry audio; the PC is
silent. The unit publishes `audio.pathTemplate` and, per NPC line, the four candidate stems, and
declares a `sound` dependency for every candidate the UP-first index resolves and a `scene`
dependency for every resolved `.vcd`, each with the mp3-first pair recorded as
`seam_map_scene.md` records it. A candidate that resolves to nothing is not an anomaly: most
lines have no `_col_f`, `_col_m` or `_col_n` take.

Whether the `.lip` and `.vcd` beside a resolved line agree with the line's text is a corpus-index
check across three units, not a property of this one.

## Dependencies

| Role | Produced by |
|---|---|
| `sound` | every resolved `line<id>_col_<lang>.mp3` or `.wav` for an NPC line |
| `scene` | every resolved `line<id>_col_<lang>.vcd` for an NPC line |

Camera framing is keyed by NPC in `vdata/camerashots/` and is that unit's join; no `camera-shot`
dependency is declared here. A Python expression inside an action may name entities, quests or
functions; those are carried as text and declare nothing.

## Anomalies

| Row | Evidence |
|---|---|
| `field-count-mismatch` | a row with other than thirteen cells (one fourteen-cell row in `kiki.dlg`) |
| `non-latin1-byte` | a byte the file's own encoding does not assign, carried raw |
| `duplicate-line-id` | a column-0 value declared twice in one file |
| `link-to-missing-line` | a PC choice whose non-zero `linkTarget` names no row |
| `link-to-non-npc-line` | a `linkTarget` that names a row whose role is not `npc-line` |
| `malformed-cell` | a cell that does not open `{` TAB and close TAB `}` |
| `unterminated-stage-direction` | a `[` with no closing `]` |
| `reserved-column-used` | the `typedUnidentified` case for columns 6–11 |

## Byte ledger owners

| Owner | Range |
|---|---|
| `lines[i].fields[j].open` | the cell's `{` and TAB |
| `lines[i].fields[j].content` | the cell text (`mapped-text`) |
| `lines[i].fields[j].close` | the cell's TAB and `}` |
| `lines[i].lineBreak` | the row's CRLF |
| `trailing` | bytes after the last row terminator |

A row is exactly its cells and its terminator, so a row's ranges abut and the file's ranges are
gapless by construction.

## Coverage and validation

A complete dialogue unit accounts for every byte of the file and has zero `unresolved` and zero
`unsupported` rows. Validation re-splits every row independently of the writer and compares
every cell, role, link target, marker, stage direction and expression token with the published
`lines[]`, re-derives every audio candidate from the key and the line ids, and checks that every
dependency was produced by a resolved candidate the unit publishes.
