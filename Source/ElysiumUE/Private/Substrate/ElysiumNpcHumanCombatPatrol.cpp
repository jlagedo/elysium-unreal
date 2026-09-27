#include "Substrate/ElysiumNpcHumanCombatPatrol.h"


// Slot 546: `0x103878b0`, the class's own schedule id space.
const TCHAR* FElysiumNpcHumanCombatPatrol::SquadSlotName(int32 SlotEn)
{
	// The class's `CAI_ClassScheduleIdSpace` `0x1093b580`, left empty by `0x102ea090(isRoot = false)`:
	// `SlotEn` translates to -1 and names `<<null>>`.
	static constexpr FSquadSlotSpecies IdSpace = {
		TEXT("CNPC_VHumanCombatPatrol"), TEXT("0x103878b0"), TEXT("0x1093b580") };
	return GlobalSquadSlotName(SquadSlotLocalToGlobal(&IdSpace, SlotEn));
}
