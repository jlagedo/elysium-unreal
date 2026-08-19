#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

// The map layer's log category, shared by AElysiumMapActor's sibling translation units under
// Map/. The one definition lives in ElysiumMapActor.cpp.
DECLARE_LOG_CATEGORY_EXTERN(LogElysium, Log, All);
