#include "Substrate/ElysiumDisposition.h"

#include "ElysiumContentPaths.h"
#include "ElysiumKeyValues.h"

#include "Misc/FileHelper.h"

const TCHAR* FElysiumDispositionTable::NeutralName = TEXT("Neutral");

namespace
{
	// "Fidget Points" is authored as a bracketed triple — `[-1,-1,-1]`, `[0,2,0]`, `[7,5,9]`. It is
	// the only value in the table that is not a bare scalar, and an unparsed one has to leave the
	// caller's default (all -1, "pick a random cell") rather than three zeroes: 0 is a meaningful
	// cell value in this grid, so zeroing on a parse failure would silently author behaviour.
	void ParseFidgetPoints(const FString& Raw, int32 (&Out)[3])
	{
		FString Body = Raw;
		Body.TrimStartAndEndInline();
		Body.RemoveFromStart(TEXT("["));
		Body.RemoveFromEnd(TEXT("]"));
		TArray<FString> Parts;
		Body.ParseIntoArray(Parts, TEXT(","), /*InCullEmpty=*/true);
		if (Parts.Num() != 3)
		{
			return;
		}
		for (int32 i = 0; i < 3; ++i)
		{
			Parts[i].TrimStartAndEndInline();
			if (!Parts[i].IsNumeric())
			{
				return;
			}
		}
		for (int32 i = 0; i < 3; ++i)
		{
			Out[i] = FCString::Atoi(*Parts[i]);
		}
	}

	const FElysiumDisposition* FindExact(const TArray<FElysiumDisposition>& Rows,
		const FString& Name, int32 Level)
	{
		for (const FElysiumDisposition& Row : Rows)
		{
			if (Row.Level == Level && Row.Name.Equals(Name, ESearchCase::IgnoreCase))
			{
				return &Row;
			}
		}
		return nullptr;
	}

	const FElysiumDisposition* FindAtOrBelow(const TArray<FElysiumDisposition>& Rows,
		const FString& Name, int32 RequestedLevel)
	{
		for (int32 Level = FMath::Max(1, RequestedLevel); Level >= 1; --Level)
		{
			if (const FElysiumDisposition* Row = FindExact(Rows, Name, Level))
			{
				return Row;
			}
		}
		return nullptr;
	}

	void ApplyEyeOverrides(const ElysiumKeyValues::FKvNode& Eye, FElysiumEyeTargetTuning& Out)
	{
		if (Eye.Has(TEXT("Default Direction")))
		{
			Out.DefaultDirection = Eye.Int(TEXT("Default Direction"), Out.DefaultDirection);
		}
		if (Eye.Has(TEXT("Fidget Points")))
		{
			ParseFidgetPoints(Eye.Str(TEXT("Fidget Points"), FString()), Out.FidgetPoints);
		}
		if (Eye.Has(TEXT("Min Interval"))) { Out.MinInterval = Eye.Flt(TEXT("Min Interval"), Out.MinInterval); }
		if (Eye.Has(TEXT("Max Interval"))) { Out.MaxInterval = Eye.Flt(TEXT("Max Interval"), Out.MaxInterval); }
		if (Eye.Has(TEXT("Hold Min"))) { Out.HoldMin = Eye.Flt(TEXT("Hold Min"), Out.HoldMin); }
		if (Eye.Has(TEXT("Hold Max"))) { Out.HoldMax = Eye.Flt(TEXT("Hold Max"), Out.HoldMax); }
		if (Eye.Has(TEXT("Eye Turn Rate"))) { Out.TurnRate = Eye.Flt(TEXT("Eye Turn Rate"), Out.TurnRate); }
	}

	void ApplyOverrides(const FString& Name, const ElysiumKeyValues::FKvNode& Node,
		FElysiumDisposition& Out)
	{
		Out.Name = Name;
		if (Node.Has(TEXT("Animation Name")))
		{
			Out.AnimName = Node.Str(TEXT("Animation Name"), Out.AnimName);
		}
		if (Node.Has(TEXT("DispositionLevel")))
		{
			Out.Level = Node.Int(TEXT("DispositionLevel"), Out.Level);
		}
		if (Node.Has(TEXT("Talking Stance Change Threshold")))
		{
			Out.TalkingStanceChangeThreshold = Node.Flt(
				TEXT("Talking Stance Change Threshold"), Out.TalkingStanceChangeThreshold);
		}
		if (Node.Has(TEXT("Talking Stance Change Chance")))
		{
			Out.TalkingStanceChangeChance = Node.Int(
				TEXT("Talking Stance Change Chance"), Out.TalkingStanceChangeChance);
		}
		if (Node.Has(TEXT("Standing Fidget Chance")))
		{
			Out.StandingFidgetChance = Node.Int(TEXT("Standing Fidget Chance"), Out.StandingFidgetChance);
		}
		if (Node.Has(TEXT("Standing Stance Change Threshold")))
		{
			Out.StandingStanceChangeThreshold = Node.Flt(
				TEXT("Standing Stance Change Threshold"), Out.StandingStanceChangeThreshold);
		}
		if (Node.Has(TEXT("Standing stance Change Chance")))
		{
			Out.StandingStanceChangeChance = Node.Int(
				TEXT("Standing stance Change Chance"), Out.StandingStanceChangeChance);
		}
		if (Node.Has(TEXT("Min Blink Interval")) || Node.Has(TEXT("MinBlinkInterval")))
		{
			Out.MinBlinkInterval = Node.Flt(TEXT("Min Blink Interval"),
				Node.Flt(TEXT("MinBlinkInterval"), Out.MinBlinkInterval));
		}
		if (Node.Has(TEXT("Max Blink Interval")) || Node.Has(TEXT("MaxBlinkInterval")))
		{
			Out.MaxBlinkInterval = Node.Flt(TEXT("Max Blink Interval"),
				Node.Flt(TEXT("MaxBlinkInterval"), Out.MaxBlinkInterval));
		}
		if (Node.Has(TEXT("Eye Turn Rate")))
		{
			Out.EyeTurnRate = Node.Flt(TEXT("Eye Turn Rate"), Out.EyeTurnRate);
		}
		if (const ElysiumKeyValues::FKvNode* Expression = Node.Child(TEXT("DefaultExpression")))
		{
			if (Expression->Has(TEXT("Expression Name")))
			{
				Out.DefaultExpression = Expression->Str(TEXT("Expression Name"), Out.DefaultExpression);
			}
			if (Expression->Has(TEXT("Talking Expression")))
			{
				Out.TalkingExpression = Expression->Str(TEXT("Talking Expression"), Out.TalkingExpression);
			}
			if (Expression->Has(TEXT("Intensity")))
			{
				Out.ExpressionIntensity = Expression->Flt(TEXT("Intensity"), Out.ExpressionIntensity);
			}
		}
		if (const ElysiumKeyValues::FKvNode* Eye = Node.Child(TEXT("EyeTarget")))
		{
			ApplyEyeOverrides(*Eye, Out.EyeTarget);
		}
	}
}

bool FElysiumDispositionTable::Load(FString& OutError)
{
	const FString Path = FElysiumContentPaths::VdataFile(TEXT("system/dispositiontable.txt"));
	FString Raw;
	if (!FFileHelper::LoadFileToString(Raw, *Path))
	{
		OutError = FString::Printf(TEXT("not found: %s"), *Path);
		return false;
	}
	return ParseText(Raw, Path, OutError);
}

bool FElysiumDispositionTable::ParseText(const FString& Raw, const FString& Source,
	FString& OutError)
{
	Rows.Reset();
	OutError.Reset();
	const TSharedPtr<ElysiumKeyValues::FKvNode> Root = ElysiumKeyValues::ParseText(Raw);
	// One top-level `DispositionTable` block whose children are the dispositions.
	const ElysiumKeyValues::FKvNode* Table = Root.IsValid() ? Root->Child(TEXT("DispositionTable")) : nullptr;
	if (Table == nullptr)
	{
		OutError = FString::Printf(TEXT("no DispositionTable block in %s"), *Source);
		return false;
	}

	for (const TPair<FString, TSharedPtr<ElysiumKeyValues::FKvNode>>& Kid : Table->Kids)
	{
		const ElysiumKeyValues::FKvNode* N = Kid.Value.Get();
		if (N == nullptr)
		{
			continue;
		}
		// Retail constructs every record from Neutral, then lets CopyDataFrom replace that base with
		// an earlier named record before this block's own fields override it. This is why Joy L2 can
		// author only its fidget chance and expression while retaining Joy L1's animation and gaze.
		FElysiumDisposition Row;
		if (!Rows.IsEmpty())
		{
			Row = Rows[0];
		}
		if (const FString* CopyName = N->Value(TEXT("CopyDataFrom")))
		{
			if (const FElysiumDisposition* Copy = FindExact(Rows, *CopyName, 1))
			{
				Row = *Copy;
			}
		}
		Row.Name = Kid.Key;
		if (Row.AnimName.IsEmpty())
		{
			Row.AnimName = Kid.Key;
		}
		ApplyOverrides(Kid.Key, *N, Row);
		Rows.Add(MoveTemp(Row));
	}
	if (Rows.IsEmpty())
	{
		OutError = FString::Printf(TEXT("DispositionTable in %s has no rows"), *Source);
		return false;
	}
	return true;
}

const FElysiumDisposition* FElysiumDispositionTable::Resolve(const FString& Disposition, int32 Level) const
{
	if (const FElysiumDisposition* Row = FindAtOrBelow(Rows, Disposition, Level))
	{
		return Row;
	}
	return FindExact(Rows, NeutralName, 1);
}

FString FElysiumDispositionTable::AnimNameFor(const FString& Disposition, int32 Level) const
{
	const FElysiumDisposition* Row = Resolve(Disposition, Level);
	if (Row != nullptr && !Row->AnimName.IsEmpty())
	{
		return Row->AnimName;
	}
	return Disposition.IsEmpty() ? FString(NeutralName) : Disposition;
}
