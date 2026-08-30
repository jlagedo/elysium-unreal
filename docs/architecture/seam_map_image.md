# Image GLB seam

This document defines one binary glTF 2.0 unit for one raw image member of the install — a
`.tga` or `.bmp` file — as distinct from a VTF-wrapped texture. Particle sprites, the loose TGA
art below `materials/`, the shipped `screenshots/` and the Faceposer tool icons below
`gfx/hlfaceposer/` are image units. Shared rules are owned by `seam_map_unit_contract.md`; the
KTX 2.0 payload rules and the scene-less container are the same ones `seam_map_texture.md`
states for a texture.

An image is not folded into the texture seam because it is a different container with no VTF
header, no mip table and no TTZ twin, and its identity is keyed above `materials/` where the
texture key space does not reach.

## Unit identity

```text
<VTMB>/Vampire/pack*.vpk -> particles/<sprite>.tga
<VTMB>/Vampire/pack*.vpk -> gfx/hlfaceposer/<icon>.bmp
  -> vtmb:image:<install-relative path with extension>
  -> $ELYSIUM_EXPORT_V2_ROOT/images/<path>.glb
```

The key is the whole install-relative path, lower-cased, extension kept: `.tga` and `.bmp` are two
formats under one kind. The member resolves UP-first.

```text
uv run elysium export_v2 image-glb <path>
uv run elysium export_v2 images-glb
```

The merged install resolves 332 `.tga` (318 below `particles/`, the rest below `materials/` and
`screenshots/`) and 25 `.bmp`, all below `gfx/hlfaceposer/`.

## Source closure

| Source member | Role | GLB destination |
|---|---|---|
| `<path>.tga` or `<path>.bmp` | unit-selecting, the whole image | `sourceFormat`, `palette`, `footer`, the KTX2 level |

## GLB structure

```text
image.glb
|- JSON chunk
|  |- one buffer and one bufferView
|  `- extensions.ELYSIUM_vtmb_image
`- BIN chunk
   `- one complete KTX 2.0 payload, one level, one face, one layer
```

The unit is scene-less. There is exactly one `buffers` entry and one `bufferViews` entry at
offset 0 over buffer 0.

```json
{
  "extensionsUsed": ["ELYSIUM_vtmb_image"],
  "extensionsRequired": ["ELYSIUM_vtmb_image"],
  "extensions": {
    "ELYSIUM_vtmb_image": {
      "schemaVersion": "1.0.0",
      "identity": {},
      "sourceResolution": {},
      "payload": {},
      "dimensions": {},
      "sourceFormat": {},
      "palette": null,
      "footer": null,
      "orientation": {},
      "omissions": [],
      "coverage": {}
    }
  }
}
```

## KTX 2.0 payload

One level, `layerCount` 0, `faceCount` 1, `supercompressionScheme` 0, no key/value data, a data
format descriptor written for the exact `vkFormat`. The pixel bytes reach the payload unchanged
except for the row reordering `orientation` records.

| Source pixels | `vkFormat` | Value | Bytes |
|---|---|---|---|
| TGA 32-bit true-colour | `VK_FORMAT_B8G8R8A8_UNORM` | 44 | verbatim |
| TGA 24-bit true-colour | `VK_FORMAT_B8G8R8_UNORM` | 30 | verbatim |
| TGA 16-bit true-colour | `VK_FORMAT_A1R5G5B5_UNORM_PACK16` | 8 | verbatim |
| TGA 8-bit greyscale | `VK_FORMAT_R8_UNORM` | 9 | verbatim |
| TGA 8-bit colour-mapped | `VK_FORMAT_R8_UINT` | 13 | the indices verbatim; the palette in `palette` |
| BMP 24-bit | `VK_FORMAT_B8G8R8_UNORM` | 30 | verbatim, row padding removed |
| BMP 8-bit palettised | `VK_FORMAT_R8_UINT` | 13 | the indices; the palette in `palette` |
| BMP 32-bit | `VK_FORMAT_B8G8R8A8_UNORM` | 44 | verbatim |

A colour-mapped image keeps its indices and its palette apart: expanding the indices through the
palette is a consumer's job, and a second expanded copy would be a `derived` duplicate of data the
unit already states. A bit depth outside the table fails the export.

RLE-packed TGA data (image types 9, 10, 11) is decoded into the payload and its source span is
`derived`; uncompressed data (types 1, 2, 3) is `mapped`.

### Orientation

A TGA stores rows bottom-up unless descriptor bit 5 is set, and a BMP with a positive height
stores rows bottom-up; KTX2 stores rows top-down. The rows are reordered on export and
`orientation` states `sourceOrigin` (`bottom-left`, `top-left`, `bottom-right`, `top-right`),
`rowsReversed` and `columnsReversed`, so the transformation is exact and reversible. The TGA
descriptor's `xOrigin`/`yOrigin` screen offsets are decoded into `sourceFormat` and do not move
pixels.

## Extension reference

| Key | Kind | Contents |
|---|---|---|
| `schemaVersion` | string | `1.0.0` |
| `identity` | object | `asset`, `imagePath`, `container` (`tga` or `bmp`), `sourcePolicy` |
| `sourceResolution` | object | the member table |
| `payload` | object | `bufferView` (0), `mimeType` `image/ktx2`, `byteLength`, `sha256`, `vkFormat`, `vkFormatValue` |
| `dimensions` | object | `width`, `height`, `bitsPerPixel` |
| `sourceFormat` | object | the decoded header, below |
| `palette` | object or null | `entries` (count), `bitsPerEntry`, `firstIndex`, `colors[]` as spelled |
| `footer` | object or null | the TGA 2.0 footer, extension area and developer directory |
| `orientation` | object | the row/column rule |
| `omissions` | array | see below |
| `coverage` | object | the coverage object |

### `sourceFormat` for a TGA

| Field | Header offset |
|---|---|
| `idLength` | 0 |
| `colorMapType` | 1 |
| `imageType` | 2 |
| `colorMapFirst`, `colorMapLength`, `colorMapDepth` | 3, 5, 7 |
| `xOrigin`, `yOrigin` | 8, 10 |
| `width`, `height` | 12, 14 |
| `pixelDepth` | 16 |
| `descriptor`, `alphaBits`, `originRight`, `originTop`, `interleave` | 17 and its decoded bits |
| `imageId` | the `idLength` bytes after the header, as bytes and as Latin-1 text |

### `footer`

A TGA whose last 26 bytes read `TRUEVISION-XFILE.\0` carries a 2.0 footer. Its extension area is
decoded field by field (author name, comments, date/time stamp, job name and time, software id
and version, key colour, pixel aspect, gamma, colour-correction/postage-stamp/scan-line offsets,
attributes type); the referenced colour-correction table, postage stamp and scan-line table are
decoded when present. The developer directory's tags are `typedUnidentified` with their offsets,
because their meaning belongs to the authoring tool.

### `sourceFormat` for a BMP

`BITMAPFILEHEADER` (`type`, `size`, `reserved1`, `reserved2`, `offBits`) and `BITMAPINFOHEADER`
(`size`, `width`, `height`, `planes`, `bitCount`, `compression`, `sizeImage`, `xPelsPerMeter`,
`yPelsPerMeter`, `clrUsed`, `clrImportant`). A `compression` other than `BI_RGB` fails the
export. Each row's 4-byte alignment pad is `padding-zero` when zero and `omitted-proven row-pad`
otherwise.

## Omissions

| Role | Meaning |
|---|---|
| `row-pad` | BMP row alignment bytes the source stores non-zero |
| `trailing-bytes` | bytes after the image data and before a footer, or after the footer, that no structure claims |
| `unused-palette-entries` | palette entries beyond the largest index the pixels use, stated as a count; the entries are still in `palette` |
| `empty-member` | a zero-byte file |

## Byte ledger owners

| Owner | Member | Range |
|---|---|---|
| `tga.header` | TGA | the 18-byte header |
| `tga.imageId` | TGA | the image identification field |
| `tga.colorMap` | TGA | the colour-map data |
| `tga.image` | TGA | the pixel data, `mapped` or `derived` |
| `tga.extensionArea`, `tga.colorCorrectionTable`, `tga.postageStamp`, `tga.scanLineTable`, `tga.developerDirectory`, `tga.developerArea[i]` | TGA | the 2.0 structures |
| `tga.footer` | TGA | the 26-byte footer |
| `tga.trailing` | TGA | unclaimed bytes, `omitted-proven` |
| `bmp.fileHeader`, `bmp.infoHeader` | BMP | the two headers |
| `bmp.palette` | BMP | the colour table |
| `bmp.row[i]` | BMP | one row's pixels |
| `bmp.row[i].pad` | BMP | one row's alignment pad |
| `bmp.gap` | BMP | bytes between the palette and `offBits` |

## Coverage and validation

A complete image unit has zero `unresolved` and zero `unsupported` rows. Export-time validation
re-decodes the image independently of the writer and compares `vkFormat`, extent, and every pixel
byte of the level against the KTX2 payload after applying `orientation` in reverse. The
standalone validator verifies the container, the scene-less core, the single buffer and view, the
payload hash and length, the KTX2 header, level index and data format descriptor against the
declared `vkFormat`, and the ledger's continuity, totals and digest.
