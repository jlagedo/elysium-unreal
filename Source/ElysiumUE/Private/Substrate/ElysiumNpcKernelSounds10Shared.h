#pragma once

// Story 5 step 4: the file-scope locals of `ElysiumNpcKernelSounds10.cpp` that its staying bodies share with
// bodies moved to their species classes. Qualified at every use (`NpcKernelSounds10Shared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumRng.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumVariant.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"

namespace NpcKernelSounds10Shared
{
	inline const TCHAR* const GSounds10ConceptPain = TEXT("Pain");                         // 0x105d8c6c
	inline const TCHAR* const GSounds10ConceptExertHeavy = TEXT("Exert_Heavy");            // 0x1057a1a0
}
