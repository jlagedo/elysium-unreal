# 0008 ranged-bottles — pick up the gun and shoot the beer bottles: firearms, pickup, breakables

## Witness
`sp_tutorial_1`'s shooting-range beat: the player picks the weapon up off the ground, fires, and
a hit registers on a bottle backed by `func_breakable`'s damage surface, which breaks as retail
resolves it. The first-person hands and weapon are **0013**'s; this beat is complete without
them.

## Scope
The firearms half of the weapon controller's transaction and its open joins, the loose pickup,
the breakable damage surface. Owned elsewhere and consumed here: the melee half and the contact
instant — **0005**; the first-person viewmodel — **0013**; the physics hands, `func_physbox`,
the constraint family — **0007**; the rest of inventory (player drop, barter, buy/sell,
`trigger_inventory_check`, `TravelsWithPlayer()`, the `item_container` lid, the lockpick HUD) —
unowned under 9.8.

## Sources
- Oracle: `docs/vtmb/combat-and-damage.md` (RE40), `docs/vtmb/inventory.md` (RE38),
  `docs/vtmb/phy_vphysics.md`, `docs/vtmb/physics-interaction.md`.
- Authored data, V2 seams under `$ELYSIUM_WORK_ROOT/exports_v2/`: `vdata/items/*` (fire modes,
  `SpreadAngle` / `SpreadAngleMax`, `SkillRequirement`, `Major` / `MinorKnockbackDist`),
  `maps/sp_tutorial_1.entities.glb` (the range's `func_breakable` bottles, the loose weapon).

## Witness data
- The damage spine, one typed health commit and the weapon controller's two-half transaction are
  built (0005/1); the ranged joins left open by RE40: the step-9 template filters and the
  **word-15 commit** (accumulated onto the damage descriptor, never multiplied in), the spread
  cone's interpolation input between `SpreadAngle` and `SpreadAngleMax` (the function is
  UNRECOVERED), `SkillRequirement`'s consumer (UNRECOVERED beyond "open").
- Ownership, loose pickup, the explicit loot-container session and the CommonUI transfer panel
  are landed (RE38 closed); the weapon on the range floor takes the loose-pickup path.
- `func_physbox` is `FElysiumPhysProp`'s brush twin (0007/7); `func_breakable`'s damage surface
  is what a shot bottle resolves through. Break threshold, debris presentation and sound are
  UNRECOVERED.

## Stories
In build order. A story is done when every behaviour it lists is in the substrate and its
recovery is written in the oracle section it names. Numbers are stable ids cited by other
documents; a split keeps the number and adds a letter. Each open story carries the retail
contract the code must match, the job, what it consumes or provides, and a size (XS–XL) with the
model / effort tier recommended for it.

- [x] **1. Loose pickup** (was 9.8): ownership, loose pickup, the loot-container session, the
  transfer panel. Oracle: `inventory.md`.
- [ ] **2. The word-15 commit and the step-9 template filters** (was 13.3).
  Retail: accumulated onto the damage descriptor, never multiplied in; the exact accumulation
  is UNRECOVERED beyond that.
  Job: both joins on the landed controller, from the recovered arms.
  Oracle: `combat-and-damage.md` (RE40).
  Size: S. Effort: Sonnet / high; corpus pass first.
- [ ] **3. The spread cone.**
  Retail: the interpolation between `SpreadAngle` and `SpreadAngleMax` — function UNRECOVERED.
  Job: recovered and ported.
  Oracle: `combat-and-damage.md` § "Spread".
  Size: S. Effort: Sonnet / high; corpus pass first.
- [ ] **4. `SkillRequirement`.**
  Retail: the consumer — UNRECOVERED.
  Job: recovered and ported.
  Oracle: `combat-and-damage.md`.
  Size: S. Effort: Sonnet / high; corpus pass first.
- [ ] **5. The gunshot's knockback entry.**
  Retail: shooter-to-victim distance against the fire mode's `Major` / `MinorKnockbackDist`
  enters 0005/8's flying chain.
  Job: the producer on the ranged commit.
  Consumes: 0005/8.
  Oracle: `combat-and-damage.md` § "Launch is a velocity assignment".
  Size: XS. Effort: Sonnet / medium.
- [ ] **6. `func_breakable`.**
  Retail: the damage surface on the brush twin; threshold, debris and sound UNRECOVERED.
  Job: the surface bound to 0007/7's body; the break resolved from the recovered threshold;
  debris and sound recovered and ported (sound through 0011's typed event).
  Consumes: 0007/7.
  Oracle: `phy_vphysics.md` § "Breakables" (new).
  Size: M. Effort: Sonnet / high; corpus pass first.

## Seams
- Provides: the closed firearms joins to 0002's combat programs and any later ranged consumer;
  `func_breakable`'s surface to any prop scripted to shatter.
- Consumes: 0005's spine and flying chain (2, 5); 0007's brush twin (6); 0011's typed event (6).
- Open recoveries: the word-15 accumulation (2), the spread function (3), the `SkillRequirement`
  consumer (4), the break threshold, debris and sound (6).
