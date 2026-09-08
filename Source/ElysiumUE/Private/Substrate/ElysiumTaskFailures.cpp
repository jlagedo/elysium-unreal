#include "Substrate/ElysiumSchedule.h"

const TCHAR* ElysiumTaskFailureName(int32 Reason)
{
	// vampire.dll 0x106152b0, read through the PE section map (0x2a is the terminal sentinel).
	static const TCHAR* Names[] = {
		TEXT("No failure"), TEXT("No Target"), TEXT("Weapon owned by someone else"),
		TEXT("Weapon doesn't exist"), TEXT("No hint node"), TEXT("Schedule not found"),
		TEXT("Don't have an enemy"), TEXT("Found no backaway node"), TEXT("Couldn't find cover"),
		TEXT("Couldn't find flank"), TEXT("FAIL_CODE_10"), TEXT("Couldn't find shoot position"),
		TEXT("Don't have a route"), TEXT("Don't have a route: no goal"),
		TEXT("Don't have a route: blocked"), TEXT("Don't have a route: illegal move"),
		TEXT("Couldn't walk to target"), TEXT("Node already locked"), TEXT("No sound present"),
		TEXT("No scent present"), TEXT("FAIL_CODE_20"), TEXT("Bad activity"),
		TEXT("No goal entity"), TEXT("No player"), TEXT("Can't reach any nodes"),
		TEXT("No AI Network to Use"), TEXT("Bad position to Target"),
		TEXT("Route Destination No Longer Valid"), TEXT("Stuck on top of something"),
		TEXT("No patrol path.  Use SetupPatrolPath in engine, or the input SetupPatrolType in World Craft."),
		TEXT("FAIL_CODE_30"), TEXT("No weapon to choose"), TEXT("Failed to create hunt patrol list."),
		TEXT("No stored unknown entity seen"), TEXT("No interesting places were available to go to."),
		TEXT("Lost our interesting place."), TEXT("Could not knock out NPC"),
		TEXT("NPC had no kick prop."), TEXT("NPC had no shoot at hint."),
		TEXT("NPC had no detected attacker."), TEXT("FAIL_CODE_40"), TEXT("NPC had no follower boss")
	};
	return Reason >= 0 && Reason < UE_ARRAY_COUNT(Names) ? Names[Reason] : TEXT("Unknown failure");
}
