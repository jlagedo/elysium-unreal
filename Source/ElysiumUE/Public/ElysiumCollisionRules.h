#pragma once

#include "CoreMinimal.h"

// Stateless retail rules: arguments are collision-group words, not Unreal channels.
namespace ElysiumCollisionRules
{
	// CSharedGameRules::vfunc0, vampire.dll 0x1042df70.
	ELYSIUMUE_API bool SharedShouldCollide(int32 GroupA, int32 GroupB);
	// CHalfLife2 slot 77 -> CHLSharedGameRules::vfunc0, vampire.dll 0x1042c0c0.
	ELYSIUMUE_API bool ShouldCollide(int32 GroupA, int32 GroupB);
}
