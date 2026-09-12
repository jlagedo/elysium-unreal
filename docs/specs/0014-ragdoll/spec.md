# 0014 ragdoll — the corpse falls under simulation and stays lootable where it died

## Witness
On `sp_tutorial_1` the park patrol killed in 0005 falls under ragdoll simulation with the
recovered death impulse, comes to rest, and its use/feed/loot anchor stays at the death origin;
a placed `prop_ragdoll` (Warrens corpses, the Malkavian mansion stalkers, the Ventrue Tower
aftermath) rests in its `demo_sequence` pose. **VtMB owns the rules, Chaos owns the solve.**

## Scope
The ragdoll rig (PHYS1): the two calibrations, the physics-asset bake, the death impulse, the
death handoff, `prop_ragdoll`, the corpse volume. Owned elsewhere and consumed here: the death
family and the final-pose handoff seam — **0005**; the physics hands, constraints and impulse
entities — **0007**; the R8 model GLB and its cooked physics source-data projection — the
character corpus.

## Sources
- Oracle: `docs/vtmb/combat-and-damage.md` (the death impulse envelope),
  `docs/vtmb/phy_vphysics.md`, `docs/vtmb/physics-interaction.md`,
  `docs/vtmb/npc-ai-reverse-engineering.md` (RE-D1–RE-D5).
- Authored data, V2 seams under `$ELYSIUM_WORK_ROOT/exports_v2/`: `models/` (324 rigs, 289 with
  15 solids / 14 constraints: geometry, solid/ledge ownership, constraints, parameters,
  metadata, provenance), `maps/*.entities.glb` (`prop_ragdoll`, 52 placements on 14 maps).

## Witness data
- Two calibrations gate simulation and must be scored against evidence, not guessed: the
  **solid transform frame** (score against each model's bind-pose bone transforms) and the
  **constraint axis identity** (score by settled pose under gravity). Simulation admission fails
  rather than guesses until both are settled.
- The bake: one `USkeletalBodySetup` per solid with the authored convex hulls as
  `AggGeom.ConvexElems`, authored mass as a body-instance override, `damping` / `rotdamping` as
  linear/angular damping, `surfaceprop` through the surface-property table; one
  `UPhysicsConstraintTemplate` per constraint with frames, finite-angle limits and friction from
  this spec's own acceptance. A zero-freedom record is not assumed a weld; source friction is not
  substituted with unmeasured Chaos damping.
- The death impulse: the damage force or its synthesised replacement, plus absolute velocity,
  plus the physics-object term, clamped to `50000.0`, applied at the hit bone or `Bip01 Spine2`
  when the hit bone is unknown, through the Source impulse unit. Retires the recorded "no
  impulse" divergence.
- Retail's corpse entity *is* the dying NPC, frozen non-solid at the death spot, while only the
  drawn body slides; the use/feed/loot anchor does not follow the pelvis. Making it follow is a
  Feel divergence needing an explicit owner call — currently unruled, not built.
- `StartBodyRagdoll` needs no change and stops returning false once the rig lands;
  `HoldBodyFinalPose` becomes the fallback for a body with no rig.

## Stories
In build order. A story is done when every behaviour it lists is in the substrate and its
recovery is written in the oracle section it names. Numbers are stable ids cited by other
documents; a split keeps the number and adds a letter. Each open story carries the retail
contract the code must match, the job, what it consumes or provides, and a size (XS–XL) with the
model / effort tier recommended for it.

- [ ] **1. The solid transform frame.**
  Job: the calibration scored against bind-pose bone transforms; admission refused until it is.
  Oracle: `phy_vphysics.md` § "Calibrations" (new).
  Size: M. Effort: Opus / high.
- [ ] **2. The constraint axis identity.**
  Job: the calibration scored by settled pose under gravity.
  Oracle: `phy_vphysics.md` § "Calibrations".
  Size: M. Effort: Opus / high.
- [ ] **3. The physics-asset bake.**
  Job: the V2 physics builder authoring `UPhysicsAsset` / `USkeletalBodySetup` /
  `UPhysicsConstraintTemplate` onto the baked `USkeletalMesh` from R8's source data.
  Size: L. Effort: Opus / high.
- [ ] **4. The death impulse.**
  Retail: the envelope above.
  Job: applied at the hit bone or `Bip01 Spine2`, through the impulse unit.
  Consumes: 0005's death transaction.
  Oracle: `combat-and-damage.md` § "The death impulse".
  Size: S. Effort: Sonnet / high.
- [ ] **5. The handoff.**
  Job: `StartBodyRagdoll` returns true; `HoldBodyFinalPose` the no-rig fallback.
  Consumes: 0005/4.
  Size: XS. Effort: Sonnet / low.
- [ ] **6. `prop_ragdoll`.**
  Job: the one placed server ragdoll, seeded from its `demo_sequence` pose onto the asset.
  Consumes: 0007's `FElysiumPhysProp`.
  Size: S. Effort: Sonnet / medium.
- [ ] **7. The corpse volume.**
  Job: the anchor stays at the death origin (retail); the pelvis-following divergence stays
  unbuilt until the owner rules.
  Size: XS. Effort: Haiku / low.

## Seams
- Provides: the physics asset and the death-impulse seam that 0007's constraints and 0017's
  corpse restore build on.
- Consumes: 0005's death family; 0007's prop leaf.
- Open recoveries: the two calibrations (1, 2) — shared with 0007's carried objects.
