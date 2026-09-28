// Story 0019/8 (29e under the strict verdict), family **RunAi19** -- `CAI_BaseNPCTroika`'s bodies.
//
// Declarations are in `ElysiumNpcRunAi.inl` (included inside `class FElysiumNpc`) or generated in
// `ElysiumNpcSlots.inl` for a slot body. Walked prose:
// `docs/vtmb/npc-ai/schedule-kernel.md` § "Story 8, family RunAi19".
//
// Owns (RunAi19's `rule` rows): 0x1028fd80 CAI_BaseNPCTroika::RunAlternateAI, 0x1028fcc0
// CAI_BaseNPCTroika::RunAI; and the two unlisted callees `FUN_10290200` / `FUN_102902e0` (modes 2
// and 3 of the door transaction) plus `FUN_1028fc90` (the stealth reset), which had no port body.

#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "Substrate/ElysiumNpcMotorShared.h"

namespace
{
	/** `m_eAlternateAI` (`+0x644c`) values, the jump table at `0x1028ff90`. */
	constexpr int32 RunAi19AlternateAiOpening = 1;   // 0x1028fe69 -> 0x10290040
	constexpr int32 RunAi19AlternateAiOpened = 2;    // 0x1028fe7f -> 0x10290200
	constexpr int32 RunAi19AlternateAiBlocked = 3;   // 0x1028fe95 -> 0x102902e0
	constexpr int32 RunAi19AlternateAiDoorWait = 4;  // 0x1028feab -> 0x10290350
	/** `PUSH 0xe` at `0x10290252` / `0x10290301`: the expired transaction's task failure. */
	constexpr int32 RunAi19AlternateAiExpiredFailReason = 0xe;
	/** `MOV EAX,0x3f800000` at `0x1028fc90`: 1.0f, the neutral stealth scalar and cone. */
	constexpr float RunAi19StealthNeutral = 1.0f;
	/** `_DAT_10450aa0` = 4.0: the sweep's start raised off the origin (`0x103c0189`). */
	constexpr float RunAi19SweepRaiseUnits = 4.0f;
	/** `PUSH 0x202400b`: `MASK_NPCSOLID`, the sweep's and the push line's mask. */
	constexpr int32 RunAi19NpcSolidMask = 0x202400b;

	/** The byte table `0x1028fedc` over `m_IdealActivity - 0xf88` (`0x1028fe27`..`0x1028fe3b`): the
	 *  nine activities whose entry is the `AutoMovement` arm `0x1028fe42`. */
	bool RunAi19GrappleVictimAutoMoves(int32 IdealActivity)
	{
		switch (IdealActivity)
		{
			case 0xf88:
			case 0xf91:
			case 0xf9a:
			case 0xfb7:
			case 0xfc0:
			case 0xff7:
			case 0x1000:
			case 0x1009:
			case 0x1039:
				return true;
			default:
				return false;
		}
	}
}

void FElysiumNpc::RunAi19ResetStealthSurface()
{
	// `FUN_1028fc90`, the three stores in the listing's order.
	Senses.StealthHearingDist = 0.f;                                          // 0x1028fc95 +0x63cc
	Senses.StealthVisionScalar = RunAi19StealthNeutral;                       // 0x1028fc9f +0x63c4
	Senses.StealthVisionCone = RunAi19StealthNeutral;                         // 0x1028fca5 +0x63c8
}

void FElysiumNpc::RunAI(bool bReduced)
{
	// `CAI_BaseNPCTroika::RunAI` (`0x1028fcc0`, slot 432): three calls whose ORDER is the body. The
	// scope-trace push/pop (`0x1028fcc5`/`0x1028fccf` name pick, `0x1028fd48` pop) is absent by
	// convention.
	RunAi19ResetStealthSurface();                                             // 0x1028fd2c 0x1028fc90
	// Before conditions are gathered: it clears SHOULD_INTERACT 0x10 and the two crosswalk bits.
	UpdatePedestrianInfo();                                                   // 0x1028fd31 0x102a0d20
	FElysiumNpcBase::RunAI(bReduced);                                         // 0x1028fd3d 0x1026f110
}

bool FElysiumNpc::RunAlternateAI(bool bReduced)
{
	// `CAI_BaseNPCTroika::RunAlternateAI` (`0x1028fd80`). `bReduced` is only forwarded; none of the
	// four arms reads it (each ends `RET 4` without touching `[ESP+4]`). Scope trace absent (the name
	// pick `0x1028fd82` / `0x1028fd8c`).
	(void)bReduced;
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;

	// The grapple head arm: the partner handle live (the three-part EHANDLE test) AND role 1.
	const bool bPartnerLive = World != nullptr && Grapple.Partner.IsSet()      // 0x1028fdf2
		&& World->Resolve(Grapple.Partner) != nullptr;                        // 0x1028fe0f / 0x1028fe14
	if (bPartnerLive && Grapple.Role == EElysiumGrappleRole::Victim)          // 0x1028fe1f
	{
		if (RunAi19GrappleVictimAutoMoves(IdealActivityNumber))               // 0x1028fe31 / 0x1028fe3b
		{
			AutoMovement();                                                   // 0x1028fe42 0x10280a50
		}
		return true;                                                          // 0x1028fe52
	}

	switch (AlternateAi)                                                      // 0x1028fe57 / 0x1028fe60 / 0x1028fe62 table
	{
		case RunAi19AlternateAiOpening:
			return RunAlternateAiOpeningDoor(Now);                            // 0x1028fe6e 0x10290040
		case RunAi19AlternateAiOpened:
			return RunAi19AlternateAiMode2(Now);                              // 0x1028fe84 0x10290200
		case RunAi19AlternateAiBlocked:
			return RunAi19AlternateAiMode3(Now);                              // 0x1028fe9a 0x102902e0
		case RunAi19AlternateAiDoorWait:
			return RunAlternateAiDoorMode4(Now);                              // 0x1028feb0 0x10290350
		default:
			// Case 0 (table entry `0x1028fec1`) and anything above 4 (`JA` at `0x1028fe60`).
			return false;                                                     // 0x1028fecc
	}
}

bool FElysiumNpc::RunAi19AlternateAiMode2(double Now)
{
	// `FUN_10290200`.
	MaintainActivity();                                                       // 0x10290203 0x102727d0
	// `FCOMP [+0x6450]; AND AH,1; JNZ`: curtime strictly before the expiry keeps waiting.
	if (Now < AlternateAiExpireTime)                                          // 0x1029021d
	{
		return true;                                                          // 0x102902a7
	}
	if (World != nullptr && OpeningDoor.IsSet()                               // 0x1029022c
		&& World->Resolve(OpeningDoor) != nullptr)                            // 0x10290249 / 0x1029024e
	{
		TaskFail(RunAi19AlternateAiExpiredFailReason);                        // 0x10290256 slot 448
		OpeningDoor = FElysiumEntityHandle();                                 // 0x1029025c +0x5d24 = -1
		bOpeningDoorWait = false;                                             // 0x10290266 +0x5d30
		AlternateAi = 0;                                                      // 0x1029026d +0x644c
		return true;                                                          // 0x10290277
	}
	if (NavIsGoalSet())                                                       // 0x10290283 0x102ee2e0
	{
		ResumeScheduledMove();                                                // 0x1029028e 0x102bf7e0
	}
	ValidateNavGoal();                                                        // 0x10290297 slot 528
	AlternateAi = 0;                                                          // 0x1029029d
	return true;                                                              // 0x102902a7
}

bool FElysiumNpc::RunAi19AlternateAiMode3(double Now)
{
	// `FUN_102902e0`.
	MaintainActivity();                                                       // 0x102902e3 0x102727d0
	if (!(Now < AlternateAiExpireTime))                                       // 0x102902fd
	{
		TaskFail(RunAi19AlternateAiExpiredFailReason);                        // 0x10290305 slot 448
		OpeningDoor = FElysiumEntityHandle();                                 // 0x1029030d
		bOpeningDoorWait = false;                                             // 0x10290317
		AlternateAi = 0;                                                      // 0x1029031d
	}
	return true;                                                              // 0x10290323
}

// =================================================================================================
// The forward-obstruction reaction (Tzimisce / Gargoyle / Hengeyokai slot 432)
// =================================================================================================

bool FElysiumNpc::RunAi19IsWorldEntity(const FElysiumEntity* Entity) const
{
	// SEAM (declaration). A null hit reads as the world: the crash guard.
	return Entity == nullptr;
}

FElysiumEntity* FElysiumNpc::RunAi19ObstructionSweep(float LookaheadUnits, bool bRetraceAtStepHeight,
	int32& OutKind)
{
	// Tzimisce `0x103c0160` addresses; Gargoyle `0x103796a0` and Hengeyokai `0x10380fc0` are the same
	// arms (`0x103796ac`..`0x103798a2` / `0x10380fcc`..`0x103811c2`) plus the re-trace below.
	FVector StartUnits = NpcKernelMotorShared::SourceOf(GetOrigin());        // 0x103c016d slot 220
	StartUnits.Z += RunAi19SweepRaiseUnits;                                   // 0x103c0189
	const FVector SweepForward = AngleVectorsForward(RetailGetAnglesDegrees());   // 0x103c01a2 slot 221 / 0x103c01a9 0x10139610
	FVector EndUnits = StartUnits + SweepForward * LookaheadUnits;                // 0x103c01ae..0x103c01ce the lookahead ConVar
	// `CTraceFilterSimpleTwoEnt(this, GetIgnoreCollisionEntity(), m_CollisionGroup)`
	// (`0x103c01d7`..`0x103c0227`): `KernelHullTrace` takes no filter and the port has no
	// `+0x368` word -- named, the trace is the family Motor seam.
	FVector MinsUnits = FVector::ZeroVector;
	FVector MaxsUnits = FVector::ZeroVector;
	RetailCollisionExtents(*this, MinsUnits, MaxsUnits);                      // 0x103c023a / 0x103c0243 m_Collision
	FKernelHullTrace Trace;
	KernelHullTrace(StartUnits, EndUnits, MinsUnits, MaxsUnits, RunAi19NpcSolidMask, Trace);   // 0x103c0425
	if (Trace.Fraction == 1.0f)                                               // 0x103c0475
	{
		return nullptr;
	}
	FElysiumEntity* const Hit = World != nullptr ? World->Resolve(Trace.HitEntity) : nullptr;
	if (RunAi19IsWorldEntity(Hit))                                            // 0x103c049e 0x1023bd00
	{
		return nullptr;
	}
	if (!NavIgnoreCollision(Hit))                                             // 0x103c04ba slot 69
	{
		return nullptr;
	}
	if (!bRetraceAtStepHeight)
	{
		OutKind = 0;                                                          // 0x103c04d2 (Tzimisce: the only value written)
		return Hit;
	}
	// The re-trace (Gargoyle `0x103798bb`.., Hengeyokai `0x103811db`..): both ends raised by slot
	// 522, asked twice, the box re-read.
	StartUnits.Z += StepHeight();                                             // 0x103798d8 slot 522
	EndUnits.Z += StepHeight();                                               // 0x103798ea slot 522
	RetailCollisionExtents(*this, MinsUnits, MaxsUnits);
	FKernelHullTrace Retrace;
	KernelHullTrace(StartUnits, EndUnits, MinsUnits, MaxsUnits, RunAi19NpcSolidMask, Retrace);
	FElysiumEntity* const Above = World != nullptr ? World->Resolve(Retrace.HitEntity) : nullptr;
	if (Retrace.Fraction == 1.0f                                              // 0x103799fe / 0x1038131e
		|| RunAi19IsWorldEntity(Above)                                        // 0x10379a00 / 0x10381320
		|| !NavIgnoreCollision(Above))                                        // 0x10379a0e / 0x1038132e
	{
		OutKind = 1;                                                          // 0x10379a40 / 0x10381360
	}
	else
	{
		OutKind = 0;                                                          // 0x10379a24 / 0x10381344
	}
	return Hit;
}

void FElysiumNpc::RunAi19ObstructionStepUp()
{
	// `0x103c0860` / `0x10379e80` / `0x103816e0`: `+0x08` slot 220, `+0x26` slot 522, `+0x3d` slot 62.
	FVector OriginCm = GetOrigin();
	OriginCm.Z += StepHeight() * ElysiumMove::U;
	SetOrigin(OriginCm);
}

bool FElysiumNpc::RunAi19BlockerHasPhysicsObject(const FElysiumEntity* Blocker) const
{
	// SEAM (declaration).
	const FElysiumNpcBase* const Npc = Blocker != nullptr ? Blocker->AsNpcBase() : nullptr;
	return Npc != nullptr && Npc->bHasPhysicsObject;
}

void FElysiumNpc::RunAi19BlockerAddVelocity(FElysiumEntity* Blocker, const FVector& VelocityUnits)
{
	// SEAM (declaration).
	(void)Blocker;
	RunAi19LastBlockerPushUnits = VelocityUnits;
	++RunAi19BlockerPushes;
}

void FElysiumNpc::RunAi19ObstructionPush(FElysiumEntity* Blocker, float Scalar, float ZPush)
{
	// Tzimisce `0x103c05e0` addresses, Hengeyokai `0x10381460` in brackets where the listing gives
	// them; Gargoyle's copy is `0x10379be6`..`0x10379db5`.
	if (Blocker == nullptr)                                                   // 0x103c05f1 [0x10381471]
	{
		return;
	}
	if (!RunAi19BlockerHasPhysicsObject(Blocker))                             // 0x103c05ff [0x1038147f] +0x36c
	{
		return;
	}
	// The angular impulse is (0, 0, 0) (`0x103c060e`); THIS body's own velocity, slot 199.
	FVector OwnVelocity = FVector::ZeroVector;
	GetVelocity(&OwnVelocity, nullptr);                                       // 0x103c0626 [0x103814a6] slot 199
	FVector Push(OwnVelocity.Y, -OwnVelocity.X, OwnVelocity.Z);               // rotated -90 degrees, not normalised
	const FVector BlockerUnits = NpcKernelMotorShared::SourceOf(Blocker->GetOrigin());   // 0x103c0642 [0x103814c2] slot 220
	// A world-only line (`CTraceFilterWorldOnly`) from the blocker along the push.
	FKernelHullTrace Line;
	KernelHullTrace(BlockerUnits, BlockerUnits + Push, FVector::ZeroVector, FVector::ZeroVector,
		RunAi19NpcSolidMask, Line);                                           // 0x103c06d5
	if (Line.Fraction != 1.0f)                                                // 0x103c0708 [0x10381588]
	{
		Push = -Push;                                                         // 0x103c0717, z included
	}
	Push *= Scalar;                                                           // the `*_obstruction_scalar` ConVar
	Push.Z += ZPush;                                                          // the `*_obstruction_z` ConVar
	RunAi19BlockerAddVelocity(Blocker, Push);                                 // 0x103c07c4 [0x10381644] IPhysicsObject slot 41
}
