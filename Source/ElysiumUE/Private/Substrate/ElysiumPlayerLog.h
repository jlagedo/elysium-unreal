#pragma once

#include "Logging/LogMacros.h"

// The one log category the player/character chain writes on. It is declared here rather than
// defined per-file because the chain is split across `ElysiumAnimatingImpl`,
// `ElysiumCombatCharacter`, `ElysiumPlayerEntity` and `ElysiumDisciplines`, and one category is
// what makes a single `LogElysiumPlayer` filter show a whole character's story.
// `ElysiumPlayerClasses.cpp` — the registration site — owns the definition.
DECLARE_LOG_CATEGORY_EXTERN(LogElysiumPlayer, Log, All);
