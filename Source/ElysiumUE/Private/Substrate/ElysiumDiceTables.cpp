#include "Substrate/ElysiumDiceTables.h"

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
// 12. dicerolls.txt
// ================================================================================================

FElysiumDiceTable::FElysiumDiceTable()
{
	for (int32 i = 0; i < NumEntries; ++i)
	{
		Faces[i] = i / (NumEntries / NumFaces);
	}
}

void FElysiumDiceTable::Load(const FKvNode& Node)
{
	Name = Node.Str(TEXT("Name"), FString());
	for (int32 i = 0; i < NumEntries; ++i)
	{
		Faces[i] = Node.Int(*FString::FromInt(i), 1);
	}
}

int32 FElysiumDiceTable::Face(int32 Draw) const
{
	return Faces[FMath::Clamp(Draw, 0, NumEntries - 1)];
}

bool FElysiumDiceTable::IsUniform() const
{
	for (int32 i = 0; i < NumEntries; ++i)
	{
		if (Faces[i] != i / (NumEntries / NumFaces))
		{
			return false;
		}
	}
	return true;
}

const FElysiumDiceTable& FElysiumDiceTable::Uniform()
{
	static const FElysiumDiceTable Table;
	return Table;
}

bool FElysiumDiceTables::Load(FString& OutError)
{
	Tables.Reset();
	HealthModifiers.Reset();
	ByName.Reset();

	static const TCHAR* Rel = TEXT("system/dicerolls.txt");
	TSharedPtr<FKvNode> Root;
	if (!ReadVdata(Rel, Root, OutError))
	{
		return false;
	}
	const FKvNode* Data = RootBlock(Root, TEXT("DiceRollData"), Rel, OutError);
	const FKvNode* Rules = Data ? Data->Child(TEXT("Rules")) : nullptr;
	if (Rules == nullptr)
	{
		if (OutError.IsEmpty())
		{
			OutError = FString::Printf(TEXT("no DiceRollData/Rules block in %s"),
				*FElysiumContentPaths::VdataFile(Rel));
		}
		return false;
	}

	// Health levels are probed until one is absent, the same rule the `Base%d` list takes: the
	// authored range is data (0..7 today) and a patch may extend it.
	if (const FKvNode* Health = Rules->Child(TEXT("HealthModifiers")))
	{
		for (int32 Level = 0; ; ++Level)
		{
			const FString* Value = Health->Value(*FString::FromInt(Level));
			if (Value == nullptr)
			{
				break;
			}
			HealthModifiers.Add(FCString::Atoi(**Value));
		}
	}

	if (const FKvNode* Weightings = Rules->Child(TEXT("TableWeightings")))
	{
		// Its own `Name` leaf is a value, not a child, so the child list is exactly the tables.
		for (const TPair<FString, TSharedPtr<FKvNode>>& Kid : Weightings->Kids)
		{
			if (!Kid.Value.IsValid())
			{
				continue;
			}
			FElysiumDiceTable Table;
			Table.InternalName = Kid.Key;
			Table.Load(*Kid.Value);
			Tables.Add(MoveTemp(Table));
		}
	}
	Reindex();

	if (Tables.IsEmpty())
	{
		OutError = FString::Printf(TEXT("no TableWeightings in %s"),
			*FElysiumContentPaths::VdataFile(Rel));
		return false;
	}
	return true;
}

void FElysiumDiceTables::Reindex()
{
	ByName.Reset();
	for (int32 i = 0; i < Tables.Num(); ++i)
	{
		Index(ByName, Tables[i].InternalName, i);
	}
}

const FElysiumDiceTable& FElysiumDiceTables::At(int32 Index) const
{
	return Tables.IsValidIndex(Index) ? Tables[Index] : FElysiumDiceTable::Uniform();
}

const FElysiumDiceTable& FElysiumDiceTables::Find(const FString& InName) const
{
	const int32* Idx = ByName.Find(ElysiumFold(InName));
	return At(Idx ? *Idx : 0);
}

const FElysiumDiceTable& FElysiumDiceTables::ForFeat(const FElysiumFeat& Feat, bool bNpc) const
{
	return Find(bNpc ? Feat.NpcWeighting : Feat.PcWeighting);
}

int32 FElysiumDiceTables::HealthModifier(int32 HealthLevel) const
{
	return HealthModifiers.IsValidIndex(HealthLevel) ? HealthModifiers[HealthLevel] : 0;
}
