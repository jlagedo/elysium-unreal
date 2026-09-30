// `CAI_BaseNPC`'s bodies of the `Motor10` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseMotor10.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcMotor10Shared.h"

// --- File-scope helpers moved with the base bodies (story 5 step 5) ---

namespace
{
	// `_DAT_104994e0`, `00 00 f0 c1`: **-30.0f**, the Z the shoot target is raised by when the
	// enemy's type-3 stat list answers 5 for stat 0xb.
	constexpr float GMotor10ShootTargetZOffset = ElysiumNpcTunables::EnemyAimPointZOffset;
	// `_DAT_1044eb08` — degrees to radians, `0x10139610`'s scale.
	constexpr float GMotor10DegToRad = ElysiumNpcTunables::DegreesToRadians;
	FVector Motor10PortOf(const FVector& Units)
	{
		return FVector(Units.X * ElysiumMove::U, -Units.Y * ElysiumMove::U,
			Units.Z * ElysiumMove::U);
	}
}

// --- Moved from `ElysiumNpcMotor10.cpp` (story 5 step 5) ---

bool FElysiumNpcBase::MotorMoveTraceSweep(int32 Kind, const FVector& StartUnits,
	const FVector& EndUnits, int32 Mask, float ExtentUnits, const void* Filter,
	FMotorMoveTrace& OutTrace) const
{
	// `MoveLimit 0x102e6d70(navType, start, end, mask, target, pct, flags, &trace, skip, surfaceOut)`
	// (R1 §6). The port's `Kind` is retail's nav type, its `ExtentUnits` is `pct` -- the PERCENT of
	// the leg `TestGroundMove 0x102e4f50` stand-tests (100 = the whole leg), not an extent -- and its
	// `Filter` is `skip`. Retail's own head, before it dispatches on `kind`, is the record's
	// initialisation:
	//
	//     trace[9] = 0;                       // +0x24 flTotalDist
	//     trace[7] = 0;                       // +0x1c pObstruction
	//     trace[4..6] = vec3_origin;          // +0x10 vHitNormal, DAT_1070d1b0
	//     trace[0] = 0;                       // +0x00 fStatus
	//     trace[1..3] = *start;               // +0x04 vEndPosition, seeded to the START
	//
	// and every arm answers `trace.fStatus >= 0`. Positions in `KernelHullTrace`'s frame (Source
	// units, the port's axes).
	OutTrace = FMotorMoveTrace();
	OutTrace.EndPositionUnits = StartUnits;

	++Motor10Seams.MoveTraceSweeps;
	Motor10Seams.LastMoveTraceKind = Kind;
	Motor10Seams.LastMoveTraceMask = Mask;
	Motor10Seams.LastMoveTraceExtent = ExtentUnits;

	// Retail dispatches on the nav type through the table `0x102e6f98` (`navigation-jump-links.md`
	// § "MoveLimit"): 0 ground `0x102e5d80`, 1 jump `0x102e6290`, 2 fly `0x102e6090`, 3 climb
	// `0x102e6be0`. No port caller passes anything but ground.
	//
	// Jump (1, `0x102e6290`): unreachable, and deliberately so (0018/7, owner's decision 1): retail's
	// link predicate `0x102ff960` ANDs slot 513 `CapabilitiesGet` with the link's per-hull word and
	// only exactly 2 reaches `IsJumpLegal` (slot 521); no shipped NPC holds capability bit 2, so no
	// jump link is ever planned and no jump leg is ever walked. The baked jump links are records
	// with no agent (`AElysiumNavJumpLink`); this arm keeps the initialised (admitting) record.
	//
	// Fly (2, `0x102e6090`): no flyer moves in the port yet; the admitting record stands until
	// 0018 row 18 (story 0018/12, flight) builds the fly probe.
	//
	// Climb (3, `0x102e6be0`): no arm. Story 1 verdicts the climb machinery dead (`CAI_Motor`
	// slots 3-5; no shipped link climbs), so any other nav type also keeps the initialised record.
	constexpr int32 GMoveLimitNavGround = 0;
	constexpr int32 GMoveLimitBlockedWorld = -2;
	if (Kind != GMoveLimitNavGround)
	{
		return OutTrace.Status >= 0;
	}
	// The ground arm (nav type 0 -> `0x102e5d80` -> `TestGroundMove 0x102e4f50`): a NAMED
	// DIVERGENCE onto Unreal's NavMesh (0018/6 R1 §6). The body's own agent answers whether the
	// straight walk stays on its mesh (`IElysiumNpcMotor::NavRaycast`, the default query filter:
	// the ground test prices nothing), baked from the same step height and hull. What does NOT run:
	// `TestGroundMove 0x102e4f50`'s 16-unit segmentation and start stand test; `CheckStep
	// 0x102e4160`'s per-step `CanStandOn 0x10026f80` agreement (and the `CheckStandPosition
	// 0x102e7270` step-end probe); and the final `|dz| > max(hull height / 2, slot 522 + 0.1)`
	// refusal (`102e565b`-`102e56aa`). Clear -> `fStatus` 0 and the end reached. A hit -> the world
	// blocker `-2` (a mesh edge is world), `vEndPosition` where the walk left the mesh and
	// `flDistObstructed` the 2-D distance still to go from there (the wander probe's `dist -
	// flDistObstructed`); any z acceptance on the hit is the caller's. A motor with no NavMesh keeps
	// retail's initialised record, which is the admitting answer.
	// `skip` non-zero: the ground test returns "clear, end = goal" without tracing (R1 §6,
	// `navigation-jump-links.md` § "The route helpers").
	const double LegUnits = FVector::Dist2D(StartUnits, EndUnits);     // ground: a 2-D distance
	if (Filter != nullptr)
	{
		OutTrace.EndPositionUnits = EndUnits;
		OutTrace.TotalDistUnits = static_cast<float>(LegUnits);
		return true;
	}
	if (Motor == nullptr)
	{
		return OutTrace.Status >= 0;
	}
	const double U = ElysiumMove::U;
	FElysiumNpcNavRaycast Ray;
	Ray.FromCm = StartUnits * U;
	Ray.ToCm = EndUnits * U;
	FElysiumNpcNavRaycastAnswer Answer;
	if (!Motor->NavRaycast(Ray, Answer))
	{
		return OutTrace.Status >= 0;
	}
	OutTrace.TotalDistUnits = static_cast<float>(LegUnits);
	if (!Answer.bHit)
	{
		OutTrace.EndPositionUnits = EndUnits;
		return true;
	}
	OutTrace.Status = GMoveLimitBlockedWorld;
	OutTrace.EndPositionUnits = Answer.HitCm / U;
	OutTrace.DistObstructedUnits = static_cast<float>(FVector::Dist2D(OutTrace.EndPositionUnits, EndUnits));
	return OutTrace.Status >= 0;
}

void FElysiumNpcBase::MotorSetOriginToTraceEnd(const FVector& EndPositionUnits)
{
	// `UTIL_SetOrigin(owner, trace.vEndPosition, true)` `0x101cf5c0`:
	//     entity->slot 62 (+0xf8) SetLocalOrigin(vec);
	//     if (bFireTriggers) entity->PhysicsTouchTriggers();
	// NOT `NDebugOverlay::Line` — `docs/vtmb/npc-kernel/layout.md`'s `m_vecGrappleSavedOrigin` row
	// already names this address "the SetAbsOrigin helper", and the body above is the whole of it.
	// The move is REAL and is performed.
	//
	// A one-line forward to the chain's slot 62 `SetOrigin` (`FElysiumEntity::SetOrigin` `0x100b2be0`,
	// the entity transform seam that moves the body). `PhysicsTouchTriggers` is the collision
	// service's own overlap update once the body moves (Unreal raises the trigger overlaps).
	++Motor10Seams.SetOriginCalls;
	Motor10Seams.LastSetOriginUnits = EndPositionUnits;
	SetOrigin(Motor10PortOf(EndPositionUnits));
}

void FElysiumNpcBase::SetHullSizeNormal(bool bForce)
{
	// `FUN_10273070`, in retail's order.
	//
	// 1. Retail's self-check (`NAI_Hull::Bits 0x102d6210` against `GetUsedHullBits 0x102f9950`,
	//    then six DevMsg lines naming the hull through `NAI_Hull::Name 0x102d6230`) writes nothing
	//    but the print: dead (story 6, no output device), removed.
	//
	// 2. The gate: SET or forced.
	if (!bIsUsingSmallHull && !bForce)
	{
		return;
	}

	// 3. `UTIL_SetSize(this, NAI_Hull::Mins(m_eHull), NAI_Hull::Maxs(m_eHull))`. The listing
	//    evaluates MAXS first (`0x102d6120`, `1000a993`) and MINS second (`0x102d6100`, `1000fd44`)
	//    and then pushes mins before maxs, so the CALL order and the ARGUMENT order differ; the
	//    extents come off one table either way.
	FVector MinsUnits = FVector::ZeroVector;
	FVector MaxsUnits = FVector::ZeroVector;
	RetailHullExtents(HullKind, EElysiumHullExtents::Full, MinsUnits, MaxsUnits);      // the replayed hull table's row
	LastSetSizeMinsUnits = MinsUnits;
	LastSetSizeMaxsUnits = MaxsUnits;
	++SetSizeCalls;
	// `UTIL_SetSize` itself is the capsule's (`IElysiumNpcMotor::SetHullSize`), in this world's axes.
	if (Motor != nullptr)
	{
		Motor->SetHullSize(Motor10PortOf(FVector(MinsUnits.X, MaxsUnits.Y, MinsUnits.Z)),
			Motor10PortOf(FVector(MaxsUnits.X, MinsUnits.Y, MaxsUnits.Z)));
	}

	// 4. The clear happens whether or not the physics rebuild does — `MOV byte [ESI+0x5f2d],0` sits
	//    before the `JZ` at `10273131`.
	bIsUsingSmallHull = false;

	// 5. `if (m_pPhysicsObject (+0x36c)) SetupVPhysicsHull();`
	if (bHasVPhysicsObject)
	{
		++VPhysicsHullRebuilds;
	}
}

bool FElysiumNpcBase::SetHullSizeSmall(bool bForce)
{
	// `FUN_10273180`, the twin. The gate is INVERTED — it runs when the small hull is NOT already
	// in use — and there is no self-check.
	if (!bIsUsingSmallHull || bForce)
	{
		// `UTIL_SetSize(this, NAI_Hull::SmallMins(m_eHull), NAI_Hull::SmallMaxs(m_eHull))`
		// (`0x102d6140` / `0x102d6160`) — the SMALL pair of the same hull's row, not a smaller
		// hull. For two rows it is the wider of the two: TZIMISCE1's small box is 45 against its
		// full 35, so "small hull" is retail's name for the alternate box, not a shrink.
		FVector MinsUnits = FVector::ZeroVector;
		FVector MaxsUnits = FVector::ZeroVector;
		RetailHullExtents(HullKind, EElysiumHullExtents::Small, MinsUnits, MaxsUnits);
		LastSetSizeMinsUnits = MinsUnits;
		LastSetSizeMaxsUnits = MaxsUnits;
		++SetSizeCalls;
		if (Motor != nullptr)
		{
			Motor->SetHullSize(Motor10PortOf(FVector(MinsUnits.X, MaxsUnits.Y, MinsUnits.Z)),
				Motor10PortOf(FVector(MaxsUnits.X, MinsUnits.Y, MaxsUnits.Z)));
		}

		bIsUsingSmallHull = true;
		if (bHasVPhysicsObject)
		{
			++VPhysicsHullRebuilds;
		}
	}
	// `return CONCAT31(uVar3, 1)` — **1 unconditionally**, including on the path where the gate
	// refused and nothing changed. Retail's, and reproduced.
	return true;
}

int32 FElysiumNpcBase::RetailHullBits(int32 Hull)
{
	// `NAI_Hull::Bits(hull)` = `PTR_DAT_1060a750[hull][0]` (`0x102d6210`). **SEAM**: no hull table.
	(void)Hull;
	return 0;
}

bool FElysiumNpcBase::WeaponOwnsThisType(const TCHAR* WeaponClassname) const
{
	// **SEAM** for `CBaseCombatCharacter::Weapon_OwnsThisType(name, 0)`.
	(void)WeaponClassname;
	++Motor10Seams.WeaponOwnsAsks;
	return false;
}

int32 FElysiumNpcBase::EnemyTypedStatValue(const FElysiumEntity& Enemy, int32 StatId)
{
	// **SEAM**: `enemy->+0x9c` then its `+0x13bc` / `+0x13c0` list table, the first entry whose
	// `+0x10` is 3, then `CVStatList_t::GetValue(statId)` (`0x102012d0`). Family Sounds10 records
	// the same join for `FireBullets` and takes the same refusal: no `CVStatList_t` container keyed
	// by retail's list type stands here, so the join is **unrecovered** and the answer is 0 — which
	// is also retail's own answer through the empty lazily-built `DAT_109f0b40`.
	(void)Enemy;
	(void)StatId;
	return 0;
}

FVector FElysiumNpcBase::RetailGetAnglesDegrees() const
{
	// **SEAM** for slot 221 `CBaseEntity::GetAngles()` (`0x100b3110`), which is a generated stub on
	// this line. `FElysiumEntity::Angles` is the port's equivalent word.
	return Angles;
}

FVector FElysiumNpcBase::AngleVectorsForward(const FVector& AnglesDegrees)
{
	// `0x10139610`'s forward, with `_DAT_1044eb08` the degrees-to-radians scale:
	//     c1 = cos(a[1] * RAD), s1 = sin(a[1] * RAD);      // yaw
	//     c2 = cos(a[0] * RAD), s2 = sin(a[0] * RAD);      // pitch
	//     forward = (c2 * c1, c2 * s1, -s2);
	const float Pitch = AnglesDegrees.X * GMotor10DegToRad;
	const float Yaw = AnglesDegrees.Y * GMotor10DegToRad;
	return FVector(FMath::Cos(Pitch) * FMath::Cos(Yaw), FMath::Cos(Pitch) * FMath::Sin(Yaw),
		-FMath::Sin(Pitch));
}

FVector FElysiumNpcBase::GetShootTarget(const FVector& PosSrcUnits, bool bNoisy, bool bFlag2) const
{
	// `FUN_10278650`, in retail's order. See `ElysiumNpcMotor10.inl` for why this is
	// `GetShootTarget` and not the standoff anchor the checklist's row named.

	// 1. `m_hShootTargetOverride` (+0x5ba8) resolving wins outright, and the three arguments are
	//    never looked at. The listing validates the handle TWICE against `PTR_DAT_10566458` — once
	//    for the gate, once to resolve — which is one redundant read and no behaviour.
	if (World != nullptr && ShootTargetOverride.IsSet())
	{
		if (const FElysiumEntity* Override = World->Resolve(ShootTargetOverride))
		{
			return NpcKernelMotor10Shared::Motor10SourceOf(Override->Origin);        // override->slot 217 GetAbsOrigin
		}
	}

	// 2. No enemy: the forward vector of this body's own angles, PLUS the source position.
	const FElysiumEntity* Enemy = GetEnemy();                // slot 167 (+0x29c)
	if (Enemy == nullptr)
	{
		const FVector ForwardUnits = AngleVectorsForward(RetailGetAnglesDegrees());
		return ForwardUnits + PosSrcUnits;
	}

	// 3. An enemy. `GetEnemies()` (slot 541, +0x874) then
	//    `CAI_Enemies::GetLastKnownPosition(&lkp, enemy)` (`0x102dfed0`). Family Positions already
	//    stands that pair as `EnemyLastKnownPosition`; it is called rather than answered twice.
	FVector LastKnownCm = FVector::ZeroVector;
	EnemyLastKnownPosition(LastKnownCm);
	const FVector LastKnownUnits = NpcKernelMotor10Shared::Motor10SourceOf(LastKnownCm);

	// 4. `enemy->slot 197 (+0x314) BodyTarget(posSrc, bNoisy, bFlag2)`.
	FVector BodyTargetUnits = NpcKernelMotor10Shared::Motor10SourceOf(
		const_cast<FElysiumNpcBase*>(this)->BodyTarget(Motor10PortOf(PosSrcUnits), bNoisy, bFlag2));

	// 5. The stat arm. `enemy->+0x9c` and its `+0xa8` must BOTH be non-null before the list walk
	//    runs at all; the walk then takes the first type-3 list, or the empty global, and asks it
	//    for stat `0xb`. An answer of exactly **5** raises the body target's Z by `_DAT_104994e0` =
	//    **-30.0** — a negative offset, so the target moves DOWN.
	if (EnemyTypedStatValue(*Enemy, 0xb) == 5)
	{
		BodyTargetUnits.Z += GMotor10ShootTargetZOffset;
	}

	// 6. `lkp + (bodyTarget - enemy->GetAbsOrigin())`.
	return LastKnownUnits + (BodyTargetUnits - NpcKernelMotor10Shared::Motor10SourceOf(Enemy->Origin));
}
