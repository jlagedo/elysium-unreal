# VtMB material-resolution research case

This offline case recovers the order in which the engine turns a studio model's material name into
a `.vmt` on disk, and what it binds when no candidate resolves. It covers the studio-render
search-path walk that composes each candidate, the material-system lookup that opens the file, and
the placeholder material the miss path substitutes. It does not run or modify the game. Generated
Ghidra projects and evidence remain below `ELYSIUM_WORK_ROOT`.

## Questions

- Given a model's `$cdmaterials` search paths and a mesh material name, which candidate paths does
  the engine try, and in what order?
- Is there a lookup after the model's own search paths — a flat `materials/<name>.vmt`, a `models/`
  prefix, or any other global fallback?
- What does a total miss bind, and does the miss emit a message?
- Where along the walk is case folded or a separator normalized, and does any step depend on case?

The completed contract belongs in `docs/vtmb/texture_format.md`, which owns VMT resolution and the
error material. The header fields the walk reads (`NumTextureSearchPaths`,
`TextureSearchPathIndex`, `StudioTexture.name`) remain owned by `docs/vtmb/mdl_v2531.md`; the
`.vtx` per-LOD material replacement list the walk consults also belongs there.

## Eliminated leads

- `engine.dll` owns no model material resolution. Its `materials/`, `materials\` and `.vmt`
  literals belong to `CModelLoader`'s sprite and brush paths (`0x200b8e0e`, `0x200bb07e`,
  `0x200bb135`), and its `debug/debugempty` reference (`0x2007232e`) is a `mat_*` debug-mode
  material, not a miss substitute.
- `client.dll` and `vampire.dll` hold no material-system `FindMaterial` call near any structure
  read at `+0x12c`/`+0x130`, so neither re-resolves a model's textures.
- Inside `StudioRender.dll`, the `Model textures` texture-group literal belongs to the procedural
  glint texture (`0x2c0512b0`) and `$modelmaterial` to the decal path (`0x2c01697f`); neither
  composes a search-path candidate.

## Reproduction

Use a workstream-private copy of the hash-pinned project. The walk spans two binaries, so the case
carries one specification per program:

```powershell
uv run elysium research material-resolution research/cases/material-resolution/specs/studio_render_materials.json --binary "<VtMB>/bin/StudioRender.dll" --project-dir "<work>/research/ghidra/project_material_resolution" --kinds funcs,asm,xrefs,grep
uv run elysium research material-resolution research/cases/material-resolution/specs/material_system_findmaterial.json --binary "<VtMB>/bin/MaterialSystem.dll" --project-dir "<work>/research/ghidra/project_material_resolution" --kinds funcs,asm,xrefs,consts,grep
```

Use `--dry-run` to validate a specification or append `--address <address>` and narrow `--kinds`
for a focused rerun. Generated evidence is written below
`$ELYSIUM_WORK_ROOT/research/ghidra/out/material-resolution-studio/` and
`.../material-resolution-materialsystem/`.

## Open questions

- Whether a `null`-slot mesh that carries triangles sits in a body-group submodel the shipped
  bodygroup selection never draws. The static walk cannot answer it; it needs the body-part and
  submodel selection for each placed instance.
- Whether `g_pFileSystem` vftable `+0x1c` is `FileExists` under another name. Its semantics are
  proven by the branch it drives; only the symbol is unconfirmed.
