#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntityHandle.h"

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

	// `CStealthKillRules::FindVictim` `0x101be1f0`. Per-frame cache on this table (retail `+0xb4` /
	// `+0x134`); a same-`Now` caller returns the cached NPC without re-tracing.
	FElysiumNpc* FindVictim(FElysiumPlayer& Player) const;

private:
	mutable FElysiumEntityHandle CachedVictim;
	mutable double CachedAt = -1.0;
};
