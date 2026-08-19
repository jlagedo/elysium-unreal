#pragma once

#include "CoreMinimal.h"

// The one log category every FElysiumEntityWorld translation unit writes to. Defined once in
// ElysiumEntityWorld.cpp.
DECLARE_LOG_CATEGORY_EXTERN(LogElysiumWorld, Log, All);

namespace ElysiumEntityWorldShared
{
	// The `!self` runtime-reference target literal, shared by the chokepoints' target resolution
	// and dialogue's EndDialog enqueue. Defined in ElysiumEntityWorld.cpp.
	extern const TCHAR* const GSelfTarget;
}
