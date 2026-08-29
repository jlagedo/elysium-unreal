#include "Substrate/ElysiumStealthTables.h"

#include "ElysiumContentPaths.h"
#include "ElysiumKeyValues.h"

#include "Substrate/ElysiumVdataLoad.h"

namespace
{
	using ElysiumKeyValues::FKvNode;
	using ElysiumVdata::ReadVdata;
	using ElysiumVdata::RootBlock;
}

// `system/stealth.txt`

FElysiumStealthTables::FElysiumStealthTables()
{
	for (int32 i = 0; i < NumLight * NumStealth; ++i)
	{
		VisionScalar[i] = 1.f;
		ConeScalar[i] = 1.f;
	}
	for (int32 i = 0; i < NumStealth; ++i)
	{
		HearingDistUnits[i] = 0.f;
	}
	for (int32 i = 0; i < NumLight; ++i)
	{
		LightThreshold[i] = 0.f;
	}
}

const FElysiumStealthTables& FElysiumStealthTables::Neutral()
{
	static const FElysiumStealthTables Table;
	return Table;
}

float FElysiumStealthTables::Vision(int32 Light, int32 Stealth) const
{
	return VisionScalar[FMath::Clamp(Light, 0, NumLight - 1) * NumStealth
		+ FMath::Clamp(Stealth, 0, NumStealth - 1)];
}

float FElysiumStealthTables::Cone(int32 Light, int32 Stealth) const
{
	return ConeScalar[FMath::Clamp(Light, 0, NumLight - 1) * NumStealth
		+ FMath::Clamp(Stealth, 0, NumStealth - 1)];
}

float FElysiumStealthTables::HearingUnits(int32 Stealth) const
{
	return HearingDistUnits[FMath::Clamp(Stealth, 0, NumStealth - 1)];
}

float FElysiumStealthTables::Threshold(int32 Light) const
{
	return LightThreshold[FMath::Clamp(Light, 0, NumLight - 1)];
}

bool FElysiumStealthTables::Load(FString& OutError)
{
	*this = FElysiumStealthTables();

	static const TCHAR* Rel = TEXT("system/stealth.txt");
	TSharedPtr<FKvNode> Root;
	if (!ReadVdata(Rel, Root, OutError))
	{
		return false;
	}
	const FKvNode* Data = RootBlock(Root, TEXT("StealthData"), Rel, OutError);
	if (Data == nullptr)
	{
		return false;
	}

	// Every gap is collected rather than returned at the first one: a table that lost one row and a
	// table that lost a whole section are different authoring facts, and the diagnostic has to say
	// which. An unauthored cell keeps the neutral default the constructor wrote.
	TArray<FString> Gaps;

	// The two matrices, row-major `light * 11 + Sneaking`.
	auto LoadMatrix = [this, Data, &Gaps](const TCHAR* Section, float* Out) -> bool
	{
		const FKvNode* Node = Data->Child(Section);
		if (Node == nullptr)
		{
			Gaps.Add(FString::Printf(TEXT("no %s section"), Section));
			return false;
		}
		int32 Read = 0;
		for (int32 Light = 0; Light < NumLight; ++Light)
		{
			const FString RowKey = FString::Printf(TEXT("Light%d"), Light);
			const FKvNode* Row = Node->Child(*RowKey);
			if (Row == nullptr)
			{
				Gaps.Add(FString::Printf(TEXT("%s/%s missing"), Section, *RowKey));
				continue;
			}
			for (int32 Stealth = 0; Stealth < NumStealth; ++Stealth)
			{
				const FString Key = FString::Printf(TEXT("Stealth%d"), Stealth);
				if (!Row->Has(*Key))
				{
					Gaps.Add(FString::Printf(TEXT("%s/%s/%s missing"), Section, *RowKey, *Key));
					continue;
				}
				Out[Light * NumStealth + Stealth] = Row->Flt(*Key, Out[Light * NumStealth + Stealth]);
				++Read;
			}
		}
		AuthoredValues += Read;
		return Read == NumLight * NumStealth;
	};

	bVisionLoaded = LoadMatrix(TEXT("StealthVisionScalarTable"), VisionScalar);
	bConeLoaded = LoadMatrix(TEXT("StealthVisionConeScalarTable"), ConeScalar);

	// The two vectors. `StealthHearingDistTable` keys on `Sneaking` alone; `StealthLightRangeTable`
	// on the light row.
	auto LoadVector = [this, Data, &Gaps](const TCHAR* Section, const TCHAR* Prefix, float* Out,
		int32 Count) -> bool
	{
		const FKvNode* Node = Data->Child(Section);
		if (Node == nullptr)
		{
			Gaps.Add(FString::Printf(TEXT("no %s section"), Section));
			return false;
		}
		int32 Read = 0;
		for (int32 i = 0; i < Count; ++i)
		{
			const FString Key = FString::Printf(TEXT("%s%d"), Prefix, i);
			if (!Node->Has(*Key))
			{
				Gaps.Add(FString::Printf(TEXT("%s/%s missing"), Section, *Key));
				continue;
			}
			Out[i] = Node->Flt(*Key, Out[i]);
			++Read;
		}
		AuthoredValues += Read;
		return Read == Count;
	};

	bHearingLoaded = LoadVector(TEXT("StealthHearingDistTable"), TEXT("Stealth"),
		HearingDistUnits, NumStealth);
	bThresholdsLoaded = LoadVector(TEXT("StealthLightRangeTable"), TEXT("Light"),
		LightThreshold, NumLight);

	if (!Gaps.IsEmpty())
	{
		// At most six named gaps in the message: a file that lost a whole section would otherwise
		// print 121 lines of the same fact.
		const int32 Shown = FMath::Min(Gaps.Num(), 6);
		OutError = FString::Printf(TEXT("%s: %d gap(s) — %s%s"),
			*FElysiumContentPaths::VdataFile(Rel), Gaps.Num(),
			*FString::Join(TArrayView<const FString>(Gaps.GetData(), Shown), TEXT("; ")),
			Gaps.Num() > Shown ? TEXT(" …") : TEXT(""));
		return false;
	}
	OutError.Reset();
	return true;
}
