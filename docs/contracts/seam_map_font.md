# Font GLB seam

This document defines one binary glTF 2.0 unit for one VtMB bitmap font: a `.fnt` glyph table
below `materials/fonts/` whose pixels live in sibling atlas textures. It also defines the one
font-list unit that joins the table to the faces the engine registers. Shared rules are owned by
`seam_map_unit_contract.md`; the decoded layout by `pipeline/src/elysium_pipeline/formats/fnt.py`
and the face inventory by `docs/vtmb/m0_menu_build.md` §3.

## Unit identity

```text
<VTMB>/Vampire/pack*.vpk -> materials/fonts/<face>_<size>_<weight>_<flags>.fnt
  -> vtmb:font:<face>_<size>_<weight>_<flags>
  -> $ELYSIUM_EXPORT_V2_ROOT/fonts/<face>_<size>_<weight>_<flags>.glb

<VTMB>/Vampire/pack*.vpk -> materials/fonts/fontlist.txt
  -> vtmb:font-list:fontlist
  -> $ELYSIUM_EXPORT_V2_ROOT/fonts/fontlist.glb
```

The key is the file stem below `materials/fonts/`. The member resolves UP-first; the merged
install resolves 226 fonts (141 VPK, 85 patch loose) and one list.

```text
uv run elysium export_v2 font-glb materials/fonts/<stem>.fnt
uv run elysium export_v2 fonts-glb
uv run elysium export_v2 font-list-glb
```

`fonts-glb` publishes every font unit and the list unit, because the join is checked in both
directions.

## Source closure

| Source member | Role | GLB destination |
|---|---|---|
| `materials/fonts/<stem>.fnt` | unit-selecting glyph table | `header`, `charMap`, `glyphs` |
| `materials/fonts/<stem>-page<n>.{tth,ttz}` | atlas page, a texture unit | `vtmb:texture:fonts/<stem>-page<n>` in `pages[]` |
| `materials/fonts/<stem>-page<n>.vmt` | atlas material, a material unit | `vtmb:material:fonts/<stem>-page<n>` in `pages[]` |
| `materials/fonts/fontlist.txt` | the face registry | the font-list unit |

The font unit carries no pixels. A glyph's coverage is the alpha channel of its page atlas,
sliced by the glyph's UV rectangle; that rule is stated in `pages[]` and applied by a consumer.

## GLB structure

Both units are scene-less and have no BIN chunk.

```json
{
  "extensionsUsed": ["ELYSIUM_vtmb_font"],
  "extensionsRequired": ["ELYSIUM_vtmb_font"],
  "extensions": {
    "ELYSIUM_vtmb_font": {
      "schemaVersion": "1.0.0",
      "identity": {},
      "sourceResolution": {},
      "header": {},
      "charMap": [],
      "glyphs": [],
      "pages": [],
      "fontList": {},
      "dependencies": [],
      "anomalies": [],
      "omissions": [],
      "coverage": {}
    }
  }
}
```

## Mapping

| Font datum | Extension |
|---|---|
| Header `u32[9]` | `header.words[9]`; `pages` from word 0, `lineHeight` from word 4, `glyphTableOffset` from word 8 (292 on every shipped member); the other six words `typedUnidentified` with offsets, or `reserved-zero` where every shipped member stores zero |
| 256-byte char→glyph map at 36 | `charMap[256]` — one glyph index per ANSI code point; code points with no glyph point at a fallback slot |
| Glyph entries, 44 bytes each at 292 | `glyphs[]` — `index`, `advance` (u8 @0), `leftBearing` (s8 @18), `height` (the s16 @20 negated), `page` (u8 @24), `width` (s16 @26), `uv` (4×f32 @28: `u0`, `v0`, `u1`, `v1`), `sourceOffset` |
| The unread bytes of each entry | `glyphs[i].unread[]` — the bytes at 1–17, 19, 21–23, 25 as `typedUnidentified` with their values, or `reserved-zero` |
| Trailing 96 bytes | `trailer` — `typedUnidentified`, or `reserved-zero` where zero |
| File name pattern | `identity.face`, `size`, `weight`, `flags` |
| Atlas pages | `pages[]` — `index`, `texture` (unit ID), `material` (unit ID), `resolved`, the `coverageChannel: "alpha"` rule |
| Registry row | `fontList` — the matching `fontlist.txt` row and its index, or null |

The glyph count is `max(charMap) + 1`, which is the rule the decoder applies; a table longer than
that count is `omissions[] unreferenced-glyphs` with the count of entries no code point reaches
(they are still decoded into `glyphs[]`). Pixel rectangles are not derived into the unit: the UV
rectangle is the authored value, and the page's dimensions belong to the texture unit.

### The font-list unit

`materials/fonts/fontlist.txt` is one row per registered face: `"Face" size weight flags`. The
unit publishes `rows[]` with `index`, `face`, `size`, `weight`, `flags`, `offset`, and the
`vtmb:font:` ID the row composes (`<face lower-cased with spaces kept as the file spells them>_<size>_<weight>_<flags>`, zero-padded as the shipped names are), with `resolved` per row. A row whose font the install lacks is `anomalies[] fontlist-font-missing`; a font unit whose stem no row names records `anomalies[] fontlist-row-missing` on its own side. Both are joins, so they warn.

## Extension reference

| Key | Kind | Contents |
|---|---|---|
| `schemaVersion` | string | `1.0.0` |
| `identity` | object | `asset`, `fontPath`, `face`, `size`, `weight`, `flags`, `sourcePolicy` |
| `sourceResolution` | object | the member table |
| `header` | object | the nine words decoded |
| `charMap` | array | 256 glyph indexes |
| `glyphs` | array | the glyph records |
| `pages` | array | the atlas references |
| `fontList` | object or null | the registry join |
| `trailer` | object | the trailing 96 bytes' classification |
| `dependencies` | array | the reference table |
| `anomalies`, `omissions` | arrays | see below |
| `coverage` | object | the coverage object |

## Dependencies

| Role | Produced by |
|---|---|
| `texture` | each page index the glyph table uses, and each `-page<n>` pair the install holds |
| `material` | each `-page<n>.vmt` |
| `font-list` | the registry unit |

A page the glyph table references and the install lacks is `unresolved`, because the glyphs'
pixels are the font's own structure. A page pair the install holds and no glyph references is
`omissions[] unreferenced-page`.

## Anomalies and omissions

| Row | Meaning |
|---|---|
| `anomalies[] glyph-outside-page` | a glyph's `page` is not below `header.pages` |
| `anomalies[] uv-out-of-range` | a UV coordinate outside `[0, 1]` or an inverted rectangle |
| `anomalies[] charmap-index-out-of-range` | a `charMap` entry beyond the glyph count |
| `anomalies[] negative-width` | a glyph with a negative width |
| `anomalies[] fontlist-row-missing` | no registry row names this font |
| `anomalies[] fontlist-font-missing` | a registry row names a font the install lacks |
| `anomalies[] name-pattern-mismatch` | a stem that does not parse as `<face>_<size>_<weight>_<flags>` |
| `omissions[] unreferenced-glyphs` | entries past the last index the char map reaches |
| `omissions[] unreferenced-page` | a shipped page no glyph uses |
| `omissions[] file-longer-than-table` | bytes after the 96-byte trailer |

## Byte ledger owners

| Owner | Range |
|---|---|
| `header` | the 36-byte header |
| `charMap` | 256 bytes at 36 |
| `glyphs[i]` | the read fields of one 44-byte entry |
| `glyphs[i].unread` | the unread bytes of that entry, `mapped` under `typedUnidentified` or `reserved-zero` |
| `trailer` | the 96 bytes after the table |
| `excess` | any further bytes, `omitted-proven` |

For the list unit: `rows[i]` (one line including its terminator), `comments[i]`, `whitespace`.

## Coverage and validation

A complete font unit has zero `unresolved` and zero `unsupported` rows, and every unread byte is
either verified zero or carried under `typedUnidentified` with its offset. Validation re-reads the
table independently of the writer and compares the header, the char map and every glyph field;
checks every glyph's page and UV rectangle against the ranges above; and checks the list join in
both directions when both units are present.
