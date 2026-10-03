#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcKernelBaseHelpersShared.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcThinkCadence.h"
#include "Substrate/ElysiumPlaceSet.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"   // ElysiumWeapons::ItemRangeWords — the weapon's `+0x8c0`

// Story 29c-1, family **BaseHelpers** — `CAI_BaseNPC`'s own unnamed layer 0–9 bodies.
//
// 40 rows: the fifteen Troika-line vtable slots whose stubs the generator left for this family, and
// the twenty-five helpers, free functions and branch overrides beside them. The walked prose is
// spread by concern, as the brief asks: `docs/vtmb/npc-ai/shape.md` for the geometry helpers,
// `conditions-and-states.md` for the attack-condition ladder and the victim-side reaction slots,
// `schedule-kernel.md` for the hint validators and the turn ladder, `lifecycle.md` for the
// think-clock forwards.
//
// THE STANDING FACT OF THIS FAMILY:
//
//   * The six `CAI_BaseHumanoid` / `CAI_BaseActor` rows it once carried (`0x1025e780`,
//     `0x1025f1a0`, `0x1025ea00`, `0x10260540`, `0x10260670`, `0x10260750`) were deleted by 0019
//     story 5 step 1: the class has no instance (`population.md`), and the rows stay in the census.
//   * **Three rows are already carried by the port and are NOT re-stood here.** `0x10272790`
//     (`ShouldMaintainActivity`'s base arm) is family Anim's `FElysiumNpc::ShouldMaintainActivity`;
//     `0x10298910` and `0x102989e0` are `FElysiumNpc::ParseGroupMask` and its two setters. Adding a
//     second copy of a rule the port already reproduces arm for arm is the drift this story exists
//     to end.

namespace
{
	// --- The image constants these bodies read -------------------------------------------------
	//
	// One named constant per `_DAT_` the decompiled C reads, with the address it is read from.
	// `vampire.dll`'s `.rdata` values are NOT in the corpus (it exposes referrers, not bytes), so a
	// value is recovered only where an oracle document pins that address or where the SDK 2013 twin
	// of the same arm states it. Anything else says UNRECOVERED and the arm that reads it says what
	// it does instead.

	constexpr float GDatOne = ElysiumNpcTunables::One;
	constexpr float GDatZero = ElysiumNpcTunables::Zero;
	// The clear-trace fraction both LOS arms compare against: a DOUBLE cell, compared as a float.
	constexpr float GDatClearFraction = static_cast<float>(ElysiumNpcTunables::OneDouble);
	constexpr float GDatNearDistanceUnits = ElysiumNpcTunables::FiveHundredTwelve;
	constexpr float GDatFaceAnimTurnYaw = ElysiumNpcTunables::Forty;
	// Family Facing recovered the Troika turn ladder's own pair, and the top rung of `0x10297a20`
	// reads the SAME two addresses: `_DAT_1049ae3c = -140.0f`, `_DAT_1049ae38 = 140.0f`.
	constexpr float GDatFaceAnimYawLow = ElysiumNpcTunables::MinusOneForty;   // _DAT_1049ae3c
	constexpr float GDatFaceAnimYawHigh = ElysiumNpcTunables::OneForty;       // _DAT_1049ae38

	// Read 2026-09-21 (`docs/vtmb/npc-ai/rdata-cells.md`) and held by the tunables table since
	// 0019/4; each stood at a 0.0 stand-in before.
	constexpr float GDatFaceAnimYawMid = ElysiumNpcTunables::FaceTurnSecondEdge;   // 0x10297a20's rung 2
	constexpr float GDatFaceAnimRandomScale = ElysiumNpcTunables::AngleQuantum;    // 0x10297a20's draw
	constexpr float GDatDistanceEpsilon = ElysiumNpcTunables::FloatEpsilon;        // the 1/(d+eps) guard
	constexpr float GDatCoverForwardMin = ElysiumNpcTunables::Half;                // 0x10295ed0's 0x283d arm

	// `CAI_Hint::m_nHintType` 0x283d — `0x10295ed0`'s one type-specific extra projection test.
	constexpr int32 GHintTypeCoverForward = 0x283d;

	// The mask `0x102961a0`'s inline ray pushes (`10296579`), the same one `0x102968f0` pushes.
	constexpr int32 GCoverTwinLosMask = 0x46804099;

	// --- The cover validators' shared geometry (`0x10295ed0` / `0x102961a0`) --------------------
	//
	// Both bodies work in Source units and SOURCE AXES; the port's world is centimetres with Y
	// negated. The conversion only shows in the facing projection (a length is blind to it), and it
	// is the same one `ValidateHintCoverRange` (`0x10296c40`, `ElysiumNpcHints.cpp`) makes.
	FVector CoverSourceUnits(const FVector& Cm)
	{
		return FVector(Cm.X, -Cm.Y, Cm.Z) / static_cast<double>(ElysiumMove::U);
	}

	// `0x102d12e0(hint)` over the hint's words — `HintYaw` (`ElysiumNpcBaseHelpers2.cpp`), restated
	// exactly as `ValidateHintCoverRange` restates it: a hint bound to a network node
	// (`m_nNodeID +0x5e4 != -1`) answers the NODE's yaw (`0x102f47b0`, node `+0x6c`, 0.0 past the
	// network); a standalone hint its own `GetAbsAngles().y`. A Source-frame yaw on both arms.
	float CoverHintYawSource(const FElysiumEntityWorld* World, const FElysiumNpcBase::FHintWords& Hint)
	{
		if (Hint.NodeId != INDEX_NONE)
		{
			return World != nullptr ? World->Places().NetworkNodeYawSource(Hint.NodeId) : 0.0f;
		}
		return static_cast<float>(Hint.Angles.Y);
	}

	// What the band-and-projection half of both validators answers.
	enum class ECoverGeometry : uint8
	{
		Pass,
		OutOfBand,      // "Distance (%d) < %d or > %d"                 (0x102961a0's string)
		OutsideRange,   // "Enemy outside of good range (%.2f) <= %.2f" (0x102961a0's string)
	};

	// What the run computed on the way: the 2-D distance (both reason strings' `%d` operand), the
	// facing projection (the `"Enemy outside of good range"` `%.2f`, valid once `bProjection`) and the
	// facing `0x101d2f40` produced, handed back because `0x10295ed0`'s 0x283d arm dots with it again.
	struct FCoverGeometryValues
	{
		float Dist = 0.f;
		bool bProjection = false;
		float Projection = 0.f;
		float FaceX = 0.f;
		float FaceY = 0.f;
	};

	// `10295f32`..`1029603c` (and `102962ae`..`102963ff`, the twin's identical run).
	ECoverGeometry CoverGeometry(const FElysiumEntityWorld* World, const FElysiumNpcBase::FHintWords& Hint,
		const FVector& CoverObjectCm, bool bIsCurrentHint, FCoverGeometryValues& Out)
	{
		// `coverObject->GetAbsOrigin() - hint->GetAbsOrigin()` (slot 217 on both), X and Y only, and
		// its 2-D length through `[0x10579660]` (the sqrt), stored as a float.
		const FVector HintUnits = CoverSourceUnits(Hint.OriginCm);
		const FVector CoverUnits = CoverSourceUnits(CoverObjectCm);
		const float DeltaX = static_cast<float>(CoverUnits.X - HintUnits.X);
		const float DeltaY = static_cast<float>(CoverUnits.Y - HintUnits.Y);
		const float Dist = FMath::Sqrt(DeltaX * DeltaX + DeltaY * DeltaY);
		Out.Dist = Dist;
		const float Band = NpcKernelBaseHelpersShared::GDatAttackBandUnits;   // `_DAT_10451acc` = 64
		if (bIsCurrentHint)
		{
			// `10295f83`: `FLD min; FSUB 64; FCOMP dist; AND EAX,0x4100 / JZ` continues only on an
			// ordered `min - 64 < dist` or `==`; a greater OR UNORDERED compare fails.
			if (!(Hint.TargetDistMin - Band <= Dist))
			{
				return ECoverGeometry::OutOfBand;
			}
			// `10295f9c`: `FLD max; FADD 64; FCOMP dist; TEST AH,5 / JP` continues on `>=` and on
			// unordered; only an ordered `max + 64 < dist` fails.
			if (Hint.TargetDistMax + Band < Dist)
			{
				return ECoverGeometry::OutOfBand;
			}
		}
		else
		{
			// `10295fbf`: `FLD dist; FCOMP min; TEST AH,5 / JNP` fails only an ordered `dist < min`.
			if (Dist < Hint.TargetDistMin)
			{
				return ECoverGeometry::OutOfBand;
			}
			// `10295fd0`: `FLD dist; FCOMP max; AND EAX,0x4100 / JZ` continues only on an ordered
			// `dist < max` or `==`; a greater OR UNORDERED compare fails.
			if (!(Dist <= Hint.TargetDistMax))
			{
				return ECoverGeometry::OutOfBand;
			}
		}
		// `10295fe3`: the delta scaled by `_DAT_104454c0 / (dist + _DAT_1046a51c)` (1 / (d + FLT_EPSILON)),
		// then `0x102d12e0` -> `0x101d2f40`'s `(cos, sin)` — a Source-frame yaw against Source-axis
		// deltas — and `faceY * (dy * s) + (dx * s) * faceX`.
		const float Scale = GDatOne / (Dist + GDatDistanceEpsilon);
		const float YawRadians = FMath::DegreesToRadians(CoverHintYawSource(World, Hint));
		Out.FaceX = FMath::Cos(YawRadians);
		Out.FaceY = FMath::Sin(YawRadians);
		const float Projection = Out.FaceY * (DeltaY * Scale) + (DeltaX * Scale) * Out.FaceX;
		Out.bProjection = true;
		Out.Projection = Projection;
		// `10296031`: `FCOMP m_flTargetAngleRangeDot (+0x458); TEST AH,0x41 / JNP` fails an ordered
		// `<` or `==` — STRICTLY greater continues, and so does an unordered compare.
		if (Projection <= Hint.TargetAngleRangeDot)
		{
			return ECoverGeometry::OutsideRange;
		}
		return ECoverGeometry::Pass;
	}

	// The retail `Activity` numbers this family's bodies name. Spelled here because the port has no
	// retail activity table (family Hints' `RestartIdealActivityId` says why).
	constexpr int32 GBaseHelpersActIdle = 1;                // ACT_IDLE — the tail of the face-anim ladder
	constexpr int32 GBaseHelpersActScriptCustomMove = 0x18; // ACT_SCRIPT_CUSTOM_MOVE
	constexpr int32 GBaseHelpersActDisposition = 0xf1;      // ACT_DISPOSITION, slot 588's restart

	// `0x10297a20`'s four turn programs, in ladder order.
	constexpr int32 GFaceAnimAct180 = 0x10ff;
	constexpr int32 GFaceAnimAct90 = 0x10fd;
	constexpr int32 GFaceAnimAct45 = 0x10fa;
	constexpr int32 GFaceAnimActSmall = 0x10f8;

}

// =================================================================================================
// `CAI_BaseNPC`'s own helpers.
// =================================================================================================

// 0x1028ebc0 — can I see this point? Retail name unrecovered; no caller in the corpus.
bool FElysiumNpc::FUN_1028ebc0(const FVector& PointCm) const
{
	// Three gates, in order, and the whole 674-byte body is nothing else:
	//   1. `CBaseCombatCharacter::FInViewCone(point, m_flFieldOfView /*+0x1574*/)` — `0x103268e0`,
	//      which picks the 2-D or the 3-D cone off a ConVar;
	//   2. `|EyePosition() - point|² <= m_flVisionDistance² (+0x63b8)`; retail computes the
	//      "beyond" predicate and takes the FAIL path on true;
	//   3. an engine ray from the eye to the point, passing only at `fraction == 1.0`
	//      (`_DAT_10449280`). The ConVar-gated pass between the trace and the compare
	//      (`thunk_FUN_10143d80` / `thunk_FUN_10142e90`) is the debug-overlay draw and changes
	//      nothing the trace answered.
	const FVector Eye = EyePosition();
	// `m_flFieldOfView` (+0x1574) is the observer's own cone threshold, which this port's cone test
	// reads internally; the target cone scalar is retail's 1.0 literal.
	if (!FElysiumNpcSenses::IsInViewCone(*this, PointCm, 1.0f))
	{
		return false;
	}
	const double VisionCm = static_cast<double>(Senses.Perception.VisionDistanceCm);
	if (FVector::DistSquared(Eye, PointCm) > VisionCm * VisionCm)
	{
		return false;
	}
	// Family Motor's `KernelHullTrace` is the kernel tier's `(*DAT_1070b254)->TraceRay` (0018 story
	// 6). The mask handed here is 0, which traces nothing and answers `Fraction == 1.0` -- retail's
	// clear line -- so this gate passes and the refusal above it is what decides.
	FKernelHullTrace Trace;
	KernelHullTrace(Eye / ElysiumMove::U, PointCm / ElysiumMove::U, FVector::ZeroVector,
		FVector::ZeroVector, 0, Trace);
	return Trace.Fraction >= GDatClearFraction;
}

// 0x102906a0 / 0x102906c0 / 0x10290700 — `IsThinkDue` (`0x10290660`) over three named clocks
bool FElysiumNpc::IsUpdateThinkDue() const
{
	// `IsThinkDue(stamp)` is `(stamp - curtime) <= frametime`: the decompiler renders the FPU
	// compare as `(a < ft) != (a == ft)`, which is `a <= ft`. `ElysiumNpcThink::IsDue` is the port's
	// own spelling of it and the four think clocks already run off it.
	return World != nullptr
		&& ElysiumNpcThink::IsDue(ScheduleHost.NextUpdate, World->NowSeconds(),
			World->FrameSeconds());
}

bool FElysiumNpc::IsNormalThinkDue() const
{
	return World != nullptr
		&& ElysiumNpcThink::IsDue(ScheduleHost.NextNormal, World->NowSeconds(),
			World->FrameSeconds());
}

bool FElysiumNpc::IsAiThinkDue() const
{
	return World != nullptr
		&& ElysiumNpcThink::IsDue(ScheduleHost.NextAI, World->NowSeconds(), World->FrameSeconds());
}

// slot 527 0x10293e80 `bool IsUnusableNode(CAI_Node*)`
bool FElysiumNpc::IsUnusableNode(void* Node)
{
	// `node->+0xa0` is the node's `CAI_Hint*`. A node with NO hint is usable; a node with one is
	// unusable exactly when `0x102d1540` says the hint is NOT available to me. Retail:
	//
	//   uVar1 = 0;
	//   if (node->hint) { uVar1 = IsHintAvailable(hint, this); if (!uVar1) return 1; }
	//   return 0;
	//
	// `Node` is retail's `CAI_Node*`, kept as `void*` by the generated signature: in this runtime it
	// is the place set's row (`const FElysiumPlaceRow*`, `IsUnusableNodeIndex` hands it), whose
	// network index finds the node's hint. `IsHintAvailableToMe` stays story 8's seam (true).
	const FElysiumPlaceRow* Row = static_cast<const FElysiumPlaceRow*>(Node);
	if (Row == nullptr || World == nullptr)
	{
		return false;
	}
	const FElysiumEntityHandle Hint = World->Places().AttachedHint(Row->NetworkIndex);   // node +0xa0
	if (!Hint.IsSet())
	{
		return false;
	}
	return !IsHintAvailableToMe(Hint.Index);                                   // 0x102d1540
}

// 0x1029f610 — the patrol path's network check. Retail name unrecovered.
bool FElysiumNpc::FUN_1029f610(const FPatrolPathCell* Cell) const
{
	// `if (cell && cell->path (+0x4)) return 0x10307ac0(cell->path, m_pNavigator->+0x2c); return 0;`
	//
	// `0x10307ac0(path, network)`: a null network answers false; then every id the path holds
	// (`+0x14`, `+0xc` of them), in order -- `id < 0` answers false with nothing counted, `*network
	// <= id` bumps `DAT_106c994c` and answers false, a null network slot answers false. All present:
	// true. The network is the world's place set, whose every loaded slot holds a node.
	if (Cell == nullptr || Cell->Path == nullptr)                              // 0x1029f614 / 0x1029f61c
	{
		return false;
	}
	if (World == nullptr)                                                      // 0x10307ac5 null network
	{
		return false;
	}
	const FElysiumPlaceSet& Places = World->Places();
	const FPatrolPathRecord& Path = *Cell->Path;
	for (int32 Index = 0; Index < Path.Count && Index < PatrolPathNodeCapacity; ++Index)
	{
		const int32 NodeId = Path.Nodes[Index];
		if (NodeId < 0)                                                        // 0x10307ad8 JL
		{
			return false;
		}
		if (Places.NumNodes() <= NodeId)                                       // 0x10307adc CMP
		{
			++PatrolNodeMissCounter();                                         // DAT_106c994c++
			return false;
		}
	}
	return true;
}

// 0x1029f650 — the patrol node's interesting-place draw
bool FElysiumNpc::FUN_1029f650(int32 PatrolNode)
{
	// `m_bPatrolPathUseHint = 0; record = PatrolNodeInterestRecord(node); if (record && Random(0,99) <
	// record->m_iIPPercent (+0x46c)) m_bPatrolPathUseHint = 1; return m_bPatrolPathUseHint;`
	//
	// The reset happens FIRST and unconditionally, so a node with no record clears a standing flag.
	ScheduleHost.bPatrolPathUseHint = false;
	const int32 Record = PatrolNodeInterestRecord(PatrolNode);
	if (Record != INDEX_NONE)
	{
		// `(*DAT_1070b244)->+8` is the engine's `RandomInt(0, 99)`.
		const int32 Roll = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(0, 99);
		if (Roll < PatrolNodeInterestPercent(Record))
		{
			ScheduleHost.bPatrolPathUseHint = true;
		}
	}
	return ScheduleHost.bPatrolPathUseHint;
}

// 0x1029f730 — the cached read side of that draw
int32 FElysiumNpc::FUN_1029f730(int32 PatrolNode)
{
	// `if (!m_bPatrolPathUseHint) return 0; if (!cache) cache = PatrolNodeInterestRecord(node); return cache;`
	// The cache word is `+0x659c`, which 29b reserved as `ScheduleHost.Unknown659c` and both Troika
	// teardown virtuals clear.
	if (!ScheduleHost.bPatrolPathUseHint)
	{
		return 0;
	}
	if (ScheduleHost.Unknown659c == 0)
	{
		const int32 Record = PatrolNodeInterestRecord(PatrolNode);
		ScheduleHost.Unknown659c = Record == INDEX_NONE ? 0u : static_cast<uint32>(Record);
	}
	return static_cast<int32>(ScheduleHost.Unknown659c);
}

// =================================================================================================
// The two cover validators. The third hint validator, the attack-position check `0x10296c40`, is
// family Hints' `ValidateHintCoverRange` (`ElysiumNpcHints.cpp`); the older duplicate this family
// carried (`FUN_10296c40` / `AttackHintRejectReason`) was deleted by 0018 story 8.
// =================================================================================================

bool FElysiumNpc::CoverHintStillValid(const FHintWords& Hint, const FVector& CoverObjectCm,
	const FVector& MyOriginCm, bool bIsCurrentHint) const
{
	// `0x10295ed0`'s rule, arm for arm off the listing, minus the cover-object resolve and the
	// `0x102968f0` tail (the entry point holds both); `schedule-kernel.md` § "The three hint
	// validators".
	//
	//   null / m_iDisabled                        -> fail
	//   the 2-D band (current hint widened by 64) -> fail outside it            (`CoverGeometry`)
	//   facing projection > TargetAngleRangeDot, else fail                    (`CoverGeometry`)
	//   current hint                              -> ACCEPT
	//   |hint - me| <= 512, else fail; type 0x283d: forward projection > 0.5, else fail
	//
	// `10295edf` / `10295eed`: a null hint, or `m_iDisabled` (`+0x5e8`) set, fails.
	if (!Hint.bValid || Hint.Disabled != 0)
	{
		return false;
	}
	FCoverGeometryValues Geometry;
	const ECoverGeometry GeometryVerdict = CoverGeometry(World, Hint, CoverObjectCm, bIsCurrentHint,
		Geometry);
	if (IsHintDebugNpc())
	{
		// `0x10295ed0` is the QUIET twin: it formats no string on any arm. Only the numbers it
		// computed are recorded for the `ai_debug_npc` readout.
		FElysiumAiDebugHintProbe& Probe = World->AiDebugHintProbe();
		Probe.bDistance = true;
		Probe.DistanceUnits = Geometry.Dist;
		Probe.bFacing = Geometry.bProjection;
		Probe.FacingProjection = Geometry.Projection;
		Probe.GoodRange = Hint.TargetAngleRangeDot;
	}
	if (GeometryVerdict != ECoverGeometry::Pass)
	{
		return false;
	}
	const float FaceX = Geometry.FaceX;
	const float FaceY = Geometry.FaceY;
	// `10296042`: the hint I already hold (`m_pHintNode +0x5ddc`) accepts here.
	if (bIsCurrentHint)
	{
		return true;
	}
	// `1029604e`: `hint->GetAbsOrigin() - GetAbsOrigin()`, all three axes, into a buffer that
	// `0x10137220` (`VectorNormalize`) normalises IN PLACE — `v *= 1 / (|v| + _DAT_1046a51c)`, Z
	// included — answering the length. (The decompiled C loses the in-place write and reads the raw
	// delta in the 0x283d arm; the listing dots the NORMALISED buffer at `[ESP+0x20]` / `[ESP+0x24]`.)
	const FVector ToHint = CoverSourceUnits(Hint.OriginCm) - CoverSourceUnits(MyOriginCm);
	const float Length = static_cast<float>(
		FMath::Sqrt(ToHint.Z * ToHint.Z + ToHint.Y * ToHint.Y + ToHint.X * ToHint.X));
	const float Inverse = GDatOne / (GDatDistanceEpsilon + Length);
	const float DirX = static_cast<float>(ToHint.X) * Inverse;
	const float DirY = static_cast<float>(ToHint.Y) * Inverse;
	// `102960a3`: `FCOMP _DAT_10483aac (512); AND EAX,0x4100 / JZ` continues only on an ordered
	// `length < 512` or `==`; a greater OR UNORDERED length fails.
	if (!(Length <= GDatNearDistanceUnits))
	{
		return false;
	}
	// `102960b6`: for `m_nHintType` (`+0x5dc`) 0x283d only, the normalised direction dotted in X and
	// Y with the SAME facing `0x101d2f40` produced above; `FCOMP _DAT_104454d0 (0.5); TEST AH,0x41 /
	// JNP` fails an ordered `<=`, so strictly greater — or unordered — continues.
	if (Hint.HintType == GHintTypeCoverForward)
	{
		const float ForwardDot = DirY * FaceY + DirX * FaceX;
		if (ForwardDot <= GDatCoverForwardMin)
		{
			return false;
		}
	}
	return true;
}

bool FElysiumNpc::FUN_10295ed0(int32 HintNode) const
{
	// The entry point. `10295ef3`: `m_hHintCoverObject` (`+0x6448`) must resolve, AFTER the null and
	// disabled gates (which the rule restates; they write nothing, so the order cannot show).
	FHintWords Hint;
	if (!HintWords(HintNode, Hint))
	{
		return false;
	}
	if (IsHintDebugNpc())
	{
		FElysiumAiDebugHintProbe& Probe = World->AiDebugHintProbe();
		Probe.Reset();
		Probe.HintIndex = Hint.HintIndex;
		Probe.Validator = TEXT("0x10295ed0");
	}
	FElysiumEntity* CoverObject = (World != nullptr)
		? const_cast<FElysiumEntityWorld*>(World)->Resolve(ScheduleHost.HintCoverObject) : nullptr;
	if (CoverObject == nullptr)
	{
		return false;
	}
	const bool bIsCurrentHint = HintNode == BaseScheduleHost.HintNode;
	if (!CoverHintStillValid(Hint, CoverObject->Origin, Origin, bIsCurrentHint))
	{
		return false;
	}
	// `102960e5`: `0x102968f0(this, hint, coverObject)` — the hint LOS check, with the COVER OBJECT
	// as its target, decides every hint that is not the current one (which accepted at `10296048`).
	return bIsCurrentHint || HintLosCheck(HintNode, CoverObject);
}

FElysiumNpc::EHintRejectReason FElysiumNpc::CoverHintRejectReason(const FHintWords& Hint,
	const FVector& CoverObjectCm, bool bIsCurrentHint) const
{
	// `0x102961a0`'s rule — the verbose twin. It shares `0x10295ed0`'s band and projection
	// (`102962ae`..`102963ff` is the same run, compare for compare: `CoverGeometry`) and differs in
	// three ways: a `target_name` gate in FRONT of everything, a single shared
	// "Distance (%d) < %d or > %d" reason for both band policies, and an INLINE LOS ray (which the
	// entry point runs) instead of `0x102968f0`. Null and disabled fail with no string.
	if (!Hint.bValid)
	{
		return EHintRejectReason::NoHint;
	}
	if (Hint.Disabled != 0)
	{
		return EHintRejectReason::Disabled;
	}
	// `102961c8`: `m_strTargetName` (`+0x468`) against MY `m_iName` (`+0x26c`) through `0x1043e780`,
	// case-insensitive. An empty target name admits everyone; a set one admits only the NPC it names.
	if (!Hint.TargetName.IsEmpty() && !Hint.TargetName.Equals(TargetName, ESearchCase::IgnoreCase))
	{
		// `10296222`: the operand is the HINT's `m_strTargetName` (`+0x468`), not my name.
		if (IsHintDebugNpc())
		{
			HintDebugNote(Hint, TEXT("0x102961a0"),
				FString::Printf(TEXT("Target name mismatch (%s)"), *Hint.TargetName));
		}
		return EHintRejectReason::TargetNameMismatch;   // "Target name mismatch (%s)"
	}
	FCoverGeometryValues Geometry;
	const ECoverGeometry Verdict = CoverGeometry(World, Hint, CoverObjectCm, bIsCurrentHint, Geometry);
	if (IsHintDebugNpc())
	{
		FElysiumAiDebugHintProbe& Probe = World->AiDebugHintProbe();
		Probe.bDistance = true;
		Probe.DistanceUnits = Geometry.Dist;
		Probe.bFacing = Geometry.bProjection;
		Probe.FacingProjection = Geometry.Projection;
		Probe.GoodRange = Hint.TargetAngleRangeDot;
	}
	switch (Verdict)
	{
	case ECoverGeometry::OutOfBand:
		// `10296689`..`102966ab`: `__ftol` (truncation) of `m_flTargetDistMax`, `m_flTargetDistMin`
		// and the distance, pushed in that order — so the string reads distance, min, max, and the
		// bounds are the RAW words even on the current hint's 64-widened band.
		if (IsHintDebugNpc())
		{
			HintDebugNote(Hint, TEXT("0x102961a0"), FString::Printf(TEXT("Distance (%d) < %d or > %d"),
				static_cast<int32>(Geometry.Dist), static_cast<int32>(Hint.TargetDistMin),
				static_cast<int32>(Hint.TargetDistMax)));
		}
		return EHintRejectReason::DistanceOutOfBand;    // "Distance (%d) < %d or > %d"
	case ECoverGeometry::OutsideRange:
		// `10296435`..`10296447`: the projection first, `m_flTargetAngleRangeDot` second, as doubles.
		if (IsHintDebugNpc())
		{
			HintDebugNote(Hint, TEXT("0x102961a0"),
				FString::Printf(TEXT("Enemy outside of good range (%.2f) <= %.2f"),
					static_cast<double>(Geometry.Projection), static_cast<double>(Hint.TargetAngleRangeDot)));
		}
		return EHintRejectReason::OutsideGoodRange;     // "Enemy outside of good range (%.2f) <= %.2f"
	case ECoverGeometry::Pass:
		break;
	}
	return EHintRejectReason::None;
}

FElysiumNpc::EHintRejectReason FElysiumNpc::FUN_102961a0(int32 HintNode) const
{
	FHintWords Hint;
	if (!HintWords(HintNode, Hint))
	{
		return EHintRejectReason::NoHint;
	}
	if (IsHintDebugNpc())
	{
		FElysiumAiDebugHintProbe& Probe = World->AiDebugHintProbe();
		Probe.Reset();
		Probe.HintIndex = Hint.HintIndex;
		Probe.Validator = TEXT("0x102961a0");
	}
	FElysiumEntity* CoverObject = (World != nullptr)
		? const_cast<FElysiumEntityWorld*>(World)->Resolve(ScheduleHost.HintCoverObject) : nullptr;
	// Retail runs the `target_name` and `m_iDisabled` gates BEFORE it resolves the cover object, so
	// a disabled hint answers `Disabled` (no string, the top arm) and a name mismatch answers its
	// own reason even with no cover object standing.
	if (!Hint.bValid || Hint.Disabled != 0)
	{
		return EHintRejectReason::Disabled;
	}
	if (!Hint.TargetName.IsEmpty() && !Hint.TargetName.Equals(TargetName, ESearchCase::IgnoreCase))
	{
		// `1029620a`..`10296232`: the hint's own `m_strTargetName` is the operand.
		if (IsHintDebugNpc())
		{
			HintDebugNote(Hint, TEXT("0x102961a0"),
				FString::Printf(TEXT("Target name mismatch (%s)"), *Hint.TargetName));
		}
		return EHintRejectReason::TargetNameMismatch;
	}
	if (CoverObject == nullptr)
	{
		// `102962a4`: pushed as the string itself, no format.
		if (IsHintDebugNpc())
		{
			HintDebugNote(Hint, TEXT("0x102961a0"), TEXT("No cover object"));
		}
		return EHintRejectReason::NoCoverObject;        // "No cover object"
	}
	const bool bIsCurrentHint = HintNode == BaseScheduleHost.HintNode;
	const EHintRejectReason Reason = CoverHintRejectReason(Hint, CoverObject->Origin,
		bIsCurrentHint);
	if (Reason != EHintRejectReason::None)
	{
		return Reason;
	}
	// `10296459`: the current hint accepts before the ray. (Retail's pass arms return without the
	// `0x102d0b20` clear in this body; the text a previous failure wrote stands.)
	if (bIsCurrentHint)
	{
		return EHintRejectReason::None;
	}
	// `10296465`..`102965e5`, the inline ray: FROM my origin raised by `m_Collision (+0x270)->
	// OBBMaxs().z` TO the hint's position for me (`0x102d1180` via `0x10012387`), `UTIL_TraceLine`
	// under mask `0x46804099` with `CTraceFilterSimple(this, COLLISION_GROUP_NONE)` (`0x1000bd7f`).
	// `FCOMP fraction, 1.0; TEST AH,5 / JNP` fails only an ORDERED `fraction < 1.0`; then `allsolid`
	// (`+0x36`) or `startsolid` (`+0x37`) fails. Otherwise it passes.
	FVector EndCm = FVector::ZeroVector;
	if (!HintPositionCm(HintNode, EndCm))
	{
		// No live hint behind the index, so there is no point to ray to (retail holds the hint
		// itself and always has one). Retail's failing arm, "Failed LOS check (%s)".
		return EHintRejectReason::FailedLos;
	}
	FVector MinsUnits = FVector::ZeroVector;
	FVector MaxsUnits = FVector::ZeroVector;
	RetailCollisionExtents(*this, MinsUnits, MaxsUnits);
	IElysiumEmbodiment* const Embodiment = World != nullptr ? World->Embodiment() : nullptr;
	if (Embodiment == nullptr)
	{
		// No embodiment: nothing to trace against — the PASS arm, as `HintLosCheck` answers it.
		return EHintRejectReason::None;
	}
	FElysiumRetailTrace Trace;
	Trace.StartCm = Origin + FVector(0.0, 0.0, MaxsUnits.Z * ElysiumMove::U);
	Trace.EndCm = EndCm;
	Trace.RetailMask = GCoverTwinLosMask;
	// `CTraceFilterSimple`'s pass entity is this NPC. The mask carries no MONSTER bit, so the seam
	// lists no character and only the world half of the answer is read.
	Trace.Ignore.Add(Handle);
	FElysiumRetailTraceResult Result;
	if (!Embodiment->TraceRetail(Trace, Result))
	{
		return EHintRejectReason::None;   // no collision world: the clear defaults, the PASS arm
	}
	if (!(Result.Fraction < GDatClearFraction) && !Result.bAllSolid && !Result.bStartSolid)
	{
		return EHintRejectReason::None;
	}
	// `1029661c`..`10296647`: the operand is `tr.m_pEnt->GetDebugName()` (`0x1000b5cd`: the
	// targetname, else the classname), or `"**UNKNOWN**"` (`0x105477a4`) for a null `m_pEnt`. Retail's
	// world hit is the world ENTITY (`"worldspawn"` by classname); the port's static world answers an
	// unset `HitEntity`, read as that entity when the ray was actually stopped.
	if (IsHintDebugNpc())
	{
		FString Blocker = TEXT("**UNKNOWN**");
		if (const FElysiumEntity* Hit = Result.HitEntity.IsSet() ? World->Resolve(Result.HitEntity) : nullptr)
		{
			Blocker = !Hit->TargetName.IsEmpty() ? Hit->TargetName
				: (Hit->Def != nullptr ? Hit->Def->Classname : FString(TEXT("**UNKNOWN**")));
		}
		else if (!Result.HitEntity.IsSet() && Result.Fraction < GDatClearFraction)
		{
			Blocker = TEXT("worldspawn");
		}
		HintDebugNote(Hint, TEXT("0x102961a0"), FString::Printf(TEXT("Failed LOS check (%s)"), *Blocker));
	}
	return EHintRejectReason::FailedLos;                // "Failed LOS check (%s)"
}

// =================================================================================================
// The face-anim turn ladder.
// =================================================================================================

FElysiumNpc::FFaceAnimPick FElysiumNpc::FaceAnimLadder(float YawDelta,
	TFunctionRef<bool(int32)> HasSequence)
{
	// `0x10297a20`, rung by rung. Each rung is GATED on the body actually authoring the activity
	// (`SelectWeightedSequence(act) != -1`); a body that does not falls through to the next rung,
	// and the tail is `ACT_IDLE` with `m_eFaceAnim = 0`.
	if ((YawDelta < GDatFaceAnimYawLow || YawDelta > GDatFaceAnimYawHigh)
		&& HasSequence(GFaceAnimAct180))
	{
		return FFaceAnimPick{ GFaceAnimAct180, 8, true };
	}
	if (YawDelta <= GDatFaceAnimYawMid && HasSequence(GFaceAnimAct90))
	{
		// A `<=` against the NEGATIVE band edge -40, which the ladder's descent requires.
		return FFaceAnimPick{ GFaceAnimAct90, 6, true };
	}
	if (YawDelta >= GDatFaceAnimTurnYaw && HasSequence(GFaceAnimAct45))
	{
		return FFaceAnimPick{ GFaceAnimAct45, 3, true };
	}
	if (HasSequence(GFaceAnimActSmall))
	{
		return FFaceAnimPick{ GFaceAnimActSmall, 1, false };
	}
	return FFaceAnimPick{ GBaseHelpersActIdle, 0, false };
}

void FElysiumNpc::FUN_10297a20()
{
	// The body around the ladder: the motor's yaw delta in, the pick's activity made ideal
	// (`thunk_FUN_10272650`, family Facing's `SetIdealActivityNumber`), and TWO words written —
	// `m_eFaceAnim` (`+0x63e4`) and `m_flFaceYawDiff` (`+0x63e8`). The three upper rungs write a
	// DRAWN duration (`__ftol` of a random, masked to 16 bits and scaled by `_DAT_1044ffdc`); the
	// two lower ones copy the yaw delta itself.
	const float YawDelta = MotorDeltaIdealYaw();   // `CAI_Motor::DeltaIdealYaw` 0x102e1f90
	const FFaceAnimPick Pick = FaceAnimLadder(YawDelta,
		[this](int32 Activity) { return SelectWeightedSequenceForActivity(Activity) != -1; });
	FaceAnim = Pick.FaceAnim;
	if (Pick.bRandomDuration)
	{
		// The 16-bit draw scaled by `_DAT_1044ffdc` = 360 / 65536.
		const int32 Draw =
			ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(0, 0xffff) & 0xffff;
		FaceYawDiff = static_cast<float>(Draw) * GDatFaceAnimRandomScale;
	}
	else
	{
		FaceYawDiff = YawDelta;
	}
	SetIdealActivityNumber(Pick.Activity);
}

// =================================================================================================
// The victim-side reaction slots, 21 / 22 / 23 / 27 / 317.
// =================================================================================================
//
// Four bodies, one shape. `thunk_FUN_102bf5d0` is the detected-attack notice family Squad ported as
// `AlertNearbyAlly`; `thunk_FUN_10269a20` is `SetCondition`, NOT a clear (29c's walk read it as one
// and it is corrected here); the bare `(*DAT_10924a6c)->vtable+4` call beside every `SetCondition`
// is the AI-debug ConVar the condition setter consults and NOT a game event (the same walk read it
// as one). Condition 10 is `COND_BEING_ATTACKED` (`0x0a`) and condition 12 `COND_SHOULD_DODGE`
// (`0x0c`) in the registry `ElysiumNpcConditions.h` carries.

// slot 21 0x1029f800 `void vfunc21(CBaseEntity*)`
void FElysiumNpc::Slot21(FElysiumEntity* Attacker)
{
	// `CNPC_VMingXiaoTentacle` overrides slots 21, 22 and 23 again under the Troika line
	// (`FElysiumNpcMingXiaoTentacle`, story 5 step 3) and forwards each to its head instead.
	AlertNearbyAlly(Attacker);
	// `m_iHitBuildupCount` (`+0x6064`) — the shape map binds it to the combat character, where the
	// port already raises it on a landed hit (`FElysiumCombatCharacter::RaiseHitBuildup`).
	RaiseHitBuildup();
	Cognition.Conditions.Set(EElysiumNpcCond::BeingAttacked);
}

// slot 22 0x1029f850 `void vfunc22(CBaseEntity*)`
void FElysiumNpc::Slot22(FElysiumEntity* Attacker)
{
	// Slot 21 without the hit-buildup increment. `CBasePlayer::Replenish` (`0x10168320`) dispatches
	// this on a feed target with the player as the argument.
	AlertNearbyAlly(Attacker);
	Cognition.Conditions.Set(EElysiumNpcCond::BeingAttacked);
}

// slot 27 0x1029f8f0 `void vfunc27(CBaseEntity*)`
void FElysiumNpc::Slot27(FElysiumEntity* Attacker)
{
	// Slot 22 plus a fourth step: slot 600 (vtable `+0x960`) on MYSELF with the attacker as the
	// argument — the melee-coordinator slot request. Order matters: the notice and the condition
	// land first, the request last.
	AlertNearbyAlly(Attacker);
	Cognition.Conditions.Set(EElysiumNpcCond::BeingAttacked);
	Slot600(Attacker);
}

// slot 317 0x1029fb70 `bool vfunc317(CBaseEntity*)`
bool FElysiumNpc::Slot317(FElysiumEntity*)
{
	// The argument is never read — `signatures.md` calls the slot unsettled for exactly that
	// reason. The body:
	//   SetCondition(COND_BEING_ATTACKED);
	//   if (ConditionInterruptsCurrentSchedule(COND_SHOULD_DODGE)) {
	//       SetCondition(COND_SHOULD_DODGE); return true;
	//   }
	//   return false;
	//
	// So the dodge bit is raised ONLY when the running program's interrupt mask lists it, and the
	// return says whether it was. `ElysiumSchedule::MaskHasCondition` is the port's `0x10269c70`.
	Cognition.Conditions.Set(EElysiumNpcCond::BeingAttacked);
	if (!ElysiumSchedule::MaskHasCondition(Schedule, *this, EElysiumNpcCond::ShouldDodge))
	{
		return false;
	}
	Cognition.Conditions.Set(EElysiumNpcCond::ShouldDodge);
	return true;
}

// =================================================================================================
// The remaining slots.
// =================================================================================================

// slot 588 0x10293e50 `void vfunc588()`
void FElysiumNpc::Slot588()
{
	// `CNPC_VTzimisceRunner` overrides this slot (`FElysiumNpcTzimisceRunner`, `0x103c3fd0`): this
	// body with the `IsActivityFinished()` gate removed, so a runner restarts mid-clip.
	// `if (IsActivityFinished()) RestartIdealActivity(ACT_DISPOSITION);` — slot 251 (`+0x3ec`) and
	// `0x10289ee0`. Dispatched three times from `CAI_BaseNPCTroika::RunTask` (`0x102aacf0`).
	//
	// NOT `CAI_BaseHumanoid`'s slot 588: that is `0x1025ea00`, `CAI_BaseActor::ValidHeadTarget`, a
	// body of another table whose class has no instance (dead; census only).
	if (IsActivityFinished())
	{
		RestartIdealActivityId(GBaseHelpersActDisposition);
	}
}

// =================================================================================================
// The seams.
// =================================================================================================

bool FElysiumNpc::ActiveWeaponMaxRangeUnits(float& OutRangeUnits) const
{
	// `GetActiveWeapon()->+0x8c0`, `m_fMaxRange1`, SOURCE units: the class constructor's word
	// (1024 for every firearm, `CWeaponRanged 0x10238070`; 50 for `CWeaponMelee 0x103e9ac0`) or
	// `Weapon_Equip`'s 1e9 (0018 story 8, findings R3). False only with no active weapon.
	ElysiumWeapons::FRangeWords Words;
	const FElysiumEntity* HeldWeapon = ActiveWeaponEntity();
	const bool bAnswered = HeldWeapon != nullptr && ElysiumWeapons::ItemRangeWords(*HeldWeapon, Words);
	OutRangeUnits = bAnswered ? Words.MaxRange1 : 0.f;
	return bAnswered;
}

int32 FElysiumNpc::PatrolNodeInterestRecord(int32 PatrolNode) const
{
	// The patrol node's record, which is the hint the node holds (`node+0xa0`):
	//
	//     id = path->+0x14[path->+0x10];              // the caller's `PatrolCurrentNode`
	//     if (id == -1) return 0;
	//     if (id < 0 || *network <= id) { ++DAT_106c994c; return 0; }
	//     node = network[1][id];
	//     return node ? node->+0xa0 : 0;
	//
	// The node's hint is `FElysiumPlaceSet::AttachedHint`, answered as its entity index; retail's
	// null is `INDEX_NONE` here (`seam-list.md`). A hint that has since died is no record.
	if (PatrolNode == INDEX_NONE)                                              // CMP -1
	{
		return INDEX_NONE;
	}
	const FElysiumPlaceSet* Places = World != nullptr ? &World->Places() : nullptr;
	if (Places == nullptr || !Places->IsValidNode(PatrolNode))                 // the network bounds test
	{
		++PatrolNodeMissCounter();                                             // DAT_106c994c++
		return INDEX_NONE;
	}
	const FElysiumEntityHandle Hint = Places->AttachedHint(PatrolNode);       // node +0xa0
	FHintWords Words;
	return Hint.IsSet() && HintWords(Hint.Index, Words) ? Hint.Index : INDEX_NONE;
}

int32 FElysiumNpc::PatrolNodeInterestPercent(int32 Record) const
{
	// The record's `+0x46c` -- `CAI_Hint::m_iIPPercent`, key `ip_percent` (`FHintWords::IpPercent`).
	// A record that names no live hint answers 0, the chance that never fires.
	FHintWords Words;
	return HintWords(Record, Words) ? Words.IpPercent : 0;
}

bool FElysiumNpc::HintLosCheck(int32 HintNode, const FElysiumEntity* LosTarget) const
{
	// `0x102968f0(hint, target)` (`__thiscall` on the NPC, `RET 8`), `docs/vtmb/npc-ai/shape.md`
	// § "The claim primitives, the hint LOS check and the idle gate". The ray runs FROM the hint TO
	// the target's eye — not from the NPC:
	//
	//     if (!hint || !target) return false;
	//     start = 0x102d1180(hint, this);             // the hint's position for this NPC
	//     start.z += m_Collision (+0x270)->m_vecMaxs.z;
	//     end   = target->EyePosition();              // slot 193, +0x304
	//     UTIL_TraceLine(start, end, 0x46804099, CTraceFilterHintLOS(group 0), &tr);
	//     return tr.fraction >= 1.0 && !tr.allsolid && !tr.startsolid;
	//
	// It WRITES NOTHING: `m_iFailedCoverLOSChecks` (`+0x6404`) is only ever zeroed in the corpus.
	// The rest of the body is debug (`r_visualizetraces`, `debug_hint_los`), not reproduced: under
	// the `debug_hint_los` ConVar (`DAT_10924cdc`), and only while `ai_debug_npc` is unset or names
	// this NPC, a blocked trace draws the blocker's bbox overlay and a line to the hit point with a
	// 4-unit cross (`0x10143d80`). UNPORTED (a drawing, no state); the gate would be `IsHintDebugNpc`
	// or an unset `AiDebugNpc`.
	// The mask the body pushes: OPAQUE and MOVEABLE among its bits, no MONSTER, no MONSTERCLIP
	// (`ElysiumRetailMask::Recipe`: the SIGHT channel, movers met, no characters, no entity props).
	constexpr int32 HintLosMask = 0x46804099;
	if (HintNode == INDEX_NONE || LosTarget == nullptr)
	{
		return false;
	}
	// `0x102d1180`: a standalone hint's `GetAbsOrigin`, a node hint's `CAI_Node::GetPosition` at the
	// NPC's pathing hull (`+0x156c`) — `HintPositionCm` is that body. False only for an index that
	// names no live hint, which is retail's null-hint arm (retail holds the `CAI_Hint*`).
	FVector StartCm = FVector::ZeroVector;
	if (!HintPositionCm(HintNode, StartCm))
	{
		return false;
	}
	FVector MinsUnits = FVector::ZeroVector;
	FVector MaxsUnits = FVector::ZeroVector;
	RetailCollisionExtents(*this, MinsUnits, MaxsUnits);        // `m_Collision` slot 2, `m_vecMaxs`
	StartCm.Z += MaxsUnits.Z * ElysiumMove::U;

	IElysiumEmbodiment* const Embodiment = World != nullptr ? World->Embodiment() : nullptr;
	if (Embodiment == nullptr)
	{
		// No embodiment: nothing to trace against, and the answer is the PASS arm — the posture this
		// seam held before the body landed and every kernel trace takes headless.
		return true;
	}
	FElysiumRetailTrace Trace;
	Trace.StartCm = StartCm;
	Trace.EndCm = LosTarget->EyePosition();                           // slot 193
	Trace.RetailMask = HintLosMask;
	// `CTraceFilterHintLOS` (vtable `0x1049ae1c`, collision group 0) has NO pass entity, and its
	// `ShouldHitEntity` refuses every combat character (`entity+0x9c != 0`) — so neither this NPC
	// nor the target can block, and the character list is IGNORED: only the world half of the
	// answer is read. The mask carries no MONSTER bit, so the seam lists none anyway. Its other two
	// arms — the entity's slot 91 `ShouldCollide(0, mask)` and the game rules' group-0 pair over
	// the entity's `m_CollisionGroup` — have no per-entity source on the world half and are not
	// asked (every mover the recipe admits blocks).
	FElysiumRetailTraceResult Result;
	if (!Embodiment->TraceRetail(Trace, Result))
	{
		return true;   // no collision world: `Result` keeps its clear defaults, the PASS arm
	}
	// `trace+0x2c` against `_DAT_104454c0` = 1.0: `TEST AH,5 / JNP` at `10296b57` jumps to the fail
	// arm only on an ORDERED `fraction < 1.0`, so an unordered fraction reaches the solid tests —
	// spelled `!(f < 1)` to keep that. Then `trace+0x36` allsolid and `trace+0x37` startsolid.
	return !(Result.Fraction < GDatClearFraction) && !Result.bAllSolid && !Result.bStartSolid;
}

bool FElysiumNpc::IsHintDebugNpc() const
{
	// `DAT_10925444`, the `ai_debug_npc` handle, as every gated arm reads it (`10296275`..`1029629c`).
	// The test is the base NPC's (`FElysiumNpcBase::IsAiDebugNpc`, brief S7), which the NPC trace
	// ring keys on too; the hint validators keep this name.
	return IsAiDebugNpc();
}

void FElysiumNpc::HintDebugNote(const FHintWords& Hint, const TCHAR* Validator, const FString& Reason) const
{
	// `0x102d0ab0(hint, reason)` — `Q_strncpy(hint + 0x478, reason ? reason : "Unknown failure",
	// 0x80)` and `hint + 0x4f8 = curtime + _DAT_1044e664` — and, for an empty reason, `0x102d0b20`,
	// which empties `+0x478` and zeroes `+0x4f8`. NAMED MODERNIZATION (debug output only): the text
	// goes to the log and to the world's probe record instead of a 128-byte word on the hint, and it
	// is not truncated at 127 characters. No rule reads either word.
	if (!IsHintDebugNpc())
	{
		return;
	}
	FElysiumAiDebugHintProbe& Probe = World->AiDebugHintProbe();
	Probe.HintIndex = Hint.HintIndex;
	Probe.Validator = Validator;
	Probe.Reason = Reason;
	if (Reason.IsEmpty())
	{
		return;
	}
	UE_LOG(LogElysiumNpcEnt, Display, TEXT("%s hint #%d %s [%s]: %s"), *DebugString(), Hint.HintIndex,
		Hint.Name.IsEmpty() ? TEXT("(unnamed)") : *Hint.Name, Validator, *Reason);
}

// =================================================================================================
// `CAI_BaseNPC`'s value helpers, re-homed here when 0019 story 6 deleted the Debug family whose files
// held them. Declared in `ElysiumNpcKernelBaseHelpersBase.inl` (inside `class FElysiumNpcBase`).
// Each one answers a rule body a value; none of them prints. Bodies moved verbatim.
// =================================================================================================

bool FElysiumNpcBase::SequenceDescriptor(int32 Sequence, FString& OutLabel,
	FString& OutActivityName) const
{
	// SEAM for `CBaseAnimating::GetSeqDesc(m_nSequence)` (`0x1000b4f6`) and the two string offsets
	// off the returned `mstudioseqdesc_t` (`+0x00` the label, `+0x04` the activity name, both
	// relative to the descriptor itself). No studio header stands here — family Anim records the
	// same refusal — so this answers FALSE and both strings stay empty, which is retail's
	// `"(INVALID)"` arm (the dead debug formatter, 0019/6).
	(void)Sequence;
	OutLabel.Reset();
	OutActivityName.Reset();
	return false;
}

bool FElysiumNpcBase::NavigatorNearestNodePositionUnits(FVector& OutUnits) const
{
	// The `0x2000` arm's three-step:
	//
	//     m_pNavigator->+0x08 = m_pNavigator->+0x04->+0x156c;   // the NPC's own +0x156c, stamped in
	//     m_pNavigator->+0x0c = gpGlobals->+0x04;               // the frame counter
	//     idx = CAI_Network::NearestNodeToNPC(m_pNavigator->+0x2c, this, GetOrigin());  0x102f3c10
	//     if (idx != -1) node = network->+0x04[idx];
	//     CAI_Node::GetPosition(node, out, m_eHull);            // 0x102fb0d0
	//
	// The two scratch writes land on navigator words the port's mover does not keep (the pathing
	// hull is read straight off the NPC wherever retail reads `nav+8`). The final position is the
	// STANDING hull's, `m_eHull`, not the pathing one the search measured with.
	const int32 Node = NavNearestNodeToNpc(Origin);                            // slot 220 GetOrigin
	FVector NodeCm = FVector::ZeroVector;
	if (Node == INDEX_NONE || World == nullptr || !World->Places().GetPositionCm(Node, HullKind, NodeCm))
	{
		return false;
	}
	OutUnits = NodeCm / ElysiumMove::U;
	return true;
}

namespace
{
	// `0x102f3c10`'s box: `local_28/24/20` = 800, 800, 200 units, or `_DAT_1046bacc` (2048) on X/Y
	// and 2048.0 on Z when the capabilities carry `4`.
	constexpr float GNearestNpcBoxXYUnits = 800.0f;
	constexpr float GNearestNpcBoxZUnits = 200.0f;
	constexpr float GNearestNpcFlyBoxUnits = ElysiumNpcTunables::TwoThousandFortyEight;   // _DAT_1046bacc
	constexpr int32 GNearestNpcCapFly = 0x4;       // bits_CAP_MOVE_FLY: type-3 (air) nodes
	constexpr int32 GNearestNpcCapGround = 0x1;    // bits_CAP_MOVE_GROUND: type-2 nodes
	constexpr int32 GNearestNpcNodeGround = 2;
	constexpr int32 GNearestNpcNodeAir = 3;
	constexpr int32 GNearestNpcNodeClimb = 4;
	// `ListNodesInBox`'s cap at both call sites (`PUSH 10`).
	constexpr int32 GNearestNodeListCount = 10;
	// `0x102f1900`: a climb node is probed only with one of the info bits `0x1d`.
	constexpr int32 GCanFitClimbBits = 0x1d;
	// The mask `0x102f3c10` hands `CanFitAtNode` (`PUSH 0x2400b`).
	constexpr int32 GCanFitMask = 0x2400b;
	// `0x102f1a20`'s trace end: the point raised by `_DAT_1044e658` (the double 0.01).
	constexpr double GCanFitRiseUnits = 0.01;
}

int32 FElysiumNpcBase::NavNearestNodeToNpc(const FVector& PositionCm) const
{
	const FElysiumPlaceSet* Places = World != nullptr ? &World->Places() : nullptr;
	if (Places == nullptr || Places->NumNodes() == 0)                          // 0x102f3c3a *this == 0
	{
		return INDEX_NONE;
	}
	// The cache `0x102f4520` (20 recent answers, keyed by point and hull, reused inside a short
	// window) and its write-back `0x102f45f0` are engine machinery -- a lookup shortcut in front of
	// this body -- and are not ported: the search runs every call.
	FElysiumNpcBase* Self = const_cast<FElysiumNpcBase*>(this);
	const int32 Caps = Self->CapabilitiesGet();                                // slot 513, local_48
	const int32 Hull = RetailPathingHull();                                    // +0x156c, local_44
	const FVector P = PositionCm / ElysiumMove::U;
	const FVector Half = (Caps & GNearestNpcCapFly) != 0
		? FVector(GNearestNpcFlyBoxUnits, GNearestNpcFlyBoxUnits, GNearestNpcFlyBoxUnits)
		: FVector(GNearestNpcBoxXYUnits, GNearestNpcBoxXYUnits, GNearestNpcBoxZUnits);
	// `CNodeNPCFilter::vfunc0` (`0x102f40f0`): the node type against the capabilities, then slot 527.
	auto IsValid = [Places, Caps, Self](int32 Node) -> bool
	{
		const int32 Type = Places->Row(Node).Type;
		if (Type == GNearestNpcNodeAir && (Caps & GNearestNpcCapFly) == 0)
		{
			return false;
		}
		if (Type == GNearestNpcNodeGround && (Caps & GNearestNpcCapGround) == 0)
		{
			return false;
		}
		return !Self->IsUnusableNode(const_cast<FElysiumPlaceRow*>(&Places->Row(Node)));   // +0x83c
	};
	// `CNodeNPCFilter::vfunc1` (`0x102f4140`): the squared distance to the node at the pathing hull.
	auto DistanceSqr = [Places, Hull, &P](int32 Node) -> float
	{
		FVector At = Places->Row(Node).OriginCm;
		Places->GetPositionCm(Node, Hull, At);
		return static_cast<float>(FVector::DistSquared(At / ElysiumMove::U, P));
	};
	const TArray<int32> Order = Places->ListNodesInBox(GNearestNodeListCount, P - Half, P + Half,
		IsValid, DistanceSqr);                                                 // 0x102f32f0
	const FVector ViewOffsetCm = EyePosition() - Origin;                       // npc +0x184..+0x18c
	int32 Fallback = INDEX_NONE;                                               // local_10
	for (const int32 Node : Order)
	{
		if (!NavCanFitAtNode(Node, Hull, GCanFitMask) || IsUnusableNodeIndex(Node))   // 0x102f1900, 0x1027db30
		{
			continue;
		}
		FVector NodeCm = Places->Row(Node).OriginCm;
		Places->GetPositionCm(Node, Hull, NodeCm);                             // param_1[0x55b]
		const ENavNodeTrace Trace = NavNearestNodeTrace(PositionCm, NodeCm + ViewOffsetCm, Handle);   // 0x102f3900
		if (Trace == ENavNodeTrace::Clear)
		{
			return Node;
		}
		if (Trace == ENavNodeTrace::ClearPastFlagged && Fallback == INDEX_NONE)
		{
			Fallback = Node;
		}
	}
	return Fallback;
}

bool FElysiumNpcBase::NavCanFitAtNode(int32 Node, int32 Hull, int32 Mask) const
{
	// `0x102f1900`: the node through the network (an id outside it bumps `DAT_106c994c` and reads a
	// NULL node -- the one caller hands only listed ids), `GetPosition(node, nav+8)`, then the two
	// geometry queries below.
	FVector NodeCm = FVector::ZeroVector;
	if (World == nullptr || !World->Places().GetPositionCm(Node, Hull, NodeCm))
	{
		return false;
	}
	const FElysiumPlaceRow& Row = World->Places().Row(Node);
	const bool bProbe = Row.Type == GNearestNpcNodeGround
		|| (Row.Type == GNearestNpcNodeClimb && (Row.Flags & GCanFitClimbBits) != 0);
	const FVector PointUnits = NodeCm / ElysiumMove::U;
	if (bProbe)
	{
		// `0x102e7270(m_pMoveProbe, pos, mask, 0, 0, 0, 0)` -- `CheckStandPosition` (R1 §4), with NO
		// `m_bForceNPCCheck` bracket (that is `CanStandAt`'s): the foot box of `m_Collision`'s OBB
		// dropped slot 523 under the node, standable iff it hit and slot 166 agrees. A base-only NPC
		// has no Troika move probe here and refuses.
		const FElysiumNpc* Troika = AsNpc();
		if (Troika == nullptr || !Troika->MoveProbeCheckStandPosition(PointUnits, Mask))
		{
			return false;
		}
	}
	// `0x102f1a20` (R1 §4): a trace from the point to the point raised by 0.01 (`0x1044e658`) on the
	// FULL row of the navigator's hull (`nav+8`: `0x102d6100` / `0x102d6120`; the small pair is
	// `+0x20` / `+0x2c`), `CTraceFilterSimple(npc, 0)`, the caller's mask; fits iff the trace does
	// NOT start solid (`[ESP+0x8b]`, `102f1c44`) -- the fraction is ignored. Family Motor's
	// `KernelHullTrace`; a world with no collision answers clear, which fits.
	FVector Mins = FVector::ZeroVector;
	FVector Maxs = FVector::ZeroVector;
	RetailHullExtents(Hull, EElysiumHullExtents::Full, Mins, Maxs);
	FKernelHullTrace Trace;
	KernelHullTrace(PointUnits, PointUnits + FVector(0.0, 0.0, GCanFitRiseUnits), Mins, Maxs, Mask, Trace);
	return !Trace.bStartSolid;
}

FElysiumNpcBase::ENavNodeTrace FElysiumNpcBase::NavNearestNodeTrace(const FVector& StartCm,
	const FVector& EndCm, const FElysiumEntityHandle& Ignore) const
{
	IElysiumEmbodiment* Embodiment = World != nullptr ? World->Embodiment() : nullptr;
	if (Embodiment == nullptr)
	{
		return ENavNodeTrace::Clear;                                           // no collision world
	}
	float Fraction = 1.0f;
	bool bStartSolid = false;
	Embodiment->TraceCameraHull(StartCm, EndCm, FVector::ZeroVector, Ignore, Fraction, bStartSolid);
	// `0x102f3a5b`: clear at `fraction == 1.0` exactly (`_DAT_10449280`); startsolid is not read.
	return Fraction == 1.0f ? ENavNodeTrace::Clear : ENavNodeTrace::Blocked;
}
