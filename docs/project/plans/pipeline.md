# Pipeline plan — open-task specifications

Specifications for **open** PL backlog rows. Status lives solely in `docs/project/roadmap.md`;
a row that lands is deleted here. No status marks in this file. Pipeline boundaries and
formats: `pipeline/CLAUDE.md`.

### PL6 Texlight merge in exporter

The exporter-side half of 3.4: bin type-0 patches by `(intensity, normal)` and merge
deterministically in `UE_bsp_to_scene.py`.

### PL11 Remove the dead Lumen-card path

`export_all.py`'s `bake_cards`/`--no-cards` calls a `cards.bat` that no longer exists and
prints a "skipped" line every run; `ElysiumCardGen.cpp` (`ELYSIUM_WITH_CARDGEN`,
`elysium.cards.probe`) still builds into editor targets. Nothing depends on either.

### PL12 Particle mirror + weather height maps

The merged install's `particles/*.txt` (1,698) + `particles/*.tga` (318) mirror verbatim and
patch-first into `$ELYSIUM_EXPORT_ROOT/particles/`, wired into export orchestration; the
`sm_hub_1` closure is compiled for 7.9. Per-map 2048² R16 top-down height maps use corrected
exported geometry bounds; `sm_hub_1` is accepted, but grid-wide height-map
generation/acceptance has not run. Format: `docs/vtmb/weather.md`. Needed by 7.9.

### PL14 Export the complete first-person model corpus

Export and bake one deterministic, patch-first manifest for both roles of the first-person body:

- exactly **21** models under `models/hands/**`, including active and repeated
  `M_Hands`/`F_Hands` clandoc values and the script-only male/female Tremere `_shield` swaps;
- exactly **17** packed weapon viewmodels: the 12 accepted firearm geometries plus the grenade,
  lockpick-reference and three Discipline models;
- each row's source key, normalized stem, role (`hands`, `hands_shield`, `firearm_geometry`,
  `grenade_geometry`, `lockpick_geometry`, or `discipline_geometry`), skeleton signature,
  attachments, sequences and events, with item `viewmodel`, `anim_prefix`, `camera_class` and
  `reload_single` joins retained as provenance.

The package layout is a contract, not an incidental editor import path:
`/ElysiumBaked/Characters/Viewmodels/Hands/<stem>/SK_<stem>` and
`/ElysiumBaked/Characters/Viewmodels/Weapons/<stem>/SK_<stem>`, with each model's animations under
its own package folder and one compatible skeleton per identical hierarchy. Manifest and asset
iteration are case-folded and sorted by normalized source key so the same corpus produces the same
package names and bake order on every run.

The character exporter owns cache fingerprints over the source MDL/VVD/VTX bytes, decoded
container version, role and bake contract. A changed or removed manifest row invalidates its own
products; the bake performs a pre-save stale sweep below the two viewmodel roots so renamed or
deleted source rows cannot survive as loadable packages. Verification resolves every manifest
package through the asset registry, checks mesh/skeleton/sequence counts and events against the
container, rejects duplicate package paths, and asserts the 21/2/17/12 census.

Missing authored references remain diagnostics rather than aliases. In particular clandoc's
`models/hands/female/gangrel/v_gangrel_fem_hands.mdl` is reported as dangling beside the present
`v_gangrel_female_hands.mdl`; PL14 does not silently repair the spelling. The outputs remain
game-derived and gitignored. Needed by CCC10.1.

### PL20 Export the wield-model corpus

The third-person sibling of PL14. Export and bake one deterministic, patch-first manifest of every
model an item's `wieldmodel_m` / `wieldmodel_f` names, keyed so the runtime resolves a row from
(classname, sex) with no path arithmetic of its own.

Each row carries: the item classname, the source key and normalized stem per sex, the item's
`anim_prefix`, the model's bone table, and — the load-bearing field — the **terminal prop bone** the
mesh is skinned to, read from the model rather than assumed from a table. Rows whose skin weight is
not wholly on one terminal bone are reported, not silently accepted: the composition contract in
`docs/vtmb/animation_and_movers.md` → "The wielded weapon is the same two-rig composition" depends
on it, and a multi-bone wield model would be a different mechanism.

`w_null.mdl` is a real authored value, not a missing model — a row naming it records the null and
stays in the manifest, because it is how an item says it has no wielded geometry. A model the
install lacks is skipped and recorded, never fatal, the same policy `UE_extract_items.py` uses.

**The bake shape is CCC10.2's open design call**, so hold this row's output format until that call
is made: a rigid single-bone mesh may be baked as a static mesh in prop-bone-local space, or as a
skeletal asset preserving the 11-bone chain. The manifest itself is the same either way.

Also covers the item-data keys the current exporters ignore — `wieldmodel_m`, `wieldmodel_f`,
`anim_prefix` — which `docs/vtmb/inventory.md` §4 now names. Outputs remain game-derived and
gitignored. Needed by CCC10.2.

### PL17 Patch-first audio catalog + typed sidecars

Codec/channel/rate/frame/duration metadata, complete static reference closure, parsed map +
entity sound schemes, sentences/surfaces, item/discipline events, radio/news, case collisions
and missing refs. Raw game audio remains gitignored under `$ELYSIUM_EXPORT_ROOT/sound/`.
→ `docs/vtmb/audio_pipeline.md`, `docs/architecture/audio-architecture.md`. Needed by 6.5–6.8,
9.2, 12.2.
