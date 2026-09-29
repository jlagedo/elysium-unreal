#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcGeometryShared.h"
#include "Substrate/ElysiumNpcMingXiaoTentacle.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"

// Story 29c-1, family **Geometry** — the eye/anchor slots (193, 194, 195, 197, 533 and the
// species-dispatched 192), the hull-bit query (slot 337), `SetSize` (slot 213) and the four
// bodies that push one body out of another. The declarations, the family's four standing facts and
// every seam are `Substrate/ElysiumNpcGeometry.inl`; the walked prose is
// `docs/vtmb/npc-ai/shape.md`.
//
// Every constant below was read out of retail `vampire.dll`'s `.rdata` at the cell the decompiled C
// names (image base `0x10000000`, `.rdata` VA `0x10445000` at file offset `0x445000`), the method
// families Species and Senses used. Where a cell is a DOUBLE the listing says so with
// `FCOMP double ptr` and the comment repeats it — `_DAT_10449260` read as a float is 0.0 and the
// tentacle scatter cone would admit the whole forward half-plane instead of a 76-degree one.

namespace
{
	// --- Retail `.rdata`, one line per constant -----------------------------------------------------

	// The fixed eye-offset override `CAI_BaseNPC::FUN_10274db0` answers when the debug-overlay bit is
	// set: an immediate `0x41c00000` on Z with X and Y zeroed, i.e. 1.5 SOURCE units.
	constexpr float GDebugEyeOffsetZUnits = 1.5f;
	constexpr uint32 GDebugOverlayEyeOffsetBit = 0x8000000u;
	constexpr int32 GDebugEyeOffsetActivityA = 0x57;
	constexpr int32 GDebugEyeOffsetActivityB = 8;

	// The image's shared zero. `ResolveStandingOnHead` tests the XY delta against it for EXACT
	// equality, which is what selects the four-diagonal arm.
	constexpr float GGeometrySharedZero = ElysiumNpcTunables::Zero;

	// The two cells, with the immediates `0x3f34fdf4` and `0xbf34fdf4` (the same two numbers) stored
	// to the X slot. Not 1/sqrt(2) to full precision — retail's is the three-digit 0.707, and the
	// vector it makes is 0.99985 long, not 1.
	constexpr float GDiagonalPlus = ElysiumNpcTunables::StandOnHeadSpringPositive;
	constexpr float GDiagonalMinus = ElysiumNpcTunables::StandOnHeadSpringNegative;

	// `RandomFloat(-0.1, 0.1)` (`PUSH 0xbdcccccd; PUSH 0x3dcccccd`), the jitter added to the
	// normalised away-direction before it is normalised a SECOND time.
	constexpr float GStandingOnHeadJitter = 0.1f;

	// `_DAT_1049a1f4` = 0.1f SOURCE units, the Z lift applied to the trace start and subtracted back
	// off the destination; `_DAT_1049a1f8` = 40.0f, the push speed in SOURCE units per second.
	constexpr float GStandingOnHeadLiftUnits = ElysiumNpcTunables::StandingOnHeadLift;
	constexpr float GStandingOnHeadSpeedUnits = ElysiumNpcTunables::StandingOnHeadSpeed;

	// Seconds — the ceiling `m_flStandingOnHeadTimer` ramps to.
	constexpr float GStandingOnHeadTimerCeiling = ElysiumNpcTunables::Five;

	// The pooled 1.0f, read twice as a CLEAR trace fraction (`ResolveStandingOnHead`) and once as a
	// one-second interval (`UpdateFakeHull`'s damage gate).
	constexpr float GGeometryTraceClearFraction = ElysiumNpcTunables::One;

	// `0x202400b` — the trace mask both of `ResolveStandingOnHead`'s hull traces use, the same one
	// family Motor records for `CheckOnGround`, `ValidateNavGoal` and `GetGroundpoint`.
	constexpr int32 GStandingOnHeadTraceMask = 0x202400b;

}

// =================================================================================================
// Slot 193 — `EyePosition`, `0x100b4b40`, and `CPayphone`'s `0x101aae60`
// =================================================================================================

bool FElysiumNpc::BoneWorldPosition(const TCHAR* /*BoneName*/, FVector& /*OutPositionCm*/) const
{
	// SEAM. `CBaseAnimating::LookupBone` + `GetBonePosition02` for the payphone, and
	// `GetBoneTransform` + two `VectorTransform`s for the werewolf's `Bip01`. Nothing in this
	// substrate hands the kernel a bone table.
	++BoneWorldPositionCalls;
	return false;
}

// =================================================================================================
// Slots 194 and 195 — `EyeAngles` `0x100b4bc0` and `LocalEyeAngles` `0x100b4be0`
// =================================================================================================

// =================================================================================================
// Slot 197 — `BodyTarget(const Vector&, bool, bool)`, `0x102789c0`
// =================================================================================================

// =================================================================================================
// Slot 192 — the species-dispatched `WorldSpaceCenter`
// =================================================================================================

FVector FElysiumNpc::SpeciesWorldSpaceCenter() const
{
	// Slot 192's only species override in the census is `CNPC_Crow::vfunc192` (`0x10357760`,
	// `GetOrigin() + (0, 0, 6)`), and no map stands that class, so it carries no arm here. Every
	// class takes the Troika line's `0x10027160`, slot 192's own body, which is another story's row
	// and is still a generated stub.
	return const_cast<FElysiumNpc*>(this)->WorldSpaceCenter();
}

// =================================================================================================
// Slot 213 — `SetSize(const Vector&)`, `0x100b1890`
// =================================================================================================

// =================================================================================================
// Slot 337 — `GetUsedHullBits`, `0x1029a050`
// =================================================================================================

int32 FElysiumNpc::GetUsedHullBits()
{
	// The Troika line, `0x1029a050`:
	//
	//     1029a050  CALL 0x10012c1a           ; CAI_BaseNPC::GetUsedHullBits
	//     1029a055  OR AL,0x1
	//     1029a057  RET
	//
	// and `CAI_BaseNPC::GetUsedHullBits` is the same two instructions over `CBaseCombatCharacter::GetUsedHullBits`
	// (`0x10341710`), whose whole body past the breadcrumb pair is `return 1`. So the base answer is
	// 1 and BOTH ORs are no-ops — recovered, not a transcription slip, and the reason 29c's walk of
	// `0x1029a050` names `CBaseCombatCharacter` while the listing calls `CAI_BaseNPC`: the chain is
	// three deep and every rung adds the same bit.
	int32 Bits = BaseCombatCharacterHullBits;

	// Twelve species classes override this method (story 5 step 4): five OR a bit onto this answer
	// and seven replace it with a bare constant.
	return Bits;
}

// =================================================================================================
// Slot 533 — `EyeOffset(Activity, Activity)`, `0x102b4ab0` over `0x10274db0`
// =================================================================================================

const int32* FElysiumNpc::HintEyeOffsetActivities(int32& OutCount)
{
	// `0x102b4ab0`'s `switch`, in the listing's case order. Six activities and no others.
	static const int32 Activities[] = { 0x1119, 0x111a, 0x111b, 0x111c, 0x111f, 0x1120 };
	OutCount = UE_ARRAY_COUNT(Activities);
	return Activities;
}

FVector FElysiumNpc::DefaultEyeOffsetCm() const
{
	// `m_vDefaultEyeOffset` (`+0x5d60`). The shape map binds the word to `FElysiumEntity` with "no
	// stored view offset; the eye point is the chain's virtual `EyePosition()`", so the offset is
	// recovered from the eye point rather than stored twice.
	return EyePosition() - Origin;
}

FVector FElysiumNpc::BaseEyeOffset(int32 Activity) const
{
	// `CAI_BaseNPC::FUN_10274db0`, 92 bytes:
	//
	//     if ((vfunc0x804() & 0x8000000) != 0 && (act == 0x57 || act == 8))
	//         return Vector(0, 0, 1.5);
	//     return m_vDefaultEyeOffset;
	//
	// The `Activity` argument is retail's `param_2`; `param_3` (the second `Activity`) never reaches
	// an instruction in this body. Both immediates are literals, not `.rdata` cells: the X and Y
	// stores are `MOV 0` and the Z store is `MOV 0x41c00000`.
	if ((DebugOverlayBits() & GDebugOverlayEyeOffsetBit) != 0
		&& (Activity == GDebugEyeOffsetActivityA || Activity == GDebugEyeOffsetActivityB))
	{
		return FVector(0.f, 0.f, GDebugEyeOffsetZUnits * ElysiumMove::U);
	}
	return DefaultEyeOffsetCm();
}

FVector FElysiumNpc::EyeOffset(int32 Activity, int32 /*SecondActivity*/)
{
	// `CAI_BaseNPCTroika::FUN_102b4ab0`, 241 bytes. The hint arm only:
	//
	//     if (m_pHintNode != 0) switch (act) {
	//       case 0x1119: case 0x111a: case 0x111b: case 0x111c: case 0x111f: case 0x1120:
	//         pos = GetAbsOrigin();
	//         ApplyHintLeanOffset(&pos, false);              // thunk_FUN_102b6120
	//         HintStandPosition(m_pHintNode, this, &hint);   // thunk_FUN_102d1180
	//         return (hint - pos) + m_vDefaultEyeOffset;
	//     }
	//     return CAI_BaseNPC::FUN_10274db0(this, out, act, act2);
	//
	// Note what the hint arm does NOT consult: the debug-overlay bit, the second activity, and the
	// two special-cased activities of the base body. And note that `hint` is an UNINITIALISED stack
	// vector on the way into `0x102d1180` — retail reads whatever the hint query leaves.
	const int32 HintNode = BaseScheduleHost.HintNode;
	if (HintNode != INDEX_NONE)
	{
		int32 Count = 0;
		const int32* Activities = HintEyeOffsetActivities(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (Activities[Index] != Activity)
			{
				continue;
			}
			// SOURCE units through both seams, as family TroikaHelpers takes them.
			FVector StandUnits = Origin / ElysiumMove::U;
			ApplyHintLeanOffset(StandUnits, /*bStanding*/ false);
			FVector HintUnits = FVector::ZeroVector;
			HintStandPosition(HintNode, HintUnits);
			return (HintUnits - StandUnits) * ElysiumMove::U + DefaultEyeOffsetCm();
		}
	}
	// `thunk_FUN_10274db0(this, out, param_2, param_3)` — the base body, which reads only `param_2`.
	return BaseEyeOffset(Activity);
}

// =================================================================================================
// `CAI_BaseNPCTroika::ResolveStandingOnHead`, `0x102bf820`
// =================================================================================================

FVector FElysiumNpc::StandingOnHeadDiagonal(int32 DiagonalRoll)
{
	// `102bf966`..`102bf9bf`. The roll is `RandomInt(0, 3)` and the listing dispatches it with three
	// `DEC EAX; JZ`, so the fall-through (0, and anything outside 1..3) is the LAST arm written.
	// Each arm loads the Y component onto the FP stack from `.rdata` and stores the X component as
	// an immediate:
	//
	//     roll 1 -> Y = _DAT_1049aea8 (+0.707), X = 0xbf34fdf4 (-0.707)
	//     roll 2 -> Y = _DAT_1049aea4 (-0.707), X = 0x3f34fdf4 (+0.707)
	//     roll 3 -> Y = _DAT_1049aea4 (-0.707), X = 0xbf34fdf4 (-0.707)
	//     else   -> Y = _DAT_1049aea8 (+0.707), X = 0x3f34fdf4 (+0.707)
	//
	// The four diagonals of the XY plane. Z stays at the zero the delta was forced to.
	switch (DiagonalRoll)
	{
	case 1:  return FVector(GDiagonalMinus, GDiagonalPlus, 0.f);
	case 2:  return FVector(GDiagonalPlus, GDiagonalMinus, 0.f);
	case 3:  return FVector(GDiagonalMinus, GDiagonalMinus, 0.f);
	default: return FVector(GDiagonalPlus, GDiagonalPlus, 0.f);
	}
}

FElysiumNpc::FStandingOnHeadStep FElysiumNpc::StandingOnHeadStep(const FVector& MyOriginCm,
	const FVector& GroundOriginCm, float PreviousTimerSeconds, float IntervalSeconds,
	int32 DiagonalRoll, float JitterX, float JitterY)
{
	FStandingOnHeadStep Out;

	// `102bf8f1`..`102bf94e`: the delta from the ground entity's origin to mine, with **Z forced to
	// zero after it is computed** (`MOV dword ptr [ESP + 0x14],0x0` overwrites the Z difference the
	// two instructions before it just stored). The push is always horizontal.
	FVector Direction = MyOriginCm - GroundOriginCm;
	Direction.Z = 0.0;

	if (Direction.X == GGeometrySharedZero && Direction.Y == GGeometrySharedZero)
	{
		// Exactly co-located in XY — `FCOMP` against `_DAT_104454c4`, an exact float compare, on
		// both axes. Retail picks one of four diagonals rather than dividing by zero.
		Direction = StandingOnHeadDiagonal(DiagonalRoll);
	}
	else
	{
		// `102bf9c1`..`102bfa13`: normalise, add an independent `RandomFloat(-0.1, 0.1)` to X and to
		// Y, then normalise AGAIN. The jitter is applied to a UNIT vector, so it is a fixed angular
		// spread of about +-8 degrees and not a distance-dependent one.
		Direction.Normalize();
		Direction.X += JitterX;
		Direction.Y += JitterY;
		Direction.Normalize();
		Direction.Z = 0.0;
	}
	Out.Direction = Direction;

	// `102bfa17`..`102bfa3b`: `m_flStandingOnHeadTimer = min(timer + interval, 5.0)`. A RAMP, so a
	// body that has been stood on for a while is pushed harder than one that just was.
	Out.TimerSeconds = FMath::Min(PreviousTimerSeconds + IntervalSeconds,
		GStandingOnHeadTimerCeiling);

	// `102bfa41`..`102bfa52`: the trace starts at my origin lifted `_DAT_1049a1f4` (0.1 units) on Z,
	// and `102bfc78` subtracts that lift back off the destination before the move.
	Out.StartCm = MyOriginCm + FVector(0.f, 0.f, GStandingOnHeadLiftUnits * ElysiumMove::U);

	// `102bfa56`..`102bfaaf`: `dir * timer * 40 * interval`, the multiplications in that order.
	Out.DeltaCm = Out.Direction * Out.TimerSeconds * (GStandingOnHeadSpeedUnits * ElysiumMove::U)
		* IntervalSeconds;
	return Out;
}

void FElysiumNpc::ResolveStandingOnHead(float IntervalSeconds)
{
	// `0x102bf820`, 1,179 bytes. `IntervalSeconds` is retail's single stack argument — a float the
	// decompiler lost to `fStack_4`, read twice off the frame (`[ESP+0xac]` for the timer and
	// `[ESP+0xb0]` after a `PUSH EBP` for the distance).
	//
	// The first two arms are refusals and both land on the SAME write: `m_flStandingOnHeadTimer = 0`
	// (`LAB_102bfc9e`). The ramp only survives while an NPC is continuously standing on something.
	FElysiumEntity* Ground = GetGroundEntity();
	if (Ground == nullptr)
	{
		// `m_hGroundEntity` (`+0x384`) did not resolve. Slot 209 `GetGroundEntity` is another
		// story's row and is still a generated stub, so this is the arm this runtime always takes
		// today — stated rather than worked around.
		StandingOnHeadTimer = 0.f;
		return;
	}

	FVector GroundMinsUnits = FVector::ZeroVector;
	FVector GroundMaxsUnits = FVector::ZeroVector;
	if (!RetailCollisionExtents(*Ground, GroundMinsUnits, GroundMaxsUnits))
	{
		// Retail's second refusal is `ground->m_Collision == 0` (`+0x9c` on the resolved entity),
		// and the body reads the ground entity's origin THROUGH that collision object. Family
		// Motor's `RetailCollisionExtents` answers an NPC's OBB and nothing for any other entity, so a
		// false answer (a non-NPC ground) is the refusal.
		StandingOnHeadTimer = 0.f;
		return;
	}

	// `102bf8e6`: the ground position retail differences against is read off the collision object,
	// not off the entity, so it is the collideable's own origin. This runtime has one origin.
	FRandomStream& Stream = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);
	const int32 DiagonalRoll = Stream.RandRange(0, 3);
	const float JitterX = Stream.FRandRange(-GStandingOnHeadJitter, GStandingOnHeadJitter);
	const float JitterY = Stream.FRandRange(-GStandingOnHeadJitter, GStandingOnHeadJitter);

	const FStandingOnHeadStep Step = StandingOnHeadStep(Origin, Ground->Origin,
		StandingOnHeadTimer, IntervalSeconds, DiagonalRoll, JitterX, JitterY);
	StandingOnHeadTimer = Step.TimerSeconds;

	// `102bfaf3`..`102bfb74`: the hull trace, with this NPC's OWN collision mins and maxs, mask
	// `0x202400b`, and a filter built from `m_pMoveProbe`'s entity (`+0x5d40`) and its collision
	// group. Under a `CAI_MoveProbe_TraceHull` VProf scope, which is how the profiler names it.
	FVector MyMinsUnits = FVector::ZeroVector;
	FVector MyMaxsUnits = FVector::ZeroVector;
	RetailCollisionExtents(*this, MyMinsUnits, MyMaxsUnits);

	// `KernelHullTrace` is in SOURCE units (the port's axes); the step is in centimetres, so both
	// ends are converted at the call (0018 story 6: until then the trace was a seam that never read
	// them, and the centimetres went unnoticed).
	const double U = ElysiumMove::U;
	FVector EndCm = Step.StartCm + Step.DeltaCm;
	FKernelHullTrace Trace;
	KernelHullTrace(Step.StartCm / U, EndCm / U, MyMinsUnits, MyMaxsUnits, GStandingOnHeadTraceMask,
		Trace);

	// `102bfb7a`: a blocked or solid first trace and retail tries the OPPOSITE direction — the same
	// start, the delta SUBTRACTED. It does not renormalise and it does not redraw; it simply pushes
	// the other way.
	if (Trace.Fraction < GGeometryTraceClearFraction)
	{
		EndCm = Step.StartCm - Step.DeltaCm;
		KernelHullTrace(Step.StartCm / U, EndCm / U, MyMinsUnits, MyMaxsUnits, GStandingOnHeadTraceMask,
			Trace);
	}

	// `102bfc4d`: the second gate reads the SAME trace result slot, so an unblocked first trace
	// passes here without a second one having run.
	if (Trace.Fraction < GGeometryTraceClearFraction)
	{
		StandingOnHeadTimer = 0.f;
		return;
	}

	// `102bfc78`..`102bfc97`: subtract the Z lift back off, `SetAbsOrigin` (slot 216) and
	// `CBaseEntity::Relink`. The timer is NOT cleared on this arm — that is what lets the ramp
	// build while the push is succeeding.
	EndCm.Z -= GStandingOnHeadLiftUnits * ElysiumMove::U;
	SetRuntimeOrigin(EndCm);
}

// =================================================================================================
// `CNPC_VMingXiao`'s two severed-tentacle scatter notices
// =================================================================================================

void FElysiumNpc::NotifyScatterCenter(FElysiumEntity* Tentacle, const FVector& PositionCm)
{
	// `FUN_1039ef90`, 40 bytes:
	//
	//     (**(code **)(*DAT_10924a6c + 4))();      // a global object, unrecovered
	//     SetCondition(this, 0x78);                // 0x10269a20
	//     m_vecScatterCenter = *param_1;           // +0x668c on CNPC_VMingXiaoTentacle
	//
	// The condition is set on the NOTIFIED tentacle, not on the notifier, and so is the centre.
	++ScatterNoticeEvents;
	FElysiumNpc* TentacleNpc = Tentacle ? Tentacle->AsNpc() : nullptr;
	if (TentacleNpc == nullptr)
	{
		return;
	}
	TentacleNpc->Cognition.Conditions.Set(static_cast<EElysiumNpcCond>(ScatterNoticeCondition));
	TentacleNpc->TentacleScatterCenterUnits = PositionCm / ElysiumMove::U;
}

// Re-homed from the deleted `ElysiumNpcDebug10.cpp` (0019 story 6): a value the RunTask, StartTask
// and Werewolf anim-event rules read. Declared in `ElysiumNpcKernelBaseHelpers.inl`.
bool FElysiumNpc::RetailBonePosition(const TCHAR* BoneName, FVector& OutPositionUnits,
	FVector& OutAnglesDegrees) const
{
	// SEAM for `CBaseAnimating::GetBonePosition01(name, &pos, &ang)` (`0x1000f263`).
	(void)BoneName;
	OutPositionUnits = FVector::ZeroVector;
	OutAnglesDegrees = FVector::ZeroVector;
	return false;
}
