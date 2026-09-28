// Story 0019/8 (29e under the strict verdict), family **RunAi19** -- `CAI_BaseNPC`'s bodies.
//
// Declarations are in `ElysiumNpcBaseRunAi19.inl` (included inside `class FElysiumNpcBase`) or
// generated in `ElysiumNpcBaseSlots.inl` for a slot body. Walked prose:
// `docs/vtmb/npc-ai/schedule-kernel.md` § "Story 8, family RunAi19".
//
// Owns (RunAi19's `rule` rows): 0x1026f110 CAI_BaseNPC::RunAI.

#include "Substrate/ElysiumNpcBase.h"

#include "ElysiumEntityWorld.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcMaker.h"

namespace
{
	/** `PUSH 0x5` at `0x1026f1d8`: the overlay's duration in seconds. */
	constexpr float RunAi19NoNodesOverlaySeconds = 5.0f;
}

bool FElysiumNpcBase::RunAi19DialogPartnerLive() const
{
	const FElysiumNpc* const Troika = AsNpc();
	return Troika != nullptr && Troika->HasLiveDialogPartner();
}

void FElysiumNpcBase::RunAi19AddTimedOverlay(const TCHAR* Text, float DurationSeconds)
{
	RunAi19LastOverlayText = Text;
	RunAi19LastOverlaySeconds = DurationSeconds;
	++RunAi19OverlayCalls;
}

void FElysiumNpcBase::RunAI(bool bReduced)
{
	// `CAI_BaseNPC::RunAI` (`0x1026f110`, slot 432), the decision pass. Absent by convention: the
	// scope-trace push/pop (`0x1026f11d`/`0x1026f127` name pick, `0x1026f3f0` pop), the VProf scopes
	// `CAI_BaseNPC_RunAI` (`0x1026f194`) and `CAI_BaseNPC_RunAI_GatherConditions` (`0x1026f22d`) with
	// their exits (`0x1026f258`..`0x1026f292`: branches `0x1026f258` `0x1026f260` `0x1026f270`, call
	// `0x1026f268`; `0x1026f37b`..`0x1026f3ea`: branches `0x1026f37b` `0x1026f383` `0x1026f392`
	// `0x1026f397` `0x1026f3c8`, calls `0x1026f39e` `0x1026f3bf`), and the three RDTSC
	// timers into `0x109203e8`, `0x1090fe68` (the `PrescheduleThink` bracket) -- no observable.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;

	// `m_bConditionsGathered (+0x5ca4) = 0`. The shape map binds the byte to `Cognition.GatheredAt`,
	// whose stamp form L07 fixed as "non-negative = gathered this pass"; the clear is the negative.
	Cognition.GatheredAt = -1.0;                                              // 0x1026f1ad

	// The debug overlay: `developer` (`DAT_1070af4c`: not a command, `m_nValue != 0`) AND the
	// navigator's `m_bNotOnNetwork` (`m_pNavigator +0x34`). The shipped `developer` is 0, so the arm
	// is dead in retail as it is here.
	if (FElysiumNpcMaker::DeveloperCvarLevel != 0                             // 0x1026f1be / 0x1026f1c3 / 0x1026f1c8 / 0x1026f1cb
		&& Conditions19NavNotOnNetwork())                                     // 0x1026f1d3
	{
		RunAi19AddTimedOverlay(TEXT("NPC w/no reachable nodes!"),
			RunAi19NoNodesOverlaySeconds);                                    // 0x1026f1e1
	}

	// `GatherConditions` only on a full pass AND with no live dialogue partner; both refusals are a
	// plain skip -- the standing condition set is left as it is.
	if (!bReduced                                                             // 0x1026f1ea
		&& !RunAi19DialogPartnerLive())                                       // 0x1026f1f9 / 0x1026f215 / 0x1026f219
	{
		GatherConditions();                                                   // 0x1026f237 slot 433
		// A derived `GatherConditions` that did not call the base still latches the byte.
		if (Cognition.GatheredAt < 0.0)                                       // 0x1026f243
		{
			Cognition.GatheredAt = Now;                                       // 0x1026f245
		}
	}

	// `0x1026ab50`, the shrunk-hull head probe (family Senses10), on every pass.
	HeadProbe();                                                              // 0x1026f29a
	PrescheduleThink();                                                       // 0x1026f2b6 slot 434
	MaintainSchedule(Now, bReduced);                                          // 0x1026f302 0x102817c0

	// The end-of-pass clear, full passes only: the one-pass life of the two damage bits and the bump.
	if (!bReduced)                                                            // 0x1026f30b
	{
		Cognition.Conditions.Clear(EElysiumNpcCond::LightDamage);             // 0x1026f311 0x10269b50(0x4c)
		Cognition.Conditions.Clear(EElysiumNpcCond::HeavyDamage);             // 0x1026f31a 0x10269b50(0x4d)
		Cognition.Conditions.Clear(EElysiumNpcCond::WasBumped);               // 0x1026f323 0x10269b50(0x38)
	}
	BaseScheduleHost.bRanAi = true;                                           // 0x1026f32c +0x1b4c
}

bool FElysiumNpcBase::RunAi19NavUpdateGoalPos(const FVector& GoalCm)
{
	// `0x102ee220`: `0x1030b950(m_pPath, &pos)` (`0x102ee22b`) then `0x102f1dc0(this, 0, 0)`
	// (`0x102ee236`), whose byte is the answer. The re-aim is the landed recording seam.
	NavUpdateGoalPos(GoalCm);
	return true;
}
