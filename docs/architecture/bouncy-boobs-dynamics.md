# Bouncy Boobs Dynamics

**Document role:** Working brief for the Unreal host of VtMB's authored breast
bone-chain records. Not a status tracker and not the retail-fact owner.
**Last reviewed:** 2026-08-18

Retail layout, solver, and confidence ledger stay in
`docs/vtmb/secondary_motion.md`. The bounded hair AnimDynamics proof and its
numeric hypotheses stay in `docs/architecture/animation-architecture.md` §8.
Work sequencing stays in `docs/project/roadmap.md`; the parked calibration
slice is specified in `docs/project/plans/animation.md`.

This file owns the breast-specific corpus reading, the difference from hair,
the host-node choice, and the open owner calls.

## What retail authors

Breasts use the **same** client-side bone-chain table as hair, ponytails and
manes: 28-byte records at `MDLHeader` +396/+400, walked after ordinary hierarchy
composition, written back into the bone-to-world palette, then ordinarily
skinned. They are **independent rows** in that table, not a second system and
not renderer cloth.

A typical unique-NPC pair is two one-bone records parented to `Bip01 Spine1`.
Jeanette is the worked example: two head-parented five-bone hair chains
(`Bone01`…`Bone07`, `Bone09`…`Bone13`) plus `right breast` / `left breast` on
`Bip01 Spine1`. The hair proof admits only the hair rows. Jeanette's skirt is
renderer cloth (`MDLHeader.Flags & 0x400`) and is a third, independent payload.

VPhysics is not involved. No breast bone declares `ProcType` or the
`Flags & 0x2` split-rotation bit.

## How they differ from hair

| | Hair / beard / braid | Breast |
|---|---|---|
| Parent of the first moving bone | `Bip01 Head`, `Bip01 HeadNub`, or a ponytail bone | almost always `Bip01 Spine1` |
| Retail walk length | 2–5 bones | **1 bone** on 110 of 113 breast-like records |
| Typical gravity | 0.9–1.1 (the chain hangs) | **0** on most unique-NPC rows (spring / inertia, not sag) |
| Typical max angle | 10–30°; Jack 100° | 20°, 30°, or a loose **90°** |
| What the player sees | motion on turns and runs | conversation-distance chest framing |

The one-bone walk is valid retail: the constructor allocates state per moving
bone and the parent already exists. It is the hair proof's AnimDynamics
**chain** recipe that cannot express it (`BoundBone ≠ ChainEnd` is required,
and the bake / installer reject a one-bone route).

Authored 90° is a solver ceiling, not a look target. The controlled hair
capture already shows a loose ceiling (Jack at 100°) is not reached. Copying
that number onto an AnimDynamics cone on a breast bone produces flop, not
retail.

## Corpus (installed `models/character/**`, header + bones)

Heuristic: first-bone name contains `breast` / `boob` / `tit`, or the parent
is a `Bip01 Spine*` bone and the first bone is not a hair/ponytail/mane name.
Temple-guard `Bone01` off `Bip01 Spine` (3 bones, max 30°, gravity 3) is the
one unique-NPC hit that is probably **not** a breast; do not select every
spine chain.

| Count | What |
|---:|---|
| 113 | breast-like records |
| 55 | character models that carry at least one |
| 25 | unique NPCs |
| 9 | player / shared female bodies |
| 21 | other (commons, a few monsters) |
| 110 / 3 | one-bone / two-or-more-bone |

Parents: `Bip01 Spine1` 106, then a handful of `Spine` / `Spine2` / `Spine3` /
`Spine7`. First-bone names: `BoobLeft03`/`BoobRight01` (18 each),
`left breast`/`right breast` (11 each), generic `Bone01`/`Bone03`/`Bone05` on
several Hollywood / Heather / VV bodies, `left_breast`/`right_breast`.

Three authoring styles sit in the same table:

| Style | Gravity | Spring exp | Max angle | Who |
|---|---:|---:|---:|---|
| Tight jiggle | 0 | 0.3 | 30° | Lily, Rosa, Maria, Pisha, Damsel, Misti, Tawni |
| Loose ceiling | 0 | 2.0 | 90° | Jeanette, Yukie, Kerri, stalker, some Tremere |
| Sag | 3 | 0.3 | 15° | some Gangrel / Tremere player armours |

Damping clusters at 0.9 and 0.95. Two records sit at 0.15, below AnimDynamics'
effective 0.7 floor (the hair mapping already clamps that range).

### Unique NPCs that carry a pair

Yukie, Damsel, Pisha, VV and `vvstrip`, Maria, Imalia, Kerri, Misti /
`mistidance`, Tawni, stalker (both paths), Heather / `heather_3` /
`heather_goth`, Jeanette, Lily / `lilydamaged`, Rosa, Therese, Tourette,
`vampire_hunter_chick` / `vampire_hunter_chock`.

Lily, Rosa and Maria have breasts and no head-parented hair. Jeanette, VV,
Therese, Damsel and Heather have both.

## What the current host does

The hair proof writes a baked-native AnimDynamics **chain** recipe
(`DYNM` on the `.eskm`, `UElysiumHairDynamicsAssetUserData` on the mesh) and
installs it from `ElysiumNpcVisual::InstallHairDynamics`. Three locks keep
breasts out:

1. `ANIM_DYNAMICS_POC_CHAINS` in `mdl_secondary_motion.py` names only the two
   hair routes.
2. The installer admits only stems `jeanette` and `malkavian_female_armor_0`
   and those exact bone names.
3. Bake and install reject a cone above 90° and reject `BoundBone == ChainEnd`.

The same `FAnimNode_ElysiumHairDynamics` wrapper is a stock
`FAnimNode_AnimDynamics` with `bChain = true`. That is the right Unreal mode
for pigtails and Jack's beard. It is the wrong mode for a one-bone breast.

There is no breast recipe, no breast feature flag, and no second solver.

## Host-node choice

Industry practice for one chest bone is a spring on that bone, not a chain:

- **Spring Controller** — the default Unreal tutorial answer since 2016: one
  node per breast, stiffness + damping, spring back to the posed local.
- **AnimDynamics single-body** (`Chain` off) — first-class in
  [Epic's AnimDynamics docs](https://dev.epicgames.com/documentation/unreal-engine/animation-blueprint-animdynamics-in-unreal-engine):
  one `Bound Bone`, box extents, angular spring, axis / cone limits. Chain
  mode is the more expensive second mode.
- **Kawaii Physics** — [pafuhana1213/KawaiiPhysics](https://github.com/pafuhana1213/KawaiiPhysics)
  names hair, skirts and breasts in its README; widely used on UE 5.3–5.8.
  Extra plugin. Not required if stock Unreal is the rule.
- **RigidBody + Physics Asset** — sphere bodies on the breast bones. More
  collision authoring than a one-bone table with no capsules.

Later Source `$jigglebone` is the same *shape* (one bone, spring, angle cap)
and is not VtMB's table.

No published remaster maps VtMB's breast records onto Unreal. The community
Bloodlines guides only note that some female outfits "have body physics."

**Working host proposal** (not landed; not a faithful solver):

- Same `FAnimNode_AnimDynamics` wrapper, second configure path:
  `bChain = false`, `BoundBone` = the breast bone.
- Linear motion locked. Angular spring on. Component simulation space.
  `ComponentLinearAccScale` left on so walk / turn / stop drive the motion
  when authored gravity is 0.
- Gravity scale copied from the record (usually 0). Do not invent sag on
  the unique-NPC set.
- Cone is a presentation clamp — `min(authored, ~15–20°)` — until a
  controlled retail series exists. Authored 90° does not ship as a cone.
- Spring / damping start from the hair hypotheses
  (`animation-architecture.md` §8) and will need a firmer spring on the
  loose-90° group than `4 × 10^(-exponent)` gives.
- No new plugin. No Chaos. No Control Rig. No Verlet port.
- No dedicated cvar unless an explicit A/B is wanted. Hair has none.

The one-bone AnimDynamics form also covers VV / Therese / Heather **hair
curls** (head-parented records whose walk is one bone). Those are hair, not
breasts; they share the primitive.

## Selection rule (proposed)

Admit a record when **all** of:

- the first moving bone's parent is `Bip01 Spine1` (widen only with a named
  exception list);
- the first-bone name matches a breast token (`breast`, `boob`, `tit`) **or**
  is a known anonymous pair on a listed body (`bone01`/`bone03` on VV and
  Heather, `Bone03`/`Bone05` on Kerri / Misti, `Bone30`/`Bone32` on Damsel,
  `Bone01`/`Bone03` on Malkavian female armour 0);
- the first-child walk is exactly one bone.

Reject temple-guard `Bone01` off `Bip01 Spine`, mid-chain hair records, and
every head / HeadNub / ponytail row. Those stay on the hair slice.

## Conversation-cast slice

If breasts run before the rest of the hair allow-list, the first useful set
is the unique NPCs the player sees at talk distance:

Jeanette, VV / `vvstrip`, Therese, Tourette, Damsel, Heather (all three
bodies), Lily / `lilydamaged`, Rosa, Maria, Pisha, Yukie, Kerri, Misti,
Imalia, Tawni.

Jeanette is the smallest proof: the mesh is already a hair-proof body, so one
focused `export characters jeanette` can show pigtails (chain) and breasts
(single-body) on the same character. Damsel / Lily / Rosa are the safer
numeric first look (tight 30° / spring 0.3 / gravity 0). Jeanette needs the
cone clamp or she is the worst first look.

Player-body sag (`gravity 3` on some Gangrel / Tremere armours) is a later
slice. Commons (blood dolls, dancers, strippers) are a later slice.

## Open owner calls

1. Breasts before or after expanding hair beyond the two proof bodies.
2. AnimDynamics single-body vs Spring Controller vs a plugin.
3. Cone clamp value, and whether the loose-90° group gets a firmer spring
   than the hair formula.
4. Whether gravity 3 on player armours is in or out of the first slice.
5. Whether one-bone **hair curls** ship with the same primitive or wait.
6. Whether a still chest on a remastered close-up is an acceptable
   presentation divergence if the clamped look is rejected.

Default remaster rule is reproduce. Secondary motion on this project is
already an Unreal presentation approximation (AnimDynamics, not the retail
point/segment Verlet solve). A clamped cone is a recorded presentation
choice beside the authored ceiling; leaving breasts rigid is the current
proof's choice, not a settled remaster divergence.

## Out of scope

- Jeanette's skirt, Sheriff's coat, and every other `0x400` garment. Those
  are renderer cloth (`docs/vtmb/secondary_motion.md`).
- Faithful numeric replay of the bone-chain solver. That is the parked
  calibration slice.
- Expanding the hair allow-list (Head / HeadNub / ponytail, two-or-more
  bones). Same table, different recipe.

## Provenance

- Retail solver and table: `docs/vtmb/secondary_motion.md`.
- Installed-model census: header + `mdl_skel.read_bones` +
  `mdl_secondary_motion.read_chain_records` / `child_walk` over
  `models/character/**/*.mdl` in the engine-resolved install.
- Host hair proof: `mdl_secondary_motion.ANIM_DYNAMICS_POC_CHAINS`,
  `ElysiumNpcVisual::InstallHairDynamics`,
  `FAnimNode_ElysiumHairDynamics::Configure`.
- Unreal single-body vs chain: Epic AnimDynamics documentation (UE 5.8).
- Industry recipes: Epic forum spring-controller guidance (2016);
  Kawaii Physics README (hair / skirts / breasts); later-Source
  `$jigglebone` QC helpers.
