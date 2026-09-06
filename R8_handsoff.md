# R8 handoff — closure pass, 2026-09-06

Status lives in `docs/project/roadmap.md`; the validation record lives in
`docs/project/seam_migration.md` → R8 → "Closure record (2026-09-06)". This file is the
operator's summary and the owner's play checklist, nothing more.

## What is on the mount now

Every asset the three closure maps read comes from the V2 export and the native import lanes:
`Models/`, `Materials/`, `Textures/` and the map's own bundle. Checked by reading the package
name tables of `sp_tutorial_1`, `sm_pawnshop_1` and `sm_hub_1`: zero references to `Meshes/`,
`Props/`, `Characters/`, `Items/`, `Shared/`, `Sky/` or `Sprites/`. Those folders still exist for
the 106 unconverted maps and empty in R9.

Publication chain, all green in this order (receipts under `E:\elysium-work\import\*`,
logs under `E:\elysium-work\_r8_explore\close_*.txt`):

```
export bundle policy
import textures --select skybox --no-measure
import materials
import characters
import model-catalogues
import cook-roots
export map sp_tutorial_1 sm_pawnshop_1 sm_hub_1
verify characters / verify expression-tables / verify model-catalogues
test content / test substrate
```

Tiers: substrate 483 / 483; content 92 / 95, the three left being the deferred oracle readings
(`BakedCharacterParity` 0.051° on one bone/frame, `RigCompose` medians vs the pinned bands,
`RigOracle` one weighted-draw divergence) recorded in the fidelity ledger.

## Owner play checklist (the one step not run here)

```powershell
uv run elysium run play sp_tutorial_1
uv run elysium run play sm_pawnshop_1
uv run elysium run play sm_hub_1
```

Look at, in this order:

1. Cast: bodies visible, idle/walk playing, no default-material (grey/checker) skins, eyes
   present. Talk to one NPC per map (dialogue + facial).
2. Wield: a held weapon on an NPC and on the player; the melee trail on a swing.
3. Placed props: sp_tutorial_1 has 31 skeletal rest placements, sm_hub_1 has 21, sm_pawnshop_1
   none. They must not float, sink or stand in bind pose.
4. Sky: the dome binds the native composite (no black or engine-default cube).
5. Ground items: drop and pick up one item; its ground model must appear.
6. Log after each map: `Failed to compile Material Instance` count must be 0 in
   `Saved/Logs/ElysiumUE.log`.

Anything wrong is a functional defect for R8, not fidelity, unless it is a precision or
appearance delta already listed in the fidelity ledger.

## Play pass findings so far

- Black characters (bodies lit, no albedo): CPD slot 6, the lightstyle brightness the lit
  masters multiply by, was never stamped on runtime-built skeletal components. Fixed at every
  construction site (bodies, wields, garments, preview, character stage, placed rest visual);
  tests `Elysium.Substrate.LightSwitch` and `Elysium.Content.NativeCloth` cover it. No re-bake
  needed: the three maps place no cloth scenery, and the bake already stamped its own actors.
- Spectral wolves standing in the tutorial alley before their scene, as grey clay: the wolf's
  whole hide is cloth, and the prop gate never hid a placed model's garments (fixed in
  `FElysiumProp::GateVisual` and the generic placed-body gates); its `UnlitGeneric` materials
  bind `M_V2_Unlit`, which lacked the Clothing usage flag, so the game drew the default grey
  material (fixed in `make_v2_materials.make_unlit`). Needs `export bundle policy` then
  `import materials` to re-author the master and re-parent its instances; no map re-bake.
  `Elysium.Content.NativeCloth` now dresses the wolf and checks both.

## Recipe rule (new this pass)

`ElysiumRecipe` = data content + one hand-bumped version string per producer; code is never
hashed. Changed a builder, a stage rule or a generator's graph? Bump its `PRODUCER_VERSION` /
`SETTINGS_VERSION` / `RULES_VERSION` / `GRAPH_VERSION` in the same commit, or run that lane with
`--force`. `--force` on `export map` forces the bake only. Each lane writes `recipes.json` beside
its receipts and logs which recipe fields moved when a product re-authors.
`pipeline/tests/test_recipe_policy.py` enforces it.

## Cost notes

- A full character re-author is ~20 minutes (19,190 products); a full restage is ~45 minutes.
  Neither happens any more for an unrelated code change.
- The catalogue stage is ~8 minutes; the three-map bake ~5 minutes once the imports are current.

## Not done / deferred (recorded in seam_migration.md)

- Rendered play acceptance (above).
- A real cook: the cook root is audited offline only.
- The fidelity ledger, the 11 cloth tuning fallbacks, the 16 mesh-less owners' tables.
- Legacy mount folders and the loose export lane: R9.
