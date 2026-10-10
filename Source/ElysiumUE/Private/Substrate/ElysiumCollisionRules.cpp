#include "ElysiumCollisionRules.h"

bool ElysiumCollisionRules::SharedShouldCollide(int32 GroupA, int32 GroupB)
{
	// vampire.dll 0x1042df70: signed ordering, including negative/unrecognised groups.
	if (GroupB < GroupA)
	{
		Swap(GroupA, GroupB);
	}
	if (GroupA == 8 || GroupB == 8) return false;
	if (GroupA == 1) return false;
	if (GroupA == 4 && GroupB == 4) return false;
	return true;
}

bool ElysiumCollisionRules::ShouldCollide(int32 GroupA, int32 GroupB)
{
	// Actual single-player dispatch: CWorld::Precache 0x1023c020 -> InstallGameRules
	// 0x10352f70 -> ctor 0x10355740, CHalfLife2 table 0x104a308c slot 77 0x10356230
	// -> getter 0x1042c0a0 -> singleton 0x109eed4c -> CHLSharedGameRules 0x1042c0c0.
	// The function reads no singleton state. Remap each argument BEFORE signed ordering.
	if (GroupA == 6) GroupA = 3;
	if (GroupB == 6) GroupB = 3;
	if (GroupB < GroupA)
	{
		Swap(GroupA, GroupB);
	}
	if (GroupA == 15)
	{
		if (GroupB == 15) return false;
	}
	else if (GroupA == 13 && GroupB == 13) return false;
	if (GroupB == 17 && (GroupA == 3 || GroupA == 16 || GroupA == 17)) return false;
	if (GroupA == 18 && GroupB == 18) return false;
	return SharedShouldCollide(GroupA, GroupB); // 0x1042df70, inlined by retail 0x1042c0c0
}
