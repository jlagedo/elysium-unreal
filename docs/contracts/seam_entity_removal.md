# Entity removal and native lifetime

The authored `Kill` input (`100acef0`) calls virtual119 (`100acf90`), which enters
`UTIL_Remove101cd940/101cd8c0`. `FElysiumEntity::Kill` uses `QueueRemove`: guard the
pending bit, latch it before virtual180 `UpdateOnRemove`, clear the targetname, and
append the still-registered handle. Reentry/repeated inputs do not repeat180.
Bare `SetEFlags(1)` is already pending and does not acquire a queue entry.

Base180 (`100a47d0`) dispatches virtual225 before name clearing and append. Its
**current** native body therefore retires during the input. A legitimate180 override
omitting base retains that body until release. A creation-owned carrier detached
or replaced through SetObject remains producer-owned and retires in the derived
terminal release. Removal does not imply damage, outputs, inventory drops or
ragdoll creation.119/queue do not clear think words or cancel queued outputs.
Captured handles remain available until purge; targetname lookup stops after180.
Pending SAVE captures identity/bit and current field; restoring the bit does not
reconstruct the transient delete queue or synthesize180.

`PurgeEntityRemovals` implements `100f6ce0`: Unreal's public
`FPhysScene::WaitPhysScenes` barrier, then insertion order with a live count. A
release callback can append a tail entry released in the same pass; nested purge
cannot recurse. `ReleaseEntityRemoval` invokes existing class destructor recipients
and `ReleaseForRemoval`, shared by immediate removal and native absent-set restore.
The host retains dead tombstones instead of Source allocator/list/networkable
mechanisms. Native references/GameData and owned components retire at terminal
release. Callback teardown cannot resume writes to the retired identity or drain
the queue of a separately created host.

Native wrapper destruction clears GameData/callback participation before backend
destruction (`26016c10`), suppressing listener End and retaining entity sentinel
links. Entity destruction (`1009df20 ->1003d8f0`) separately notifies each surviving
OTHER side's End176, including its live authored-parent relay, then frees the
outgoing links without an own End. The accepted physical adapter continues to drain
captured FIFO records before226/229; pending Kill does not cancel those records.

`RemoveImmediate` implements `101cd970`: virtual116 guard, latch,180, shared
terminal release, without119/name-clear/queue append. Unnamed `CLight::Spawn` uses
it without `CLight::Kill`'s TurnOff. Native snapshot absent-set records are terminal
statements using the shared release directly. Existing NPC/sequence SUB_Remove
sites now enter the queue once; existing interesting-place/physics-animation-link
immediate sites use RemoveImmediate. These are service adaptations; their higher
gameplay remains owned by those layers.

Witnesses: `ModelPhysics.DeferredKill` loads the shipped tutorial relay
`logic_kill_elev_doors` and `tutwareelevdrf` door, services the original OnTrigger
Kill row in real Tick, and observes225 before post-I/O purge. Solved bottle contacts
prove pending links, suppressed wrapper End and other176/parent at purge.
`DeferredKillEpoch` proves bare latch, preserved think words and180/release world
teardown fences. `PendingRemoval`, the owned-release fixtures, `RemovalSpawn` and the physical
event family retain overriding180, live-tail purge, detached ownership and typed
pending restore. Existing `l0_native_physical_touch` and `l0_physics_object_contract`
arenas cover the affected native contact and body services.
Main acceptance receipts are separate.

Affected consumers preserve the same distinction: cine adoption/EndShot clears the view slot
immediately while outgoing disposable entities await purge; DeleteItems clears inventory
slots while item entities await purge; stale/dead fixtures establish the terminal fence before
testing refusal. Missing-classname maker1034b160 clears its name, then the negative DispatchSpawn
fence purges it before Load returns; the fixture captures identity through its immutable authored
record. CSceneEntity18010080a80 cancels playing scenes before10083a60 cleanup and
base180, so OnCanceled remains deliverable before terminal caller invalidation. Existing host
cast/clip/event cleanup and scene-reference release are retained; full10083a60 actor285,
specialized recipient246 and nested-scene cleanup remains owned by L5.

Final arm baseline repair covers the 23 failing tests in 20 existing fixture files, with
no production changes. Original terminal/refusal outcomes remain checked after actual purge,
and pending identity is observed first. The native query lifetime adversaries explicitly use
101cd970 immediate removal. The I/O fan-out observer sees the classname fallback after
queue-phase name clearing. The failed main arm receipt is retained with the run evidence.

Remaining E2: CNotify target/pair, Aim deletion and PhysicsAnimLink contracts;
full arbitrary class/listener disposition is not inferred. Wider P1/P2/P3/native
query/material and full L0 gates remain open.
