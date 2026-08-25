#pragma once

// Shared scraper for `FElysiumEntity::GetDebugState`'s "Key Value" rows — a math_counter's
// "Value", a func_rotating's "Angle", an NPC's "Body owner". The class that actually populates a
// given row is file-local to its own .cpp (`ElysiumLogicClasses.cpp`, `ElysiumFuncRotating.cpp`,
// `ElysiumNpcMind.cpp`, ...), so this base virtual plus a named key is the one seam every
// Substrate suite reads a live entity's derived state back through instead of reaching for a
// concrete class it cannot name.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntity.h"

namespace ElysiumEntityDebugTest
{
	// The named row's value, or Fallback when Entity is null or carries no such row.
	inline FString Row(const FElysiumEntity* Entity, const TCHAR* Key,
		const FString& Fallback = FString())
	{
		if (Entity == nullptr)
		{
			return Fallback;
		}
		TArray<TPair<FString, FString>> State;
		Entity->GetDebugState(State);
		for (const TPair<FString, FString>& StateRow : State)
		{
			if (StateRow.Key == Key)
			{
				return StateRow.Value;
			}
		}
		return Fallback;
	}

	// The common numeric case: a row that renders a float as text. Same null/missing fallback as
	// `Row`.
	inline float RowAsFloat(const FElysiumEntity* Entity, const TCHAR* Key, float Fallback = -1.f)
	{
		if (Entity == nullptr)
		{
			return Fallback;
		}
		TArray<TPair<FString, FString>> State;
		Entity->GetDebugState(State);
		for (const TPair<FString, FString>& StateRow : State)
		{
			if (StateRow.Key == Key)
			{
				return FCString::Atof(*StateRow.Value);
			}
		}
		return Fallback;
	}

	// The single most common case across these suites: a math_counter's live "Value" row. -1
	// means "no such counter, or no such row" — a value no Add(1) chain here can produce.
	inline float CounterValue(const FElysiumEntity* Entity, float Fallback = -1.f)
	{
		return RowAsFloat(Entity, TEXT("Value"), Fallback);
	}
}

#endif   // WITH_DEV_AUTOMATION_TESTS
