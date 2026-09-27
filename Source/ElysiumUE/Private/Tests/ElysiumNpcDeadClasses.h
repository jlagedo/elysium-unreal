#pragma once

// The 21 retail NPC classes with no instance (`docs/vtmb/npc-ai/population.md` § "NPC classes with
// no instance"; `research/tooling/ghidra/driver/kernel_classes.tsv` rows marked
// `dead-census-retained`). 0019 story 5 step 1 deleted their port code but kept their census rows,
// so a coverage test that walks the census for arms must skip exactly these and still fail loudly
// for every live class.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/CString.h"

namespace ElysiumNpcDeadClasses
{
	inline constexpr const TCHAR* Names[] =
	{
		TEXT("CAI_BaseHumanoid"), TEXT("CAI_ExpressiveNPC"), TEXT("CGeneric_NPC"),
		TEXT("CGeneric_NPC_bathack"), TEXT("CGenericNPC"), TEXT("CGenericSabbat_NPC"),
		TEXT("CNPC_Bullseye"), TEXT("CNPC_Crow"), TEXT("CNPC_VBatSwarm"), TEXT("CNPC_VCombatman"),
		TEXT("CNPC_VGangrel"), TEXT("CNPC_VMalkavian"), TEXT("CNPC_VMoleman"),
		TEXT("CNPC_VNosferatu"), TEXT("CNPC_VSheriffSwarm"), TEXT("CNPC_VStalker"),
		TEXT("CNPC_VTest"), TEXT("CNPC_VToreador"), TEXT("CNPC_VTremere"), TEXT("CNPC_VVentrue"),
		TEXT("CScriptedTarget"),
	};
	static_assert(UE_ARRAY_COUNT(Names) == 21, "population.md lists 21 classes with no instance");

	/** Whether the census class has no instance and so no port code since 0019 story 5 step 1. */
	inline bool Contains(const TCHAR* RetailClass)
	{
		if (RetailClass == nullptr)
		{
			return false;
		}
		for (const TCHAR* Name : Names)
		{
			if (FCString::Strcmp(Name, RetailClass) == 0)
			{
				return true;
			}
		}
		return false;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
