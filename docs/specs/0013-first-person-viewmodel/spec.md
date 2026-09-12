# 0013 first-person-viewmodel — the hands and the weapon in first person

## Witness
On `sp_tutorial_1`'s shooting range the first-person view draws the clan's hands and the packed
weapon: draw, idle, fidget, fire, dry-fire, the ordinary reload, the switch-driven `lower` and
the M37's begin / per-shell / complete phases play on both components for every accepted firearm
family; a Tremere shield script swaps the hands; melee in retained first person renders no hands
and no weapon. Presentation only: nothing here spends ammunition, creates a projectile or
originates a VtMB event.

## Scope
The corpus (pipeline) and the body (runtime) of the first-person viewmodel, ranged only (owner
call, made): lockpick and Discipline viewmodels ride the same machinery and are deferred, not
designed out. Owned elsewhere and consumed here: the firearms transaction — **0008**; the
visibility decision — **0012** (1); the catalog (LIFE2) and the event carrier (LIFE5) — **0005**.

## Sources
- Oracle: `docs/vtmb/animation_and_movers.md` (the viewmodel selection and placement),
  `docs/vtmb/camera-view-modes.md` (the projection), `docs/vtmb/wielded_weapons.md`.
- Authored data, V2 seams under `$ELYSIUM_WORK_ROOT/exports_v2/`: `models/hands/**` (21 models),
  the 17 packed weapon viewmodels, `vdata/items/*` (`viewmodel`, `anim_prefix`, `camera_class`,
  `reload_single`, `shows_view_model`, `hides_hands_model`).

## Witness data
- A separate body, not a camera mode; two halves landing in order, the corpus then the body.
- **Corpus.** One deterministic, patch-first manifest: exactly 21 models under `models/hands/**`
  (active + repeated `M_Hands` / `F_Hands` clandoc values, plus the script-only male/female
  Tremere `_shield` swaps — patch-first restorations, retail wires only Nosferatu and the shared
  pair) and exactly 17 packed weapon viewmodels (12 accepted firearm geometries + grenade,
  lockpick reference, three Discipline models). Each row keeps source key, normalized stem,
  role, skeleton signature, attachments, sequences/events and the `viewmodel` / `anim_prefix` /
  `camera_class` / `reload_single` / `shows_view_model` / `hides_hands_model` joins — the last two
  suppress the hands component outright; a row that omits them bakes a phantom. Package layout:
  `/ElysiumBaked/Characters/Viewmodels/Hands/<stem>/SK_<stem>` and
  `.../Viewmodels/Weapons/<stem>/SK_<stem>`, case-folded sorted iteration. Missing authored
  references (`v_gangrel_fem_hands.mdl`, a multiplayer row's dangling reference) are reported,
  not repaired.
- **Body.** Hands and weapon are two skeletal components, not merged, no socket attach, each
  consuming one semantic intent, resolving its family sequence, evaluating independently in
  camera-root space with authored `Camera01` as bone 0 and bind frames as the alignment
  contract. Projection is independent of player FOV: `t = tan(viewmodel_fov·π/360)`, scales `1/t`
  and `aspect/t`, clip range 1..28400, 4:3 or 16:9 under the widescreen/anamorphic setting; a
  hidden view suppresses both submissions without destroying components or resetting sequence
  state. Selection: one activity per request, translated per weapon through its `{source,
  target}` table into the family the hands bank holds; hands on slot 1, the packed weapon on its
  own slot, one shared playback rate; the idle think owns fidget and the `_EMPTY` form;
  `ACT_VM_LOWER` marks the transition across which the hand offset is held. Placement: turn lag,
  the `-0.1` forward pull, the `0.25` blend, the stair-smoothing term shared by the view and both
  components, the slot-1-only basis offset; idle drift and the `viewmodel_fov` / `scr_ofs*` terms
  are no-ops at their defaults. Server sequence events are timing carriers into weapon-mode
  dispatch; client viewmodel events own muzzle flash and shells only. Melee has no first-person
  model, measured: nothing is synthesized.
- RE42 opens: the alternate weapon-attachment placement source, the blend interface identity,
  `THAUMATURGY`'s and `ENFIELD`'s owning class, what retail does with a missing hands model, the
  fourth live `viewmodel` entity. The ELGVM1 harness exists; 12 of its 13 scenarios have never
  run and the one that did finished partial on a timeout — no viewmodel claim is
  capture-verified.

## Stories
In build order. A story is done when every behaviour it lists is in the substrate and its
recovery is written in the oracle section it names. Numbers are stable ids cited by other
documents; a split keeps the number and adds a letter. Each open story carries the retail
contract the code must match, the job, what it consumes or provides, and a size (XS–XL) with the
model / effort tier recommended for it.

- [ ] **1. The corpus** (was LIFE6).
  Job: the export/bake of the 21 hands models and 17 packed weapon viewmodels, the manifest
  with its provenance joins, the package layout, the verification (mesh/skeleton/sequence
  counts, the 21/2/17/12 census, duplicate rejection). Outputs game-derived and gitignored.
  Size: M. Effort: Sonnet / high.
- [ ] **2. Two components in camera-root space.**
  Job: hands and weapon as two skeletal components, `Camera01` as bone 0, bind-frame alignment.
  Size: M. Effort: Opus / medium.
- [ ] **3. Projection and visibility.**
  Job: the `tan(viewmodel_fov·π/360)` projection, the FOV/aspect policy, the clip range; the
  visibility suppression wired to 0012/1.
  Consumes: 0012/1.
  Size: S. Effort: Sonnet / high.
- [ ] **4. Selection.**
  Job: activity → per-weapon `{source, target}` table → hands-bank family; slot 1 hands,
  own-slot weapon, one rate; idle/fidget/`_EMPTY`; `ACT_VM_LOWER`'s held offset.
  Size: M. Effort: Opus / high.
- [ ] **5. Placement.**
  Job: turn lag, the `-0.1` pull, the `0.25` blend, stair-smoothing, the slot-1-only basis
  offset; the default no-ops reproduced as no-ops.
  Size: M. Effort: Sonnet / high.
- [ ] **6. Events.**
  Job: server sequence events as timing carriers into weapon-mode dispatch; client viewmodel
  events for muzzle flash and shells only.
  Consumes: 0005's event carrier.
  Size: S. Effort: Sonnet / high.
- [ ] **7. The Tremere shield swap and the empty melee view.**
  Job: the `_shield` hands-model swap from the script; no hands and no weapon under retained
  first person on melee.
  Size: S. Effort: Sonnet / medium.

## Seams
- Provides: the first-person body and corpus to any later spec drawing a weapon in first person.
- Consumes: 0008's transaction; 0012/1's visibility; 0005's catalog and event carrier.
- Open recoveries: the RE42 items above.
