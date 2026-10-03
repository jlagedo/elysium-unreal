#pragma once

#include "Logging/LogMacros.h"

// The one log category the `npc_*` family writes on. It is declared here rather than defined
// per-file because the family is split across `ElysiumInterestingPlace`, `ElysiumScriptedCharacter`,
// `ElysiumNpc`, `ElysiumNpcSenses` and `ElysiumNpcMaker`, and one category is what makes a single
// `LogElysiumNpcEnt` filter show a whole NPC's story. `ElysiumNpcClasses.cpp` — the registration
// site — owns the definition.
DECLARE_LOG_CATEGORY_EXTERN(LogElysiumNpcEnt, Log, All);

// Retail's one-NPC trace (`CBaseEntity::TraceMessage`, slot 18, and the `npc_task_text` prints),
// written through `FElysiumNpcBase::TraceMessage`. Its own category so the chain can be filtered
// apart from the family's other rows. Defined beside `LogElysiumNpcEnt` in `ElysiumNpcClasses.cpp`.
DECLARE_LOG_CATEGORY_EXTERN(LogElysiumNpcTrace, Log, All);
