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
except its findings in `stories/v4/packets.md`. If the body does not fall and rest, stop: report
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
