# Targeted L0 physics producer recovery (2026-10-10)

Targeted recovery and independent slot 237 prerequisite packet, not a completed physics service
or independently confirmed walk. Read against main `20885e71`. Existing r022/r024/r026 walks were reused for
their confirmed transform, think and GameFrame facts. There are **no** `L0-r044.md` or
`L0-r045.md` walks; the physics-object/shadow briefs are planning inputs, not confirmation.

The current corpus contains `vphysics.dll` (3506 functions, 3502 decompiled at lookup). Its
object/environment tables are not registered in corpus vtable metadata. Constructor stores and
read-only PE tables establish the dispatch below; “no function matches” on several small bodies
means missing Ghidra boundaries, not absent implementation. Reads used `vtmb_code`, `vtmb_where`,
`vtmb_vtable`, and a scratch Capstone PE reader at
`E:/elysium-work/codex/l0-physics-producer-20261010/pe_read.py`. No corpus or Ghidra project writes.
All `0x100...` addresses below are **vampire.dll**, all `0x260...` are **vphysics.dll**.

## Owner-approved engine ownership and revised dependency plan (2026-10-10)

This plan supersedes the earlier requirement to clone the IVP construction/core/shape chain.
The owner explicitly applies `docs/vision.md`'s ownership test: Unreal owns geometry, rigid-body
mechanics, broadphase/contact solving and internal scheduling. Retail remains the oracle for
authored inputs, game-visible rules, lifecycle and event order. No packed topology, feature ID,
support tree, allocator or IVP core scheduler is required without a named authored observer.
The compact box reconstruction had only implementation/test consumers. The separate removal
packet verified that inventory and removed its header, implementation, two tests and packed
game-derived box golden. The typed authored surface service/import and ignored semantic
expectations remain; no replacement adapter or body/contact closure is implied.

1. **Body adapter inputs and lifetime.** Actual callers100a6150/100a5d80/1002dfe0/1002e3a0
   supply authored bbox/model geometry, pose, mass/damping/inertia/material parameters and body
   flags. Feed these to Chaos-owned shape/body services; retain substrate body identity, entity
   registration/swap, activity queries, deferred removal and ordered publication. Missing inputs
   are exact authored parameter conversion, existing baked model collision availability and
   adapter create/destroy/query outputs; IVP shape/core construction is not an input gap.
2. **Query/contact rule seam.** Preserve group/pass-entity/owner predicates, GetDamageType/rules
   hull choice, bilateral veto priority and ignored-pair mutation notifications. Chaos owns
   candidate generation and mechanical contacts. Map Chaos results to game-visible callbacks
   in retail order; no Source broadphase/hash mechanism is required to find those candidates.
3. **Shadow controls.** Store submitted targets separately from solved pose; preserve permission
   flags, maximum speeds, thresholds, gravity modes, timing and blocked feedback. Chaos supplies
   the motion mechanism. Solve/publication clocks and activity/deletion lifecycle must be wired
   through an explicit adapter; an immediate UE teleport is not the submitted-target contract.
4. **Game simulation order.** ActualPhysFrame is post-think. NPC shadow slot47 samples before
   STEP trace/movement; think type2 then1 precedes slot46 target submission. Base inherited slot
   229 is empty. Preserve pusher transaction/output order against Chaos query/motion results;
   any still-unknown cvar/timer producer remains a named rule gap, not an invented internal loop.
5. **Bounds/partition/cache last.** Consume real backend shape and solved pose, preserve the
   game-level dirty/relink lifecycle, and call query callbacks before their mask gate. Only then
   resume r022a cache/parent frame and remaining absolute setters; localSetOrigin invalidates.

The current surface packet qualifies because authored MDL/VMT surface names select it, authored
friction/elasticity/density/thickness/damping affect physical feel/body parameters, movement and
gamematerial flags feed game rules, and sound slots/matrix choose audible authored events.
Implicit defaults, base position and repeated keys govern interpretation of those authored
inputs. Keep these as typed fields/names/indices, not an IVP memory-layout API. Existing audible
consumers remain on the older divergent assets. The selected-model body adapter now consumes
typed friction/elasticity/density/thickness and authored body parameters; contact response,
movement and audio consumers remain open. Native row
bytes/template pointers are research evidence only and do not establish full material/body
closure. Shipped expected data must be generated into ignored deployed artifacts; tracked tests
contain only project-authored boundary inputs and code.

The known retail consumers are concrete: MDL `SurfacePropIndex@392` and VMT `$surfaceprop`
name the row (format contracts `mdl_v2531.md`/`texture_format.md`); constructor template26018ba0
selects its material, whose26015b20/26015b30/26015b60 read friction/elasticity/dampening.
NPC1026d460 caches surfacedata for steps, and player `UpdateStepSound1011e940` reads
gamematerial for dry-step volume. `CGameMovement101234d0` reads climbable, though shipped movement
does not reach its ladder arm. Remaining movement/matrix/audio factor consumers are unwired or
need their own later game-rule walk; no audible or Chaos material adapter closure is claimed here.

## Factory, environment and exact service identity

`CPhysicsHook::Init 0x1002d7d0` obtains the factory and requests `VPhysics030` into
`DAT_10723934`, `VPhysicsCollision007` into `DAT_1072393c`, and `VPhysicsSurfaceProps001` into
`DAT_10723944`. It parses `scripts/surfaceproperties.txt` through the surface service. The
VPhysics factory `0x26001dd0` returns singleton `0x260e4d1c`; ctor `0x26001ba0` installs
table `0x260c91a8`. Service slot 0 `0x26001bd0` allocates/constructs an environment through
`0x260116b0 -> 0x2600f920` and appends it to the service-owned environment vector.
The environment constructor installs table `0x260c9998`, builds the IVP environment through
`0x260393e0`, and stores its owning wrapper at IVP-environment `+0x174`.

`LevelInitPreEntity 0x1002d930` assigns the environment to `DAT_10723940`; installs factory and
physics-event handlers; enables the deferred-delete gate (slot 47); sets timestep via slot 29
(literal `0x3c75c28f`); sets gravity; builds the world object through `0x1002e140`; and constructs
the serial-handle shadow registry through `0x100d1230`, published at `DAT_106b89bc`.

| Receiver/table | Slot | Body | Recovered role |
|---|---:|---|---|
| Environment `0x260c9998` | 7 | `0x260104f0` | Create dynamic polygon object |
| Environment | 8 | `0x260105d0` | Create static polygon object |
| Environment | 20 | `0x26010f80` | Destroy object immediately or queue deletion |
| Environment | 26 | `0x26010900` | Simulate, including deletion/activity handling |
| Environment | 35 | `0x26011360` | Create shadow controller |
| Environment | 36 | `0x26011380` | Destroy shadow controller |
| Environment | 44 / 45 | `0x26011440` / `0x26011450` | Active count / objects |
| Environment | 46 / 47 | `0x260114c0` / `0x260121f0` | Delete-list cleanup / gate |
| Object `0x260c9db0` | 4 | `0x26016cf0` | Marked-for-deletion byte `+0x35`, **not sleep** |
| Object | 6 | `0x260173c0` | Entity game-data pointer |
| Object | 11 | `0x260173d0` | Mass setter |
| Object | 22 / 23 | `0x26016ea0` / `0x26016ee0` | Gravity / drag enable |
| Object | 37 | `0x26017eb0` | Live position/angles |
| Object | 45 | `0x26018520` | Configure/create shadow controller |
| Object | 46 | `0x260185b0` | Update shadow target and gravity mode |
| Object | 47 | `0x26018640` | Sample shadow body pose at simulation clock |
| Object | 48 / 51 | `0x260186d0` / `0x26018600` | Get / remove controller |
| Controller interface `0x260ca098` | 1 | `0x2601aad0` | Target writer |
| Controller interface | 2 | `0x2601ac40` | Speed limits |
| Controller interface | 4 | `0x2601aa40` | Teleport-distance setter |
| Controller interface | 5 / 6 | `0x2601a700` / `0x2601a710` | Movement / rotation permission |
| IVP controller `0x260ca0c8` | 4 / 5 | `0x2601aa50` / `0x2601a6f0` | Simulate / priority 500 |

These are two controller subobjects, not one table: `0x2601b0e0` allocates `0xb8` bytes,
ctor `0x2601a670` installs the IVP table at base and the public interface at base+4.
Environment slot 35 returns base+4. Interface deletion `0x2601b250` adjusts `this` by -4.

## Real construction, reference and registry

`VPhysicsSetObject 0x100a6490` warns on nonnull-over-nonnull and assigns `m_pPhysicsObject
(+0x36c)`. `VPhysicsSwapObject 0x100a68f0` removes the shadow-registry entry when the incoming
object is null, warns if the old object is null, then assigns. Neither creates a body or destroys
the old body. A reference plus a bool-presence fixture is not their production input.

`VPhysicsInitSetup 0x100a5ad0` requires the entity's edict (`+0x2e0`), destroys any existing object
through slot 225, and relinks. `VPhysicsInitShadow 0x100a6150` then rejects solid type 0 and reads
absolute origin followed by absolute angles. Solid type 2 builds a box through
`0x100a5d80 -> 0x1002dfe0`, collision-service slot 19 (`BBoxToCollide`). Other solids use
`PhysModelCreate 0x1002e3a0`: engine model-collision slot 58, parsed solid/object parameters and
surface identity. Its missing-model-data/debug/removal arms must not be replaced by an arbitrary
capsule. The custom path `0x1002e7e0` supplies defaults from `0x1046ecb4`, surface lookup, name and
entity game data to dynamic/static environment creation. Its decompiler aliases arguments and
locals; complete parameter-layout/static-selection recovery still needs asm before porting.

On success `0x100a6150` assigns the object, calls object slot 45 with speeds `10000.0f` and the
two permission arguments, submits slot 46 `(origin, angles, false, 0.0f)`, then adds the entity to
the registry (`0x1002edd0 -> 0x100d1290`). Registry insertion deduplicates resolved handles.
Removal (`0x1002ee60 -> 0x100d13d0`) also clears stale handles. These are entity handles with
serial validation; environment-owned bodies are a separate collection.

Environment dynamic creation `0x260104f0` calls `0x260189f0` and appends the result to its owned
vector. `0x260189f0` constructs an object (`0x260168e0`, table `0x260c9db0`), converts collision
shape through `0x26009a00`, creates the real IVP object through `0x260397c0`, initializes it
through `0x26016930`, and applies physical parameters. The body at object+8 is independent of
the controller at object+0xc and the entity's logical transform.

## Target versus solved pose

`SetShadow 0x26018520` reuses an existing controller by changing speeds. Otherwise it changes
object flags, retrieves the owning environment through `0x26016db0`, creates the controller via
environment slot 35, sets speeds, disables drag, and conditionally disables gravity when physics
movement is forbidden. Controller ctor helper `0x2601a7e0` saves core parameters and attaches
the controller to the real simulation scheduler; `0x2601a920` restores them on destruction.

Asm fixes `UpdateShadow 0x260185b0` (object slot 46): its args are origin, angles, bool, interval.
On bool change it stores object+0x34 and enables gravity iff the bool is false, provided there is
no controller or its AllowPhysicsMovement slot 5 is true. With a controller it calls interface
slot 1 with origin, angles, interval. Without a controller it does not publish a target/pose.
There is **no instantaneous body teleport** in this function.

Target writer `0x2601aad0` converts Source origin to IVP `(X,-Z,Y)*0.0254`, converts angles to
quaternion through `0x26001270`, stores `max(interval,0)`, sets its active bit, compares old/new
position and quaternion, and wakes the real object on a changed target. Position comparison
`0x2601aba0` uses squared distance tolerance at `0x260ca118` (about `1e-8`); quaternion comparison
`0x2601abf0` uses sum of absolute component differences. Target storage is not simulation.
Speed setter `0x2601ac40` converts linear/angular units and combines requested limits with the
core's current velocity magnitudes; replacing it with a fixed interpolation rate loses state.

Controller IVP callback `0x2601aa50` uses the simulation event interval, invokes `0x2601a250`,
and counts remaining arrival time down to zero. With no active target it clears previous
velocity state. `0x2601a250` reads actual pose/core velocities, computes arrival fraction,
applies teleport-distance handling through `0x2602f0b0` (temporarily disabling collision for
the object's `0x300` flag arm), and sets linear/angular velocity through `0x26019600`.
That velocity helper has zero-speed, clamp, damping and additive-correction arms; it is not a
target-copy operation. Some quaternion/decompiler aliases still require asm before a port.
Attachment chain `0x2602ca30 -> 0x2602cb90 -> 0x2602cbe0` registers per-core controllers;
`0x2602ccd0` sorts by priority; `0x2602d090` walks controller records backwards and calls IVP
slot 4 before later core/contact work. This is a simulation event, not an actor-animation tick.

NEW correction: NPC `0x10272f40` constructs shadow `(allowMove=true, allowRotate=false, ...)`,
sets model mass or male/female fallback 90/65, and calls controller slot 4 with
`(GetSize().x + GetSize().y) * 0.5f * 0.5f`. Asm `0x2601aa40` converts that to metres and writes
the teleport threshold. Current `BuildVPhysicsShadow` labels `(X+Y+Z)*0.5` squared as damping:
that metadata is not this retail setting, and changing metadata alone would not create a body.

## Frame, pose consumer and destruction clocks

`CPhysicsHook::FrameUpdatePostEntityThink 0x1002dd70` calls `PhysFrame 0x1002ea40` with host
frametime, then queued collision work `0x1002cd50`. `PhysFrame` with no environment returns.
It replaces dt outside [0,1] with zero, caps dt at 0.1, multiplies the cvar read through
`DAT_106b85c4`, and caps again at 0.1. The cvar's name was not established by this read.
It sets `DAT_106b8814`, calls environment slot 26, obtains slots 44/45 active objects and visits
them forwards: nonnull object game data gets entity slot 226 (`VPhysicsUpdate`). Next it visits
the shadow linked list forwards, serial-resolves handles, and calls slot 229 only when the entity
has an object and object slot 4 is not marked for deletion. Finally it clears the in-simulation
flag and calls `CCollisionEvent::FrameUpdate`.

Environment simulation `0x26010900` first removes marked bodies from activity, checks its own
dt limits, caps at 0.1, marks environment+0x61 during IVP work, and advances through
`0x26039610` or `0x26039650`. It drains deferred deletion when environment+0x62 permits.
Exact fixed-step/clock constants, subsequent contact-event production and activity transitions
are not closed by this targeted read. PhysFrame ordering alone cannot supply those producers.

`VPhysicsUpdate 0x100b4f30` dispatches movetypes 1/8 to `VPhysicsUpdatePusher 0x100a55b0`;
movetype 7 reads object slot 37, checks finite pose, calls absolute setters, touches triggers,
and relinks children. Others do nothing. Pusher feedback reads object slot 47, compares the
solved body pose to the proposed entity motion, can rewind local pose/time, and reads contact
slot 44 to obtain the blocking entity before Blocked/MoveDone. Copying the target would hide
this blocked state. NPC slot 229 is inherited empty `0x100273d0`; it is not an NPC pose writer.

`VPhysicsDestroyObject 0x100b5040` removes the shadow entry, calls `0x10158520` to forget the saved
body recipe and invoke environment slot 20, then clears the entity reference. `0x26010f80` removes
the owned-list entry immediately; if environment+0x61 or +0x62 is set it marks object+0x3d bit 4
(literal OR 4) and queues destruction. Otherwise it removes the active entry, repairs the
swapped active index, and invokes the deleting destructor. `0x26011240` drains queued objects.
Object destructor `0x26016c10` first removes its controller (`0x26018600 -> env slot 36 ->
0x26011380 -> interface deletion -> 0x2601a7b0/0x2601a920`) before IVP body destruction.
`LevelShutdownPostEntity 0x1002dc50` clears collision/event state, destroys the environment
through VPhysics service slot 1, clears its global, and destroys/frees the shadow registry.

## Exact lower inputs and repaired implementation boundaries

### Independent actual model-body reference packet (2026-10-11)

Only `VPhysicsSetObject 0x100a6490` and base `VPhysicsGetObjectList 0x100a6ad0` are selected
complete contracts. Set warns whenever both references are nonnull, including identical references;
it assigns on every arm and neither destroys the old body nor registers a shadow. The list tests
stored nonnull identity then signed capacity >0; success writes only output[0] and returns1. Failure
returns0 without touching output. Null output with admitted body/capacity is not a safe retail arm.

The actual prepared PhysProp factory supplies a live Chaos actor (`FSingleParticlePhysicsProxy*`),
not the component or the reusable FBodyInstance address. Successful native creation publishes it;
failed recreation, source absence, model replacement, hide/break, Kill and world teardown retire it.
Reference clearing alone never destroys an aliased actor. No identity assertion requires allocator
addresses to differ across recreation. Map `prop_physics`'s authored model/first solid and gameplay
Wake/Break/constraint attachment supply the current observer; base list is the actual single-body
enumeration used by physics/save consumers. Remaining save admission/serializer remains open.

**Correction to the older destruction description:** `PTR_DAT_1053c5e0` points to106bda64, whose
constructor10043a00 installs `CPhysSaveRestoreBlockHandler_at4` table10449420. Destroy helper
10158520 calls this IPhysSaveRestoreManager slot3 thunk1000ed68->10045110, then environment#20.
It is not collision-pair removal. Model creation1002e3a0 registers (body,model) on success through
manager#2 thunk10012f12->10044db0. Removal10045110 searches the model map first and removes its
first matching body entry, returning; absent model entry it searches/removes the bbox map. Pointer
comparators/tree/free-list internals are not substrate requirements. The saved model/bbox recipe
association and its lifetime are the observable requirement for reconstructing saved body geometry;
that physics-save registration/consumer chain is not implemented by this reference-only packet.

Swap100a68f0 and generic225 remain open: null Swap removes shadow membership before warning on
absent old reference and assigning;225 with body removes shadow membership, retires saved recipe,
requests body deletion and clears reference. The selected actual model producer has no shadow
controller/membership; missing generic shadow callers are not required to fake one for it. Chaos
owns contact removal/body release mechanics. No Source registry/tree/hash/allocator reconstruction,
fake pair cleanup or full generic225 closure is implied. Solved226 still requires the complete
absolute-setter, trigger-touch, child-relink and pusher feedback chains in retail order; direct
component-to-Origin publication and the deferred r022a candidate are not introduced.

The existing components are useful engine algorithms, but do not already implement this service:

| Host | Existing production | Missing service contract |
|---|---|---|
| `AElysiumNpcBody` | CharacterMovement/capsule, actor pose sampled by `SampleTransform` | Independent rigid body + target controller, owned activity/delete lists; Teleport also calls Stop |
| `UElysiumMovementComponent` | PlayerMove on command dt in TickComponent | Environment simulation clock and player shadow controller handoff |
| `FElysiumPhysProp::Visual` | Cooked Chaos collision/mass and world simulation | Generic object identity, ordered active callbacks, shadow permissions/limits and deferred deletion |
| `FElysiumPhysicsSourceData` | Resident exact hull/solid/parameter evidence | Creation-time retail default/override/surface conversion and live object service |

Construction does **not** depend on the caller's think loop: the authored-input/body-lifecycle
boundary can land before STEP. Chaos owns shape construction and solving; the substrate keeps
identity, parameters, activity/deferred removal and ordered game publication. Subsequent packets
complete query/contact rules and shadow target **and solved state** before their STEP callers.
A box-body subset cannot claim complete `VPhysicsInitShadow` while its model/custom/failure
arms are absent. Preserve the externally observable contract and event/state sequencing; no
IVP-copy requirement or divergent sequencing is accepted. Current adapters and this targeted
recovery do not establish those complete boundaries; a pointer or target store is insufficient.

The live NPC loop adds specific L0 prerequisites, not a general “physics missing” assertion:

- `PhysicsStepTroika 0x1003a610` runs `PhysicsRunThink(2)` (named only), samples object slot 47
  before STEP under its cvar/parent/time gates, and reconciles that pose through `0x10035030`.
  It then runs `PhysicsStepRunTimestep 0x1003b190`, `PhysicsRunThink(1)` (primary only), water
  transition, and submits slot 46 only if logical origin changed. Its alternate timed arm also
  submits after the primary think. Exact omitted call args need asm for implementation.
- `0x1003b190` reads mask slot 237 `0x1003e7a0`, performs `PhysicsTryMove 0x10034590`, ground
  corner traces and `PhysicsTouchTriggers 0x100a53d0`. The independent mask selector is now
  implemented: `0x2000` flag -> `0x202400b`, else nonzero +0xa8 -> `0x201400b`, else
  `0x200400b`, preserving first-arm precedence. It does not require an object or think scheduler.
- `0x10035030` selects game-rules slot 76 when GetDamageType entity slot 189 is nonzero, or builds a
  centered ray, including cylinder flag for collision flags bit 0x40. It constructs
  `CTraceFilterSimple 0x101d3190` with owner and entity collision group +0x368, then engine trace
  slot 4. Filter `0x101d31c0` reads candidate ShouldCollide slot 91, game-rules group-pair slot
  77 and bilateral entity slot 68 vetoes, in addition to StandardFilterRules/pass entity.
  `FElysiumRetailTrace` cannot represent group/rules-slot-76/cylinder; MapActor's rejection callback
  supplies only render/solid-flag arms. Generic box sweeps alone are not the required producer.
- `PhysicsRunThink 0x10033f60` distinguishes named-only/primary-only modes, sets current named
  index around callbacks and uses `PhysicsRunSpecificThink 0x10033de0` for stamps/deletion.
  Current `RunThinks` runs only the primary callback at NextThink<=Now, while retail specific
  think permits one-frametime early execution. r024a storage is not this dispatch production.

Implementation order follows the approved plan above, independently of historical run slices:
1. Authored body inputs, Chaos-created body identity/lifecycle/activity and ordered timing.
   Use resident cooked hull/parameter data after default/override/surface semantics are walked;
   no retail compact box/core construction is needed. Slot 237 is already complete independently.
2. Chaos query/contact outputs with full shape/filter/result rules and ordered mutation callbacks.
   Reuse existing query algorithms only where their observable contract arms are proven.
3. Shadow target/solved-pose distinction, permissions, limits, blocked feedback and live lifecycle.
4. Post-think PhysFrame, NPC sample-think-submit and STEP/pusher feedback with real backend inputs.
5. Callback/bounds/partition/cache publication, then local/cache frames and absolute setters.
   Current SetOrigin-driven UE teleport and pre-think SyncMovingNpcRecords are insufficient.
Test real service-created bodies and observable outputs; fixtures cannot supply absent bodies.

Collision service factory `0x260099f0` returns `0x260f0698`, ctor `0x26009850` installs table
`0x260c9550`. Slot 18 `0x2600aad0` adjusts `this` by +4 and reaches `CollideGetAABB 0x26021db0`;
slot 19 `0x2600a310` constructs/caches bbox collision. `0x26021db0` builds a transformed shape
query (`0x2601dad0`), reads support points (`0x2601db90`) or recursive ledge bounds
(`0x26021ca0`), and expands bounds by a literal at `0x260ca288`. Geometry implementation is now
available in the corpus/PE, but this does **not** close its full geometry/creation contract or
license an assumed UE AABB. This corrects the older “outside current corpus” planning premise.

## Completed independent mask prerequisite

`PhysicsSolidMaskForEntity 0x1003e7a0` (slot 237) reads only entity flags and the player
self-downcast cache +0xa8. PE asm confirms the first literal `0x202400b` at `0x1003e81e`, the
player literal `0x201400b` at `0x1003e839`, and default `0x200400b` at `0x1003e843`.
Base construction `0x1009da80` clears the cache; real player factory `0x1034f1e0` reaches
`0x1034f330 -> CBasePlayer ctor 0x1015d7c0`, which writes `param_1[0x2a] = param_1`.
Existing registered NPC Spawn/NPCInit supplies flag `0x2000`. The port's virtual `AsPlayer`
returns null on the base and actual self on `FElysiumPlayer`; flags or class-name strings do not
manufacture the player identity. This is construction identity, not a serialized raw address:
world teardown/recreation rebuilds it through the real player factory.

`Elysium.Substrate.PhysicsMaskProduction` exercises real registered base/NPC creation,
SpawnPlayer, runtime flag changes, first-arm precedence on a player, const access, slot 237
metadata and virtual member dispatch, and world teardown/recreation. The bounded
`l0_physics_solid_mask` arena record calls the actual virtual slot on a spawned relay, NPC and
player and checks all three mask results. Gates are recorded by the coordinator in runs/progress.
Only this selector is marked ported; no generic trace, object/controller, geometry or cache
publication closure follows. No modernization is accepted. The failed r022a candidate stays
deferred; this packet replaces an unspecified service frontier with exact production and ordering
dependencies while landing one complete independent prerequisite.

## Independent damage selector/rules checkpoint (2026-10-10)

Targeted actual bodies confirm trace `0x10035030` offset `0x2f4` is **GetDamageType slot 189**,
not a custom-collision predicate; base `0x100ad850` returns zero. Nonzero selects rules slot76
`0x10128fa0` (ordinary centered box, no cylinder), not an entity callback. The complete base
selector is now hand-written. Full nonzero trace/engine geometry remains open.

Actual SP rules dispatch is `CWorld::Precache 0x1023c020 -> InstallGameRules 0x10352f70 ->
ctor 0x10355740 -> CHalfLife2 table0x104a308c #77 0x10356230 -> getter0x1042c0a0 ->
singleton0x109eed4c -> CHLSharedGameRules0x1042c0c0`, with the shared0x1042df70 rule inlined.
Both stateless predicates are complete independently; the existing NPC rules caller uses them.
Base CGameRules #77 0x10128f80 selects a different singleton0x109eff34/shared table.

The exact remaining same-L0 group/pair frontier, corrected cylinder comparisons, pass-owner
identity and bilateral order are recorded in `docs/vtmb/physics-interaction.md`, "Independent
trace selectors and actual single-player collision rules". Group setter100aaa40 needs real
object#25 26016da0 -> IVP2602e320 -> pair/contact rebuild26048b00. Anim slot68 needs
pair-hash10158b90/service#3 26011b70; its mutation notifications need that same live recheck.
No fake setter/pair producer or full filter implementation was added; the shared helper still
uses nav tester-first veto order. The special-mask production test deliberately bypasses that
unfinished tail exactly as retail does. Neither broad trace row nor original run closes.

## Historical independent bbox reconstruction (2026-10-10; removed)

`Collision007#19 2600a310` was implemented by `FElysiumPhysicsCollision::BBoxToCollide` with
real Source float endpoints, actual immutable compact surface output and explicit allocation
lifetime. Exact reverse cache2600a480, bootstrap2600a1a0, clone/ordered rewrite2600a050,
surface builder26009fd0 and append2600a3e0 were reproduced for this single-box operation.
Full point degeneracy alone rejects; plane/line/reversed/tiny/NaN arms retain retail geometry.
Seed Qhull output permutation/triangle features are retrieved bytes, not invented feature IDs.
The source representation includes the full412-byte surface/header/ledge/triangle/point/tree,
with construction-derived mass/inertia/radius/quantized bounds. No solver cooking clamps thin
boxes. Cache borrows raw pointers; DestroyCollide leaves stale identity and cache teardown does
not own shapes. No safe lifetime modernization was introduced.

Read-only x86 retail probe provenance, shape layout, arithmetic/store order and bounded36/36
full-byte comparisons are in physics-interaction.md, "Historical BBoxToCollide geometry
construction". The then-tracked16-output goldens and public operation/cache lifetime automation
were bounded constructor evidence; they were removed with the unreachable reconstruction.
There was no internal helper arena
action and no new entity/body construction record: the actual upper caller is still absent.

Historical prerequisite assessment before the owner corrected engine ownership:
It does **not** close vampire1002dfe0/100a5d80, their surface/default/callback/body arms,
Collision007#18 world AABB, geometry query, group recheck or ignored-pair mutation. The old
IVP constructor/core/shape chain remains oracle evidence only. No production consumer requires
the compact representation; the separate packet has removed it. The revised plan above
instead requires authored body inputs, Chaos identity/lifecycle/activity, ordered contact events
and shadow target/solved-state feedback with the retail game rules preserved.
The saved r022a frame/cache packet remains deferred and original run counts stay23/88.

## Independent constructor material input (2026-10-10)

Template26018ba0's SurfaceProps001#3/#10 dependency establishes an authored material observer.
Its parser/lookup input is now implemented with typed semantics, using the whole deployed
`scripts/surfaceproperties.txt` unchanged.
Factory26014be0 singleton260f0f40/table260c9c30, parser26015170, lookup26014ed0/260150e0,
copy26015140 and material row/vtable260c9d04 were recovered through corpus plus read-only PE.
The matching oracle section in physics-interaction.md records every ordered field write,
retail row layout as research evidence and scalar/sound/numeric inputs; surface_properties.md corrects the older
implicit-default/root-first/pool claims. Existing flattened audio/Chaos assets remain divergent
and are not accepted as the constructor input.

Filename/name/sound share a case-sensitive symbol table, though pair26024fd0 lowercases authored
key and value. Default copies occur at row entry; base copies occur exactly at token position.
Repeated sounds overwrite one symbol, duplicate definitions still intern but first row wins,
forward base leaves values unchanged, and first parse appends one shadow row even on empty text.
Ordinary lookup unknown=-1; #10 negative=null, >127->row0 except reserved0xf000->shadow.
Tokenizer selector260e4d11 has no writes and selects colon-enabled tableB; atof/atoi were checked
against actual vphysics VC6 helpers rather than assumed interchangeable with KvReader strtol.

`FElysiumEntityWorld` initializes the process service from project-resident deployed bytes before
entity creation and exposes its actual `PhysicsSurfaces()` service. GLB export/import preserves
the entire ordered raw source, including duplicate names/repeats/copy position. Source storage is
released after parse; table owns strings/rows, later parse may relocate borrowed rows, process
singleton survives world teardown. UFS staging is declared for only ElysiumCorpus/physics;
the relativeContent/IFileManager path was statically reviewed, without a packaged cook claim.

Read-only native DLL capture identifies the real template26018ba0 material consumption and
produces named semantic verification rows. Both compiledUE public production tests compare
all13 float fields,28 sound names, gamematerial and climbable for64 deployed plus12 project-authored
boundary rows, including lifetime behavior. Generated shipped expectations remain ignored;
tracked cases contain no shipped content. Pipeline focused5/5 and earlier bounded4 existing arena
regressions pass. Evidence/provenance is in the oracle and progress checkpoint. No helper-only
arena action or absent body fixture was added.

This completes an independent authored surface parser input, not the Chaos material/body adapter
or audible consumers. Next, recover/implement the concrete
entity callback and authored geometry/pose/mass/damping/inertia/material inputs to Chaos body
identity/create/destroy/activity and ordered postthink timing. Contact filtering/notifications,
shadow feedback and NPC sample-think-submit follow in the revised plan. IVP core/scheduler,
support-tree topology and packed IDs are not prerequisites without an authored observer.
Original runs remain23/88, broad audit rows remain partial and r022a remains deferred.

## Independent selected-model input/lifetime checkpoint (2026-10-11)

The narrow independent dependency now has actual public importer, cooked first-solid userdata,
prepared model factory and PhysProp caller coverage. Creation transfers authored pose,
mass/massbias/override, sleep, damping, relative inertia, explicit COM and typed surface values
to Chaos; native identity/activity, source-absence inert state and immediate caller retirement
are exercised. See physics-interaction.md's selected model section for recovered addresses and
entity_io.md for real key/save producer defaults. Shared static geometry remains all solids;
only the prop proxy chooses the separately cooked first solid.

This advances step1 without closing its complete packet or r044/r045. Generic publication,
registration callbacks, game-data flags, active enumeration/deferred deletion, bbox/static
callers, contact dampening/drag/rotinertialimit response and controller/timing consumers remain
open. Real native input checks prove configuration/conversion, not retail angular/contact feel.
r022a remains deferred. No original run closes (23/88 unchanged).

## Selected saved-recipe prerequisite (2026-10-11)

Confirmed1002e3a0 successful live creation ->manager#2 thunk10012f12 ->10044db0 model association;
1002dfe0 BBoxToCollide ->10044c20 bounds,1002e7e0 successful custom body ->10044f60 bbox association.
Removal10158520 ->manager#3 thunk1000ed68 ->10045110 removes first model match, otherwise bbox,
before environment deletion. These maps feed Physics Save10043c70/Restore100440c0, one-object
type1 reconstruction100443e0; physics-interaction.md/savegame_format.md record narrowed arms.

Actual selected model Spawn now registers owned inputs on actual Chaos output; ordered Physics
Save captures runtime body state, typed disposable archive persists it, and Physics Restore
recreates native geometry/state before final reconnect. Sleeping retains recipe; replacement,
failure, Kill/component destruction and teardown retire it. Base true eligibility and confirmed
false overrides are implemented. Eligible aliases reconstruct independently per field; missing native recipes fence persistence
with an explicit error. Downstream pointer graph fixups remain open. Save10043c70 itself does not deduplicate
registered headers. Native mass/current damping join pose/velocities/activity/gravity as state;
fluid enter/exit/cache restoration remains open.

Root scoped the finite bbox native service prerequisite independently of retail Spawn: explicit
min/max, surface and body parameters produce actual Chaos geometry, identity, saved reconstruction
and retirement. No guessed default object record or CalcBounds producer is used. Actual bbox
caller remains100a6150 ->InitSetup/225 +217/219 ->100a5d80 model type/surface/default ->
1002dfe0/1002e7e0 ->SetObject/shadow caps/permissions/update registry. Static/shadow selection
is unresolved. Generic Swap/225, solved226/touch/relink, contact/controller/multiobject state and
r022a cache matrix stay open. Original23/88 unchanged; coordinator owns acceptance gates.

Type1 env#50=26025400 dispatches26019450; PE26019483-260194bc allocates a new body per
destination owner field. Only afterward is old/new mapping26025280 registered (#49 initialises
at26025240). Thus eligible aliases independently reconstruct real native bodies, rather than
coalescing one body's ownership by entity iteration. Base leaves own invisible service components
and saved query profile/mask; SetObject clear remains non-destructive, selected225/Kill/destructor
retire owned reconstruction only. Downstream controller/pointer graph fixups remain open.

Saved-null arm100440c0 invokes225 for count<1. Null eligible fields persist presence=false/count0
instead of being omitted. Physics restore retires baseline Spawn's body; prop225 sets bSimulating
false and final GateBody leaves it absent. Actual Destroy ->Freeze ->archive ->ApplySnapshot checks
null field/no native actor after reconnect. Generic shadow225 stays open. Revised opening now
distinguishes completed selected-model material inputs from open contact/movement/audio consumers.
