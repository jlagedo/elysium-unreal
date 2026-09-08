# Script GLB seam

This document defines one binary glTF 2.0 unit for one VtMB level script: a Python 2.1 source
file below `python/`, with its compiled `.pyc` twin as a companion where one ships. Shared rules
are owned by `seam_map_unit_contract.md`; the script surface, the `vampire` module and the VM
build facts by `docs/vtmb/python_bridge.md` and `docs/vtmb/script_api.md`.

## Unit identity

```text
<VTMB>/Unofficial_Patch -> python/<path>.py
<VTMB>/Vampire/pack*.vpk -> python/<path>.pyc
  -> vtmb:script:<path>
  -> $ELYSIUM_EXPORT_V2_ROOT/scripts/<path>.glb
```

The key is the normalized path below `python/` without `.py`. The `.py` member selects the unit
and resolves UP-first; the same-stem `.pyc` resolves UP-first independently and joins as the
companion role `pyc`. A `.pyc` with no `.py` sibling in the merged install is its own unit keyed
by the same rule with `identity.sourceKind: "pyc-only"`; a unit with both carries
`"py+pyc"`, and one with source alone `"py"`.

```text
uv run elysium export_v2 script-glb python/<path>.py
uv run elysium export_v2 scripts-glb
```

The merged install resolves 67 `.py` (35 patch loose, 32 retail loose) and 44 `.pyc` (24 VPK,
19 patch loose, 1 retail loose). `python/warehouse/warehouse.old` is Python source under a
different extension; the interpreter never imports it, so it is a corpus-index residue row, not a
unit.

## Source closure

| Source member | Role | GLB destination |
|---|---|---|
| `python/<path>.py` | unit-selecting, executed source | `source`, `tokens`, `structure`, `references` |
| `python/<path>.pyc` | optional compiled companion | `pyc` — marshal tree and disassembly |

CPython 2.1 has no `zipimport`, so the interpreter reads only the loose filesystem; a `.pyc` that
ships only inside a VPK is provenance and never executes. The unit states this per member in
`sourceResolution.members[].executed` so a reader does not have to know the rule.

## GLB structure

The unit is scene-less and has no BIN chunk: every datum is text, a token or a small integer
table the extension states directly.

```json
{
  "extensionsUsed": ["ELYSIUM_vtmb_script"],
  "extensionsRequired": ["ELYSIUM_vtmb_script"],
  "extensions": {
    "ELYSIUM_vtmb_script": {
      "schemaVersion": "1.0.0",
      "identity": {},
      "sourceResolution": {},
      "source": {},
      "tokens": [],
      "structure": {},
      "references": [],
      "entityNames": [],
      "pyc": null,
      "dependencies": [],
      "anomalies": [],
      "omissions": [],
      "coverage": {}
    }
  }
}
```

## Mapping

| Script datum | Extension |
|---|---|
| Source bytes | `source.text` verbatim, `source.encoding`, `source.lineEnding` (`crlf`, `lf`, `mixed`), `source.bom`, `source.lines[]` byte spans |
| Lexical structure | `tokens[]` — `type`, `string`, `line`, `col`, `offset` under the Python 2.1 grammar |
| Module structure | `structure.imports[]`, `structure.functions[]` (`name`, `lineSpan`, `args`, `defaults`, `nested`), `structure.classes[]`, `structure.assignments[]` (module-level targets) |
| Install references | `references[]` — a string literal that names an install member, with its token index and the unit ID it resolves to |
| Entity references | `entityNames[]` — literals passed to `FindEntityByName`, `FindEntitiesByName` and their local aliases (`Find`, `Finds`) |
| Compiled twin | `pyc` — magic, mtime, the code-object tree and per-code disassembly |

The tokenizer is the unit's own. The Python 3 `tokenize` module rejects Python 2 syntax that the
shipped scripts use — `print x`, backtick repr, `<>`, `0777` octal literals, `ur''` prefixes — so
`tokens[]` follows the 2.1 grammar and a token the tokenizer cannot classify fails the unit
rather than being skipped.

`structure` is a syntactic index, not an execution model: a function defined twice
(`giovanni.py` defines `cutscene()` at lines 118 and 488) appears twice, in order, with
`shadows` naming the earlier definition.

`entityNames` is a join to `vtmb:map-entities:` units by `targetname`, resolved by the corpus
index; it produces no `dependencies` row because a script is loaded per map and the map is not a
datum of the file.

### `pyc`

The companion is decoded completely from the marshal stream:

| Field | Contents |
|---|---|
| `magic` | the four-byte magic word; the Python 2.1 word 60202 (`2A EB 0D 0A`) is the expected value and every shipped member is verified against it |
| `mtime` | the source modification time the compiler wrote |
| `code` | the root code object, recursively: `argcount`, `nlocals`, `stacksize`, `flags`, `code`, `consts[]`, `names[]`, `varnames[]`, `freevars[]`, `cellvars[]`, `filename`, `name`, `firstlineno`, `lnotab` |
| `code.<path>.instructions[]` | the bytecode disassembled against the Python 2.1 opcode table: `offset`, `opcode`, `name`, `arg`, `argValue`, `line` |

`filename` carries Troika's build path (`J:/Remaster/Vampire_v409_041008_LOCS/…`); it is a
decoded string, not a reference. Where the `.py` sibling exists the export compiles nothing: it
compares the `.pyc`'s top-level `def`/`class` name set, `firstlineno` values and string constants
against `structure` and records each disagreement as `anomalies[] pyc-source-drift` with both
sides. A marshal type code outside the 2.1 set is `unresolved`.

## Extension reference

| Key | Kind | Contents |
|---|---|---|
| `schemaVersion` | string | `1.0.0` |
| `identity` | object | `asset`, `scriptPath`, `sourceKind`, `sourcePolicy` |
| `sourceResolution` | object | the member table, each row with `executed` |
| `source` | object | `text`, `encoding`, `lineEnding`, `bom`, `lines[]` |
| `tokens` | array | the token stream |
| `structure` | object | `imports[]`, `functions[]`, `classes[]`, `assignments[]` |
| `references` | array | `token`, `literal`, `kind`, `asset`, `resolved` |
| `entityNames` | array | `token`, `name`, `call` |
| `pyc` | object or null | the marshal decode |
| `dependencies` | array | the reference table |
| `anomalies` | array | see below |
| `omissions` | array | `empty-member` |
| `coverage` | object | the coverage object |

## Dependencies

| Role | Produced by |
|---|---|
| `dialogue` | a literal ending in `.dlg` → `vtmb:dialogue:` |
| `sound` | a literal below `sound/` or passed to `PlayDialogFile`/`PlaySound` → `vtmb:sound:` |
| `map` | a literal naming `maps/<map>.bsp` or a `ChangeLevel` target → `vtmb:map:` |
| `model` | a literal below `models/` → `vtmb:model:` |
| `vdata` | a literal naming a camera-shot table, sign definition or hack file → `vtmb:vdata:` |

A literal that fails to resolve keeps `resolved: false` and warns; the script's own decode does
not depend on it.

## Anomalies

| Row | Meaning |
|---|---|
| `mixed-line-endings` | both `\r\n` and bare `\n` terminate lines |
| `tab-space-indent-mix` | one block indented with tabs and spaces |
| `non-ascii-byte` | a byte above 0x7F, with offset and the decoded Latin-1 character |
| `duplicate-definition` | a top-level name defined twice |
| `pyc-source-drift` | the compiled twin disagrees with the source on names, line numbers or constants |
| `pyc-magic-mismatch` | a `.pyc` whose magic word is not 60202 |

## Byte ledger owners

| Owner | Member | Range |
|---|---|---|
| `source.bom` | PY | the UTF-8 BOM when present |
| `source.lines[i]` | PY | one line including its terminator, `mapped-text` |
| `pyc.magic` | PYC | 4 bytes |
| `pyc.mtime` | PYC | 4 bytes |
| `pyc.code.<path>.<field>` | PYC | one marshalled field of one code object, including its type-code byte |

A `.pyc` is claimed field by field, so a marshal record and the ledger range name the same
place; there is no padding in the format and none is claimed.

## Coverage and validation

A complete script unit has zero `unresolved` and zero `unsupported` rows. Validation re-reads the
source bytes and checks that `source.lines[]` concatenate to them exactly, re-tokenizes
independently of the writer and compares the token stream, re-derives `structure` from the tokens,
and — where a `.pyc` exists — re-unmarshals it and compares every code-object field and every
instruction against `pyc`.
