# V4d Step 0 — the spike: does a dead NPC's body fall and rest? (2026-10-04)

**Verdict: yes.** Handed to physics, the body falls and comes to rest on the arena floor, headless
(`-nullrhi`, fixed step 60 Hz), with the project's animation instance (`ABP_ElysiumBiped_C`) still
installed and unpaused, and with a garment attached. Nothing holds it. The builder can be written.

Everything below was measured in a scratch state that is reverted; only this file remains.

## What was done

- **Physics asset, no content touched.** Scratch code at the top of `StartBodyRagdoll`
  (`Visual/ElysiumEntityBodies.cpp`): when the body has no physics asset, a transient `UPhysicsAsset`
  in the transient package, filled by `FPhysicsAssetUtils::CreateFromSkeletalMesh` (default
  `FPhysAssetCreateParams`: capsules, `bSetToMesh=false`), put on the **component** with
  `SetPhysicsAsset(Asset, /*bForceReInit*/ true)`. It needed the `PhysicsUtilities` module in the
  editor block of `ElysiumUE.Build.cs`. `regular_cop`: 16 bodies, 15 constraints (pelvis, spine ×3,
  neck, head, upper arm and hand ×2, thigh, calf, foot ×2). `bum_male`: 15 / 14.
- **Collision switch.** `AElysiumNpcBody::ApplyCollisionState` behind a scratch console variable:
  `1` = the frozen body keeps its actor collision and switches off only the capsule; `0` = today's
  code (`SetActorEnableCollision(bEnabled && !bFrozen)`).
- **Log.** A 0.2 s timer started in `StartBodyRagdoll`: `Bip01 Pelvis` world position, its body's
  linear speed, the pelvis body instance's own world Z, the component Z, a downward trace for the
  floor, the collision states.
- One build (36 s). Three arena runs (25–28 s each): two scratch copies of `damage_lethal_death`
  (one per value of the variable), the real `damage_lethal_death`, and one copy with
  `"body": "bum_male"` (a body that wears a `UChaosClothComponent`).

## The series (`regular_cop`, floor Z = 0, t = seconds after the handoff, z = pelvis cm, v = cm/s)

```
0.02 z90.8 v21 | 0.2 z81.1 v106 | 0.4 z71.2 v38 | 0.6 z65.4 v55 | 0.8 z46.6 v208 | 1.0 z24.9 v119
1.2 z20.0 v33  | 1.4 z18.3 v22  | 1.6 z16.8 v16 | 1.8 z16.1 v3.6 | 2.0 z16.1 v1.3 | 2.2 z16.1 v0.8
2.6 .. 11.8: z16.2, v 0.3 .. 0.7 (60 samples, no further change)
```

- Identical, sample for sample, with the capsule-only switch and with today's whole-actor switch,
  and in the real `damage_lethal_death` (seed 1): the run is deterministic.
- The pelvis **body instance's** world Z equals the pelvis **bone's** world Z at every sample
  (90.8, 24.9, 16.2): the simulated pose reaches the component's bone transforms headless. A
  record can read the fall through `GetBoneLocation` under `-nullrhi`.
- Head Z 152.5 → 16.2. The pelvis ends 59 cm from where it started in the horizontal plane
  (y 977.4 → 918.8): the body topples forward, it does not sink in place.
- `bum_male` (one garment child): `0.02 z85.7 | 0.4 z58.0 | 0.6 z34.7 | 0.8 z32.7 | 2.0 z32.9 v1.0 |
  2.4 .. 11.8 z32.9 v0.0 .. 0.2`. It falls and rests, the garment does not hold it — but the pelvis
  rests at **32.9 cm**, above the 24 cm the `corpse_on_floor` probe is briefed with (see below).

## Findings the brief does not have, or has wrong

1. **The dead body's collision switch does not reach the ragdoll. Brief item 3 ("Missing: … a fix
   in the dead body's collision", `ElysiumNpcBody.cpp:1313`) is not needed for the fall.** The
   visual mesh is created with `NewObject<USkeletalMeshComponent>(Owner)` where the owner is the
   **map actor** (`ElysiumEntityBodies.cpp:1741`; measured `owner=ElysiumMapActor_0
   ownerCollision=1`), and is only *attached* to the motor (`ElysiumMapActorEmbodiment.cpp:107`).
   `SetActorEnableCollision` on `AElysiumNpcBody` gates the components that actor owns (its capsule,
   its own unused `GetMesh()`), not a component attached to it. With today's code unchanged
   (`motorColl=0`) the body collided with the floor and rested exactly as with the capsule-only
   switch. The coder may leave `ApplyCollisionState` alone; if item 3 is kept for clarity it changes
   nothing observable.
2. **The handoff's existing order works.** Before the handoff the component is `NoCollision`, has no
   physics state and no body instances (`physicsState=0 instBodies=0`). `SetCollisionProfileName(
   "Ragdoll")` creates the physics state from the asset, `SetSimulatePhysics(true)` then simulates
   (`sim=1`), no "refused to simulate" warning. So a baked asset on the mesh needs no change in
   `StartBodyRagdoll`: the true branch ran for the first time here and is sound.
3. **The animation instance does not fight physics.** `pauseAnims=0`, the component ticks, the graph
   is still installed; `SetSimulatePhysics` sets `bBlendPhysics` and every bone with a body takes
   the simulated transform. Nothing more is needed (bones without a body — fingers, face — still
   take the graph's pose, relative to their simulated parent).
4. **The body never sleeps.** `IsAnyRigidBodyAwake()` and the pelvis instance's `IsInstanceAwake()`
   stayed true for the whole 11.8 s, with a residual 0.3–0.7 cm/s on the pelvis (0.0–0.2 on
   `bum_male`). **`corpse_on_floor`'s "at rest" must be a speed threshold** (a few cm/s; 5 is safely
   above the jitter and below the last settling sample of 3.6 → the body is at rest from about
   t = 2.0 s), never the sleep state. Sleep thresholds and damping are free for Unreal under the
   ruling, so the coder may also tune the asset so it sleeps; the probe must not depend on it.
5. **The 24 cm bound is body- and asset-dependent.** `regular_cop` rests at 16.2 cm, `bum_male` at
   32.9 cm, both with Unreal's default capsule asset. The retail `.phy` hulls will give other
   numbers; the integrator must read the measured rest height of each record's body before trusting
   24 cm, and widen the bound or name the body if a rig rests propped.
6. **Time to rest: about 2 s** after the handoff (death at t = 3.0 → at rest by about 5.0 in
   `damage_lethal_death`, duration 15). The record's deadline has room.
7. **The component moves with the ragdoll; the motor does not.** The mesh component's Z went
   2.0 → −23.6 (it follows the root body) and the pelvis ended 59 cm from the death spot, while the
   motor capsule stayed put (`capsuleBottomZ=2.1` throughout). So: the entity's origin, and the
   use / feed / loot anchor, must keep being read from the motor (the retail contract: frozen at
   the death spot), never from the visual component or a bone; and `corpse_on_floor` must read the
   pelvis **bone** (`GetBoneLocation("Bip01 Pelvis")`), not the component's location, which is
   below the floor at rest.
8. **`on_ground` stays false on a corpse.** It is the motor's floor answer and reads false on the
   frozen body, ragdoll or not; it is what keeps `damage_lethal_death` `expected-fail` today
   (`probe[2]`). With the ragdoll in, the record still ends `expected-fail` until that probe is
   replaced by `corpse_on_floor` and `known_red` is removed.
9. **The asset can be set per component.** `SetPhysicsAsset` on the component, without touching the
   mesh package, is enough for the handoff. The bake step may still attach it to the mesh as
   briefed; this is only the cheaper alternative if re-saving mesh packages turns out to be a
   problem.
10. **`regular_cop` wears no garment** (`children=0`), so the briefed record does not exercise the
    garment case; `bum_male` does and it holds nothing. The cloth component is attached to the body
    and follows it as leader pose. What the cloth *looks* like on a ragdoll was not seen (headless).

## Not settled by the spike

- Nothing was rendered: whether knees and elbows bend the right way, whether the garment and the
  hair dynamics look right on a simulated body, is the integrator's lab check.
- The default asset disables collision between its own bodies and has ball-and-socket limits; the
  `.phy` asset's behaviour (self-collision, hinge limits, masses) is untested.
- A save / load of a ragdolled corpse (`RestoreDeathBodyState` runs the handoff again from the
  spawn pose) was not run.
- Only the arena floor was tested (a static-world actor); a map floor was not.

## Revert

`ElysiumUE.Build.cs`, `Visual/ElysiumEntityBodies.cpp` and `Visual/ElysiumNpcBody.cpp` restored with
`git checkout -- <path>`; the scratch records under `Arena/scenarios/_spike/` deleted; no content
package was written. The editor binary on this machine still holds the scratch code until the next
build.
