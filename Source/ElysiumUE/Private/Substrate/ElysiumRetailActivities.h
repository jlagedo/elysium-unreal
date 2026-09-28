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
	/** `ActivityList_NameForIndex` (`0x10412550`): the name registered for a value, or null. */
	const TCHAR* NameOf(int32 Value);

	/** `ActivityList_IndexForName` (`0x10412520`): the value, case-folded, or -1. */
	int32 ValueOf(const FString& Name);

	/** `IsGrappleActivity` (`0x104126a0`). */
	bool IsGrapple(int32 Value);

	/** How many registrations the table carries (4,460). */
	int32 Num();
}
