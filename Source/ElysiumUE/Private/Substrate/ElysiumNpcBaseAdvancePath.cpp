// `CAI_Navigator::AdvancePath` (`0x102f0400`) — declared in `ElysiumNpcBaseMotor.inl` (included inside
// `class FElysiumNpcBase`). 0018 story 5: the corner chain's per-waypoint arms land here (lane E).

#include "Substrate/ElysiumNpcBase.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumVariant.h"
#include "Substrate/ElysiumNpc.h"

namespace
{
	// `GOALTYPE_PATHCORNER`: the goal type whose waypoints carry flag `0x02` (`bits_WP_TO_PATHCORNER`,
	// stamped by `DoFindPath`'s type-3 arm, `0x102f2330`).
	constexpr int32 GAdvanceGoalTypePathCorner = 3;
}

bool FElysiumNpcBase::NavAdvancePath()
{
	// `0x102f0400`, arm by arm, in retail's order. Retail reads the head waypoint (`path+0x24`)
	// unconditionally; a navigator with no head has nothing to advance (crash guard).
	if (!Navigator.bHasHeadWaypoint)
	{
		return false;
	}
	FElysiumNpc* Troika = AsNpc();                                          // npc+0x98

	// (a) Waypoint flag `0x02`: the corner at `wp+0x20` is handed the input `"InPass"` (`0x1057b630`,
	// vslot `0x1d8`) with the walking NPC as activator (`0x102f05b0`) and the corner as caller, so its
	// `OnPass` output fires with the NPC as activator. Runs for the GOAL corner too: `TaskMovementComplete`
	// (`0x10273ec0`) calls this after `TaskComplete`. A handle that no longer resolves skips the input.
	// The corner's `speed` is copied by the re-find (arm c), not here; `wait` is never read.
	const bool bCornerWaypoint = Navigator.GetGoalType() == GAdvanceGoalTypePathCorner;
	if (bCornerWaypoint && World != nullptr)
	{
		const FElysiumEntityHandle HeadHandle = Troika != nullptr && Troika->NavHeadCorner.IsSet()
			? Troika->NavHeadCorner : BaseScheduleHost.GoalEnt;
		if (FElysiumEntity* Corner = World->Resolve(HeadHandle))            // 0x102f0410 wp+0x20
		{
			World->AcceptInput(Corner->Handle, FName(TEXT("InPass")), FElysiumVariant::Void(),
				Handle, Corner->Handle);                                    // 0x102f0451 slot 0x1d8
		}
	}

	// The goal waypoint (`0x1030bd50`, `wp+0x28 & 8`): nothing else runs and nothing is popped. The
	// route's end belongs to `OnNavComplete` / `TaskMovementComplete`.
	if (Navigator.bHeadIsGoal)                                              // 0x102f0456 0x1030bd50
	{
		return true;
	}

	// (b) `npc+0x98` non-null: `0x102a0bc0(sub, wp)`, then the door arm. Neither is reached by a
	// corner waypoint (flag `0x02` only): `0x102a0bc0` acts on waypoint flag `0x04` for a type-8 goal,
	// and flag `0x10` starts the door transaction `0x10298800`. SEAM, 0018 story 7's: the port's route
	// has no waypoint list to carry those flags, so no arm here reads or sets them.

	// (c) Flag `0x02` with a next waypoint: `m_pGoalEnt (+0x5de8) = m_pGoalEnt->GetNextTarget()` (slot
	// 172, the CURRENT `m_pGoalEnt`'s next, not the waypoint's entity), then `DoFindPath` re-lays the
	// chain from it: the only re-find in the chain (`0x102f04de..0x102f04fb`). Its answer is dropped:
	// a null next corner leaves the path emptied and nothing failed.
	if (bCornerWaypoint)
	{
		FElysiumEntity* Current = World != nullptr ? World->Resolve(BaseScheduleHost.GoalEnt) : nullptr;
		FElysiumEntity* Next = Current != nullptr ? Current->GetNextTarget() : nullptr;   // slot 172
		BaseScheduleHost.GoalEnt = Next != nullptr ? Next->Handle : FElysiumEntityHandle::Invalid();
		// The leg to the corner just passed ended with the arrival.
		bMoveIssued = false;
		if (Troika != nullptr)
		{
			(void)Troika->NavFindPathCorners();                             // 0x102f04fb 0x102f2330
		}
		else
		{
			// A base-only NPC has no chain builder here (the Troika line's, `NavFindPathCorners`): the
			// route ends with the corner passed.
			Navigator.bHasHeadWaypoint = false;
			Navigator.bHeadIsGoal = false;
		}
		return Navigator.bHasHeadWaypoint;
	}

	// The pop (`0x1030ba90`). The port's route has no waypoint list of its own, so no head stands after
	// the one reached; the caller treats that as the end of the route. (Retail's flag `0x04` node pop
	// stores `path+0x44 = wp+0x10`, and a pop with no next waypoint prints `"ERROR: Force end of route
	// without goal"` and sets the goal bit on the last: both need the waypoint list.)
	Navigator.bHasHeadWaypoint = false;
	Navigator.bHeadIsGoal = false;
	return false;
}
