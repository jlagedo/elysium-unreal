# Textures (`.tth`/`.ttz`) and VMT materials

VtMB ships no loose `.vtf` — every texture is a pair under `materials/`: `<name>.tth` (header) +
`<name>.ttz` (compressed data). The reader is `pipeline/src/elysium_pipeline/formats/tex_to_png.py`; the inverse writer (RE probes
only, below) is `pipeline/src/elysium_pipeline/formats/tex_from_png.py`.

## `.tth` header

`"TTH\0"` sig, a mip offset/size table, then an **embedded standard VTF header** starting at the
`"VTF\0"` marker. Relative to that marker: `width` uint16 @16, `height` uint16 @18,
`reflectivity` float[3] @32, `highResFormat` uint32 @52, `mipCount` uint8 @56.

`reflectivity` is the **linear** average albedo `vtex` computed from the texture; VtMB's engine
multiplies every bounce ray by it when building a model's ambient cube (the consumer:
`docs/vtmb/sky-ambience.md` → "K3/K5"), so it is a decodable input, not dead header space.
`tex_to_png.reflectivity(tth)` reads it and the exporter writes it into the `.mtl` as
`reflectivity r g b`. Over `sp_tutorial_1`'s 412 materials it correlates with the decoded
texture's own mean **linear** albedo at 1.0000 (0.9597 against the gamma-encoded mean) — both the
decode's proof and the proof that the average is linear.

## `.ttz` payload

Zlib-compressed (`78 da`) raw image data = a DXT mip pyramid, ordered **smallest→largest** (the
full-res mip is LAST). Formats seen: DXT5 (enum 15, most common), DXT1 (13), DXT3 (14), BGR888
(3), BGRA8888 (12), RGBA8888 (0), and signed UVWQ8888 displacement data (23).

Decode: decompress `.ttz`, slice the last mip (size from the block formula), wrap DXT blocks in a
minimal DDS header, let PIL decode → RGBA.

## Writing a `.tth` (RE probes only)

Full layout, as needed to **write** one (`pipeline/src/elysium_pipeline/formats/tex_from_png.py`): `"TTH\0"`, `uint16 version`
(1), `uint8 mip_count`, `uint8 inline_mips`, `uint32 vtf_blob_len` (meaningful bytes from the
`"VTF\0"` marker; some products retain arbitrary compiler allocation-fill beyond it, often
`0xcd`), then
`mip_count + 1` pairs of `uint32 raw_offset, uint32 ttz_prefix`, then the
embedded VTF 7.0/7.1 header (64 B), a low-res DXT1 16×16 thumbnail (128 B), and the `inline_mips`
**smallest** mips. A mip's `raw_offset` is its position in the reconstructed
`[header][thumbnail][mips]` image counted from the `"VTF\0"` marker, so the first mip sits at
192; `ttz_prefix` is how many compressed bytes precede it, which works because the `.ttz` is one
zlib stream with a `Z_SYNC_FLUSH` between mips (`decompressobj().decompress(ttz[:prefix])` yields
exactly the mips before it). Entry `mip_count` holds the two totals — end offset and `.ttz`
length. Inline mips carry prefix 0. Retail textures keep their three smallest mips inline; the
patch's uncompressed re-exports keep none and leave the per-mip columns unfilled, so only the
totals row is load-bearing. `encode_like(template_tth, img)` clones a shipped texture's format,
flags and mip policy — BGR888 round-trips bit-exact, DXT5 within a re-encode.

Retail compiler bookkeeping is not uniformly canonical: the outer mip count may exceed the
dimension-derived chain, individual inline counts and VTF mip counts may be stale, and declared
meaningful TTH/TTZ lengths may be followed by arbitrary allocation-fill bytes. A small set of
textures contains only a complete high-resolution prefix of the logical mip pyramid plus partial
or disconnected lower-level storage. The outer table still provides the source ranges and totals;
decoders validate available complete levels rather than inventing missing texels.

The declared low-resolution image (normally DXT1 16×16) is separate from the rendered mip chain.
Some retail declarations are non-canonical or internally inconsistent. Source exposes the image through
`ITexture::GetLowResColorSample`/`IMaterial::GetLowResColorSample` so CPU code can make an
approximate colour query without reading the primary image. It is a generated engine optimization,
not independent authored artwork; the primary texture and the header's independently consumed
`reflectivity` remain the authoritative image and average-albedo inputs.

This writer exists only for RE probes that need the *original game* to draw an authored image
(e.g. the sky-orientation probes in `docs/vtmb/sky-ambience.md`); it produces nothing the runtime consumes.

## Cubemaps

VtMB cubemaps are **VTF 7.1 = 7 faces** (six axes + a legacy low-end spheremap fallback): DXT cubes ship
the large mips zlib'd in `.ttz` (small mips in `.tth`); recompiled maps store uncompressed
**BGR888 inline in the `.tth`, no `.ttz`**. `tex_to_png.decode_cubemap` handles both, slicing the
full-res mip's first six faces and deliberately drops the fallback face. The naming convention that ties a cubemap to a BSP face is
`docs/vtmb/bsp_format.md` → "Cubemaps".

Of the 1,325 baked reflection probes embedded in the 108 map BSPs' PAKFILE zips, 575 carry no
`.ttz` because the whole 7-mip pyramid fits the `.tth`'s inline range (declared `.ttz` length 0);
552 of those are `BGR888`, 23 are small `DXT5` `cubemapdefault`. The split is the ordinary
inline/external size threshold every VtMB texture uses, not a clean format rule.

## VMT materials (`pipeline/src/elysium_pipeline/formats/vmt.py`)

KeyValues text. `parse(text, resolve_include)` returns `basetexture` (normalized, `\`→`/`,
lowercased), `selfillum`, `translucent`, `alphatest`. Shader `"patch"` follows one `include`.
`$selfillum "1"` means the base texture's **alpha channel is the emission mask** — emission =
`RGB × (alpha/255)`, only masked pixels glow.

Material resolution: material name → `materials/<name>.vmt` → `$basetexture` →
`materials/<basetexture>.tth`/`.ttz`.

317 of the 1,078 install materials under the UI trees name a different texture in their own VMT
than their material path (e.g. `hud/disciplines/bloodheal_hud` -> `bloodheal_base`).

`Refract` is a separate framebuffer-distortion shader and does not require `$basetexture`.
Its `$dudvmap` is a signed vector field: UVWQ8888 stores U/V/W as two's-complement bytes centred
on zero, while `$refractamount` scales the framebuffer offset. The `sp_theatre` pawnshop rain
layer is six instances of `models/scenery/structural/santamonica/rain_window.mdl`; its sole
material declares `$dudvmap .../rain_refract_dudv` and `$refractamount .01`. The DUDV texture is
256×512 UVWQ8888 with W=127 and Q=255 throughout. It is distortion input, never pane colour.
When a `Refract` VMT carries both `$normalmap` and `$dudvmap`, the normal map is the tangent-space
path and the DUDV is the older signed-displacement path.

**What a VMT's shader does is shipped as data, not compiled into a binary.**
`materials/dxshaders/*.psh` are readable **ps.1.1 assembly source** with Valve's own comments
intact, and `shaders/vsh/*.vcs` / `shaders/psh/*.vcs` are the compiled combos (a small offset
header, then one DX8 bytecode program per static combo, each ending `ff ff 00 00`). Both resolve
through `install.build_index` like any other asset, so "what does this material do to colour" is
a file read, not a decompile. The shader-level findings (`unlitgeneric.psh`,
`lightmappedgeneric.psh`, the sky-vs-world brightness relationship) are `docs/vtmb/color_gamma.md`.
