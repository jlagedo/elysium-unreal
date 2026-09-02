# Texture GLB Exporter seam

This document defines the isolated Texture GLB Exporter's complete binary glTF 2.0 (`.glb`) unit
for one VtMB texture identity. The unit is the sole input of `uv run elysium import textures`,
which lands it as an Unreal texture asset ("Import" below); the runtime reaches it through that
asset, never through the GLB.

The source-format facts referenced by this contract remain owned by
`docs/vtmb/texture_format.md`; the colour-space facts remain owned by `docs/vtmb/color_gamma.md`.

Referencing GLB units are defined by `docs/architecture/seam_map_material.md` and
`docs/architecture/seam_map_character.md`.

## Flow and source notation

An arrow names a member inside a source container:

```text
<VTMB>/Vampire/pack001.vpk -> materials/models/character/teeth/upperteeth.tth
```

Loose files use the same install-relative member name:

```text
<VTMB>/Unofficial_Patch -> materials/models/character/eyes/prince.tth
```

Source selection is **UP-first** for every member independently:

```text
Unofficial_Patch loose -> retail loose -> retail VPK
```

The winning TTH and TTZ members may therefore come from different source containers.

## Texture unit

One UP-first TTH path produces one texture GLB:

```text
materials/<texture>.tth
materials/<texture>.ttz
  -> vtmb:texture:<texture>
  -> $ELYSIUM_EXPORT_V2_ROOT/textures/<texture>.glb
```

The unit key is the normalized path below `materials/`, lowercased, forward-slashed, without the
`.tth`/`.ttz` suffix. The TTH is required and selects the unit; the TTZ is optional, because a
recompiled or patch-re-exported texture may carry its whole image inline in the TTH.

The public commands are:

```text
uv run elysium export_v2 texture-glb <texture>
uv run elysium export_v2 textures-glb
```

The argument tolerates a `materials/` prefix and a `.tth`/`.ttz` suffix. The corpus form selects
every install `materials/**.tth` member the UP-first install index resolves, **plus** every map
BSP's own PAKFILE `.tth` members (`source_keys`; "Probes without a `.ttz`" below) — an install
member of the same spelling wins where one exists, today never observed.

The feature's code is isolated from `formats/tex_to_png.py` and from the map and character texture
bakes: format aggregation lives below `elysium_pipeline.formats.texture_glb`, the product writer is
`elysium_pipeline.exporters.texture_glb`, and its independent structural reader is
`elysium_pipeline.validation.texture_glb`.

It owns:

- the texture's complete admitted mip pyramid as one native KTX 2.0 payload;
- the source pixel format, dimensions, frame count and cubemap face set;
- the VTF header's reflectivity, bump scale, start frame and raw flag word;
- the decoded Source sampling flags;
- the source-mip-to-KTX-level and source-face-to-KTX-face mappings;
- the evidence for every source range that contributes no payload byte;
- a gapless byte ledger over each selected source member.

It does not own colour space, texture role, or sampler binding as *meaning*: whether a unit is
albedo, a normal map, an alpha mask, an iris or a refraction field is a property of the binding in
the referencing material, not of the texture identity.

## Source closure covered by the specification

| Source member | Texture ownership | GLB destination |
|---|---|---|
| `<VTMB>/Vampire/pack*.vpk -> materials/<texture>.tth` | unit-selecting header, mip table, embedded VTF header, low-resolution image and inline mips | `sourceFormat`, `sampling`, `dimensions`, and the inline part of the KTX2 levels |
| `<VTMB>/Vampire/pack*.vpk -> materials/<texture>.ttz` | optional zlib image stream holding the larger mips | the external part of the KTX2 levels |

Both rows resolve through the UP-first policy independently.

### PAKFILE-origin units

A map's PAKFILE lump contributes texture units the install never carries: baked reflection
probes, one `.tth` (plus a `.ttz` when the zip carries one — always optional, same as the
install-origin row above) per probe. Their identity and destination follow the same shape as any
other unit:

```text
maps/<map>.bsp -> PAKFILE -> materials/maps/<map>/<stem>.tth
                              materials/maps/<map>/<stem>.ttz  (optional)
  -> vtmb:texture:maps/<map>/<stem>
  -> $ELYSIUM_EXPORT_V2_ROOT/textures/maps/<map>/<stem>.glb
```

Each member's `origin` is `bsp-pakfile`, nesting the winning `maps/<map>.bsp` install entry's own
origin (`pakfile_origin`, `formats/unit_contract/origin.py`) — the probe did not resolve UP-first
on its own; it rides inside the BSP that did. `sourceResolution.policy` still reads `"up-first"`
for a PAKFILE-origin unit: the policy describes how the *carrying* BSP was selected, and the BSP
resolved UP-first the ordinary way before its PAKFILE lump was ever opened.

### Probes without a `.ttz`

SF-1.2 investigation (read-only, 2026-08-31), against the 108 map BSPs' PAKFILE zips ahead of
SF-1.3. The 1,325 baked reflection probes embedded there (108 `cubemapdefault` fallback probes +
1,217 positioned `c<x>_<y>_<z>` probes) are ordinary VtMB cubemap textures, decoded the same way
as any other `.tth`/`.ttz` pair (`texture_format.md` → "Cubemaps"); 575 of them ship as `.tth`-only
members with no `.ttz` twin. They are not header-only stubs and not a distinct member kind: each
one's whole admitted mip pyramid — all 7 VTF faces (6 axes plus the dropped low-end spheremap),
down to 1×1 — already fits inside the `.tth`'s inline image range, so the map compiler wrote no
external `.ttz` stream at all. Confirmed on all 575: the outer mip table's declared total `.ttz`
length is `0`; the bytes available after the embedded VTF header already exceed `7 ×` the full-res
face size; and `tex_to_png.decode_cubemap(tth, ttz=None)` decodes all six kept faces into valid,
equal-sized, square RGBA images for every one — zero failures, zero short reads.

Numbers: 552/575 are uncompressed `BGR888` (enum 3) — 32×32 (503), 64×64 (41), 128×128 (8) — and
23/575 are `DXT5` (enum 15), all 32×32 `cubemapdefault` probes. It is not a clean format split: 27
positioned `BGR888` probes (2 in `sm_diner_1`, 23 in `sp_giovanni_2a`, one each in
`sp_giovanni_3`/`sp_giovanni_4`) carry a `.ttz` despite being uncompressed, and 78 of the 108
`cubemapdefault` probes (mostly `DXT5`, same format as the 23 that don't) spill their larger mips
into a `.ttz`. Whether one probe's pyramid lands wholly inline or splits across a `.ttz` is the
ordinary per-texture inline/external size threshold the map compiler applies to every VtMB
texture, not a property of "probe" as a kind.

What SF-1.3 did: nothing special. The "Texture unit" section above already stated the `.ttz` was
optional, and `texture_glb.decode` already carried `closure.ttz` as `Optional[SourceMember]`
throughout (`has_external_file = closure.ttz is not None`). SF-1.3's PAKFILE enumeration paired
each probe's `.tth` member with its `.ttz` sibling when the zip carried one, and passed `None`
when it did not — the same source closure a loose or VPK texture with no `.ttz` already produced.
One shape neither this investigation nor SF-1.3's first pass covered: 2 of the 1,325 probes ship a
`.ttz` alongside an already-complete inline pyramid, a superfluous stream `decode_texture` admits
as `admittedDeclaredInlineChain` rather than mis-describing as a single-level
`admittedFullResolutionImageOnly` admission (fixed after an independent review; see
`test_a_declared_inline_chain_superfluous_ttz_exports_and_validates`).

### LaCroix examples

```text
<VTMB>/Vampire/pack001.vpk
  -> materials/models/character/teeth/upperteeth.{tth,ttz}
  => vtmb:texture:models/character/teeth/upperteeth

<VTMB>/Unofficial_Patch
  -> materials/models/character/eyes/prince.{tth,ttz}
  => vtmb:texture:models/character/eyes/prince
```

## Binary glTF layout

The texture unit is an ordinary GLB 2.0 container with exactly two chunks in the required order:

```text
texture.glb
|- header      magic 'glTF', version 2, total length
|- chunk 0     JSON  (0x4E4F534A), UTF-8, padded to 4 bytes with 0x20
|  |- one buffer and one bufferView
|  `- extensions.ELYSIUM_vtmb_texture
`- chunk 1     BIN   (0x004E4942), padded to 4 bytes with 0x00
   `- one complete KTX 2.0 payload
```

The unit is **scene-less**. It carries no `scenes`, `nodes`, `meshes`, `images`, `textures` or
`samplers`, and declaring any of them fails validation: a core `images` entry would be a second,
weaker statement of the same pixels. There is exactly one `buffers` entry whose `byteLength` is the
KTX2 payload length, and exactly one `bufferViews` entry at offset 0 over buffer 0. The JSON is
serialized with compact separators and rejects `NaN` and infinity, so one source closure yields one
byte-identical product.

The JSON chunk declares one custom namespace, used and required:

```json
{
  "asset": { "version": "2.0", "generator": "Elysium Texture GLB Exporter" },
  "extensionsUsed": ["ELYSIUM_vtmb_texture"],
  "extensionsRequired": ["ELYSIUM_vtmb_texture"],
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

The extension is **required** rather than optional because the payload is reachable only through
it: a consumer that ignores `ELYSIUM_vtmb_texture` sees a buffer and no image.

## KTX 2.0 payload

The BIN chunk is one complete, standalone KTX 2.0 file — identifier, header, level index, data
format descriptor and level data — not a bare pixel blob. It carries no key/value data, no
supercompression global data and no supercompression scheme, so `levels[i].byteLength` equals
`levels[i].uncompressedByteLength` for every level.

| KTX2 field | Value |
|---|---|
| `vkFormat` | the Vulkan format the source pixel format maps to losslessly |
| `typeSize` | `1` |
| `pixelWidth`, `pixelHeight` | the largest admitted mip's dimensions |
| `pixelDepth` | `0` |
| `layerCount` | the VTF frame count when it exceeds 1, otherwise `0` |
| `faceCount` | `6` for a cubemap, otherwise `1` |
| `levelCount` | the number of admitted mip levels |
| `supercompressionScheme` | `0` |
| `kvdByteOffset`, `kvdByteLength` | `0` |
| `sgdByteOffset`, `sgdByteLength` | `0` |

The level index is ordered largest level first and the level data is stored smallest level first,
as the KTX2 specification requires. Each level's `byteOffset` is aligned to the least common
multiple of the format's texel-block size and 4, and the padding between levels is zero. Within one
level the images run frame-major then face-minor, which is the KTX2 image order.

The data format descriptor is written for the exact `vkFormat`: descriptor type 0, vendor 0,
version 2, one basic block, the format's texel-block dimensions and `bytesPlane0`, and one sample
per channel. Block-compressed formats use the `KHR_DF_MODEL_BC1A`, `BC2` and `BC3` colour models
with a 4×4 texel block; uncompressed formats use `KHR_DF_MODEL_RGBSDA`.

### Format mapping

Every admitted VtMB pixel format maps to a native Vulkan format with no recompression, no
transcoding and no Basis Universal derivative:

| Source format | Enum | `vkFormat` | Value | Block | Bytes |
|---|---|---|---|---|---|
| `RGBA8888` | 0 | `VK_FORMAT_R8G8B8A8_UNORM` | 37 | 1×1 | 4 |
| `BGR888` | 3 | `VK_FORMAT_R8G8B8_UNORM` | 23 | 1×1 | 3 |
| `BGRA8888` | 12 | `VK_FORMAT_B8G8R8A8_UNORM` | 44 | 1×1 | 4 |
| `DXT1` | 13 | `VK_FORMAT_BC1_RGBA_UNORM_BLOCK` | 133 | 4×4 | 8 |
| `DXT3` | 14 | `VK_FORMAT_BC2_UNORM_BLOCK` | 135 | 4×4 | 16 |
| `DXT5` | 15 | `VK_FORMAT_BC3_UNORM_BLOCK` | 137 | 4×4 | 16 |
| `UVWQ8888` | 23 | `VK_FORMAT_R8G8B8A8_UINT` | 41 | 1×1 | 4 |

`BGR888` is the one format whose bytes are rewritten: each 3-byte texel is reversed into the
`R8G8B8` order the Vulkan format names. Every other format's bytes reach the payload unchanged.
`UVWQ8888` keeps its signed two's-complement bytes verbatim under a `_UINT` format and a linear
transfer function, because the DUDV field is a displacement vector rather than colour; the semantic
conversion belongs to the referencing `Refract` material.

An unadmitted `highResImageFormat` fails the export rather than producing an approximation.

## Extension reference

| Root key | Kind | Contents |
|---|---|---|
| `schemaVersion` | string | `1.1.0` |
| `identity` | object | `asset` (stable texture ID), `texturePath` |
| `sourceResolution` | object | `policy`, and `members[]` of `role`, `path`, `origin`, `byteLength`, `sha256` |
| `payload` | object | the KTX2 reference and identity |
| `dimensions` | object | `width`, `height`, `declaredWidth`, `declaredHeight`, `frames`, `faces`, `mipCount` |
| `sourceFormat` | object | the decoded TTH and VTF header fields |
| `sampling` | object | the decoded Source sampling flags |
| `mips` | array | one source-mip-to-KTX-level row per emitted level |
| `faces` | array | one source-face-to-KTX-face row per cube face, empty for a 2D texture |
| `omissions` | array | one row per source range that contributes no payload byte |
| `coverage` | object | `mapped[]`, `byteLedger[]`, `unresolved[]`, `unsupported[]` |

A member `origin` is either `{"kind": "loose", "root": <install subdirectory>}` or
`{"kind": "vpk", "container": <pack file>, "offset": …, "size": …}`, from the same resolution the
byte ledger is written against.

### `payload`

| Field | Meaning |
|---|---|
| `bufferView` | always `0` |
| `mimeType` | `image/ktx2` |
| `byteLength` | the KTX2 file's length, equal to the bufferView's |
| `sha256` | SHA-256 of the KTX2 file |
| `vkFormat`, `vkFormatValue` | the Vulkan format name and value |
| `textureType` | `2d`, `2d-array`, `cubemap` or `cubemap-array` |
| `supercompression` | `none` |

### `sourceFormat`

| Field | Meaning |
|---|---|
| `tthVersion` | the TTH outer version; `1` |
| `vtfVersion` | the embedded VTF version, `7.0` or `7.1` |
| `flags` | the raw VTF flag word |
| `startFrame` | the VTF start frame |
| `reflectivity` | the `vtex` linear average albedo, three floats |
| `bumpScale` | the VTF bump scale |
| `sourceFormat`, `sourceFormatEnum` | the high-resolution image format name and enum |
| `sourceWidth`, `sourceHeight` | the VTF header's declared image dimensions |
| `sourceMipCount` | the chain length the outer table and the declared dimensions jointly admit |
| `tthMipTableCount` | the TTH outer mip count |
| `vtfMipCount` | the embedded VTF header's mip count |
| `declaredInlineMips` | the TTH's declared inline mip count |
| `resolvedInlineMips` | the inline mip count the meaningful TTH blob actually holds |
| `zlibStreamComplete` | whether the TTZ stream terminated with a valid zlib trailer |

`reflectivity` is a decodable input, not dead header space: VtMB multiplies every ambient-cube
bounce ray by it (`docs/vtmb/sky-ambience.md`), so it is carried rather than dropped.

`declaredInlineMips` and `vtfMipCount` are restated as the source wrote them, including where they
are stale; `resolvedInlineMips` and `sourceMipCount` are what the file's own byte extents support.

### `sampling`

The Source texture flags, decoded to booleans, all read off `sourceFormat.flags`:

| Field | Bit |
|---|---|
| `pointSample` | `0x0001` |
| `trilinear` | `0x0002` |
| `clampS` | `0x0004` |
| `clampT` | `0x0008` |
| `anisotropic` | `0x0010` |
| `noMip` | `0x0100` |
| `noLod` | `0x0200` |
| `allMips` | `0x0400` |

These are filtering and addressing state, so they survive as texture-owned data. The remaining
flags stay available in the raw `flags` word.

### `mips`

One row per emitted KTX2 level, in level order:

| Field | Meaning |
|---|---|
| `ktxLevel` | the KTX2 level index; `0` is the largest |
| `sourceMip` | the source mip index the level came from, counted smallest-first as VTF stores them |
| `width`, `height` | the level's dimensions |
| `images` | the number of images in the level, `frames × faces` |
| `byteLength` | the level's total byte length |
| `sha256` | SHA-256 of the level's concatenated images |

`sourceMip` descends as `ktxLevel` ascends. It is not required to reach 0: where the source stores
no complete lowest mip, the chain ends at the smallest complete level and the missing indexes are
recorded in `omissions`.

### `faces`

Empty for a 2D texture. For a cubemap, one row per KTX2 face in the standard
`+X, -X, +Y, -Y, +Z, -Z` order:

| Field | Meaning |
|---|---|
| `ktxFace` | the KTX2 face index |
| `name` | the axis name |
| `sourceFace` | the VTF face index the KTX2 face was taken from |
| `transform` | the in-face rotation applied, one of `identity`, `rotate-cw`, `rotate-ccw`, `rotate-180` |

The basis change from Source's X-forward, Y-left, Z-up cube to glTF's Y-up right-handed cube is the
face permutation source `0, 1, 4, 5, 3, 2` — glTF `+Y` takes Source `+Z`, glTF `+Z` takes Source
`-Y` — with the per-face rotation the table records. The rotation is applied to the stored bytes,
so a block-compressed face is rotated by permuting its blocks and then rewriting each block's
selector bits in place; the endpoint pairs are untouched. This is exact for every admitted format,
and it is what makes the payload a valid cubemap in its own right rather than a Source cube plus a
rule the consumer has to know.

### `omissions`

Every source range that contributes no payload byte is named here with its evidence, as is every
image the source declares but does not contain. Every row carries its `role` and a `reason` string
naming why the range is omitted, plus the per-role fields below. Five roles occur:

| Role | Meaning |
|---|---|
| `low-res-cpu-sample` | the VTF low-resolution image |
| `low-end-spheremap` | the VTF 7.1 seventh cubemap face |
| `incomplete-lower-mips` | source mips the file does not store as a complete level |
| `unexplained-leading-image-storage` | image storage the declared mip chain does not account for |
| `primary-image-not-recoverable` | the declared full-resolution image is not present in the source |

The **low-resolution image** is a `vtex`-generated CPU colour-sampling optimization that Source
exposes through `ITexture::GetLowResColorSample`, not a rendered mip and not authored artwork.
Elysium implements no such query, so the range is parsed, validated and ledgered without
contributing bytes. The row carries `byteLength` (the inline range), `declaredByteLength`,
`externalByteLength` (the same image where the TTZ rather than the TTH carries it), and the
declared `format`, `width` and `height`.

A blob ahead of the image stream counts as the colour sample only when it is no larger than
`declaredByteLength`, or smaller than the smallest emitted level and therefore too small to be a
level at all. That admissibility test is what separates a thumbnail from image data, and it is
load-bearing: without it a stream larger than the declared chain gets the chain slid onto its tail
and every level below the first is composed from unrelated bytes. The bound is stated in terms of
the emitted levels so that a reader with only the published unit can re-check it.

The **seventh cubemap face** is Source's obsolete low-end spheremap fallback, dropped in VTF 7.5
and not one of the six cubemap axes. The row carries the total `byteLength` it occupies.

**Incomplete lower mips** are recorded where the source does not physically contain a complete,
contiguous chain down to 1×1. The row carries `sourceMips`, `representedOnlyByLedger` and
`zlibStreamComplete`. The exporter never pads or fabricates texels to make malformed source storage
KTX2-shaped.

**Unexplained leading image storage** is recorded where a leading blob fails the admissibility test
above. Some shipped units store the full-resolution image more than once and no pyramid at all, so
the declared chain describes nothing in the stream. The payload then admits the full-resolution
image alone, and the row carries `byteLength`, the `sourceMips` that were consequently not emitted,
`streamFullResolutionImages` (the inflated stream's size in whole declared full-resolution images)
and `admittedFullResolutionImageOnly`.

**A non-recoverable primary image** is recorded where the largest complete level the source
contains is smaller than the resolution the header declares — a missing TTZ, a zlib stream that
ends early, or a mip table whose offsets run backwards. The row carries `declaredWidth`,
`declaredHeight`, `recoveredWidth`, `recoveredHeight` and `zlibStreamComplete`. The unit still
publishes, because what it carries is everything the user's install holds, but it publishes loudly:
the exporter warns on the unit and the corpus command reports how many units published with
warnings.

## Non-canonical source storage

Retail compiler bookkeeping is not uniformly canonical, and the specification is written against
what the bytes support rather than what the headers claim. The decoder resolves these cases from
the file's own extents:

- The TTH outer mip count, the embedded VTF mip count and the declared inline mip count disagree
  with each other and with the dimension-derived chain. The chain length is the smaller of the
  outer count and what the dimensions admit; the inline count is reduced until the inline mips fit
  the meaningful TTH blob.
- A declared meaningful TTH or TTZ length is followed by arbitrary compiler allocation fill. The
  fill is claimed as `padding-zero` when it is zero and `omitted-proven` when it is not.
- A TTZ stream ends without a valid zlib trailer. The decode retains every byte the stream produced
  before the failure and records `zlibStreamComplete: false`.
- A unit stores a complete contiguous chain from some level downward but not the whole pyramid. The
  payload begins at the largest available image and carries every following complete level; the
  rest enters `omissions` and the ledger.
- The declared low-resolution image is internally inconsistent, or the inflated TTZ carries image
  bytes ahead of the admitted chain. Those bytes contribute nothing to the payload and are counted
  in `low-res-cpu-sample.externalByteLength`.
- The mip table's `ttz_prefix` column is stale or non-monotonic. Per-mip TTZ spans are used only
  when that column is monotonic, starts at 0 and ends at the declared meaningful length; otherwise
  the stream is claimed as one range.

`dimensions.width` and `height` are the emitted payload's largest level, and
`sourceFormat.sourceWidth` and `sourceHeight` are what the VTF header declares. They agree wherever
the source stores a complete top-level image; where a unit's primary image is not physically
present, they differ, and that difference is the product's statement that the largest recoverable
level is smaller than the declared one.

## Coverage

Coverage is stated in one vocabulary here, because a texture unit has no semantic-record layer
separate from its bytes: `coverage.mapped[]` names the extension sections that carry the decode,
and `coverage.byteLedger[]` grades every source byte.

A complete texture GLB has zero `unresolved` and zero `unsupported` records.

Schema `1.1.0` requires one `coverage.byteLedger` row for every member listed by
`sourceResolution.members`:

| Field | Meaning |
|---|---|
| `sourcePath` | the member's install-relative path, matching its `sourceResolution` row |
| `sourceSha256` | SHA-256 of the member's bytes, matching its `sourceResolution` row |
| `byteLength` | the member's byte length |
| `accountedBytes` | bytes claimed by the range table; equal to `byteLength` |
| `coveragePercent` | `100.0` |
| `stateBytes` | claimed bytes per state, key-sorted |
| `rangesSha256` | digest of the range table |
| `ranges` | the gapless ordered range table |

A range row is `{"offset", "length", "state", "owner"}`. Rows are ordered by `offset`, each row
begins where the previous one ends, the first begins at 0, and the last ends at `byteLength`.
`rangesSha256` is the SHA-256 of the UTF-8 encoding of

```text
json.dumps({"path": sourcePath, "byteLength": byteLength, "ranges": ranges},
           sort_keys=True, separators=(",", ":"))
```

which is the same digest rule the Character GLB seam uses.

The allowed states are:

| State | Meaning |
|---|---|
| `mapped` | a header, table or image range decoded into the extension or the KTX2 payload |
| `derived` | a compressed range whose decoded content is represented; the TTZ's zlib spans |
| `omitted-proven` | an evidence-backed omission, carrying its reason in `omissions` |
| `reserved-zero` | a declared field the source stores as zero |
| `padding-zero` | alignment or unreferenced storage the source stores as zero |

`reserved-zero` and `padding-zero` are verified: claiming either over a non-zero source byte aborts
publication. Publication also fails when ranges overlap, leave even one byte unclaimed, disagree
with the source hash or length, or when a member has no ledger row.

`owner` is the decoder's path to the record that paid for the range, so a range and the extension
record it belongs to name the same place:

| Owner | Member | Range |
|---|---|---|
| `tth.header` | TTH | the 12-byte outer header |
| `tth.mip-table` | TTH | the `mipCount + 1` offset/prefix pairs |
| `vtf.header` | TTH | the 64-byte embedded VTF header |
| `vtf.low-res-cpu-sample` | TTH | the inline low-resolution image range |
| `vtf.mip[m].frame[f].face[c]` | TTH | one admitted inline image |
| `vtf.mip[m].frame[f].omitted-image[c]` | TTH | one inline image the payload does not carry |
| `tth.trailing-compiler-fill` | TTH | allocation fill past the declared meaningful length |
| `ttz.mip[m].zlib` | TTZ | one admitted mip's compressed span |
| `ttz.mip[m].omitted-zlib` | TTZ | one omitted mip's compressed span |
| `zlib-primary-mip-stream` | TTZ | the whole stream, where the prefix column resolves no per-mip spans |
| `ttz.trailing-compiler-fill` | TTZ | allocation fill past the declared meaningful length |

This is **100% byte accountability without an opaque source mirror**. The unit embeds no TTH or TTZ
copy, no DDS sibling and no PNG or JPEG fallback; the ledger is what makes the absence of a mirror
safe.

## Validation

Export-time validation receives the selected source members and runs before the destination is
published. It re-reads the members' bytes, re-hashes them against the declared source identities,
and verifies every range a `-zero` state claims really is zero. It then re-decodes the texture
independently of the writer and compares the level count, the `vkFormat` and every level's decoded
pixel bytes against the KTX2 payload — which covers each admitted mip, face and frame, since a
level is the concatenation of its images. Only then is the GLB written to a temporary sibling and
atomically renamed over the destination.

The re-decode is a payload check, not a whole-extension check. `dimensions` and
`sourceFormat.sourceFormatEnum` are held against the KTX2 payload's own extent and format, and
each `mips` row against its level's digest; `sampling` and the `faces` rows are carried without a
cross-check, because neither has a counterpart in the payload to disagree with.

The standalone validator reads a published GLB with no install present and verifies the container,
the chunk order, the scene-less core, the extension's presence and version, the identity prefix,
the single buffer and bufferView, the payload hash and length, the KTX2 header, level index and
data format descriptor against the declared `vkFormat`, each level's extent and digest, the
ledger's range continuity, state totals, source identities and range-table digest, and the absence
of any embedded opaque source payload.

Zero-range honesty is the one ledger claim it cannot re-check: proving a `padding-zero` range is
zero needs the source bytes, so that check runs only in the export-time path above.

## Import

`uv run elysium import textures` turns every published texture unit into one Unreal texture asset
below `/ElysiumBaked/Textures`. It is the second slice of the seam migration
(`docs/project/seam_migration.md` → "Settled", "Slice 2 is textures"), and the template the
material, model and map lanes follow: the unit is the only input, the asset carries everything
the unit knows, and nothing here reads the install.

**Consumers beyond materials (R6.6).** The UI draws its art straight off this lane's `T_` assets
— HUD frames and icons, the 72 use icons, the sheet chrome, the chargen pages, the sign
backgrounds, the title lockup, the clan sigils, the feed-vision mask — by the same install path
the screen names (`ElysiumUI::ArtTexture`, `docs/architecture/ui-architecture.md` → "9. Art from
assets"); `FElysiumContentPaths::BakedTexture` is the naming rule below in C++, pinned against
`asset_path_for` by `Elysium.Substrate.UiArt`. A UI material whose VMT names another texture
resolves through the material lane's `MI_` (its `BaseTexture`), never through a second decode.

### Identity and naming

```text
vtmb:texture:<dir>/<stem>  ->  /ElysiumBaked/Textures/<dir>/T_<safe stem>          2D
                           ->  /ElysiumBaked/Textures/<dir>/TC_<safe stem>         cubemap
                           ->  /ElysiumBaked/Textures/<dir>/TA_<safe stem>         2D array (frames > 1)
                           ->  /ElysiumBaked/Textures/<dir>/T_<safe stem>_linear   the linear twin, when one exists
```

`<dir>` is the unit's directory below `materials/`, kept as package folders; `<safe stem>` is
`asset_names.safe_name` over the file stem. The exact original path lives in the provenance, so
folding is never a loss. The legacy `/ElysiumBaked/Shared/Textures` package is not touched by
this lane; the material slice deletes it once no material references it.

### Two phases, one command

**Phase 1 — stage (offline Python, `importers/textures.py`).** For every unit under
`$ELYSIUM_EXPORT_V2_ROOT/textures/**` the lane parses the KTX2 payload, decodes every
block-compressed level to 8-bit pixels with its own BC1/BC2/BC3 decoder, and writes one
**uncompressed** DX10 DDS (`B8G8R8A8_UNORM`, every authored level, frames as array slices, cube
faces in Unreal order — rotated block-exactly first, decoded after), a provenance sidecar and one
`manifest.json` under `$ELYSIUM_WORK_ROOT/import/textures/`. An already-uncompressed source is
staged as it is (see the compression table). Phase 1 also scans every material unit's
`dependencies[]` to learn each texture's binding parameters, which decide the colour/data role
below. Phase 1 is pure Python and is what the pytest suite exercises with synthetic units.

*Why the staged DDS is uncompressed.* Unreal 5.8 cannot import a block-compressed DDS at all:
both import paths — the Interchange translator
(`Engine/Plugins/Interchange/Runtime/Source/Import/Private/Texture/InterchangeDDSTranslator.cpp`,
`GetTexturePayloadData`) and the legacy factory
(`Engine/Source/Editor/UnrealEd/Private/Factories/EditorFactories.cpp`, the DDS branch of
`UTextureFactory`) — map the file's DXGI format through `UE::DDS::DXGIFormatGetClosestRawFormat`,
whose table carries no BC entry, and refuse the file with `DDS DXGIFormat not supported : 71 :
BC1_UNORM`. `FTextureSource` holds only uncompressed formats anyway, so nothing is lost by
decoding before import that would not have been lost by Unreal decoding after it; the difference
is that the decode is now the lane's, spec-exact and shared with phase 3, so the measured delta is
Unreal's re-encode alone. The KTX2 in the unit remains the block-exact record.

**Phase 2 — import (headless editor, `pipeline/unreal/import_textures.py`).** Launched like the
corpus bake (`-run=pythonscript`), it reads the manifest, imports each DDS with an
`AssetImportTask`, applies the settings the manifest states, attaches the provenance record,
stamps the recipe (`bake_lib.RECIPE_TAG`), saves, and prunes every asset under
`/ElysiumBaked/Textures` the manifest does not name. It also writes each built asset's mip 0 back
beside its staged DDS (`<stem>.built.dds`) so phase 3 can measure the re-encode.

**Phase 3 — measure (offline Python).** For each unit with a `.built.dds` (BC or BGRA8, whatever
Unreal built), decode both sides with the same decoder and record the max and mean per-channel
texel delta in the run report. The loss is accepted; it is reported so it stays a number rather
than a belief.

### The staging manifest

`manifest.json` is the contract between the two phases. One entry per asset (a twin is its own
entry pointing at the same DDS):

```json
{
  "schemaVersion": "1.0.0",
  "settingsVersion": "elysium-texture-import-v2",
  "packageRoot": "/ElysiumBaked/Textures",
  "select": null,
  "pruneScope": "/ElysiumBaked/Textures/",
  "keep": [],
  "stageFailures": [],
  "assets": [
    {
      "assetPath": "/ElysiumBaked/Textures/hud/signs/T_notepad_yellow",
      "class": "Texture2D",
      "dds": "hud/signs/notepad_yellow.dds",
      "stagedFormat": "bgra8",
      "provenance": "hud/signs/notepad_yellow.provenance.json",
      "unit": "vtmb:texture:hud/signs/notepad_yellow",
      "unitGlb": "textures/hud/signs/notepad_yellow.glb",
      "unitSha256": "…",
      "twinOf": null,
      "role": "colour",
      "roleEvidence": ["$basetexture"],
      "roleConflict": false,
      "srgb": true,
      "compression": "default",
      "mipGen": "leave-existing",
      "addressX": "wrap", "addressY": "wrap",
      "filter": "default",
      "neverStream": true,
      "expected": {"width": 512, "height": 1024, "mips": 11, "faces": 1, "slices": 1},
      "recipe": {"unitSha256": "…", "settingsVersion": "elysium-texture-import-v2",
                 "role": "colour", "srgb": true, "compression": "default", "twin": false}
    }
  ]
}
```

`class` is `Texture2D`, `TextureCube` or `Texture2DArray`. `dds` and `provenance` are relative to
the staging root. The recipe stamp is `bake_lib.recipe_fingerprint("textures", assetPath, recipe)`
— the sha256 of the canonical JSON of `["textures", assetPath, recipe]` — so a unit whose GLB
changes, or a settings-version bump, re-imports; nothing else does.

`select` is the `--select` **directory** of unit keys the stage ran under (`hud/signs`; `hud`
selects `hud/**` and nothing under `hudson/`), lower-cased, or `null` for the whole corpus.
`pruneScope` is the package folder the editor phase may prune, always with a trailing slash: the
root, or the selected directory folded component by component the way asset paths are folded
(`odd dir` → `/ElysiumBaked/Textures/odd_dir/`). The editor phase uses it verbatim and compares
case-insensitively, so a scoped run never deletes the rest of the mount.

`keep` protects assets: for every unit the stage could not read or plan, every asset path its key
could own (`T_`, `TC_`, `TA_`, each with its `_linear` twin) is listed, and the editor phase never
prunes a path in `keep`; `stageFailures` names those units with their reasons. A transient read
error therefore costs one relaunch, never a previously good asset — and the staging prune likewise
leaves a failed unit's own staged files in place. A stem that folds to an asset path another unit
already owns fails both units by name rather than letting either win, and protects the shared
path the same way.

Every staged file is written to a temporary sibling and renamed into place, and the sidecar
records `ddsSha256`; a unit counts as current only when the sidecar names the GLB hash, the
settings version **and** the hash the staged DDS still has, so a truncated product is rebuilt
rather than trusted.

### Role, sRGB and compression

A texture unit does not own its meaning; the material binding does (`seam_map_material.md`). The
lane derives one role per texture from the set of parameters that bind it across every material
unit:

| Bindings | Role | `srgb` |
|---|---|---|
| any colour parameter (`$basetexture`, `$basetexture2`, `$detail`, `$detail2`, `$iris`, `$selfillumtexture`, `$texture2`, `$envmap`, `$lightwarptexture`, `%tooltexture`) | `colour` | on |
| none at all | `colour` (`roleEvidence: []`) | on |
| only normal-class (`$bumpmap`, `$bumpmap2`, `$normalmap`, `$dudvmap`, `$dudvtexture`) | `data-normal` | off |
| only mask-class (`$envmapmask`, `$masktexture`, `$basealphaenvmapmask`, `$spotlightmask`, `$cloudalphatexture`, `$blendmodulatetexture`) or any other data-only set | `data-mask` | off |

A texture with both a colour binding and a data binding is a **conflict**: its asset is `colour`
with `roleConflict: true`, and the lane emits a `_linear` twin (`role` = the data role, `srgb`
off, `twinOf` = the colour asset). Source filtered the raw bytes; only an sRGB-off asset filters
the same way, so the twin is the exact reading and a material-side `LinearToSrgb` is not used.

`compression` preserves the source format rather than re-choosing by role:

| Source `vkFormat` | staged DDS | `compression` | Unreal setting |
|---|---|---|---|
| BC1, BC2, BC3 | decoded, `B8G8R8A8_UNORM` (DXGI 87), `stagedFormat: bgra8` | `default` | `TC_Default`, so Unreal re-encodes to BC1 or BC3 by alpha (BC2 has no Unreal output and builds as BC3; a BC1 punch-through alpha builds as BC3; both recorded in `builtFormats`) |
| BGRA8 | as is, DXGI 87 | `uncompressed` | `TC_EditorIcon` when `srgb`, else `TC_VectorDisplacementmap` |
| RGBA8, RGB8 | as is, DXGI 28 (RGB8 widened with an opaque alpha), `stagedFormat: rgba8` | `uncompressed` | `TC_EditorIcon` when `srgb`, else `TC_VectorDisplacementmap` |
| RGBA8_UINT (`UVWQ8888`) | as is, DXGI 28 | `uncompressed` | `TC_VectorDisplacementmap`, `srgb` off; staged as DXGI 28 (`R8G8B8A8_UNORM`, bytes untouched) because Unreal's DDS reader does not swizzle `R8G8B8A8_UINT` into its BGRA8 source and would swap U and W; the sidecar keeps `vkFormat` 41. Always a data role whatever binds it, never a twin |

`LossyCompressionAmount` is `TLCA_None`. Never `TC_Normalmap`: it rebuilds a channel the source
had.

### Mips, sampling and streaming

| Unit | Asset |
|---|---|
| every KTX2 level | imported as-is, `TMGS_LeaveExistingMips`; a single level is `TMGS_NoMipmaps` |
| a chain ending above 1×1 (65 units) | imported as-is; Unreal warns and what it builds for the tail is recorded on the first run |
| `sampling.clampS` / `clampT` | `AddressX` / `AddressY` = `TA_Clamp`, else `TA_Wrap` |
| `sampling.pointSample` | `Filter = TF_Nearest` |
| `sampling.trilinear` | `Filter = TF_Trilinear` |
| `sampling.noLod` / `allMips` | `NeverStream = true` |
| `sampling.noMip`, `anisotropic` | no per-asset slot; carried in provenance |

### Cubemaps and frame arrays

A `2d-array` unit (`dimensions.frames > 1`) stages as a DX10 DDS with `arraySize = frames` and
imports as `UTexture2DArray`; playback rate belongs to the material (`$animatedtexture`).

A `cubemap` unit stages as a DX10 cube DDS. Its payload faces are in glTF order and orientation;
the lane puts them back into **VTF/D3D order with their source orientation** by inverting the
unit's own `faces[]` rows (DDS face `s` is the KTX2 face whose row names `sourceFace == s`,
rotated by the inverse of that row's `transform`; block rotation reuses
`texture_glb.decode.transform_cubemap_face`). That is the layout Unreal's `UTextureCube` samples
through the plain D3D face table and the one the legacy bake already imports for the map-baked
env cubes (`tex_to_png.cubemap_dds`: VTF order, no rotation; the material owns the one
handedness correction). The runtime's `SkySlices` table is *not* involved: it assembles a sky from
six separate 2D `skybox/*` textures, which are 2D units in this corpus, not cubemap units. The
mapping is accepted by two headless checks, not by eye: **parity** — each staged face of an
`envmap/<name>` unit decodes equal to the same face of the legacy `<map>/tex/cube/envmap_<name>.dds`
the game draws today; and **seam continuity** — across the twelve cube edges the emitted
orientation scores lower than every other per-face rotation (`texture_cube.seam_error`). The
sidecar records the applied `faceMapping`. A rendered frame is a courtesy when a reader flips.

The corpus also carries a human-readable witness: `shadertest/cubedemo` (`TC_cubedemo`, 64×64,
seven levels) is a shipped Source shader-test cube whose faces are labelled `RT`, `LF`, `BK`,
`FT`, `UP`, `DN` with direction arrows. Decoding the staged DDS beside the legacy
`tex_to_png.decode_cubemap` output of the same `.tth`/`.ttz` (2026-08-31) put `RT`/`LF`/`BK`/
`FT`/`UP`/`DN` in D3D slots `+X`/`−X`/`+Y`/`−Y`/`+Z`/`−Z`, every label upright, max texel
difference 1/255 and best-fit rotation `k=0` on all six faces; Unreal's built face 0 (DXT1) shows
`RT` in the same orientation. The labels read mirrored on both paths because cube faces are
authored as seen from inside the cube; that is the handedness the material corrects, not a face
slip. A rerun is the `cubedemo` unit through `stage_textures` and `decode_cubemap` side by side.
The exporter's in-face block rotation is exact only for levels of at least 4×4; import applies the
exact inverse, so a 2×2 or 1×1 BC face round-trips byte-for-byte, but the payload's own
orientation at those levels is an open exporter defect (tracked in `seam_migration.md` open
questions).

### Provenance

Every asset carries one `UElysiumTextureProvenance` (`UAssetUserData`, runtime module, so a
packaged game reads it) attached through the texture's `asset_user_data` property:

| Field | From |
|---|---|
| `AssetId`, `TexturePath`, `UnitSchemaVersion`, `UnitSha256`, `PayloadSha256` | `identity`, the GLB file, `payload.sha256` |
| `SourceFormat`, `SourceFormatEnum`, `VkFormat`, `VtfVersion`, `TthVersion`, `Flags` | `sourceFormat` |
| `Reflectivity` (linear RGB), `BumpScale`, `StartFrame` | `sourceFormat` |
| `Width`, `Height`, `Frames`, `Faces`, `MipCount`, `SourceMipCount` | `dimensions`, `sourceFormat` |
| `Sampling` (the eight booleans) | `sampling` |
| `Members[]` (`Role`, `Path`, `OriginKind`, `Container`, `Offset`, `Size`, `ByteLength`, `Sha256`) | `sourceResolution.members` |
| `Role`, `RoleEvidence[]`, `RoleConflict`, `TwinOf` | the manifest |
| `FaceMapping[]` (cubemaps) | the computed glTF→Unreal permutation and rotations |

The sidecar is a JSON object whose keys are the field names above in camelCase (`assetId`,
`sourceFormat`, `reflectivity` as a three-number array, `sampling` as an object of the eight
booleans, `members` as rows of `role`, `path`, `originKind`, `container`, `offset`, `size`,
`byteLength`, `sha256`, `faceMapping` as rows of `unrealFace`, `ktxFace`, `sourceFace`,
`transform`). `UElysiumTextureProvenance::ApplyJson` tolerates a missing key, leaving that field
at its default, and refuses only a body that is not a JSON object.

`SourceFormat`, `Role`, `RoleConflict` and `AssetId` are also published as asset-registry tags
(`ElysiumSourceFormat`, `ElysiumRole`, `ElysiumRoleConflict`, `ElysiumAssetId`, listed under
`MetaDataTagsForAssetRegistry` in `Config/DefaultGame.ini` beside `ElysiumRecipe`), so the
Content Browser can filter on them without loading the asset. Ledgers, omissions and per-mip
digests are not copied: they are the export's proof and live in the unit.

### Idempotency, pruning and failure

The recipe stamp decides per asset; a current corpus launches, reports every asset reused, and
exits. Assets under `/ElysiumBaked/Textures` the manifest does not name are deleted, directories
emptied by pruning with them. A unit that fails to stage or import is counted and named with its
reason and the run continues; the command exits non-zero when any unit failed, and a relaunch
resumes from the stamps.
