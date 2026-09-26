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
	// nothing else in the closure. Retail's "this navigator has failed" latch; no consumer in this
	// substrate reads it yet.
	bool bNavFailed = false;

	// `CAI_Navigator::vfunc3` (`0x102ecb50`) copies three of the owner NPC's own pointers —
	// `m_pMotor` (+0x5d44), `m_pMoveProbe` (+0x5d40), `m_pLocalNavigator` (+0x5d38) — into
	// navigator+0x20/+0x24/+0x28 and stores its argument at +0x2c. The three pointers do not exist
	// here, so what survives the port is the FACT that the snapshot was taken and the argument it
	// was taken with. Read by the test and by nothing else.
	bool bSnapshotTaken = false;
	int32 SnapshotArgument = 0;
};


/** What the seams above were ASKED, so a test can assert that a body reached its motor call and
 *  that the refusal was the recovered one. Read by the test suite and by nothing else. */
struct FMotorSeamLedger
{
	int32 MoveDone = 0;                  // slot 133's tail dispatch of `m_pfnMoveDone` (+0x114)
	int32 IntervalMovementApplied = 0;   // `AutoMovement`'s gated `thunk_FUN_102e0bd0`
	int32 PerformMovement = 0;           // the navigator's vtable slot 5 delegate
	float PerformMovementInterval = 0.f;
	int32 PostRunWeaponUpdates = 0;      // `PostRun`'s ordered pair, tallied on the second half
	float PostRunInterval = 0.f;
	int32 SetupJumpCommits = 0;          // `thunk_FUN_102c4e80`
	int32 MoveProbeChecks = 0;           // `CanStandAt`'s `thunk_FUN_102e7270`
	int32 HullTraces = 0;                // every `KernelHullTrace` caller
	int32 JumpArcSolves = 0;             // `thunk_FUN_102c4cc0`
	int32 LinkFacingCancels = 0;         // `thunk_FUN_102e1e20(-1)`
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
// Both words are filled from `ElysiumRetailHulls::ClassHulls` by the body that wears this kernel,
// keyed on the retail class. A class with no row of its own inherits the nearest ancestor's, which
// is retail's own arrangement: `CAI_BaseNPC`'s constructor zeroes both before any derived
// constructor runs (`navigation-jump-links.md` § "The two hull words", 2026-09-20).
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

mutable FMotorSeamLedger MotorSeams;

/** `CAI_BaseNPC::FUN_1027dc80` `0x1027dc80` — the BASE branch of slot 531 `OnObstructingDoor`. The
 *  Troika-line body slot 531 carries is `0x102984a0` and belongs to story 29d, so this lands as a
 *  named method rather than as a second definition of the generated virtual. */
enum class EObstructingDoorResult : int32 { Ok = 0, Illegal = -1 };
