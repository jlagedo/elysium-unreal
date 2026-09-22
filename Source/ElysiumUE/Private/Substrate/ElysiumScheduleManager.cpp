#include "Substrate/ElysiumScheduleManager.h"

void FElysiumScheduleManager::Add(FElysiumScheduleProgram&& Program)
{
	const FString Folded = Program.Name.ToLower();
	const int32 GlobalId = Program.GlobalId;

	const int32 Index = Store.Add(MoveTemp(Program));

	// Newest wins, which is what linking at the head of retail's list does.
	if (int32* Existing = ByName.Find(Folded))
	{
		++NumShadowed;
		*Existing = Index;
	}
	else
	{
		ByName.Add(Folded, Index);
	}

	if (GlobalId != INDEX_NONE)
	{
		ById.Add(GlobalId, Index);
	}
}

void FElysiumScheduleManager::AddAll(FElysiumScheduleParseResult&& Result)
{
	for (FElysiumScheduleProgram& Program : Result.Programs)
	{
		Add(MoveTemp(Program));
	}
	Result.Programs.Reset();
}

const FElysiumScheduleProgram* FElysiumScheduleManager::FindByName(const FString& Name) const
{
	const int32* Index = ByName.Find(Name.ToLower());
	return Index != nullptr ? &Store[*Index] : nullptr;
}

const FElysiumScheduleProgram* FElysiumScheduleManager::FindById(int32 GlobalId) const
{
	const int32* Index = ById.Find(GlobalId);
	return Index != nullptr ? &Store[*Index] : nullptr;
}

void FElysiumScheduleManager::Reset()
{
	Store.Reset();
	ByName.Reset();
	ById.Reset();
	NumShadowed = 0;
}
