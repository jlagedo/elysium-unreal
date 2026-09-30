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

	// (b) `npc+0x98` non-null: `0x102a0bc0(sub, wp)`, its answer ignored, then the door arm. The wait
	// test acts on waypoint flag `0x04` for a type-8 goal: on a red crosswalk it latches `AT_CROSSWALK`
	// and the link (`0x102a0b90`), and the path still pops below -- the pedestrian walks on toward the
	// far curb until the next think's `CROSSWALK_DONTWALK` selects schedule `0x102`, whose
	// `PAUSE_MOVING` pauses the path. The waypoint's words are the pedestrian legs' head
	// (`FElysiumNpc::PedestrianHeadWaypoint`); a route with no legs hands a waypoint that is no node,
	// which the test's second gate refuses, as a corner or goal waypoint of retail's does.
	if (Troika != nullptr)
	{
		FElysiumNpc::FDialogPedWaypoint Head;
		Troika->PedestrianHeadWaypoint(Head);
		(void)Troika->ResolvePedestrianPathNode(&Head);                     // 0x102f046a 0x102a0bc0
		// The door arm (waypoint flag `0x10`: `0x1027f550` then `0x10298800`), the doors lane's
		// (0018/7): the head leg a slot-531 splice laid at a door's stand point.
		Troika->NavAdvanceDoorWaypoint();                                   // 0x102f047f..0x102f04c9
	}

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

	// The pop (`0x1030ba90`). A pedestrian route's curb legs are the one waypoint list the port keeps:
	// the reached leg is popped, its node stored as `path+0x44`, and the next leg issued.
	if (Troika != nullptr && Troika->PedestrianLegs.Num() > 1)
	{
		return Troika->NavPedestrianAdvance();
	}
	// Any other route has no waypoint list of its own, so no head stands after the one reached; the
	// caller treats that as the end of the route. (A pop with no next waypoint prints `"ERROR: Force
	// end of route without goal"` and sets the goal bit on the last: that needs the list.)
	Navigator.bHasHeadWaypoint = false;
	Navigator.bHeadIsGoal = false;
	return false;
}
