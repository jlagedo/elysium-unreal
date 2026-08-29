# Local project content (`Content/`)

Nothing game-sourced or derived from the user's VtMB install is tracked. `Content/` has two
deliberately separate package classes:

- **Original project-authored packages** live only below `Content/ElysiumAuthored/`, mounted as
  `/Game/ElysiumAuthored/**`. They are committed through Git LFS and may contain original camera
  profiles, curves, shakes, Blueprint helpers, Level Sequences, VFX and tuning data assets. They
  must not copy or encode game-derived art, transforms, timings, scripts, map data, or converted
  assets. Current residents: `VFX/` (the melee-trail and rain Niagara systems, materials and
  textures), `Hair/DA_HairDynamics` (owner-authored AnimDynamics chains), and
  `Cloth/DA_ClothTuning` (Chaos cloth material/solver tuning). These are edited live in the
  editor and saved in place — they have no generator.
- **Generated local packages** are products of the pipeline and remain ignored/regenerable.

The tracked files under `Content/Fonts/` and `Content/InputPrompts/` are licensed, loose source
assets and their licence texts.

`uv run elysium export bundle policy` generates the local project packages:

- `/Game/ElysiumGenerated/Boot` from `Content/ElysiumGenerated/Boot.umap`;
- `/Game/ElysiumGenerated/Materials/**`;
- `/Game/ElysiumGenerated/Audio/**`;
- `/Game/ElysiumGenerated/UI/Fonts/**`;
- `/Game/ElysiumGenerated/Input/**`.

Their physical `.uasset` and `.umap` files are ignored. Edit the corresponding generator under
`pipeline/unreal/`, then regenerate; never hand-author or commit a replacement for a generated
package.

`uv run elysium export map <map>` writes the user's game-derived map packages below
`Plugins/ElysiumBaked/Content/`, mounted virtually as `/ElysiumBaked/<map>/**`.
That directory is also ignored.

The generated virtual path roots are `/Game/ElysiumGenerated/**` and `/ElysiumBaked/**`, each
defined once in `pipeline/src/elysium_pipeline/mounts.py`, the single source of truth every
generator and bake resolves its packages against. Renaming a root is a deliberate change guarded by
`pipeline/tests/test_profile_package_contract.py::test_virtual_package_roots_remain_immutable`, not
a forbidden edit. The original authored namespace is `/Game/ElysiumAuthored/**`; generated tools
and clean operations must never write to or remove it.

## Fonts

The source faces in `Content/Fonts/` are game-independent and distributable under
their included SIL OFL licences. `pipeline/unreal/make_ui_fonts.py` imports the UI
faces as local `UFontFace` packages during the Slate-enabled content pass. Runtime
sign and popup code also reads the licensed loose faces through
`FElysiumContentPaths::FontFile`.

## Input prompts

The selected PNG sources in `Content/InputPrompts/Kenney/` are redistributable under their
included Creative Commons Zero licence. `pipeline/unreal/make_input_glyphs.py` imports them as
local UI textures; native CommonInput controller-data classes map their generated paths to
keyboard, Xbox and DualSense keys. Keep the upstream licence and provenance file beside any
future additions.
