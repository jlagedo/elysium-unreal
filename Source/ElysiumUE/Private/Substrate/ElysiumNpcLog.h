#pragma once

#include "Logging/LogMacros.h"

// The one log category the `npc_*` family writes on. It is declared here rather than defined
// per-file because the family is split across `ElysiumInterestingPlace`, `ElysiumScriptedCharacter`,
// `ElysiumNpc`, `ElysiumNpcSenses` and `ElysiumNpcMaker`, and one category is what makes a single
// `LogElysiumNpcEnt` filter show a whole NPC's story. `ElysiumNpcClasses.cpp` — the registration
// site — owns the definition.
DECLARE_LOG_CATEGORY_EXTERN(LogElysiumNpcEnt, Log, All);
