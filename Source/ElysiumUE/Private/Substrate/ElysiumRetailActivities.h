// Retail's activity enum — `ACT_*` name <-> value — as `ActivityList_RegisterSharedActivities`
// (`vampire.dll 0x104126e0`) registers it. The table is generated
// (`uv run elysium research gen_activity_enum`, `ElysiumRetailActivities.cpp`);
// `docs/vtmb/activity_enum.md` is the prose over it.
//
// The kernel's activity words (`m_Activity`, `m_IdealActivity`, a task's activity operand once
// translated) are these values; the corpus's schedule text interns activity NAMES under its own
// ids, so a name crossing between the two goes through `ValueOf`.

#pragma once

#include "CoreMinimal.h"

namespace ElysiumRetailActivities
{
	/** `ActivityList_NameForIndex` (`0x10412550`): the name the SHARED registrations
	 *  (`0x104123a0`) put in the name table for a value, or null -- a grapple registration
	 *  (`0x10412590`) stores its value alone and names nothing. */
	const TCHAR* NameOf(int32 Value);

	/** Not retail's: the name ANY registration pushed for a value, grapple rows included. The
	 *  sequence bridge's clip key (a named modernization), so a grapple activity resolves by id. */
	const TCHAR* RegistrationNameOf(int32 Value);

	/** `ActivityList_IndexForName` (`0x10412520`) over the same shared-only table: the value, or -1
	 *  (a grapple name answers -1). The fold is the table comparator's, unproven from the image. */
	int32 ValueOf(const FString& Name);

	/** `IsGrappleActivity` (`0x104126a0`). */
	bool IsGrapple(int32 Value);

	/** How many registrations the table carries (4,460). */
	int32 Num();
}
