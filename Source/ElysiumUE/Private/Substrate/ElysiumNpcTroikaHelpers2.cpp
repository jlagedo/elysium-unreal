#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcTroikaHelpers2Shared.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcMotor2Shared.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumSchedule.h"

// Story 29c-1, family **TroikaHelpers**, part two — the twenty-eight bodies that fill no
// Troika-line slot: the hint lean and the tactical hint search, the alert grade, the detected-attack
// notice, the move stop, the dialogue-release path, the target lead, the ignore-collision triple,
// the jump stamp, the expresser factory, the two unnamed free functions, and the `CAI_Motor`,
// `CAI_Navigator` and `CAI_StandoffBehavior` helpers. The seventeen slot bodies are
// `Substrate/ElysiumNpcTroikaHelpers.cpp`.
//
// Every one of the last three groups sits on a class this substrate does not stand. Family
// **Motor**'s standing fact holds throughout: `IElysiumNpcMotor` takes a destination, a yaw and a
// speed and keeps no route, no facing queue and no path, so each body is ported verbatim and then
// asks a seam that answers NOTHING and names the retail call it stands for. Nothing here invents a
// motor state to make a body "work".

namespace
{
	// `0x7f7fffff` — `FLT_MAX`, the "never expire" sentinel `0x102c4380` and `0x102c43f0` pin
	// `m_flIgnoreCollisionTimer` (`+0x6458`) to.
	const double TroikaIgnoreCollisionNever = static_cast<double>(TNumericLimits<float>::Max());

	// The lean hint's type, the one literal `ApplyHintLeanOffset` and `FindTacticalHintNode` both
	// switch on. Family **Schedule** reads the same `0x27d8` as `GScheduleHintTypeLean`.
	constexpr int32 TroikaHintTypeLean = 0x27d8;

	// `FindTacticalHintNode`'s hint type and `ApplyHintLeanOffset`'s lean constants.
	constexpr int32 TroikaTacticalHintType = 8;
	// `_DAT_1049949c` — the yaw offset the lean adds to or subtracts from the hint's own facing,
	// 45 degrees (`102b6176 FSUB` / `102b61a5 FADD float ptr`).
	constexpr float TroikaHintLeanYawOffset = ElysiumNpcTunables::FortyFive;
	// `_DAT_1049ae8c` (the `bStanding == false` scale, 1.4, `102b621a`) and `_DAT_1049ae90` (the
	// `true` one, 1.2, `102b61e5`).
	constexpr float TroikaHintLeanScaleCrouch = ElysiumNpcTunables::CoverLeanClearScale;
	constexpr float TroikaHintLeanScaleStand = ElysiumNpcTunables::CoverLeanSetScale;

	// `_DAT_104454c4` / `_DAT_104454c0` — the shared `0.0` and `1.0` cells.
	constexpr float TroikaSharedZero = ElysiumNpcTunables::Zero;

	// `FUN_102aa9e0`'s assert code, raised through the entity's own vtable `+0x700` (slot 448,
	// `TaskFail`), and the source line it stamps.
	constexpr int32 TroikaTaskArgumentAssertReason = 0x1d;

	// The 2-D cross product `FindTacticalHintNode` decides the lean side with:
	// `(cover - hint).x * forward.y - forward.x * (cover - hint).y`. At or below zero the NPC leans
	// LEFT; strictly above, it does not.
	float TroikaLeanCross2D(const FVector& DeltaUnits, const FVector& ForwardUnits)
	{
		return DeltaUnits.X * ForwardUnits.Y - ForwardUnits.X * DeltaUnits.Y;
	}
}

// -------------------------------------------------------------------------------------------------
// The hint seams this family adds beside family Hints'.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::HintStandPosition(int32 HintNode, FVector& InOutPointUnits) const
{
	// `thunk_FUN_102d1180(hint, this, out)` — `CAI_Hint::GetPosition`, whose one port body is
	// `HintPositionCm` (the hint's origin, or its network node at this NPC's pathing hull). This is
	// that answer in this family's SOURCE units (`cm / U`, the frame `ApplyHintLeanOffset`'s callers
	// scale back by `U`). False, and the point left as the caller had it, only for an index that
	// names no live hint.
	FVector PointCm = FVector::ZeroVector;
	if (!HintPositionCm(HintNode, PointCm))
	{
		return false;
	}
	InOutPointUnits = PointCm / ElysiumMove::U;
	return true;
}

bool FElysiumNpc::ClaimHintNode(int32 HintNode)
{
	// `thunk_FUN_102d1350(hint, this)` — the claim. **SEAM**: false, which is the arm that DROPS
	// the node again, so a search that "found" something still ends with no hint.
	(void)HintNode;
	return false;
}

float FElysiumNpc::LeanScaleRecordField() const
{
	// Slot 214 (vtable `+0x358`) then the float at `+0x04`. The slot is unidentified in the census.
	// **SEAM**, `0.0`.
	return 0.0f;
}

float FElysiumNpc::IdealHintSearchRangeUnits() const
{
	// Slot 550 (vtable `+0x898`) — the ideal range `FindTacticalHintNode` searches at. **SEAM**,
	// `0.0`.
	return 0.0f;
}

bool FElysiumNpc::TargetLeadQuery(const FElysiumEntity& LeadEntity, FVector& OutPointUnits,
	FVector& OutVelocityUnits) const
{
	// `thunk_FUN_102e0290(m_pPathfinder, target, out, outVelocity)`, reached through vtable `+0x874`
	// (slot 541). **SEAM**: family **Motor**'s standing fact — no pathfinder here. False is retail's
	// own "the query failed" arm, which lands on the target's plain origin.
	(void)LeadEntity;
	(void)OutPointUnits;
	(void)OutVelocityUnits;
	return false;
}

// -------------------------------------------------------------------------------------------------
// `0x102b6120` — the cover-lean position offset.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::ApplyHintLeanOffset(FVector& InOutPointUnits, bool bStanding) const
{
	// `0x102b6120`, arm by arm:
	//     if (m_pHintNode == NULL) { *out = vec3_origin; return false; }        // +0x5ddc
	//     HintStandPosition(m_pHintNode, this, out);                            // 0x102d1180
	//     if (m_pHintNode->m_nHintType == 0x27d8) {
	//         yaw = HintYaw(m_pHintNode);                                       // 0x102d12e0
	//         fwd = AngleVectors(m_bLeaningLeft ? yaw - LEAN : yaw + LEAN);     // +0x63fd
	//         scale = slot214()->[+0x04] * (bStanding ? _DAT_1049ae90 : _DAT_1049ae8c);
	//         *out += fwd * scale;
	//     }
	//     return true;
	//
	// **NOT leaning left ADDS the offset and leaning left SUBTRACTS it** — retail tests
	// `m_bLeaningLeft == 0` for the `+` arm, which reads backwards and is reproduced as written.
	// SOURCE units throughout.
	if (BaseScheduleHost.HintNode == INDEX_NONE)
	{
		InOutPointUnits = FVector::ZeroVector;
		return false;
	}
	HintStandPosition(BaseScheduleHost.HintNode, InOutPointUnits);

	int32 HintType = INDEX_NONE;
	if (NavHintNodeType(BaseScheduleHost.HintNode, HintType) && HintType == TroikaHintTypeLean)
	{
		float Yaw = 0.f;
		if (HintYaw(BaseScheduleHost.HintNode, Yaw))
		{
			const float LeanYaw =
				bLeaningLeft ? Yaw - TroikaHintLeanYawOffset : Yaw + TroikaHintLeanYawOffset;
			// The yaw is RETAIL-frame (`HintYaw`) and the point is this family's `cm / U` frame, whose Y
			// is Source's reflected: the forward is reflected with it.
			const FVector LeanForward = FRotator(0.0, -static_cast<double>(LeanYaw), 0.0).Vector();
			const float Scale = LeanScaleRecordField()
				* (bStanding ? TroikaHintLeanScaleStand : TroikaHintLeanScaleCrouch);
			InOutPointUnits += LeanForward * Scale;
		}
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// `0x102b7110` — the tactical hint search.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::FindTacticalHintNode(uint32 SearchType)
{
	// `0x102b7110`, arm by arm:
	//     m_bForceCoverLOSCheck = 1;                                           // +0x6408
	//     m_pHintNode = FindHintOfType(this, 8, searchType, IdealRange(), NULL, NULL);
	//     m_bForceCoverLOSCheck = 0;
	//     if (m_bStayEntrenched && m_pHintNode == NULL)                        // +0x6435
	//         m_pHintNode = FindHintOfType(this, 8, searchType, IdealRange(), NULL, NULL);
	//     if (m_pHintNode) {
	//         m_iPeekOutCount = 0;                                             // +0x640c
	//         m_iFailedCoverLOSChecks = 0;                                     // +0x6404
	//         if (!ClaimHint(m_pHintNode, this)) m_pHintNode = NULL;           // 0x102d1350
	//         else if (m_pHintNode->m_nHintType == 0x27d8) {
	//             cover = m_hHintCoverObject;                                  // +0x6448
	//             cross = (cover - hint).x * fwd.y - fwd.x * (cover - hint).y;
	//             m_bLeaningLeft = (cross <= 0.0);                             // +0x63fd
	//         }
	//     }
	//     if (m_pHintNode) {
	//         m_vecSavedSleepExtents = slot16();                               // +0x65d0
	//         SetAbsoluteAttackExtents(this, ...);
	//         return true;
	//     }
	//     return false;
	//
	// The `m_bForceCoverLOSCheck` bracket is around the FIRST search only; the entrenched retry runs
	// with it clear, which is retail's and is reproduced.
	//
	// The 29c walk reads the retry gate as "retries once if dialog-flagged"; `+0x6435` is
	// `m_bStayEntrenched` (29b's name) and that is what this reads.
	ScheduleHost.bForceCoverLosCheck = true;
	BaseScheduleHost.HintNode = FindHintNear(TroikaTacticalHintType,
		static_cast<uint8>(SearchType & 0xffu), IdealHintSearchRangeUnits());
	ScheduleHost.bForceCoverLosCheck = false;
	if (bStayEntrenched && BaseScheduleHost.HintNode == INDEX_NONE)
	{
		BaseScheduleHost.HintNode = FindHintNear(TroikaTacticalHintType,
			static_cast<uint8>(SearchType & 0xffu), IdealHintSearchRangeUnits());
	}

	if (BaseScheduleHost.HintNode != INDEX_NONE)
	{
		PeekOutCount = 0;
		ScheduleHost.FailedCoverLosChecks = 0;
		if (!ClaimHintNode(BaseScheduleHost.HintNode))
		{
			BaseScheduleHost.HintNode = INDEX_NONE;
		}
		else
		{
			int32 HintType = INDEX_NONE;
			FVector HintOriginUnits = FVector::ZeroVector;
			float Yaw = 0.f;
			const FElysiumEntity* Cover = (World != nullptr && ScheduleHost.HintCoverObject.IsSet())
				? World->Resolve(ScheduleHost.HintCoverObject)
				: nullptr;
			if (NavHintNodeType(BaseScheduleHost.HintNode, HintType) && HintType == TroikaHintTypeLean
				&& Cover != nullptr
				&& NavHintNodeOrigin(BaseScheduleHost.HintNode, HintOriginUnits)
				&& HintYaw(BaseScheduleHost.HintNode, Yaw))
			{
				// All three in the RETAIL frame: `NavHintNodeOrigin` answers Source units, `HintYaw` a
				// Source yaw, so the cover's origin is taken into the same frame before the cross.
				const FVector HintForward = FRotator(0.0, static_cast<double>(Yaw), 0.0).Vector();
				const FVector Delta = NpcKernelMotor2Shared::MotorTailSourceOf(Cover->Origin) - HintOriginUnits;
				bLeaningLeft = TroikaLeanCross2D(Delta, HintForward) <= TroikaSharedZero;
			}
		}
	}

	if (BaseScheduleHost.HintNode != INDEX_NONE)
	{
		const FVector ExtentsUnits = HintAttackExtentsUnits();
		ScheduleHost.SavedSleepExtents = ExtentsUnits;
		SetAttackExtents(ExtentsUnits * ElysiumMove::U);
		return true;
	}
	return false;
}

// -------------------------------------------------------------------------------------------------
// `0x102b8980` — the alert-level rung and its grade letter.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::AdvanceAlertLevelGrade()
{
	// `0x102b8980`, the whole body:
	//     if (m_bFullInvestigate) SetAlertLevel(3);                 // +0x6340, 0x102b5dc0
	//     switch (m_eAlertLevel) {                                  // +0x63f4, READ AFTER the write
	//         default: SetAlertLevel(1); return 0x4c;
	//         case 1:  SetAlertLevel(2); return 0x4d;
	//         case 2:
	//         case 3:  SetAlertLevel(3); return 0x51 + ((m_afMemory & 0x8000000) != 0);
	//     }
	//
	// `m_bFullInvestigate` writes the level BEFORE the switch reads it, so a full-investigate NPC
	// always lands on the 2/3 arm and answers 0x51 or 0x52. The answers are Troika schedule ids,
	// not letters (story 8 L06: `0x102b9060` tail-jumps here at `0x102b9204` and returns this value
	// as its schedule; 0x51/0x52 are SCHED_TROIKA_INVESTIGATE_SOUND / _OTHER_SOUND).
	if (FullInvestigate != 0)
	{
		AlertLevel = 3;
	}
	switch (AlertLevel)
	{
	case 1:
		AlertLevel = 2;
		return 0x4d;
	case 2:
	case 3:
		AlertLevel = 3;
		// `m_afMemory & 0x8000000` (+0x5d8c, `FElysiumNpcScheduleHost::MemoryBits`) adds one, so
		// 0x51 becomes 0x52. The bit's name is **unrecovered**.
		return 0x51 + ((BaseScheduleHost.MemoryBits & 0x8000000u) != 0 ? 1 : 0);
	default:
		break;
	}
	AlertLevel = 1;
	return 0x4c;
}

// -------------------------------------------------------------------------------------------------
// `0x102bf560` — the detected-attack notice.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::RecordDetectedAttack(const FElysiumEntity* Attacker)
{
	// `0x102bf560`, the whole body:
	//     if (m_bIgnoreDetectedAttack != 0) return;                 // +0x65f5
	//     e = Redirect(attacker);                                   // 0x102707d0
	//     m_hDetectedAttacker = e ? e->GetRefEHandle() : -1;        // +0x65c0
	//     m_flDetectedAttackTime = curtime + _DAT_10454110;         // +0x65c4
	//
	// The stamp is written on BOTH arms, including the null one: a refused notice still opens the
	// window. The two words are `FElysiumNpcMemory::DetectedAttackAttacker` /
	// `DetectedAttackTime`, which family **Squad**'s `HasDetectedAttack` is the reader of. This
	// runtime stores the stamp, not retail's expiry; the reader adds `_DAT_10454110` (5.0,
	// `ElysiumNpcCond::DetectedAttackRetentionSeconds`).
	if (bIgnoreDetectedAttack)
	{
		return;
	}
	const FElysiumEntity* Redirected = SummonerRedirect(const_cast<FElysiumEntity*>(Attacker)); // 0x102707d0
	Senses.Memory.DetectedAttackAttacker =
		Redirected != nullptr ? Redirected->Handle : FElysiumEntityHandle();
	Senses.Memory.DetectedAttackTime = World != nullptr ? World->NowSeconds() : 0.0;
}

// -------------------------------------------------------------------------------------------------
// `0x102bf770` — the move stop.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::StopScheduledMove()
{
	// `0x102bf770`, the whole body:
	//     if (m_IdealActivity == NavCurrentLinkActivity())          // +0x0ff0, 0x102ee3f0
	//         SetActivity(ResolveLinkActivity());                   // 0x1027a6c0, 0x10272650
	//     m_bShouldMove = 0;                                        // +0x1a40
	//     NavReset();                                               // 0x102ee2a0
	//     m_flDesiredMoveYaw = 0;                                   // +0x63ec
	//
	// The last three run UNCONDITIONALLY — the activity replay is the only guarded half. This is
	// the inverse of family **Motor**'s `ResumeScheduledMove` (`0x102bf7e0`), which stops the goal
	// and then SETS `m_bShouldMove`; the two are siblings and the asymmetry is retail's.
	if (IdealActivityNumber == NavCurrentLinkActivity())
	{
		LastSetActivityId = ResolveLinkActivity();
		++SetActivityIdCalls;
	}
	BaseScheduleHost.bShouldMove = false;
	++NavResets;
	ScheduleHost.DesiredMoveYaw = 0.f;
}

// -------------------------------------------------------------------------------------------------
// `0x102c0360` — the dialogue-release path.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::OnDialogRelease()
{
	// `0x102c0360`, the whole body:
	//     if (m_hDialogPartner resolves) {                          // +0x0fe8
	//         m_bCutsceneForceLOD = 0;                              // +0x1590
	//         FireOutput(m_OnDialogEnd, partner, this, 0);          // +0x5f5c, 0x100cd660
	//         SetDialogPartner(this, NULL);
	//     }
	//     if (cvar DAT_109241fc) { <the entity_debug_stats hook> }
	//
	// `docs/vtmb/game_runtime.md` names this the owning NPC's dialogue-end path, called from
	// `CDialog::Release`. `FElysiumNpcDialogue::End` fires the same output through a DIFFERENT,
	// input-driven trigger; this is the engine-side one, and the `+0x1590` clear is the half neither
	// path carried before.
	//
	// The tail is a DEBUG hook — `thunk_FUN_10245660("entity_debug_stats")` and a `+0x10` dispatch,
	// behind `DAT_109241fc`, the `dialog_facial_debug` ConVar (shipped "0", so as shipped the hook
	// never runs). It reaches no game state and is named rather than ported.
	if (!HasLiveDialogPartner())
	{
		return;
	}
	bCutsceneForceLOD = false;
	static const FName OnDialogEndOutput(TEXT("OnDialogEnd"));
	// Retail's activator is the PARTNER (`FireOutput(&m_OnDialogEnd, partner, this, 0)`). This
	// runtime carries the partner as the world's open session and holds no handle for it on the NPC,
	// so the activator is this NPC's own handle and the gap is named rather than papered over: the
	// day `m_hDialogPartner` (+0x0fe8) has a producer, the handle replaces `Handle` here.
	FireOutput(OnDialogEndOutput, Handle);
	++DialogPartnerClears;
}

// -------------------------------------------------------------------------------------------------
// `0x102c36d0` — the predictive aim point.
// -------------------------------------------------------------------------------------------------

float FElysiumNpc::ClampTargetLeadRatio(float Ratio, float MinRatio, float MaxRatio)
{
	// Retail's own shape, not a `Clamp`: `if (r <= Max) { if (r < Min) r = Min; } else r = Max;`.
	if (Ratio <= MaxRatio)
	{
		return Ratio < MinRatio ? MinRatio : Ratio;
	}
	return MaxRatio;
}

FVector FElysiumNpc::BlendTargetLeadPoint(const FVector& PredictedUnits, const FVector& OtherUnits,
	float PredictedWeight, float OtherWeight, float WeightScale)
{
	// `(Predicted * PredictedWeight + Other * OtherWeight) * WeightScale` — the same three words on
	// both arms. The scale multiplies the SUM, so a `WeightScale` of 0 answers the origin and a
	// pair of weights that do not sum to 1 is not normalised. Retail's.
	return (PredictedUnits * PredictedWeight + OtherUnits * OtherWeight) * WeightScale;
}

void FElysiumNpc::ComputeTargetLeadPoint(const FVector& AimFromUnits, const FElysiumEntity* Lead,
	float Interval, const FVector* FallbackUnits, FVector& InOutPointUnits) const
{
	// `0x102c36d0`, arm by arm:
	//     if (lead == NULL) { if (fallback) *out = *fallback; return; }
	//     if (interval > 0.0) {
	//         if (TargetLeadQuery(pathfinder, lead, out, &vel)) {              // 0x102e0290
	//             ratio = |aimFrom - *out| / interval;                        // VectorNormalize
	//             ratio = clamp(ratio, m_flTargetLeadMin, m_flTargetLeadMax);  // +0x655c / +0x6560
	//             predicted = *out + (vel.x, vel.y, 0) * ratio;    // the Z term is _DAT_104454c4
	//             if (fallback) {
	//                 *out = Blend(predicted, *fallback, PredictedWeight, CurrentWeight, Scale);
	//                 TraceHull(*fallback -> *out, my collision box, mask 0x2000b, world only);
	//                 if (trace.fraction == 1.0) return;
	//                 *out = *fallback;
	//                 return;
	//             }
	//             *out = Blend(predicted, *out, PredictedWeight, CurrentWeight, Scale);
	//             return;
	//         }
	//     }
	//     *out = lead->GetAbsOrigin();
	//
	// **The two blends differ in their second operand** — the fallback arm blends the predicted
	// point against the FALLBACK, the other against the QUERIED point. That asymmetry is retail's
	// and is what makes the trace meaningful: it sweeps from the fallback to the blend.
	//
	// The predicted point's Z term is `ratio * _DAT_104454c4`, the shared ZERO cell, so the lead is
	// PLANAR — a predicted point never rises or falls. Recovered, not a simplification.
	//
	// `TargetLeadQuery` is a seam answering false, so today every call lands on the last line.
	// SOURCE units throughout.
	if (Lead == nullptr)
	{
		if (FallbackUnits != nullptr)
		{
			InOutPointUnits = *FallbackUnits;
		}
		return;
	}

	if (Interval > TroikaSharedZero)
	{
		FVector QueriedUnits = InOutPointUnits;
		FVector VelocityUnits = FVector::ZeroVector;
		if (TargetLeadQuery(*Lead, QueriedUnits, VelocityUnits))
		{
			InOutPointUnits = QueriedUnits;
			const float Ratio = ClampTargetLeadRatio(
				static_cast<float>((AimFromUnits - QueriedUnits).Size()) / Interval,
				TargetLeadMin, TargetLeadMax);
			const FVector PredictedUnits(QueriedUnits.X + VelocityUnits.X * Ratio,
				QueriedUnits.Y + VelocityUnits.Y * Ratio,
				QueriedUnits.Z + Ratio * TroikaSharedZero);
			if (FallbackUnits != nullptr)
			{
				InOutPointUnits = BlendTargetLeadPoint(PredictedUnits, *FallbackUnits,
					TargetLeadPredictedWeight, TargetLeadCurrentWeight, TargetLeadWeightScale);
				// The hull sweep from the fallback to the blend, with THIS entity's own collision
				// box and retail's mask `0x2000b` and world-only filter. Family **Motor**'s
				// `KernelHullTrace` and `RetailCollisionExtents` are the two seams and both answer
				// nothing, so the sweep reads CLEAR — `fraction == 1.0` — and the blend stands.
				FVector MinsUnits = FVector::ZeroVector;
				FVector MaxsUnits = FVector::ZeroVector;
				RetailCollisionExtents(*this, MinsUnits, MaxsUnits);
				FKernelHullTrace Trace;
				if (KernelHullTrace(*FallbackUnits, InOutPointUnits, MinsUnits, MaxsUnits, 0x2000b,
						Trace)
					&& Trace.Fraction != NpcKernelTroikaHelpers2Shared::TroikaSharedOne)
				{
					InOutPointUnits = *FallbackUnits;
				}
				return;
			}
			InOutPointUnits = BlendTargetLeadPoint(PredictedUnits, QueriedUnits,
				TargetLeadPredictedWeight, TargetLeadCurrentWeight, TargetLeadWeightScale);
			return;
		}
	}
	InOutPointUnits = Lead->Origin / ElysiumMove::U;
}

// -------------------------------------------------------------------------------------------------
// The ignore-collision triple — `0x102c4380`, `0x102c43b0`, `0x102c43f0`.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::FUN_102c4380(const FElysiumEntity* Other)
{
	// `0x102c4380`, the whole body:
	//     CBaseAnimating::StartIgnoringCollision(this, other);
	//     m_flIgnoreCollisionTimer = 0x7f7fffff;                   // +0x6458, FLT_MAX
	//
	// Family **Motor** carries `IgnoreCollisionEntity` (`+0x055c`) and states that nothing writes
	// it; the WRITE lands here, because that is what this body does. `IgnoreCollisionUntil` is a
	// port member the shape map binds, so the sentinel is real and not recorded.
	IgnoreCollisionEntity = Other != nullptr ? Other->Handle : FElysiumEntityHandle();
	IgnoreCollisionUntil = TroikaIgnoreCollisionNever;
}

void FElysiumNpc::FUN_102c43b0(float Seconds)
{
	// `0x102c43b0`, the whole body:
	//     if (GetIgnoreCollisionEntity() == NULL) return;
	//     m_flIgnoreCollisionTimer = seconds + curtime;             // +0x6458
	//     FUN_102c43f0(this);
	//
	// The guard is what makes this a RENEWAL and not an arm: a body that is not ignoring anything
	// is left alone, sentinel and all. And the expiry runs immediately afterwards, so a zero or
	// negative duration expires on the same call.
	if (!IgnoreCollisionEntity.IsSet())
	{
		return;
	}
	IgnoreCollisionUntil = static_cast<double>(Seconds)
		+ (World != nullptr ? World->NowSeconds() : 0.0);
	FUN_102c43f0();
}

void FElysiumNpc::FUN_102c43f0()
{
	// `0x102c43f0`, the whole body:
	//     if (m_flIgnoreCollisionTimer <= curtime) {
	//         CBaseAnimating::StopIgnoringCollisionWithEntity(this);   // 0x1008bd30
	//         m_flIgnoreCollisionTimer = 0x7f7fffff;                   // never refires
	//     }
	//
	// The pin back to `FLT_MAX` is the whole point: the expiry is a ONE-SHOT, and a body that has
	// already released its partner does not release it again every frame.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	if (IgnoreCollisionUntil <= Now)
	{
		IgnoreCollisionEntity = FElysiumEntityHandle();
		IgnoreCollisionUntil = TroikaIgnoreCollisionNever;
	}
}

// -------------------------------------------------------------------------------------------------
// `0x102c4ad0` — the jump origin and target.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::SetJumpOriginAndTarget(const FElysiumEntity* Goal, float Height, float Backoff)
{
	// `0x102c4ad0`, the whole body:
	//     m_vJumpOrigin = GetAbsOrigin();                                  // +0x649c
	//     m_fJumpHeight = height;                                          // +0x64b4
	//     d = VectorNormalize(goal->GetAbsOrigin() - GetAbsOrigin());
	//     if (d < backoff) { m_vJumpTarget = goal->GetAbsOrigin(); return; }   // +0x64a8
	//     m_vJumpTarget.x = goal.x - (goal.x - my.x) * backoff;
	//     m_vJumpTarget.y = goal.y - (goal.y - my.y) * backoff;
	//     m_vJumpTarget.z = goal.z - backoff * 0.0;
	//
	// **The Z term is retail's literal `- backoff * 0.0`**: the target keeps the goal's own height
	// on BOTH arms and only the planar components are pulled back. Verbatim.
	//
	// `Backoff` is the same argument on both sides of the branch — a DISTANCE on the test and a
	// FRACTION on the offset. That reads odd and is reproduced: `CNPC_VAsianVampire::SetupJump` is
	// the consumer of the pair (`+0x649c`/`+0x64a8`) and was tuned against it.
	//
	// SOURCE units, as family Motor's `SetupJump` writes the same two words in.
	const FVector MyUnits = Origin / ElysiumMove::U;
	JumpOrigin = MyUnits;
	JumpHeight = Height;
	if (Goal == nullptr)
	{
		// Retail dereferences the goal without a null test and would fault. NAMED DIVERGENCE: a
		// crash is not a behaviour the port reproduces, and family Squad's `SetFollowerBossName`
		// took the same refusal for the same reason.
		return;
	}
	const FVector GoalUnits = Goal->Origin / ElysiumMove::U;
	const double Distance = (GoalUnits - MyUnits).Size();
	if (Distance < static_cast<double>(Backoff))
	{
		JumpTarget = GoalUnits;
		return;
	}
	JumpTarget = FVector(GoalUnits.X - (GoalUnits.X - MyUnits.X) * Backoff,
		GoalUnits.Y - (GoalUnits.Y - MyUnits.Y) * Backoff,
		GoalUnits.Z - static_cast<double>(Backoff) * TroikaSharedZero);
}

// -------------------------------------------------------------------------------------------------
// `0x102aa9e0` — the unnamed task helper.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::FUN_102aa9e0(FPatrolPathCell* Cell)
{
	if (Cell != nullptr && Cell->Path != nullptr)             // 0x102aa9e8 TEST EDI / 0x102aa9f1 TEST [EDI+4]
	{
		if (PatrolPathNextPoint(*Cell->Path))                 // 0x102aa9f3 CALL 0x1000b64f -> 0x10307b80; 0x102aa9fa
		{
			ReleasePatrolPath(Cell);                          // 0x102aa9ff CALL 0x10009638 -> 0x1029f5d0
		}
		// 0x102aaa07 CALL 0x100151f4 -> 0x1029f650(this, cell): `0x1029f6c0` reads the cell's current
		// node, so a cell the release just emptied reads none and only the flag clear lands.
		FUN_1029f650(PatrolCurrentNode(*Cell));
		TaskComplete(false);                                  // 0x102aaa0c PUSH 0 / 0x102aaa10 CALL 0x1000ac68 -> 0x10273e80
		return;                                               // 0x102aaa17
	}
	// 0x102aaa20 / 0x102aaa2a: `+0x1b44` = `AI_BaseNPCTroika.cpp`, `+0x1b48` = 0x3d9c -- the ASSERT pair
	// the shape map calls ABSENT; the line is named here and not stored.
	TaskFail(TroikaTaskArgumentAssertReason);                 // 0x102aaa1c PUSH 0x1d / 0x102aaa34 CALL [EAX+0x700]
}

// -------------------------------------------------------------------------------------------------
// `0x102a0b90` — the crosswalk stamp.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::SetAtCrosswalk(int32 CrosswalkNode)
{
	// `0x102a0b90`, the whole body:
	//     m_bfAINPCFlags |= 0x4;                                    // +0x14b8, AT_CROSSWALK
	//     *(int*)(this + 0x630c) = param_1;
	//
	// The flag bit is what NAMES the write: `0x4` on word one is `AT_CROSSWALK`
	// (`ElysiumNpcFlags.h`), and `+0x630c` sits between `m_iInterestingPlaceGroups` (`+0x62dc`) and
	// `m_iRestorePedLinkNode` (`+0x6310`) — the pedestrian link block. So the unbound word is the
	// crosswalk NODE, and 29c's `Slot0x630c` target name is replaced by that reading.
	NpcFlags.Set(EElysiumNpcFlag::AT_CROSSWALK);
	AtCrosswalkNode = CrosswalkNode;
}

// -------------------------------------------------------------------------------------------------
// `CAI_Motor`'s own vtable — slots 3, 4, 6, 8, 15, 17 and 18 there.
// -------------------------------------------------------------------------------------------------

// -------------------------------------------------------------------------------------------------
// `CAI_Navigator`'s own vtable — slots 7, 11 and 17 there.
// -------------------------------------------------------------------------------------------------

// -------------------------------------------------------------------------------------------------
// `CAI_StandoffBehavior`'s own vtable — slots 3, 5, 20 and 21 there.
// -------------------------------------------------------------------------------------------------

