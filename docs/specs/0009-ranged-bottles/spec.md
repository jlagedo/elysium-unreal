# 0009 ranged-bottles — pick up the gun and shoot the beer bottles: firearms, the first-person viewmodel, pickup, breakables

## Witness
`sp_tutorial_1`'s shooting-range beat: the player picks the weapon up off the ground (9.8's loose
pickup), the first-person viewmodel draws hands + weapon (LIFE6), the player fires (13.3's firearms
half — damage spine, weapon controller, spread cone), and a hit registers on a bottle backed by
`func_breakable`'s damage surface (PHYS3's breakable slice), which breaks as retail resolves it. No
automation test name is given for the composed beat in the plan; each absorbed row states its own
acceptance (below) and the beat is otherwise owner-piloted Play-tier, per `docs/vtmb/*.md` retail
facts cited under Requirements.

## Scope
- Roadmap rows absorbed: **13.3 Firearms & melee basics** — firearms half only, melee is spec 0006;
  **LIFE6 The first-person viewmodel**; **9.8 Inventory & items** — loose pickup only; **PHYS3 The
  rest of the physics world** — breakable props only.
- Out of scope (belongs to another spec or is parked):
  - 13.3's melee contact instant and its animation-event dependency — spec 0006.
  - LIFE6's lockpick and Discipline viewmodels — same machinery, deferred by owner call, not this
    spec.
  - 9.8's remainder: player drop, `StartBarter` + buy/sell UI (9.10), `trigger_inventory_check`,
    the `TravelsWithPlayer()` absent-set half, the plain `item_container` lid mover, the tutorial
    lockpick attempt HUD — unassigned here, stays parked under 9.8.
  - PHYS3's non-breakable parts — `func_physbox` as the brush twin for the `phys_*` constraint
    family, `env_physimpact`/`env_physexplosion`, `prop_ragdoll` — spec 0008.
  - PHYS1 (ragdoll rig) — spec 0006. PHYS2 (physics hands) — spec 0008.
  - 13.2 Disciplines, 13.5 Combat AI, 13.4 Terminals, 9.9 NPC disposition, 9.10 Economy — separate
    rows, not assigned here.

## Requirements

### Firearms (13.3)
1. The damage spine, one typed health commit, and the weapon controller's two-half attack
   transaction are built; this spec ports the remaining open joins. → `docs/vtmb/combat-and-damage.md`
   (RE40: open numeric joins on 13.3).
2. The **word-15 commit**: accumulated onto the damage descriptor, never multiplied in — an open
   join, not yet ported.
3. The ranged **spread cone's interpolation input** between `SpreadAngle` and `SpreadAngleMax` — an
   open join; the plan does not state the interpolation function.
4. `SkillRequirement`'s consumer — an open join, unspecified beyond "open" in the plan.
5. Melee contact instant is explicitly out of scope here (spec 0006's).
*Acceptance:* the tutorial's range lesson completes as retail (the melee lesson is 0006's).
*Deps:* 9.8, 11.10.

### First-person viewmodel (LIFE6)
6. A separate body, not a camera mode. Two halves, landing in order: the corpus (pipeline), then the
   body (runtime).
7. **Corpus:** one deterministic, patch-first manifest for both first-person roles — exactly 21
   models under `models/hands/**` (active + repeated `M_Hands`/`F_Hands` clandoc values, plus the
   script-only male/female Tremere `_shield` swaps — both patch-first restorations, retail wires
   only Nosferatu and the shared pair, `docs/vtmb/animation_and_movers.md`) and exactly 17 packed
   weapon viewmodels (12 accepted firearm geometries + grenade, lockpick-reference, three
   Discipline models). Each row keeps source key, normalized stem, role, skeleton signature,
   attachments, sequences/events, and the `viewmodel`/`anim_prefix`/`camera_class`/
   `reload_single`/`shows_view_model`/`hides_hands_model` joins as provenance — the last two
   suppress the hands component outright; a row that omits them bakes a phantom. Package layout is
   a contract: `/ElysiumBaked/Characters/Viewmodels/Hands/<stem>/SK_<stem>` and
   `.../Viewmodels/Weapons/<stem>/SK_<stem>`, case-folded sorted iteration for reproducible output.
   Verification: resolve every manifest package, check mesh/skeleton/sequence counts and events
   against the container, reject duplicate paths, assert the 21/2/17/12 census. Missing authored
   references (e.g. `v_gangrel_fem_hands.mdl`, a multiplayer row's dangling reference) are reported,
   not repaired. Outputs stay game-derived and gitignored.
8. **Body:** hands and weapon render as **two skeletal components** — not merged, no socket attach —
   each consuming one semantic animation intent, resolving its matching family sequence,
   evaluating independently in the same camera-root space with authored `Camera01` as bone 0 and
   bind frames as the alignment contract.
9. Viewmodel projection is independent of player FOV: `t = tan(viewmodel_fov·π/360)`, scales `1/t`
   and `aspect/t`, clip range 1..28400 in the recovered space, 4:3 or 16:9 under the
   widescreen/anamorphic setting. 11.13d's view projection owns the local-body/viewmodel visibility
   decision; a hidden view suppresses both submissions without destroying components or resetting
   sequence state.
10. Selection contract, recovered whole, consumed not re-derived: one activity per request,
    translated per weapon through its own `{source, target}` table into the family the hands bank
    holds; applied to the hands on slot 1 and the packed weapon on its own slot with one shared
    playback rate; the idle think owns fidget and the `_EMPTY` form; `ACT_VM_LOWER` marks the
    transition across which the hand offset is held rather than resampled.
11. Placement is a second transaction on top of projection: turn lag, the `-0.1` forward pull, the
    `0.25` blend, the stair-smoothing term shared by the view and both components, and the
    slot-1-only basis offset — with idle drift and the `viewmodel_fov`/`scr_ofs*` terms reproduced
    as the no-ops they are at their defaults. → `docs/vtmb/animation_and_movers.md`,
    `docs/vtmb/camera-view-modes.md`.
12. Server sequence events are timing carriers into weapon-mode dispatch — weapon logic commits the
    shot/reload transaction; client viewmodel events own muzzle flash and shell presentation only.
13. Tremere shield scripts swap the hands-role model to the baked `_shield` variant.
14. **Melee has no first-person model, measured:** when retained first person suppresses retail's
    melee/`force_3rd` camera move, the presenter renders no hands and no weapon; nothing is
    synthesized.
15. **Scope — owner call, made: ranged only.** Lockpick and Discipline viewmodels ride the same
    machinery, deferred rather than designed out.
*Acceptance (presentation-only):* test intents drive draw, idle, fidget, fire, dry-fire, ordinary
reload, the switch-driven `lower`, and the M37 begin/per-shell/complete phases on both components for
every accepted firearm family — clan/shield selection, matching sequence/cycle, `Camera01`/
right-hand alignment, both FOV/aspect policies, lag and stair-smoothing placement, visibility
suppression/resume, and the empty melee view — without spending ammunition, creating a projectile,
or originating a VtMB event. Per RE42: the ELGVM1 harness is built but 12 of its 13 scenarios have
never run, and the one that did finished partial on a timeout — **no viewmodel claim is
capture-verified yet**.
*Deps:* LIFE2 (catalog), LIFE5 (event carrier for real fire timing), 11.13d.

### Loose pickup (9.8)
16. Ownership, loose pickup, the explicit loot-container session, and the CommonUI transfer panel
    form the loot core and are landed — this spec's witness needs only the loose-pickup path, for
    the weapon lying in the shooting range. → `docs/vtmb/inventory.md`,
. RE38 (inventory ownership and transfer) is
    closed.
*Deps:* 9.4.

### Breakables (PHYS3, breakable slice only)
17. `func_physbox` (179 placements, 17 maps) is `FElysiumPhysProp`'s brush twin: the entity's own
    `.ents` convex `hulls` as the body, `func_breakable`'s damage surface — the mechanism a shot
    bottle resolves through. →,
    `docs/vtmb/phy_vphysics.md`, `docs/vtmb/physics-interaction.md`.
18. The plan states no further detail for `func_breakable` beyond the damage-surface line above —
    break threshold, debris presentation and sound are unspecified here.
*Deps:* PHYS2's seam (spec 0008); this spec does not consume PHYS2's carry/throw session, only the
breakable damage surface on the physics leaf.

## Design
The port's shape as the plan already decided it:
- LIFE6's two skeletal components (hands, weapon) evaluate independently in camera-root space; no
  merge, no socket attach. Selection and placement are two separate transactions layered on
  projection.
- 9.8's loose pickup already resolves to inventory ownership; the weapon becomes a `viewmodel`-
  bearing item the moment it is picked up.
- 13.3's weapon controller commits damage through the existing damage spine; the open joins here are
  additive fixes on top of that controller, not a new one.
- PHYS3's breakable slice is `FElysiumPhysProp` with a `func_breakable` damage surface bound to the
  `func_physbox` brush body — the same leaf PHYS2's carry session targets, but reached here only
  through damage, not through the physics-hands grab.

## Seams
- Consumes: 9.8's landed loose-pickup/ownership machinery (the weapon in the world); LIFE2 (catalog
  — spec unknown) and LIFE5 (event carrier for real fire timing — spec unknown) for LIFE6; 11.13d
  (view projection — spec unknown) for viewmodel visibility; 11.10 (Play test tier, spec 0000) for
  acceptance; PHYS2's impulse/eligibility groundwork (spec 0008) is a sibling, not a dependency —
  breakables resolve through `func_breakable`'s own damage surface, not the carry session.
- Provides: the firearms open joins closed (spread-cone interpolation, word-15 commit,
  `SkillRequirement` consumer) for 13.5 (combat AI) and any later ranged-combat consumer; the
  first-person viewmodel body and corpus (hands + weapon, ranged only) for any later spec drawing a
  weapon in first person; `func_breakable`'s damage surface as `FElysiumPhysProp`'s breakable twin,
  for any prop scripted to shatter under damage.

## Tasks
- [~] 13.3: damage spine, one typed health commit, weapon controller two-half attack transaction —
  landed.
- [ ] 13.3: step-9 template filters and the word-15 commit (accumulate, not multiply).
- [ ] 13.3: ranged spread cone's interpolation input (`SpreadAngle` ↔ `SpreadAngleMax`).
- [ ] 13.3: `SkillRequirement`'s consumer.
- [ ] LIFE6: corpus export/bake — 21 hands models, 17 packed weapon viewmodels, manifest,
  provenance joins, package layout contract, verification (mesh/skeleton/sequence counts, census,
  duplicate rejection).
- [ ] LIFE6: two independent skeletal components (hands, weapon), camera-root space, `Camera01`
  bone-0 alignment.
- [ ] LIFE6: viewmodel projection (`t = tan(viewmodel_fov·π/360)`, FOV/aspect policy, clip range),
  visibility suppression wired to 11.13d.
- [ ] LIFE6: selection contract — activity → per-weapon `{source,target}` table → hands-bank family;
  slot 1 hands, own-slot weapon, shared playback rate; idle/fidget/`_EMPTY`; `ACT_VM_LOWER` held
  offset.
- [ ] LIFE6: placement transaction — turn lag, `-0.1` forward pull, `0.25` blend, stair-smoothing,
  slot-1-only basis offset; idle drift and `viewmodel_fov`/`scr_ofs*` as no-ops at defaults.
- [ ] LIFE6: server sequence events as timing carriers into weapon-mode dispatch; client viewmodel
  events for muzzle flash/shell presentation only.
- [ ] LIFE6: Tremere shield hands-model swap.
- [ ] LIFE6: empty melee view (no hands, no weapon; nothing synthesized).
- [x] 9.8: ownership, loose pickup, explicit loot-container session, CommonUI transfer panel —
  landed.
- [ ] PHYS3: `func_physbox` as `FElysiumPhysProp`'s brush twin (`.ents` convex hulls as body).
- [ ] PHYS3: `func_breakable`'s damage surface on that brush twin.

## Open questions
- The word-15 commit's exact accumulation formula beyond "accumulated, never multiplied in" —
  unspecified.
- The spread cone's interpolation function between `SpreadAngle` and `SpreadAngleMax` — unspecified.
- `SkillRequirement`'s consumer shape — unspecified.
- RE42 opens for LIFE6: the alternate weapon-attachment placement source, the blend interface
  identity, `THAUMATURGY`'s and `ENFIELD`'s owning class, what retail does with a missing hands
  model, and the fourth live `viewmodel` entity.
- LIFE6's ELGVM1 harness: 12 of 13 scenarios have never run; the one that did finished partial on a
  timeout — no viewmodel claim is capture-verified.
- `func_breakable`'s break threshold, debris presentation and sound — not stated in the plan beyond
  "damage surface".
