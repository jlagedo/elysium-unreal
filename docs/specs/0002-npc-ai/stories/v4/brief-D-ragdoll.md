# V4d — the corpse falls: retail's bodies, Unreal's solve (the owner's ruling, 2026-10-04)

Added to V4 by the owner after a three-agent re-validation (retail's death chain read from the
listing; the port and pipeline inventoried; an independent sizing). It replaces README § 8 Q3 and
§ 7 K5 as they were first written, and withdraws item 1 of `brief-J-judge.md`. *Size:* M. One
spike, then one coder and one integrator. Runs after V4a (it uses the `corpse_on_floor` probe the
seam adds); it does not depend on V4b or V4c.

## The ruling: a named modernization

The death animation's look and the ragdoll's fall are Unreal's problem, not retail's. **The fall
is solved by Unreal (Chaos); no calibration against VtMB's simulation is required. The bodies, the
masses and the joint limits are taken from the game's own `.phy` data. What game logic observes
stays retail's.** This is the visual-only half of a modernization (`CLAUDE.md` § Project rules):
retail's ragdoll is client-side and nothing the bytecode reads depends on where the drawn body
rests.

| from the game (`.phy`) | free for Unreal | retail contract, unchanged |
|---|---|---|
| which bones get a body; the convex hull per solid; the mass per solid; one joint per `ragdollconstraint` with its per-axis min/max | damping, solver iterations, sleep thresholds, the collision profile, the impulse scale, how the body looks as it falls and rests | `OnDeath` on the kill tick; the `corpse` event; the corpse is the same NPC entity, frozen at the death spot, with its use / feed / loot anchor there; its removal (`SUB_PVSRemove`, re-checked every 10 s while a player sees it); whether the model has a ragdoll at all |

## What retail does (read from the listing, 2026-10-04)

`CAI_BaseNPCTroika::Event_Killed 0x102bf340` (the crime record, `MarkAsDead`) →
`CAI_BaseNPC::Event_Killed 0x10265ad0` (the death sound, `OnDeath`) →
`CBaseCombatCharacter::Event_Killed 0x1032b9b0` (`m_lifeState = 1`, the force: the damage force or
`CalcDamageForceVector`, plus velocity, clamped) → slot 301 `CreateCorpse 0x1032c0e0`, three arms:
the player → `SpawnStaticCorpse`; `MiscFlag 0x80000` → `SpawnStaticCorpse` (an unsimulated copy of
the pose), `Hide`, the NPC removed at +0.5 s; otherwise `BecomeClientRagdoll(force, bone, 0)` with
`bone` the hit bone or `LookupBone("Bip01 Spine2")`, its return discarded (`0x1032c2a1`). Tail: the
corpse (slot 137, `this`) gets `ThinkSet(SUB_PVSRemove)` at +10 s (`0x1032c404`).
`BecomeClientRagdoll 0x10090180` → `TriggerClientRagdoll 0x1008b800` latches `m_vecForce` /
`m_nForceBone` (only for |F| > 0 and bone > 0), sets `m_nRenderFX = 0x17`, makes the NPC not solid,
move type none, zero bounds, the think cleared. The server simulates nothing; the client builds
the ragdoll from the `.phy`.

**Corrections to README § 1 and § 7 K5:** the `ACT_DIERAGDOLL` seed runs only when the bone is −1
(`0x1009021a`), and `CreateCorpse` always passes a real bone: an ordinary corpse ragdolls from the
pose it holds. No seed pose is held, and K5's "rigless corpse holding its seed pose" stand-in is
withdrawn.

## What exists, and what is missing (inventoried 2026-10-04)

- The `.phy` is decoded (`pipeline/src/elysium_pipeline/formats/model_glb/physics.py:299-433`:
  solids and hulls, `solid` mass / damping / inertia, `ragdollconstraint` parent / child and per-axis
  min / max / friction; `importers/physics_data.py:151-180` joins each solid to its bone) and
  already baked: one `UElysiumPhysicsData` asset per model (`pipeline/unreal/import_physics_data.py`;
  621 in the last run, 363 under `character/`), read by nothing.
- The handoff exists: `StartBodyRagdoll` (`Visual/ElysiumEntityBodies.cpp:1507-1558`) sets the
  `Ragdoll` profile and simulates if the mesh has a physics asset; it returns false today because
  none has one. The true branch has never run (`ElysiumTestServices.h:909` `bBodiesRagdoll` is never
  set).
- **Missing:** a builder from `UElysiumPhysicsData` to a `UPhysicsAsset`; its bake step; and a fix
  in the dead body's collision — the frozen NPC switches the whole actor's collision off every
  think (`ElysiumNpcBody.cpp:1313`), the ragdoll's mesh included, so a body handed to physics would
  not simulate or would fall through the floor.
- 321 character models carry a ragdoll rig (npc 238, pc 58, monster 19, gibs 6); 289 share the
  same 15 bodies and 14 joints.

## Step 0 — the spike (one agent, one build, about two hours; before the builder is written)

The biggest unknown is whether this project's NPC body falls and rests at all once handed to
physics (its custom animation instance, the per-think collision switch, the attached garments).
Settle it cheaply: in a scratch state, give `regular_cop` Unreal's default physics asset (the
editor's `CreatePhysicsAsset`), switch off only the movement capsule on the dead body, run
`damage_lethal_death`, and read the pelvis height over time. Nothing from the spike is committed
except its findings in `stories/v4/packets-spike.md` (done: see § "After the spike"). If the body does not fall and rest, stop: report
what holds it (the animation instance still driving the pose, a garment's attachment, the profile).

## Step 1 — the coder (files; re-locate by Grep)

1. **The builder** — a C++ editor function beside `BuildPhysicsAsset`
   (`ElysiumClothBuildLibrary.cpp:256`, the precedent): from a model's `UElysiumPhysicsData`, a
   `UPhysicsAsset` with one body per solid (the solid's convex hull, as `bake_lib.set_phy_collision`
   `:520` converts a `.phy` hull for props; the solid's mass) and one constraint per
   `ragdollconstraint` (half the range as the limit, the midpoint as the frame offset, a zero range
   locked; x and z signs through the skeleton's mirror, `bsp.py:121`). The axis and unit conversion
   are the pipeline's existing ones: state them at the line. Knee and elbow on `regular_cop` are
   z-only hinges (−95..4, −120..4): the acceptance check for the sign mapping.
2. **The bake step** — a step in `pipeline/unreal/import_characters.py` with its own recipe
   fingerprint (as cloth's `_PHYS` assets, `:285-353`), attaching the asset to the mesh.
   `PRODUCER_VERSION` (`:17`) is NOT bumped: no mesh or animation is re-imported. `--bodies` scopes
   a run. Step 2's scope is the bodies the two witness maps and the arena records use; the whole
   corpus is a later run (0014).
3. **The dead body's collision** — the frozen corpse switches off only its movement capsule, so the
   ragdoll's bodies collide with the world (`ElysiumNpcBody.cpp:~1313`). Whether a ragdoll collides
   with the player is not recovered from retail (inferred: it does not): leave it not colliding
   with pawns, named at the line.
4. **Whether a model has a ragdoll** is answered from its `.phy` data asset (retail's "the model
   interface answers a ragdoll"), never from whether Unreal built a physics asset; today it is a
   stub (`ElysiumNpcBaseSelect.cpp:75-79`). A model with no rig keeps its last pose
   (`BecomeClientRagdoll` answering false changes no schedule). **Open, one read first:** the two
   re-validation agents disagree on whether a rig-less ordinary NPC ever reaches `SCHED_DIE`
   (`CreateCorpse` replaces the think; inferred that it does not). Read `SelectSchedule`'s state-7
   fork against `0x1032c404` before writing this item.
5. **Tests**: the ragdoll-true branch gets its first test (the recording double answering a rig);
   no test of the solver.

Not in V4d (0014 keeps them, no rework): the death impulse (the arena's scalar `TakeDamage` yields
zero force in retail, `docs/vtmb/` `combat-and-damage.md:1832-1835`, so step 2's records need
none), `prop_ragdoll`, joint friction and surface properties, the full-corpus rollout (a reuse run,
~12–18 min inferred), the calibrations (0014/1–2: a visual check replaces them), the static-corpse
arms (`MiscFlag 0x80000`, the player), feed and explosion deaths (unread).

## Step 2 — the integrator

One build; two or three scoped bakes (minutes each); `damage_lethal_death` and `verbs_stealth_kill`
with the `corpse_on_floor` probe (the pelvis within 24 cm of the floor and at rest by the record's
deadline) green; the retail contract unchanged in the traces (`OnDeath` on the kill tick, `corpse`,
the entity at the death spot, no `move` / `task` / `schedule` after death); the default and arm
tiers green; the full suite with every other verdict unchanged. A visual check in the lab
(`uv run elysium gr --arena`, `elysium.gr_scenario damage_lethal_death`): knees and elbows bend the
right way, nothing folds or explodes — a screenshot in the report. A sign or frame mistake is fixed
by a re-bake, not a study. Commit once; tick V4d.

## Rules

As every V4 brief: coders never build; the integrator builds once and commits once on the work
branch, never pushes; the query budget (10 s warns, 60 s stops); text through Grep / Read / Glob;
a build, a bake or a run waited on by blocking or by its completion notification, never a sleep or
a polling loop.

## After the spike (2026-10-04)

Step 0 is done: `packets-spike.md` (read it whole; it is short). Where it and the text above
disagree, this section wins. The spike's findings are in `packets-spike.md`, not in `packets.md`
as Step 0 said — that file does not exist.

**The verdict.** Handed to physics, the body **falls and comes to rest, headless** (`-nullrhi`,
fixed step 60 Hz), with the project's animation instance still installed and unpaused and with a
garment attached, and **with no change to the handoff**: `StartBodyRagdoll`'s existing order
(`SetCollisionProfileName("Ragdoll")`, then `SetSimulatePhysics(true)`) creates the physics state
and simulates once the component has a physics asset; the true branch ran for the first time and
is sound; the animation instance does not fight physics. The builder can be written. The run is
deterministic. Time to rest is about 2 s after the handoff.

**Changes to Step 1.**

- **Item 3 (the dead body's collision fix) is withdrawn: it is not needed for the fall.** The
  drawn mesh is created with the **map actor** as its owner (`Visual/ElysiumEntityBodies.cpp`
  ~:1741) and only *attached* to the motor (`Map/ElysiumMapActorEmbodiment.cpp` ~:107), so
  `SetActorEnableCollision` on `AElysiumNpcBody` gates the capsule and that actor's own unused
  mesh, not the ragdoll. With today's code unchanged the body collided with the floor and rested
  exactly as with a capsule-only switch. **Leave `ApplyCollisionState` alone**; the coder does
  not touch `Visual/ElysiumNpcBody.cpp`. README §8 Q3's "a ragdoll would fall through the floor"
  and § "What exists, and what is missing" above ("a fix in the dead body's collision") are
  wrong on this point. The ragdoll not colliding with pawns stays as briefed (inferred, named at
  the line where the profile is set).
- **Item 4's open read is settled.** `CreateCorpse 0x1032c0e0` replaces the think at `0x1032c404`
  on every ordinary arm, so **an ordinary kill never reaches `SCHED_DIE`, rig or no rig** (the
  state-7 fork `0x1028a8ec` is reached only by other state-7 writers; lane C2 walks the arms and
  corrects `lifecycle.md`, J8). A model with no rig **keeps its last pose** — *inferred* (retail's
  client has no ragdoll to build; nothing server-side poses it). Write item 4 on that: no read of
  `SelectSchedule` is needed first.
- **Item 2, an option the spike opened:** the asset can be set **per component**
  (`SetPhysicsAsset(Asset, /*bForceReInit*/ true)`) without re-saving the mesh package. The bake
  step may still attach it to the mesh as briefed; use the component path only if re-saving mesh
  packages turns out to be a problem, and say which you chose.
- Items 1 and 5 stand.

**The rest test and the height bound (Step 2, and the `corpse_on_floor` probe the seam wrote).**

- **At rest is a speed threshold, never the sleep state.** The body never sleeps:
  `IsAnyRigidBodyAwake()` stayed true for the whole 11.8 s with a residual 0.3–0.7 cm/s on the
  pelvis. The probe passes on **pelvis speed under ~5 cm/s** (above the jitter, below the last
  settling sample of 3.6). Sleep thresholds and damping are free for Unreal under the ruling, so
  the coder may tune the asset so it sleeps; the probe must not depend on it.
- **The probe reads the `Bip01 Pelvis` bone of the drawn mesh** (`GetBoneLocation`; the simulated
  pose reaches the component's bone transforms headless), never the component's location — the
  component follows the root body and ends below the floor (Z 2.0 → −23.6).
- **The height bound is per body and is measured with the real `.phy` asset.** "Within 24 cm" in
  Step 2 above is not a constant: with Unreal's default capsule asset `regular_cop` rests at
  **16.2 cm** and `bum_male` at **32.9 cm**. The retail `.phy` hulls will give other numbers. The
  integrator, after the scoped bake, reads the measured rest height of **each record's body** on
  the `.phy`-built asset (the lab, `elysium_entity_get` or the probe's own value in the trace),
  and sets each record's bound from it with the measurement in `about`; a rig that rests propped
  gets a wider bound or a named body, never a loosened rule.
- **`on_ground` stays false on a corpse** (the motor's floor answer). The two death records turn
  green only when their end probe is `corpse_on_floor` (the seam's edit) and `known_red` is
  removed.

**What keeps reading the motor.** The mesh component moves with the ragdoll (the pelvis ended
59 cm from the death spot); the motor capsule stays put. So the **entity's origin and the use /
feed / loot anchors keep being read from the motor** — the retail contract: frozen at the death
spot — never from the visual component or a bone. The integrator's trace check ("the entity at
the death spot") is that.

**Untested by the spike — the integrator's to check, the coder's to keep in mind.**

- **Rendering**: nothing was drawn. Whether knees and elbows bend the right way, and how a
  garment (`UChaosClothComponent`, leader pose) and the hair dynamics look on a simulated body.
  `regular_cop` wears no garment, so the briefed record does not exercise one; `bum_male` does
  (it fell and rested; its look was not seen). The lab's visual check covers both bodies.
- **Joint limits**: the default asset has ball-and-socket limits and no collision between its own
  bodies; the `.phy` asset's hinges, self-collision and masses are untested.
- **Save / load** of a ragdolled corpse (`RestoreDeathBodyState` runs the handoff again from the
  spawn pose): not run. Save files are disposable, but a load must not throw or leave the body
  standing: one manual check, noted in the report.
- **A map floor**: only the arena floor (a static-world actor) was tested. `verbs_stealth_kill`'s
  staging decides whether a map floor is exercised; if not, one lab kill on a witness map.

The spike's scratch code is reverted; the editor binary on the spike's machine holds it until the
next build. It needed the `PhysicsUtilities` module in the editor block of `ElysiumUE.Build.cs`
for `FPhysicsAssetUtils`: the builder may need the same.
