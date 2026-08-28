# Texture GLB seam

This document defines one binary glTF 2.0 unit for one VtMB texture identity. Format facts remain
owned by `docs/vtmb/texture_format.md`.

## Unit identity

```text
<VTMB>/Vampire/pack*.vpk -> materials/<texture>.tth
<VTMB>/Vampire/pack*.vpk -> materials/<texture>.ttz
  -> vtmb:texture:<texture>
  -> textures/<texture>.glb
```

The TTH and TTZ members resolve UP-first as one texture source pair. One normalized texture path
produces one texture GLB.

## GLB structure

A texture GLB is a scene-less glTF asset with one authoritative KTX 2.0 payload. It carries no
PNG/JPEG fallback, DDS sibling, Basis Universal derivative, or opaque TTH/TTZ mirror:

```text
texture.glb
|- JSON chunk
|  |- buffers and bufferViews
|  `- ELYSIUM_vtmb_texture 1.1.0
`- BIN chunk
   `- one complete KTX 2.0 payload
```

```json
{
  "extensionsUsed": [
    "ELYSIUM_vtmb_texture"
  ],
  "extensionsRequired": [
    "ELYSIUM_vtmb_texture"
  ],
  "extensions": {
    "ELYSIUM_vtmb_texture": {
      "schemaVersion": "1.1.0",
      "identity": {},
      "sourceResolution": {},
      "payload": {},
      "dimensions": {},
      "sourceFormat": {},
      "sampling": {},
      "mips": [],
      "faces": [],
      "omissions": [],
      "coverage": {}
    }
  }
}
```

## Mapping

| Texture datum | GLB mapping |
|---|---|
| Default 2D image | native KTX2 image levels in `payload.bufferView` |
| Dimensions and format | `dimensions`, `sourceFormat`, and KTX2 `vkFormat` |
| Mip pyramid | ordered KTX2 levels plus `mips` source-to-level records |
| Cubemap faces | standard KTX2 `+X,-X,+Y,-Y,+Z,-Z` faces after the recorded Source-to-glTF basis change |
| Reflectivity and source flags | extension fields |

DXT1, DXT3, and DXT5 map losslessly to native BC1, BC2, and BC3 KTX2 payloads. BGR888,
BGRA8888, RGBA8888, and UVWQ8888 map to the corresponding Vulkan byte formats without lossy
recompression. Color-space, normal-map, mask, iris, refract, and other sampling roles belong to
the referencing material binding. Source point/trilinear/anisotropic/clamp/no-mip flags remain in
`sampling`.

The VTF low-resolution image is a compiler-generated CPU colour-sampling optimization, not a
rendered mip. The seventh VTF 7.1 cubemap face is the Source low-end spheremap fallback, not one of
the six cubemap axes. Elysium implements neither obsolete Source mechanism. Their exact source
ranges are parsed, validated, and classified as `omitted-proven`; neither contributes bytes to the
KTX2 payload.

Some shipped units declare or physically contain an incomplete lower-resolution mip chain while
retaining a complete contiguous chain from the largest available image downward. The KTX2 begins
at that largest image and carries every following complete level. Bytes belonging only to a
partial or disconnected lower mip remain ledgered as `omitted-proven`; the exporter never pads or
fabricates texels to make malformed source storage KTX2-shaped.

## LaCroix examples

```text
<VTMB>/Vampire/pack001.vpk
  -> materials/models/character/teeth/upperteeth.{tth,ttz}
  -> vtmb:texture:models/character/teeth/upperteeth

<VTMB>/Unofficial_Patch
  -> materials/models/character/eyes/prince.{tth,ttz}
  -> vtmb:texture:models/character/eyes/prince
```

## Coverage

A complete texture GLB has zero `unresolved` and zero `unsupported` records. Schema `1.1.0`
requires one gapless `coverage.byteLedger` row for every selected TTH/TTZ member, with the same
states and invariants as the Character GLB seam. The ledger accounts for every source byte without
embedding an opaque source copy.

Export-time validation receives the selected source members and independently compares dimensions,
source format, every admitted mip/frame/face, decoded pixel values, source identities, and the KTX2
structure before atomically publishing the destination. A standalone validator can verify GLB and
KTX2 structure, payload and source hashes, range continuity, state totals, and the range-table
digest without the original install.
