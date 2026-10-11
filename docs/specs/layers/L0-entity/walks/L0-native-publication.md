# Native dynamic pose publication (2026-10-11)

Targeted next dependency packet against main `29f36060`. Reuses confirmed r022/r026/r027
transform, touch and postthink walks. This is not closure of original checklist 23/88, moves1/8
pusher feedback, NPC controllers, collision-event delivery or the full L0 physics service.
Addresses `100...` are vampire.dll, `260...` vphysics.dll. Fresh corpus code/assembly and
read-only PE walks corrected the prior producer document's object4 interpretation.

## Native output, inactivity and lifetime

`PhysFrame1002ea40` simulates, then environment44/45 (`26011440/26011450`) copies its active
object vector forwards. Each copied object's CURRENT game data (`object+0x18`, object6) receives
slot226. The object list is frozen before callbacks; entity fields are not the enumeration
producer. Object aliases do not transfer creation game data. The later shadow roster is read
after226, serial resolves each entity and reads its current object; object4 false admits229.
`CCollisionEvent::FrameUpdate` follows; this packet does not implement all of that event service.

Object4 `26016cf0` tests inactivity byte35, not deletion. `26016d00` maps0 to1 and nonzero to2;
`26016d10` clears it. Wake listener `2600fbb0` clears pending inactivity, retains an entry when
old35==1, otherwise appends a nonstatic object whose active index isffff. Sleep listener
`2600fc70` sets pending inactivity but does not remove the active entry. At the NEXT simulation
entry `26010900` removes nonzero35 entries backwards through `26010c20`, repairing the swapped
last entry's index. Thus a final sleeping solve receives226 once; sleeping shadow bodies skip229.
Deletion uses a separate object3d bit4 and environment destruction path.

Unreal owns the native scene, dirty proxy order, wake/sleep, solver and rigid mechanics. Its
`FPhysScene_Chaos::OnSyncBodies` dispatches dirty proxy outputs, including a final sleeping
transform, then `MoveComponent` with `SkipPhysicsUpdate` in EndPhysics. The adapter captures that
native activity output only in an admitted positive scene frame, EndPhysics, with the component at its
actual body pose. It expires output before every frame and after consumption. Native wake/sleep
events and reached explicit Wake/changed-shadow-target calls feed semantic active membership:
append on wake, retain when pending sleep is canceled, remove backwards with last-entry replacement
at next frame entry, freeze forwards before226. This preserves observable callback order without
reconstructing IVP backend arrays, cores or scheduling. Recovered membership order belongs to each
physics environment; the adapter keys it by native UWorld, so another world never supplies its
last-entry replacement. In the native mutation witness the pre-removal roster is
`child, grandchild, second, third, dynamic`; removing sleeping `child` replaces position0 with the
last recreated `dynamic`, leaving filtered ordinary-body callbacks `dynamic, second, third`.
Active callbacks still run for awake bodies when simulation dt is rejected/zero. Native actor
incarnations and serial-resolved live game data prevent retired/recreated output from dispatching.

## Solved pose and raw frame chain

Generic226 `100b4f30` move7 calls object37 `26017eb0`: live IVP pose `2602e560`, optional position,
optional angles via `26001310`. The latter rounds basis components to float, tests a DOUBLE .001
threshold (`260c91a0`), and writes pitch/yaw/roll in float degrees (`260c919c`). The normal roll
is atan2+180; the degenerate arm writes literal180 and uses the alternate yaw basis. This differs
from entity MatrixAngles `10137ed0`, whose degenerate roll is0. The adapter reflects native
Chaos basis into Source coordinates before this exact conversion; UE Rotator's branches are
not substituted. The independent bridge `260012a0` maps raw IVP r0/r2/-r1 into Source matrix
row0, r8/r10/-r9 into row1, and -r4/-r6/r5 into row2. Thus raw0/raw8 supply xy,
raw4 pitch, raw6/raw5 roll, and raw2/raw10 degenerate yaw. No angle normalization is added;
atan2 signed-zero and quadrant outputs are retained. `100b4f72..4fb6` checks angle x,y,x for
nonfinite and warns without rejecting.
Then216 origin,218 angles, physical trigger query, and `PhysicsRelinkChildren100342c0` occur.
Move0 is empty. Moves1/8 dispatch100a55b0 and remain explicit unsupported diagnostics.

216 `100b2300` computes the prior absolute frame, invalidates0x10800 descendants, marks partition
dirty, clears0x800, writes absolute origin/frame translation before parent inversion and the
changed-only local/network write. 218 `100b2510` similarly invalidates0x800/child0x3000 and writes
absolute angles/raw frame before inverse parent composition, without the partition call.
`CalcAbsolutePosition100b1ac0` clears dirty first, rebuilds its local frame, composes the parent's
RAW stored frame, then stores reduced angle words. `GetParentToWorldTransform100b2150` resolves
serial/dirty parent and returns that frame, preserving small components through near-gimbal
child/grandchild composition. Attachment byte/slot262 is the existing unreached L1 hook.

Actual native creation/restore enables separate local and absolute words, coordinate frame,
logical pose, submitted shadow target and native solved pose. Retail absolute/local writers do
not teleport the native producer. Current object null/aliases cannot select an eager host
callback. Explicit host runtime setters seed the frame; broader unparented NPC CharacterMovement
sampling/folded host producers remain open and do not acquire native-local mode merely by name.
The previous broad r022 cache candidate remains deferred; this packet does not close its scope.

Physical query `100a53d0` asks engine edict geometry independently of mutable object fields.
SOLID_BBOX uses authored axis-aligned bounds; SOLID6 uses actual native component geometry.
Pair links precede eligibility. Recovered101c5460/101c5420 now admits the reached movetype7
toucher when trigger spawnflag8 is set, independently of its current object field. The existing
player/filter path remains; derived-type fallback and other activator classes remain open.
Child traversal100342c0 touches solid/trigger, reads current
movetype and calls232 unless7, recurses, then rereads its live peer. 232 `100a69e0` reads angles
before origin and submits object46 with false and frame interval. Deferred untouch and EventQueue
remain the existing postphysics frame tail. Full contacts, all child removal callbacks, NPC,
fluids, nonbbox/default creators and multiobject graphs remain open unless independently tested.

## Witnesses and generation preservation

`Elysium.Substrate.ModelPhysics.DynamicPublicationFrame` uses the real GameWorld/session/pawn/Map
Gameplay -> Chaos -> PostMove path. It verifies consecutive native frames with full angles and a
null owner current field plus an alias; actual geometry enters/leaves a move7-enabled trigger;
child232 targets the current pose and a same-frame queued observer follows publication/touch;
the leaving physical query queues a separate observer which sees deferred untouch completed.
Actual parent/child/grandchild near-gimbal targets retain the raw frame's small components.
Object37 identity roll360, negative angles/quadrants, both sides of the xy threshold and full
nonzero pitch/yaw/roll exercise the recovered converter on real native poses.

Authored native sleep thresholds drive an automatic final sleeping solve: one226 on that final
output, none on subsequent frames. Native sleeping229 is excluded and native wake re-admits it.
Three ordinary bodies exercise wake order, alias deletion/recreation from an earlier callback,
stale frozen-incarnation suppression, next-frame new incarnation dispatch and backwards sleep
removal order. Zero/.0001/negative scaled dt retain awake callbacks while actual scene time and
body pose stay fixed. A real auxiliary preview physics world stays awake throughout removal in
the game environment; cleanup leaves the main session, native identity and cvar intact.
An admitted positive frame plus game-side SkipPhysicsUpdate outside native EndPhysics does not
produce a spurious sleeping-body callback; frame expiration also prevents stale zero-dt output.

Actual registered generation exposed previous hand edits in the generated file:128 was reset
to false,225 to empty teardown and233 to a stub, causing native lifetime/save failures and a
save witness crash. Corrected RETAIL_DEFAULTS128/CHAIN_HAND225/233 and hand-target225 preserve
the existing implementations, now housed in SlotBodies. The generator test checks every hand
body remains outside generated definitions and specifically preserves128/225/226/232/233.
No retail ledger was regenerated. Another intermediate native run used a second game-session
fixture whose front-end tore down the main session; the auxiliary witness now creates a native
preview physics world without BeginPlay or another Elysium session. Failed intermediate runs
are retained; authoritative final gates are recorded in progress.md/runs.md.
