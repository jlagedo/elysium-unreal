#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
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

namespace
{
	// `UTIL_AngleDiff` `0x1013d580`: `a - b` walked back into `[-180, 180]` by whole turns, with
	// `_DAT_10462948 = -180.0f` and `_DAT_1044c3a8 = 180.0f`. Retail wraps only on the side the
	// `a <= b` test selects, and reproducing that is free.
	float RetailAngleDiff(float A, float B)
	{
		float Delta = A - B;
		if (A <= B)
		{
			while (Delta < -180.0f)
			{
				Delta += 360.0f;
			}
		}
		else
		{
			while (Delta > 180.0f)
			{
				Delta -= 360.0f;
			}
		}
		return Delta;
	}

	// `UTIL_VecToYaw` (`0x101d2c70`, reached through `0x1027db80`) over a delta in THIS world's
	// axes. `0x1027db80` answers the body's current angles for a zero vector rather than 0, so the
	// caller's `AngleDiff` against that same yaw lands on 0 — which is the arm reproduced here.
	float RetailYawOf(const FVector& PortDelta, float ZeroVectorYaw)
	{
		if (PortDelta.X == 0.0 && PortDelta.Y == 0.0 && PortDelta.Z == 0.0)
		{
			return ZeroVectorYaw;
		}
		return FMath::RadiansToDegrees(
			static_cast<float>(FMath::Atan2(-PortDelta.Y, PortDelta.X)));
	}

	// `VectorAngles` `0x10139970`: Source `[pitch yaw roll]` for a direction in this world's axes.
	FVector RetailVectorAngles(const FVector& PortDir)
	{
		const double Y = -PortDir.Y;   // back into Source's Y
		if (PortDir.X == 0.0 && Y == 0.0)
		{
			// Straight up is pitch 270 and straight down 90, which is `VectorAngles`' own
			// degenerate arm and the opposite of what the sign of Z suggests.
			return FVector(PortDir.Z > 0.0 ? 270.0 : 90.0, 0.0, 0.0);
		}
		float Yaw = FMath::RadiansToDegrees(static_cast<float>(FMath::Atan2(Y, PortDir.X)));
		if (Yaw < 0.0f)
		{
			Yaw += 360.0f;
		}
		const double Flat = FMath::Sqrt(PortDir.X * PortDir.X + Y * Y);
		float Pitch = FMath::RadiansToDegrees(static_cast<float>(FMath::Atan2(-PortDir.Z, Flat)));
		if (Pitch < 0.0f)
		{
			Pitch += 360.0f;
		}
		return FVector(Pitch, Yaw, 0.0);
	}

	// `AngleVectors` `0x10139550`, forward only — pitch AND yaw, unlike
	// `ElysiumSkeletalBasis::FromSourceAngles`, which is the yaw-only standing-body form.
	FVector RetailForward(const FVector& SourceAngles)
	{
		const float Pitch = FMath::DegreesToRadians(static_cast<float>(SourceAngles.X));
		const float Yaw = FMath::DegreesToRadians(static_cast<float>(SourceAngles.Y));
		return FVector(FMath::Cos(Pitch) * FMath::Cos(Yaw),
			-(FMath::Cos(Pitch) * FMath::Sin(Yaw)), -FMath::Sin(Pitch));
	}

	// Slots 370/371 that forward to 368/369 — the class answers its head aim with its BODY
	// direction. One row per class in this family's rows, each carrying the retail address of its
	// own 370 and 371 bodies so the table can be checked against `docs/vtmb/npc-kernel/slots.md`.
	struct FElysiumFacingHeadIsBodyRow
	{
		const TCHAR* RetailClass;
		const TCHAR* Body370;
		const TCHAR* Body371;
	};

	constexpr FElysiumFacingHeadIsBodyRow GHeadIsBody[] =
	{
		{ TEXT("CCineNPC"),            TEXT("0x101a6d40"), TEXT("0x101a6d70") },
		{ TEXT("CCineAI"),             TEXT("0x101a6d40"), TEXT("0x101a6d70") },
		{ TEXT("CCineAISchedule"),     TEXT("0x101a6d40"), TEXT("0x101a6d70") },
		{ TEXT("CPayphone"),           TEXT("0x101aa7f0"), TEXT("0x101aa820") },
		{ TEXT("CNPCMaker"),           TEXT("0x1034adf0"), TEXT("0x1034ae20") },
		{ TEXT("CNPCMaker_Fleshpile"), TEXT("0x1034be90"), TEXT("0x1034bec0") },
		{ TEXT("CNPCMaker_Zombie"),    TEXT("0x1034cad0"), TEXT("0x1034cb00") },
		{ TEXT("CNPC_VRat"),           TEXT("0x103ad7f0"), TEXT("0x103ad820") },
	};

	// Slot 465's species overrides. The Troika line's own body is `return;` (`0x10295a60`), which is
	// why each arm below ends by chaining to it and why the shared algorithm is the chain alone.
	struct FElysiumFacingActivityRow
	{
		const TCHAR* RetailClass;
		const TCHAR* Body465;
	};

	constexpr FElysiumFacingActivityRow GOnChangeActivity[] =
	{
		{ TEXT("CNPC_Crow"),          TEXT("0x10357b30") },
		{ TEXT("CNPC_VMingXiao"),     TEXT("0x103947b0") },
		{ TEXT("CNPC_VSabbatGunman"), TEXT("0x103a56f0") },
		{ TEXT("CNPC_VWerewolf"),     TEXT("0x103d5f60") },
	};

	// `thunk_FUN_101e8da0(0x10739d08)` — `CNPC_VMingXiao`'s own playback-tuning record, read by
	// field offset. **SEAM**: this substrate holds no such table, so every field answers 0 and the
	// non-discipline arm's blend lands on its floor.
	float MingXiaoTuningField(int32)
	{
		return 0.f;
	}
}

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
	// The retail cvar at `0x10924f74`, read as `!vtable[1]() && m_nValue != 0`. Its name and its
	// default are **unrecovered** — the pointer is uninitialised `.data` and no corpus function
	// constructs it. **SEAM**, answering false, which is what leaves slots 517/518/519 adding
	// nothing on top of the unmodelled queue below them.
	return false;
}

bool FElysiumNpc::TurningAnimsEnabled() const
{
	// The retail cvar at `0x109247ec`, the same `ConVar::GetBool()` shape, ORed with
	// `m_bAllowTurningAnims` by `SetTurnActivity` and read by both `MaxYawSpeed` overrides. Name and
	// default **unrecovered**. **SEAM**, answering false, which leaves the authored
	// `m_bAllowTurningAnims` (+0x65f9) as the live gate.
	return false;
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
	const FVector AimAngles = RetailVectorAngles(AimDirection);
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
	const float TargetYaw = RetailAngleDiff(RetailYawOf(YawDelta, BodyYaw), BodyYaw);
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

void FElysiumNpc::SetHeadDirectionHumanoid(const FVector& LookTarget, float Interval)
{
	// `CAI_BaseHumanoid::SetHeadDirection` `0x1025eaf0`, slot 537's branch override — 972 bytes, and
	// a different mechanism from the base above: pose parameters rather than bone controllers.
	//
	// Recovered shape, in order:
	//   * the same `CapabilitiesGet() & bits_CAP_TURN_HEAD` gate (+0x804);
	//   * `GetAttachment( "head", &pos, &ang )` (`0x105c8ed8` is the attachment name);
	//   * head yaw:   `AngleDiff( VecToYaw(target - pos), ang.y ) + GetPoseParameter(+0x5fe0)`,
	//                 written back through `SetPoseParameter(+0x5fe0, …)` (vtable +0x564);
	//   * head pitch: the same with the attachment's own pitch folded in, onto +0x5fe4;
	//   * chest yaw:  the third of the triple, onto +0x5fe8;
	//   * ten more paired `GetPoseParameter`/`SetPoseParameter` copies, from the source indices at
	//     +0x5fec..+0x6010 onto the destination indices at +0x5fb8..+0x5fdc;
	//   * `FlushBoneCache()`, then `m_flags(+0x5f4c) &= ~0x3` — the two cache bits
	//     `RefreshHumanoidHeadCache` sets, dropped so the next read re-derives.
	//
	// **SEAM**: the pose-parameter indices at +0x5fb8..+0x6010 are `CAI_BaseHumanoid`'s own words,
	// which no port member carries, and reaching the "head" attachment needs the animating tier's
	// bone cache, which this substrate does not expose to the kernel. Past the gate the body
	// therefore drops the cache bits — the one effect it can reproduce — and writes nothing.
	// Nothing a spawned `npc_*` dispatches through reaches it: slot 537 on the Troika line is the
	// base body above.
	if ((CapabilityWord & 0x1000) == 0)
	{
		return;
	}
	(void)LookTarget;
	(void)Interval;
	HumanoidHeadCacheBits &= ~static_cast<uint32>(0x3);
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

// --- `CAI_BaseHumanoid`'s cached head/eye basis --------------------------------------------------

void FElysiumNpc::RefreshHumanoidHeadCache() const
{
	// `0x1025e7b0`, the refresh both cached readers call first. Two independent latches in
	// +0x5f4c:
	//
	//   bit 0x2 — the head ORIGIN and the head DIRECTION. `GetAttachment("head", &origin, &ang)`;
	//             on success the direction is `AngleVectors(ang)`, on failure the origin falls back
	//             to `EyePosition()` (`0x100b4b40`) and the direction to `AngleVectors(GetAngles())`
	//             (vtable +0x374). Either way `0x10139550` writes the unit forward at +0x5f68.
	//   bit 0x1 — the EYE direction: `EyePosition()` (vtable +0x458) minus the cached head origin,
	//             normalized, at +0x5f5c.
	//
	// **SEAM**: the "head" attachment is the animating tier's, which the kernel cannot reach here,
	// so the refresh takes retail's own no-attachment arm every time — the head origin is the eye
	// point and the head direction is the body's forward. The eye direction is then the zero vector,
	// because the eye point and the head origin coincide on that arm; retail normalizes it anyway,
	// which is why nothing here guards the degenerate case either.
	if ((HumanoidHeadCacheBits & 0x2) == 0)
	{
		HumanoidHeadCacheBits |= 0x2;
		HumanoidHeadDirection = RetailForward(Angles);
		HumanoidHeadCacheBits &= ~static_cast<uint32>(0x1);
	}
	if ((HumanoidHeadCacheBits & 0x1) == 0)
	{
		HumanoidHeadCacheBits |= 0x1;
		// `EyePosition() - m_vHeadOrigin`, normalized. On the no-attachment arm above the head
		// origin IS the eye point, so the difference is the zero vector and the normalize leaves it
		// there — which is the recovered answer, not a placeholder.
		HumanoidEyeDirection = FVector::ZeroVector;
	}
}

FVector FElysiumNpc::HeadDirection3DHumanoid() const
{
	// `CAI_BaseHumanoid#371` `0x1025f160`: refresh, then return the cached vector at
	// +0x5f68/+0x5f6c/+0x5f70.
	RefreshHumanoidHeadCache();
	return HumanoidHeadDirection;
}

FVector FElysiumNpc::HeadDirection2DHumanoid() const
{
	// `CAI_BaseHumanoid#370` `0x1025f0f0`: call `HeadDirection3D` through +0x5cc, zero Z, normalize
	// (`0x10137220`). The 2-D form is the 3-D one flattened, the same pattern `EyeDirection2D` and
	// `BodyDirection2D` take.
	FVector Flat = HeadDirection3DHumanoid();
	Flat.Z = 0.0;
	return Flat.GetSafeNormal();
}

FVector FElysiumNpc::EyeDirection3DHumanoid() const
{
	// `CAI_BaseHumanoid#373` `0x1025f0b0`: refresh, then the cached vector at +0x5f5c/+0x5f60/
	// +0x5f64.
	RefreshHumanoidHeadCache();
	return HumanoidEyeDirection;
}

FVector FElysiumNpc::EyeDirection2DHumanoid() const
{
	// `CAI_BaseHumanoid#372` `0x1025f040`: `EyeDirection3D` through +0x5d4, Z zeroed, normalized.
	FVector Flat = EyeDirection3DHumanoid();
	Flat.Z = 0.0;
	return Flat.GetSafeNormal();
}

bool FElysiumNpc::HeadDirectionIsBodyDirection() const
{
	// The vtable's own rule, not a name compare: resolve slot 370's nearest override for the class
	// this NPC IS and ask whether that body is one of the forwards.
	const FElysiumNpcClassSlot* Row = ElysiumNpcKernelClass::OverrideOf(RetailClass(), 370);
	if (Row == nullptr || Row->Address == nullptr)
	{
		return false;
	}
	for (const FElysiumFacingHeadIsBodyRow& Known : GHeadIsBody)
	{
		if (FCString::Strcmp(Row->Address, Known.Body370) == 0)
		{
			return true;
		}
	}
	return false;
}

bool FElysiumNpc::RetailHeadDirection(bool b2D, FVector& OutDirection) const
{
	// What slots 370/371 answer for the class this NPC is. The slots THEMSELVES carry the Troika
	// line's bodies (`0x10331cb0` / `0x10331a40`'s sibling `0x10331b30`) and are the generator's,
	// not this family's; this is the species half standing beside them.
	if (HeadDirectionIsBodyDirection())
	{
		// `CCineNPC` `0x101a6d40`/`0x101a6d70` and its six siblings: 370 forwards to 368 and 371 to
		// 369. `CNPCMaker`'s trio reach the same place through a multiple-inheritance thunk on the
		// adjusted subobject, which is why their listings show a self-referential +0x5c0.
		OutDirection = b2D ? const_cast<FElysiumNpc*>(this)->BodyDirection2D()
			: const_cast<FElysiumNpc*>(this)->BodyDirection3D();
		return true;
	}
	if (IsRetailClass(TEXT("CAI_BaseHumanoid")))
	{
		OutDirection = b2D ? HeadDirection2DHumanoid() : HeadDirection3DHumanoid();
		return true;
	}
	return false;
}

// --- Slots 535/536 `AddLookTarget`, `CAI_BaseHumanoid`'s list ------------------------------------

void FElysiumNpc::AddLookTargetHumanoid(FElysiumEntity* LookAt, int32 Priority, float Duration,
	float Influence)
{
	// `CAI_BaseHumanoid::vfunc536` `0x1025f760`, 293 bytes. Remove any record already holding this
	// entity — resolving each stored handle and comparing the POINTER, so a stale handle never
	// matches — by shifting the tail down 0x24 bytes per record and decrementing the count; then
	// grow by one and fill the new tail record.
	const FElysiumEntityHandle LookAtHandle =
		LookAt != nullptr ? LookAt->Handle : FElysiumEntityHandle();
	for (int32 Index = 0; Index < LookTargets.Num(); ++Index)
	{
		const FElysiumEntity* Stored =
			World != nullptr ? World->Resolve(LookTargets[Index].Target) : nullptr;
		if (Stored == LookAt)
		{
			LookTargets.RemoveAt(Index);
			break;   // retail breaks on the first match, so a duplicate would survive
		}
	}
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	FLookTargetRecord Record;
	Record.Kind = 0;                       // +0x00
	Record.Target = LookAtHandle;          // +0x04, `0xffffffff` for a null entity
	Record.Priority = Priority;            // +0x20
	Record.StartTime = Now;                // +0x14, gpGlobals->curtime
	Record.EndTime = Now + Duration;       // +0x18
	Record.Rate = Influence / Duration;    // +0x1c — retail divides unguarded
	LookTargets.Add(Record);
}

void FElysiumNpc::AddLookTargetHumanoid(const FVector& Position, int32 Priority, float Duration,
	float Influence)
{
	// `CAI_BaseHumanoid::vfunc535` `0x1025f8e0`, 275 bytes. The same list management, matching an
	// existing record by its stored POSITION (all three components, by exact float equality) instead
	// of by entity, and writing kind 1 with the position at +0x08.
	for (int32 Index = 0; Index < LookTargets.Num(); ++Index)
	{
		if (LookTargets[Index].Position == Position)
		{
			LookTargets.RemoveAt(Index);
			break;
		}
	}
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	FLookTargetRecord Record;
	Record.Kind = 1;
	Record.Position = Position;
	Record.Priority = Priority;
	Record.StartTime = Now;
	Record.EndTime = Now + Duration;
	Record.Rate = Influence / Duration;
	LookTargets.Add(Record);
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
				ScheduleHost.MemoryBits |= 0x2000;   // +0x5d8c m_afMemory
			}
			SetIdealActivityNumber(Pick.Activity);
			return;
		}
	}
	SetIdealActivityNumber(1);
}

// --- Slot 465's species half --------------------------------------------------------------------

FElysiumNpc::FMotionTrailPick FElysiumNpc::SabbatGunmanMotionTrail(float GroundSpeed,
	float SpeedThreshold, int32 TrailId, float TrailScalar)
{
	// `CNPC_VSabbatGunman::OnChangeActivity` `0x103a56f0`: at or below the threshold the trail is
	// cleared and the playback scalar is -1.0 — retail's "no scalar" literal, not a speed; above it
	// both come from the other two convars.
	if (GroundSpeed <= SpeedThreshold)
	{
		return FMotionTrailPick{ 0, -1.0f };
	}
	return FMotionTrailPick{ TrailId, TrailScalar };
}

FElysiumNpc::FMingXiaoPlayback FElysiumNpc::MingXiaoPlaybackScalar(int32 Activity,
	bool bDisciplineArm, int32 TentacleCount, TFunctionRef<float(int32)> TuningField)
{
	// `CNPC_VMingXiao::OnChangeActivity` `0x103947b0`. Two arms of three rows each, selected by
	// `0x10398870` — the gate `docs/vtmb/animation_and_movers.md` names "+0x6674". The activity
	// numbers are the listing's raw words read as floats: 9 (`ACT_WALK`), 0x13 (`ACT_RUN`), 0x4b and
	// 0x1132.
	FMingXiaoPlayback Out;
	Out.bWalkOrRun = (Activity == 9 || Activity == 0x13);
	if (bDisciplineArm)
	{
		// A flat scalar straight off the record: +0x1c for walk/run, +0x18 for 0x4b, +0x14 otherwise.
		if (Out.bWalkOrRun)
		{
			Out.Scalar = TuningField(0x1c);
			return Out;
		}
		if (Activity == 0x4b)
		{
			// This arm writes the scalar and leaves at once, taking the second tail below without a
			// second `SetPlaybackAndSpeedScalar`.
			Out.Scalar = TuningField(0x18);
			Out.bSecondWriteSkipped = true;
			return Out;
		}
		Out.Scalar = TuningField(0x14);
		return Out;
	}
	// The tentacle arm: a base plus a per-tentacle term over `6 - m_iConnectedTentacleCount`
	// (+0x670c), floored at `_DAT_104493d0 = 0.1`.
	float Base = 0.f;
	float Per = 0.f;
	if (Out.bWalkOrRun)
	{
		Base = TuningField(0x5c);
		Per = TuningField(0x60);
	}
	else if (Activity == 0x4b)
	{
		Base = TuningField(0x54);
		Per = TuningField(0x58);
	}
	else
	{
		Base = TuningField(0x4c);
		Per = TuningField(0x50);
	}
	Out.Scalar = Per * static_cast<float>(6 - TentacleCount) + Base;
	if (Out.Scalar <= 0.1f)
	{
		Out.Scalar = 0.1f;
	}
	return Out;
}

void FElysiumNpc::OnChangeActivitySpecies(int32 Activity)
{
	// Slot 465's four species overrides, dispatched through the census rather than through a name
	// compare. `CAI_BaseNPCTroika::OnChangeActivity` `0x10295a60` — the body the generator emits on
	// `FElysiumNpc::OnChangeActivity` — is `return;`, and every arm here ends by chaining to it, so
	// the base's emptiness IS the shared algorithm.
	const FElysiumNpcClassSlot* Row = ElysiumNpcKernelClass::OverrideOf(RetailClass(), 465);
	const TCHAR* BodyAddress = nullptr;
	if (Row != nullptr && Row->Address != nullptr)
	{
		// The table is the gate as well as the record: an override this family did not recover is
		// not one of these four, and it takes the base chain rather than a guess.
		for (const FElysiumFacingActivityRow& Known : GOnChangeActivity)
		{
			if (FCString::Strcmp(Row->Address, Known.Body465) == 0)
			{
				BodyAddress = Known.Body465;
				break;
			}
		}
	}
	if (BodyAddress == nullptr)
	{
		OnChangeActivity(Activity);
		return;
	}

	if (FCString::Strcmp(BodyAddress, TEXT("0x10357b30")) == 0)
	{
		// `CNPC_Crow::OnChangeActivity`, 40 bytes: one activity, one write.
		//     if (act == 0x22) m_flCycle = RandomFloat( 0.0f, 0.75f );
		// `0x3f400000` is the 0.75 in the listing and `DAT_1070b244` is `VEngineRandom001`. A crow
		// entering activity 0x22 starts its clip at a random phase so a flock does not beat in step.
		// **SEAM**: the kernel reaches no `m_flCycle` here — the animating tier owns the phase — so
		// the draw is taken (the stream must advance identically) and discarded. Named decision: the
		// draw goes on `EElysiumRngStream::Ambient`, the stream this runtime's other idle-phase
		// picks use.
		if (Activity == 0x22)
		{
			ElysiumRng::Stream(EElysiumRngStream::Ambient).FRandRange(0.f, 0.75f);
		}
	}
	else if (FCString::Strcmp(BodyAddress, TEXT("0x103947b0")) == 0)
	{
		// `CNPC_VMingXiao::OnChangeActivity`, 303 bytes. **SEAM** on all three inputs: the gate
		// `0x10398870`, the tuning record `0x101e8da0(0x10739d08)` and the tentacle count (+0x670c)
		// have no port source, so the discipline arm is false, every field answers 0 and the blend
		// lands on its 0.1 floor. The two tails `0x1039ab30` and `0x1039aca0` are MingXiao's own and
		// are named here rather than invented.
		const FMingXiaoPlayback Pick = MingXiaoPlaybackScalar(Activity, /*bDisciplineArm*/ false,
			/*TentacleCount*/ 0, [](int32 Field) { return MingXiaoTuningField(Field); });
		(void)Pick;   // SetPlaybackAndSpeedScalar has no kernel-tier seam in this substrate
	}
	else if (FCString::Strcmp(BodyAddress, TEXT("0x103a56f0")) == 0)
	{
		// `CNPC_VSabbatGunman::OnChangeActivity`, 160 bytes. **SEAM** on all four inputs: the three
		// convars `DAT_1093c104`/`DAT_1093c14c`/`DAT_1093c1f4` and `m_flGroundSpeed` (+0x0654). A
		// convar retail cannot read answers 0 — the body spells that arm itself — so the threshold
		// is 0, the stopped arm is the one taken, and the trail is cleared.
		const FMotionTrailPick Pick = SabbatGunmanMotionTrail(/*GroundSpeed*/ 0.f,
			/*SpeedThreshold*/ 0.f, /*TrailId*/ 0, /*TrailScalar*/ 0.f);
		MotionTrail = Pick.MotionTrail;   // +0x1484
	}
	else if (FCString::Strcmp(BodyAddress, TEXT("0x103d5f60")) == 0)
	{
		// `CNPC_VWerewolf::OnChangeActivity`, 126 bytes: the scope-trace push/pop and an
		// unconditional forward. There is no species behaviour here at all, and recording that is
		// the point — a reader looking for one stops at this line.
	}

	OnChangeActivity(Activity);
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

float FElysiumNpc::GetFacingTimeToTeleport() const
{
	// `CNPC_VChangBros::GetFacingTimeToTeleport` `0x1036dc60`, 138 bytes:
	//     if (m_iSquadDisconnected < 1 && m_pSquad != NULL && NumSquadMembers(m_pSquad) > 1)
	//         return 21.0f;                                  // _DAT_104ada0c
	//     return 7.0f;                                       // _DAT_104ada08
	// The Chang brothers wait three times as long before teleporting while the other one is still
	// standing. `ConnectedSquad()` is already this runtime's `m_iSquadDisconnected < 1 ? m_pSquad :
	// NULL` (+0x5bb0, +0x5da4) and answers nothing, because no squad object exists here — so the
	// long arm is unreachable and the answer is the lone-brother 7 seconds.
	if (ConnectedSquad() != nullptr)
	{
		// `thunk_FUN_103160a0(m_pSquad) > 1` — the squad's member count. **SEAM**, unreachable
		// today.
		return 21.0f;
	}
	return 7.0f;
}

void FElysiumNpc::FacePlayerAdvance()
{
	// `CNPC_VAndreiBlood::FacePlayerAdvance` `0x1035e5f0`, 165 bytes:
	//     if (m_hClosestPlayer resolves)
	//         m_pMotor->thunk_FUN_102e20b0( player->GetAbsOrigin(), 10.0f );
	// `DAT_104a6f80 = 10.0f` is the fixed turn rate, and the call is the motor's
	// set-ideal-yaw-to-target. The whole body is the gate plus that one command.
	const FElysiumPlayer* Player =
		Senses.Memory.ClosestPlayer.IsSet() && World != nullptr ? World->FindPlayer() : nullptr;
	if (Player == nullptr || Player->IsInert() || Player->Handle != Senses.Memory.ClosestPlayer)
	{
		return;
	}
	// **SEAM**: `IElysiumNpcMotor::Face` takes a yaw, not a target, and no turn RATE crosses it, so
	// the recovered 10.0 has nowhere to land yet. The commanded yaw is retail's own — the yaw from
	// this body to the player's origin.
	if (IElysiumNpcMotor* Mover = Motor)
	{
		const float TargetYaw =
			RetailYawOf(Player->Origin - Origin, static_cast<float>(Angles.Y));
		// `Face` is in this world's yaw, which is the negated Source one.
		Mover->Face(-TargetYaw);
	}
}

bool FElysiumNpc::PlayerIsFacingMe() const
{
	// `CNPC_VSabbatLeader::PlayerIsFacingMe` `0x103aaf50`, 349 bytes, read from the listing because
	// the decompiler dropped the in-place normalize:
	//     if (!m_hClosestPlayer resolves) return false;
	//     AngleVectors( player->GetAbsAngles(), &fwd );      // full pitch+yaw forward
	//     Vector d = GetAbsOrigin() - player->GetAbsOrigin();
	//     float len = VectorNormalize( d );
	//     if (len > 0.0001f && DotProduct( fwd, d ) < 0.34202f) return false;
	//     return true;
	// `_DAT_104c3ce4 = 9.999999747378752e-05f` and `_DAT_104c3cf0 = 0.3420200049877167f`, which is
	// cos(70 degrees): a 140-degree cone, and a player standing ON the leader (inside the length
	// epsilon) counts as facing him.
	const FElysiumPlayer* Player =
		Senses.Memory.ClosestPlayer.IsSet() && World != nullptr ? World->FindPlayer() : nullptr;
	if (Player == nullptr || Player->IsInert() || Player->Handle != Senses.Memory.ClosestPlayer)
	{
		return false;
	}
	const FVector PlayerForward = RetailForward(Player->Angles);
	FVector Delta = Origin - Player->Origin;
	const float Length = static_cast<float>(Delta.Size());
	Delta = Delta.GetSafeNormal();
	if (Length > 0.0001f && static_cast<float>(FVector::DotProduct(PlayerForward, Delta)) < 0.34202f)
	{
		return false;
	}
	return true;
}

void FElysiumNpc::UpdateFacingTimer()
{
	// `CNPC_VChangBros::UpdateFacingTimer` `0x1036d600`, 392 bytes. Three nested gates; passing ALL
	// of them leaves `m_fFacingTime` alone, and anything else resets it to now — so the timer
	// measures how long the player has been standing close, level and looking this way.
	//
	//     Vector d = GetAbsOrigin() - player->GetAbsOrigin();
	//     if (|d.z| < 50.0)                                          // _DAT_104ada10
	//     {
	//         Vector flat( d.x, d.y, 0 );  float len = VectorNormalize( flat );
	//         if (len < 150.0 && 1e-05 < len)                        // _DAT_104ada14, _DAT_104ad9fc
	//         {
	//             QAngle a;  VectorAngles( flat, a );
	//             if (|AngleDiff( player->GetAbsAngles().y, a.y )| < 70.0) return;   // _DAT_104ada18
	//         }
	//     }
	//     m_fFacingTime = gpGlobals->curtime;
	//
	// The distances are Source units and this world is centimetres, hence `ElysiumMove::U`. The yaw
	// term compares the PLAYER's yaw against the yaw of the brother-minus-player delta, so it asks
	// whether the player is pointed at the brother — not the other way round.
	const FElysiumPlayer* Player =
		Senses.Memory.ClosestPlayer.IsSet() && World != nullptr ? World->FindPlayer() : nullptr;
	const bool bPlayerValid = Player != nullptr && !Player->IsInert()
		&& Player->Handle == Senses.Memory.ClosestPlayer;
	if (bPlayerValid)
	{
		const FVector Delta = Origin - Player->Origin;
		if (FMath::Abs(Delta.Z) < 50.0 * ElysiumMove::U)
		{
			FVector Flat(Delta.X, Delta.Y, 0.0);
			const float Length = static_cast<float>(Flat.Size());
			Flat = Flat.GetSafeNormal();
			if (Length < 150.0f * ElysiumMove::U && 1e-05f * ElysiumMove::U < Length)
			{
				const FVector FlatAngles = RetailVectorAngles(Flat);
				const float YawDelta = RetailAngleDiff(static_cast<float>(Player->Angles.Y),
					static_cast<float>(FlatAngles.Y));
				if (FMath::Abs(YawDelta) < 70.0f)
				{
					return;
				}
			}
		}
	}
	FacingTime = World != nullptr ? World->NowSeconds() : 0.0;
}
