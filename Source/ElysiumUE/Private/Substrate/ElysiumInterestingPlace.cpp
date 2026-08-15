#include "Substrate/ElysiumInterestingPlace.h"

#include "Substrate/ElysiumNpcLog.h"

bool FElysiumInterestingPlace::IsAvailable() const
{
	return bEnabled && !IsInert() && Claimants.Num() < FMath::Max(1, MaxNpcs);
}

bool FElysiumInterestingPlace::Claim(const FElysiumEntityHandle& Npc)
{
	if (Claimants.Contains(Npc.Index))
	{
		return true;
	}
	if (!IsAvailable())
	{
		return false;
	}
	Claimants.Add(Npc.Index);
	return true;
}

bool FElysiumInterestingPlace::IsEnabledFor(const FElysiumEntityHandle& Npc) const
{
	return bEnabled && !IsInert() && Claimants.Contains(Npc.Index);
}

void FElysiumInterestingPlace::Arrived(const FElysiumEntityHandle& Npc)
{
	FireOutput(FName(TEXT("OnNPCArrived")), Npc);
}

void FElysiumInterestingPlace::Left(const FElysiumEntityHandle& Npc)
{
	FireOutput(FName(TEXT("OnNPCLeft")), Npc);
}

void FElysiumInterestingPlace::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	Out.Emplace(TEXT("Enabled"), bEnabled ? TEXT("yes") : TEXT("no"));
	Out.Emplace(TEXT("Type"), Type);
	Out.Emplace(TEXT("Occupancy"), FString::Printf(TEXT("%d/%d"), Claimants.Num(), FMath::Max(1, MaxNpcs)));
	Out.Emplace(TEXT("Group"), FString::FromInt(GroupId));
	Out.Emplace(TEXT("Rating"), FString::FromInt(Rating));
}

namespace ElysiumInterestingPlaces
{
	const FElysiumInterestingPlaceTable* Types()
	{
		static FElysiumInterestingPlaceTable Table;
		static bool bAttempted = false;
		static bool bLoaded = false;
		if (!bAttempted)
		{
			bAttempted = true;
			FString Error;
			bLoaded = Table.Load(Error);
			if (!bLoaded)
			{
				UE_LOG(LogElysiumNpcEnt, Warning, TEXT("interesting-place types unavailable: %s"), *Error);
			}
			else
			{
				UE_LOG(LogElysiumNpcEnt, Log, TEXT("loaded %d interesting-place types"), Table.Num());
			}
		}
		return bLoaded ? &Table : nullptr;
	}
}
