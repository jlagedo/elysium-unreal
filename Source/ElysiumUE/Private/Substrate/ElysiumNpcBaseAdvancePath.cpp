// `CAI_Navigator::AdvancePath` (`0x102f0400`) — declared in `ElysiumNpcBaseMotor.inl` (included inside
// `class FElysiumNpcBase`). 0018 story 5: the corner chain's per-waypoint arms land here (lane E).

#include "Substrate/ElysiumNpcBase.h"

bool FElysiumNpcBase::NavAdvancePath()
{
	// SEAM (0018/5 wave 1): no caller yet. The port's route has no waypoint list of its own, so no
	// head stands after the one reached; the caller treats that as the end of the route.
	Navigator.bHasHeadWaypoint = false;                                      // 0x1030ba90 pop
	return false;
}
