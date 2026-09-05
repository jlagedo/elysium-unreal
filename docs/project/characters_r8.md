# R8 — characters: the skeletal lane rebuilt on the GLB corpus

> Working note, owned by `seam_migration.md` → "R8". It carries no task status —
> `docs/project/roadmap.md` owns that — and it is not a seam contract: every ruling below is
> written into its owning seam doc as the first deliverable of the task that needs it. It exists to
> close the design of R8 before code, on measurements rather than the R6-era description of the
> lane. The measurement reports it rests on (13 read-only exploration passes, 2026-09-04, against
> `main` @ `baedd066`) are under `$ELYSIUM_WORK_ROOT/_r8_explore/` — `A_legacy_exporter.md`,
> `B_legacy_bake.md`, `C_runtime_readers.md`, `D_v2_unit_audit.md`, `E_materials_eyes.md`,
> `F_wield_items.md`, `G_animated_props.md`, `H_cloth_hair_procedural.md`, `I_facial.md`,
> `J_animation_contract.md`, `K_tests_tooling.md`, `L_player_cast.md`, `N_known_issues.md` — with
> the scripts that produced every number.

## 0. Verdict

**Physics scope, owner ruling 2026-09-05:** R8 includes physics export/import and data
conservation only. Preserve and verify geometry, solid/ledge ownership, constraints, all
parameters, metadata, source gaps and provenance in the GLB and cooked source-data projection.
Simulation-ready `UPhysicsAsset` construction, solver calibration, ragdoll activation and gameplay
physics are deferred to PHYS1 and later physics work. They are not R8 completion or legacy-retirement
prerequisites. This ruling supersedes earlier PHYS1 dependencies in linked plans.

**R8 ports the rules and rebuilds the bake on a complete GLB corpus.** Producer corrections
are part of the migration whenever comparison exposes a dropped datum; byte-ledger coverage
alone does not prove that every decoded record has a published destination. Model schema 2.1
therefore retains ordered sparse morph records, including explicit zeros and entire unrendered
meshes (`seam_map_model.md` → "Core content" / "Import — skeletal staging"). The V2 model unit
carries every datum the legacy skeletal exporter reads out of an MDL, verified on real files: the
per-bone ownership masks (`boneWeights`, strictly {0,1} over 600,778 pairs corpus-wide), the raw
sequences with grids, autolayers, events with their full option strings, movement records, the
split-rotation declaration, the skin table the `.eskm` never had, facial rules, eyeballs,
procedural tables, cloth, secondary motion, and the full VPhysics rig. The hardest derived
product — the `<layer>@<host>` fan-out — was reproduced from the GLB alone with zero name
differences on the largest bank (1,505 of 1,505). What the unit does not carry is every
*derivation* the legacy exporter performs (~37 rules, ~2,000 lines: basis change, single-rooting,
split-rotation correction, host composition, bind-fill, cell naming, unit conversions, the
bank-family partition, the three-seed cast join) and every *decision* the bake takes.

So the shape is the R3 shape, then the R1 shape: **R8.1** ports the rules into a V2 stage that
re-emits the legacy product set byte-equal or named-divergence-only (proved by a differ that does
not exist yet and is the first thing to build); **R8.2** turns the stage into an `import
characters` lane on the props-lane template, binds the existing 2,027 V2 `MI_` instead of the
2,526 legacy per-slot instances, and lands the three master rulings; **R8.3** and **R8.4** run
weapons and animated props through the same build; **R8.5** cooks the nine loose `npc/` reads
onto assets. The C++ skeletal builders survive (they are the only door to a `USkeletalMesh`).
Every asset lands on **one baked-asset standard** — the mount mirrors `exports_v2` — which the
unit contract now owns and which the landed lanes that drift from it (models, maps, sprites,
sky) adopt first, so no rename is left for a later project; stems survive only as a lookup
through a cast table, so the retail captures keep their keys. The parity numbers are pinned as
equalities, because a producer change must not move them in either direction.

Twelve defects were found on the way, six of them live. They are in §3 and each has a task.

## 1. What was measured

| Quantity | Value | Report |
|---|---|---|
| Model units on disk | 4,445; `models/character` **485** (not 489), `models/weapons` 206 | D |
| Character + weapons GLB bytes | 3.49 GiB of 5.50 GiB (64 % of the corpus in 691 files) | D |
| `identity.shape == "bank"` | **0 units** — every bank MDL compiles a placeholder body part | D |
| `roles: animation-bank` | 0; `include-only` 81 (10 pure aggregators) | D |
| Include edges / distinct targets | 443 / 81, transitively closed | D, L |
| Body → bank remap | by bone name; `boneRemap` is `-1` everywhere (a runtime scratch table) | D |
| Masks | `boneWeights` ∈ {0, 1}; 0 zero-weight bones carry data; channel set == owned set | D |
| `@host` derivation from the GLB | 1,505 / 1,505 names on `move_and_ranged`, 0 missing, 0 extra | D |
| Legacy products | 293 body + 372 bank + 2,899 prop `.eskm` (3.6 GB); 9 sidecar families; `npc_manifest.json` 60.7 MB | A |
| Legacy bake output | `Characters/` **13,816 assets, 3.9 GB, 39 % of the mount**; `Props/` 9,005; `Items/` 335 | B |
| Bank clips | 7,327 `A_` under 372 bank folders; 505 of 533 blend spaces on two banks | B |
| Skeletons | 293 body singletons + **10** bank families (267 of 372 banks in one) | A, B |
| Material units bound by characters + weapons | 1,540; `vertexlitgeneric` 1,147 · `eyes` 374 · `unlitgeneric` 12 · `teeth` 7 | E |
| `MI_` closure | **1,540 / 1,540** on disk; blend Opaque 1,399 / Translucent 110 / Masked 29 / Additive 2 | E |
| Texture closure | **1,231 / 1,231** as `T_`/`TC_`/`TA_`, every iris included | E, C |
| `$halflambert` in the corpus | 0 of 11,624 material units | E |
| Runtime readers under `npc/` | **9** (confirmed), 3 of them debug-only; `items/ground_models.json` a tenth, a gate only | C |
| Facial | 199 V2 bodies with rigs (177 legacy); 191 of 199 span >1 material; rules are non-linear (1,548 `DIV`) | I |
| Cloth / hair / procedural / PHY | 53 / 102 / 258 / 363 character units; 321 carry ragdoll constraints (289 the 15-solid biped) | H |
| Weapons | 67 real wield models (57 rigid, 10 skeletal-bound); 19 viewmodels + 21 hands unexported | F |
| Placed skeletal models | 2,899 catalogued; 345 non-static-equivalent; 240 skeletal `placed-prop` roles | G, D |
| Player bodies | 59 `pc/**` units (56 in `clandoc000.txt` + 3 disguises); 27 carry no role | L |
| Bodies placed by the three test maps | 17 / 6 / 22 distinct (`sp_tutorial_1` / `sm_pawnshop_1` / `sm_hub_1`); no player body, no LaCroix | L |

## 2. Corrections to the R8 text and the owning docs

These are facts the exploration overturned; each is applied in R8.0b (§6).

1. **`models/character` is 485 units, not 489.** File count and `identity.family` agree.
2. **The "Nosferatu/Malkavian obfuscate noise chains" are not character materials.** The
   `$a_*`/`$j_*`/`$xo_*` `gaussiannoise` → `lessorequal` chain exists on exactly four units:
   `dev/dev_tvmonitor1a` and the three `models/scenery/furniture/tvs/danetv8inch0*screen` —
   a CRT static-and-roll effect on `M_V2_TwoTexture`, already in the world lane. No character or
   weapon material carries a noise proxy. The real obfuscate materials are
   `highlights/obfuscate_character` (`refract`), `effects/obfuscate_overlay` and the discipline
   info model, and `ElysiumDisciplines.cpp` records Obfuscate as unbuilt. R8.5 drops the line;
   `seam_map_material.md` → "Runtime bind" is corrected to name the TV units.
3. **`mouthshader` is not a shader.** It is the value of `$clientshader` on two materials (the
   female Malkavian PC's teeth); `StudioRender.dll` compares the string and sets a per-slot
   boolean with no parameters. Nothing in the runtime implements it, and the PC body carries no
   flex rig to drive it. R8.5 binds the two instances through `Create(MI_)` and records the
   boolean as a named divergence (no consumer), not a feature.
4. **`identity.shape: "bank"` and `roles: "animation-bank"` are dead vocabulary** and are
   retired from `seam_map_model.md`; a bank is `skeletal` by shape and `include-only` by role.
5. **`coordinateTransform.domains` describes the core glTF, not the extension.** Eyeball
   `up`/`forward`, procedural `pos`/`quat`, hitbox and sequence bounds, movement, swings and
   envelopes are all raw source values; `procedural` and `secondaryMotion` have no row at all. A
   consumer trusting `domains.eyes` gets a mirrored eye basis. The doc gains the rule "core is
   converted, every extension record restates source" and the two missing rows.
6. **`phonemeFilter` lives at `mdl.header.phonemeFilter`**, not under `facial`
   (`seam_map_model.md:222` is wrong; the decoder is right).
7. **`animation-architecture.md` §2** describes a glTF morph merge that no longer exists (the
   `.eskm` `MORF` section is already flat), calls the clips "compressed" (the bake sets no
   compression settings), and says "smallest compatible bank-family skeleton" (the partition is
   greedy first-fit, not minimal). Three sentences to correct.
8. **`physics-architecture.md`'s `RAGD` chunk is superseded.** The unit's `physics` already
   carries every field the chunk was specified for plus `editParams` (`totalmass`, `jointmerge`).
   PHYS1 reads the unit; no chunk is built (§4 D9).
9. **The 8.4a `solid`/`disableshadows` row looks stale against code**: both keys are read on the
   catalogue and plain-static branches with a passing test; only the no-catalogue fallback branch
   skips them, and that branch is dead on a v8 index. R8.0b re-verifies and corrects the roadmap.
10. **`mdl-coverage-and-gaps.md:108-147`'s "280 of 484 pass the byte ledger"** predates the model
    seam (2026-08-11); today every install member is a published unit with a complete ledger and
    the corpus index reports `unclaimed: 0`. Marked closed by the model seam.
11. **`seam_map_character.md` and `seam_map_animation_bank.md`** describe retired identities and
    frame themselves as offline research products. They are deleted in R8.2; anything not already
    in `seam_map_model.md` (the eye-node convention, the byte-ledger states) folds into it.
12. **A bank clip's authored columns are identical for every body that plays it.** Measured:
    5,609 of 5,609 `(label, owner)` pairs carry the same activity, weight and flags across all
    293 bodies; only `rawIndex` differs. `J_animation_contract.md` §2.1 gives per-body variation
    as the reason those columns cannot ride the clip, and that reason does not hold — which is
    the licence for D7's denormalisation of the three selection columns onto the body row.
13. Stale literals: "7,871 clips" (`bake_characters.py`, `character_partition.py`), "166 models /
    136 banks / 527 blend spaces" (`roadmap.md`), the index "~47 KB" and slice "~92 KB" comments
    (`ElysiumContentPaths.h`, `ElysiumNpcClips.h`; real: 2.51 MB and ~550 KB),
    `ElysiumTextureCache.h`'s `AElysiumMapActor` ownership claim, `ElysiumFacialRig.h`'s "five
    families / Jeanette alone", `ElysiumModelParityTests.cpp`'s citation of the deleted
    `.claude/rules/tests.md`.

## 3. Defects found

| # | Defect | Where | Live? | Fix in |
|---|---|---|---|---|
| F1 | `rest_pose_static_equivalent` tests `bone.index in split` where `split` has been a `(dict, set)` tuple since `cfee8dc7`; the split-rotation correction is never applied to static equivalence, so a `static_equivalent: true` can license a storage pose | `placed_models.py:298-302` | **yes** | R8.0b (fix + re-census: every placed model with a split bone and `static_equivalent: true`) |
| F2 | `FindWieldModel` / `SweepWieldModels` test `LeaderPoseComponent == Body`, so the 57 rigid-bound melee weapons are invisible to the trail, the pawn and `LabWieldCheck`; `ElysiumNpcVisual.h` still claims "no per-binding branch" | `ElysiumNpcVisual.cpp:332-365` | **yes** | R8.0b |
| F3 | The corpus index backfill labels `playermodel` as `wield` and `infomodel` as `ground-item`; `roles: wield` (189) is really `playermodel ∪ wieldmodel_*` | `corpus_index_glb/backfill.py:89-90` | **yes** (index) | R8.0b |
| F4 | `ClanDataTables` has no typed vdata projector, so 27 of 59 player bodies carry no role | `vdata_glb/projection.py:607-641`, `backfill.py:224-235` | **yes** (index) | R8.0b |
| F5 | The runtime `SetModel` branch hands the character basename fold to `Request.StaticStem`, which expects the whole-path `static_stem` (`palm` vs `SM_models_scenery_props_palm`) | `ElysiumProp.cpp:550`, `ElysiumGreenRoomRun.cpp:479` | **yes** (on a nested path) | R8.0a (the resolver closes it) |
| F6 | The player's eyeballs never fade: `InstallEyes` MIDs the eye slot before `RefreshBodyVisibility`, and the eye master has no `ModelAlpha` and is opaque | `ElysiumEyePass.cpp:153`, `ElysiumPawn.cpp:230` | **yes** (inferred from code; verify in game) | R8.2 (`M_V2_Eyes` ruling) |
| F7 | `andrei` carries the same 14 clips under `Anims/andrei/` and `Anims/_banks/…_and_and/` — a body that is also a cinematic root is walked twice; both sets are stamped, nothing reports it | `bake_characters.py:528-557` | yes (waste + two assets per label) | R8.1 rule + verifier |
| F8 | `phonemes_male` is named in comments and never tried; both lipsync call sites fall back to `phonemes` only | `ElysiumChoreoScene.cpp:1642`, `ElysiumEntityWorldDialogue.cpp:638` | yes (male fallback silently wrong) | R8.5 |
| F9 | `demal_expressions` is VFE-only and the mirror copies `.txt` only, so one shipped table is unloadable | `UE_extract_scenes.py` | latent (unreferenced) | R8.5 (tables import from `table`) |
| F10 | `BakedPropMesh` never applies `MaterialSafeName`; a dashed or dotted model path resolves an asset the bake never wrote | `ElysiumContentPaths.h:328` | latent (0 dashed stems today) | R8.0a (`BakedUnit(id)`) |
| F11 | `npc_export`'s basename-collision fallback (`bank_stem`) has no C++ counterpart in `ModelStem()` | `npc_export.py:879-902` | latent (unmeasured) | R8.1 measures; delete one side |
| F12 | ~~`A_katana_bobble_layer@katana_aggressive_run` is not baked while its `bushhook` siblings are~~ — **closed 2026-09-04, not a defect**: `_derived_bindings` derives an overlay against a host only when the clip's own mask owns a `SPLIT_ROTATION` bone, and the `katana`/`knife`/`stake`/`tireiron`/`baseballbat` bobble layers carry mask 1 (24 arm bones, entirely below the split) where `bushhook`/`sledgehammer` carry mask 2 (49). A mask-1 overlay is already an ordinary parent-relative pose, so nothing derived is missing; the reporter now asks the exporter's own question | `animation-critical-path.md` | no | closed |
| F13 | Two additive signals exist (`clips/` `Flags & 0x14` and `UElysiumAnimPostAdditive`) and nothing asserts they agree | `ElysiumNpcClips.h`, `ElysiumSkeletalBuild.cpp` | latent | R8.5 (one signal) + T-B3 |
| F14 | Five `except Exception: continue` around the `.ents` seed walk can drop a whole map's cast silently | `npc_export.py:86-189` | latent | dies with `npc_export` (R8.1) |
| F15 | `mdl_gltf._choose_skeleton_root` duplicates `UE_mdl_skeletal._single_root` in a second number system with no tests | `mdl_gltf.py:457-468` | drift risk | dies with `mdl_gltf` (R8.1) |

## 4. Rulings

Each ruling names the seam doc it is written into before code. An **owner call** is a ruling the
owner signs; every one carries a default so the plan is executable without it.

### D1 — Identity and naming: one standard for every asset, the mount mirrors `exports_v2`

**Owner ruling (2026-09-04): R8 is an architectural pass, and no rename is left for a later
project.** The unit of work is `vtmb:model:<key>`, and every asset R8 writes — and every asset a
landed lane already wrote — lands on the one baked-asset standard now owned by
`seam_map_unit_contract.md` → "Baked assets": `/ElysiumBaked/<Kind>/<dir>/<Prefix>_<base>` with
per-label products nested under `<base>/` and corpus-wide assets under `<Kind>/_Corpus/`. For
this lane:

```text
vtmb:model:character/npc/unique/downtown/lacroix/lacroix
  -> /ElysiumBaked/Models/character/npc/unique/downtown/lacroix/SK_lacroix        the body mesh
                                                                 SKEL_lacroix      its skeleton
                                                                 SM_lacroix        the static twin (the props lane, same folder)
                                                                 CLOTH_lacroix     (…_<n>, …_PHYS)
                                                                 PHYS_lacroix      PHYS1, later
                                                                 DYN_lacroix       the generated hair recipe
                                                                 DA_lacroix        the body data asset (D7)
                                                                 MI_lacroix_DetailSway   (a detail prop's sway instance, where one exists)
     /ElysiumBaked/Models/character/npc/unique/downtown/lacroix/lacroix/A_<label>  clips, BS_<label> blend spaces, A_<layer>_<host> derived forms
vtmb:model:character/shared/male/move_and_ranged
  -> /ElysiumBaked/Models/character/shared/male/SKEL_move_and_ranged   (a bank's own skeleton, when it is its own family)
     /ElysiumBaked/Models/character/shared/male/move_and_ranged/A_…     the bank's 2,291 clips and 254 grids, no `_banks` namespace
vtmb:model:weapons/katana/wield/w_m_katana
  -> /ElysiumBaked/Models/weapons/katana/wield/SK_w_m_katana          (+ SM_w_m_katana, the static twin)
  corpus-wide
  -> /ElysiumBaked/Models/_Corpus/SKEL_Family_<crc32>   bank-family skeletons (content-named, not lowest-member-named — a member added later cannot rename the family)
     /ElysiumBaked/Models/_Corpus/DA_Cast               stem <-> id, roles (player body, npc, bank, cinematic root), the successor of npc_index.json's npcs section
     /ElysiumBaked/Models/_Corpus/DA_PropSkins, DA_WieldModels, DA_PlacedModels, DA_CinematicSets, SM_Missing
```

Blend profiles keep `ElysiumLayerMask_%08X` (already content-derived). One resolver
(`asset_paths.baked_path` / `FElysiumContentPaths::BakedUnit`, twins over one golden fixture)
answers every path from the unit id; `static_stem`, `PropModelStem`, `BakedPropMesh(stem)`,
`BakedCharacterMesh(stem)`, `BakedBankAnim`, `mesh_asset`, `texture_asset_name` and
`baked_asset_name` retire (R8.1 proves the four clip labels on which `baked_asset_name` and
`safe_name` differ do not collide once folded by `safe_name`, or names the divergence).

**The stem does not disappear; it stops being an address.** `FElysiumAnimating::ModelStem()`
becomes `ModelId()` (built from the raw `model` path the entity already holds);
`Swing.ClipOwnerStem` becomes the owner id (saves are disposable); `DA_ClothTuning` and
`DA_HairDynamics` are re-keyed to ids in the same change as the eye/cloth work (R8.2), with a
content test that every key resolves. Everything human-facing that is keyed by stem today — the
two retail captures, `_oracle/<stem>.json`, the parity slice, `-CastBody=`, the green room and
arena pickers, `verify characters <stem>` — resolves through `Models/_Corpus/DA_Cast`, so no
capture is re-keyed and the debug pickers stop scanning `npc/*.eskm` (L §4's follow-up closes
here). The body stem in `DA_Cast` is `basename(model).lower()`; R8.1 measures whether any shipped
body collides on it (F11) and, if one does, the table carries the collision-resolved form.

**Drift the standard exposes in landed lanes, all moved in R8.0a** (nothing is pushed later):

| Today | Standard | Cost |
|---|---|---|
| `Meshes/SM_<static_stem>` (593 on the mount, 3,661 selected), `Meshes/Detail/MI_DetailSway_<stem>`, `Meshes/DA_ElysiumPropSkins`, `Meshes/SM_elysium_missing_model` | `Models/<dir>/SM_<base>`, `Models/<dir>/MI_<base>_DetailSway`, `Models/_Corpus/DA_PropSkins` (keyed by id), `Models/_Corpus/SM_Missing` | re-stage + re-import the model corpus — this is also the model lane's first owner-approved `--all` run, so it authors ~3,068 assets it has never authored, not only a rename; re-bake the V2 maps; `BakedUnit(id)` replaces `BakedPropMesh(stem)`/`BakedItemMesh`/`BakedPropSkins` and the four `PropModelStem` call sites; `Def.ModelMesh` becomes the id |
| `/ElysiumBaked/<map>/…` at the mount root (117 folders, both bakes) | `Maps/<map>/…` | `BakedMapDir` + the three importers' root + both bake scripts + `mounts.py`; every map re-bakes (regenerable; the full-corpus re-bake is R9.2's acceptance anyway) |
| `Sprites/MI_Sprite_<key>_<Blend>` | `Materials/<dir>/MI_<base>_Sprite_<Blend>` | re-bake sprites (per-map, cheap) |
| `Sky/Textures/TC_Sky_<sky>` | `Textures/skybox/TC_<sky>_Sky`, **written by the texture lane** (which already imports and face-reorders the six units), the upper-hemisphere mean in provenance; the map bake binds it through the resolver instead of composing PNGs out of `shared/tex` | the composite moves lanes; `ElysiumEnvironment::BuildSkyCubeFrom`'s private PNG reader retires with it |
| `Sky/Materials/MI_Sky_<sky>` | `Materials/skybox/MI_<sky>_Sky` — derived from VtMB units, so baked, not generated | path constants |
| `Sky/Meshes/SM_SkyDome`, `Lookdev/Materials.umap` | `/Game/ElysiumGenerated/Sky/`, `/Game/ElysiumGenerated/Lookdev/` (no VtMB unit behind them) | path constants |
| `Shared/{Materials,Textures,Meshes}` (17,107 assets, the mount's largest root) | **stays as it is** — the legacy map bake's own corpus, retired by R9.2 with its producer; it joins the tracked legacy-root list rather than the standard | none |
| `Materials/particles/MI_Particle*` placeholders | `Materials/_Corpus/` | path constants |
| `Textures/`, `Materials/`, `SurfaceProperties/` | already on the standard | none |

Every composite carries a `<Role>` (`_Sky`) so it can never occupy a unit's own address, and
the stage asserts that no composite path equals a unit path — golden fixture
`vtmb:texture:skybox/hav`, a real six-face cube unit whose own asset is `Textures/skybox/TC_hav`
and whose composite is `TC_hav_Sky`, with five maps naming `skyname hav`.

Path length: the longest baked path today is 206 characters (a legacy prop with a folded stem);
under the standard the deepest character folder plus the longest derived label is ~225 from the
repo root. The stage refuses > 240 and the note records `LongPathsEnabled` as the escape hatch.

→ `seam_map_unit_contract.md` → "Baked assets" (written); `seam_map_model.md` → "## Import"
rewritten and "## Import — skeletal" added; `seam_map_map.md` → "## Import" root;
`seam_map_material.md` sprite twin; `seam_map_texture.md` sky cube.

### D2 — The stage/bake split: Python decides, C++ authors, one staged payload between them

The props lane's shape applies: an offline stage writes `$ELYSIUM_WORK_ROOT/import/characters/
manifest.json` plus one `<key>.provenance.json` per unit; a headless editor phase reads the
manifest and authors assets; provenance rides on the asset; prune is manifest-driven. But the
skeletal builders **stay in C++**: GeometryScript's `UDynamicMesh` has no bones, weights or morphs,
so `bake_lib.create_static_mesh`'s Python door does not exist for a `USkeletalMesh`, and the
31-commit history of `ElysiumSkeletalBuild.cpp` is a list of engine-behaviour rules that are
independent of the input format.

The handoff between the stage and the builders is a **staged skeletal payload per unit** — the
existing section layout (`SKEL`, `ATCH`, `MATL`, `MESH`, `MORF`, `MASK`, `ANIM`), versioned as a
stage-internal product under `import/characters/<key>.skel`, read by `FElysiumSkeletalSource`
through its existing section parsers. It is not an export product, is never published, is never
read by the game, and lives beside the manifest that names it. `DYNM`/`BDYN` are dropped (dead on
both sides). This keeps the R8.1 differ byte-level and the C++ change to a path.

Rejected: typed `USTRUCT` payloads (a second serialization contract for ~350 lines of C++ that
already parse the sections); a Python skeletal builder (no engine door).

→ `seam_map_model.md` → "## Import — skeletal", stage/import phases.

### D3 — Composition stays at bake; the `@host` fan-out is kept for R8, revisited later

The `<layer>@<host>` derived family (additive conjugation, host composition of overlays that own
`Bip01 Spine1`, the motion-additive fold) and the split-rotation correction are stage rules ported
verbatim, because "poses are baked native" and T-B1's bake half is a hard constraint: raw delta,
no `AdditiveAnimType`, no `RefPoseSeq`, `UElysiumAnimPostAdditive` tag, untracked bones at the
additive identity, no `<delta>@<host>` built. The 3× multiplication of bank clips it costs
(1,505 derived against 786 raw on one 68 MB bank) is recorded as a **modernization candidate** —
compose the overlay in the AnimGraph over Unreal's layered blend — for after R8, in
`animation-architecture.md`. Not in R8.

The sequence walk the stage implements is the legacy one (label-deduped, invalid base cells
skipped), so the payload is byte-equal; the differ reports every descriptor the dedup drops and
whether any is reachable by activity. If none is, the dedup stays and is recorded; if one is, it
is an owner call in R8.5.

→ `animation-architecture.md` §2 (the constraint list) and §8 (the candidate).

### D4 — Bank families: the legacy algorithm over the whole corpus, from the V2 units

`eskm.rig_families` (greedy, sorted, bone-tree compatibility, case-folded, one root after merge)
is re-run over the V2 bank units' `mdl.bones[]`, always over the whole corpus, never a slice; the
result is a section of the stage manifest (the successor of `families.json`). Connected
components over include edges is the wrong relation — it yields two clusters of 226 and 140
members through the `conversations` banks. Bodies stay singletons. The bank-skeleton set is
whatever the partition says (10 today); a slice-scoped run is refused.

→ `seam_map_model.md` → "## Import — skeletal", partition subsection.

### D5 — Cutover is per body, on the R4.6 shape: one tracked list, read by the resolver and the pipeline

Under D1 the new lane writes `Models/…` and the legacy bake writes `Characters/…`, `Props/…`,
`Items/…` — different roots, so the two coexist and the runtime has to choose, which is exactly
R4.6's situation for maps. The mechanism is R4.6's: `UElysiumMapTransportSettings` (or a
sibling `UElysiumModelTransportSettings`) gains `ModelsOnV2` — unit ids, plus `families:` rows
for bank families — a tracked, reviewable list an unlisted unit never probes disk for.
`BakedUnit(id)` resolves a listed id to the standard path and an unlisted one to the legacy
accessor; `model_transport.py` reads the same list so the legacy bake skips listed units, the
new lane imports only listed units, and the sweep is split by it. When the list is complete the
legacy accessors and roots are deleted (R8.5 / R9.2). Order: the 56 player bodies first (chargen
and the green room need them on every boot, zero map interaction), then the three test maps'
casts (17 / 6 / 22 stems), then the rest. Banks and props flip wholesale with R8.1 (the payload
is byte-equal, so the output is identical whoever writes it). The review checklist names the
cross-map blast radius of a body.

Nothing on the mount is overwritten, so the asset differ (§6 R8.2) compares the legacy
`Characters/…` asset to its `Models/…` counterpart through the resolver and `DA_Cast` in place.
What is frozen before R8.1 is the **export** tree `npc/` + `items/` (`$ELYSIUM_WORK_ROOT/
_r8_legacy_export/`), because R8.1's producer writes the legacy products at the legacy paths —
the `--legacy-root` R3.3 grew for the same reason.

→ `seam_migration.md` R8 header; `seam_map_model.md` → "## Import — skeletal", cutover;
`seam_map_map.md` → "The explicit per-map cutover flag (R4.6)" gains the model list.

### D6 — Materials: a skinned sibling master pair, `M_V2_Eyes` completed, wield masters retired

**Owner call (default: as ruled here).** Two masters join the inventory, `M_V2_LitSkinned` and
`M_V2_LitSkinnedTranslucent`, authored by `_build_lit` with a `skinned=True` rider: usage
`skeletal`, `morph`, **`clothing`** (no `ism`, no `nanite`); default `BLEND_MASKED` with
`dither_opacity_mask` and clip 0.333; `ModelAlpha` (scalar, 1.0) and `UseAlphaTest` (switch,
from `$alphatest`, 43 units); `OpacityMask = lerp(1, BaseTexture.a, UseAlphaTest) × Alpha ×
ModelAlpha`. Routing is by **consumer**, and a consumer is a `shape != "skeletal"` model binding **or** a
drawn (non-tool, non-nodraw) map face whose `texinfo.texData` resolves to the unit: a unit with a
skeletal consumer takes the skinned master, a unit with both gets an `MI_<unit>_Skinned` twin
beside its world instance (the `MI_<unit>_Decal` precedent). The shared-consumer count is **311**;
307 are lit instances needing the twin, and four retain a non-lit master. The initial 289-unit
census classified source shape alone; 37 rigid character units still receive skeletal products,
adding 22 material routes. The predicate is shared with character staging. The count includes
`metal/metald` and `metal/walkwayb` are bound only by a skeletal vent prop yet are drawn as world
geometry on 21 maps including `sp_tutorial_1` (~3,996 drawn faces), and the skinned master carries
no `nanite`/`ism` usage, so a model-only predicate drops them to the engine default in a cooked
build. **Every skeletal consumer resolves the material provenance's `skinnedAsset`**, which names
the twin for a shared lit material and the ordinary instance for a skeletal-only or non-lit unit.
This keeps D10's static-twin material lookup from binding the world sibling. `M_V2_Lit` drops `skeletal`/`morph` once R8.4 routes
props to the sibling.

Why not the two options R8.2 named: 1,399 of 1,540 character slots are Opaque and an opaque
material compiles no opacity mask; `DitherOpacityMask` is a `UMaterial`-only property with no
instance override; a MID sets no base property and no static switch. And **the clothing usage flag
forces the split anyway**: no V2 master sets `used_with_clothing`, so every garment section on a
V2 master draws engine grey in `-game`. Fallback if more masters are refused: per-`MI_`
`bOverride_UsageFlags` fixes the cloth flag and leaves the fade a hard clip.

`M_V2_Eyes` gains `IrisOrigin`/`IrisU`/`IrisV`/`NormalOrigin`/`EyeUpN` (vectors),
`Flatten` (0.5), `ModelAlpha`, a clamped `Iris` sampler, two-sided, `tangent_space_normal` off,
and **`Vampire` becomes a scalar** (a MID cannot flip the `VampireEyes` switch; `$vampire` sets it
on the instance, 12 units). The five vectors stay per eye per body per frame on the MID
(`FElysiumEyePass` unchanged); only the iris read dies, because `$iris` is bound on 374/374 eye
instances already. Eye detection (`GetBaseMaterial() == master`) survives with the path changed.
`M_V2_Unlit` gains `ModelAlpha` on its existing mask path (the 12 form units the player becomes).
`M_V2_Refract` gains `skeletal` + `morph` (the obfuscate skin is `$model 1`).

`M_PlayerBody`, `M_Eyes` and the four `M_Wield_*` retire with no replacement; the weapon set
(138 Lit + 16 LitTranslucent) is strictly subsumed and gains the real `TC_` cube binding.

Named divergences recorded with the ruling: `$halflambert` (0 occurrences; a fixed StudioRender
term; Lumen plus the surface LUT is the modernization); `two_sided` from `$nocull` (103 of 1,540)
instead of the legacy blanket flag — **verify on a hair-heavy body before signing**; the cornea
constants of `M_Eyes` (specular 0.6 / roughness 0.15) recorded as the baseline the LUT replaces.

Draft text for all three blocks is in `E_materials_eyes.md` §8.

→ `seam_map_material.md` → "Master inventory", "Exposed parameters, by master", "Runtime bind".

### D7 — Facial, eyes and procedural ride the mesh; clips and joins ride the body asset

Data that is index- or name-aligned with one mesh goes on that mesh as `UAssetUserData`
(`UElysiumCharacterProvenance`, mirroring `UElysiumModelProvenance`): identity (`AssetId`,
`ModelPath`, `Stem`), slots and skin families, the facial rig (controllers, RPN rules,
flexdescs, morph ramps, lids, mouth, phoneme filter — the rules are products and quotients, so
the evaluator stays and only its source moves), the eye set (with `Iris` as a soft texture
reference and `bVampire`), the procedural axis-interpolation table with `DriverAxes` (converted at
stage, as `UE_mdl_skeletal.unreal_axis_rules` does today), `split_bones`, the cloth reference,
the cinematic-root fact. Not on the `USkeleton`: bank skeletons are shared, and the face's curve
names stay per body (293 bodies, 293 skeletons — a rule, stated).

Data that is a join between a body and its banks goes in a per-body **`UDataAsset`** `DA_<base>`
beside the mesh in the unit's `Models/<dir>/` folder, reached through `BakedUnit(id)`: the
sequence table `(label, owner, activity, weight, rawIndex)` in include-tree order with soft
pointers to the resolved sequence or blend space (the `DA_WieldModels` shape), and the per-body
autolayer view already resolved to the derived `@host` asset — which deletes the whole
`<label>@<host>`-then-plain ladder and `ResolveLayerHost`. **Not a `UPrimaryDataAsset`:** the
project has none, no `PrimaryAssetTypesToScan` and no `FPrimaryAssetId`, and that name namespace
is flat, where 5 character basenames and 40 bank bases collide. An asset manager is its own
roadmap row if it is ever wanted, not a clause here; `PreloadMapAnimations` keeps its wire
closure and changes only its unit of work, to one `FStreamableManager::RequestAsyncLoad` over the
union of the map's body assets, held for the map epoch.

**The split criterion is what a selector needs before anything is loaded.** A column a selector
scores over *unloaded* candidates rides the **body row**; everything the player of a loaded clip
needs rides the **clip**. So `reachCm`, `lowReachCm` and `comboMask` move onto the sequence row
(optionally folded to `maxReachCm` per activity), because `MaxReachCmForActivity` and
`PickByStateMask` scan up to 23 candidates on a katana and 76 on `ACT_DISPOSITION` before
choosing — on per-clip metadata that is that many synchronous package loads on the swing frame,
which R8.5's own "no synchronous read inside a tick" forbids. The invariant is an acceptance
item: **no selector resolves a soft pointer to score a candidate.** Per-cell motion stays on the
cell sequence, because `FBlendSample::Animation` is a hard reference and the grid is already
resident.

Data that is a property of the clip goes on the clip as `UAnimMetaData` beside the existing two
classes: `Flags/Fps/Frames/Fade`, the melee block (reach, low reach, blocked reaction, envelopes,
swings, combo), the **event timeline** (metadata, never `UAnimNotify` — T-C1 owns retail's window)
and the **movement path** (metadata, never root motion — `ElysiumClipMovement.h` owns the rule),
and on a grid cell its motion (less the three selection columns above). Per-grid metadata names
each axis's pose parameter and `loop`;
ranges are already on `FBlendParameter`. Masks stay `UBlendProfile` with the mirror. The
sidecar's additive bit and `UElysiumAnimPostAdditive` collapse to the metadata (F13).

Corpus-wide tables under `Models/_Corpus/`, rooted once like `DA_WieldModels`: `DA_PlacedModels` (keyed by
`vtmb:model:` id — `AssetId`, skeletal and static soft refs, clip mode, static equivalence, rest
candidates, clips; a `TMap`, not a 2,899-row scan), `DA_CinematicSets` and `DA_Cast`. Expression
tables get their own small import lane (250 units): `ExpressionTables/DA_<base>` per unit plus
`ExpressionTables/_Corpus/DA_ExpressionTables`, imported from the VFE `table` (what the client loads; the
26 graded TXT/VFE differences are a named correction), with `phonemes`/`phonemes_male` fallbacks
from `selectedTables[].fallbacks` (F8). `lip/` and `scenes/` stay corpus text and move with R9.1.

The resolver keeps `FElysiumClipTable::Find` semantics so callers compile untouched; the six cache
doors collapse to one `LoadObject<UElysiumBodyAnimData>`; `FElysiumBlendTable` splits by kind;
the pure grid arithmetic and both anim nodes are untouched.

→ `animation-architecture.md` §2.1 and §6; `seam_map_model.md` → "## Import — skeletal", cooked
products.

### D8 — Cloth is re-pointed, hair is cooked for all and gated by the authored table, ragdoll reads the unit

**Cloth**: `UElysiumClothBuildLibrary` retains its simulation construction rules and gains
standard unit addressing and saved-product verification. `DA_ClothTuning` is re-keyed to model
IDs with its authored values preserved. The reader consumes the stage's converted garment record (inch→cm, Y reflection, winding
reversed once, `rest_length_squared × 2.54²`, radii × 2.54, `rig_bone_name` on every name,
bind-space colliders, `*_local` dropped). The stage **must re-key `render_maps` through
`sourceVertices`** onto the V2 primitive order — the cloth record inside the unit is keyed to the
legacy surface order the V2 mesh does not use. Acceptance is the build's own counters
(`orphaned`, `root-bound`, `degenerate normals`, all 0 on the 49 today) diffed against the
frozen 49-line log. The character garments the legacy partition never reached are retained.
All six scenery units with garments use this same cloth build; five have a one-bone static source
shape and therefore need a skeletal projection for cloth in addition to their static twin. Their
source shape stays static, and material consumer routing includes both representations. The whole
GLB census is 59 units and 60 garments; source shape must not exclude those five cloth owners.

**Hair / secondary motion**: the stage resolves the child walk (bone indices → names) and the
provisional unit mapping and writes a generated `DYN_<stem>` per body (the structs in
`ElysiumHairDynamicsData.h`) for every source owner (107 units and 600 records in the expanded
corpus); `InstallHairDynamics` composes the generated recipe
with `DA_HairDynamics`, whose keys become model IDs while its values stay unchanged. It stays
the override **and the install gate** until LIFE9 thaws.
Breast rows are cooked into the asset's `Bodies` array and not installed (the six bouncy-boobs
owner calls stay open where they are). `DYNM`/`BDYN` and their dead readers retire.

**Procedural**: on the mesh (D7); the evaluator stays; the 24 prop tables go on the props'
`SK_` in R8.4.

**Physics data**: the unit's `physics` replaces the proposed `RAGD` transport. R8 imports a
`UElysiumPhysicsData` source-data asset and roots it from the owning mesh metadata. It retains
ordered solids/ledges, hull vertices and triangles, constraints, parameters, unknown fields,
source-bone gaps and provenance. Fresh-process verification compares these records with their
hashed GLB/stage inputs. Source gaps remain explicit; no body or constraint is dropped to make a
simulation builder pass. Simulation-ready `UPhysicsAsset` construction, calibration, ragdoll
activation and gameplay physics are deferred and cannot block R8. Existing simulation behavior
is not expanded by this migration.

→ `seam_map_model.md` (garment re-key, domains rows), `physics-architecture.md` (L0 deleted),
`plans/gameplay.md` PHYS1 (reads the unit), `plans/animation.md` LIFE9 (gate stated).

### D9 — Wield: skeletal always, the decision layer survives, the table is kept

Every wield unit builds a `USkeletalMesh` (the attach math needs a reference skeleton; the trail
socket is a skeletal socket; three of five bindings are skeletal); the props lane's static twin is
untouched. The reference-pose override and the re-skin (`M_ref · M_bind⁻¹`, same influence rules)
**move into the skeletal build**, not optional (68–144 cm placement error without). `wield_corpus`
(classification, the four checks, `TrailTip`) is invoked unchanged by the stage over GLB-decoded
bones; the item join comes from the `vtmb:vdata:` unit's resolved model fields. `DA_WieldModels`
is kept under `Models/_Corpus/`, keyed by classname, with `AssetId` added and the mesh path
resolved from the id through the standard (`Models/weapons/<dir>/SK_<base>`).
Viewmodels (19) and hands (21) are **not** built: the build can consume them, the products are
LIFE6's. `items/ground_models.json` retires for a package-exists test on the `SM_` via
`FElysiumContentPaths::BakedModel(id)` (123 of 125 resolve today; the two dangling ids are
diagnostics, no placeholder). The seven prop-bone channel exemption stays byte-identical through
R8.2's build rewrite — losing it mounts every melee weapon 123° off.

→ `seam_map_model.md` → "Wield, item and placement joins" + "## Import — skeletal";
`wielded-weapon-integration.md`.

### D10 — Animated props: one material authority, and the static-equivalence rule moves to the stage

Skeletal props bind their `MI_` at bake from `materialBindings` (the same resolution the static
twin already has, through the skinned master per D6); `BindMapMaterials` retires; `ApplyAnimatedPropSkin` keeps only the family
override and drops its reset-to-base; `ConfigureRest` narrows to a collision source; the V2 skins
table needs no change. `rest_pose_static_equivalent` is ported to the unit (bind + frame 0 of
every rest candidate, 0.01 cm / 0.1°) after F1 is fixed, and the census is re-run before any
`static_equivalent: true` is trusted. The FNV-1a rest choice and its placement token are
unchanged. Ming Xiao, the one multi-submodel character, bakes every submodel as a section with a
named anomaly — the props lane's "submodel 0" rule would lose half a boss.

**Character and wield skin families gain a consumer** (scope add; owner row in §8). The table's
schema and fold need no change; its **input set** does: the 13 multi-family character units and
R8.3's wield units get rows in `Models/_Corpus/DA_PropSkins`, whose `Overrides` are hard
`TObjectPtr<UMaterialInterface>` and so stay reachable cooked. The applier goes on
`FElysiumAnimating` at install **and** on the `skin` key/input write, reusing the reduced
`ApplyAnimatedPropSkin`, and the rule is `seam_map_model.md`'s own —
`skinFamilies[min(skin, familyCount-1)][skinReference]` per skin reference, VtMB's clamp
reproduced — not "family 0". Without it 83 Chinatown temple guards plus Gary, Ash and the Sabbat
henchman draw the wrong body, and no test map places a skinned body, so the shot baseline cannot
see it. Acceptance: a content test that all 90 authored placements resolve to their authored
family row, plus one `ch_temple_2` capture, the only map naming all four `temple_guard` families.

→ `seam_map_model.md` → "## Import — skeletal", props; `animation-architecture.md` §1.2.

### D11 — Parity is an equality, and the differs come first

R8 is a producer change. Acceptance is "the numbers did not move", not "green": `RigCompose`
is pinned at its current reading (control 3.168 cm, layered 1.358 cm — T-C7 owns that residual),
`OracleIdentity` at 0.001–0.003 cm median, the running-graph identity at 0.009 cm, `compose`
arm peak-to-peak at 2.336 / 1.663 cm. A rebuild that improved any of them is a finding to explain.
Two differs are built before any producer code: the **product differ** (R8.1: staged payload and
sidecars against the frozen legacy export, per section, byte-equal or named-divergence with a
float classifier like R3.3's) and the **asset differ** (R8.2: `verify characters --legacy-mount`,
inventory 0/0/0, skeleton binds ≤ 1e-4, profiles identical and ∈ {0,1}, clip poses at five
fractions after `WaitOnExistingCompression()` ≤ 0.05° / 0.05 cm ordinary and ≤ 0.5 on a delta,
blend samples ≤ 1e-3 with the legacy-length flag, metadata parity on the 118 post-additive
clips). One new instrument: the crossfade residual (0.5° / 5° / 10°) has no test and is the
licence for "poses are baked native"; R8.2 adds it.

T-A5 is marked DONE before R8.2 (it is the instrument); T-B3 lands first inside R8.2 (the binary
mask guard, extended to the mirror); T-C8 is measured offline before the R8.2 re-bake so the cast
is re-baked once.

**T-B2's bake half landed on 2026-09-04, before this plan runs.**
`bUseLegacySamplePointAnimationLengthCalculations = true` is set on every `UBlendSpace` the
skeletal builder authors, and `Elysium.Content.FanDuration` is green — 225 fans over 84 owners,
207 scored across 1,863 sampled headings, every one blending durations to within 0.0010 s. Two
consequences here: R8.5's blend-space writer **inherits** the flag rather than landing it, and the
acceptance becomes "stays green"; and the **six placed-model fans are still on the harmonic
branch** (`Props/character_monster_wolf_form_wolf_form/BS_wolf_Form_run{,2,3,4}` and
`Props/character_monster_tzimisce_creation3_tzim3/BS_hit_head`), because the cast run cannot reach
the props path — R8.4 authors them through the same builder and closes it.

→ `animation-critical-path.md` (task sequencing notes); `seam_map_model.md` → "## Import —
skeletal", verification.

### D12 — Scope fences

Not in R8: simulation-ready PhysicsAsset construction, solver calibration, ragdoll activation
and gameplay physics (PHYS1 and later); viewmodels and hands (LIFE6), hair widening and
breast rows (LIFE9 and its owner calls), Obfuscate (disciplines), `lip/`/`scenes/` deploy
(R9.1), LOD import for skinned meshes (recorded: 517 character/weapon units carry >1 LOD and
146 a shadow row; morphs exist on LOD 0 only, so LODs need `bGenerateMorphTargets` or a face that
freezes at distance — a follow-up after parity, with the props lane's screen-size policy), the
armor-equip → body-swap wire (pre-existing gap, filed separately), the `@host` modernization
(after R8), the 6 scenery garments (props lane), the cinematic per-actor slices (kept as they
are; four skeletons from one unit is a later call).

## 5. The lane

```
exports_v2/models/<key>.glb  ──stage──▶  $ELYSIUM_WORK_ROOT/import/characters/
                                           manifest.json            one entry per unit; recipe; keep set; partition + cast sections
                                           <key>.provenance.json    → UElysiumCharacterProvenance
                                           <key>.skel               staged skeletal payload (SKEL ATCH MATL MESH MORF MASK ANIM)
                                           <key>.garment.json       converted cloth record (re-keyed)
                                           <key>.body.json          sequence table, autolayer view, clip metadata, cell motion
                                         ──import──▶ /ElysiumBaked/Models/<dir>/{SK_,SKEL_,SM_,CLOTH_,PHYS_,DYN_,DA_}<base>
                                                     /ElysiumBaked/Models/<dir>/<base>/{A_,BS_}<label>
                                                     /ElysiumBaked/Models/_Corpus/{SKEL_Family_<crc>, DA_Cast, DA_PropSkins, DA_WieldModels, DA_PlacedModels, DA_CinematicSets, SM_Missing}
exports_v2/expression-tables/<key>.glb ──▶ /ElysiumBaked/ExpressionTables/DA_<base>, /ElysiumBaked/ExpressionTables/_Corpus/DA_ExpressionTables
```

**Landing on work in flight.** A parallel session is adding two things to the model lane this
plan builds on, and R8.0a composes with them rather than around them: `_fold_owner_collisions`
(two units folding to one asset path is a stage failure, not a race one wins) and
`_merge_prior_manifest` (a scoped run carries forward the rows and `keep` entries its narrower
selection leaves out). Both share the producer stamp's instinct — the collision guard is the
fold's own proof and R8.0a extends it to the per-label nest and the composites; the merge is what
makes a *scoped* run of one lane safe without `pruneScope: null`, while the stamp is still what
makes *two lanes in one root* safe.

**Selection**, from three inputs, not one. (a) Every `models/character/**` unit with an inbound
data edge after the F3/F4 fixes (map entities, `clandoc000.txt`, scenes, scripts), plus the
include closure of banks, plus every skeletal `placed-prop`, plus the 67 wield units the vdata
fields name. (b) A tracked **`codeReferenced:`** list, because a unit can have a consumer in C++
and no data edge at all: seeded with the 20 `character/gibs/**` units (every one carries
`physics.solids`, 8 skeletal — PHYS1 exists for exactly these), the 7 `weapons/ejection/**` and
`weapons/projectile/bullet01`, each with a named consumer in `physics-interaction.md:27,37` and
`effects-architecture.md:237,743`. (c) The producer-assignment list of D5. **A refusal is
reported, never silent:** the stage manifest carries `unreferenced[]` (unit id, shape, whether it
carries `physics`, reason) and `import_report.json` prints the count — 69 character and 25 weapon
units today, an acceptance number. A data-edge-only rule refuses the severed-limb rigs and the
shell casings, and R8.2's differ ("0 unmapped either way") is structurally unable to see a unit
neither producer ever built.

**Stage rules ported from `UE_mdl_skeletal` / `npc_export` / `UE_mdl_cloth` / `placed_models` /
`wield_corpus`** (the 37 numbered transforms in `A_legacy_exporter.md` §2), grouped: basis
`(x,z,−y)·0.0254 → (X,Z,Y)·100` composed to one change (the reflection never appears; winding
reversal spent once), single-rooting with the largest-subtree rule, reparented-stray restatement,
`rig_bone_name` folding with a fatal collision, split-rotation correction per clip per frame with
bind anchoring, bind-fill of owned channels, mask de-dup, `@host` derivation and the channel
unions, the motion-additive fold (as it stands, not extended), cell naming and the unbound-grid
demotion, fan de-dup, ground speed, movement restatement, activity/weight, event interning,
melee conversions (envelopes scale-only), procedural axis directions, the eye basis, morph keying
by `(flexdesc, ramp)`, cloth conversion and render-map re-key, secondary-motion resolution behind
the allow-list, rest candidates and static equivalence, the partition, bank-owner attribution in
engine include order, the foreign-clan fidget drop, stem naming, the wield ref-pose re-skin, the
cinematic actor split. Every rule keeps its current test or gains one; the additive/host rules
gain pytest coverage from the differ's fixtures, because today only in-editor harnesses cover them.

**Import phase.** The existing builders with their inputs re-pointed: `BuildFamilySkeleton`,
`DeclareCompatibleSkeletons` (with the profile mirror), `BuildSkeletalMeshFromSource` (slots bound
to resolved `MI_`, `MakeSectionMaterial` deleted, `MorphThresholdPosition = 0`, split normals,
four influences, curve metadata, the seven prop-bone exemption, the wield re-skin added),
`BuildAnimSequencesFromSource` (rational frame rate, silent-appendix rule, masks before sequences,
additive identity sweep, metadata), `BuildBlendSpacesFromGrids` (reading the staged grid record
instead of `blends/`, `bUseLegacySamplePointAnimationLengthCalculations = true` in R8.5),
`BuildClothAssetsFromSidecar` (new input), plus the new writers: provenance user data, the body
asset, per-clip metadata, `DYN_<stem>`, the two corpus tables. Memory: the lane adopts the map
bake's `TickCommandletFrames` / `FinishAssetCompilation` / `UnloadBakedPackages` drain and reads
GLB JSON chunks without pulling the BIN until an accessor is needed (the character tree is
3.5 GB, LaCroix alone 87.8 MB); a full-cast forced bake is measured before R8.2 lands, as
`c7cd5100` measured the map bake.

**CLI.** `uv run elysium import characters [--bodies <stem>…] [--force] [--stage-only]`,
`import wield [--items <stem>…]`, `import props [--maps …|--all]`, `import expression-tables`,
each launching `pipeline/unreal/import_<lane>.py` with `-Import<Lane>=<manifest> -ImportForce=1`
and its own timeout constant; `verify characters --legacy-mount <path>` is the asset differ;
`doctor` gains a corpus-index-derived character line and reads the per-domain incomplete
markers. `export characters`, `export wield`, `-BakeCharacters=`, `-BakeWield=` retire at the end.

**And the composed build stays composed.** `uv run elysium export all` is the only whole-corpus
build path, and it names the legacy bundles today: R8.1 removes the `npc` and `items` bundles
from both `profiles.toml` profiles and their branches in `export_all.py` in the *same commit*
that deletes `npc_export.py` and `UE_extract_items.py`, or the command raises `ImportError` on a
deleted module; `test_profile_package_contract.py` grows an assertion that every bundle a profile
names resolves to a live module. R8.2–R8.4 put the new lanes into the slots `export_characters`
and `_ensure_wield_bake` occupy inside `export_profile`. The ordering constraint that lives only
in that function's step-3 comment is lifted into `seam_map_model.md` as a stated rule — **masters
→ textures and materials → models → characters, wield, props → maps** — because a map's level
stage loads every placed skeletal prop and its rest clip. "`export all` completes on a clean
export root" is an R8.5 acceptance line.

## 6. Tasks

### R8.0a — The standard: one resolver, every lane on it (one re-import, one re-bake)

- **The baked-asset standard implemented** (D1): `elysium_pipeline.asset_paths.baked_path` and
  `FElysiumContentPaths::BakedUnit` twins with the golden fixture; `Baked<Kind>(id)` accessors
  rewritten as one-line calls; the landed lanes moved — models re-staged and re-imported to
  `Models/<dir>/SM_<base>` with `DA_PropSkins` and `SM_Missing` under `_Corpus/` and the table
  keyed by id, `Def.ModelMesh` and the four call sites on the id, the map root to `Maps/<map>/`
  in both bakes, sprites and detail-sway instances to their unit's folder, the sky cubes to
  `Textures/skybox/`, the sky dome, sky materials and the lookdev map to `/Game/ElysiumGenerated`,
  the particle placeholders to `Materials/_Corpus/`; `static_stem`, `PropModelStem`, `mesh_asset`,
  `texture_asset_name` and the stem-keyed accessors deleted; `Meshes/`, `Sprites/`, `Sky/`,
  `Lookdev/` and the per-map root folders deleted; every map re-baked; `ModelParity`,
  `ModelNames` and the bake tests re-pointed at the resolver. `shared_corpus.static_stem`,
  `mesh_asset` and `texture_asset` survive this task and retire in R9.2 with the legacy map bake,
  which calls all three.
- **Prune and landing by producer** (the contract's own rule): the `ElysiumProducer` registry tag
  stamped beside `ElysiumRecipe` by every lane's existing `stamp_recipe`; every prune loop
  rewritten to delete only its own lane's assets; `pruneScope: null` on a partial-cutover
  manifest; `foreign` and `unstamped` counts in `import_report.json`. Without it the first
  unflagged `import models` after this task deletes the character lane's assets out of
  `Models/`, and the map bake's sprite instances out of `Materials/`.
- The tracked **legacy-root list** beside the resolver's kind roots (`Shared/` → R9.2,
  `Characters/` → R8.2, `Props/` → R8.4, `Items/` → R8.3), each row naming the task that empties
  it, plus the registry test that asserts every asset on the mount is under a kind root,
  `/Game/ElysiumGenerated`, or a listed legacy root.
- `build_content.py`'s `GENERATORS` becomes the claim list for `/Game/ElysiumGenerated`, and
  `build content` reports and deletes any package no listed generator claims.
- Acceptance: the resolver fixture green in both languages; the registry test green; `foreign` and
  `unstamped` both zero on a full `import models`; `ModelParity` green over the full corpus; the
  three test maps' shots unchanged against the R2.1 baseline; pytest green; doctor clean.

→ lands: one naming standard on the whole mount, and a prune that cannot eat another lane.

### R8.0b — R8 preflight: rulings, corrections, live defects, the frozen baseline

- Rulings D2–D12 written into their seam docs (the three material blocks from
  `E_materials_eyes.md` §8; the `## Import — skeletal` section of `seam_map_model.md`; the
  `animation-architecture.md`, `physics-architecture.md`, `wielded-weapon-integration.md` edits;
  the model list on the transport settings page).
- Corrections §2 applied; `seam_migration.md` R8 re-cut to this note.
- F1, F2, F3, F4, F5, F10 fixed; the corpus index re-run; the static-equivalence census re-run
  after F1 and the flipped set recorded.
- The legacy export root's `npc/` + `items/` trees frozen as the R8.1 `--legacy-root`.
- T-A5 marked DONE; T-C8 measured offline (p90 recorded; the 60 Hz decision taken before R8.2).
- Acceptance: doctor clean, pytest green, every `Elysium.Content.*` and `Elysium.Substrate.*`
  reading unchanged, the shots unchanged, the corpus index re-run clean, the frozen export on
  disk.

→ lands: the design on record; the baseline cannot move under the rebuild.

### R8.1 — Producer parity: the stage re-emits the legacy product set, byte-equal or named

- `validation/skeletal_diff.py`: reads a staged payload and a legacy `.eskm` per section, and
  every sidecar family against the frozen tree; byte-equal, or a named divergence with a float
  classifier (float32 round-trip through the glTF basis is the expected class); reports row
  **order**, not just sets (the include-tree order decides which variant plays).
- `importers/characters.py` + a `skeletal_stage/` package porting the rules (§5), writing the
  legacy products at the legacy paths (the R3.2 shape: the game and the bake are untouched):
  `.eskm`, `banks/`, `placed_models/`, the six sidecars, `npc_index.json`, `npc_manifest.json`,
  `families.json`, `wield_models.json`, `ground_models.json`; and, in parallel, the staged
  `import/characters/` tree of §5.
- Named divergences expected: `eyes/` (V2 eyeballs are source-space with material indices —
  semantically equal only), `DYNM` (dropped), `MATL.albedo` (the texture lane's `T_` replaces the
  private PNG), the float class, the sequence dedup report.
- F7 rule (a body that is a cinematic root writes its clips once) and F11 measurement; the
  `hustler_*_ref` duplicate and the 97 duplicated PNGs disappear with the private texture closure.
- Whole-cast run; then the legacy `npc_export.py`, `UE_mdl_skeletal.py`, `UE_mdl_cloth.py`,
  `mdl_gltf.py`, `UE_extract_wield.py`, `UE_extract_items.py` and their dead tests are deleted;
  the ported facts tests (`test_mdl_*`, `test_skeletal_refpose`, `test_animated_props`,
  `test_wield_corpus`, `test_character_partition`) are re-pointed.
- Acceptance: the differ reports zero unnamed differences over 293 + 372 + 2,899 + 67 units;
  `BakedCharacterParity`, `OracleIdentity`, `RigRetarget`, `RigPose`, `RigLayers`,
  `UpperBodyLayerArming`, `FanDuration` unchanged; pytest green.

→ lands: one producer for the cast; the export contract is the model unit.

### R8.2 — Skeletal bake off the unit: the `import characters` lane, bodies on V2 materials

- Masters first: `make_v2_materials.py` gains the skinned pair, the `M_V2_Eyes` completion, the
  `M_V2_Unlit` and `M_V2_Refract` edits; the material import routes by referencing shape and
  writes the 287 `_Skinned` twins; the eye instances carry `Vampire` from `$vampire`. Verify the
  `two_sided` change on a hair-heavy body and the eye fade at the first-person endpoint (F6).
- The lane: manifest, provenance sidecars, staged payloads, editor phase on the existing builders
  with slots bound to `MI_`, `UElysiumCharacterProvenance` attached, recipe stamps, manifest-driven
  prune (neither legacy bake prunes), the memory drain, T-B3's guard (every profile scale ∈ {0,1},
  owned set == mask row, the mirror covered). Cloth re-pointed (D8) and built in the same lane
  after the mesh; `DYN_<stem>` written. Ming Xiao's sections. The seven prop bones. The player
  bodies cut over first (D5), then the test-map casts, then all.
- The asset differ `verify characters --legacy-mount` (D11), the crossfade-residual instrument,
  `Elysium.Content.CharacterParity` reading the manifest as ground truth (the `ModelParity`
  template), a Python↔C++ twin test for the body stem.
- Retire: `M_PlayerBody`, `M_Eyes`, `Characters/Materials` (2,526 `MI_SK_*`),
  `Characters/Textures` (834), `npc/tex` and `npc/textures.json`, `bake_characters.py` body and
  bank paths, `CharacterTracker` (its cross-unit digest structure is kept in the recipe: a body's
  recipe names its bank skeletons' tree digests), `seam_map_character.md`,
  `seam_map_animation_bank.md`.
- Acceptance: O1–O5 of the asset differ — every legacy asset maps to exactly one `Models/…`
  counterpart through the resolver and `DA_Cast` and vice versa (0 unmapped either way), then
  ≤ 1e-4 / ≤ 0.05 / ≤ 1e-3 / identical; every `Elysium.Content.*` reading unchanged (D11);
  cloth counters equal to the frozen log; a full-cast forced bake's peak memory and time
  recorded; the three test maps' shots unchanged against the R2.1 baseline.

→ lands: bodies on V2, one material authority, provenance on every character asset.

### R8.3 — Wield off the unit

- Stage: weapon units + the vdata item unit's resolved model fields; `wield_corpus` over
  GLB-decoded bones; the re-skin in the build; `DA_WieldModels` with `AssetId` and the
  standard path; `import wield`.
- Runtime: `AssetId` on the ref struct; the ground-model gate replaced by `BakedModel(id)`
  package-exists (~150 lines of catalogue plumbing deleted); `ItemGroundModels()`, `WieldDir()`,
  `WieldManifest()`, `WieldSource()` retired.
- Retire: `bake_wield.py`, `make_wield_materials.py`, the four `M_Wield_*`, `/ElysiumBaked/
  Items/Wield/**` (335), `items/**`; keep and re-point the census and ref-pose assertions.
- Acceptance: `gr_wield_check` mapping/tracking/placement on animated male and female bases;
  `WieldBinding`, `WieldTrailSockets` green; the 20 `TrailTip` sockets present; the fire-axe
  second skin family in the skins table.

→ lands: items on V2.

### R8.4 — Animated props

- Skeletal props through the lane with slots bound at bake; `BindMapMaterials` deleted,
  `ApplyAnimatedPropSkin` reduced, `ConfigureRest` narrowed; the 24 prop procedural tables on
  their `SK_`; `DA_PlacedModels` keyed by id replaces the index's `placed_models` and the
  2,899-row linear scan; `bake_map_v2.py` reads the table.
- 8.4a re-verified in game (§2.9) and the roadmap corrected.
- Acceptance: the 43 rest-pose placements on the three test maps identical (position, material,
  skin) to R5.1's record; `OpeningAnimatedProps`, `PropSolidCatalogue` green; shots unchanged;
  `FanDuration` extended to the props path and green over the six placed-model fans the cast run
  cannot reach.

→ lands: one material authority for a placed model.

### R8.5 — Cooked: no loose read under `npc/`

- Cheapest first: the iris read (`FElysiumTextureCache`, `LoadDDS`, `SolidTex` deleted — one
  file), the two debug probes (asset-registry queries), `ground_models.json` (already R8.3).
- Facial / eyes / procedural readers → the mesh user data (one bake pass, one reader deletion
  each); the runtime morph-curve registration deleted once O2 proves the bake writes it.
- The blend-space writer **inherits** T-B2's landed legacy sample-length flag and adds its
  runtime half (per-cell motion vectors on the cell sequences; the mover blends displacement
  vectors rather than lerping per-cell speeds); `ResolveGridClip` asks the loaded `UBlendSpace`
  after a one-off equivalence check against the 84 shipped tables.
- Clips + index → `DA_<base>` + per-clip metadata + `DA_CinematicSets` + `DA_Cast`; the
  resolver collapse of D7; `PreloadMapAnimations` becomes a bundle load; F13 collapsed.
- Expression tables lane; F8 fixed; `mouthshader` bound through `Create(MI_)` and recorded.
- **The retail comparators are severed from the export tree.** `retail_compositor.Corpus`,
  `graph_identity` and `compose_diff` derive owner attribution, sequence numbering and clip
  track-bone sets from the **install** (`formats.install.build_index`,
  `mdl_skel.first_reference_bases` / `local_sequence_labels`, `find_anim`/`read_anim` track
  headers) instead of from `npc/`; `--export-root` drops from `debug oracle`. They are **not**
  re-pointed at `<key>.body.json`: an oracle must share no input with the producer it scores.
  Otherwise D11's "the numbers did not move" discipline expires one phase after it is adopted,
  because R8.5 deletes the tree all three read.
- Retire: the nine readers, `FElysiumNpcIndex`, `FElysiumNpcClipSet::Load`,
  `FElysiumBlendTable::Load`, the `npc/` accessors in `ElysiumContentPaths.h`, the `npc/` and
  `items/` export trees and their `.elysium-incomplete` domains, `export characters`,
  `character_sweep.py` (prune is manifest-driven).
- Acceptance: zero reads under `npc/` or `items/` from **either** side — the C++ `Root()`
  accessors and Python's `paths.export_root()` (grep + a policy test); `debug oracle --emit`,
  `debug oracle --run` and `debug compose` re-emit and re-score inside the pinned bands
  (0.009 cm; 2.336 / 1.663 cm) and `OracleIdentity` re-runs against the re-emitted `_oracle/`;
  every `Elysium.Content.*` reading unchanged, `FanDuration` included; no
  synchronous file read inside a tick; `uv run elysium export all` completes on a clean export
  root; a hub-chain playthrough with dialogue.

→ lands: the cast reads cooked content and the corpus only.

### PHYS1 — deferred simulation work, not an R8 dependency

Consumes the physics data that R8 exports, imports and verifies. Simulation-ready PhysicsAsset
construction, solver calibration, ragdoll activation and gameplay physics have separate future
acceptance. Their absence does not prevent R8 completion or deletion of superseded transports
once their data-preserving replacements pass the applicable migration checks.

## 7. Risks, ranked

1. **The bank remap goes silently empty.** It is derived from two bind poses on baked assets and
   needs one registered retarget source per bank, `RetargetSource` on every bank sequence, and
   translation retargeting on `Animation`; drop any one and every correction reads as "nothing
   to correct" with no log. Mitigation: O2 compares `AnimRetargetSources` and the retargeting
   mode; `RigRetarget` pinned.
2. **The profile mirror is lost in the rewrite** (`DeclareCompatibleSkeletons` mirrors bank masks
   onto the body skeleton; an unset mask is a null profile that weights every bone zero, and the
   layer poses nothing). Mitigation: O2 compares the profile set; T-B3 covers the mirror;
   `UpperBodyLayerArming` pinned.
3. **The cloth render-map re-key** (D8): wrong and the driven/skinned split lands on the wrong
   vertices, invisibly. Mitigation: the three build counters against the frozen log.
4. **The seven prop-bone exemption** through the build rewrite (123.5° / 7.5 cm measured without
   it). Mitigation: the verifier's wield-mount channel check kept; `gr_wield_check`.
5. **A slice-scoped partition** renames a bank family for the whole cast. Mitigation: refused in
   code; the manifest carries the corpus fingerprint.
6. **Authored keys are stems** (`DA_ClothTuning.Garments`, `DA_HairDynamics.Stems`); a rename
   misses silently (hair by design). Mitigation: D1; a content test that every
   `DA_ClothTuning.Garments` key resolves to a built asset.
7. **Memory.** No character-side measurement exists; the map bake died at 25 GB before its drain.
   Mitigation: the drain adopted; a measured full-cast run before R8.2 lands; JSON-chunk-only
   reads.
8. **`RigCompose` moves.** T-C7's residual is red; a rebuild that changes it either way hides a
   rule change. Mitigation: pinned as an equality (D11).
9. **A missing `AnimRetargetSources`/curve carry across a skeleton rebuild** unbinds every face
   silently. Mitigation: `CarriedCurves` kept; O2/O5.
10. **`two_sided` from `$nocull`** culls open character geometry that relied on the blanket flag.
    Mitigation: verified on hair/fur bodies before the ruling is signed; the fallback is a
    per-instance override on the affected units, not a return to the blanket.
11. **The player-body skin/two-producer window**: during per-body cutover a bank family is shared
    by legacy and V2 bodies. Mitigation: banks flip wholesale in R8.1 on a byte-equal payload, so
    the window never exists for shared assets.
12. **Dense morph accessors** (every target on every primitive) make the stage read 3.5 GB where
    it read 793 MB. Mitigation: lazy accessor reads; sparse accessors are the exporter fix if it
    bites, recorded not taken.
13. **The stem → id mapping is wrong for one body** and an oracle silently reads the wrong
    asset. Mitigation: `DA_Cast` is written by the stage from the same manifest the assets are
    built from, and a content test asserts every capture stem, every `_oracle/*.json` stem and
    every parity-slice stem resolves to exactly one id.
14. **Path length.** The deepest character folder plus the longest derived label is ~225
    characters from the repo root. Mitigation: the stage refuses > 240; `LongPathsEnabled` is
    the escape hatch; the fixture carries the longest real path.
15. **The R8.0a re-bake moves every map at once.** Mitigation: it is a root move under one
    accessor with the per-map cutover flags untouched; acceptance is the R2.1 shot baseline and
    the R4.6 parity tests, not a look.
16. **A lane's prune eats another lane's assets** — four producers now share `Models/`, and every
    existing prune deletes its kind root minus its own manifest. Mitigation: the producer stamp
    and the `foreign`/`unstamped` counts land in R8.0a, before any second producer writes.
17. **A generator resurrects a retired master.** `build content` rebuilds whatever its
    `GENERATORS` list names, so a master "retired" in R8.2 returns on the next run — and
    `make_player_body_material.py` is the only file in the repo that sets `used_with_clothing`,
    the flag D6's whole ruling turns on. Mitigation: a generator retires in the same task as its
    assets, and the claim list is the criterion.

## 8. Owner calls and closed questions

**Owner calls (default in bold; the plan executes the default):**

| # | Call | Default |
|---|---|---|
| OC1 | Two more masters (`M_V2_LitSkinned` pair) vs per-instance usage overrides | **the pair** (D6) — the fallback leaves the fade a hard clip |
| OC2 | `two_sided` faithful from `$nocull` vs the legacy blanket | **faithful**, after a hair-body check |
| OC3 | `@host` fan-out kept vs composed in the graph | **kept in R8**; modernization recorded for later |
| OC4 | Sequence dedup (legacy) vs positional walk | **legacy for parity**; reachability measured; revisit only if a dropped descriptor is reachable |
| OC5 | Cinematic per-actor slices kept vs four skeletons from one unit | **kept**; later call |
| OC6 | Hair cooked for all 102 with the authored gate vs authored-only | **cooked, gated** (LIFE9 owns widening) |
| OC7 | Physics scope in R8 | **export/import and data conservation only** (owner ruling 2026-09-05); simulation-ready assets, calibration, ragdoll activation and gameplay physics deferred, never an R8 blocker |
| OC8 | Expression tables: 250 assets + registry vs one asset | **250 + registry** |
| OC9 | `mouthshader`: implement the per-slot flag vs bind and record | **bind and record** |
| OC10 | 8.4a row: delete vs narrow to the dead branch | **verify in game, then delete** |
| OC11 | LOD import for skinned meshes | **not in R8**; follow-up with the morph caveat |
| OC13 | `NS_<root>` (1,698 per-unit particle systems): `Particles/<dir>/NS_<base>` under the standard, or `Particles` drops out of the contract's kind list | **the standard** — it is a per-unit product of a VtMB unit, so the criterion in D1 already decides it; the alternative is a stated exception |
| OC14 | Transformation bodies (`animalism_bat`, `animalism_raven`, `batswarm`, `mingxiao_transformation`, `creation1_*`, `hengeyokai`): build in R8 or sit on the `codeReferenced:` list marked with the task that needs them | **build them** — they are player bodies the camera fades, and R8.2's `M_V2_Unlit` `ModelAlpha` edit exists for exactly these 12 units |
| OC15 | Character and wield skin families (D10's scope add): in R8, or a follow-up | **in R8** — the fix after R8 costs a re-stage of the whole cast, and 83 temple guards draw the wrong body until it lands |
| OC12 | Ragdoll interaction anchor (death origin vs pelvis) — PHYS1's | default retail (origin); not R8's |

**Closed by ruling:** the naming pattern — one standard for every kind, the mount mirrors
`exports_v2`, ids are the only address, stems resolve through `DA_Cast`; the landed lanes that
drift move in R8.0a (D1).

**Closed by measurement (no call needed):** does the unit carry the clips columns — yes, all of
them, richer; is the include order preserved — yes, twice (`includeIndex` and `dependencies`);
is the mask binary — yes, corpus-wide; can `@host` be derived — yes, exactly; do the `MI_` and
`T_` exist — 1,540/1,540 and 1,231/1,231; is `$halflambert` a question — no; are the obfuscate
chains characters' — no; is the bank partition an include-graph question — no; does the save
carry a stem — only `Swing.ClipOwnerStem`, and the two live derivations are single choke points;
does a per-body cutover need a runtime flag — no; skeletal or static for wield — skeletal; keep
`DA_WieldModels` — yes; viewmodels in R8.3 — no; `ground_models.json` successor — package-exists;
the character byte ledger — closed by the model seam; does anything need the `RAGD` chunk — no;
where does `phonemeFilter` live — the header; TXT or VFE — VFE; `.lip` — corpus text, R9.1;
one re-bake or two — one (T-C8 measured first, T-B3 in the same bake; T-B2's bake half is already
in); the source capsule for
models — no (1.5 GiB for a lane that never re-reads the source).

## 9. Retirements ledger

| Retires | In |
|---|---|
| `asset_names.texture_asset_name` and `baked_asset_name` (after the collision check), `FElysiumContentPaths::PropModelStem`, `BakedPropMesh`, `BakedItemMesh`, `BakedPropSkins`, `BakedMeshes*`, every `BakedCharacter*`/`BakedBank*`/`BakedProp*`/`BakedWield*` stem accessor, `model_names.json` (replaced by the resolver fixture); the mount folders `Meshes/`, `Sprites/`, `Sky/`, `Lookdev/` and the 117 per-map root folders | R8.0a |
| `shared_corpus.static_stem` / `mesh_asset` / `texture_asset` — the legacy map bake calls all three, so they die with it | R9.2 |
| `make_player_body_material.py`, `make_eye_material.py` (deleted from `build_content.py`'s `GENERATORS` in the same task as the masters they author, or the next `build content` rebuilds them) | R8.2 |
| `make_wield_materials.py` likewise | R8.3 |
| `exporters/npc_export.py` (1,404), `UE_mdl_skeletal.py` (1,711), `UE_mdl_cloth.py`, `formats/mdl_gltf.py` (893), `UE_extract_wield.py` (451), `UE_extract_items.py` (151); `test_eskm_sections`, `test_character_sources`, `test_wield_export` | R8.1 |
| `npc/` and `items/` as export products (3.6 GB + `npc_manifest.json` 60.7 MB + `clips/` 143.8 MB) | R8.5 (paths), R8.1 (producer) |
| `bake_characters.py` (922), `character_recipes.py` (structure kept in the recipe), `bake_verify_characters.py`'s container reads (checks kept), `M_PlayerBody`, `M_Eyes`, `Characters/Materials` 2,526, `Characters/Textures` 834, `seam_map_character.md`, `seam_map_animation_bank.md` | R8.2 |
| `bake_wield.py` (980), `make_wield_materials.py` (223), `M_Wield_*` ×4, `/ElysiumBaked/Items/Wield/**` 335, `test_bake_wield_reuse`, `test_item_models` (stem test kept) | R8.3 |
| `BindMapMaterials`, the static-twin material copy, the `placed_models` linear scan | R8.4 |
| The nine readers, `FElysiumTextureCache` (~170 lines), `FElysiumSkeletalSource::Load(.eskm)`, the `npc/`/`items/` accessors, `export characters`, `export wield`, `character_sweep.py`, `DYNM`/`BDYN` | R8.5 |

## 10. Doc debt cleared by R8

`seam_map_unit_contract.md` → "Baked assets" (written 2026-09-04); `seam_map_model.md`
(§2.4–2.6, "## Import" rewritten onto the standard, the `## Import — skeletal` section, Ming
Xiao, `boneRemap` note, the wield join); `seam_map_map.md` (the `Maps/` root, the model list on
the cutover page); `seam_map_material.md` (three blocks; the sprite twin; the TV-unit
correction);
`animation-architecture.md` (§2 three sentences; §2.1 and §6 the cooked homes; §8 the `@host`
candidate); `physics-architecture.md` (L0 deleted); `wielded-weapon-integration.md` (the
re-skin's new home; the roadmap's "wield bake re-skins" wording); `plans/gameplay.md` PHYS1;
`plans/animation.md` LIFE9 gate, LIFE6 note; `plans/characters-ui.md` 8.4a; `roadmap.md` stale
figures and the R8 rows; `mdl-coverage-and-gaps.md` (ledger row closed, `hit_yaw` corrected);
`procedural_bones.md` vs `animation-architecture.md` on the partial-update mask (the architecture
wording wins); `seam_migration.md` R8 (this note; 485; the obfuscate line).
