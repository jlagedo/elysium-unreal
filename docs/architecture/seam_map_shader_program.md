# Shader-program GLB seam

This document defines two binary glTF 2.0 unit kinds for VtMB's shipped shader programs: one for
each readable ps.1.x assembly source under `materials/dxshaders/`, and one for each compiled
combo bundle under `shaders/{psh,vsh,fxc}/`. Shared rules are owned by
`seam_map_unit_contract.md`; the family selector rules that choose a program from a material's
state are owned by `docs/vtmb/shader_combos.md`.

The programs are DirectX 8 fixed-function-era code no modern target runs. They are exported
because they are the reference the Unreal material reproduction is measured against, and
because a corpus that leaves 379 members undecoded cannot claim it understands the material
system.

## Unit identity

```text
<VTMB>/Vampire/pack*.vpk -> materials/dxshaders/<stem>.psh
  -> vtmb:shader-source:<stem>
  -> $ELYSIUM_EXPORT_V2_ROOT/shader-programs/source/<stem>.glb

<VTMB>/Vampire/pack*.vpk -> shaders/<subdir>/<stem>.vcs
  -> vtmb:shader-program:<subdir>/<stem>
  -> $ELYSIUM_EXPORT_V2_ROOT/shader-programs/<subdir>/<stem>.glb
```

`<subdir>` is one of `psh` (133 members, compiled pixel programs), `vsh` (95, compiled vertex
programs) and `fxc` (39, HLSL-compiled programs). Both members resolve UP-first. One file
produces one unit.

```text
uv run elysium export_v2 shader-source-glb <stem>
uv run elysium export_v2 shader-sources-glb
uv run elysium export_v2 shader-program-glb <subdir>/<stem>
uv run elysium export_v2 shader-programs-glb
```

## Source closure

| Source member | Role | Unit | GLB destination |
|---|---|---|---|
| `materials/dxshaders/<stem>.psh` | unit-selecting | shader-source | `source.version`, `source.instructions[]`, `source.comments[]` |
| `shaders/psh/<stem>.vcs` | unit-selecting | shader-program | `header`, `combos[]` |
| `shaders/vsh/<stem>.vcs` | unit-selecting | shader-program | `header`, `combos[]` |
| `shaders/fxc/<stem>.vcs` | unit-selecting | shader-program | `header`, `combos[]` with constant tables |
| `materials/dxshaders/<stem>.psh` | same-stem twin of a `psh/` program | shader-program | `sourceComparison` and a `shader-source` dependency |

The install ships 112 readable sources and 133 compiled pixel bundles. A compiled program whose
stem matches a readable source is cross-checked against it; a source with no compiled twin, or a
compiled program with no source, is a complete unit on its own and says so in `identity`.

## Shader-source unit

The `.psh` is text: a version line (`ps.1.1` or `ps.1.4`), `;` comments and one instruction per
line, with `+` prefixing a co-issued instruction.

```json
{
  "extensionsUsed": ["ELYSIUM_vtmb_shader_source"],
  "extensionsRequired": ["ELYSIUM_vtmb_shader_source"],
  "extensions": {
    "ELYSIUM_vtmb_shader_source": {
      "schemaVersion": "1.0.0",
      "identity": {},
      "sourceResolution": {},
      "source": {
        "version": {"major": 1, "minor": 1, "text": "ps.1.1", "line": 1},
        "defines": [],
        "instructions": [],
        "comments": [],
        "lines": []
      },
      "dependencies": [],
      "anomalies": [],
      "omissions": [],
      "coverage": {}
    }
  }
}
```

`lines[]` carries every source line verbatim with its byte offset and line-ending kind, so the
text is recoverable exactly. `instructions[]` is the parse of the non-comment lines:

| Field | Meaning |
|---|---|
| `line` | index into `lines[]` |
| `coIssued` | the line begins with `+` |
| `opcode` | the mnemonic, lower-cased (`tex`, `dp3`, `mad`, `texm3x3vspec`, …) |
| `modifiers` | the instruction modifiers (`_sat`, `_x2`, `_d2`, `_bx2` on sources, …) in source order |
| `destination` | register class, index, write mask |
| `sources` | one row per operand: register class, index, swizzle or selector, negation, modifier |
| `text` | the instruction text without its comment |

A `def` line becomes a `defines[]` row (register, four floats). A token the parser does not
classify fails the unit rather than passing through, because the vocabulary is closed by the
ps.1.x specification.

The unit is scene-less and carries no BIN chunk.

## Shader-program unit

A `.vcs` is a header, a combo table and one Direct3D shader token stream per combo:

```text
int32 version          0 on every shipped member
int32 totalCombos
int32 dynamicCombos
int32 flags
int32 centroidMask
{int32 offset; int32 size} × totalCombos
byte[] combo streams at the stated offsets
```

`shaders/psh/lightmappedgeneric.vcs` is 100 bytes: one combo at offset 28, 72 bytes long.
`shaders/vsh/lightmappedgeneric.vcs` is 520 bytes with two combos. `shaders/fxc/debugdrawdepth_ps20.vcs`
is 308 bytes with one 280-byte combo. The first token of a stream names its model — `0xFFFF0101`
is `ps_1_1`, `0xFFFE0101` is `vs_1_1`, `0xFFFF0200` is `ps_2_0` — and the `fxc/` streams carry a
`CTAB` constant table inside a comment token.

```json
{
  "extensionsUsed": ["ELYSIUM_vtmb_shader_program"],
  "extensionsRequired": ["ELYSIUM_vtmb_shader_program"],
  "extensions": {
    "ELYSIUM_vtmb_shader_program": {
      "schemaVersion": "1.0.0",
      "identity": {},
      "sourceResolution": {},
      "header": {},
      "combos": [],
      "sourceComparison": null,
      "dependencies": [],
      "anomalies": [],
      "omissions": [],
      "coverage": {}
    }
  }
}
```

The unit is scene-less and carries **no BIN chunk and no copy of the bytecode**. The Direct3D
shader token encoding is bijective with its disassembly: every token is either a version token,
an instruction token whose opcode, control bits and length are stated fields, a parameter token
whose register class, index, swizzle, write mask and modifier bits are stated fields, a comment
token whose payload length is stated, or the end token. `combos[].tokens[]` restates every token
as decoded fields plus its raw 32-bit value, and `combos[].instructions[]` is the disassembled
text. Reassembling `tokens[]` reproduces the combo's bytes exactly, which is what pays the
ledger's `mapped` claim over the stream.

| `combos[]` field | Meaning |
|---|---|
| `index` | the combo's position in the table |
| `offset`, `size` | the table entry as stored |
| `model` | `ps_1_1`, `ps_1_4`, `vs_1_1`, `ps_2_0`, … from the version token |
| `tokens` | the decoded token stream |
| `instructions` | the disassembly, one row per instruction with the same field shape as the source unit's `instructions[]` |
| `constantTable` | the decoded `CTAB` (creator string, target, constant descriptions with register set, index, count, type class and default values) or `null` |
| `comments` | every comment token that is not a `CTAB`, with its payload's FourCC and bytes |
| `sha256` | digest of the stream |

`header.flags`, `header.dynamicCombos` and `header.centroidMask` are carried as stored and listed
in `coverage.typedUnidentified` with their offsets: their meaning in this toolchain generation is
not verified, and `dynamicCombos` is not read as a divisor of `totalCombos` until it is. The
mapping from a combo index to the shader-define state that selects it (which `$`-flag or render
setting each static and dynamic index corresponds to) belongs to the family selector rules in
`docs/vtmb/shader_combos.md`; the unit states the index and the decoded program, and a material
unit's `shaderResolution` names a program by stem and condition, so the join is made there.

### Source comparison

A `psh/<stem>.vcs` whose stem matches a `materials/dxshaders/<stem>.psh` assembles that source and
compares the tokens combo by combo:

| `sourceComparison` field | Meaning |
|---|---|
| `source` | the `vtmb:shader-source:` ID |
| `assembled` | whether the source assembled |
| `combosMatched` | the combo indexes whose tokens equal the assembled tokens |
| `combosDiffering` | the combo indexes that differ, each with the first differing token |
| `state` | `equivalent`, `source-binary-drift`, `not-assembled` |

A difference is an `anomalies[] source-binary-drift` row, never a correction of either side. A
`vsh/` or `fxc/` program has no readable twin in the install and carries `sourceComparison: null`.

## Dependencies

| Role | Produced by | Unit |
|---|---|---|
| `shader-source` | a same-stem readable source | shader-program |

A program declares no material back-reference; a material declares the programs its state
admits through `shaderResolution.programs`, and the corpus index writes the inverse.

## Anomalies and omissions

| Row | Meaning |
|---|---|
| `anomalies[] source-binary-drift` | the readable source and the compiled combo disagree |
| `anomalies[] combo-table-overlap` | two combo entries share bytes |
| `anomalies[] combo-out-of-bounds` | an entry runs past the file |
| `anomalies[] stream-without-end-token` | a combo stream does not close with `0x0000FFFF` |
| `omissions[] inter-combo-fill` | non-zero bytes between combo streams, with digest |
| `omissions[] trailing-fill` | non-zero bytes after the last combo, with digest |

Zero bytes between or after combos are `padding-zero`.

## Byte ledger owners

| Owner | Member | Range |
|---|---|---|
| `source.lines[i]` | PSH | one line including its terminator |
| `source.lines[i].comment` | PSH | the `;` comment part of a line |
| `vcs.header` | VCS | the 20-byte header |
| `vcs.comboTable[i]` | VCS | one offset/size pair |
| `vcs.combos[i].tokens[j]` | VCS | one 32-bit token, or a comment token with its payload |
| `vcs.combos[i].constantTable` | VCS | the `CTAB` payload |
| `vcs.padding` | VCS | verified zero fill |

Every PSH byte is `mapped-text`; every VCS byte is `mapped`, `padding-zero` or an evidence-backed
omission.

## Coverage and validation

A complete unit has zero `unresolved` and zero `unsupported` rows; the header words listed in
`typedUnidentified` are carried, not dropped, and do not count as unresolved. Export-time
validation re-parses the source text and re-tokenizes every combo independently of the writer,
reassembles `tokens[]` and compares the result byte for byte with the source stream, and checks
that the disassembly's instruction count equals the instruction-token count. Standalone
validation checks the scene-less rule, the absence of a BIN chunk, the ledger, and that every
`combos[].sha256` equals the digest of the reassembled tokens.
