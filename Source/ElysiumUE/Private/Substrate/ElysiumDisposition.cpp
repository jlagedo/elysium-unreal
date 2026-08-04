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
}

bool FElysiumDispositionTable::Load(FString& OutError)
{
	Rows.Reset();

	const FString Path = FElysiumContentPaths::VdataFile(TEXT("system/dispositiontable.txt"));
	FString Raw;
	if (!FFileHelper::LoadFileToString(Raw, *Path))
	{
		OutError = FString::Printf(TEXT("not found: %s"), *Path);
		return false;
	}
	const TSharedPtr<ElysiumKeyValues::FKvNode> Root = ElysiumKeyValues::ParseText(Raw);
	// One top-level `DispositionTable` block whose children are the dispositions.
	const ElysiumKeyValues::FKvNode* Table = Root.IsValid() ? Root->Child(TEXT("DispositionTable")) : nullptr;
	if (Table == nullptr)
	{
		OutError = FString::Printf(TEXT("no DispositionTable block in %s"), *Path);
		return false;
	}

	for (const TPair<FString, TSharedPtr<ElysiumKeyValues::FKvNode>>& Kid : Table->Kids)
	{
		const ElysiumKeyValues::FKvNode* N = Kid.Value.Get();
		if (N == nullptr)
		{
			continue;
		}
		FElysiumDisposition Row;
		// The KV reader folds keys to lower but keeps values' case, and the block key is the
		// disposition's authored name — which is also the token the stance clips are named for.
		Row.Name = Kid.Key;
		Row.AnimName = N->Str(TEXT("Animation Name"), Kid.Key);
		Row.Level = N->Int(TEXT("DispositionLevel"), 0);
		Row.TalkingStanceChangeThreshold = N->Flt(TEXT("Talking Stance Change Threshold"), 0.f);
		Row.TalkingStanceChangeChance = N->Int(TEXT("Talking Stance Change Chance"), 0);
		Row.StandingFidgetChance = N->Int(TEXT("Standing Fidget Chance"), 0);
		Row.StandingStanceChangeThreshold = N->Flt(TEXT("Standing Stance Change Threshold"), 0.f);
		// Authored with a lowercase `stance` in the middle on every row — the KV reader folds keys
		// to lower, so this reads as spelled either way.
		Row.StandingStanceChangeChance = N->Int(TEXT("Standing stance Change Chance"), 0);
		// Authored under two spellings, and reading only one is a silent content loss rather than a
		// parse failure: four rows space the words and six run them together, and two of the six carry
		// the only cadences in the file that are not 2.5/6.0 — `Error`, the row a character falls to
		// when its disposition does not resolve, blinks at 1.5/2.0.
		Row.MinBlinkInterval = N->Flt(TEXT("Min Blink Interval"), N->Flt(TEXT("MinBlinkInterval"), 2.5f));
		Row.MaxBlinkInterval = N->Flt(TEXT("Max Blink Interval"), N->Flt(TEXT("MaxBlinkInterval"), 6.f));
		// The disposition-level "Eye Turn Rate", which is a different key from the one inside the
		// `EyeTarget` block below and carries a different value on every row that authors both.
		Row.EyeTurnRate = N->Flt(TEXT("Eye Turn Rate"), 0.9f);
		if (const ElysiumKeyValues::FKvNode* Eye = N->Child(TEXT("EyeTarget")))
		{
			Row.EyeTarget.DefaultDirection = Eye->Int(TEXT("Default Direction"), 0);
			ParseFidgetPoints(Eye->Str(TEXT("Fidget Points"), FString()), Row.EyeTarget.FidgetPoints);
			Row.EyeTarget.MinInterval = Eye->Flt(TEXT("Min Interval"), 5.f);
			Row.EyeTarget.MaxInterval = Eye->Flt(TEXT("Max Interval"), 8.f);
			Row.EyeTarget.HoldMin = Eye->Flt(TEXT("Hold Min"), 0.15f);
			Row.EyeTarget.HoldMax = Eye->Flt(TEXT("Hold Max"), 0.25f);
			Row.EyeTarget.TurnRate = Eye->Flt(TEXT("Eye Turn Rate"), 0.3f);
		}
		Rows.Add(Kid.Key, MoveTemp(Row));
	}
	if (Rows.IsEmpty())
	{
		OutError = FString::Printf(TEXT("DispositionTable in %s has no rows"), *Path);
		return false;
	}
	return true;
}

const FElysiumDisposition* FElysiumDispositionTable::Resolve(const FString& Disposition) const
{
	// The KV reader lowercased the block keys, and the maps spell the keyfield with mixed case
	// (`Neutral`, `fear`, `Damaged`), so both ends fold before comparing.
	if (const FElysiumDisposition* Row = Rows.Find(Disposition.ToLower()))
	{
		return Row;
	}
	return Rows.Find(FString(NeutralName).ToLower());
}

FString FElysiumDispositionTable::AnimNameFor(const FString& Disposition) const
{
	const FElysiumDisposition* Row = Resolve(Disposition);
	if (Row != nullptr && !Row->AnimName.IsEmpty())
	{
		return Row->AnimName;
	}
	return Disposition.IsEmpty() ? FString(NeutralName) : Disposition;
}
