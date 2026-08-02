#pragma once

#include "CoreMinimal.h"

struct FElysiumWeightedName
{
	FString Name;
	float Weight = 1.0f;
};

// One row of vdata/system/interestingplacetypelist.txt. This remains engine-neutral: the table
// describes who may use a place and which ACT_* vocabulary to request; Unreal supplies movement
// and clip playback through the world-service seam.
struct FElysiumInterestingPlaceType
{
	FString Name;
	TArray<FElysiumWeightedName> IntoActivities;
	TArray<FElysiumWeightedName> Activities;
	TArray<FElysiumWeightedName> OutOfActivities;
	TArray<FElysiumWeightedName> AcceptedClasses;

	bool Accepts(const FString& ClassName, const FString& StatTemplate) const;
	FString PickActivity(const TArray<FElysiumWeightedName>& Choices, uint32 Seed) const;
};

class FElysiumInterestingPlaceTable
{
public:
	bool Load(FString& OutError);
	const FElysiumInterestingPlaceType* Find(const FString& Name) const;
	int32 Num() const { return Rows.Num(); }

private:
	TMap<FString, FElysiumInterestingPlaceType> Rows;
};
