# VPK archives (original VtMB format)

`pack000.vpk … pack103.vpk` sit in the game's `Vampire\` folder — VtMB's own asset containers,
predating the modern multi-chunk VPK format. ~67,469 files are indexed across them. The reader is
`pipeline/src/elysium_pipeline/formats/vpk.py`.

## Container

No file header; file data is concatenated from offset 0. A flat directory sits near the tail.
Directory entry:

```
uint32 name_len          # exact length, NOT null-terminated
char   name[name_len]    # path, mixed case, '\' or '/'
uint32 file_offset       # within this same .vpk
uint32 file_size
```

Footer at EOF (~9 bytes): `[uint32 count][uint32 dir_offset][uint8 0]`; `dir_offset` is readable
at `filesize-5`. `pack010.vpk` (savegame templates) uses a different layout, and `pack100.vpk` /
`pack102.vpk` are empty 9-byte stubs — `vpk.index_all` skips unparseable packs. Each pack is
self-contained.

`pipeline/src/elysium_pipeline/formats/vpk.py`: `index_all(dir)` → `{lowercase_name: (path, offset, size)}`; `extract(entry)` →
bytes.

## Patch-first asset resolution

VtMB's own installed **Unofficial Patch** overlays loose files on top of the VPKs, and every
asset class — maps, models, materials, particles, the script layer — resolves through the same
patch-first order: `Unofficial_Patch\` before `Vampire\` and the VPKs. This is the game's own
file-resolution rule, not a pipeline convention; `pipeline/src/elysium_pipeline/formats/install.py` reproduces it so exported
content matches what the installed game loads.

`install.build_index(dirs)` merges the install into one table keyed by lowercase,
forward-slashed, install-relative paths, with loose entries shadowing VPK ones. `install.read(idx,
key)` fetches the bytes from wherever they live. `install.map_path(name)` resolves a map **name**
to the `.bsp` the engine would load (patch shadows retail); a caller-supplied *path* is honoured
as-is, so a batch caller must enumerate names rather than glob a tree —
`install.all_map_names()` returns the patch-first union of map stems (all 108, patch-only maps
included).

Reading the VPKs alone builds content the installed game does not run: over the converters' asset
trees (materials, models, maps, resource, particles, scripts, vdata) the merge is 69,708 files —
3,620 patch overrides shadow the VPKs and 2,239 exist only in the patch. The menu's own three
trees alone see 2,507 patch files shadow the VPKs, including `trackerscheme.res` (the scheme
behind every font and colour), 79 `vamp_mainfont` pages, `vtm_title` (1024×512, where retail's is
512×256 and stops at "THE MASQUERADE"), the blood-cel rates (40/30 → 20/15), and
`m_clans_emmiter` (wrapper restored, five more clans, plus `bloodlinestemp`/`vtm_glowtemp` — a
floating BLOODLINES wordmark and glow). Full UI-side detail: `docs/vtmb/vtmb-ui.md`.

Particles resolve by **walking the graph**, not globbing: the scene names emitters, emitters name
particles, particles name sprites. A glob over `m_*.txt` misses whatever an override adds under
another name.
