# V4d — the corpse falls: final coder brief

Final, 2026-10-04. Run after V4c integration, retaining V4a, V4b, V11 and V4o. Read
AGENTS.md first, HANDOVER.md, packets-spike.md, S1 §§1/3, S4 §§e/f.1, S13 §1 and
**packets-S14.md**. S14 verifies the scout and supersedes this brief's old contradictions.
Source is being edited by V4c: relocate functions by name and preserve its final diff.
Three disjoint coder lanes, then one integrator using brief-D-integrator.md.

## The contract

**Chaos owns the fall; no calibration against retail. The .phy supplies bodies, convex
hulls, masses and joint limits. What game logic observes stays retail's.** Solver iterations,
damping, sleep thresholds and presentation collision tuning are Unreal's. The entity and
use/feed/loot anchor remain at the death spot; the visual pelvis may move independently.
Keep the existing world-colliding, Pawn-ignoring Ragdoll profile. Do not change the motor's
ApplyCollisionState: the spike proved its actor collision switch does not gate the separately
map-owned mesh. Keep V4c's `RetailSolidFlags |= 4` in the rigged branch.

Ordinary death: Troika Event_Killed **0x102bf340** → NPC **0x10265ad0** (freeze/script
exceptions, DeathSound, OnDeath) → combat character **0x1032b9b0** (lifeState 1, slot 301)
→ CreateCorpse **0x1032c0e0**. Player and No_Ragdoll_Death 0x80000 use static corpses.
Ordinary NPCs call BecomeClientRagdoll **0x10090180** with the hitbox's bone or
**LookupBone("Bip01 Spine2")** and discard its answer. The same NPC is the corpse.
TriggerClientRagdoll **0x1008b800** latches force/bone only for nonzero force and bone>0.
Death impulse simulation is outside this story.

Capability is the source model's rig. No rig: zero bounds, return false; no new solid,
move or think writes, no HoldBodyFinalPose. Rig: always make the weighted ACT_DIERAGDOLL
pick; commit/reset cycle only for explicit bone−1 (0x1009021a). Ordinary deaths retain the
current pose. Preserve V4c's shared RNG draw, solid flag, render FX 0x17, move-none, bounds
and think-clear ordering. The corpse tail replaces think with or without a rig: no ordinary
corpse returns to NPCThink/SelectSchedule. The separate state 7 fork
**0x1028a8ec..0x1028a92b** executes the transaction with zero force/bone−1/flag 0,
selecting 0x2c on success or 0x2b on refusal; it is not the next program of an ordinary corpse.

| Corpse | V4c clock to preserve | V4d lifetime |
|---|---|---|
| ordinary mortal | SUB_PVSRemove 0x102696f0 at +10; rearm +10 while view cone/PVS/FVisible all pass, otherwise remove | retain while seen; release on actual removal |
| Kindred / Has_Burning_Death | CreateCorpse burn tail 0x1032c32f: SUB_Remove+10, regardless of sight | release at +10; burn look remains0014 |
| pedestrian, bit 9 clear | 0x103a38c0 calls base, clears think, sets SOLID_NONE | retain indefinitely; sleep allowed |
| fade child | later 0x10265d72 →0x102695d0 →0x10269960; start +10, alpha−7/+0.1, zero then remove/+0.2, about +13.8 | retain until actual removal; preserve C2 alpha state/visual wiring |

Fade wins over burn or pedestrian clear. S13's ordinary maker assigns 4/0x204: no maker patch
or map bake. `verbs_stealth_kill` is a direct regular_cop cast with spawnflags 4, not a fade
child; `corpse_fades` owns stealth_victim_maker/Shovelhead. Keep both stages.

## First step — one controlled frame measurement

The reads settle units/basis, **not solid placement or the joint reference basis**. D1 first
prepares the diagnostic regular_cop asset and candidate transforms below. The integrator
performs the first scoped bake and **one controlled Physics Asset Editor/lab check** after
build 1; coders still never build, bake or run. No 34-body bake before that gate. Feed the
measured choice back to D1/D2 serially; reserve reviewed build 2/re-bake for a frame correction.

Verified conversions (model_glb/physics.py::_ledge, physics_data.py::physics_projection,
phy.py::_read_ledge, bsp.py::source_to_unreal/source_quat_to_unreal,
skeletal_stage/payload.py::_conv_pos/_conv_quat):

- Binary IVP `(x,y,z)` metres becomes published **`g=(x,-y,-z)`** metres. PhysicsData copies
  `g` unchanged. Its frame label does not mean raw binary IVP. Published vertices to Unreal
  centimetres: **`q=100*(g.x,g.z,g.y)`**. Raw binary IVP would use `100*(x,-z,-y)`.
- Source MDL positions are inches: `2.54*(x,-y,z)`; quaternion becomes `(-x,y,-z,w)`.
  Thus `R_u=M R_s M`, `M=diag(1,-1,1)`. Use the mesh's accumulated reference-bone
  transform `B_u`, not a parent-relative transform or assumed shared humanoid pose.
  PoseToBone is raw Source-inch row-major 3x4 inverse bind.
- **Candidate if hulls are solid-local:** convert solid origin/QAngles into `S_u` through
  the Source conversion, then `p_b=B_u.inverse(S_u(q))`. If already model-local, the
  candidate is `B_u.inverse(q)`; if already corresponding bone-local, it is `q`.
  These are alternatives to distinguish, not transforms to stack. Props have no articulated
  bone frame and their collision precedent cannot choose among them.
- After identifying joint frame `J_u`, express the same pivot/basis in both bodies:
  `J_parent=B_parent.inverse*J_u`, `J_child=B_child.inverse*J_u`. Angular axes transform
  with `det(M)*M`. Only if aligned to the mirrored Source bone basis do x/z ranges reverse
  `(min,max)→(-max,-min)` while y keeps its sign. Limits: half-span symmetric limit,
  midpoint signed frame/rest offset; zero span locked. Do not assume Source z is UE Swing2.

**The single measurement:** regular_cop's real 15 solids/14 joints, collision and constraint
gizmos visible. Record right-thigh/right-calf hull placement against the bind mesh and common
knee pivot. In the same controlled check sweep its only free Source z axis to the authored
−95° and +4° stops: knee bends anatomically, calf hull follows, endpoint pivots coincide.
Confirm left-knee and elbow mirroring with those transforms (elbow z −120°..+4°, x/y locked).
Record accepted solid→model→bone and joint-axis matrices, endpoint sign, screenshot and recipe
in phy_vphysics.md before the wider bake. Free fall alone does not prove axes/stops.
Correct frames/signs and re-bake; never enlarge limits or floor tolerance to hide errors.
If the check cannot distinguish a consistent frame, stop the wider bake and report the exact
failed transform. No generic frame-reading task remains in the handoff.

## D1 — editor builder and frame recovery

**Only these files:**

- `Source/ElysiumUE/Private/Editor/ElysiumClothBuildLibrary.h`
- `Source/ElysiumUE/Private/Editor/ElysiumClothBuildLibrary.cpp`
- `Source/ElysiumUE/ElysiumUE.Build.cs` (editor dependencies only, if needed)
- `docs/vtmb/phy_vphysics.md`

1. Add an editor-callable ragdoll builder beside BuildPhysicsAsset. Agree its exact signature
   with D2: typed PhysicsData, skeletal mesh, output package; error and readback body/convex/
   constraint counts and frame version. Prepare the diagnostic first. **Source:**
   ElysiumClothBuildLibrary.cpp::BuildPhysicsAsset/MakePackage/BoneBindTransform; S14 §2.
2. One simulating USkeletalBodySetup per solid, exact named bone join; one FKConvexElem per
   ledge, no fitted primitives, simplification or decomposition. Cook in the settled bone frame;
   update index/bounds maps and registry/package. Do not copy cloth's PhysType_Kinematic.
   **Source:** BuildPhysicsAsset; bake_lib.py::set_phy_collision (one exact hull per ledge).
3. Actual authored mass override per solid, read back in receipt. Resolve constraint endpoints
   by authored solid IDs/typed ordinals, never bone ordinals. Refuse missing/ambiguous joins,
   required limits or usable hulls with unit/solid/field errors; skip no-physics units without
   inventing rigs. **Source:** physics_data.py::physics_projection, Gaps/optional numbers;
   regular_cop solid/constraint data, S14 §2.
4. One constraint per ragdollconstraint with measured frames and exact ranges; zero span locked.
   Disable adjacent-body collision as named presentation tuning, state remaining self-collision
   policy; solver choices never alter masses/limits. Use convex point clouds or validate winding:
   phy.py's determinant comment is wrong. Add PhysicsUtilities only if the chosen editor API
   needs it. **Source:** bsp.py::source_quat_to_unreal, existing PhysicsAsset builder, S14 §2.
5. Record measured matrices/endpoints and citations in phy_vphysics.md, distinguishing read
   equations from measurement. **Source:** that document's The ragdoll rig and first-step check.

## D2 — scoped bake, independent recipe, mesh binding

**Only:** `pipeline/unreal/import_characters.py`.

1. Add the ragdoll step after physics_source on fresh and reused meshes, calling D1's API with
   PhysicsSourceData. Distinct mesh-local `_RAGDOLL` package, never cloth's `_PHYS`; protect
   against pruning. **Source:** import_characters.py::physics_source/cloth_assets/entry loop.
2. Independent ragdoll producer/version/fingerprint: physics projection hash/receipt or data
   recipe, stored mesh recipe, builder/frame version. Stamp/check under its own producer,
   report counts/masses/frame/build/skip/failure. **Do not bump characters-v2 or change mesh/
   animation fingerprints.** **Source:** import_characters.py::fingerprint/cloth_assets,
   import_physics_data.py::publish_entry; bake_lib.py::recipe_fingerprint/stored_recipe/stamp_recipe.
3. Attach saved asset to skeletal mesh and save the package, including repairing missing/wrong
   attachment on reuse. Package save is permitted; mesh/animation re-import is not owed. No
   runtime sidecar reads. **Source:** physics_source/cloth_assets binding/save precedent;
   ElysiumEntityBodies.cpp::StartBodyRagdoll/GetPhysicsAsset.
4. Keep repeatable --bodies and include closure. Scope S14's 34 witness models plus record bodies
   (already included); diagnostic first, full scope after gate; report no-.phy skips. **Source:**
   importers/characters.py::stage_characters, cli.py::import_characters; S14 §4. Do not edit
   CLI, staging, decoder, asset-name catalogue or PhysicsData producer. Report extra-file lines.

## D3 — capability, handoff, terminal release

**Only these files (Source paths below are under Source/ElysiumUE):**

- `Private/Substrate/ElysiumNpc.h`, `Private/Substrate/ElysiumNpc.cpp`
- `Private/Substrate/ElysiumNpcBase.h`, `Private/Substrate/ElysiumNpcBase.cpp`
- `Private/Substrate/ElysiumNpcBaseSelect.cpp`, `Private/Substrate/ElysiumNpcBaseSelect.inl`
- `Private/Substrate/ElysiumCombatCharacter.cpp`
- `Private/Substrate/ElysiumEntity.cpp`, `Private/Substrate/ElysiumScriptedCharacter.cpp`
- `Public/ElysiumWorldServices.h`, `Public/ElysiumMapActor.h`
- `Private/Map/ElysiumMapActorEmbodiment.cpp`
- `Private/Visual/ElysiumEntityBodies.h`, `Private/Visual/ElysiumEntityBodies.cpp`
- `Private/Tests/ElysiumNpcCombatTests.cpp`, `Private/Tests/ElysiumTestServices.h`
- `docs/vtmb/npc-ai/lifecycle.md`, `docs/vtmb/combat-and-damage.md`,
  `docs/vtmb/physics-interaction.md`

1. HasClientRagdollRig reads cooked mesh provenance `PhysicsSourceData->Data.bHasPhysics`.
   Share the predicate with selector capability. A missing PhysicsAsset is a failed bake,
   never a rigless classification. **Source:** 0x10090180 model-interface slot 18;
   CharacterProvenance.h::PhysicsSourceData; physics_projection::bHasPhysics.
2. CorpseForceBone's absent-hit input resolves native `Bip01 Spine2` on the drawn mesh,
   through typed source/native bone names if needed; never hardcode regular_cop ordinal 5.
   With source rig and valid fallback, CreateCorpse calls BecomeClientRagdoll(Force,bone,false)
   and StartBodyRagdoll on the kill tick. Keep no-rig bounds/tail. A missing fallback on a
   purported rig is a named data/attachment failure, never substitute explicit bone−1.
   **Source:** 0x1032c1e4..0x1032c22b /0x1032c29c. The packet has no hitbox index/bone;
   retain a named no-hit accessor seam for CTakeDamageInfo's 0x101c2a30 input and use Spine2.
   Producer completion and impulse are 0014; do not claim hitbox damage support.
3. SelectBecomeClientRagdoll executes the same transaction with zero force/explicit−1/flag 0,
   not just a capability bool. Wire a common implementation for base/Troika callers without
   double seed draws/handoffs. Preserve C2's seed tests/RNG and corpse think override. Guard
   CompleteDeathHandoff/RestoreDeathBodyState by source capability, prevent a second ordinary
   handoff, and never replay OnDeath, weighted picks or clocks on visual restoration. Remove
   stale late-DIE/hold-pose commentary. **Source:** 0x1028a8ec..0x1028a92b/0x10090180;
   ElysiumNpcBase.cpp::CompleteDeathHandoff, ElysiumNpc.cpp::RestoreDeathBodyState.
4. Keep StartBodyRagdoll's proven profile→simulate→verify→wake order. Diagnose absent/empty
   assets or refusal as bake/handoff failures while keeping source capability. Log successful
   admission with the real asset path, body count and active-simulation result; log the same
   body's pre-release simulation state on terminal cleanup, so floor/exists probes cannot
   conceal a non-simulating pose or retained physics body. No new no-rig hold-pose modernization.
   **Source:** packets-spike.md Findings 2; StartBodyRagdoll;
   0x10090180 false arm.
5. Add one idempotent **ReleaseNpcVisual** embodiment/Bodies adapter. Capture actual Visual;
   release claims, stop simulation/collision, destroy physics state, remove presentation-owned
   garments/wield/ornament/trail components and destroy mesh; erase claim entries and clear
   entity Visual. Route actual **FElysiumEntity::Kill** through NPC release, including already
   hidden removal. Route **FElysiumScriptedCharacter destructor** through the same release
   before DestroyMotor. Respect retired/EndPlay guard before dereferencing UObject pointers.
   Never discover the mesh by motor attachment: simulation may detach it. Never release at
   lethal Event_Killed, ordinary ScriptHide, or the DestroyMotor path used for model replacement
   (which preserves authored children). Do not destroy other entities' attached children as
   presentation garbage. **Source:** Entity.cpp::Kill; AnimatingImpl.cpp::GateVisual/
   OnRuntimeModelChanged; ScriptedCharacter.cpp destructor/DestroyMotor; MapActorEmbodiment.cpp::
   BuildNpcMotor/DestroyNpcMotor; EntityWorld.cpp::RegisterNpcBody/Teardown;
   NpcVisual.cpp::GateLeaderCloth/child-cleanup precedents. Today Kill hides/stops pose ticks;
   only world teardown destroys registered NPC meshes, so a new simulating corpse is retained.
6. Recheck burn tail after V4c. In this snapshot the sound is still a comment. If absent,
   emit exactly once `character/vampire burning death.wav`, volume 1, attenuation 0.8,
   pitch 100 (native multiplier 1), channel 0, via existing entity audio; preserve later fade,
   invent no AI sound. **Source:** 0x1032c3c1..0x1032c3d7;
   ElysiumNpcSounds.cpp's EmitSound→PlayBodySound precedent (set channel explicitly).
7. Add NpcCombat.Death arms with typed provenance true/false and recording service: ordinary
   rigged death resolves Spine2, starts once and preserves sequence/cycle despite weighted pick;
   assert retail writes/tail. No rig: false writes only/no simulation/no hold. Source rig with
   failed asset stays rigged and reports failure. Terminal removal while simulating releases
   once (also hidden case), lethal death does not; pedestrian keeps body. Assert one burn sound
   and exact fields/no duplicate using existing BodySounds/request recording, no arena audio
   vocabulary. Keep C2 seed/fade/pedestrian tests. **Source:** 0x1032c0e0/0x10090180/0x103a38c0;
   ElysiumTestServices.h::StartBodyRagdoll/PlayBodySound. These test transaction/lifetime;
   real assets plus integrator checks prove Chaos.
8. Update matching lifecycle/combat/physics recovery without overwriting C2. physics-interaction
   still calls burning a static creation arm; combat-and-damage's creation list is already
   corrected in this snapshot. Record source capability/fallback/release and named absent
   hitbox/impulse inputs. **Source:** 0x1032c0e0/0x10090180; S14 §§1–3.

## Not yours and rules

Manifests have **empty pairwise intersections**. No coder edits Arena/, tracker, spec.md,
research/, generated Slots.cpp, another lane's files or planning packets. Integrator owns
records/close. V4c owns removal/fade/maker/team/attack logic; preserve its completed changes.
No capsule collision rewrite, visual-to-entity feedback or re-created death schedule.

0014 owns death impulse/hitbox producer completion, prop_ragdoll, friction/surface properties,
full-corpus rollout and BurnModel ash/particles/skeleton look. Gib-source sweeps are 0014;
no V4d record depends on them. V6 owns corpse save semantics; one load check is presentation
smoke only. Retain the no-rig refusal writes; do not assert an unmeasured client animation clock.

**Touch only listed files. Never build, bake, run tests, arena or game, or commit. Never push.
Shadowed locals are compile errors (C4458/C4459); check includes/declarations/double definitions.**
Every query has a 60 s hard timeout: >10 s warns/logs, 60 s stops dependent work until optimized;
never retry as-is/widen. No whole read over ~200 KB to find one item; indexed/narrow reads.
Time required query paths before scheduling work. Wait on completion, never polling/sleep loops.

Reports **under 350 words**: changed paths/functions/source addresses; read-only validation;
exact lines owed by other files; diagnostic/API details and remaining owner placement.
No file named report*.md. D1/D2 coordinate signatures by messages/reports, not cross-file edits.
Declare extra-file changes by path/function/source; integrator applies serially after reports.
