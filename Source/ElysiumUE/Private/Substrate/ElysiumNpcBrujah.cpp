#include "Substrate/ElysiumNpcBrujah.h"


// Slot 546: `0x10367a10`, the class's own schedule id space.
const TCHAR* FElysiumNpcBrujah::SquadSlotName(int32 SlotEn)
{
	// The class's `CAI_ClassScheduleIdSpace` `0x1093a788`, left empty by `0x102ea090(isRoot = false)`:
	// `SlotEn` translates to -1 and names `<<null>>`.
	static constexpr FSquadSlotSpecies IdSpace = {
		TEXT("CNPC_VBrujah"), TEXT("0x10367a10"), TEXT("0x1093a788") };
	return GlobalSquadSlotName(SquadSlotLocalToGlobal(&IdSpace, SlotEn));
}
