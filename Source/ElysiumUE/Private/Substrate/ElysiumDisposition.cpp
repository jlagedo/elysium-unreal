#include "Substrate/ElysiumDisposition.h"

#include "ElysiumContentPaths.h"
#include "ElysiumKeyValues.h"

#include "Misc/FileHelper.h"

const TCHAR* FElysiumDispositionTable::NeutralName = TEXT("Neutral");

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
