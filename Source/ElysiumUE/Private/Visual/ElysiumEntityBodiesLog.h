#pragma once

#include "Logging/LogMacros.h"

// The one log category the body factory writes on. It is declared here rather than defined
// per-file because UElysiumEntityBodies is split across `ElysiumEntityBodies.cpp`,
// `ElysiumEntityBodiesProps.cpp` and `ElysiumEntityBodiesLab.cpp`, and one category is what makes
// a single `LogElysiumBodies` filter show the whole factory's story. `ElysiumEntityBodies.cpp`
// owns the definition.
DECLARE_LOG_CATEGORY_EXTERN(LogElysiumBodies, Log, All);
