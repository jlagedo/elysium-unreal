// The identity rules every schedule, task and condition id in this runtime obeys.
//
// Retail does not carry a type for these: an id is a bare `int`, and local is told from global by
// MAGNITUDE. Each of the three global namespaces seeds its id counter at 1,000,000,000, so an id at
// or above that is a GLOBAL id and anything below it is a class-LOCAL one. Exactly two retail
// bodies test it -- `CAI_BaseNPC::SetSchedule 0x10280de0` and `0x102cc260` -- and this port tests it
// in the same two places.

#pragma once

#include "CoreMinimal.h"

namespace ElysiumScheduleId
{
	/** `0x3b9aca00`. The value every global namespace's id counter is seeded with, and therefore
	 *  the boundary between a class-local id and a global one.
	 *
	 *  It is a SEED, not an offset applied on the way out: `CAI_LocalIdSpace::Init 0x102ea0e0`
	 *  reads its space's global base off the namespace's next-free counter (`0x102ea070` returns
	 *  `namespace[+0x04]`), and `0x102e9fe0` raises that counter to `globalId + 1` on every
	 *  insertion. So each class's space begins where the previous one ended and no two classes
	 *  share a global id, which is what makes a global id a usable identity when schedule local
	 *  `0x156` names six different programs in six different tables. */
	inline constexpr int32 GlobalBase = 1'000'000'000;

	/** Retail's "no schedule". `CAI_Schedule*` null, and what a selector answers with no opinion. */
	inline constexpr int32 None = 0;

	/** The sentinel `SetSchedule(int)` still routes through the class's space rather than stamping
	 *  (`0x10280de0`: `if (id < 0x3b9aca00 || id == -1) id = ScheduleLocalToGlobal(...)`). */
	inline constexpr int32 Sentinel = INDEX_NONE;

	/** Retail's "this space holds no ids" marker in `m_localBase`, tested by name at `0x102ea2d0`
	 *  and `0x102ea280`. `Init` leaves it here on a space that has a parent and `0` on a root. */
	inline constexpr int32 EmptyLocalBase = 9999;

	inline bool IsGlobal(int32 Id) { return Id >= GlobalBase; }
	inline bool IsSet(int32 Id) { return Id != None; }
}
