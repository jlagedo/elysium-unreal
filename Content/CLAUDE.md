# Local project content (`Content/`)

Nothing game-sourced or derived from the user's VtMB install is tracked. `Content/` has two
deliberately separate package classes:

- **Original project-authored packages** live only below `Content/ElysiumAuthored/`, mounted as
  `/Game/ElysiumAuthored/**`. They are committed through Git LFS and may contain original camera
  profiles, curves, shakes, Blueprint helpers, and Level Sequences. They must not copy or encode
  game-derived art, transforms, timings, scripts, map data, or converted assets.
- **Generated local packages** are products of the pipeline and remain ignored/regenerable.

The tracked files under `Content/Fonts/` are licensed, loose source fonts and their licence texts.

`uv run elysium export bundle policy` generates the local project packages:

- `/Game/Elysium` from `Content/Elysium.umap`;
- `/Game/VtMB/Materials/**`;
- `/Game/VtMB/Audio/**`;
- `/Game/VtMB/UI/Fonts/**`.

Their physical `.uasset` and `.umap` files are ignored. Edit the corresponding generator under
`pipeline/unreal/`, then regenerate; never hand-author or commit a replacement for a generated
package.

`uv run elysium export map <map>` writes the user's game-derived map packages below
`Plugins/ElysiumBaked/Content/`, mounted virtually as `/ElysiumBaked/<map>/**`.
That directory is also ignored.

The immutable generated virtual path contracts are `/Game/Elysium`, `/Game/VtMB/**`, and
`/ElysiumBaked/**`. The original authored namespace is `/Game/ElysiumAuthored/**`; generated tools
and clean operations must never write to or remove it.

## Fonts

The source faces in `Content/Fonts/` are game-independent and distributable under
their included SIL OFL licences. `pipeline/unreal/make_ui_fonts.py` imports the UI
faces as local `UFontFace` packages during the Slate-enabled content pass. Runtime
sign and popup code also reads the licensed loose faces through
`FElysiumContentPaths::FontFile`.
