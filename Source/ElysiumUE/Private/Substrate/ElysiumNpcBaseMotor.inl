// `CAI_BaseNPC`'s declarations of the `Motor` family (story 5 step 5),
// moved from `ElysiumNpcMotor*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseMotor.cpp`.

//
// Retail's `CAI_Navigator` as much of it as this family's four rows reach. It is declared as a
// nested type rather than a free `FElysiumNpcNavigator` in a file of its own because four one-line
// retail bodies do not justify a new substrate class, and because the five navigator words are
// already CHAIN rows onto the motor — a second owner for them would be a second answer to the same
// question. **Named decision**, stated here and in the story report.
struct FNavigator
{
	// `CAI_Navigator+0x18` — the native navigation type. `FUN_1027d990` reads it (29 direct callers,
	// the widest read in this family) and `FUN_1027d9b0` writes it through `0x102eeba0`. The port's
	// `EElysiumNpcNavType` is the same four-value vocabulary (Ground 0, Jump 1, Fly 2, Climb 3), so
	// the write is pushed on to `IElysiumNpcMotor::SetNavigationType` as well as stored.
	int32 NavType = 0;

	// `CAI_Navigator+0x1c` — set to 1 by `OnNavFailed` (`0x102eeae0`, `CAI_Navigator#10`) and by
	// `OnNavComplete` (`0x102eea90`, `CAI_Navigator#8`): the "this route has ended" latch; no
	// consumer in this substrate reads it yet.
	bool bNavFailed = false;

	// `CAI_Navigator::vfunc3` (`0x102ecb50`) copies three of the owner NPC's own pointers —
	// `m_pMotor` (+0x5d44), `m_pMoveProbe` (+0x5d40), `m_pLocalNavigator` (+0x5d38) — into
	// navigator+0x20/+0x24/+0x28 and stores its argument at +0x2c. The three pointers do not exist
	// here, so what survives the port is the FACT that the snapshot was taken and the argument it
	// was taken with. Read by the test and by nothing else.
	bool bSnapshotTaken = false;
	int32 SnapshotArgument = 0;
};


// `CBaseAnimating::m_hIgnoreCollisionEntity` (+0x055c) — the single entity
// `CBaseAnimating::IsIgnoreCollisionEntity` (`0x1008be20`) compares against, and the tail every
// `ShouldIgnoreCollision`/`NavIgnoreCollision` arm falls through to. Nothing in this runtime writes
// it yet (the visual layer states the same fact for bodies at `ElysiumNpcBody.cpp:843`), so the
// tail answers "not that entity" for every candidate.
FElysiumEntityHandle IgnoreCollisionEntity;

// +0x1568 `CAI_BaseNPCTroika::m_eHull` — the STANDING hull. It sizes the collision box and every
// trace and line-of-sight helper resolves its extents from it (`CNPC_VWerewolf::GetGroundpoint`
// `0x103d6a40` among them). Below the shape map's band, so 29b did not bind it; the extents
// themselves come from the generated table (`RetailHullExtents` below).
//
// Both words are written by CONSTRUCTORS, as retail's are: this default is `CAI_BaseNPC`'s zero
// (`0x1027c300`, store `0x1027c574`), and each class whose retail constructor stores a hull writes
// it in its own (`FElysiumNpcCamera` ... `FElysiumNpcTzimisceRunner`; the generated
// `ElysiumRetailHulls::ClassHulls` is the table they are tested against). A class with no store
// of its own keeps its nearest ancestor's (`navigation-jump-links.md` § "The two hull words").
int32 HullKind = 0;

FNavigator Navigator;

/** `thunk_FUN_1026e940` / `(*DAT_1070b254)->TraceRay` — the engine hull trace `CheckOnGround`
 *  (`0x1026e5e0`), `ValidateNavGoal` (`0x10280360`) and `GetGroundpoint` (`0x103d6a40`) run, all
 *  three with mask `0x202400b`. **SEAM**: nothing in this substrate traces a hull for the kernel;
 *  answers false, which every caller reads as retail's CLEAR trace (`fraction == 1.0`) and no hit
 *  entity. */
struct FKernelHullTrace
{
	float Fraction = 1.f;                  // trace_t +0x2c — 1.0 is "nothing in the way"
	FElysiumEntityHandle HitEntity;        // trace_t::m_pEnt
	FVector PlaneNormal = FVector::ZeroVector;  // trace_t +0x18 plane.normal
	bool bAllSolid = false;                // trace_t +0x36 allsolid
	bool bStartSolid = false;              // trace_t +0x37 startsolid
	FVector EndPosUnits = FVector::ZeroVector;  // trace_t +0x0c endpos (the seam's clear answer: the end)
};

/** Which of a hull row's two extent pairs is being asked for.
 *
 *  Retail's table carries both for every hull, read by four accessors: `0x102d6100` / `0x102d6120`
 *  answer the FULL box and `0x102d6140` / `0x102d6160` the SMALL one. They are not a scale of one
 *  another -- TZIMISCE1 and TZIMISCE2's small boxes are WIDER than their full ones -- so the
 *  choice is the caller's and has to be stated rather than inferred from the hull id. */
enum class EElysiumHullExtents : uint8
{
	Full,    // 0x102d6100 / 0x102d6120
	Small,   // 0x102d6140 / 0x102d6160
};

/** `CAI_BaseNPC::FUN_1027dc80` `0x1027dc80` — the BASE branch of slot 531 `OnObstructingDoor`. The
 *  Troika-line body slot 531 carries is `0x102984a0` and belongs to story 29d, so this lands as a
 *  named method rather than as a second definition of the generated virtual. */
enum class EObstructingDoorResult : int32 { Ok = 0, Illegal = -1 };

/** `FUN_1027d990` — `return m_pNavigator->field_0x18;`. The most-called body of this family. */
int32 NavGetType() const;

/** `FUN_1027d9b0` → `0x102eeba0` — `m_pNavigator->field_0x18 = value`, and the one push onto the
 *  port's mover, which IS this runtime's navigator. */
void NavSetType(int32 Type);

/** `CAI_Navigator::vfunc3` `0x102ecb50` — the owner-pointer snapshot above. */
void NavSnapshotOwnerPointers(int32 Argument);

/** `CAI_Navigator#10 OnNavFailed` `0x102eeae0`, and `CAI_Navigator#9` `0x102eeb50`, whose whole body
 *  is a tail-jump to slot 10 with its second argument shifted down one stack word — so slot 9 IS
 *  `OnNavFailed` under another index, which the 29c walk recorded as unrecovered and the listing
 *  settles. Writes the file/line marker at owner+0x1b44/+0x1b48 (ABSENT in the shape map), calls
 *  `TaskFail` (slot 448) with the caller's reason, re-plays the resolved link activity through
 *  `SetIdealActivity`, and sets the failed latch. */
void NavOnNavFailed(int32 FailReason);

/** `CAI_Navigator::Move` (`0x102eff40`, navigator slot 5), which `PerformMovement` (`0x1026c120`)
 *  dispatches from `NPCThink`. NAMED DIVERGENCE: this runtime's mover integrates the route on the
 *  actor tick, so what the think still owes the route is its END, sampled here: an arrival runs
 *  `OnNavComplete` (navigator slot 8 `0x102eea90`: the reset `0x102eeb70`, the owner's
 *  `TaskMovementComplete` `0x10273ec0` through `0x102eccc0`, `+0x1c = 1`); a route the mover gave up
 *  runs `OnNavFailed(0xc)` (slot 10, `Move`'s own `(0xc, 1)` at `0x102f0180`). No active goal, no
 *  work (`Move`'s `0x102ee2e0` gate). Story 8 wave 2 (L13): the retail task arms now read the
 *  navigator's goal words, so the route's end has to reach them. */
void NavigatorMoveStep();

/** `thunk_FUN_102ee680(m_pNavigator)` — SDK `CAI_Navigator::IsGoalActive()`. The port's mover
 *  answers the same question through `SampleNavigation().bActiveGoal`. */
bool NavIsGoalActive() const;

/** `thunk_FUN_102ee2c0(m_pNavigator)` — SDK `CAI_Navigator::StopMoving()`. Wired to the mover's
 *  own `Stop()`. */
void NavStopMoving();

/** `thunk_FUN_102ee620(m_pNavigator)` — the navigator's route/goal-state word, which
 *  `ValidateNavGoal` requires to be exactly 6. **SEAM**: `IElysiumNpcMotor` keeps no goal type, so
 *  this answers -1 and `ValidateNavGoal` takes retail's own "not that state" arm. */
int32 NavGoalState() const;

/** `thunk_FUN_102ee6a0` (is the pending link valid) and `thunk_FUN_102ee510` (its cached activity)
 *  — the pair `FUN_1027a6c0` reads. **SEAM**: no link objects here; answers false. The link's
 *  retail identity is **unrecovered**. */
bool NavLinkActivity(int32& OutActivity) const;

/** `thunk_FUN_102e0bd0(m_pMotor, …)` — `CAI_Motor::MoveGroundExecute`'s apply of one interval's
 *  root-motion delta, which `AutoMovement` (`0x10280a50`) calls under its gate. **SEAM**: Unreal's
 *  animation instance extracts and applies root motion itself, so this records that the gate was
 *  PASSED and applies nothing; the gate and the order above it are the retail contract this port
 *  keeps (see `docs/vtmb/npc-ai/shape.md` § `0x10280a50`). */
bool MotorApplyIntervalMovement(const FVector& DeltaUnits, float YawDelta);

/** `CBaseAnimating::GetIntervalMovement(flInterval, …)` — the per-frame root-motion delta
 *  `AutoMovement` blends. **SEAM**: the animating tier here publishes no interval movement to the
 *  kernel; answers false with the delta zeroed. */
bool AnimIntervalMovement(float Interval, FVector& OutDeltaUnits, float& OutYawDelta) const;

bool KernelHullTrace(const FVector& StartUnits, const FVector& EndUnits, const FVector& HullMins,
	const FVector& HullMaxs, int32 Mask, FKernelHullTrace& OutTrace) const;

/** The shared hull table's mins and maxs for a hull id, in SOURCE units.
 *
 *  Answers from the replayed `NAI_Hull` table (`Substrate/ElysiumRetailHullTable.h`,
 *  `docs/vtmb/data/hull_table.json`). False, with both left at zero, for a hull id retail's own
 *  table does not carry -- which is what every caller's failure arm was written against. */
bool RetailHullExtents(int32 Hull, EElysiumHullExtents Which, FVector& OutMinsUnits,
	FVector& OutMaxsUnits) const;

/** `m_Collision`'s slots +4 / +8 — an entity's OBB mins and maxs, SOURCE units, which `CheckStuck`
 *  (`0x103ab580`) builds both boxes from. **SEAM**: `FElysiumEntity` carries no collision extents;
 *  answers false with both left at zero. */
static bool RetailCollisionExtents(const FElysiumEntity& Entity, FVector& OutMinsUnits,
	FVector& OutMaxsUnits);

/** `CBaseEntity`'s OWN slot-153 body (`0x10026e70`, spelled `CAISound::FUN_10026e70` because
 *  `CAISound` is the class the corpus attributes it to) — `m_vecVelocity` compared COMPONENT-WISE
 *  for EXACT equality against `DAT_1070d1b0`/`b4`/`b8`, the image's shared zero vector, answering 0
 *  when all three match and 1 otherwise.
 *
 *  Slot 153 has two bodies in this family and this is the other one: every class on the NPC line —
 *  `CAI_BaseNPC` through every `CNPC_V*` leaf — carries `0x10280300`, which is `IsMoving()` above
 *  and forwards to the navigator; the 497 classes that are NOT NPCs carry this, including the five
 *  the kernel's closure walks (`CAISound`, `CAI_Hint`, `CAI_InterestingPlace`,
 *  `CAI_InterestingPlaceConverstation`, `CAI_StandoffGoal`). It is therefore **not** a species
 *  override of the NPC's slot and does not belong in `IsMoving`'s dispatch; it is the base entity's
 *  answer, and it lands here beside its twin under the class it came from, exactly as `CanStandOn`
 *  carries `CAISound::FUN_10026f80`.
 *
 *  No port caller: nothing in this runtime stands a `CAI_Hint` or a `CAISound` as an entity with a
 *  vtable, so this is reached only by the test that pins it — the shape family BaseHelpers already
 *  uses for `FUN_1028ebc0`. Exact equality is retail's and is kept: a velocity of `-0.0` on any axis
 *  compares equal to `0.0` and answers "not moving", which is the shipped answer. */
static bool BaseEntityIsMoving(const FElysiumEntity& Entity);

/** The active weapon's capability word (weapon vtable +0x5a0, slot 360, retail body `0x1014f930`),
 *  which `ShouldMoveAndShoot` requires to carry `0x6000`. **SEAM**: `FElysiumWeapon` stands no such
 *  word; answers 0, so the Troika gate closes and the base rung is never reached. */
uint32 ActiveWeaponCapabilityWord() const;

/** `CAI_Motor`'s deceleration query (`0x102e1300`, `CAI_Motor#16`) lives on `IElysiumNpcMotor`
 *  itself as `MinStoppingDistance()`; this is the NPC-side read, so a body cites the address at the
 *  point of use. Answers the interface's own floor when there is no motor. */
float MotorMinStoppingDistanceUnits() const;

/** `CBaseAnimating::IsIgnoreCollisionEntity(other)` (`0x1008be20`) — the tail of both
 *  collision-ignore chains: `m_hIgnoreCollisionEntity` resolved and compared against the candidate. */
bool IsIgnoreCollisionEntityTail(const FElysiumEntity* Other) const;

/** `CAI_BaseNPC::AutoMovement` `0x10280a50` — slot 250 first, then the interval movement, applied
 *  ONLY when `GetMoveType() == 4` and `FL_FROZEN 0x400` is clear. The gate is the retail contract a
 *  modernization has to keep; the extraction underneath it is Unreal's. */
bool AutoMovement();

/** `CAI_BaseNPC::PostRun` `0x1026c7c0` — the PAIRING and its ORDER: dispatch own vtable +0x408 with
 *  the elapsed interval from `thunk_FUN_1026c540`, then `CBaseCombatCharacter::Weapon_FrameUpdate`
 *  with the same number. */
void PostRun();

/** `CAI_BaseNPC::CheckOnGround` `0x1026e5e0` — the gated ground hull-trace and the two writes it
 *  can make (clear the ground entity, or adopt the traced one). */
void CheckOnGround();

bool OnObstructingDoorBase(float& InOutMoveGoalMaxDistance, int32 DoorState, float DistClear,
	EObstructingDoorResult& OutResult) const;

/** `CAI_BaseNPC::MaxYawSpeed` `0x10280bb0` — the base line's single constant. */
static float MaxYawSpeedBase();

/** `CAI_Navigator::OnNavFailed`'s activity resolution, `FUN_1027a6c0` `0x1027a6c0`: the link's
 *  cached activity when the link is valid and not -1, else 1 (`ACT_IDLE`). */
int32 ResolveLinkActivity() const;

/** `thunk_FUN_102ee140(m_pNavigator)` — the navigator's current goal position, SOURCE units.
 *  **SEAM**: the mover keeps no readable goal; answers false and leaves `OutGoal` untouched. */
bool NavGoalPosition(FVector& OutGoalUnits) const;

/** `CAI_BaseNPC::IsJumpLegal`'s shared geometry helper `FUN_10280790` `0x10280790`, as a pure
 *  function of the three points and the three thresholds, so both fills of slot 521 are one body. */
static bool IsJumpLegalGeometry(const FVector& StartUnits, const FVector& ApexUnits,
	const FVector& EndUnits, float MaxRise, float MaxDrop, float MaxDistance);

