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

Two documents are the contract, and neither is restated here. `docs/vtmb/wielded_weapons.md` owns the
VtMB behaviour and supplies every count this task must reproduce — 244 definitions, 192 naming a
wield model, 384 rows, 296 nulls, 8 absent, 67 real models.
`docs/architecture/wielded-weapon-integration.md` owns the uniform bake lane, the package layout,
the runtime-binding metadata and the material contract.

**The export and bake are uniform**: every real model emits one `.eskm` whose **reference pose is
the model's own clip at frame 0** (`wield_corpus.bake_pose`; assert the emitted pose equals it),
carrying its own clips; the corpus's albedo set (89 texture keys, patch-first) decodes into the
wield tree, since neither the character texture table nor the prop bake supplies weapon textures.
Binding is manifest metadata the bake never branches on. A wield model **never enters the character
partition** — each gets a private skeleton, the animated-prop shape.

Each row carries: the item classname, the source key and normalized stem per sex, the item's
`anim_prefix`, `shows_view_model`, `camera_class` and the `BitFlag_*`/`reload_single` bits, and —
per model — the **mount bone** and the **hand bone** it descends from, both read from the model.
The mount bone is the unique *skinned* root under a `Bip01 <L|R> Hand`, not the terminal leaf and
not the hand's only child: several models park unskinned authoring leftovers beside the real mount,
and two hang them from a second root outside the Biped chain. Carry the mount bone's parent-relative
bind, the classification (`socket_hand` 37 / `socket_prop` 20 / `leader_pose` 2 / `copy_pose` 6 /
`projectile` 2 — assert the census, do not hard-code the assignment; mode is per model, not per
item), per-bone motion **with each bone's skinned flag** (an unskinned mover moves no vertices),
and per-material render semantics: the resolved albedo key, its VMT flags (envmap, alphatest,
translucent, additive, selfillum), and any extra skin family as slot overrides — `w_{m,f}_fire_axe`
family 1 (`Transparent`) is the shipped case, and the handleclaws `null` material's empty texture
payload is a recorded retail defect, not a pipeline failure.

A multi-bone sub-rig is ordinary, not an anomaly — firearms carry 2–13 bones below the mount and
nothing animates them. The invariant worth asserting is one skinned root under one hand, whose only
violations are `w_{m,f}_claws`, `w_{m,f}_handleclaws`, `changball` and `gio_spirit`; a seventh
violation is a decode regression. `w_{m,f}_handleclaws`' transitive `$include` tree is deliberately
not chased — its own 30 container clips are the completeness boundary.

Carry an **`on_body` scope, not a boolean**: the mount bone's name matched against the character
corpus, naming which skeletons declare it *and under which parent*. Only the seven canonical prop
bones sit under a hand. `Box01` and `Box02` match four NPC bodies but hang from `Bip01 Pelvis` and
`Bip01 R Finger1`, so a boolean would route four item families to a hip socket.

`w_null.mdl` is a real authored value, not a missing model — a row naming it records the null and
stays in the manifest, because it is how an item says it has no wielded geometry, and because equip
applies it unconditionally like any other path. Distinguish it from an **empty** string, which is a
different authored value. A model the install lacks is skipped and recorded, never fatal, the same
policy `UE_extract_items.py` uses.

Reduce repeated keys **last-wins** before reading a scalar: `kv.parse` collapses a repeated key into
a list where the runtime's `TMap` overwrites, and `anim_prefix` is among the keys that repeat.

Label each row's **NPC carriage**, sourced from the authored `additionalequipment` /
`alternateequipment` keyfields (excluding the literal `"0"`, Hammer's unset-field sentinel). Player
obtainability rests on secondary walkthrough evidence owned by the doc and is not emitted — the
exporter derives, it does not transcribe. No row is dropped for being unreachable; a cut definition
still round-trips.

Carry every bind through as authored. `w_f_bushhook.mdl`'s degenerate identity bind is reproduced,
not corrected; record it explicitly on that row rather than letting it read as ordinary data.

Bake to `/ElysiumBaked/Items/Wield/<stem>/SK_<stem>` — private skeleton per model, textures imported
from the wield tree and bound per slot from the manifest's material rows — and build
`/ElysiumBaked/Items/DA_WieldModels` from the exported JSON manifest so a missing or renamed package
fails at bake rather than at load. Sweep stale packages below the wield root before saving.

Also covers the item-data keys the current exporters ignore — `wieldmodel_m`, `wieldmodel_f`,
`anim_prefix` — which `docs/vtmb/inventory.md` §4 now names. Outputs remain game-derived and
gitignored. Needed by CCC10.2.

### PL17 Patch-first audio catalog + typed sidecars

Codec/channel/rate/frame/duration metadata, complete static reference closure, parsed map +
entity sound schemes, sentences/surfaces, item/discipline events, radio/news, case collisions
and missing refs. Raw game audio remains gitignored under `$ELYSIUM_EXPORT_ROOT/sound/`.
→ `docs/vtmb/audio_pipeline.md`, `docs/architecture/audio-architecture.md`. Needed by 6.5–6.8,
9.2, 12.2.
