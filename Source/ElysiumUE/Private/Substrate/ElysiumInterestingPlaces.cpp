#include "Substrate/ElysiumInterestingPlaces.h"

#include "ElysiumContentPaths.h"
#include "ElysiumKeyValues.h"

#include "Misc/FileHelper.h"

namespace
{
	void ReadWeighted(const ElysiumKeyValues::FKvNode* Node, TArray<FElysiumWeightedName>& Out)
	{
		if (!Node)
		{
			return;
		}
		for (const TPair<FString, FString>& Pair : Node->Pairs)
		{
			if (!Pair.Key.IsEmpty())
			{
				Out.Add({ Pair.Key, FMath::Max(0.0f, FCString::Atof(*Pair.Value)) });
			}
		}
	}
}

int32 ElysiumInterestingPlaces::PickHighestRatedCandidate(
	TConstArrayView<int32> Ratings, FRandomStream& Random)
{
	for (int32 Rating = 5; Rating >= 0; --Rating)
	{
		int32 Count = 0;
		for (const int32 CandidateRating : Ratings)
		{
			Count += CandidateRating == Rating ? 1 : 0;
		}
		if (Count == 0)
		{
			continue;
		}

		int32 Pick = Random.RandRange(0, Count - 1);
		for (int32 CandidateIndex = 0; CandidateIndex < Ratings.Num(); ++CandidateIndex)
		{
			if (Ratings[CandidateIndex] == Rating && Pick-- == 0)
			{
				return CandidateIndex;
			}
		}
	}
	return INDEX_NONE;
}

bool FElysiumInterestingPlaceType::Accepts(const FString& ClassName,
	const FString& StatTemplate) const
{
	if (AcceptedClasses.IsEmpty())
	{
		return true;
	}
	for (const FElysiumWeightedName& Accepted : AcceptedClasses)
	{
		if (Accepted.Name.Equals(ClassName, ESearchCase::IgnoreCase)
			|| (!StatTemplate.IsEmpty()
				&& Accepted.Name.Equals(StatTemplate, ESearchCase::IgnoreCase)))
		{
			return true;
		}
	}
	return false;
}

FString FElysiumInterestingPlaceType::PickActivity(
	const TArray<FElysiumWeightedName>& Choices, uint32 Seed) const
{
	float Total = 0.0f;
	for (const FElysiumWeightedName& Choice : Choices)
	{
		Total += Choice.Weight;
	}
	if (Choices.IsEmpty() || Total <= 0.0f)
	{
		return FString();
	}
	float Pick = (static_cast<float>(Seed) / static_cast<float>(MAX_uint32)) * Total;
	for (const FElysiumWeightedName& Choice : Choices)
	{
		Pick -= Choice.Weight;
		if (Pick <= 0.0f)
		{
			return Choice.Name;
		}
	}
	return Choices.Last().Name;
}

bool FElysiumInterestingPlaceTable::Load(FString& OutError)
{
	Rows.Reset();
	const FString Path = FElysiumContentPaths::VdataFile(
		TEXT("system/interestingplacetypelist.txt"));
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		OutError = FString::Printf(TEXT("not found: %s"), *Path);
		return false;
	}
	const TSharedPtr<ElysiumKeyValues::FKvNode> Root = ElysiumKeyValues::ParseText(Text);
	const ElysiumKeyValues::FKvNode* Table = Root.IsValid()
		? Root->Child(TEXT("InterestingPlaceTypeList")) : nullptr;
	if (!Table)
	{
		OutError = FString::Printf(TEXT("no InterestingPlaceTypeList block in %s"), *Path);
		return false;
	}

	for (const TPair<FString, TSharedPtr<ElysiumKeyValues::FKvNode>>& Child : Table->Kids)
	{
		if (Child.Key != TEXT("interestingplacetype") || !Child.Value)
		{
			continue;
		}
		const ElysiumKeyValues::FKvNode& Node = *Child.Value;
		const ElysiumKeyValues::FKvNode* Identifiers = Node.Child(TEXT("Identifiers"));
		FElysiumInterestingPlaceType Row;
		Row.Name = Identifiers ? Identifiers->Str(TEXT("Name"), FString()) : FString();
		if (Row.Name.IsEmpty())
		{
			continue;
		}
		ReadWeighted(Node.Child(TEXT("Into_Activities")), Row.IntoActivities);
		ReadWeighted(Node.Child(TEXT("Activities")), Row.Activities);
		ReadWeighted(Node.Child(TEXT("Outof_Activities")), Row.OutOfActivities);
		ReadWeighted(Node.Child(TEXT("AcceptedClasses")), Row.AcceptedClasses);
		Rows.Add(Row.Name.ToLower(), MoveTemp(Row));
	}
	if (Rows.IsEmpty())
	{
		OutError = FString::Printf(TEXT("InterestingPlaceTypeList in %s has no rows"), *Path);
		return false;
	}
	return true;
}

const FElysiumInterestingPlaceType* FElysiumInterestingPlaceTable::Find(const FString& Name) const
{
	return Rows.Find(Name.ToLower());
}
