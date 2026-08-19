#include "Substrate/ElysiumSoundVolumeTable.h"

#include "ElysiumContentPaths.h"
#include "ElysiumKeyValues.h"

#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumVdataLoad.h"

namespace
{
	using ElysiumKeyValues::FKvNode;
	using ElysiumVdata::ReadVdata;
	using ElysiumVdata::RootBlock;
	using ElysiumVdata::Index;
}

// ================================================================================================
// 16. system/sound_volume_table.txt
// ================================================================================================

bool FElysiumSoundVolumeTable::Load(FString& OutError)
{
	Levels.Reset();
	Categories.Reset();
	Misc.Reset();
	LevelByIndex.Reset();
	CategoryByName.Reset();

	static const TCHAR* Rel = TEXT("system/sound_volume_table.txt");
	TSharedPtr<FKvNode> Root;
	if (!ReadVdata(Rel, Root, OutError))
	{
		return false;
	}
	const FKvNode* Data = RootBlock(Root, TEXT("SoundVolumeTable"), Rel, OutError);
	if (Data == nullptr)
	{
		return false;
	}

	const FKvNode* Volumes = Data->Child(TEXT("VolumeLevels"));
	if (Volumes == nullptr)
	{
		OutError = FString::Printf(TEXT("no SoundVolumeTable/VolumeLevels block in %s"),
			*FElysiumContentPaths::VdataFile(Rel));
		return false;
	}
	// The two level blocks are joined BY INDEX rather than by position: `OccludedVolumeLevels`
	// authors the same keys, so a patch that omitted one row would otherwise shift the whole join.
	const FKvNode* Occluded = Data->Child(TEXT("OccludedVolumeLevels"));
	for (const TPair<FString, FString>& Pair : Volumes->Pairs)
	{
		if (!Pair.Key.IsNumeric())
		{
			continue;   // the block holds level indices only; anything else is not a level
		}
		FElysiumSoundLevel Row;
		Row.Level = FCString::Atoi(*Pair.Key);
		Row.RadiusUnits = FCString::Atof(*Pair.Value);
		// An unauthored occlusion row reads as "cannot be occluded", which is the block's own `0`.
		Row.bOccludable = Occluded != nullptr && Occluded->Int(*Pair.Key, 0) != 0;
		Levels.Add(MoveTemp(Row));
	}

	if (const FKvNode* Types = Data->Child(TEXT("SoundTypes")))
	{
		for (const TPair<FString, FString>& Pair : Types->Pairs)
		{
			FElysiumSoundCategory Row;
			Row.Name = Pair.Key;   // already folded by the KV reader
			Row.Level = FCString::Atoi(*Pair.Value);
			Categories.Add(MoveTemp(Row));
		}
	}
	if (const FKvNode* MiscNode = Data->Child(TEXT("MiscData")))
	{
		for (const TPair<FString, FString>& Pair : MiscNode->Pairs)
		{
			Misc.Add(Pair.Key, Pair.Value);
		}
	}
	Reindex();

	if (Categories.IsEmpty())
	{
		OutError = FString::Printf(TEXT("no SoundVolumeTable/SoundTypes rows in %s"),
			*FElysiumContentPaths::VdataFile(Rel));
		return false;
	}
	return true;
}

void FElysiumSoundVolumeTable::Reindex()
{
	LevelByIndex.Reset();
	CategoryByName.Reset();
	for (int32 i = 0; i < Levels.Num(); ++i)
	{
		LevelByIndex.Add(Levels[i].Level, i);
	}
	for (int32 i = 0; i < Categories.Num(); ++i)
	{
		Index(CategoryByName, Categories[i].Name, i);
	}
}

const FElysiumSoundLevel* FElysiumSoundVolumeTable::Level(int32 InIndex) const
{
	const int32* Row = LevelByIndex.Find(InIndex);
	return Row ? &Levels[*Row] : nullptr;
}

const FElysiumSoundLevel* FElysiumSoundVolumeTable::FindCategory(const FString& Category) const
{
	const int32* Row = CategoryByName.Find(ElysiumFold(Category));
	return Row ? Level(Categories[*Row].Level) : nullptr;
}

float FElysiumSoundVolumeTable::MiscFloat(const TCHAR* Tag, float Def) const
{
	const FString* Raw = Misc.Find(ElysiumFold(FString(Tag)));
	return Raw ? FCString::Atof(**Raw) : Def;
}

const FElysiumSoundLevel& FElysiumSoundVolumeTable::NormalFallback()
{
	// LEVEL_2 verbatim: 240 game units, occludable.
	static const FElysiumSoundLevel Row{ NormalLevel, 240.f, true };
	return Row;
}
