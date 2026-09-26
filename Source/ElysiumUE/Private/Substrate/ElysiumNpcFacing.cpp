#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcFacingShared.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumSkeletalBasis.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"

// Story 29c-1, family **Facing** — the facing-target queue and the turn-activity ladder.
//
// 44 rows of `order.md` layers 0–9, ported arm by arm in retail's order. Every threshold below was
// read out of the pinned retail `vampire.dll`'s `.rdata` at its cited address (image base
// `0x10000000`, `.rdata` RVA == file offset in this image), so the numbers are recovered facts, not
// estimates. The walked prose is `docs/vtmb/npc-ai/shape.md`.
//
// Two coordinate facts govern every angle here and are stated once:
//
//   * `FElysiumEntity::Angles` is **Source** `[pitch yaw roll]` in degrees, exactly as retail's
//     `GetAngles()` (slot 221) answers it, while `Origin` is this world's axes, where
//     `bsp.source_to_unreal` has negated Y. A yaw derived from a port-space delta is therefore the
//     NEGATED Unreal one, and the ladders are written against retail's sign, in which a positive
//     yaw delta turns LEFT. `RetailYawOf` below is the one place that conversion happens.
//   * Retail's distance constants are Source units; this world is centimetres. `ElysiumMove::U` is
//     the 2.54 that bridges them, applied at the point of use so the recovered constant stays
//     visible.
// --- The `CAI_Motor` seam -----------------------------------------------------------------------

float FElysiumNpc::MotorDeltaIdealYaw() const
{
	// `CAI_Motor::DeltaIdealYaw` `0x102e1f90`:
	//     float cur = UTIL_AngleMod( GetOuter()->GetAngles().y );
	//     if (cur == m_IdealYaw) return 0.0f;                     // _DAT_104454c4
	//     return UTIL_AngleDiff( m_IdealYaw, cur );
	// with `UTIL_AngleMod(a) = 0.0054931640625f * (int(a * 65536/360) & 65535)` — the 16-bit
	// quantisation, whose leading constant is `_DAT_1044ffdc`.
	//
	// `IElysiumNpcMotor` takes a commanded yaw through `Face()` and keeps no `m_IdealYaw`, so the
	// seam's stored answer stands in for the whole expression; the formula is recorded here for the
	// day the mover carries one.
	return MotorIdealYawDelta;
}

void FElysiumNpc::MotorAddFacingTarget(const FFacingTargetRequest& Request)
{
	// `CAI_Motor` slots 12/13/14 — `0x102e2150`, `0x102e2120`, `0x102e20f0`. All three are a
	// 32-byte forward of their parameters into the motor's `m_facingQueue` (motor+0x54). **SEAM**:
	// no queue exists, so the request is recorded and adds nothing.
	FacingTargetRequests.Add(Request);
}

bool FElysiumNpc::FacingTargetsEnabled() const
{
	// The retail cvar at `0x10924f74`, read as `!vtable[1]() && m_nValue != 0`:
	// `debug_allow_move_facing`, shipped "1" — the facing-target family is ON.
	return ElysiumNpcTunables::ConVarInt(ElysiumNpcTunables::EConVar::DebugAllowMoveFacing) != 0;
}

bool FElysiumNpc::TurningAnimsEnabled() const
{
	// The retail cvar at `0x109247ec`, the same `ConVar::GetBool()` shape, ORed with
	// `m_bAllowTurningAnims` by `SetTurnActivity` and read by both `MaxYawSpeed` overrides:
	// `debug_turning`, shipped "0", so the authored `m_bAllowTurningAnims` (+0x65f9) is the live gate.
	return ElysiumNpcTunables::ConVarInt(ElysiumNpcTunables::EConVar::DebugTurning) != 0;
}

int32 FElysiumNpc::SelectWeightedSequenceForActivity(int32) const
{
	// `CBaseAnimating::SelectWeightedSequence(Activity, -1)` on the base line, `0x10295460` on the
	// Troika line. **SEAM**: this substrate resolves activities by NAME and stands no sequence
	// index at the kernel tier, so nothing is authored and the ladder walks to its `ACT_IDLE` tail —
	// retail's own answer for a body with no turn clips.
	return -1;
}

void FElysiumNpc::SetIdealActivityNumber(int32 Activity)
{
	// `CAI_BaseNPC::SetIdealActivity` `0x10272650`, the half this family is measured by:
	//     if (act == ACT_INVALID) { vtable[0x4d8](); return; }       // slot 342's reset arm
	//     m_IdealActivity = act;                                     // +0x0ff0
	//     ResetIdeal(act, &m_nIdealSequence, &m_IdealTranslatedActivity, &m_IdealWeaponActivity);
	// `0x10272130`'s translation chain is story 29d's; the store is reproduced and the three ideal
	// words beside it are left for it.
	IdealActivityNumber = Activity;
}

void FElysiumNpc::SetPoseParameterByName(const TCHAR* Name, float Value)
{
	// `CBaseAnimating::SetPoseParameter(const char*, float)`, slot 345 (retail vtable +0x564).
	// **SEAM**: the animating tier exposes no pose-parameter surface to the kernel, so the write is
	// recorded and goes no further.
	PoseParameterWrites.Add(FPoseParameterWrite{ FString(Name), Value });
}

// --- Slot 277 `SetViewtarget` -------------------------------------------------------------------

void FElysiumNpc::SetViewtarget(const FVector& NewViewtarget)
{
	// `0x100b5b00`, the whole body: three floats into `m_viewtarget` (+0x0848). Retail networks the
	// word; nothing in this substrate reads it yet.
	Viewtarget = NewViewtarget;
}

// --- Slots 517/518/519 `AddFacingTarget` --------------------------------------------------------
//
// All three are the same 71-byte shape: the cvar gate, then a TAIL JUMP into `m_pMotor` (+0x5d44)
// with `this` swapped for the motor and the arguments untouched. 29c's walk read the frame fix-up
// after `POP ESI` as an argument shift; the listing (`0x10278cb0`) shows the four stack slots
// written back to themselves, so nothing shifts.

void FElysiumNpc::AddFacingTarget(FElysiumEntity* FaceEntity, float Duration, float Ramp,
	float Tolerance)
{
	// `0x10278cb0`, slot 519 — the entity overload, motor slot 14 (`0x102e20f0`).
	if (!FacingTargetsEnabled())
	{
		return;
	}
	FFacingTargetRequest Request;
	Request.MotorSlot = 14;
	Request.Target = FaceEntity != nullptr ? FaceEntity->Handle : FElysiumEntityHandle();
	Request.Duration = Duration;
	Request.Ramp = Ramp;
	Request.Tolerance = Tolerance;
	MotorAddFacingTarget(Request);
}

void FElysiumNpc::AddFacingTarget(const FVector& Position, float Duration, float Ramp,
	float Tolerance)
{
	// `0x10278d20`, slot 518 — the position overload, motor slot 13 (`0x102e2120`).
	if (!FacingTargetsEnabled())
	{
		return;
	}
	FFacingTargetRequest Request;
	Request.MotorSlot = 13;
	Request.Position = Position;
	Request.Duration = Duration;
	Request.Ramp = Ramp;
	Request.Tolerance = Tolerance;
	MotorAddFacingTarget(Request);
}

void FElysiumNpc::AddFacingTarget(FElysiumEntity* FaceEntity, const FVector& Position, float Duration,
	float Ramp, float Tolerance)
{
	// `0x10278d90`, slot 517 — the entity-plus-offset overload, motor slot 12 (`0x102e2150`).
	if (!FacingTargetsEnabled())
	{
		return;
	}
	FFacingTargetRequest Request;
	Request.MotorSlot = 12;
	Request.Target = FaceEntity != nullptr ? FaceEntity->Handle : FElysiumEntityHandle();
	Request.Position = Position;
	Request.Duration = Duration;
	Request.Ramp = Ramp;
	Request.Tolerance = Tolerance;
	MotorAddFacingTarget(Request);
}

// --- Slot 520 `GetFacingDirection`, slot 526 `OverrideMoveFacing` --------------------------------

float FElysiumNpc::GetFacingDirection(FVector& OutDirection)
{
	// `0x10278e00`, 19 bytes: `return m_pMotor->vtable[0x3c](out);` — motor slot 15. **SEAM**: the
	// motor keeps no facing state to read back, so the direction is left untouched and the weight
	// is 0.
	(void)OutDirection;
	return 0.f;
}

bool FElysiumNpc::OverrideMoveFacing(void*, float)
{
	// `0x1027d9f0`: retail's `g_ScopeTraceStack` push/pop around an unconditional `return false;`.
	// The bookkeeping is retail's own debug stack, which Unreal's stat/trace system replaces; the
	// ANSWER is the rule, and it is what every species override of slot 526 is measured against —
	// the base declines to take over a move's facing, always.
	return false;
}

// --- Slot 539 `SetAim` --------------------------------------------------------------------------

void FElysiumNpc::SetAim(const FVector& AimDirection)
{
	// `0x1026b480`, 79 bytes:
	//     QAngle angles; VectorAngles( aim, angles );          // 0x10139970
	//     SetPoseParameter( "aim_pitch", angles.x );           // vtable +0x564, slot 345
	//     SetPoseParameter( "aim_yaw",   0.0f );
	// The yaw argument is a hard zero in the listing (`uVar1 = 0` pushed as the float), not the
	// computed yaw: retail aims the pitch pose and pins the yaw pose at neutral.
	const FVector AimAngles = NpcKernelFacingShared::FacingRetailVectorAngles(AimDirection);
	SetPoseParameterByName(TEXT("aim_pitch"), static_cast<float>(AimAngles.X));
	SetPoseParameterByName(TEXT("aim_yaw"), 0.f);
}

// --- Slot 537 `SetHeadDirection` ----------------------------------------------------------------

void FElysiumNpc::SetHeadDirection(FVector& LookTarget, float Interval)
{
	// `CAI_BaseNPC::SetHeadDirection` `0x1026af70`, the Troika line's fill of slot 537. Read from
	// the listing, because the decompiler lost the pitch half's register aliasing.
	//
	// Constants, all out of retail `.rdata`: `_DAT_104454c4 = 0.0f`, `_DAT_1047049c = 0.8f`,
	// `_DAT_1049954c = 0.2f`, `_DAT_104493d0 = 0.1` (a **double**, subtracted as one),
	// `_DAT_10450568 = 360.0f`, `_DAT_10446758 = 57.29578f` (radians to degrees).
	if ((CapabilityWord & 0x1000) == 0)   // bits_CAP_TURN_HEAD; `CapabilitiesGet` 0x1026db30
	{
		return;
	}

	// The yaw half, from GetOrigin (slot 220) and GetAngles (slot 221).
	const float BodyYaw = static_cast<float>(Angles.Y);
	const FVector YawDelta = LookTarget - Origin;
	const float TargetYaw = NpcKernelFacingShared::FacingRetailAngleDiff(NpcKernelFacingShared::RetailYawOf(YawDelta, BodyYaw), BodyYaw);
	if (Interval > 0.0f)
	{
		// A do/while: an interval at or under one step still integrates once.
		float Value = HeadYaw;
		float Remaining = Interval;
		do
		{
			Value = Value * 0.8f + TargetYaw * 0.2f;
			Remaining -= 0.1f;
		} while (Remaining > 0.0f);
		HeadYaw = Value;
	}
	if (HeadYaw > 360.0f)
	{
		// Retail's lone guard, and it is one-sided: a filter that ran away negative is left alone.
		HeadYaw = 0.f;
	}
	// `SetBoneController(0, m_flHeadYaw)` `0x10095cb0` returns the value CLAMPED to the controller's
	// authored range and retail assigns it back. `Studio_SetController` returns its argument
	// unchanged when the model declares no controller at that index, and no shipped VtMB model
	// declares one — the same fact `FElysiumCombatCharacter::FilterHeadTurn` records — so the
	// assignment is the identity and the value never reaches a skeleton, so it is left out rather
	// than written as a self-assignment.

	// The pitch half, from EyePosition (slot 193) and the FULL 3-D distance to the target.
	const FVector Eye = EyePosition();
	const FVector PitchDelta = LookTarget - Eye;
	const float Distance = static_cast<float>(PitchDelta.Size());
	const float TargetPitch = -FMath::RadiansToDegrees(
		FMath::Atan(static_cast<float>(PitchDelta.Z) / Distance));
	if (Interval > 0.0f)
	{
		float Value = HeadPitch;
		float Remaining = Interval;
		do
		{
			Value = Value * 0.8f + TargetPitch * 0.2f;
			Remaining -= 0.1f;
		} while (Remaining > 0.0f);
		HeadPitch = Value;
	}
	if (HeadPitch > 360.0f)
	{
		HeadPitch = 0.f;
	}
	// `SetBoneController(1, m_flHeadPitch)` — and retail does NOT assign this one back, unlike the
	// yaw. Reproduced as the asymmetry it is.
}

// --- Slots 372/373 `EyeDirection2D` / `EyeDirection3D` -------------------------------------------

FVector FElysiumNpc::EyeDirection2D()
{
	// `0x1026b210`, 20 bytes: a tail jump through this object's own vtable at +0x5c8, which is
	// slot 370 — `HeadDirection2D`. The base tier has no independent eye aim, and the forward IS the
	// whole behaviour.
	return HeadDirection2D();
}

FVector FElysiumNpc::EyeDirection3D()
{
	// `0x1026b240`, 20 bytes: the same forward through +0x5cc, slot 371 — `HeadDirection3D`.
	return HeadDirection3D();
}

// --- Slot 572 `SetTurnActivity` -----------------------------------------------------------------

FElysiumNpc::FTurnActivityPick FElysiumNpc::TurnActivityBaseLadder(float YawDelta,
	TFunctionRef<bool(int32)> HasSequence)
{
	// `CAI_BaseNPC::SetTurnActivity` `0x10289d10`. Five rungs against the motor's yaw delta, in
	// retail's order; the first whose activity the body authors wins, and the ladder falls to
	// `ACT_IDLE` (1) when none does. The first three tag `m_afMemory |= 0x2000`; the last two do
	// not, and that asymmetry is the base line's alone — the Troika ladder tags every pick.
	//
	// `_DAT_1049a198 = -80.0f`, `_DAT_1049a194 = -100.0f`, `_DAT_104454c8 = 80.0f`,
	// `_DAT_10450564 = 100.0f`, `_DAT_1049a188 = 160.0` (a **double**), `_DAT_1049a180 = -45.0f`,
	// `_DAT_1049949c = 45.0f`.
	if (YawDelta < -80.0f && -100.0f <= YawDelta && HasSequence(0xa2))
	{
		return FTurnActivityPick{ 0xa2, true };   // ACT_90_RIGHT
	}
	if (80.0f <= YawDelta && YawDelta < 100.0f && HasSequence(0xa1))
	{
		return FTurnActivityPick{ 0xa1, true };   // ACT_90_LEFT
	}
	if (160.0f <= FMath::Abs(YawDelta) && HasSequence(0x9d))
	{
		return FTurnActivityPick{ 0x9d, true };   // ACT_180_LEFT
	}
	if (YawDelta < -45.0f && HasSequence(0x3c))
	{
		return FTurnActivityPick{ 0x3c, false };  // ACT_TURN_RIGHT
	}
	if (45.0f <= YawDelta && HasSequence(0x3b))
	{
		return FTurnActivityPick{ 0x3b, false };  // ACT_TURN_LEFT
	}
	return FTurnActivityPick{ 1, false };         // ACT_IDLE
}

FElysiumNpc::FTurnActivityPick FElysiumNpc::TurnActivityTroikaLadder(float YawDelta,
	TFunctionRef<bool(int32)> HasSequence)
{
	// `CAI_BaseNPCTroika::SetTurnActivity` `0x10297640`, the body slot 572 carries for every
	// spawnable species. Wider bands than the base, two rungs the base has no equivalent of, and the
	// 180 rungs are ORDERED PAIRS — past 140 degrees retail asks for the turn that matches the sign
	// first and accepts the other one if the body does not author it.
	//
	// `_DAT_1049ae40 = -70.0f`, `_DAT_1049ae3c = -140.0f`, `_DAT_104528d4 = 70.0f`,
	// `_DAT_1049ae38 = 140.0f`, `_DAT_1049ae34 = -15.0f`, `_DAT_10463584 = 15.0f`.
	if (YawDelta < -70.0f && -140.0f <= YawDelta && HasSequence(0xa2))
	{
		return FTurnActivityPick{ 0xa2, true };   // ACT_90_RIGHT
	}
	if (70.0f <= YawDelta && YawDelta < 140.0f && HasSequence(0xa1))
	{
		return FTurnActivityPick{ 0xa1, true };   // ACT_90_LEFT
	}
	if (YawDelta < -140.0f)
	{
		if (HasSequence(0x9e))
		{
			return FTurnActivityPick{ 0x9e, true };   // ACT_180_RIGHT
		}
		if (HasSequence(0x9d))
		{
			return FTurnActivityPick{ 0x9d, true };   // ACT_180_LEFT
		}
	}
	if (140.0f < YawDelta)
	{
		if (HasSequence(0x9d))
		{
			return FTurnActivityPick{ 0x9d, true };
		}
		if (HasSequence(0x9e))
		{
			return FTurnActivityPick{ 0x9e, true };
		}
	}
	if (YawDelta < -15.0f && HasSequence(0x3c))
	{
		return FTurnActivityPick{ 0x3c, true };   // ACT_TURN_RIGHT — tagged here, unlike the base
	}
	if (15.0f <= YawDelta && HasSequence(0x3b))
	{
		return FTurnActivityPick{ 0x3b, true };   // ACT_TURN_LEFT
	}
	return FTurnActivityPick{ 1, false };         // ACT_IDLE
}

void FElysiumNpc::SetTurnActivity()
{
	// Slot 572's Troika-line body, `0x10297640`. The gate is `cvar(0x109247ec) ||
	// m_bAllowTurningAnims`, and when it is closed the body still falls through to the `ACT_IDLE`
	// tail — the turn is simply never animated.
	if (TurningAnimsEnabled() || bAllowTurningAnims)
	{
		const FTurnActivityPick Pick = TurnActivityTroikaLadder(MotorDeltaIdealYaw(),
			[this](int32 Activity) { return SelectWeightedSequenceForActivity(Activity) != -1; });
		if (Pick.Activity != 1)
		{
			if (Pick.bTagsTurnMemory)
			{
				BaseScheduleHost.MemoryBits |= 0x2000;   // +0x5d8c m_afMemory
			}
			SetIdealActivityNumber(Pick.Activity);
			return;
		}
	}
	SetIdealActivityNumber(1);
}

// --- The facing readers and writers that fill no slot --------------------------------------------

void FElysiumNpc::ClearFacingTarget()
{
	// `CAI_Motor` slot 7 `0x102e11f0`, 95 bytes:
	//     SetIdealYaw( -1 );                         // thunk 0x102e1e20
	//     Vector v; GetFacingTarget( v );            // thunk 0x102e26b0, fills three floats
	//     GetOuter()->vtable[0x4d8]( v.y > 0.0f ? 0x2d : 0x2e );   // slot 342
	//     field_0x30 = 0;
	// **SEAM**: this substrate models neither the motor's ideal yaw nor its facing queue, so the
	// recorded requests are dropped and nothing else happens. The 0x2d/0x2e pair and the sentinel
	// compare against `_DAT_104454c4 = 0.0f` are recorded here so the arm is not rediscovered; which
	// of the two the outer NPC receives is **unrecovered** — slot 342's argument space is not
	// settled.
	FacingTargetRequests.Reset();
}

bool FElysiumNpc::FacingIdeal() const
{
	// `0x10278c80`, 32 bytes, read from the listing because the decompiler turned the FPU compare
	// into a strict `<`:
	//     FABS( m_pMotor->DeltaIdealYaw() );  FCOMP double [0x10499568];  TEST AH,0x41;  JP …
	// less-OR-EQUAL returns true. `_DAT_10499568` is a **double** — `0.006` — not the float the
	// overlapping symbol at that address reads as. This is the SDK's own `FacingIdeal`, tolerance
	// included.
	return FMath::Abs(MotorDeltaIdealYaw()) <= 0.006f;
}

