#include "ElysiumNavAreas.h"

UElysiumNavArea_Pedestrian::UElysiumNavArea_Pedestrian(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Cost 1, the default. The roadway is not cheaper ground; it is ground the pedestrian query
	// filter prefers, and that filter is story 5's. Pricing it here instead would make every
	// NPC prefer the road, which is the opposite of what retail's `0x2000` means.
	DefaultCost = 1.0f;
	DrawColor = FColor(60, 160, 220);
}

UElysiumNavArea_DoorCut::UElysiumNavArea_DoorCut(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// UNavArea_Null already means "not walkable"; the colour is the only thing worth stating, so
	// a door cut is distinguishable from any other null area in the navigation debug draw.
	DrawColor = FColor(200, 70, 70);
}
