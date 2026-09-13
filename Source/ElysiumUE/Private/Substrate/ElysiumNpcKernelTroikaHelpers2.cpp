#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumSchedule.h"

// Story 29c-1, family **TroikaHelpers**, part two — the twenty-eight bodies that fill no
// Troika-line slot: the hint lean and the tactical hint search, the alert grade, the detected-attack
// notice, the move stop, the dialogue-release path, the target lead, the ignore-collision triple,
// the jump stamp, the expresser factory, the two unnamed free functions, and the `CAI_Motor`,
// `CAI_Navigator` and `CAI_StandoffBehavior` helpers. The seventeen slot bodies are
// `Substrate/ElysiumNpcKernelTroikaHelpers.cpp`.
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

	// `_DAT_10454110` — `RecordDetectedAttack`'s window. **UNRECOVERED** (past `.data`'s raw size).
	constexpr float GTroikaTailDetectedAttackWindowSeconds = 0.0f;

	// The lean hint's type, the one literal `ApplyHintLeanOffset` and `FindTacticalHintNode` both
	// switch on. Family **Schedule** reads the same `0x27d8` as `GScheduleHintTypeLean`.
	constexpr int32 TroikaHintTypeLean = 0x27d8;

	// `FindTacticalHintNode`'s hint type and `ApplyHintLeanOffset`'s lean constants.
	constexpr int32 TroikaTacticalHintType = 8;
	// `_DAT_1049949c` — the yaw offset the lean adds to or subtracts from the hint's own facing.
	// **UNRECOVERED**; `0.0` leans along the hint's facing itself, which is the degenerate arm.
	constexpr float TroikaHintLeanYawOffset = 0.0f;
	// `_DAT_1049ae8c` (the `bStanding == false` scale) and `_DAT_1049ae90` (the `true` one).
	// **UNRECOVERED**; `0.0` applies no offset, which is the arm that leaves the point where the
	// hint put it.
	constexpr float TroikaHintLeanScaleCrouch = 0.0f;
	constexpr float TroikaHintLeanScaleStand = 0.0f;

	// `_DAT_104454c4` / `_DAT_104454c0` — the shared `0.0` and `1.0` cells, both recovered.
	constexpr float TroikaSharedZero = 0.0f;
	constexpr float TroikaSharedOne = 1.0f;

	// `_DAT_10450564` — `CAI_Motor#4`'s deceleration scale, applied to BOTH the interval bound and
	// the velocity it issues. **UNRECOVERED**.
	constexpr float TroikaMotorDecelScale = 0.0f;
	// `_DAT_10450aa4` — how much of the remaining distance `CAI_Motor#4` draws off the interval per
	// call. **UNRECOVERED**.
	constexpr float TroikaMotorDecelDrain = 0.0f;
	// `_DAT_1044e658` — the distance below which `CAI_Motor#4` treats the goal as reached and draws
	// nothing off the interval. **UNRECOVERED**; family **Lifecycle** reads the same cell for
	// `CAI_StandoffGoal::Spawn`'s next-think.
	constexpr float TroikaMotorArrivedDistanceUnits = 0.0f;
	// `_DAT_1044ffdc` — the scale `CAI_Motor#18` applies to `ftol(yaw) & 0xffff`. The mask says it
	// is the 16-bit angle quantum; the CELL's value is **UNRECOVERED** and is not guessed at, so
	// `0.0` lands and every reissued yaw is zero.
	constexpr float TroikaMotorYawQuantum = 0.0f;

	// The literal activity ids the three `CAI_Motor` bodies force through the owner's vtable
	// `+0x4d8` (slot 342, `ForcePreTranslatedSequenceAndActivity`).
	constexpr int32 TroikaMotorInitActivity = 0x33;     // CAI_Motor#3
	constexpr int32 TroikaMotorStopFaceActivity = 0x2c; // CAI_Motor#6
	constexpr int32 TroikaMotorFullStopActivity = 0x30; // CAI_Motor#8

	// `CAI_Motor#3`'s `SetSolid(2)` and its `m_flGravity` (+0x3ec) zero, and the `(0, 5, 0)` it
	// dispatches through the owner's `+0x340` (slot 208).
	constexpr int32 TroikaMotorInitSolid = 2;
	constexpr int32 TroikaMotorInitSlot208Arg = 5;

	// `CAI_StandoffBehavior::vfunc3`'s capability bit and its sequence activity, both literals.
	constexpr uint32 TroikaStandoffRangedCapabilityBit = 0x8000000u;
	constexpr int32 TroikaStandoffHeaviestSequenceActivity = 8;
	constexpr int32 TroikaStandoffDisciplineCastCounterRequired = 2;

	// `CAI_StandoffBehavior::vfunc20` / `vfunc21`'s behaviour-local schedule id, and `vfunc5`'s
	// literal `2`.
	constexpr int32 TroikaStandoffLocalScheduleId = 0x17;
	constexpr int32 TroikaStandoffOwnerWord0x1fcValue = 2;

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
	// `thunk_FUN_102d1180(hint, this, out)` — the hint's own "where do I stand for you" query.
	// **SEAM**: family **Hints**' standing fact is that there is no `CAI_Hint` entity here and a
	// hint node is a bare index. False, and the point is left exactly as the caller had it.
	(void)HintNode;
	(void)InOutPointUnits;
	return false;
}

bool FElysiumNpc::HintYaw(int32 HintNode, float& OutYaw) const
{
	// `thunk_FUN_102d12e0(hint)` — the hint's own facing yaw. **SEAM**, false.
	(void)HintNode;
	(void)OutYaw;
	return false;
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

FVector FElysiumNpc::HintAttackExtentsUnits() const
{
	// Slot 16 (vtable `+0x40`) — the attack-extent margin. **SEAM**: the port already carries the
	// margin itself (`FElysiumNpc::SetAttackExtents`, `CBaseEntity::SetAttackExtents 0x1009af40`),
	// and nothing produces a per-hint one, so this answers the zero margin.
	return FVector::ZeroVector;
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

int32 FElysiumNpc::NavCurrentLinkActivity() const
{
	// `thunk_FUN_102ee3f0(m_pNavigator)`. **SEAM**: family Motor records the same absent link
	// object (`NavLinkActivity`). `-1` — and `m_IdealActivity` is never `-1` on a live body, so the
	// compare fails and the activity is not replayed, which is the arm that changes nothing.
	return INDEX_NONE;
}

EElysiumScheduleId FElysiumNpc::StandoffScheduleForLocalId(int32 LocalId) const
{
	// `thunk_FUN_102cc1f0(this, localId)` — `CAI_Behavior::GetSchedule(localId)`. **SEAM**: there is
	// no behaviour-local id space on this substrate. `None`, so the compare against a RUNNING
	// program fails and neither `vfunc20` nor `vfunc21` clears its condition.
	(void)LocalId;
	return EElysiumScheduleId::None;
}

uint32 FElysiumNpc::StandoffOwnerCapabilityWord() const
{
	// Slot 513 (vtable `+0x804`) on the owning NPC. **SEAM**, `0` — which closes `vfunc3`'s gate
	// and, because the gate is inside the `+0x19` test, still CLEARS `bStandoffRangedCache`.
	return 0u;
}

int32 FElysiumNpc::SelectHeaviestSequence(int32 Activity, int32 CurrentSequence) const
{
	// `CBaseAnimating::SelectHeaviestSequence(owner, 8, -1)`. **SEAM**: the animating tier publishes
	// no weighted sequence set to the kernel (family **Facing** records the same gap for
	// `SelectWeightedSequence`). `INDEX_NONE`, which is retail's `< 0` refusal.
	(void)Activity;
	(void)CurrentSequence;
	return INDEX_NONE;
}

int32 FElysiumNpc::DisciplineCastCounter() const
{
	// `owner->m_iDisciplineCastCounter`, which `vfunc3` requires to be exactly 2. **SEAM**: no such
	// counter on this leaf; `0`.
	return 0;
}

bool FElysiumNpc::TaskArgumentNeedsClear(const void* TaskArgument) const
{
	// `thunk_FUN_10307b80(arg->+0x4)` — the predicate `FUN_102aa9e0` gates its clear on, over an
	// argument type this substrate stands nothing for. **SEAM**, false.
	(void)TaskArgument;
	return false;
}

bool FElysiumNpc::CreateExpresserObject()
{
	// The factory at vtable `+0x91c` (slot 583). **SEAM**: family **Lifecycle** already states that
	// there is no expression substrate here (`ExpressiveNpcExpresser` answers null and the offset
	// `+0x5f48` is unbound in the shape map). False is retail's own null-result arm, which answers
	// the whole body false.
	++ExpresserFactoryCalls;
	return false;
}

bool FElysiumNpc::ExpresserBaseGate() const
{
	// `thunk_FUN_1027cae0(this)` — the base gate `0x10312cd0` opens with. Retail's base body answers
	// true for a live NPC, which is the fact family **Lifecycle** landed as slot 158 `IsAlive`, so
	// that slot is asked rather than a second gate stood beside it.
	return const_cast<FElysiumNpc*>(this)->IsAlive();
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
	if (ScheduleHost.HintNode == INDEX_NONE)
	{
		InOutPointUnits = FVector::ZeroVector;
		return false;
	}
	HintStandPosition(ScheduleHost.HintNode, InOutPointUnits);

	int32 HintType = INDEX_NONE;
	if (NavHintNodeType(ScheduleHost.HintNode, HintType) && HintType == TroikaHintTypeLean)
	{
		float Yaw = 0.f;
		if (HintYaw(ScheduleHost.HintNode, Yaw))
		{
			const float LeanYaw =
				bLeaningLeft ? Yaw - TroikaHintLeanYawOffset : Yaw + TroikaHintLeanYawOffset;
			const FVector LeanForward = FRotator(0.0, static_cast<double>(LeanYaw), 0.0).Vector();
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
	ScheduleHost.HintNode = FindHintNear(TroikaTacticalHintType,
		static_cast<uint8>(SearchType & 0xffu), IdealHintSearchRangeUnits());
	ScheduleHost.bForceCoverLosCheck = false;
	if (bStayEntrenched && ScheduleHost.HintNode == INDEX_NONE)
	{
		ScheduleHost.HintNode = FindHintNear(TroikaTacticalHintType,
			static_cast<uint8>(SearchType & 0xffu), IdealHintSearchRangeUnits());
	}

	if (ScheduleHost.HintNode != INDEX_NONE)
	{
		PeekOutCount = 0;
		ScheduleHost.FailedCoverLosChecks = 0;
		if (!ClaimHintNode(ScheduleHost.HintNode))
		{
			ScheduleHost.HintNode = INDEX_NONE;
		}
		else
		{
			int32 HintType = INDEX_NONE;
			FVector HintOriginUnits = FVector::ZeroVector;
			float Yaw = 0.f;
			const FElysiumEntity* Cover = (World != nullptr && ScheduleHost.HintCoverObject.IsSet())
				? World->Resolve(ScheduleHost.HintCoverObject)
				: nullptr;
			if (NavHintNodeType(ScheduleHost.HintNode, HintType) && HintType == TroikaHintTypeLean
				&& Cover != nullptr
				&& NavHintNodeOrigin(ScheduleHost.HintNode, HintOriginUnits)
				&& HintYaw(ScheduleHost.HintNode, Yaw))
			{
				const FVector HintForward = FRotator(0.0, static_cast<double>(Yaw), 0.0).Vector();
				const FVector Delta = (Cover->Origin / ElysiumMove::U) - HintOriginUnits;
				bLeaningLeft = TroikaLeanCross2D(Delta, HintForward) <= TroikaSharedZero;
			}
		}
	}

	if (ScheduleHost.HintNode != INDEX_NONE)
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

TCHAR FElysiumNpc::AdvanceAlertLevelGrade()
{
	// `0x102b8980`, the whole body:
	//     if (m_bFullInvestigate) SetAlertLevel(3);                 // +0x6340, 0x102b5dc0
	//     switch (m_eAlertLevel) {                                  // +0x63f4, READ AFTER the write
	//         default: SetAlertLevel(1); return 'L';
	//         case 1:  SetAlertLevel(2); return 'M';
	//         case 2:
	//         case 3:  SetAlertLevel(3); return 'Q' + ((m_afMemory & 0x8000000) != 0);
	//     }
	//
	// `m_bFullInvestigate` writes the level BEFORE the switch reads it, so a full-investigate NPC
	// always lands on the 2/3 arm and answers 'Q' or 'R'. The letters are the whole of the return:
	// L, M, Q and R, in rung order, and they are retail's — the grade string they belong to is
	// **unrecovered**.
	if (FullInvestigate != 0)
	{
		AlertLevel = 3;
	}
	switch (AlertLevel)
	{
	case 1:
		AlertLevel = 2;
		return TEXT('M');
	case 2:
	case 3:
		AlertLevel = 3;
		// `m_afMemory & 0x8000000` (+0x5d8c, `FElysiumNpcScheduleHost::MemoryBits`) adds one to the
		// letter, so 'Q' becomes 'R'. The bit's name is **unrecovered**.
		return static_cast<TCHAR>(TEXT('Q')
			+ ((ScheduleHost.MemoryBits & 0x8000000u) != 0 ? 1 : 0));
	default:
		break;
	}
	AlertLevel = 1;
	return TEXT('L');
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
	// `DetectedAttackTime`, which family **Squad**'s `HasDetectedAttack` is the reader of.
	if (bIgnoreDetectedAttack)
	{
		return;
	}
	const FElysiumEntity* Redirected = RedirectDetectedAttacker(Attacker);
	Senses.Memory.DetectedAttackAttacker =
		Redirected != nullptr ? Redirected->Handle : FElysiumEntityHandle();
	Senses.Memory.DetectedAttackTime = (World != nullptr ? World->NowSeconds() : 0.0)
		+ static_cast<double>(GTroikaTailDetectedAttackWindowSeconds);
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
	ScheduleHost.bShouldMove = false;
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
	// behind an unrecovered cvar (`DAT_109241fc`, the same `ConVar::GetBool()` shape families Facing
	// and Motor record). It reaches no game state and is named rather than ported.
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
					&& Trace.Fraction != TroikaSharedOne)
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
// `0x10312cd0` — the expresser factory (slot 424 on `CAI_BaseHumanoid` / `CAI_ExpressiveNPC`).
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::CreateExpresser()
{
	// `0x10312cd0`, the whole body:
	//     if (!BaseGate(this)) return false;                        // 0x1027cae0
	//     e = this->vtable[+0x91c]();                               // slot 583, the factory
	//     m_pExpresser = e;                                         // +0x5f48
	//     if (e == NULL) return false;
	//     e->+0x04 = this;                                          // the owner back-link
	//     e->+0x10 = &this->+0x5f44;                                // the list-node back-link
	//     Setup(e);                                                 // 0x10312b20
	//     return true;
	//
	// `docs/vtmb/npc-ai/lifecycle.md` names it the expresser factory on `CAI_BaseNPC`. The
	// store — `+0x5f48` — is unbound in the shape map and family **Lifecycle**'s
	// `ExpressiveNpcExpresser` answers null for the same reason, so the factory seam answers false
	// and the whole body answers false AFTER the gate. The gate is what is observable: a dead body
	// never reaches the factory at all.
	if (!ExpresserBaseGate())
	{
		return false;
	}
	return CreateExpresserObject();
}

// -------------------------------------------------------------------------------------------------
// `0x102aa9e0` — the unnamed task helper.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::FUN_102aa9e0(const void* TaskArgument)
{
	// `0x102aa9e0`, the whole body:
	//     if (arg != NULL && arg->+0x4 != NULL) {
	//         if (thunk_FUN_10307b80(arg->+0x4)) thunk_FUN_1029f5d0(arg);
	//         thunk_FUN_1029f650(this, arg);
	//         TaskComplete(false);                                  // 0x10273e80
	//         return;
	//     }
	//     m_assertFile = "E:\\Vampire\\main\\dlls\\AI_BaseNPCT..."; m_assertLine = 0x3d9c;
	//     this->vtable[+0x700](0x1d);                               // slot 448, TaskFail
	//
	// `+0x1b44`/`+0x1b48` is the ASSERT pair, which the shape map calls ABSENT (family **Motor**
	// records the same for `OnNavFailed`); the line number `0x3d9c` is named here and not stored.
	// The argument's own type is not stood by this substrate, so the two middle calls are seams and
	// the branch itself is what lands. `docs/vtmb/npc-ai/programs.md` documents the body and the
	// port does not cite it, so the `FUN_` spelling stands.
	if (TaskArgument != nullptr)
	{
		if (TaskArgumentNeedsClear(TaskArgument))
		{
			++TaskArgumentClears;
		}
		++TaskArgumentForwards;
		// `TaskComplete(false)` — `m_ScheduleState.fTaskStatus = COMPLETE`, which this runtime
		// spells as the schedule state's external-completion latch.
		Schedule.bTaskCompletedExternally = true;
		return;
	}
	TaskFail(TroikaTaskArgumentAssertReason);
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

void FElysiumNpc::FUN_102e0ea0()
{
	// `CAI_Motor#3` `0x102e0ea0`, in retail's order:
	//     owner->vtable[+0x4d8](0x33);          // slot 342, force activity 0x33 — FIRST
	//     MotorStateReset(this);                // thunk_FUN_102e2840
	//     SetSolid(owner, 2);                   // thunk_FUN_100dc480(owner->m_Collision, 2)
	//     owner->m_flGravity (+0x3ec) = 0;
	//     owner->vtable[+0x340](0, 5, 0);       // slot 208
	//
	// The activity dispatch is BEFORE the state reset, which the 29c walk has the other way round.
	++TroikaMotor.ForcedActivities;
	TroikaMotor.LastForcedActivity = TroikaMotorInitActivity;
	++TroikaMotor.StateResets;
	++TroikaMotor.SolidSets;
	(void)TroikaMotorInitSolid;
	(void)TroikaMotorInitSlot208Arg;
	++TroikaMotor.OwnerSlot208Dispatches;
}

bool FElysiumNpc::FUN_102e0f90(const FVector& GoalUnits, float Yaw)
{
	// `CAI_Motor#4` `0x102e0f90`:
	//     d = VectorNormalize(goal - owner->slot220());                  // vtable +0x370
	//     SetAbsVelocity((goal - pos) * _DAT_10450564);                  // thunk_FUN_102e2690
	//     if (d < m_flMoveInterval * _DAT_10450564) {                    // +0x30
	//         if (d <= _DAT_1044e658) d = 0;
	//         m_flMoveInterval -= d * _DAT_10450aa4;
	//         owner->vtable[+0xf8](goal);
	//         return true;
	//     }
	//     m_flMoveInterval = 0;
	//     ReissueMove(this, yaw, -1.0);                                  // thunk_FUN_102e1c10
	//     return false;
	//
	// The velocity is issued on BOTH arms — it is written before the branch — and the `d <= arrived`
	// clamp means a body inside the arrival radius decelerates by nothing at all rather than by a
	// tiny amount. Both are retail's. SOURCE units.
	//
	// `slot220` (vtable `+0x370`) is the position accessor family **Hints** records as unidentified
	// in the census and answers with the entity's own origin; that same reading is used here rather
	// than a second one.
	const FVector PositionUnits = HintComparePosition(this) / ElysiumMove::U;
	const FVector Delta = GoalUnits - PositionUnits;
	const float Distance = static_cast<float>(Delta.Size());

	++TroikaMotor.VelocitySets;
	TroikaMotor.LastVelocityUnits = Delta * TroikaMotorDecelScale;

	if (Distance < TroikaMotor.MoveInterval * TroikaMotorDecelScale)
	{
		const float Drain = Distance <= TroikaMotorArrivedDistanceUnits ? 0.f : Distance;
		TroikaMotor.MoveInterval -= Drain * TroikaMotorDecelDrain;
		++TroikaMotor.OwnerMoveDispatches;
		return true;
	}
	TroikaMotor.MoveInterval = 0.f;
	++TroikaMotor.MoveReissues;
	TroikaMotor.LastReissueYaw = Yaw;
	TroikaMotor.LastReissueSpeed = -1.0f;
	return false;
}

void FElysiumNpc::FUN_102e1180(const FVector& GoalUnits)
{
	// `CAI_Motor#6` `0x102e1180`:
	//     SetAbsVelocity(goal);                          // thunk_FUN_102e2690(this, param_1)
	//     owner->vtable[+0x340](0);                      // slot 208
	//     owner->vtable[+0x4d8](0x2c);                   // slot 342, force activity 0x2c
	//     ReissueMove(this, UTIL_VecToYaw(goal), -1.0);  // thunk_FUN_101d2c70, thunk_FUN_102e1c10
	//
	// Note it hands the GOAL VECTOR straight to `SetAbsVelocity`, not a scaled direction — that is
	// what the decompiled C says and it is reproduced.
	++TroikaMotor.VelocitySets;
	TroikaMotor.LastVelocityUnits = GoalUnits;
	++TroikaMotor.OwnerSlot208Dispatches;
	++TroikaMotor.ForcedActivities;
	TroikaMotor.LastForcedActivity = TroikaMotorStopFaceActivity;
	++TroikaMotor.MoveReissues;
	// `UTIL_VecToYaw(v)` — the planar heading, degrees.
	TroikaMotor.LastReissueYaw = static_cast<float>(
		FMath::RadiansToDegrees(FMath::Atan2(GoalUnits.Y, GoalUnits.X)));
	TroikaMotor.LastReissueSpeed = -1.0f;
}

void FElysiumNpc::FUN_102e1270()
{
	// `CAI_Motor#8` `0x102e1270` (also `CAI_HumanoidMotor#8`), the whole body:
	//     SetAbsVelocity(0, 0, 0);                       // thunk_FUN_102e2690 on a zeroed local
	//     owner->vtable[+0x4d8](0x30);                   // slot 342, force activity 0x30
	//
	// The retail motor's full stop, and there is no reissued move on this one.
	++TroikaMotor.VelocitySets;
	TroikaMotor.LastVelocityUnits = FVector::ZeroVector;
	++TroikaMotor.ForcedActivities;
	TroikaMotor.LastForcedActivity = TroikaMotorFullStopActivity;
}

FVector FElysiumNpc::BlendFacingQueue(TArrayView<const FFacingQueueEntry> Entries,
	const FVector& SelfUnits)
{
	// `CAI_Motor#15` `0x102e2180`'s second loop, as a pure function:
	//     acc = (0,0,0);
	//     for (e : entries) {
	//         w = Weight(e);
	//         VectorNormalize(Target(e) - GetAbsOrigin());     // computed and DISCARDED
	//         acc = (Target(e) - GetAbsOrigin()) * w + acc * (1 - w);
	//         VectorNormalize(acc);
	//     }
	//
	// The discarded normalise is retail's: the registers the blend multiplies are loaded BEFORE the
	// call, so the raw delta is what is blended, and only the accumulator is normalised. `1 - w` is
	// `_DAT_104454c0 - w`, the shared `1.0`.
	FVector Accumulator = FVector::ZeroVector;
	for (const FFacingQueueEntry& Entry : Entries)
	{
		const FVector Delta = Entry.TargetUnits - SelfUnits;
		const float Inverse = TroikaSharedOne - Entry.Weight;
		Accumulator = Delta * Entry.Weight + Accumulator * Inverse;
		Accumulator.Normalize();
	}
	return Accumulator;
}

FVector FElysiumNpc::FUN_102e2180(int32& OutSurvivors)
{
	// `CAI_Motor#15` `0x102e2180`, both loops:
	//     for (i = 0; i < m_facingQueueCount; ) {                        // +0x3c
	//         if (!Live(queue[i])) { RemoveAt(queue, i, 1); --count; }   // 0x102d8b50, 0x102e2b10
	//         else ++i;
	//     }
	//     <the blend above>
	//
	// The compaction does NOT advance `i` when it drops an entry, so two expired entries in a row
	// are both removed — that is the correct in-place compaction and it is what retail wrote.
	//
	// **SEAM**: family **Facing**'s standing fact is that this mover keeps no facing queue
	// (`FacingTargetRequests` records what would have gone into one). The queue is therefore empty,
	// the compaction removes nothing, and the blend answers the zero vector — retail's own answer
	// for an empty queue.
	OutSurvivors = TroikaMotor.FacingQueueCount;
	TArray<FFacingQueueEntry> Live;
	return BlendFacingQueue(Live, Origin / ElysiumMove::U);
}

float FElysiumNpc::FUN_102e2580() const
{
	// `CAI_Motor#17` `0x102e2580`, the whole body:
	//     s = BaseSpeed(this);                            // thunk_FUN_102e12c0
	//     if (s < this->vtable[+0x40]()) s = this->vtable[+0x40]();
	//     f = HullSpeedFloor(this->field_0x8);            // thunk_FUN_102d61b0
	//     return f <= s ? s : f;
	//
	// **Both bounds are FLOORS.** 29c's walk reads the first as an upper bound; the listing raises
	// `s` to it when `s` is below, which is a maximum and not a clamp. So the body is
	// `max(max(base, motorFloor), hullFloor)` and the motor's `+0x40` cannot slow a body down.
	// SOURCE units.
	// `thunk_FUN_102e12c0(this)` — the base speed. **SEAM**, `0.0`; the same seam `CAI_Navigator#17`
	// asks for its own `out[9]`.
	float Speed = 0.f;
	if (Speed < TroikaMotor.SpeedCeilingUnits)
	{
		Speed = TroikaMotor.SpeedCeilingUnits;
	}
	return TroikaMotor.HullSpeedFloorUnits <= Speed ? Speed : TroikaMotor.HullSpeedFloorUnits;
}

void FElysiumNpc::FUN_102e19e0(const FVector& GoalUnits)
{
	// `CAI_Motor#18` `0x102e19e0`:
	//     if (owner->vtable[+0x838](goal, m_flMoveInterval)) return;     // slot 526, the clip test
	//     UTIL_VecToYaw(goal + 0xc);                                     // computed, DISCARDED
	//     seq = owner->m_nSequence;                                      // +0x6f0
	//     thunk_FUN_102e2790(this, seq);
	//     if (!HasPoseParameter(this, seq, "move_yaw")) {                 // thunk_FUN_102e2820
	//         ReissueMove(this, (ftol(...) & 0xffff) * _DAT_1044ffdc, -1.0);
	//         return;
	//     }
	//     this->vtable[+0x3c](&avg);                                     // motor slot 15 — the
	//                                                                    // facing-queue average
	//     VectorNormalize(avg);
	//     yaw = UTIL_VecToYaw(avg);
	//     ReissueMove(this, (ftol(yaw) & 0xffff) * _DAT_1044ffdc, -1.0);
	//     diff = UTIL_AngleDiff(yaw, owner->slot221()[1]);               // 0x1013d580
	//     if (owner->field_0x98) owner->field_0x98->m_flDesiredMoveYaw = -diff;   // +0x63ec
	//     else SetPoseParameter(this, "move_yaw", -diff);                // thunk_FUN_102e27d0
	//
	// `vtable[+0x3c]` on the MOTOR is slot 15, which IS `FUN_102e2180` above — the steering asks the
	// facing-queue average for its heading rather than the goal. That is the recovered link between
	// the two bodies and it is a call here, not a copy.
	//
	// The named condition key is `"move_yaw"`, and the two writes are the same number by two routes:
	// an NPC-backed motor writes the NPC's own word, a bare one writes the pose parameter.
	if (TroikaMotor.bSteerClipped)
	{
		return;
	}
	const int32 Sequence = SequenceNumber;
	if (!TroikaMotor.bHasMoveYawPoseParam)
	{
		++TroikaMotor.MoveReissues;
		TroikaMotor.LastReissueYaw = static_cast<float>(FMath::TruncToInt(
			FMath::RadiansToDegrees(FMath::Atan2(GoalUnits.Y, GoalUnits.X))) & 0xffff)
			* TroikaMotorYawQuantum;
		TroikaMotor.LastReissueSpeed = -1.0f;
		return;
	}
	(void)Sequence;

	int32 Survivors = 0;
	FVector Average = FUN_102e2180(Survivors);
	Average.Normalize();
	const float Yaw = static_cast<float>(
		FMath::RadiansToDegrees(FMath::Atan2(Average.Y, Average.X)));
	++TroikaMotor.MoveReissues;
	TroikaMotor.LastReissueYaw =
		static_cast<float>(FMath::TruncToInt(Yaw) & 0xffff) * TroikaMotorYawQuantum;
	TroikaMotor.LastReissueSpeed = -1.0f;

	// `UTIL_AngleDiff(yaw, myAngles.yaw)` — the wrapped difference family **Bosses** already
	// transcribes for the same `0x1013d580`.
	const float Diff = static_cast<float>(FMath::FindDeltaAngleDegrees(
		Angles.Y, static_cast<double>(Yaw)));
	// Retail's `owner->field_0x98` is the owning NPC behind a bare motor; in this runtime the motor
	// IS the NPC, so the NPC-backed arm is the one that always runs.
	ScheduleHost.DesiredMoveYaw = -Diff;
	++TroikaMotor.PoseParamWrites;
	TroikaMotor.LastPoseParamYaw = -Diff;
}

// -------------------------------------------------------------------------------------------------
// `CAI_Navigator`'s own vtable — slots 7, 11 and 17 there.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::NavStopAndMarkDirty()
{
	// `thunk_FUN_102eeb70(this)` then `this->field_0x1c = 1`:
	//     nav->+0x54 = -1;
	//     nav->+0x58 = -1.0;
	//     nav->+0x60 = -1.0;
	//     ClearRoute(nav->+0x28);                                        // thunk_FUN_102ddc40
	//     nav->+0x1c = 1;
	//
	// `+0x1c` is family **Motor**'s `Navigator.bNavFailed` — the same latch `OnNavFailed`
	// (`0x102eeae0`) sets — so this writes THAT member rather than a second copy of the word.
	NavigatorWord0x54 = -1;
	NavigatorWord0x58 = -1.0f;
	NavigatorWord0x60 = -1.0f;
	++NavigatorRouteClears;
	Navigator.bNavFailed = true;
}

void FElysiumNpc::FUN_102eea70()
{
	// `CAI_Navigator#7` `0x102eea70`.
	NavStopAndMarkDirty();
}

void FElysiumNpc::FUN_102eeac0()
{
	// `CAI_Navigator#11` `0x102eeac0` — a BYTE-IDENTICAL body at a distinct slot. Two retail entry
	// points, one behaviour, so one method and two forwards.
	NavStopAndMarkDirty();
}

FElysiumNpc::FNavPathSample FElysiumNpc::NavPathSample() const
{
	// `m_pPath`'s four reads: the current point (`0x10012805`), the straight-line test
	// (`0x1030bd50`), the navigator radius (`0x102ecc40`) and the next waypoint's kind against its
	// owner's (`path+0x24` -> `+0x30` -> `+0x2c`). **SEAM**: family Motor's standing fact — no path
	// object here. Everything false and zero.
	return FNavPathSample();
}

FElysiumNpc::FNavMoveInfo FElysiumNpc::FUN_102eee40() const
{
	// `CAI_Navigator#17` `0x102eee40`, field by field:
	//     out[12] = m_navType;                                           // +0x18
	//     out[0..2] = CurrentPathPoint(m_pPath);
	//     out[3] = out[0] - myPos.x;  out[4] = out[1] - myPos.y;         // slot 220 (+0x370)
	//     if (m_navType == 0) { out[5] = 0; out[10] = Length2D(out+3); }
	//     else                { out[5] = out[2] - myPos.z; out[10] = VectorNormalize(out+3); }
	//     out[6..8] = out[3..5];
	//     out[9]  = BaseSpeed(nav->+0x20);                               // thunk_FUN_102e12c0
	//     out[11] = nav->+0x20->[+0x30] * out[9];
	//     out[13] = NavRadius(this);                                     // 0x102ecc40
	//     if (out[10] < out[11]) out[11] = out[10];
	//     if (IsStraightLine(m_pPath)) { out[14] |= 1; out[15] = m_pPath; return; }
	//     wp = m_pPath->+0x24; next = wp->+0x30;
	//     if (next && next->+0x2c != wp->+0x2c) out[14] |= 4;
	//     out[15] = m_pPath;
	//
	// **`out[10]` is computed BEFORE `out[11]`, and `out[3..5]` is normalised on the 3-D arm** — so
	// `out[6..8]` is the NORMALISED direction at nav type != 0 and the RAW delta at nav type 0.
	// That asymmetry is retail's and is reproduced.
	//
	// The goal tolerance is the MIN of the computed limit and the distance, which is why a body that
	// is nearly there cannot overshoot its own tolerance.
	FNavMoveInfo Info;
	Info.NavType = NavGetType();

	const FNavPathSample Path = NavPathSample();
	if (!Path.bValid)
	{
		// No path: retail would have dereferenced `m_pPath`. The block comes back zeroed with
		// `bHasPath` clear, which is this runtime's stated refusal.
		return Info;
	}

	const FVector MyUnits = HintComparePosition(this) / ElysiumMove::U;
	Info.TargetUnits = Path.PointUnits;
	Info.DeltaUnits.X = Info.TargetUnits.X - MyUnits.X;
	Info.DeltaUnits.Y = Info.TargetUnits.Y - MyUnits.Y;
	if (Info.NavType == 0)
	{
		Info.DeltaUnits.Z = 0.0;
		Info.DistanceUnits = static_cast<float>(
			FMath::Sqrt(Info.DeltaUnits.X * Info.DeltaUnits.X
				+ Info.DeltaUnits.Y * Info.DeltaUnits.Y));
	}
	else
	{
		Info.DeltaUnits.Z = Info.TargetUnits.Z - MyUnits.Z;
		Info.DistanceUnits = static_cast<float>(Info.DeltaUnits.Size());
		Info.DeltaUnits.Normalize();
	}
	Info.DirUnits = Info.DeltaUnits;
	// `thunk_FUN_102e12c0(nav->+0x20)` — the motor's base speed, the same seam `CAI_Motor#17` asks.
	Info.BaseSpeedUnits = 0.f;
	// `nav->+0x20->[+0x30]` is `CAI_Motor::m_flMoveInterval`, which this family carries.
	Info.GoalToleranceUnits = TroikaMotor.MoveInterval * Info.BaseSpeedUnits;
	Info.Radius = Path.RadiusUnits;
	if (Info.DistanceUnits < Info.GoalToleranceUnits)
	{
		Info.GoalToleranceUnits = Info.DistanceUnits;
	}
	if (Path.bStraightLine)
	{
		Info.Flags |= 1u;
	}
	else if (Path.bNextWaypointKindDiffers)
	{
		Info.Flags |= 4u;
	}
	Info.bHasPath = true;
	return Info;
}

// -------------------------------------------------------------------------------------------------
// `CAI_StandoffBehavior`'s own vtable — slots 3, 5, 20 and 21 there.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::StandoffVfunc3()
{
	// `0x102c7410`, arm by arm:
	//     if (this[0x19] == 0) return false;
	//     if (owner->vtable[+0x804]() & 0x8000000) {                     // slot 513
	//         if (SelectHeaviestSequence(owner, 8, -1) >= 0) {
	//             if (owner->m_iDisciplineCastCounter == 2
	//                 && GetActiveWeapon(owner) != NULL) return true;
	//             return false;                                          // and +0x19 is KEPT
	//         }
	//     }
	//     this[0x19] = 0;                                                // both other refusals
	//     return false;
	//
	// **The clear is NOT on every refusal.** A body that has the capability bit and a sequence but
	// fails the counter or the weapon KEEPS `+0x19`; only "no capability" and "no sequence" clear
	// it. That is the whole shape of the body and it is easy to read the other way round.
	if (!bStandoffRangedCache)
	{
		return false;
	}
	if ((StandoffOwnerCapabilityWord() & TroikaStandoffRangedCapabilityBit) != 0)
	{
		if (SelectHeaviestSequence(TroikaStandoffHeaviestSequenceActivity, INDEX_NONE) >= 0)
		{
			const bool bHasWeapon = World != nullptr && Inventory.ActiveWeapon.IsSet()
				&& World->Resolve(Inventory.ActiveWeapon) != nullptr;
			return DisciplineCastCounter() == TroikaStandoffDisciplineCastCounterRequired && bHasWeapon;
		}
	}
	bStandoffRangedCache = false;
	return false;
}

void FElysiumNpc::StandoffVfunc5()
{
	// `0x102c7530`, the whole body:
	//     if (owner->m_pHintNode != NULL) {                              // +0x5ddc
	//         if (IsHintUnusable(hint)) {                                // 0x102d14c0
	//             if (thunk_FUN_102d1450(hint, owner)) ReleaseHint(hint, 0.0);   // 0x102d1420
	//         }
	//     }
	//     owner->m_pHintNode = NULL;
	//     owner->m_flDistTooFar = this->+0x50;                           // +0x5de4
	//     owner->+0x1fc = 2;
	//
	// **The release is gated on `IsHintUnusable` being TRUE**, which reads backwards — retail
	// releases a hint it has already decided is unusable. Reproduced as written. The hint is cleared
	// UNCONDITIONALLY afterwards, so a usable hint is dropped without being released, which is the
	// leak the schedule kernel's own release path exists to avoid.
	//
	// `IsHintUnusable` and `ReleaseHintNode` are family **Hints**' bodies and are called rather than
	// restated; `thunk_FUN_102d1450` (does this NPC own the hint) is `ScheduleHost.bOwnsHint`.
	if (ScheduleHost.HintNode != INDEX_NONE)
	{
		const double Now = World != nullptr ? World->NowSeconds() : 0.0;
		if (IsHintUnusable(ScheduleHost.HintNode, Now) && ScheduleHost.bOwnsHint)
		{
			ReleaseHintNode(ScheduleHost.HintNode, 0.f);
		}
	}
	ScheduleHost.HintNode = INDEX_NONE;
	DistTooFar = StandoffDistTooFar;
	Field_0x01fc = TroikaStandoffOwnerWord0x1fcValue;
}

void FElysiumNpc::StandoffClearNewEnemyOnLocalSchedule()
{
	// `0x102c7960` / `0x102c79a0`, the whole body:
	//     if (owner->m_pSchedule != NULL                                 // +0x5c38
	//         && owner->m_pSchedule == GetSchedule(this, 0x17))          // 0x102cc1f0
	//         ClearCondition(owner, COND_NEW_ENEMY 0x54);                // 0x10269f30
	//
	// `StandoffScheduleForLocalId` is a seam answering `None`, so a running program never matches
	// and the condition is never cleared. That is the recovered refusal: the behaviour-local id
	// space has no source here.
	if (Schedule.Current == EElysiumScheduleId::None)
	{
		return;
	}
	if (Schedule.Current != StandoffScheduleForLocalId(TroikaStandoffLocalScheduleId))
	{
		return;
	}
	Cognition.Conditions.Clear(EElysiumNpcCond::NewEnemy);
}

void FElysiumNpc::StandoffVfunc20()
{
	// `CAI_StandoffBehavior#20` `0x102c7960`.
	StandoffClearNewEnemyOnLocalSchedule();
}

void FElysiumNpc::StandoffVfunc21()
{
	// `CAI_StandoffBehavior#21` `0x102c79a0` — a BYTE-IDENTICAL body at a distinct slot, the same
	// pair the navigator's 7/11 are.
	StandoffClearNewEnemyOnLocalSchedule();
}
