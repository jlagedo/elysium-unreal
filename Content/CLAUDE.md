# Local project content (`Content/`)

Nothing game-sourced or Unreal-packaged is tracked. The tracked files under
`Content/Fonts/` are licensed, loose source fonts and their licence texts.

`uv run elysium export bundle policy` generates the local project packages:

- `/Game/Elysium` from `Content/Elysium.umap`;
- `/Game/VtMB/Materials/**`;
- `/Game/VtMB/Audio/**`;
- `/Game/VtMB/UI/Fonts/**`.

Their physical `.uasset` and `.umap` files are ignored. Edit the corresponding
generator under `pipeline/unreal/`, then regenerate; never hand-author a package and
attempt to add it to Git.

`uv run elysium export map <map>` writes the user's game-derived map packages below
`Plugins/ElysiumBaked/Content/`, mounted virtually as `/ElysiumBaked/<map>/**`.
That directory is also ignored.

The immutable virtual path contracts are `/Game/Elysium`, `/Game/VtMB/**`, and
`/ElysiumBaked/**`.

## Fonts

The source faces in `Content/Fonts/` are game-independent and distributable under
their included SIL OFL licences. `pipeline/unreal/make_ui_fonts.py` imports the UI
faces as local `UFontFace` packages during the Slate-enabled content pass. Runtime
sign and popup code also reads the licensed loose faces through
`FElysiumContentPaths::FontFile`.
