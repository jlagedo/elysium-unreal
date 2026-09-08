#pragma once

#include "CoreMinimal.h"

class FElysiumPlayer;
class FElysiumNpc;
namespace ElysiumKeyValues { struct FKvNode; }

// CStealthKillRules 0x101bedb0/0x101bef50. Shared by hearing and the paired kill.
struct FElysiumStealthKillRules
{
	float DeafArcDegrees[20] = {};
	int32 FeatMin = 1;
	int32 FeatMax = 10;
	float HearingMin = 0.f;
	float HearingMax = 3.f;
	float DistanceMaxUnits = 70.f;
	bool Load(FString& OutError);
	bool Parse(const ElysiumKeyValues::FKvNode& Root, FString& OutError);
	float ArcDot(int32 CombatFeat) const;
	float MinDepthUnits(int32 Sneaking, float Hearing) const;
	bool InDeafArc(const FElysiumPlayer& Player, const FElysiumNpc& Victim) const;
	bool InDeafZone(const FElysiumPlayer& Player, const FElysiumNpc& Victim) const;
};
