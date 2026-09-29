// `CAI_BaseNPC`'s bodies of the `Helpers2` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseHelpers2.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcTroikaHelpers2Shared.h"
#include "Substrate/ElysiumPlaceSet.h"
#include "Substrate/ElysiumSchedule.h"

// --- File-scope helpers moved with the base bodies (story 5 step 5) ---

namespace
{
	// `_DAT_10450564` — `CAI_Motor#4`'s deceleration scale (100.0), applied to BOTH the interval
	// bound and the velocity it issues.
	constexpr float TroikaMotorDecelScale = ElysiumNpcTunables::Hundred;
	// `_DAT_10450aa4` — how much of the remaining distance `CAI_Motor#4` draws off the interval per
	// call (0.01).
	constexpr float TroikaMotorDecelDrain = ElysiumNpcTunables::Hundredth;
	// `_DAT_1044e658` — the distance at or below which `CAI_Motor#4` treats the goal as reached and
	// draws nothing off the interval: the DOUBLE 0.01 (`102e1050 FCOMP double ptr`).
	constexpr double TroikaMotorArrivedDistanceUnits = ElysiumNpcTunables::HundredthDouble;
	// The literal activity id `CAI_Motor#8` forces through the owner's vtable
	// `+0x4d8` (slot 342, `ForcePreTranslatedSequenceAndActivity`).
	constexpr int32 TroikaMotorFullStopActivity = 0x30; // CAI_Motor#8
}

// --- Moved from `ElysiumNpcTroikaHelpers2.cpp` (story 5 step 5) ---

bool FElysiumNpcBase::HintYaw(int32 HintNode, float& OutYaw) const
{
	// `thunk_FUN_102d12e0(hint)` — the hint's yaw, the whole body:
	//
	//     if (hint->m_nNodeID (+0x5e4) != -1) return 0x102f47b0(DAT_1093407c, id);   // node +0x6c
	//     return hint->GetAbsAngles()->y;                                             // vtable +0x374
	//
	// `0x102f47b0` answers 0.0f outside `-1 < id <= count` (`FElysiumPlaceSet::NetworkNodeYawSource`).
	// A RETAIL-frame (Source) yaw on both arms: `FHintWords::Angles` is the keyvalue `angles`
	// verbatim, and the node row's yaw is reflected back. False only for an index that names no
	// live hint.
	FHintWords Words;
	if (!HintWords(HintNode, Words))
	{
		return false;
	}
	if (Words.NodeId != INDEX_NONE)                                            // 0x102d12e3 CMP -1
	{
		OutYaw = World != nullptr ? World->Places().NetworkNodeYawSource(Words.NodeId) : 0.0f;
		return true;
	}
	OutYaw = static_cast<float>(Words.Angles.Y);                              // vtable +0x374, [+4]
	return true;
}

FVector FElysiumNpcBase::HintAttackExtentsUnits() const
{
	// Slot 16 (vtable `+0x40`) — the attack-extent margin. **SEAM**: the port already carries the
	// margin itself (`FElysiumEntity::SetAttackExtents`, `CBaseEntity::SetAttackExtents 0x1009af40`),
	// and nothing produces a per-hint one, so this answers the zero margin.
	return FVector::ZeroVector;
}

int32 FElysiumNpcBase::NavCurrentLinkActivity() const
{
	// `thunk_FUN_102ee3f0(m_pNavigator)`. **SEAM**: family Motor records the same absent link
	// object (`NavLinkActivity`). `-1` — and `m_IdealActivity` is never `-1` on a live body, so the
	// compare fails and the activity is not replayed, which is the arm that changes nothing.
	return INDEX_NONE;
}

int32 FElysiumNpcBase::SelectHeaviestSequence(int32 Activity, int32 CurrentSequence) const
{
	// `CBaseAnimating::SelectHeaviestSequence(activity, -1)`. The sequence bridge (story 8 wave 2):
	// this runtime's resolver has no weights, so the heaviest sequence is the resolver's own first
	// (primary) answer for the activity, which is also what the weighted pick answers here.
	(void)CurrentSequence;
	return SelectWeightedSequenceForActivity(Activity);
}

void FElysiumNpcBase::FUN_102e1270()
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

void FElysiumNpcBase::NavStopAndMarkDirty()
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

void FElysiumNpcBase::FUN_102eea70()
{
	// `CAI_Navigator#7` `0x102eea70`.
	NavStopAndMarkDirty();
}
