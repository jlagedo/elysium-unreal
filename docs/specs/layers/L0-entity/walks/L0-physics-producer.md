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

`VPhysicsDestroyObject 0x100b5040` removes the shadow entry, calls `0x10158520` to forget collision
identity and invoke environment slot 20, then clears the entity reference. `0x26010f80` removes
the owned-list entry immediately; if environment+0x61 or +0x62 is set it marks object+0x3d bit 4
(literal OR 4) and queues destruction. Otherwise it removes the active entry, repairs the
swapped active index, and invokes the deleting destructor. `0x26011240` drains queued objects.
Object destructor `0x26016c10` first removes its controller (`0x26018600 -> env slot 36 ->
0x26011380 -> interface deletion -> 0x2601a7b0/0x2601a920`) before IVP body destruction.
`LevelShutdownPostEntity 0x1002dc50` clears collision/event state, destroys the environment
through VPhysics service slot 1, clears its global, and destroys/frees the shadow registry.

## Exact lower inputs and repaired implementation boundaries

The existing components are useful engine algorithms, but do not already implement this service:

| Host | Existing production | Missing service contract |
|---|---|---|
| `AElysiumNpcBody` | CharacterMovement/capsule, actor pose sampled by `SampleTransform` | Independent rigid body + target controller, owned activity/delete lists; Teleport also calls Stop |
| `UElysiumMovementComponent` | PlayerMove on command dt in TickComponent | Environment simulation clock and player shadow controller handoff |
| `FElysiumPhysProp::Visual` | Cooked Chaos collision/mass and world simulation | Generic object identity, ordered active callbacks, shadow permissions/limits and deferred deletion |
| `FElysiumPhysicsSourceData` | Resident exact hull/solid/parameter evidence | Creation-time retail default/override/surface conversion and live object service |

Construction does **not** depend on the caller's think loop: a complete owned body/controller
environment can land independently before STEP. The first real producer boundary is shape and
parameter acquisition, body lifecycle/identity, controller target **and simulation**, environment
clock/activity/deferred deletion and contact/event production together. A box-body subset may
be a separate complete service implementation, but cannot claim complete `VPhysicsInitShadow`
while its model/custom/failure arms are absent. UE may replace the internal solver if the retail
externally observable contract and event/state sequencing are maintained. No IVP-copy requirement
or accepted divergent sequencing is inferred here. Current body adapters and this targeted
recovery do not establish that complete producer boundary; a pointer or target store is insufficient.

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
- `0x10035030` chooses custom collision (entity slot 189 -> game-rules slot 76) or builds a
  centered ray, including cylinder flag for collision flags bit 0x40. It constructs
  `CTraceFilterSimple 0x101d3190` with owner and entity collision group +0x368, then engine trace
  slot 4. Filter `0x101d31c0` reads candidate ShouldCollide slot 91, game-rules group-pair slot
  77 and bilateral entity slot 68 vetoes, in addition to StandardFilterRules/pass entity.
  `FElysiumRetailTrace` cannot represent group/custom/cylinder; MapActor's rejection callback
  supplies only render/solid-flag arms. Generic box sweeps alone are not the required producer.
- `PhysicsRunThink 0x10033f60` distinguishes named-only/primary-only modes, sets current named
  index around callbacks and uses `PhysicsRunSpecificThink 0x10033de0` for stamps/deletion.
  Current `RunThinks` runs only the primary callback at NextThink<=Now, while retail specific
  think permits one-frametime early execution. r024a storage is not this dispatch production.

Implementation order must follow actual inputs, independently of historical run slices:
1. Slot 237 and its actual input producers are complete independently; complete required trace shape/filter/results production.
   Reuse existing box query algorithms where all contract arms are proven; add missing data and
   filters rather than naming the current seam equivalent. Query-time partition remains separate.
2. Complete the owned environment/body/controller boundary above, starting from retail box
   construction and the exact clock/activity/contact/deletion reads. Use resident cooked hull and
   parameter data for the model path only after its default/override/surface semantics are walked.
   Test real service-created bodies, controller target/solved-pose distinction, blocked movement,
   active callback order, dt limits and callback-time destruction; fixtures cannot supply bodies.
3. Complete required think phases and STEP/pusher/relink feedback using those live inputs, then
   implement independent local/cache frames with these consumers. Their current SetOrigin-driven
   UE teleport and pre-think SyncMovingNpcRecords cannot remain authoritative publication.

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
